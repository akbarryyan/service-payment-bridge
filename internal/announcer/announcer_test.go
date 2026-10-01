package announcer

import (
	"context"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"sync"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgtype"
	"github.com/jackc/pgx/v5/pgxpool"

	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
)

const testDatabaseURL = "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"

const (
	testMerchantID = "ANN-TEST-MERCHANT"
	testDeviceID   = "ANN-TEST-DEVICE"
)

const rp50000Payload = "/ext/awal-qris.mp3+/ext/lima.mp3+/ext/puluh.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3"

type fakePublisher struct {
	mu           sync.Mutex
	failuresLeft int
	topics       []string
	payloads     []string
}

func (f *fakePublisher) Publish(topic string, payload []byte) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.topics = append(f.topics, topic)
	f.payloads = append(f.payloads, string(payload))
	if f.failuresLeft > 0 {
		f.failuresLeft--
		return errors.New("broker unreachable")
	}
	return nil
}

// setup returns an Announcer over a fake publisher and a PAID transaction to announce
// (mqtt_messages.transaction_id has a foreign key to transactions).
func setup(t *testing.T, failures int) (*Announcer, *fakePublisher, *pgxpool.Pool, string) {
	t.Helper()
	ctx := context.Background()

	pool, err := database.NewPool(ctx, testDatabaseURL)
	if err != nil {
		t.Fatalf("connect test database: %v", err)
	}
	t.Cleanup(pool.Close)

	txID := fmt.Sprintf("ANN-TEST-%d", time.Now().UnixNano())
	seed := []string{
		`INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
		 VALUES ('` + testMerchantID + `', 'ann-client', 'ANN_KEY_REF', 'ANN_SECRET_REF', '` + testMerchantID + `', '05') ON CONFLICT (merchant_id) DO NOTHING`,
		`INSERT INTO devices (device_id, merchant_id, mqtt_topic)
		 VALUES ('` + testDeviceID + `', '` + testMerchantID + `', 'topic_` + testDeviceID + `') ON CONFLICT (device_id) DO NOTHING`,
	}
	for _, stmt := range seed {
		if _, err := pool.Exec(ctx, stmt); err != nil {
			t.Fatalf("seed: %v", err)
		}
	}
	if _, err := pool.Exec(ctx,
		`INSERT INTO transactions (transaction_id, merchant_id, device_id, amount, status) VALUES ($1, $2, $3, 50000, 'PAID')`,
		txID, testMerchantID, testDeviceID); err != nil {
		t.Fatalf("seed transaction: %v", err)
	}
	t.Cleanup(func() {
		ctx := context.Background()
		pool.Exec(ctx, `DELETE FROM mqtt_messages WHERE transaction_id IN (SELECT transaction_id FROM transactions WHERE merchant_id = $1)`, testMerchantID)
		pool.Exec(ctx, `DELETE FROM transactions WHERE merchant_id = $1`, testMerchantID)
		pool.Exec(ctx, `DELETE FROM devices WHERE device_id = $1`, testDeviceID)
		pool.Exec(ctx, `DELETE FROM merchants WHERE merchant_id = $1`, testMerchantID)
	})

	pub := &fakePublisher{failuresLeft: failures}
	a := New(pub, sqlc.New(pool), slog.New(slog.NewTextHandler(io.Discard, nil)))
	a.retryDelay = 0
	return a, pub, pool, txID
}

func outboundRow(t *testing.T, pool *pgxpool.Pool, txID string) (status, payload string, errMsg pgtype.Text) {
	t.Helper()
	err := pool.QueryRow(context.Background(),
		`SELECT status, payload, error_message FROM mqtt_messages WHERE transaction_id = $1 AND direction = 'OUTBOUND'`, txID).
		Scan(&status, &payload, &errMsg)
	if err != nil {
		t.Fatalf("read mqtt_messages row: %v", err)
	}
	return status, payload, errMsg
}

func TestAnnounce_PublishesAudioForAmount(t *testing.T) {
	a, pub, pool, txID := setup(t, 0)

	if err := a.Announce(context.Background(), Request{TransactionID: txID, MerchantID: testMerchantID, DeviceID: testDeviceID, Rupiah: 50000}); err != nil {
		t.Fatalf("Announce() error = %v", err)
	}
	if len(pub.topics) != 1 || pub.topics[0] != "topic/"+testMerchantID+"/"+testDeviceID || pub.payloads[0] != rp50000Payload {
		t.Fatalf("published %v %v, want one %q to topic/%s/%s", pub.topics, pub.payloads, rp50000Payload, testMerchantID, testDeviceID)
	}
	if status, payload, _ := outboundRow(t, pool, txID); status != "PROCESSED" || payload != rp50000Payload {
		t.Errorf("mqtt_messages = %s %q, want PROCESSED with the payload", status, payload)
	}
}

func TestAnnounce_RetriesTransientFailures(t *testing.T) {
	a, pub, pool, txID := setup(t, 2)

	if err := a.Announce(context.Background(), Request{TransactionID: txID, MerchantID: testMerchantID, DeviceID: testDeviceID, Rupiah: 50000}); err != nil {
		t.Fatalf("Announce() error = %v", err)
	}
	if len(pub.topics) != 3 {
		t.Errorf("publish attempts = %d, want 3", len(pub.topics))
	}
	if status, _, _ := outboundRow(t, pool, txID); status != "PROCESSED" {
		t.Errorf("mqtt_messages status = %s, want PROCESSED", status)
	}
}

func TestAnnounce_GivesUpAfterThreeAttempts(t *testing.T) {
	a, pub, pool, txID := setup(t, 5)

	if err := a.Announce(context.Background(), Request{TransactionID: txID, MerchantID: testMerchantID, DeviceID: testDeviceID, Rupiah: 50000}); err == nil {
		t.Fatal("Announce() error = nil, want error after exhausting retries")
	}
	if len(pub.topics) != 3 {
		t.Errorf("publish attempts = %d, want 3", len(pub.topics))
	}
	if status, _, errMsg := outboundRow(t, pool, txID); status != "FAILED" || errMsg.String == "" {
		t.Errorf("mqtt_messages = %s %q, want FAILED with an error message", status, errMsg.String)
	}
}

func TestAnnounce_RejectsInvalidAmount(t *testing.T) {
	a, pub, _, txID := setup(t, 0)

	if err := a.Announce(context.Background(), Request{TransactionID: txID, MerchantID: testMerchantID, DeviceID: testDeviceID, Rupiah: 0}); err == nil {
		t.Fatal("Announce() error = nil, want error for amount 0")
	}
	if len(pub.topics) != 0 {
		t.Errorf("published %d times, want 0", len(pub.topics))
	}
}

func TestAnnounce_RejectsMissingIdentity(t *testing.T) {
	a, pub, _, txID := setup(t, 0)

	if err := a.Announce(context.Background(), Request{TransactionID: txID, DeviceID: testDeviceID, Rupiah: 50000}); err == nil {
		t.Fatal("Announce() without MerchantID error = nil, want error")
	}
	if len(pub.topics) != 0 {
		t.Errorf("published %d times, want 0", len(pub.topics))
	}
}
