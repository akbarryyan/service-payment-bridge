package paymentpoller_test

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/jackc/pgx/v5/pgxpool"

	"service-payment-bridge/internal/announcer"
	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/mqttclient"
	"service-payment-bridge/internal/paymentpoller"
	"service-payment-bridge/internal/secrets"
	"service-payment-bridge/internal/transaction"
)

const (
	testDatabaseURL = "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
	testBrokerURL   = "tcp://localhost:11883"
	testMerchantID  = "POLL-TEST-MERCHANT"
	testDeviceID    = "POLL-TEST-DEVICE"
	rp50000Payload  = "/ext/awal-qris.mp3+/ext/lima.mp3+/ext/puluh.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3"
	paidAnswer      = `{"responseCode":"2005100","latestTransactionStatus":"00","paidTime":"2026-09-30T10:57:53+07:00","amount":{"value":"50000.00","currency":"IDR"}}`
)

type fixture struct {
	pool       *pgxpool.Pool
	q          *sqlc.Queries
	manjoURL   string
	queryCalls *atomic.Int32
	messages   chan string
	publisher  *mqttclient.Client
}

func setup(t *testing.T) *fixture {
	t.Helper()
	ctx := context.Background()
	start := time.Now()

	pool, err := database.NewPool(ctx, testDatabaseURL)
	if err != nil {
		t.Fatalf("connect test database: %v", err)
	}
	t.Cleanup(pool.Close)

	key, _ := rsa.GenerateKey(rand.Reader, 2048)
	der, _ := x509.MarshalPKCS8PrivateKey(key)
	t.Setenv("POLL_TEST_PRIVATE_KEY_REF", base64.StdEncoding.EncodeToString(der))
	t.Setenv("POLL_TEST_SECRET_REF", "poll-test-secret")

	if _, err := pool.Exec(ctx, `
		INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
		VALUES ($1, 'poll-client', 'POLL_TEST_PRIVATE_KEY_REF', 'POLL_TEST_SECRET_REF', $1, '05')
		ON CONFLICT (merchant_id) DO NOTHING`, testMerchantID); err != nil {
		t.Fatalf("seed merchant: %v", err)
	}
	if _, err := pool.Exec(ctx, `
		INSERT INTO devices (device_id, merchant_id, mqtt_topic) VALUES ($1, $2, $3)
		ON CONFLICT (device_id) DO NOTHING`, testDeviceID, testMerchantID, "topic_"+testDeviceID); err != nil {
		t.Fatalf("seed device: %v", err)
	}

	f := &fixture{pool: pool, q: sqlc.New(pool), queryCalls: &atomic.Int32{}, messages: make(chan string, 10)}

	manjo := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/v1.0/access-token/b2b":
			// No responseCode: cleanup below identifies mock token log rows by that.
			json.NewEncoder(w).Encode(manjoclient.AccessTokenResponse{TokenType: "Bearer", AccessToken: "test-token", ExpiresIn: "900"})
		case "/v1.0/qr/qr-mpm-query":
			f.queryCalls.Add(1)
			w.Write([]byte(paidAnswer))
		}
	}))
	t.Cleanup(manjo.Close)
	f.manjoURL = manjo.URL

	f.publisher, err = mqttclient.Connect(testBrokerURL, "", "")
	if err != nil {
		t.Fatalf("connect mosquitto (docker compose up -d?): %v", err)
	}
	t.Cleanup(f.publisher.Disconnect)

	subscriber, err := mqttclient.Connect(testBrokerURL, "", "")
	if err != nil {
		t.Fatalf("connect mosquitto: %v", err)
	}
	t.Cleanup(subscriber.Disconnect)
	if err := subscriber.Subscribe("topic_"+testDeviceID, func(_ mqtt.Client, m mqtt.Message) {
		f.messages <- string(m.Payload())
	}); err != nil {
		t.Fatalf("subscribe: %v", err)
	}

	// Registered last so it runs first, while pool and clients are still open.
	t.Cleanup(func() {
		ctx := context.Background()
		pool.Exec(ctx, `DELETE FROM mqtt_messages WHERE topic = $1`, "topic_"+testDeviceID)
		pool.Exec(ctx, `DELETE FROM manjo_api_logs WHERE transaction_id IN (SELECT transaction_id FROM transactions WHERE merchant_id = $1)`, testMerchantID)
		pool.Exec(ctx, `DELETE FROM manjo_api_logs WHERE operation = 'ACCESS_TOKEN' AND transaction_id IS NULL AND created_at >= $1 AND coalesce(response_body->>'responseCode', '') = ''`, start)
		pool.Exec(ctx, `DELETE FROM transactions WHERE merchant_id = $1`, testMerchantID)
		pool.Exec(ctx, `DELETE FROM devices WHERE device_id = $1`, testDeviceID)
		pool.Exec(ctx, `DELETE FROM merchants WHERE merchant_id = $1`, testMerchantID)
	})
	return f
}

// newPoller builds a poller with its own Service and client registry, like a separate
// server instance would have.
func (f *fixture) newPoller() *paymentpoller.Poller {
	logger := slog.New(slog.NewTextHandler(io.Discard, nil))
	svc := transaction.NewServiceWithBaseURL(f.q, manjoclient.NewRegistry(), secrets.EnvProvider{}, f.manjoURL)
	a := announcer.New(f.publisher, f.q, logger)
	return paymentpoller.New(f.q, svc, a, 3*time.Second, logger).OnlyMerchant(testMerchantID)
}

func (f *fixture) insertQRGenerated(t *testing.T, nextQueryAt time.Time) string {
	t.Helper()
	txID := fmt.Sprintf("POLL-TEST-%d", time.Now().UnixNano())
	if _, err := f.pool.Exec(context.Background(), `
		INSERT INTO transactions (transaction_id, merchant_id, device_id, amount, status, reference_no, external_id, expire_at, next_query_at)
		VALUES ($1, $2, $3, 50000, 'QR_GENERATED', $4, $5, now() + interval '7 minutes', $6)`,
		txID, testMerchantID, testDeviceID, "REF-"+txID, "EXT-"+txID, nextQueryAt); err != nil {
		t.Fatalf("insert transaction: %v", err)
	}
	return txID
}

func (f *fixture) status(t *testing.T, txID string) sqlc.TransactionStatus {
	t.Helper()
	tx, err := f.q.GetTransactionByID(context.Background(), txID)
	if err != nil {
		t.Fatalf("read transaction: %v", err)
	}
	return tx.Status
}

func TestRunOnce_PaidTransactionIsAnnounced(t *testing.T) {
	f := setup(t)
	txID := f.insertQRGenerated(t, time.Now().Add(-time.Second))

	f.newPoller().RunOnce(context.Background())

	select {
	case got := <-f.messages:
		if got != rp50000Payload {
			t.Errorf("announcement = %q, want %q", got, rp50000Payload)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("no announcement received on topic_" + testDeviceID)
	}
	if s := f.status(t, txID); s != sqlc.TransactionStatusPAID {
		t.Errorf("status = %s, want PAID", s)
	}
}

func TestRunOnce_TwoPollersAnnounceOnce(t *testing.T) {
	f := setup(t)
	txID := f.insertQRGenerated(t, time.Now().Add(-time.Second))

	p1, p2 := f.newPoller(), f.newPoller()
	var wg sync.WaitGroup
	for _, p := range []*paymentpoller.Poller{p1, p2} {
		wg.Add(1)
		go func() {
			defer wg.Done()
			p.RunOnce(context.Background())
		}()
	}
	wg.Wait()

	received := 0
	timeout := time.After(2 * time.Second)
collect:
	for {
		select {
		case <-f.messages:
			received++
		case <-timeout:
			break collect
		}
	}
	if received != 1 {
		t.Errorf("announcements = %d, want exactly 1", received)
	}
	if s := f.status(t, txID); s != sqlc.TransactionStatusPAID {
		t.Errorf("status = %s, want PAID", s)
	}
}

func TestRunOnce_SkipsTransactionsNotYetDue(t *testing.T) {
	f := setup(t)
	txID := f.insertQRGenerated(t, time.Now().Add(time.Hour))

	f.newPoller().RunOnce(context.Background())

	if n := f.queryCalls.Load(); n != 0 {
		t.Errorf("qr-mpm-query called %d times, want 0 for a transaction not yet due", n)
	}
	if s := f.status(t, txID); s != sqlc.TransactionStatusQRGENERATED {
		t.Errorf("status = %s, want QR_GENERATED", s)
	}
}
