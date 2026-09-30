package manjoclient

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func testConfig(t *testing.T, baseURL string) Config {
	t.Helper()
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatalf("failed to generate test key: %v", err)
	}
	der, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		t.Fatalf("failed to marshal test key: %v", err)
	}
	pemBytes := pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: der})

	return Config{
		BaseURL:       baseURL,
		ClientKey:     "test-client-key",
		PrivateKeyPEM: pemBytes,
		ClientSecret:  "test-client-secret",
		PartnerID:     "test-client-key",
		ChannelID:     "05",
	}
}

func TestAccessToken_Success(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/v1.0/access-token/b2b" {
			t.Errorf("unexpected path: %s", r.URL.Path)
		}
		w.WriteHeader(http.StatusOK)
		json.NewEncoder(w).Encode(AccessTokenResponse{
			ResponseCode: "2007300", ResponseMessage: "Successful",
			TokenType: "Bearer", AccessToken: "test-access-token", ExpiresIn: "900",
		})
	}))
	defer server.Close()

	c := New(testConfig(t, server.URL))
	token, expiresIn, err := c.AccessToken(context.Background())
	if err != nil {
		t.Fatalf("AccessToken() error = %v", err)
	}
	if token != "test-access-token" {
		t.Errorf("token = %q, want %q", token, "test-access-token")
	}
	if expiresIn.Seconds() != 900 {
		t.Errorf("expiresIn = %v, want 900s", expiresIn)
	}
}

func TestAccessToken_ReportsSuccessToHookWithMaskedToken(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		json.NewEncoder(w).Encode(AccessTokenResponse{
			ResponseCode: "2007300", TokenType: "Bearer", AccessToken: "secret-token-value", ExpiresIn: "900",
		})
	}))
	defer server.Close()

	var calls []AccessTokenCall
	cfg := testConfig(t, server.URL)
	cfg.OnAccessToken = func(_ context.Context, call AccessTokenCall) { calls = append(calls, call) }

	if _, _, err := New(cfg).AccessToken(context.Background()); err != nil {
		t.Fatalf("AccessToken() error = %v", err)
	}
	if len(calls) != 1 {
		t.Fatalf("hook called %d times, want 1", len(calls))
	}
	call := calls[0]
	if call.StatusCode != http.StatusOK || call.Err != nil {
		t.Errorf("StatusCode/Err = %d/%v, want 200/nil", call.StatusCode, call.Err)
	}
	if strings.Contains(string(call.ResponseBody), "secret-token-value") {
		t.Errorf("ResponseBody leaks the access token: %s", call.ResponseBody)
	}
	if !strings.Contains(string(call.ResponseBody), "2007300") {
		t.Errorf("ResponseBody = %s, want it to keep non-secret fields like responseCode", call.ResponseBody)
	}
}

func TestAccessToken_ReportsFailureToHook(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusUnauthorized)
		w.Write([]byte(`{"responseCode":"4017300"}`))
	}))
	defer server.Close()

	var calls []AccessTokenCall
	cfg := testConfig(t, server.URL)
	cfg.OnAccessToken = func(_ context.Context, call AccessTokenCall) { calls = append(calls, call) }

	if _, _, err := New(cfg).AccessToken(context.Background()); err == nil {
		t.Fatal("AccessToken() expected error, got nil")
	}
	if len(calls) != 1 {
		t.Fatalf("hook called %d times, want 1", len(calls))
	}
	if calls[0].StatusCode != http.StatusUnauthorized || calls[0].Err == nil {
		t.Errorf("StatusCode/Err = %d/%v, want 401/non-nil", calls[0].StatusCode, calls[0].Err)
	}
	if string(calls[0].ResponseBody) != `{"responseCode":"4017300"}` {
		t.Errorf("ResponseBody = %s, want the raw error body", calls[0].ResponseBody)
	}
}

func TestAccessToken_Unauthorized(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusUnauthorized)
		w.Write([]byte(`{"responseCode":"4017300","responseMessage":"Unauthorized"}`))
	}))
	defer server.Close()

	c := New(testConfig(t, server.URL))
	_, _, err := c.AccessToken(context.Background())
	if err == nil {
		t.Fatal("AccessToken() expected error, got nil")
	}
	apiErr, ok := err.(*APIError)
	if !ok {
		t.Fatalf("error type = %T, want *APIError", err)
	}
	if apiErr.StatusCode != http.StatusUnauthorized {
		t.Errorf("StatusCode = %d, want 401", apiErr.StatusCode)
	}
}

func TestGenerateQR_Success(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/v1.0/access-token/b2b":
			json.NewEncoder(w).Encode(AccessTokenResponse{
				TokenType: "Bearer", AccessToken: "test-access-token", ExpiresIn: "900",
			})
		case "/v1.0/qr/qr-mpm-generate":
			auth := r.Header.Get("Authorization")
			if auth != "Bearer test-access-token" {
				t.Errorf("Authorization header = %q, want %q", auth, "Bearer test-access-token")
			}
			json.NewEncoder(w).Encode(GenerateQRResponse{
				ResponseCode: "2004700", ResponseMessage: "Successful",
				ReferenceNo: "A0000001702", QRContent: "00020101...",
			})
		default:
			t.Errorf("unexpected path: %s", r.URL.Path)
		}
	}))
	defer server.Close()

	c := New(testConfig(t, server.URL))
	resp, externalID, err := c.GenerateQR(context.Background(), GenerateQRRequest{
		PartnerReferenceNo: "TRX-001",
		Amount:             Amount{Value: "50000.00", Currency: "IDR"},
		MerchantID:         "MT58530503",
		ValidityPeriod:     "3600",
		AdditionalInfo: GenerateQRAdditionalInfo{
			PaymentID: "99", DynamicAmount: "N", ProdDesc: "Q161 Payment",
		},
	})
	if err != nil {
		t.Fatalf("GenerateQR() error = %v", err)
	}
	if resp.QRContent == "" {
		t.Error("QRContent is empty")
	}
	if resp.ReferenceNo != "A0000001702" {
		t.Errorf("ReferenceNo = %q, want %q", resp.ReferenceNo, "A0000001702")
	}
	if externalID == "" {
		t.Error("externalID is empty")
	}
}

func TestGenerateQR_Conflict409(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/v1.0/access-token/b2b":
			json.NewEncoder(w).Encode(AccessTokenResponse{
				TokenType: "Bearer", AccessToken: "test-access-token", ExpiresIn: "900",
			})
		case "/v1.0/qr/qr-mpm-generate":
			w.WriteHeader(http.StatusConflict)
			w.Write([]byte(`{"responseCode":"4094700","responseMessage":"Conflict"}`))
		}
	}))
	defer server.Close()

	c := New(testConfig(t, server.URL))
	_, _, err := c.GenerateQR(context.Background(), GenerateQRRequest{
		PartnerReferenceNo: "TRX-001",
		Amount:             Amount{Value: "50000.00", Currency: "IDR"},
		MerchantID:         "MT58530503",
		ValidityPeriod:     "3600",
	})
	apiErr, ok := err.(*APIError)
	if !ok {
		t.Fatalf("error type = %T, want *APIError", err)
	}
	if apiErr.StatusCode != http.StatusConflict {
		t.Errorf("StatusCode = %d, want 409", apiErr.StatusCode)
	}
}

// uatQuerySuccess is a real qr-mpm-query response from UAT after paying via Alto.
const uatQuerySuccess = `{"responseCode":"2005100","responseMessage":"Successful","originalReferenceNo":"A503988936201952B746","originalPartnerReferenceNo":"E9Q3F86LGI80XP0DCWRBI7WS","originalExternalId":"30443786930722726463280097920912","serviceCode":"47","latestTransactionStatus":"00","transactionStatusDesc":"Success","paidTime":"2026-09-30T10:57:53+07:00","amount":{"value":"10000.00","currency":"IDR"},"feeAmount":{"value":"0.00","currency":"IDR"},"terminalId":"659","additionalInfo":{"currency":"IDR"}}`

func TestQueryPayment_RequestMatchesCollectionFormat(t *testing.T) {
	cfg := testConfig(t, "")
	cfg.PartnerID = "MT-TEST-MERCHANT" // must NOT be used as X-PARTNER-ID for query

	var gotHeaders http.Header
	var gotBody []byte
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/v1.0/access-token/b2b":
			json.NewEncoder(w).Encode(AccessTokenResponse{TokenType: "Bearer", AccessToken: "test-access-token", ExpiresIn: "900"})
		case "/v1.0/qr/qr-mpm-query":
			gotHeaders = r.Header.Clone()
			gotBody, _ = io.ReadAll(r.Body)
			w.Write([]byte(uatQuerySuccess))
		default:
			t.Errorf("unexpected path %s", r.URL.Path)
		}
	}))
	defer server.Close()
	cfg.BaseURL = server.URL

	resp, err := New(cfg).QueryPayment(context.Background(), QueryPaymentParams{
		OriginalReferenceNo:        "A503988936201952B746",
		OriginalPartnerReferenceNo: "TRX-20260930-ABC123",
		OriginalExternalID:         "EXT123",
		MerchantID:                 "MT58530503",
	})
	if err != nil {
		t.Fatalf("QueryPayment() error = %v", err)
	}

	if v := gotHeaders.Get("X-CLIENT-KEY"); v != "" {
		t.Errorf("X-CLIENT-KEY = %q, want absent (collection format)", v)
	}
	if v := gotHeaders.Get("X-PARTNER-ID"); v != cfg.ClientKey {
		t.Errorf("X-PARTNER-ID = %q, want client key %q", v, cfg.ClientKey)
	}
	if v := gotHeaders.Get("Authorization"); v != "Bearer test-access-token" {
		t.Errorf("Authorization = %q", v)
	}
	if v := gotHeaders.Get("CHANNEL-ID"); v != cfg.ChannelID {
		t.Errorf("CHANNEL-ID = %q, want %q", v, cfg.ChannelID)
	}
	if gotHeaders.Get("X-EXTERNAL-ID") == "" {
		t.Error("X-EXTERNAL-ID is empty")
	}
	wantSig, _ := SignHMAC(cfg.ClientSecret, http.MethodPost, "/v1.0/qr/qr-mpm-query", "test-access-token", gotBody, gotHeaders.Get("X-TIMESTAMP"))
	if v := gotHeaders.Get("X-SIGNATURE"); v != wantSig {
		t.Errorf("X-SIGNATURE does not match HMAC over path /v1.0/qr/qr-mpm-query and the sent body")
	}

	var body map[string]any
	if err := json.Unmarshal(gotBody, &body); err != nil {
		t.Fatalf("request body is not JSON: %v", err)
	}
	want := map[string]any{
		"originalReferenceNo":        "A503988936201952B746",
		"originalPartnerReferenceNo": "TRX-20260930-ABC123",
		"originalExternalId":         "EXT123",
		"serviceCode":                "47",
		"merchantId":                 "MT58530503",
	}
	for k, v := range want {
		if body[k] != v {
			t.Errorf("body[%q] = %v, want %v", k, body[k], v)
		}
	}
	if info, _ := body["additionalInfo"].(map[string]any); info["currency"] != "IDR" {
		t.Errorf("body.additionalInfo = %v, want currency IDR", body["additionalInfo"])
	}

	if resp.LatestTransactionStatus != "00" || resp.PaidTime != "2026-09-30T10:57:53+07:00" || resp.Amount.Value != "10000.00" {
		t.Errorf("response parsed as %+v", resp)
	}
}

func TestQueryPayment_ExpiredQRIsAPIErrorWithResponseCode(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/v1.0/access-token/b2b" {
			json.NewEncoder(w).Encode(AccessTokenResponse{TokenType: "Bearer", AccessToken: "t", ExpiresIn: "900"})
			return
		}
		w.WriteHeader(http.StatusForbidden)
		w.Write([]byte(`{"responseCode":"4035100","responseMessage":"Transaction Expire"}`))
	}))
	defer server.Close()

	_, err := New(testConfig(t, server.URL)).QueryPayment(context.Background(), QueryPaymentParams{})
	var apiErr *APIError
	if !errors.As(err, &apiErr) {
		t.Fatalf("error = %v, want *APIError", err)
	}
	if apiErr.StatusCode != http.StatusForbidden || apiErr.ResponseCode() != "4035100" {
		t.Errorf("APIError = %d / %q, want 403 / 4035100", apiErr.StatusCode, apiErr.ResponseCode())
	}
}

func TestAPIErrorResponseCode_NonJSONBody(t *testing.T) {
	if got := (&APIError{StatusCode: 502, Body: "<html>bad gateway</html>"}).ResponseCode(); got != "" {
		t.Errorf("ResponseCode() = %q, want empty", got)
	}
}
