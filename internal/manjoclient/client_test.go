package manjoclient

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"net/http"
	"net/http/httptest"
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
