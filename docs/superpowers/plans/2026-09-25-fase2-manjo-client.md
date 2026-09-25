# Fase 2 — Manjo Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Membangun package `internal/manjoclient` yang bisa request Access Token B2B, generate kedua varian signature (RSA-SHA256 & HMAC-SHA512), cache & auto-refresh token secara thread-safe, dan memanggil Generate QRIS MPM ke Manjo — diverifikasi lewat unit test (mock) dan sandbox nyata (kredensial UAT Pupuk Kalteng).

**Architecture:** `internal/secrets` menyediakan interface kecil `Provider` untuk resolve referensi kredensial (implementasi pertama: baca dari environment variable). `internal/manjoclient` adalah pure component — semua kredensial diterima lewat `Config` yang sudah ter-resolve, tidak ada dependency ke MQTT/database. `Client` terdiri dari `TokenManager` (cache in-memory, mutex-protected) yang memanggil `AccessToken()` secara internal saat dibutuhkan, dan `GenerateQR()` yang otomatis ambil token dari `TokenManager`.

**Tech Stack:** Go stdlib murni untuk crypto (`crypto/rsa`, `crypto/sha256`, `crypto/hmac`, `crypto/sha512`, `crypto/x509`, `encoding/pem`, `encoding/hex`) — tidak ada dependency baru.

## Global Constraints

- Signature Access Token (RSA-SHA256): **hex-encoded** (dikonfirmasi dari `docs/manjo-collection/BI SNAP/access-token.yml`, bukan base64)
- Private key format: PKCS8 (`x509.ParsePKCS8PrivateKey`), bukan PKCS1
- Timestamp WIB harus digeser manual +7 jam dari UTC (`time.FixedZone`) — **jangan** pakai string-replace `Z`→`+07:00` (itu bug yang sudah ditemukan & dihindari di script sumber)
- `X-PARTNER-ID` pada Generate QR default = `ClientKey` (mcCodePayId) mengikuti ground truth collection, bukan `merchantId` seperti contoh statis di `manjo-api-docs.md` — **status belum final**, diverifikasi di Task 6
- Base URL default: `https://snapqris.manjo.co.id/api` — **belum final**, diverifikasi di Task 6
- `Client` **tidak** melakukan retry sendiri untuk error apa pun — itu tanggung jawab pemanggil (Fase 3)
- Tidak ada integrasi MQTT/database di plan ini
- Kredensial sandbox real **tidak boleh** ditulis ke file yang ter-track git — semua lewat `.env.sandbox` (gitignored)

---

## File Structure

```
internal/secrets/
├── provider.go
└── provider_test.go

internal/manjoclient/
├── timestamp.go
├── timestamp_test.go
├── signature.go
├── signature_test.go
├── token_manager.go
├── token_manager_test.go
├── types.go
├── client.go
├── client_test.go
└── sandbox_test.go

.env.sandbox            (baru, gitignored, berisi kredensial UAT Pupuk Kalteng asli)
.env.sandbox.example    (baru, template tanpa nilai asli)
.gitignore              (modify — tambah .env.sandbox & docs/manjo-collection/environments/)
```

---

### Task 1: `internal/secrets` — SecretProvider

**Files:**
- Create: `internal/secrets/provider.go`
- Test: `internal/secrets/provider_test.go`

**Interfaces:**
- Produces: `secrets.Provider` interface (`Resolve(ctx context.Context, ref string) (string, error)`), `secrets.EnvProvider` struct implementasi — dipakai `sandbox_test.go` di Task 6

- [x] **Step 1: Tulis failing test `internal/secrets/provider_test.go`**

```go
package secrets

import (
	"context"
	"testing"
)

func TestEnvProvider_Resolve_Found(t *testing.T) {
	t.Setenv("TEST_SECRET_REF", "hello-secret")

	p := EnvProvider{}
	v, err := p.Resolve(context.Background(), "TEST_SECRET_REF")
	if err != nil {
		t.Fatalf("Resolve() error = %v", err)
	}
	if v != "hello-secret" {
		t.Errorf("Resolve() = %q, want %q", v, "hello-secret")
	}
}

func TestEnvProvider_Resolve_NotFound(t *testing.T) {
	p := EnvProvider{}
	_, err := p.Resolve(context.Background(), "TEST_SECRET_REF_DOES_NOT_EXIST")
	if err == nil {
		t.Fatal("Resolve() expected error for missing env var, got nil")
	}
}
```

- [x] **Step 2: Jalankan test, verifikasi gagal**

Run: `go test ./internal/secrets/... -v`
Expected: FAIL — `secrets.EnvProvider` undefined (package `secrets` belum ada `provider.go`).

- [x] **Step 3: Tulis `internal/secrets/provider.go`**

```go
package secrets

import (
	"context"
	"fmt"
	"os"
)

// Provider resolves a credential reference (e.g. merchants.manjo_private_key_ref)
// into its actual secret value. EnvProvider is the first implementation
// (ref = environment variable name); a real secret manager (Vault/cloud)
// can be swapped in later without changing callers.
type Provider interface {
	Resolve(ctx context.Context, ref string) (string, error)
}

type EnvProvider struct{}

func (EnvProvider) Resolve(ctx context.Context, ref string) (string, error) {
	v, ok := os.LookupEnv(ref)
	if !ok {
		return "", fmt.Errorf("secrets: environment variable %q not set", ref)
	}
	return v, nil
}
```

- [x] **Step 4: Jalankan test, verifikasi lulus**

Run: `go test ./internal/secrets/... -v`
Expected: `PASS` untuk `TestEnvProvider_Resolve_Found` dan `TestEnvProvider_Resolve_NotFound`.

---

### Task 2: `internal/manjoclient/timestamp.go`

**Files:**
- Create: `internal/manjoclient/timestamp.go`
- Test: `internal/manjoclient/timestamp_test.go`

**Interfaces:**
- Produces: `manjoclient.NowJakarta() string` — dipakai `signature.go` (Task 3) dan `client.go` (Task 5)

- [x] **Step 1: Tulis failing test `internal/manjoclient/timestamp_test.go`**

```go
package manjoclient

import (
	"regexp"
	"testing"
	"time"
)

func TestNowJakarta_Format(t *testing.T) {
	got := NowJakarta()
	pattern := `^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}\+07:00$`
	matched, err := regexp.MatchString(pattern, got)
	if err != nil {
		t.Fatalf("regexp error: %v", err)
	}
	if !matched {
		t.Errorf("NowJakarta() = %q, does not match pattern %q", got, pattern)
	}
}

func TestNowJakarta_RepresentsCorrectInstant(t *testing.T) {
	before := time.Now()
	got := NowJakarta()
	after := time.Now()

	parsed, err := time.Parse("2006-01-02T15:04:05.000-07:00", got)
	if err != nil {
		t.Fatalf("failed to parse NowJakarta() output %q: %v", got, err)
	}

	if parsed.Before(before.Add(-2*time.Second)) || parsed.After(after.Add(2*time.Second)) {
		t.Errorf("NowJakarta() = %q (parsed instant %v) is not close to actual now (between %v and %v) — timestamp may be mislabeled instead of shifted", got, parsed, before, after)
	}
}
```

- [x] **Step 2: Jalankan test, verifikasi gagal**

Run: `go test ./internal/manjoclient/... -v -run TestNowJakarta`
Expected: FAIL — `manjoclient.NowJakarta` undefined.

- [x] **Step 3: Tulis `internal/manjoclient/timestamp.go`**

```go
package manjoclient

import "time"

// NowJakarta returns the current time formatted as WIB (UTC+7), matching
// the format Manjo's sandbox actually accepts (yyyy-MM-ddTHH:mm:ss.SSS+07:00).
// It shifts the clock explicitly rather than relabeling UTC as WIB — a bug
// found in an earlier version of the reference implementation.
func NowJakarta() string {
	loc := time.FixedZone("WIB", 7*60*60)
	return time.Now().In(loc).Format("2006-01-02T15:04:05.000-07:00")
}
```

- [x] **Step 4: Jalankan test, verifikasi lulus**

Run: `go test ./internal/manjoclient/... -v -run TestNowJakarta`
Expected: `PASS` untuk kedua test.

---

### Task 3: `internal/manjoclient/signature.go`

**Files:**
- Create: `internal/manjoclient/signature.go`
- Test: `internal/manjoclient/signature_test.go`

**Interfaces:**
- Consumes: tidak ada (pure function)
- Produces: `manjoclient.SignAccessToken(privateKeyPEM []byte, clientKey, timestamp string) (string, error)`, `manjoclient.SignHMAC(clientSecret, method, path, accessToken string, body []byte, timestamp string) (string, error)` — dipakai `client.go` (Task 5)

**Catatan nilai referensi:** nilai HMAC known-answer di bawah (`b1deb1951e3dddc2809eff2fcfe370cbc918297e9773b464a32060d5f77142602481af06650d0789e2a723ae89acc451fa86a701ed749eeaefa0e36d4ac3338f`) sudah dihitung independen pakai `openssl` (bukan dari kode yang diuji):
```bash
BODY='{"amount":"50000.00"}'
BODYHASH=$(printf '%s' "$BODY" | openssl dgst -sha256 -hex | sed 's/^.* //')
STRINGTOSIGN="POST:/v1.0/qr/qr-mpm-generate:test-token:${BODYHASH}:2026-09-25T10:00:00.000+07:00"
printf '%s' "$STRINGTOSIGN" | openssl dgst -sha512 -hmac "test-secret" -hex
```

- [x] **Step 1: Tulis failing test `internal/manjoclient/signature_test.go`**

```go
package manjoclient

import (
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
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
```

- [x] **Step 2: Jalankan test, verifikasi gagal**

Run: `go test ./internal/manjoclient/... -v -run TestSign`
Expected: FAIL — `SignAccessToken`/`SignHMAC` undefined.

- [x] **Step 3: Tulis `internal/manjoclient/signature.go`**

```go
package manjoclient

import (
	"crypto"
	"crypto/hmac"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/sha512"
	"crypto/x509"
	"encoding/hex"
	"encoding/pem"
	"fmt"
)

// SignAccessToken computes the X-SIGNATURE header for Access Token B2B
// (RSA-SHA256 over "clientKey|timestamp", hex-encoded — confirmed against
// docs/manjo-collection/BI SNAP/access-token.yml, not base64).
func SignAccessToken(privateKeyPEM []byte, clientKey, timestamp string) (string, error) {
	block, _ := pem.Decode(privateKeyPEM)
	if block == nil {
		return "", fmt.Errorf("manjoclient: failed to decode PEM block from private key")
	}

	key, err := x509.ParsePKCS8PrivateKey(block.Bytes)
	if err != nil {
		return "", fmt.Errorf("manjoclient: failed to parse PKCS8 private key: %w", err)
	}

	rsaKey, ok := key.(*rsa.PrivateKey)
	if !ok {
		return "", fmt.Errorf("manjoclient: private key is not an RSA key")
	}

	message := clientKey + "|" + timestamp
	hashed := sha256.Sum256([]byte(message))

	signature, err := rsa.SignPKCS1v15(rand.Reader, rsaKey, crypto.SHA256, hashed[:])
	if err != nil {
		return "", fmt.Errorf("manjoclient: failed to sign access token request: %w", err)
	}

	return hex.EncodeToString(signature), nil
}

// SignHMAC computes the X-SIGNATURE header for Generate QR / Query / Refund /
// Notify (HMAC-SHA512 over method:path:accessToken:bodyHash:timestamp,
// hex-encoded). body must already be the exact bytes sent as the HTTP body
// (json.Marshal output is compact by default, satisfying "minify").
func SignHMAC(clientSecret, method, path, accessToken string, body []byte, timestamp string) (string, error) {
	bodyHash := sha256.Sum256(body)
	bodyHashHex := hex.EncodeToString(bodyHash[:])

	stringToSign := method + ":" + path + ":" + accessToken + ":" + bodyHashHex + ":" + timestamp

	mac := hmac.New(sha512.New, []byte(clientSecret))
	mac.Write([]byte(stringToSign))

	return hex.EncodeToString(mac.Sum(nil)), nil
}
```

- [x] **Step 4: Jalankan test, verifikasi lulus**

Run: `go test ./internal/manjoclient/... -v -run TestSign`
Expected: `PASS` untuk keempat test, termasuk `TestSignHMAC_KnownAnswer` yang cocok persis dengan nilai `openssl`.

---

### Task 4: `internal/manjoclient/token_manager.go`

**Files:**
- Create: `internal/manjoclient/token_manager.go`
- Test: `internal/manjoclient/token_manager_test.go`

**Interfaces:**
- Consumes: `fetchTokenFunc func(ctx context.Context) (token string, expiresIn time.Duration, err error)` (disuntikkan lewat constructor)
- Produces: `manjoclient.NewTokenManager(fetch fetchTokenFunc) *TokenManager`, `(*TokenManager).Get(ctx context.Context) (string, error)` — dipakai `client.go` (Task 5)

- [x] **Step 1: Tulis failing test `internal/manjoclient/token_manager_test.go`**

```go
package manjoclient

import (
	"context"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func TestTokenManager_CachesTokenAcrossCalls(t *testing.T) {
	var fetchCount int32
	tm := NewTokenManager(func(ctx context.Context) (string, time.Duration, error) {
		atomic.AddInt32(&fetchCount, 1)
		return "token-1", 900 * time.Second, nil
	})

	tok1, err := tm.Get(context.Background())
	if err != nil {
		t.Fatalf("Get() error = %v", err)
	}
	tok2, err := tm.Get(context.Background())
	if err != nil {
		t.Fatalf("Get() error = %v", err)
	}

	if tok1 != "token-1" || tok2 != "token-1" {
		t.Errorf("tokens = %q, %q, want both %q", tok1, tok2, "token-1")
	}
	if atomic.LoadInt32(&fetchCount) != 1 {
		t.Errorf("fetch called %d times, want 1", fetchCount)
	}
}

func TestTokenManager_RefreshesWhenNearExpiry(t *testing.T) {
	var fetchCount int32
	tm := NewTokenManager(func(ctx context.Context) (string, time.Duration, error) {
		n := atomic.AddInt32(&fetchCount, 1)
		if n == 1 {
			return "token-1", 900 * time.Second, nil
		}
		return "token-2", 900 * time.Second, nil
	})

	fakeNow := time.Now()
	tm.now = func() time.Time { return fakeNow }

	tok1, err := tm.Get(context.Background())
	if err != nil {
		t.Fatalf("Get() error = %v", err)
	}
	if tok1 != "token-1" {
		t.Fatalf("tok1 = %q, want token-1", tok1)
	}

	// Advance clock to within the refresh threshold (< 60s left of the 900s TTL).
	fakeNow = fakeNow.Add(900*time.Second - 30*time.Second)
	tm.now = func() time.Time { return fakeNow }

	tok2, err := tm.Get(context.Background())
	if err != nil {
		t.Fatalf("Get() error = %v", err)
	}
	if tok2 != "token-2" {
		t.Errorf("tok2 = %q, want token-2 (should have refreshed)", tok2)
	}
	if atomic.LoadInt32(&fetchCount) != 2 {
		t.Errorf("fetch called %d times, want 2", fetchCount)
	}
}

func TestTokenManager_ConcurrentGet_FetchesOnce(t *testing.T) {
	var fetchCount int32
	tm := NewTokenManager(func(ctx context.Context) (string, time.Duration, error) {
		atomic.AddInt32(&fetchCount, 1)
		time.Sleep(10 * time.Millisecond) // simulate network latency
		return "token-1", 900 * time.Second, nil
	})

	var wg sync.WaitGroup
	for i := 0; i < 20; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if _, err := tm.Get(context.Background()); err != nil {
				t.Errorf("Get() error = %v", err)
			}
		}()
	}
	wg.Wait()

	if atomic.LoadInt32(&fetchCount) != 1 {
		t.Errorf("fetch called %d times under concurrent load, want 1", fetchCount)
	}
}
```

- [x] **Step 2: Jalankan test, verifikasi gagal**

Run: `go test ./internal/manjoclient/... -v -run TestTokenManager`
Expected: FAIL — `NewTokenManager` undefined.

- [x] **Step 3: Tulis `internal/manjoclient/token_manager.go`**

```go
package manjoclient

import (
	"context"
	"sync"
	"time"
)

const tokenRefreshThreshold = 60 * time.Second

type fetchTokenFunc func(ctx context.Context) (token string, expiresIn time.Duration, err error)

// TokenManager caches a Manjo access token in memory and refreshes it
// proactively before expiry. Concurrent Get() calls are serialized through
// a single mutex, so only one goroutine ever performs the actual fetch;
// the rest reuse the token it obtained (single-flight via lock, not
// singleflight.Group — simpler and sufficient for this use case).
type TokenManager struct {
	mu        sync.Mutex
	fetch     fetchTokenFunc
	token     string
	expiresAt time.Time
	now       func() time.Time
}

func NewTokenManager(fetch fetchTokenFunc) *TokenManager {
	return &TokenManager{
		fetch: fetch,
		now:   time.Now,
	}
}

func (tm *TokenManager) Get(ctx context.Context) (string, error) {
	tm.mu.Lock()
	defer tm.mu.Unlock()

	if tm.token != "" && tm.now().Before(tm.expiresAt.Add(-tokenRefreshThreshold)) {
		return tm.token, nil
	}

	token, expiresIn, err := tm.fetch(ctx)
	if err != nil {
		return "", err
	}

	tm.token = token
	tm.expiresAt = tm.now().Add(expiresIn)

	return tm.token, nil
}
```

- [x] **Step 4: Jalankan test dengan `-race`, verifikasi lulus**

Run: `go test ./internal/manjoclient/... -v -race -run TestTokenManager`
Expected: `PASS` untuk ketiga test, tanpa race warning.

---

### Task 5: `internal/manjoclient` — `types.go` + `client.go`

**Files:**
- Create: `internal/manjoclient/types.go`
- Create: `internal/manjoclient/client.go`
- Test: `internal/manjoclient/client_test.go`

**Interfaces:**
- Consumes: `NowJakarta()` (Task 2), `SignAccessToken`/`SignHMAC` (Task 3), `NewTokenManager`/`TokenManager.Get` (Task 4)
- Produces: `manjoclient.Config`, `manjoclient.Amount`, `manjoclient.GenerateQRRequest`, `manjoclient.GenerateQRResponse`, `manjoclient.APIError`, `manjoclient.New(cfg Config) *Client`, `(*Client).AccessToken(ctx) (string, time.Duration, error)`, `(*Client).GenerateQR(ctx, req GenerateQRRequest) (*GenerateQRResponse, error)` — dipakai `sandbox_test.go` (Task 6) dan Transaction Service di Fase 3

- [x] **Step 1: Tulis `internal/manjoclient/types.go`**

```go
package manjoclient

import (
	"fmt"
	"net/http"
)

type Config struct {
	BaseURL       string
	ClientKey     string // X-CLIENT-KEY / mcCodePayId
	PrivateKeyPEM []byte // PKCS8 PEM, termasuk header BEGIN/END
	ClientSecret  string // vkey, dipakai HMAC-SHA512
	PartnerID     string // X-PARTNER-ID
	ChannelID     string // CHANNEL-ID
	HTTPClient    *http.Client
}

type Amount struct {
	Value    string `json:"value"`
	Currency string `json:"currency"`
}

type GenerateQRAdditionalInfo struct {
	PaymentID     string `json:"paymentId"`
	DynamicAmount string `json:"dynamicAmount"`
	ProdDesc      string `json:"prodDesc"`
}

type GenerateQRRequest struct {
	PartnerReferenceNo string                   `json:"partnerReferenceNo"`
	Amount             Amount                   `json:"amount"`
	MerchantID         string                   `json:"merchantId"`
	SubMerchantID      string                   `json:"subMerchantId,omitempty"`
	StoreID            string                   `json:"storeId,omitempty"`
	TerminalID         string                   `json:"terminalId,omitempty"`
	ValidityPeriod     string                   `json:"validityPeriod"`
	AdditionalInfo     GenerateQRAdditionalInfo `json:"additionalInfo"`
}

type GenerateQRResponseAdditionalInfo struct {
	PaymentID      string `json:"paymentId"`
	MerchantCode   string `json:"merchantCode"`
	ExpiryDuration string `json:"expiryDuration"`
	ExpireDate     string `json:"expireDate"`
	Amount         string `json:"amount"`
}

type GenerateQRResponse struct {
	ResponseCode       string                           `json:"responseCode"`
	ResponseMessage    string                           `json:"responseMessage"`
	ReferenceNo        string                           `json:"referenceNo"`
	PartnerReferenceNo string                           `json:"partnerReferenceNo"`
	QRContent          string                           `json:"qrContent"`
	MerchantName       string                           `json:"merchantName"`
	StoreID            string                           `json:"storeId"`
	TerminalID         string                           `json:"terminalId"`
	AdditionalInfo     GenerateQRResponseAdditionalInfo `json:"additionalInfo"`
}

type AccessTokenResponse struct {
	ResponseCode    string `json:"responseCode"`
	ResponseMessage string `json:"responseMessage"`
	TokenType       string `json:"tokenType"`
	AccessToken     string `json:"accessToken"`
	ExpiresIn       string `json:"expiresIn"`
}

// APIError represents a non-2xx response from Manjo, carrying the HTTP
// status code so callers can branch on it (401 -> refresh+retry, 409 ->
// check status before retry, etc. per architecture.md Section 13).
type APIError struct {
	StatusCode int
	Body       string
}

func (e *APIError) Error() string {
	return fmt.Sprintf("manjoclient: manjo returned HTTP %d: %s", e.StatusCode, e.Body)
}
```

- [x] **Step 2: Tulis failing test `internal/manjoclient/client_test.go`**

```go
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
	resp, err := c.GenerateQR(context.Background(), GenerateQRRequest{
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
	_, err := c.GenerateQR(context.Background(), GenerateQRRequest{
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
```

- [x] **Step 3: Jalankan test, verifikasi gagal**

Run: `go test ./internal/manjoclient/... -v -run "TestAccessToken|TestGenerateQR"`
Expected: FAIL — `New`/`Client` undefined.

- [x] **Step 4: Tulis `internal/manjoclient/client.go`**

```go
package manjoclient

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"time"
)

const defaultBaseURL = "https://snapqris.manjo.co.id/api"

type Client struct {
	cfg          Config
	httpClient   *http.Client
	tokenManager *TokenManager
}

func New(cfg Config) *Client {
	if cfg.BaseURL == "" {
		cfg.BaseURL = defaultBaseURL
	}
	if cfg.HTTPClient == nil {
		cfg.HTTPClient = &http.Client{Timeout: 15 * time.Second}
	}

	c := &Client{
		cfg:        cfg,
		httpClient: cfg.HTTPClient,
	}
	c.tokenManager = NewTokenManager(c.fetchAccessToken)
	return c
}

func (c *Client) fetchAccessToken(ctx context.Context) (string, time.Duration, error) {
	return c.AccessToken(ctx)
}

// AccessToken calls POST /v1.0/access-token/b2b directly, bypassing the
// cache. Normally reached indirectly via GenerateQR through TokenManager.
func (c *Client) AccessToken(ctx context.Context) (string, time.Duration, error) {
	timestamp := NowJakarta()
	signature, err := SignAccessToken(c.cfg.PrivateKeyPEM, c.cfg.ClientKey, timestamp)
	if err != nil {
		return "", 0, err
	}

	body := []byte(`{"grantType":"client_credentials"}`)

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.cfg.BaseURL+"/v1.0/access-token/b2b", bytes.NewReader(body))
	if err != nil {
		return "", 0, err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-TIMESTAMP", timestamp)
	req.Header.Set("X-CLIENT-KEY", c.cfg.ClientKey)
	req.Header.Set("X-SIGNATURE", signature)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return "", 0, fmt.Errorf("manjoclient: access token request failed: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", 0, fmt.Errorf("manjoclient: failed to read access token response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return "", 0, &APIError{StatusCode: resp.StatusCode, Body: string(respBody)}
	}

	var tokenResp AccessTokenResponse
	if err := json.Unmarshal(respBody, &tokenResp); err != nil {
		return "", 0, fmt.Errorf("manjoclient: failed to parse access token response: %w", err)
	}

	var expiresInSeconds int
	if _, err := fmt.Sscanf(tokenResp.ExpiresIn, "%d", &expiresInSeconds); err != nil {
		return "", 0, fmt.Errorf("manjoclient: invalid expiresIn %q: %w", tokenResp.ExpiresIn, err)
	}

	return tokenResp.AccessToken, time.Duration(expiresInSeconds) * time.Second, nil
}

// GenerateQR calls POST /v1.0/qr/qr-mpm-generate, obtaining a valid access
// token from the cache automatically.
func (c *Client) GenerateQR(ctx context.Context, req GenerateQRRequest) (*GenerateQRResponse, error) {
	token, err := c.tokenManager.Get(ctx)
	if err != nil {
		return nil, fmt.Errorf("manjoclient: failed to obtain access token: %w", err)
	}

	body, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("manjoclient: failed to marshal generate QR request: %w", err)
	}

	timestamp := NowJakarta()
	const path = "/v1.0/qr/qr-mpm-generate"

	signature, err := SignHMAC(c.cfg.ClientSecret, http.MethodPost, path, token, body, timestamp)
	if err != nil {
		return nil, err
	}

	externalID, err := generateExternalID(35)
	if err != nil {
		return nil, err
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.cfg.BaseURL+path, bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	httpReq.Header.Set("Content-Type", "application/json")
	httpReq.Header.Set("Authorization", "Bearer "+token)
	httpReq.Header.Set("X-TIMESTAMP", timestamp)
	httpReq.Header.Set("X-SIGNATURE", signature)
	httpReq.Header.Set("X-PARTNER-ID", c.cfg.PartnerID)
	httpReq.Header.Set("X-EXTERNAL-ID", externalID)
	httpReq.Header.Set("CHANNEL-ID", c.cfg.ChannelID)

	resp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return nil, fmt.Errorf("manjoclient: generate QR request failed: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("manjoclient: failed to read generate QR response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return nil, &APIError{StatusCode: resp.StatusCode, Body: string(respBody)}
	}

	var qrResp GenerateQRResponse
	if err := json.Unmarshal(respBody, &qrResp); err != nil {
		return nil, fmt.Errorf("manjoclient: failed to parse generate QR response: %w", err)
	}

	return &qrResp, nil
}

func generateExternalID(n int) (string, error) {
	const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz"
	randomBytes := make([]byte, n)
	if _, err := rand.Read(randomBytes); err != nil {
		return "", fmt.Errorf("manjoclient: failed to generate X-EXTERNAL-ID: %w", err)
	}
	b := make([]byte, n)
	for i, rb := range randomBytes {
		b[i] = alphabet[int(rb)%len(alphabet)]
	}
	return string(b), nil
}
```

- [x] **Step 5: Jalankan test, verifikasi lulus**

Run: `go test ./internal/manjoclient/... -v -run "TestAccessToken|TestGenerateQR"`
Expected: `PASS` untuk keempat test (`TestAccessToken_Success`, `TestAccessToken_Unauthorized`, `TestGenerateQR_Success`, `TestGenerateQR_Conflict409`).

- [x] **Step 6: Jalankan seluruh test package**

Run: `go test ./internal/manjoclient/... -v -race`
Expected: semua test dari Task 2-5 `PASS`, tanpa race warning.

---

### Task 6: Sandbox Setup & Verifikasi Nyata

**Files:**
- Create: `.env.sandbox` (gitignored — berisi kredensial UAT Pupuk Kalteng asli)
- Create: `.env.sandbox.example`
- Modify: `.gitignore`
- Create: `internal/manjoclient/sandbox_test.go`

**Interfaces:**
- Consumes: `secrets.EnvProvider` (Task 1), `manjoclient.New`/`Config`/`GenerateQRRequest` (Task 5)
- Produces: bukti empiris apakah temuan #5 (`X-PARTNER-ID = ClientKey`) dan #6 (base URL) di spec benar — deliverable akhir Fase 2

- [x] **Step 1: Ekstrak kredensial dari Bruno collection**

Baca `docs/manjo-collection/environments/UAT Pupuk Kalteng.yml`, ambil 4 nilai:
- `mcCodePayId` → jadi `MANJO_SANDBOX_CLIENT_KEY`
- `vkey` → jadi `MANJO_SANDBOX_CLIENT_SECRET`
- `merchantId` → jadi `MANJO_SANDBOX_MERCHANT_ID`
- `privateKey` → gabungkan semua baris body (hapus newline & leading whitespace tiap baris) jadi satu baris base64 tanpa spasi → jadi `MANJO_SANDBOX_PRIVATE_KEY`

- [x] **Step 2: Tulis `.env.sandbox`**

```
MANJO_SANDBOX_BASE_URL=https://snapqris.manjo.co.id/api
MANJO_SANDBOX_CLIENT_KEY=<nilai mcCodePayId dari Step 1>
MANJO_SANDBOX_PRIVATE_KEY=<nilai privateKey satu baris dari Step 1>
MANJO_SANDBOX_CLIENT_SECRET=<nilai vkey dari Step 1>
MANJO_SANDBOX_MERCHANT_ID=<nilai merchantId dari Step 1>
```

- [x] **Step 3: Tulis `.env.sandbox.example`**

```
MANJO_SANDBOX_BASE_URL=https://snapqris.manjo.co.id/api
MANJO_SANDBOX_CLIENT_KEY=
MANJO_SANDBOX_PRIVATE_KEY=
MANJO_SANDBOX_CLIENT_SECRET=
MANJO_SANDBOX_MERCHANT_ID=
```

- [x] **Step 4: Update `.gitignore`**

Tambahkan dua baris ke `.gitignore` (isi existing dari Fase 1 tetap dipertahankan):

```
.env.sandbox
docs/manjo-collection/environments/
```

- [x] **Step 5: Tulis `internal/manjoclient/sandbox_test.go`**

```go
package manjoclient

import (
	"context"
	"encoding/base64"
	"encoding/pem"
	"os"
	"strings"
	"testing"
	"time"

	"service-payment-bridge/internal/secrets"
)

func TestSandbox_AccessTokenAndGenerateQR(t *testing.T) {
	if os.Getenv("RUN_SANDBOX_TESTS") != "1" {
		t.Skip("RUN_SANDBOX_TESTS not set to 1 — skipping real sandbox call (see .env.sandbox.example)")
	}

	ctx := context.Background()
	provider := secrets.EnvProvider{}

	clientKey := mustResolve(t, ctx, provider, "MANJO_SANDBOX_CLIENT_KEY")
	privateKeyRaw := mustResolve(t, ctx, provider, "MANJO_SANDBOX_PRIVATE_KEY")
	clientSecret := mustResolve(t, ctx, provider, "MANJO_SANDBOX_CLIENT_SECRET")
	merchantID := mustResolve(t, ctx, provider, "MANJO_SANDBOX_MERCHANT_ID")

	baseURL, err := provider.Resolve(ctx, "MANJO_SANDBOX_BASE_URL")
	if err != nil {
		baseURL = defaultBaseURL
	}

	cfg := Config{
		BaseURL:       baseURL,
		ClientKey:     clientKey,
		PrivateKeyPEM: wrapPKCS8PEM(t, privateKeyRaw),
		ClientSecret:  clientSecret,
		PartnerID:     clientKey, // temuan #5 di spec — X-PARTNER-ID = mcCodePayId di collection asli
		ChannelID:     "05",
	}

	c := New(cfg)

	reqCtx, cancel := context.WithTimeout(ctx, 15*time.Second)
	defer cancel()

	token, expiresIn, err := c.AccessToken(reqCtx)
	if err != nil {
		t.Fatalf("AccessToken() against sandbox failed: %v", err)
	}
	if token == "" {
		t.Fatal("AccessToken() returned empty token")
	}
	if expiresIn.Seconds() != 900 {
		t.Errorf("expiresIn = %v, want 900s", expiresIn)
	}
	t.Logf("access token obtained, expiresIn=%v", expiresIn)

	resp, err := c.GenerateQR(reqCtx, GenerateQRRequest{
		PartnerReferenceNo: "SANDBOX-TEST-" + time.Now().Format("20060102150405"),
		Amount:             Amount{Value: "1000.00", Currency: "IDR"},
		MerchantID:         merchantID,
		ValidityPeriod:     "3600",
		AdditionalInfo: GenerateQRAdditionalInfo{
			PaymentID:     "99",
			DynamicAmount: "N",
			ProdDesc:      "Sandbox test Fase 2",
		},
	})
	if err != nil {
		t.Fatalf("GenerateQR() against sandbox failed: %v (kalau 401/404, coba PartnerID=merchantID sesuai contoh manjo-api-docs.md — lihat temuan #5 di spec)", err)
	}
	if resp.QRContent == "" {
		t.Error("GenerateQR() succeeded but QRContent is empty")
	}
	t.Logf("qrContent received (%d chars), referenceNo=%s", len(resp.QRContent), resp.ReferenceNo)
}

func mustResolve(t *testing.T, ctx context.Context, p secrets.Provider, ref string) string {
	t.Helper()
	v, err := p.Resolve(ctx, ref)
	if err != nil {
		t.Fatalf("failed to resolve %s: %v (see .env.sandbox.example)", ref, err)
	}
	return v
}

// wrapPKCS8PEM wraps a raw base64 PKCS8 key (no PEM headers, possibly with
// embedded whitespace) into a proper PEM block — mirrors what the Bruno
// collection script does at runtime.
func wrapPKCS8PEM(t *testing.T, raw string) []byte {
	t.Helper()
	clean := strings.Join(strings.Fields(raw), "")
	decoded, err := base64.StdEncoding.DecodeString(clean)
	if err != nil {
		t.Fatalf("wrapPKCS8PEM: failed to decode base64 private key: %v", err)
	}
	return pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: decoded})
}
```

- [x] **Step 6: Verifikasi test di-skip tanpa env var**

Run: `go test ./internal/manjoclient/... -v -run TestSandbox`
Expected: `SKIP` dengan pesan jelas (bukan FAIL) — `RUN_SANDBOX_TESTS` belum diset.

- [x] **Step 7: Jalankan sandbox test sungguhan**

Run:
```bash
export $(grep -v '^#' .env.sandbox | xargs)
export RUN_SANDBOX_TESTS=1
go test ./internal/manjoclient/... -v -run TestSandbox
```

Expected salah satu dari dua hasil:
- **PASS** — `AccessToken()` dapat token asli (`expiresIn=900`), `GenerateQR()` dapat `qrContent` asli. Berarti temuan #5 (`X-PARTNER-ID=ClientKey`) dan #6 (base URL `snapqris.manjo.co.id`) **terbukti benar**.
- **FAIL di `GenerateQR()`** dengan error `401`/`404` — berarti temuan #5/#6 salah. Debug pertama: ganti `PartnerID: clientKey` jadi `PartnerID: merchantID` di `sandbox_test.go` Step 5, jalankan ulang. Kalau masih gagal, coba `MANJO_SANDBOX_BASE_URL=https://snapqris-uat.manjo.co.id/api` di `.env.sandbox`.

Catat hasil akhir (kombinasi `PartnerID`/`BaseURL` yang benar-benar diterima Manjo) — ini jadi acuan default yang benar untuk `Config` di Fase 3, update `types.go`/dokumentasi kalau default yang dipakai di Step 5 `client.go`/Task 5 ternyata salah.

---

## Selesai

Setelah Task 1-6 lulus (termasuk sandbox test sungguhan di Task 6 Step 7), `internal/manjoclient` siap dipakai Transaction Service di Fase 3: request access token, generate signature yang benar-benar diterima Manjo, cache token thread-safe, dan generate QR sungguhan — semua sudah terverifikasi lewat unit test (mock) maupun sandbox nyata, bukan asumsi dari dokumentasi statis.

## Execution Notes (dieksekusi 2026-09-25)

**Temuan #5 dan #6 di spec (kredensial UAT Pupuk Kalteng) sekarang terjawab lewat sandbox test sungguhan:**

| Temuan | Dugaan awal (dari ground truth Bruno collection) | Hasil verifikasi sandbox nyata |
|---|---|---|
| Base URL | `https://snapqris.manjo.co.id/api` (dipakai mayoritas file collection) | **`https://snapqris-uat.manjo.co.id/api`** — host non-uat balas `410 Unauthorized. Public Key Not Found` untuk kredensial UAT Pupuk Kalteng ini |
| `X-PARTNER-ID` | `ClientKey` (mcCodePayId) — sesuai script `qr-mpm-generate.yml` | **`merchantID`** — `ClientKey` ditolak `404 Invalid X-PARTNER-ID`; kemungkinan script Bruno di collection sudah stale/salah utk kredensial ini |

**Temuan baru (di luar dua yang sudah diantisipasi):** `expiresIn` access token sandbox UAT nyata adalah **13 jam (46800 detik)**, bukan `900` detik seperti didokumentasikan `manjo-api-docs.md`. `TokenManager`/`Client` tetap benar (menangani durasi apa pun secara generik), hanya asersi test `expiresIn=900` di `sandbox_test.go` diubah jadi `t.Logf` (informasional) alih-alih `t.Errorf`, supaya tidak menganggap ini sebagai bug kode.

**File yang disesuaikan setelah temuan di atas** (di luar apa yang tertulis di plan awal, hasil debug real-time):
- `.env.sandbox` & `.env.sandbox.example`: `MANJO_SANDBOX_BASE_URL` default diubah ke host `-uat`
- `internal/manjoclient/sandbox_test.go`: `Config.PartnerID` diubah dari `clientKey` ke `merchantID`; assertion `expiresIn` dilonggarkan jadi log, bukan fail

**Hasil akhir:** `TestSandbox_AccessTokenAndGenerateQR` **PASS** — `qrContent` asli (266 karakter) dan `referenceNo` asli diterima dari Manjo UAT.

**Verifikasi akhir seluruh project:**
- `go build ./...`, `go vet ./...`, `gofmt -l .` — bersih
- `go test ./... -v -race` — **23 test PASS** di 8 package (Fase 1: 8 test; Fase 2: 15 test — `secrets` 2, `timestamp` 2, `signature` 4, `token_manager` 3, `client` 4; sandbox test di-skip tanpa `RUN_SANDBOX_TESTS`, PASS saat dijalankan manual dengan env var itu)
- Tidak ada race warning di `TokenManager` concurrent access (`TestTokenManager_ConcurrentGet_FetchesOnce`, 20 goroutine paralel)
