package altosim

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func TestSign_KnownAnswer(t *testing.T) {
	// Computed independently:
	//   printf '%s' "POST:/altopay/qr-payment/payment:akey_test:<sha256(body)>:<ts>" | openssl dgst -sha256 -hmac vkey_test
	got := Sign("akey_test", "vkey_test", []byte(`{"command":"qr-payment-credit"}`), "2026-09-30 03:00:00.000Z")
	want := "be8c4ed686ef69d5da4799a41b44ba1a6fb3fb5c7a58d128e2b9924fa482cd1d"
	if got != want {
		t.Errorf("Sign() = %s, want %s", got, want)
	}
}

func TestFormatTimestamp_UTCWithMillis(t *testing.T) {
	wib := time.FixedZone("WIB", 7*60*60)
	got := FormatTimestamp(time.Date(2026, 9, 30, 10, 0, 0, 123_000_000, wib))
	if got != "2026-09-30 03:00:00.123Z" {
		t.Errorf("FormatTimestamp() = %q, want %q", got, "2026-09-30 03:00:00.123Z")
	}
}

func TestPay_SendsSignedRequest(t *testing.T) {
	var gotPath string
	var gotHeaders http.Header
	var gotBody []byte
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotPath, gotHeaders = r.URL.Path, r.Header.Clone()
		gotBody, _ = io.ReadAll(r.Body)
		w.Write([]byte(`{"status":"ok"}`))
	}))
	defer server.Close()

	req, err := BuildPaymentRequest(realQR, "2026-09-30 03:00:00.000Z", Identifiers{CustomerReferenceNumber: "c", ForwardingReferenceNumber: "f", AuthorizationID: "A"})
	if err != nil {
		t.Fatalf("BuildPaymentRequest() error = %v", err)
	}
	c := &Client{BaseURL: server.URL, APIKey: "akey_test", ValidationKey: "vkey_test"}

	status, body, err := c.Pay(context.Background(), req)
	if err != nil || status != http.StatusOK || string(body) != `{"status":"ok"}` {
		t.Fatalf("Pay() = %d %s %v", status, body, err)
	}
	if gotPath != PaymentPath {
		t.Errorf("path = %s, want %s", gotPath, PaymentPath)
	}
	if gotHeaders.Get("X-Alto-Key") != "akey_test" || gotHeaders.Get("X-Alto-Timestamp") != "2026-09-30 03:00:00.000Z" {
		t.Errorf("headers = %v", gotHeaders)
	}
	if want := Sign("akey_test", "vkey_test", gotBody, "2026-09-30 03:00:00.000Z"); gotHeaders.Get("X-Alto-Signature") != want {
		t.Error("X-Alto-Signature does not sign the body actually sent")
	}
}
