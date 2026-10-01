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
	"service-payment-bridge/internal/qrtopic"
	"service-payment-bridge/internal/secrets"
	"service-payment-bridge/internal/testbroker"
	"service-payment-bridge/internal/transaction"
)

const (
	testDatabaseURL = "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
	testMerchantID  = "POLL-TEST-MERCHANT"
	testDeviceID    = "POLL-TEST-DEVICE"
	rp50000Payload  = "/ext/awal-qris.mp3+/ext/lima.mp3+/ext/puluh.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3"
)

// paidAnswerAt builds a qr-mpm-query "00" success body with the given paidTime, so tests
// can control how old the payment looks without depending on a fixed clock. The default
// mock answer (below) always uses time.Now(), since a fixed past paidTime would make the
// stale-payment guard (F2) skip every test's announcement.
func paidAnswerAt(paidTime time.Time) string {
	return fmt.Sprintf(`{"responseCode":"2005100","latestTransactionStatus":"00","paidTime":"%s","amount":{"value":"50000.00","currency":"IDR"}}`,
		paidTime.Format(time.RFC3339))
}

// expiredAnswer is qr-mpm-query's HTTP 403 answer once the QR has expired.
const expiredAnswer = `{"responseCode":"4035100","responseMessage":"Transaction Expire"}`

// mockAnswer is one canned qr-mpm-query response: HTTP status plus body.
type mockAnswer struct {
	status int
	body   string
}

type fixture struct {
	pool       *pgxpool.Pool
	q          *sqlc.Queries
	manjoURL   string
	queryCalls *atomic.Int32
	messages   chan string
	publisher  *mqttclient.Client

	mu     sync.Mutex
	answer *mockAnswer // nil = default: HTTP 200, PAID with paidTime = now
}

// setAnswer overrides qr-mpm-query's response for the rest of this test. Safe to call
// before the poller runs; the mock handler reads it under the same mutex.
func (f *fixture) setAnswer(status int, body string) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.answer = &mockAnswer{status: status, body: body}
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
		ON CONFLICT (device_id) DO NOTHING`, testDeviceID, testMerchantID, qrtopic.DeviceTopic(testMerchantID, testDeviceID)); err != nil {
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
			f.mu.Lock()
			a := f.answer
			f.mu.Unlock()
			if a == nil {
				a = &mockAnswer{status: http.StatusOK, body: paidAnswerAt(time.Now())}
			}
			w.WriteHeader(a.status)
			w.Write([]byte(a.body))
		}
	}))
	t.Cleanup(manjo.Close)
	f.manjoURL = manjo.URL

	f.publisher, err = mqttclient.Connect(testbroker.URL(), testbroker.Username(), testbroker.Password())
	if err != nil {
		t.Fatalf("connect mosquitto (docker compose up -d?): %v", err)
	}
	t.Cleanup(f.publisher.Disconnect)

	subscriber, err := mqttclient.Connect(testbroker.URL(), testbroker.Username(), testbroker.Password())
	if err != nil {
		t.Fatalf("connect mosquitto: %v", err)
	}
	t.Cleanup(subscriber.Disconnect)
	if err := subscriber.Subscribe(qrtopic.DeviceTopic(testMerchantID, testDeviceID), func(_ mqtt.Client, m mqtt.Message) {
		f.messages <- string(m.Payload())
	}); err != nil {
		t.Fatalf("subscribe: %v", err)
	}

	// Registered last so it runs first, while pool and clients are still open.
	t.Cleanup(func() {
		ctx := context.Background()
		pool.Exec(ctx, `DELETE FROM mqtt_messages WHERE topic = $1`, qrtopic.DeviceTopic(testMerchantID, testDeviceID))
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

// insertQRGeneratedNullNextQueryAt inserts a row the way an older binary (or any code
// path that never set next_query_at) would have left it: due for its first check, but
// with no schedule at all.
func (f *fixture) insertQRGeneratedNullNextQueryAt(t *testing.T) string {
	t.Helper()
	txID := fmt.Sprintf("POLL-TEST-%d", time.Now().UnixNano())
	if _, err := f.pool.Exec(context.Background(), `
		INSERT INTO transactions (transaction_id, merchant_id, device_id, amount, status, reference_no, external_id, expire_at, next_query_at)
		VALUES ($1, $2, $3, 50000, 'QR_GENERATED', $4, $5, now() + interval '7 minutes', NULL)`,
		txID, testMerchantID, testDeviceID, "REF-"+txID, "EXT-"+txID); err != nil {
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
		t.Fatal("no announcement received on " + qrtopic.DeviceTopic(testMerchantID, testDeviceID))
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

// F1: a row whose next_query_at was never set (e.g. left by an older binary) must still
// be picked up and announced, not stuck in QR_GENERATED forever.
func TestRunOnce_NullNextQueryAtIsClaimedAndAnnounced(t *testing.T) {
	f := setup(t)
	txID := f.insertQRGeneratedNullNextQueryAt(t)

	f.newPoller().RunOnce(context.Background())

	select {
	case got := <-f.messages:
		if got != rp50000Payload {
			t.Errorf("announcement = %q, want %q", got, rp50000Payload)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("no announcement received on " + qrtopic.DeviceTopic(testMerchantID, testDeviceID))
	}
	if s := f.status(t, txID); s != sqlc.TransactionStatusPAID {
		t.Errorf("status = %s, want PAID", s)
	}
}

// F2: a payment that was actually already made long ago (e.g. discovered right after a
// migration backfill or a restart after downtime) must not be announced with a stale
// amount the first time it's claimed.
func TestRunOnce_StalePaymentIsNotAnnounced(t *testing.T) {
	f := setup(t)
	txID := f.insertQRGenerated(t, time.Now().Add(-time.Second))
	f.setAnswer(http.StatusOK, paidAnswerAt(time.Now().Add(-time.Hour)))

	f.newPoller().RunOnce(context.Background())

	select {
	case got := <-f.messages:
		t.Fatalf("unexpected announcement for a stale payment: %q", got)
	case <-time.After(2 * time.Second):
	}
	if s := f.status(t, txID); s != sqlc.TransactionStatusPAID {
		t.Errorf("status = %s, want PAID (the transition itself still happens)", s)
	}
}

// F7: only a PAID transition announces (spec decision #6). An EXPIRED transition must
// still move the status but never publish anything to the device.
func TestRunOnce_ExpiredTransactionIsNotAnnounced(t *testing.T) {
	f := setup(t)
	txID := f.insertQRGenerated(t, time.Now().Add(-time.Second))
	f.setAnswer(http.StatusForbidden, expiredAnswer)

	f.newPoller().RunOnce(context.Background())

	select {
	case got := <-f.messages:
		t.Fatalf("unexpected announcement for an expired transaction: %q", got)
	case <-time.After(2 * time.Second):
	}
	if s := f.status(t, txID); s != sqlc.TransactionStatusEXPIRED {
		t.Errorf("status = %s, want EXPIRED", s)
	}
}

// panickingChecker is a PaymentChecker test double that always panics, used to prove F3:
// a panic in one worker must not bring down the whole poller (or the service).
type panickingChecker struct{}

func (panickingChecker) CheckPayment(ctx context.Context, tx sqlc.Transaction) (*transaction.PaymentCheckResult, error) {
	panic("boom: simulated panic in payment checker")
}

func TestRunOnce_RecoversFromWorkerPanic(t *testing.T) {
	f := setup(t)
	txID := f.insertQRGenerated(t, time.Now().Add(-time.Second))

	logger := slog.New(slog.NewTextHandler(io.Discard, nil))
	a := announcer.New(f.publisher, f.q, logger)
	p := paymentpoller.New(f.q, panickingChecker{}, a, 3*time.Second, logger).OnlyMerchant(testMerchantID)

	p.RunOnce(context.Background())

	if s := f.status(t, txID); s != sqlc.TransactionStatusQRGENERATED {
		t.Errorf("status = %s, want QR_GENERATED (checker panicked before any transition)", s)
	}
}
