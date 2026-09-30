package transaction

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"

	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/resolver"
)

func TestMapQueryResult(t *testing.T) {
	now := time.Date(2026, 9, 30, 11, 0, 0, 0, time.UTC)
	paidTime, _ := time.Parse(time.RFC3339, "2026-09-30T10:57:53+07:00")
	answer := func(code string) *manjoclient.QueryPaymentResponse {
		return &manjoclient.QueryPaymentResponse{LatestTransactionStatus: code, PaidTime: "2026-09-30T10:57:53+07:00", Amount: manjoclient.Amount{Value: "10000.00", Currency: "IDR"}}
	}
	expired := &manjoclient.APIError{StatusCode: http.StatusForbidden, Body: `{"responseCode":"4035100","responseMessage":"Transaction Expire"}`}

	tests := []struct {
		name       string
		resp       *manjoclient.QueryPaymentResponse
		err        error
		wantStatus sqlc.TransactionStatus
		wantCode   string
		wantErr    bool
	}{
		{"00 success is PAID", answer("00"), nil, sqlc.TransactionStatusPAID, "00", false},
		{"01 initiated is pending", answer("01"), nil, "", "", false},
		{"02 paying is pending", answer("02"), nil, "", "", false},
		{"03 pending is pending (Paid only in notifications)", answer("03"), nil, "", "", false},
		{"04 refunded", answer("04"), nil, sqlc.TransactionStatusREFUNDED, "04", false},
		{"05 canceled", answer("05"), nil, sqlc.TransactionStatusCANCELLED, "05", false},
		{"06 failed", answer("06"), nil, sqlc.TransactionStatusFAILED, "06", false},
		{"07 not found is retried", answer("07"), nil, "", "", true},
		{"unknown code is retried", answer("99"), nil, "", "", true},
		{"403 4035100 is EXPIRED", nil, expired, sqlc.TransactionStatusEXPIRED, "", false},
		{"403 other code is retried", nil, &manjoclient.APIError{StatusCode: http.StatusForbidden, Body: `{"responseCode":"4035101"}`}, "", "", true},
		{"404 is retried", nil, &manjoclient.APIError{StatusCode: http.StatusNotFound, Body: `{}`}, "", "", true},
		{"network error is retried", nil, errors.New("connection reset"), "", "", true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := mapQueryResult(tt.resp, tt.err, now)
			if got.status != tt.wantStatus || got.code != tt.wantCode || (got.err != nil) != tt.wantErr {
				t.Errorf("mapQueryResult() = status %q code %q err %v, want status %q code %q err? %v",
					got.status, got.code, got.err, tt.wantStatus, tt.wantCode, tt.wantErr)
			}
		})
	}

	paid := mapQueryResult(answer("00"), nil, now)
	if !paid.paidAt.Equal(paidTime) || paid.amount != 10000 {
		t.Errorf("PAID outcome paidAt %v amount %d, want %v and 10000", paid.paidAt, paid.amount, paidTime)
	}
	noTime := answer("00")
	noTime.PaidTime = ""
	if got := mapQueryResult(noTime, nil, now); !got.paidAt.Equal(now) {
		t.Errorf("missing paidTime: paidAt = %v, want now %v", got.paidAt, now)
	}
}

// queryMock is a fake Manjo serving access token + generate, with a swappable query answer.
type queryMock struct {
	mu     sync.Mutex
	status int
	body   string
}

func (m *queryMock) answer(status int, body string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.status, m.body = status, body
}

func newQueryMock(t *testing.T) (*queryMock, *httptest.Server) {
	t.Helper()
	m := &queryMock{status: http.StatusOK, body: `{"latestTransactionStatus":"03"}`}
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/v1.0/access-token/b2b":
			// No responseCode: test cleanup identifies mock token log rows by that.
			json.NewEncoder(w).Encode(manjoclient.AccessTokenResponse{TokenType: "Bearer", AccessToken: "test-token", ExpiresIn: "900"})
		case "/v1.0/qr/qr-mpm-generate":
			json.NewEncoder(w).Encode(manjoclient.GenerateQRResponse{
				ReferenceNo: fmt.Sprintf("A%d", time.Now().UnixNano()), QRContent: "00020101...",
				AdditionalInfo: manjoclient.GenerateQRResponseAdditionalInfo{ExpiryDuration: "450000"},
			})
		case "/v1.0/qr/qr-mpm-query":
			m.mu.Lock()
			defer m.mu.Unlock()
			w.WriteHeader(m.status)
			w.Write([]byte(m.body))
		}
	}))
	t.Cleanup(server.Close)
	return m, server
}

func newQRGeneratedTransaction(t *testing.T, svc *Service, q *sqlc.Queries, device resolver.ResolvedDevice) sqlc.Transaction {
	t.Helper()
	res, err := svc.GenerateQR(context.Background(), device, 50000)
	if err != nil || res.Status != "SUCCESS" {
		t.Fatalf("GenerateQR() = %+v, %v", res, err)
	}
	tx, err := q.GetTransactionByID(context.Background(), res.TransactionID)
	if err != nil {
		t.Fatalf("GetTransactionByID() error = %v", err)
	}
	return tx
}

func queryLogCount(t *testing.T, pool *pgxpool.Pool, transactionID string) int {
	t.Helper()
	var n int
	err := pool.QueryRow(context.Background(),
		`SELECT count(*) FROM manjo_api_logs WHERE transaction_id = $1 AND operation = 'QUERY_PAYMENT'`, transactionID).Scan(&n)
	if err != nil {
		t.Fatalf("count manjo_api_logs: %v", err)
	}
	return n
}

func TestCheckPayment_PendingLeavesTransactionOpen(t *testing.T) {
	mock, server := newQueryMock(t)
	svc, q, device, pool := setupService(t, server.URL)
	tx := newQRGeneratedTransaction(t, svc, q, device)
	mock.answer(http.StatusOK, `{"responseCode":"2005100","latestTransactionStatus":"03"}`)

	res, err := svc.CheckPayment(context.Background(), tx)
	if err != nil {
		t.Fatalf("CheckPayment() error = %v", err)
	}
	if res.Transitioned || res.QueryErr != nil {
		t.Errorf("result = %+v, want no transition and no QueryErr", res)
	}
	stored, _ := q.GetTransactionByID(context.Background(), tx.TransactionID)
	if stored.Status != sqlc.TransactionStatusQRGENERATED {
		t.Errorf("status = %s, want QR_GENERATED", stored.Status)
	}
	if n := queryLogCount(t, pool, tx.TransactionID); n != 0 {
		t.Errorf("pending poll wrote %d QUERY_PAYMENT log rows, want 0", n)
	}
}

func TestCheckPayment_PaidTransitionsExactlyOnce(t *testing.T) {
	mock, server := newQueryMock(t)
	svc, q, device, pool := setupService(t, server.URL)
	tx := newQRGeneratedTransaction(t, svc, q, device)
	mock.answer(http.StatusOK, `{"responseCode":"2005100","latestTransactionStatus":"00","paidTime":"2026-09-30T10:57:53+07:00","amount":{"value":"50000.00","currency":"IDR"}}`)

	res, err := svc.CheckPayment(context.Background(), tx)
	if err != nil {
		t.Fatalf("CheckPayment() error = %v", err)
	}
	if !res.Transitioned || res.Transaction.Status != sqlc.TransactionStatusPAID {
		t.Fatalf("result = %+v, want transition to PAID", res)
	}
	wantPaid, _ := time.Parse(time.RFC3339, "2026-09-30T10:57:53+07:00")
	if !res.Transaction.PaidAt.Time.Equal(wantPaid) {
		t.Errorf("paid_at = %v, want %v", res.Transaction.PaidAt.Time, wantPaid)
	}
	if res.Transaction.ManjoStatusCode.String != "00" || res.Transaction.NextQueryAt.Valid {
		t.Errorf("manjo_status_code %q next_query_at %v, want 00 and NULL", res.Transaction.ManjoStatusCode.String, res.Transaction.NextQueryAt)
	}
	if res.ManjoAmount != 50000 {
		t.Errorf("ManjoAmount = %d, want 50000", res.ManjoAmount)
	}
	if n := queryLogCount(t, pool, tx.TransactionID); n != 1 {
		t.Errorf("QUERY_PAYMENT log rows = %d, want 1", n)
	}

	// A second poller holding the same stale row must not transition (or announce) again.
	again, err := svc.CheckPayment(context.Background(), tx)
	if err != nil {
		t.Fatalf("second CheckPayment() error = %v", err)
	}
	if again.Transitioned {
		t.Error("second CheckPayment transitioned again, want exactly one transition")
	}
}

func TestCheckPayment_ExpiredByManjo(t *testing.T) {
	mock, server := newQueryMock(t)
	svc, q, device, _ := setupService(t, server.URL)
	tx := newQRGeneratedTransaction(t, svc, q, device)
	mock.answer(http.StatusForbidden, `{"responseCode":"4035100","responseMessage":"Transaction Expire"}`)

	res, err := svc.CheckPayment(context.Background(), tx)
	if err != nil {
		t.Fatalf("CheckPayment() error = %v", err)
	}
	if !res.Transitioned || res.Transaction.Status != sqlc.TransactionStatusEXPIRED || res.ExpiredByDeadline {
		t.Errorf("result = %+v, want EXPIRED by Manjo", res)
	}
}

func TestCheckPayment_QueryErrorBeforeDeadlineIsRetried(t *testing.T) {
	mock, server := newQueryMock(t)
	svc, q, device, _ := setupService(t, server.URL)
	tx := newQRGeneratedTransaction(t, svc, q, device)
	mock.answer(http.StatusInternalServerError, `{"responseCode":"5005100"}`)

	res, err := svc.CheckPayment(context.Background(), tx)
	if err != nil {
		t.Fatalf("CheckPayment() error = %v", err)
	}
	if res.Transitioned || res.QueryErr == nil {
		t.Errorf("result = %+v, want no transition and a QueryErr", res)
	}
}

func TestCheckPayment_DeadlinePassedExpiresLocally(t *testing.T) {
	mock, server := newQueryMock(t)
	svc, q, device, pool := setupService(t, server.URL)
	tx := newQRGeneratedTransaction(t, svc, q, device)
	mock.answer(http.StatusInternalServerError, `{"responseCode":"5005100"}`)

	if _, err := pool.Exec(context.Background(),
		`UPDATE transactions SET expire_at = now() - interval '3 minutes' WHERE transaction_id = $1`, tx.TransactionID); err != nil {
		t.Fatalf("backdate expire_at: %v", err)
	}
	tx, _ = q.GetTransactionByID(context.Background(), tx.TransactionID)

	res, err := svc.CheckPayment(context.Background(), tx)
	if err != nil {
		t.Fatalf("CheckPayment() error = %v", err)
	}
	if !res.Transitioned || res.Transaction.Status != sqlc.TransactionStatusEXPIRED || !res.ExpiredByDeadline {
		t.Errorf("result = %+v, want EXPIRED by deadline", res)
	}
}
