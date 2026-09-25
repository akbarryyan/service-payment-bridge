package resolver

import (
	"context"
	"fmt"

	"service-payment-bridge/internal/database/sqlc"
)

type ResolvedDevice struct {
	DeviceID             string
	MerchantID           string
	TenantID             *string
	MQTTTopic            string
	ManjoClientID        string
	ManjoPrivateKeyRef   string
	ManjoClientSecretRef string
	ManjoMerchantID      string
	ManjoChannelID       string
	ManjoStoreID         *string
	ManjoTerminalID      *string
	ManjoSubMerchantID   *string
	DeviceActive         bool
	MerchantActive       bool
	TenantActive         bool // true kalau tidak ada tenant sama sekali (tidak relevan)
}

type Resolver struct {
	q *sqlc.Queries
}

func New(q *sqlc.Queries) *Resolver {
	return &Resolver{q: q}
}

func (r *Resolver) ResolveDevice(ctx context.Context, deviceID string) (*ResolvedDevice, error) {
	row, err := r.q.GetDeviceWithMerchantAndTenant(ctx, deviceID)
	if err != nil {
		return nil, fmt.Errorf("resolver: device %q not found: %w", deviceID, err)
	}

	result := &ResolvedDevice{
		DeviceID:             row.DeviceID,
		MerchantID:           row.MerchantID,
		MQTTTopic:            row.MqttTopic,
		ManjoClientID:        row.ManjoClientID,
		ManjoPrivateKeyRef:   row.ManjoPrivateKeyRef,
		ManjoClientSecretRef: row.ManjoClientSecretRef,
		ManjoMerchantID:      row.ManjoMerchantID,
		ManjoChannelID:       row.ManjoChannelID,
		DeviceActive:         row.DeviceStatus == sqlc.MerchantStatusACTIVE,
		MerchantActive:       row.MerchantActiveStatus == sqlc.MerchantStatusACTIVE,
		TenantActive:         true,
	}

	if row.TenantID.Valid {
		v := row.TenantID.String
		result.TenantID = &v
	}
	if row.ManjoStoreID.Valid {
		v := row.ManjoStoreID.String
		result.ManjoStoreID = &v
	}
	if row.ManjoTerminalID.Valid {
		v := row.ManjoTerminalID.String
		result.ManjoTerminalID = &v
	}
	if row.ManjoSubMerchantID.Valid {
		v := row.ManjoSubMerchantID.String
		result.ManjoSubMerchantID = &v
	}
	if result.TenantID != nil {
		result.TenantActive = row.TenantActiveStatus.Valid && row.TenantActiveStatus.MerchantStatus == sqlc.MerchantStatusACTIVE
	}

	return result, nil
}
