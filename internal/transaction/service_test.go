package transaction

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"

	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/resolver"
	"service-payment-bridge/internal/secrets"
)

const testDatabaseURL = "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"

func testPrivateKeyForService(t *testing.T) (pemBytes []byte, rawBase64 string) {
	t.Helper()
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatalf("failed to generate test key: %v", err)
	}
	der, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		t.Fatalf("failed to marshal test key: %v", err)
	}
	return pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: der}), base64.StdEncoding.EncodeToString(der)
}

func setupService(t *testing.T, manjoBaseURL string) (*Service, *sqlc.Queries, resolver.ResolvedDevice, *pgxpool.Pool) {
	t.Helper()
	ctx := context.Background()

	pool, err := database.NewPool(ctx, testDatabaseURL)
	if err != nil {
		t.Fatalf("failed to connect to test database: %v", err)
	}
	t.Cleanup(pool.Close)
	q := sqlc.New(pool)

	merchantID := "TXSVC-TEST-MERCHANT"
	deviceID := "TXSVC-TEST-DEVICE"

	privateKeyRawEnv := "TXSVC_TEST_PRIVATE_KEY_REF"
	secretEnv := "TXSVC_TEST_SECRET_REF"

	_, testKeyRawBase64 := testPrivateKeyForService(t)
	t.Setenv(privateKeyRawEnv, testKeyRawBase64)
	t.Setenv(secretEnv, "test-client-secret")

	_, err = pool.Exec(ctx, `
		INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
		VALUES ($1, 'test-client-id', $2, $3, $1, '05')
		ON CONFLICT (merchant_id) DO UPDATE SET manjo_private_key_ref = EXCLUDED.manjo_private_key_ref, manjo_client_secret_ref = EXCLUDED.manjo_client_secret_ref`,
		merchantID, privateKeyRawEnv, secretEnv)
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
		pool.Exec(context.Background(), `DELETE FROM manjo_api_logs WHERE transaction_id IN (SELECT transaction_id FROM transactions WHERE merchant_id = $1)`, merchantID)
		pool.Exec(context.Background(), `DELETE FROM transactions WHERE merchant_id = $1`, merchantID)
		pool.Exec(context.Background(), `DELETE FROM devices WHERE device_id = $1`, deviceID)
		pool.Exec(context.Background(), `DELETE FROM merchants WHERE merchant_id = $1`, merchantID)
	})

	r := resolver.New(q)
	device, err := r.ResolveDevice(ctx, deviceID)
	if err != nil {
		t.Fatalf("ResolveDevice() error = %v", err)
	}

	registry := manjoclient.NewRegistry()
	svc := NewServiceWithBaseURL(q, registry, secrets.EnvProvider{}, manjoBaseURL)

	return svc, q, *device, pool
}

func TestGenerateQR_Success(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/v1.0/access-token/b2b":
			json.NewEncoder(w).Encode(manjoclient.AccessTokenResponse{
				TokenType: "Bearer", AccessToken: "test-token", ExpiresIn: "900",
			})
		case "/v1.0/qr/qr-mpm-generate":
			json.NewEncoder(w).Encode(manjoclient.GenerateQRResponse{
				ResponseCode: "2004700", ReferenceNo: "A0000001", QRContent: "00020101...",
				AdditionalInfo: manjoclient.GenerateQRResponseAdditionalInfo{
					ExpireDate: time.Now().Add(time.Hour).Format("20060102150405"),
				},
			})
		}
	}))
	defer server.Close()

	svc, q, device, pool := setupService(t, server.URL)

	result, err := svc.GenerateQR(context.Background(), device, 50000)
	if err != nil {
		t.Fatalf("GenerateQR() error = %v", err)
	}
	if result.Status != "SUCCESS" {
		t.Errorf("Status = %q, want SUCCESS (ErrorCode=%s)", result.Status, result.ErrorCode)
	}
	if result.QRISPayload == "" {
		t.Error("QRISPayload is empty")
	}

	tx, err := q.GetTransactionByID(context.Background(), result.TransactionID)
	if err != nil {
		t.Fatalf("failed to read back transaction: %v", err)
	}
	if tx.Status != sqlc.TransactionStatusQRGENERATED {
		t.Errorf("stored status = %s, want QR_GENERATED", tx.Status)
	}
	if !tx.ExternalID.Valid || tx.ExternalID.String == "" {
		t.Error("stored external_id is empty")
	}

	rows, err := pool.Query(context.Background(), `SELECT operation, http_status FROM manjo_api_logs WHERE transaction_id = $1`, result.TransactionID)
	if err != nil {
		t.Fatalf("failed to query manjo_api_logs: %v", err)
	}
	defer rows.Close()
	found := false
	for rows.Next() {
		found = true
	}
	if !found {
		t.Error("expected at least one manjo_api_logs row for this transaction, found none")
	}
}

func TestGenerateQR_Conflict409_MarksFailed(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/v1.0/access-token/b2b":
			json.NewEncoder(w).Encode(manjoclient.AccessTokenResponse{
				TokenType: "Bearer", AccessToken: "test-token", ExpiresIn: "900",
			})
		case "/v1.0/qr/qr-mpm-generate":
			w.WriteHeader(http.StatusConflict)
			w.Write([]byte(`{"responseCode":"4094700"}`))
		}
	}))
	defer server.Close()

	svc, _, device, _ := setupService(t, server.URL)

	result, err := svc.GenerateQR(context.Background(), device, 50000)
	if err != nil {
		t.Fatalf("GenerateQR() error = %v", err)
	}
	if result.Status != "FAILED" || result.ErrorCode != "CONFLICT" {
		t.Errorf("Status/ErrorCode = %s/%s, want FAILED/CONFLICT", result.Status, result.ErrorCode)
	}
}

func TestGenerateQR_401_RefreshesTokenAndRetriesOnce(t *testing.T) {
	var tokenCalls, qrCalls int32

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/v1.0/access-token/b2b":
			tokenCalls++
			json.NewEncoder(w).Encode(manjoclient.AccessTokenResponse{
				TokenType: "Bearer", AccessToken: "test-token", ExpiresIn: "900",
			})
		case "/v1.0/qr/qr-mpm-generate":
			qrCalls++
			if qrCalls == 1 {
				w.WriteHeader(http.StatusUnauthorized)
				w.Write([]byte(`{"responseCode":"4017300"}`))
				return
			}
			json.NewEncoder(w).Encode(manjoclient.GenerateQRResponse{
				ReferenceNo: "A0000002", QRContent: "00020101...",
				AdditionalInfo: manjoclient.GenerateQRResponseAdditionalInfo{
					ExpireDate: time.Now().Add(time.Hour).Format("20060102150405"),
				},
			})
		}
	}))
	defer server.Close()

	svc, _, device, _ := setupService(t, server.URL)

	result, err := svc.GenerateQR(context.Background(), device, 50000)
	if err != nil {
		t.Fatalf("GenerateQR() error = %v", err)
	}
	if result.Status != "SUCCESS" {
		t.Errorf("Status = %s, want SUCCESS after 401-retry", result.Status)
	}
	if qrCalls != 2 {
		t.Errorf("qr-mpm-generate called %d times, want 2 (1 fail + 1 retry)", qrCalls)
	}
	if tokenCalls < 2 {
		t.Errorf("access-token called %d times, want >= 2 (initial + refresh after 401)", tokenCalls)
	}
}
