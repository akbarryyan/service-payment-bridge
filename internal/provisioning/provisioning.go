// Package provisioning registers Q161 Pro devices: the devices row, the device's own Mosquitto
// Dynamic Security login and role, and the mqttcfg.dat the firmware reads at boot.
package provisioning

import (
	"context"
	"crypto/rand"
	"errors"
	"fmt"
	"math/big"
	"slices"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgtype"

	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/dynsec"
	"service-payment-bridge/internal/qrtopic"
	"service-payment-bridge/internal/resolver"
)

const (
	passwordLength   = 24
	passwordAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"

	// Firmware buffers: the merchant ID fits 25 chars, the SN 31 (see firmware def.h).
	maxMerchantIDLen = 25
	maxSNLen         = 31
)

// Config is what the device needs to reach the broker's TLS listener.
type Config struct {
	DeviceServer string
	DevicePort   string
}

// AddRequest registers SN under the merchant whose Manjo merchant ID is ManjoMerchantID.
// TenantID, StoreID and TerminalID are optional.
type AddRequest struct {
	ManjoMerchantID string
	SN              string
	TenantID        string
	StoreID         string
	TerminalID      string
}

// AddResult describes the provisioned device. ConfigFile contains the device password.
type AddResult struct {
	Username     string
	Topic        string
	RequestTopic string
	Created      bool // false: the device already existed and its password was rotated
	ConfigFile   []byte
}

// Provisioner registers and revokes devices.
type Provisioner struct {
	q      *sqlc.Queries
	broker *dynsec.Client
	cfg    Config
}

// New creates a Provisioner.
func New(q *sqlc.Queries, broker *dynsec.Client, cfg Config) *Provisioner {
	return &Provisioner{q: q, broker: broker, cfg: cfg}
}

// Username is the device's MQTT login.
func Username(merchantID, sn string) string { return merchantID + "-" + sn }

// RoleName is the device's own Dynamic Security role.
func RoleName(merchantID, sn string) string { return "device-" + merchantID + "-" + sn }

// Add registers the device, or — when it already exists under the same merchant — reactivates
// it and rotates its password. Safe to re-run after a partial failure.
func (p *Provisioner) Add(ctx context.Context, req AddRequest) (*AddResult, error) {
	m, sn := req.ManjoMerchantID, req.SN
	if err := validateID("merchant", m, maxMerchantIDLen); err != nil {
		return nil, err
	}
	if err := validateID("sn", sn, maxSNLen); err != nil {
		return nil, err
	}

	merchants, err := p.q.ListMerchantsByManjoMerchantID(ctx, m)
	if err != nil {
		return nil, fmt.Errorf("provisioning: look up merchant %s: %w", m, err)
	}
	switch len(merchants) {
	case 0:
		return nil, fmt.Errorf("provisioning: no merchant with manjo_merchant_id %s — register the merchant first", m)
	case 1:
	default:
		return nil, fmt.Errorf("provisioning: %d merchants share manjo_merchant_id %s — resolve that in the merchants table first", len(merchants), m)
	}
	merchant := merchants[0]

	topic, requestTopic := qrtopic.DeviceTopic(m, sn), qrtopic.DeviceRequestTopic(m, sn)
	created, err := p.upsertDevice(ctx, merchant.MerchantID, topic, req)
	if err != nil {
		return nil, err
	}

	password, err := GeneratePassword()
	if err != nil {
		return nil, err
	}
	username, role := Username(m, sn), RoleName(m, sn)
	if err := p.ensureRole(ctx, role, topic, requestTopic); err != nil {
		return nil, err
	}
	if err := p.ensureClient(ctx, username, password, role); err != nil {
		return nil, err
	}

	return &AddResult{
		Username:     username,
		Topic:        topic,
		RequestTopic: requestTopic,
		Created:      created,
		ConfigFile:   RenderConfig(p.cfg.DeviceServer, p.cfg.DevicePort, m, username, password),
	}, nil
}

func (p *Provisioner) upsertDevice(ctx context.Context, merchantID, topic string, req AddRequest) (created bool, err error) {
	existing, err := p.q.GetDevice(ctx, req.SN)
	switch {
	case errors.Is(err, pgx.ErrNoRows):
		_, err = p.q.CreateDevice(ctx, sqlc.CreateDeviceParams{
			DeviceID:        req.SN,
			MerchantID:      merchantID,
			TenantID:        optionalText(req.TenantID),
			MqttTopic:       topic,
			ManjoStoreID:    optionalText(req.StoreID),
			ManjoTerminalID: optionalText(req.TerminalID),
		})
		if err != nil {
			return false, fmt.Errorf("provisioning: create device %s: %w", req.SN, err)
		}
		return true, nil
	case err != nil:
		return false, fmt.Errorf("provisioning: look up device %s: %w", req.SN, err)
	case existing.MerchantID != merchantID:
		return false, fmt.Errorf("provisioning: device %s is registered to merchant %s, not %s", req.SN, existing.MerchantID, merchantID)
	default:
		if err := p.q.SetDeviceStatus(ctx, sqlc.SetDeviceStatusParams{DeviceID: req.SN, Status: sqlc.MerchantStatusACTIVE}); err != nil {
			return false, fmt.Errorf("provisioning: reactivate device %s: %w", req.SN, err)
		}
		return false, nil
	}
}

// ensureRole creates the device role with exactly the three ACLs of the spec (Section 4).
func (p *Provisioner) ensureRole(ctx context.Context, role, topic, requestTopic string) error {
	if err := ignore(p.broker.CreateRole(ctx, role), dynsec.ErrAlreadyExists); err != nil {
		return fmt.Errorf("provisioning: create role %s: %w", role, err)
	}
	acls := []struct{ aclType, topic string }{
		{dynsec.ACLSubscribeLiteral, topic},
		{dynsec.ACLPublishClientReceive, topic},
		{dynsec.ACLPublishClientSend, requestTopic},
	}
	for _, acl := range acls {
		if err := ignore(p.broker.AddRoleACL(ctx, role, acl.aclType, acl.topic), dynsec.ErrAlreadyExists); err != nil {
			return fmt.Errorf("provisioning: add %s %s to %s: %w", acl.aclType, acl.topic, role, err)
		}
	}
	return nil
}

// ensureClient creates the login, or resets the password, re-enables it and restores its role.
func (p *Provisioner) ensureClient(ctx context.Context, username, password, role string) error {
	err := p.broker.CreateClient(ctx, username, password, role)
	if !errors.Is(err, dynsec.ErrAlreadyExists) {
		if err != nil {
			return fmt.Errorf("provisioning: create client %s: %w", username, err)
		}
		return nil
	}
	if err := p.broker.SetClientPassword(ctx, username, password); err != nil {
		return fmt.Errorf("provisioning: rotate password of %s: %w", username, err)
	}
	if err := p.broker.EnableClient(ctx, username); err != nil {
		return fmt.Errorf("provisioning: enable %s: %w", username, err)
	}
	roles, err := p.broker.ClientRoles(ctx, username)
	if err != nil {
		return fmt.Errorf("provisioning: read roles of %s: %w", username, err)
	}
	// addClientRole on a role the client already has answers "Internal error", so only add
	// it when missing.
	if !slices.Contains(roles, role) {
		if err := p.broker.AddClientRole(ctx, username, role); err != nil {
			return fmt.Errorf("provisioning: give %s role %s: %w", username, role, err)
		}
	}
	return nil
}

// Revoke disables the device's login (remove also deletes its client and role) and marks the
// device INACTIVE. brokerAccountMissing reports a device that never had a broker login (e.g.
// registered before per-device accounts existed); the DB row is still deactivated.
func (p *Provisioner) Revoke(ctx context.Context, sn string, remove bool) (brokerAccountMissing bool, err error) {
	device, err := resolver.New(p.q).ResolveDevice(ctx, sn)
	if err != nil {
		return false, fmt.Errorf("provisioning: %w", err)
	}
	username, role := Username(device.ManjoMerchantID, sn), RoleName(device.ManjoMerchantID, sn)

	switch err := p.broker.DisableClient(ctx, username); {
	case errors.Is(err, dynsec.ErrNotFound):
		brokerAccountMissing = true
	case err != nil:
		return false, fmt.Errorf("provisioning: disable %s: %w", username, err)
	}
	if remove {
		if err := ignore(p.broker.DeleteClient(ctx, username), dynsec.ErrNotFound); err != nil {
			return false, fmt.Errorf("provisioning: delete client %s: %w", username, err)
		}
		if err := ignore(p.broker.DeleteRole(ctx, role), dynsec.ErrNotFound); err != nil {
			return false, fmt.Errorf("provisioning: delete role %s: %w", role, err)
		}
	}

	if err := p.q.SetDeviceStatus(ctx, sqlc.SetDeviceStatusParams{DeviceID: sn, Status: sqlc.MerchantStatusINACTIVE}); err != nil {
		return false, fmt.Errorf("provisioning: deactivate device %s: %w", sn, err)
	}
	return brokerAccountMissing, nil
}

// GeneratePassword returns 24 alphanumeric characters from crypto/rand, without modulo bias.
func GeneratePassword() (string, error) {
	out := make([]byte, passwordLength)
	max := big.NewInt(int64(len(passwordAlphabet)))
	for i := range out {
		n, err := rand.Int(rand.Reader, max)
		if err != nil {
			return "", fmt.Errorf("provisioning: generate password: %w", err)
		}
		out[i] = passwordAlphabet[n.Int64()]
	}
	return string(out), nil
}

// RenderConfig produces mqttcfg.dat in the Q181 SE format the firmware parses: one key=value
// per line; unknown keys are ignored by the device.
func RenderConfig(server, port, merchantID, username, password string) []byte {
	return []byte("server=" + server + "\n" +
		"port=" + port + "\n" +
		"ssl=1\n" +
		"merchant=" + merchantID + "\n" +
		"user=" + username + "\n" +
		"pass=" + password + "\n")
}

// validateID keeps IDs safe inside MQTT topics ('/', '+', '#'), usernames ('-' separates
// merchant and SN) and the firmware's fixed-size buffers.
func validateID(field, value string, maxLen int) error {
	if value == "" || len(value) > maxLen {
		return fmt.Errorf("provisioning: %s must be 1-%d characters, got %q", field, maxLen, value)
	}
	for _, r := range value {
		if !(r >= 'A' && r <= 'Z' || r >= 'a' && r <= 'z' || r >= '0' && r <= '9') {
			return fmt.Errorf("provisioning: %s must be alphanumeric, got %q", field, value)
		}
	}
	return nil
}

func optionalText(v string) pgtype.Text {
	return pgtype.Text{String: v, Valid: v != ""}
}

func ignore(err, allowed error) error {
	if errors.Is(err, allowed) {
		return nil
	}
	return err
}
