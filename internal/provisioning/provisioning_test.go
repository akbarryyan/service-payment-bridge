package provisioning

import (
	"context"
	"errors"
	"fmt"
	"strings"
	"testing"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/jackc/pgx/v5/pgxpool"

	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/dynsec"
	"service-payment-bridge/internal/mqttclient"
	"service-payment-bridge/internal/qrtopic"
	"service-payment-bridge/internal/testbroker"
)

const testDatabaseURL = "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"

func TestGeneratePassword(t *testing.T) {
	a, err := GeneratePassword()
	if err != nil {
		t.Fatal(err)
	}
	b, _ := GeneratePassword()
	if len(a) != 24 || a == b {
		t.Errorf("passwords %q / %q: want 24 chars and different each time", a, b)
	}
	for _, r := range a {
		if !strings.ContainsRune(passwordAlphabet, r) {
			t.Errorf("password contains %q, outside the alphanumeric alphabet", r)
		}
	}
}

func TestRenderConfig(t *testing.T) {
	got := string(RenderConfig("192.168.137.1", "18883", "MT58530503", "MT58530503-00078020709", "secret"))
	want := "server=192.168.137.1\nport=18883\nssl=1\nmerchant=MT58530503\nuser=MT58530503-00078020709\npass=secret\n"
	if got != want {
		t.Errorf("RenderConfig()\n got: %q\nwant: %q", got, want)
	}
}

func TestValidateID(t *testing.T) {
	for _, ok := range []string{"MT58530503", "00078020709"} {
		if err := validateID("id", ok, 31); err != nil {
			t.Errorf("validateID(%q) = %v, want nil", ok, err)
		}
	}
	for _, bad := range []string{"", "a/b", "a+b", "a#b", "a-b", "a b", strings.Repeat("9", 32)} {
		if err := validateID("id", bad, 31); err == nil {
			t.Errorf("validateID(%q) = nil, want error", bad)
		}
	}
}

type fixture struct {
	pool   *pgxpool.Pool
	q      *sqlc.Queries
	broker *dynsec.Client
	p      *Provisioner
}

// Two test merchants: A (the one devices are provisioned under) and B (to prove a device
// can't be moved between merchants).
const (
	merchantA, manjoA = "PROV-TEST-MERCHANT-A", "PROVTESTA"
	merchantB, manjoB = "PROV-TEST-MERCHANT-B", "PROVTESTB"
)

func setup(t *testing.T) *fixture {
	t.Helper()
	ctx := context.Background()
	pool, err := database.NewPool(ctx, testDatabaseURL)
	if err != nil {
		t.Fatalf("connect DB: %v", err)
	}
	t.Cleanup(pool.Close)

	conn, err := mqttclient.Connect(testbroker.URL(), testbroker.Username(), testbroker.Password())
	if err != nil {
		t.Fatalf("connect broker (scripts/broker-bootstrap.sh --dev): %v", err)
	}
	t.Cleanup(conn.Disconnect)
	broker, err := dynsec.New(conn)
	if err != nil {
		t.Fatal(err)
	}

	for _, m := range [][2]string{{merchantA, manjoA}, {merchantB, manjoB}} {
		if _, err := pool.Exec(ctx, `
			INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
			VALUES ($1, 'prov-client', 'PROV_KEY_REF', 'PROV_SECRET_REF', $2, '05') ON CONFLICT (merchant_id) DO NOTHING`, m[0], m[1]); err != nil {
			t.Fatalf("seed merchant: %v", err)
		}
	}

	q := sqlc.New(pool)
	f := &fixture{pool: pool, q: q, broker: broker, p: New(q, broker, Config{DeviceServer: "192.168.137.1", DevicePort: "18883"})}
	t.Cleanup(func() {
		ctx := context.Background()
		rows, _ := pool.Query(ctx, `SELECT d.device_id, m.manjo_merchant_id FROM devices d JOIN merchants m USING (merchant_id) WHERE d.merchant_id IN ($1, $2)`, merchantA, merchantB)
		var gone [][2]string
		for rows.Next() {
			var sn, m string
			rows.Scan(&sn, &m)
			gone = append(gone, [2]string{m, sn})
		}
		rows.Close()
		for _, d := range gone {
			broker.DeleteClient(ctx, Username(d[0], d[1]))
			broker.DeleteRole(ctx, RoleName(d[0], d[1]))
		}
		pool.Exec(ctx, `DELETE FROM devices WHERE merchant_id IN ($1, $2)`, merchantA, merchantB)
		pool.Exec(ctx, `DELETE FROM merchants WHERE merchant_id IN ($1, $2)`, merchantA, merchantB)
	})
	return f
}

func testSN() string { return fmt.Sprintf("PROVSN%d", time.Now().UnixNano()%1_000_000_000_000) }

func passwordFrom(cfg []byte) string {
	for _, line := range strings.Split(string(cfg), "\n") {
		if strings.HasPrefix(line, "pass=") {
			return strings.TrimPrefix(line, "pass=")
		}
	}
	return ""
}

func canLogin(username, password string) bool {
	conn, err := mqttclient.Connect(testbroker.URL(), username, password)
	if err != nil {
		return false
	}
	conn.Disconnect()
	return true
}

// assertDeviceACL proves the device role of spec Section 4 on the real broker: the device
// receives on its own topic, hears nothing from another device's topic, and cannot publish to
// its own receive topic (the forged-payment attack).
func assertDeviceACL(t *testing.T, username, password, merchantID, sn string) {
	t.Helper()
	dev, err := mqttclient.Connect(testbroker.URL(), username, password)
	if err != nil {
		t.Fatalf("device login: %v", err)
	}
	defer dev.Disconnect()
	admin, err := mqttclient.Connect(testbroker.URL(), testbroker.Username(), testbroker.Password())
	if err != nil {
		t.Fatal(err)
	}
	defer admin.Disconnect()

	own, other := qrtopic.DeviceTopic(merchantID, sn), qrtopic.DeviceTopic(merchantID, "OTHER"+sn)
	collect := func(ch chan string) mqtt.MessageHandler {
		return func(_ mqtt.Client, m mqtt.Message) { ch <- m.Topic() + "|" + string(m.Payload()) }
	}
	next := func(ch chan string, wait time.Duration) string {
		select {
		case s := <-ch:
			return s
		case <-time.After(wait):
			return ""
		}
	}

	devGot := make(chan string, 4)
	if err := dev.Subscribe(own, collect(devGot)); err != nil {
		t.Fatalf("device subscribe own topic: %v", err)
	}
	// Denied by the broker (SUBACK 0x80). Whether paho surfaces that as an error varies, so
	// only the delivery check below counts.
	_ = dev.Subscribe(other, collect(devGot))

	admin.Publish(other, []byte("to-other"))
	admin.Publish(own, []byte("to-own"))
	if got := next(devGot, 3*time.Second); got != own+"|to-own" {
		t.Errorf("device received %q first, want %q", got, own+"|to-own")
	}
	if got := next(devGot, time.Second); got != "" {
		t.Errorf("device received %q from a topic it must not hear", got)
	}

	adminGot := make(chan string, 4)
	if err := admin.Subscribe(own, collect(adminGot)); err != nil {
		t.Fatal(err)
	}
	dev.Publish(own, []byte("forged")) // dropped silently by the broker
	time.Sleep(time.Second)
	admin.Publish(own, []byte("marker"))
	if got := next(adminGot, 3*time.Second); got != own+"|marker" {
		t.Errorf("first message on %s = %q, want the marker (the device's forged publish must be dropped)", own, got)
	}
}

func deviceStatus(t *testing.T, f *fixture, sn string) sqlc.MerchantStatus {
	t.Helper()
	d, err := f.q.GetDevice(context.Background(), sn)
	if err != nil {
		t.Fatalf("GetDevice(%s): %v", sn, err)
	}
	return d.Status
}

func TestAddRotateRevoke(t *testing.T) {
	ctx := context.Background()
	f := setup(t)
	sn := testSN()
	user := Username(manjoA, sn)

	first, err := f.p.Add(ctx, AddRequest{ManjoMerchantID: manjoA, SN: sn})
	if err != nil {
		t.Fatalf("Add() error = %v", err)
	}
	if !first.Created || first.Username != user || first.Topic != "topic/"+manjoA+"/"+sn || first.RequestTopic != "qris/request/"+manjoA+"/"+sn {
		t.Errorf("Add() = %+v", first)
	}
	if !strings.Contains(string(first.ConfigFile), "user="+user+"\n") {
		t.Errorf("config file lacks user line: %q", first.ConfigFile)
	}
	firstPass := passwordFrom(first.ConfigFile)
	if !canLogin(user, firstPass) {
		t.Fatal("provisioned device cannot log in")
	}
	assertDeviceACL(t, user, firstPass, manjoA, sn)
	if d, _ := f.q.GetDevice(ctx, sn); d.MqttTopic != first.Topic || d.Status != sqlc.MerchantStatusACTIVE {
		t.Errorf("device row = %+v", d)
	}
	roles, err := f.broker.ClientRoles(ctx, user)
	if err != nil || len(roles) != 1 || roles[0] != RoleName(manjoA, sn) {
		t.Errorf("ClientRoles() = %v, %v", roles, err)
	}

	second, err := f.p.Add(ctx, AddRequest{ManjoMerchantID: manjoA, SN: sn})
	if err != nil || second.Created {
		t.Fatalf("second Add() = %+v, %v; want Created=false", second, err)
	}
	if canLogin(user, firstPass) || !canLogin(user, passwordFrom(second.ConfigFile)) {
		t.Error("re-running Add did not rotate the password")
	}

	missing, err := f.p.Revoke(ctx, sn, false)
	if err != nil || missing {
		t.Fatalf("Revoke() = %v, %v", missing, err)
	}
	if canLogin(user, passwordFrom(second.ConfigFile)) {
		t.Error("revoked device can still log in")
	}
	if s := deviceStatus(t, f, sn); s != sqlc.MerchantStatusINACTIVE {
		t.Errorf("status after Revoke = %s, want INACTIVE", s)
	}

	third, err := f.p.Add(ctx, AddRequest{ManjoMerchantID: manjoA, SN: sn})
	if err != nil {
		t.Fatalf("Add() after Revoke error = %v", err)
	}
	if !canLogin(user, passwordFrom(third.ConfigFile)) || deviceStatus(t, f, sn) != sqlc.MerchantStatusACTIVE {
		t.Error("Add after Revoke did not reactivate the device")
	}

	if _, err := f.p.Revoke(ctx, sn, true); err != nil {
		t.Fatalf("Revoke(remove) error = %v", err)
	}
	if _, err := f.broker.ClientRoles(ctx, user); !errors.Is(err, dynsec.ErrNotFound) {
		t.Errorf("client still exists after Revoke(remove): %v", err)
	}
}

func TestAddRejects(t *testing.T) {
	ctx := context.Background()
	f := setup(t)
	sn := testSN()

	if _, err := f.p.Add(ctx, AddRequest{ManjoMerchantID: "NOSUCHMERCHANT", SN: sn}); err == nil {
		t.Error("Add() for an unknown merchant succeeded")
	}
	if _, err := f.p.Add(ctx, AddRequest{ManjoMerchantID: manjoA, SN: "bad/sn"}); err == nil {
		t.Error("Add() with an SN containing '/' succeeded")
	}
	if _, err := f.p.Add(ctx, AddRequest{ManjoMerchantID: manjoA, SN: sn}); err != nil {
		t.Fatalf("Add() error = %v", err)
	}
	if _, err := f.p.Add(ctx, AddRequest{ManjoMerchantID: manjoB, SN: sn}); err == nil {
		t.Error("Add() moved a device to another merchant")
	}
}

func TestRevokeDeviceWithoutBrokerAccount(t *testing.T) {
	ctx := context.Background()
	f := setup(t)
	sn := testSN()
	if _, err := f.pool.Exec(ctx, `INSERT INTO devices (device_id, merchant_id, mqtt_topic) VALUES ($1, $2, $3)`, sn, merchantA, "legacy_"+sn); err != nil {
		t.Fatal(err)
	}

	missing, err := f.p.Revoke(ctx, sn, false)
	if err != nil || !missing {
		t.Fatalf("Revoke() = %v, %v; want brokerAccountMissing=true", missing, err)
	}
	if s := deviceStatus(t, f, sn); s != sqlc.MerchantStatusINACTIVE {
		t.Errorf("status = %s, want INACTIVE", s)
	}
}
