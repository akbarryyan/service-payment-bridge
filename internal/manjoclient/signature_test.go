package manjoclient

import (
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/base64"
	"encoding/hex"
	"encoding/pem"
	"testing"
)

func generateTestPrivateKeyPEM(t *testing.T) (*rsa.PrivateKey, []byte) {
	t.Helper()
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatalf("failed to generate test RSA key: %v", err)
	}

	der, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		t.Fatalf("failed to marshal PKCS8 key: %v", err)
	}

	pemBytes := pem.EncodeToMemory(&pem.Block{
		Type:  "PRIVATE KEY",
		Bytes: der,
	})

	return key, pemBytes
}

func TestSignAccessToken_ProducesVerifiableSignature(t *testing.T) {
	key, pemBytes := generateTestPrivateKeyPEM(t)

	clientKey := "EQ1WYMK9calE"
	timestamp := "2026-09-25T10:00:00.000+07:00"

	sig, err := SignAccessToken(pemBytes, clientKey, timestamp)
	if err != nil {
		t.Fatalf("SignAccessToken() error = %v", err)
	}

	sigBytes, err := hex.DecodeString(sig)
	if err != nil {
		t.Fatalf("signature is not valid hex: %v", err)
	}

	message := clientKey + "|" + timestamp
	hashed := sha256.Sum256([]byte(message))

	if err := rsa.VerifyPKCS1v15(&key.PublicKey, crypto.SHA256, hashed[:], sigBytes); err != nil {
		t.Errorf("signature failed verification: %v", err)
	}
}

func TestSignAccessToken_InvalidPEM(t *testing.T) {
	_, err := SignAccessToken([]byte("not a pem"), "key", "ts")
	if err == nil {
		t.Fatal("SignAccessToken() expected error for invalid PEM, got nil")
	}
}

func TestSignHMAC_KnownAnswer(t *testing.T) {
	// Reference value computed independently via openssl — see Task 3 note
	// in the implementation plan for the exact command.
	const want = "b1deb1951e3dddc2809eff2fcfe370cbc918297e9773b464a32060d5f77142602481af06650d0789e2a723ae89acc451fa86a701ed749eeaefa0e36d4ac3338f"

	got, err := SignHMAC(
		"test-secret",
		"POST",
		"/v1.0/qr/qr-mpm-generate",
		"test-token",
		[]byte(`{"amount":"50000.00"}`),
		"2026-09-25T10:00:00.000+07:00",
	)
	if err != nil {
		t.Fatalf("SignHMAC() error = %v", err)
	}
	if got != want {
		t.Errorf("SignHMAC() = %q, want %q", got, want)
	}
}

func TestSignHMAC_Deterministic(t *testing.T) {
	body := []byte(`{"amount":"75000.00"}`)
	got1, err := SignHMAC("secret", "POST", "/path", "token", body, "2026-09-25T10:00:00.000+07:00")
	if err != nil {
		t.Fatalf("SignHMAC() error = %v", err)
	}
	got2, err := SignHMAC("secret", "POST", "/path", "token", body, "2026-09-25T10:00:00.000+07:00")
	if err != nil {
		t.Fatalf("SignHMAC() second call error = %v", err)
	}
	if got1 != got2 {
		t.Errorf("SignHMAC() not deterministic: %q != %q", got1, got2)
	}
	if len(got1) != 128 {
		t.Errorf("SignHMAC() length = %d, want 128 (SHA-512 hex)", len(got1))
	}
}

func TestWrapPKCS8PEM_Roundtrip(t *testing.T) {
	_, pemBytes := generateTestPrivateKeyPEM(t)

	block, _ := pem.Decode(pemBytes)
	if block == nil {
		t.Fatal("failed to decode generated test PEM")
	}
	raw := base64.StdEncoding.EncodeToString(block.Bytes)

	got, err := WrapPKCS8PEM(raw)
	if err != nil {
		t.Fatalf("WrapPKCS8PEM() error = %v", err)
	}

	gotBlock, _ := pem.Decode(got)
	if gotBlock == nil {
		t.Fatal("WrapPKCS8PEM() output is not valid PEM")
	}
	if string(gotBlock.Bytes) != string(block.Bytes) {
		t.Error("WrapPKCS8PEM() roundtrip produced different DER bytes")
	}
}

func TestWrapPKCS8PEM_InvalidBase64(t *testing.T) {
	_, err := WrapPKCS8PEM("not valid base64!!!")
	if err == nil {
		t.Fatal("WrapPKCS8PEM() expected error for invalid base64, got nil")
	}
}
