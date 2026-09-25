package resolver

import (
	"context"
	"testing"

	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
)

const testDatabaseURL = "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"

func TestResolveDevice_Found(t *testing.T) {
	pool, err := database.NewPool(context.Background(), testDatabaseURL)
	if err != nil {
		t.Fatalf("failed to connect to test database: %v", err)
	}
	t.Cleanup(pool.Close)
	q := sqlc.New(pool)

	ctx := context.Background()
	merchantID := "RESOLVER-TEST-MERCHANT"
	deviceID := "RESOLVER-TEST-DEVICE"

	_, err = pool.Exec(ctx, `
		INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
		VALUES ($1, 'test-client-id', 'TEST_PRIVATE_KEY_REF', 'TEST_CLIENT_SECRET_REF', $1, '05')
		ON CONFLICT (merchant_id) DO NOTHING`, merchantID)
	if err != nil {
		t.Fatalf("failed to insert test merchant: %v", err)
	}
	_, err = pool.Exec(ctx, `
		INSERT INTO devices (device_id, merchant_id, mqtt_topic)
		VALUES ($1, $2, $3)
		ON CONFLICT (device_id) DO NOTHING`, deviceID, merchantID, "topic/"+merchantID+"/_/"+deviceID)
	if err != nil {
		t.Fatalf("failed to insert test device: %v", err)
	}
	t.Cleanup(func() {
		pool.Exec(context.Background(), `DELETE FROM devices WHERE device_id = $1`, deviceID)
		pool.Exec(context.Background(), `DELETE FROM merchants WHERE merchant_id = $1`, merchantID)
	})

	r := New(q)
	got, err := r.ResolveDevice(ctx, deviceID)
	if err != nil {
		t.Fatalf("ResolveDevice() error = %v", err)
	}
	if got.MerchantID != merchantID {
		t.Errorf("MerchantID = %q, want %q", got.MerchantID, merchantID)
	}
	if got.TenantID != nil {
		t.Errorf("TenantID = %v, want nil (device tanpa tenant)", got.TenantID)
	}
	if !got.DeviceActive || !got.MerchantActive {
		t.Error("DeviceActive/MerchantActive = false, want true")
	}
}

func TestResolveDevice_NotFound(t *testing.T) {
	pool, err := database.NewPool(context.Background(), testDatabaseURL)
	if err != nil {
		t.Fatalf("failed to connect to test database: %v", err)
	}
	t.Cleanup(pool.Close)
	q := sqlc.New(pool)

	r := New(q)
	_, err = r.ResolveDevice(context.Background(), "DOES-NOT-EXIST-DEVICE-ID")
	if err == nil {
		t.Fatal("ResolveDevice() expected error for unknown device, got nil")
	}
}
