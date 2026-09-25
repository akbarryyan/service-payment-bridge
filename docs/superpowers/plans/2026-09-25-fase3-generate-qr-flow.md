# Fase 3 — Generate QR Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Mengimplementasikan siklus penuh Generate QR — pesan MQTT masuk dari Q161 sampai `qris_payload` terkirim balik — terintegrasi dengan database (Fase 1) dan Manjo Client (Fase 2), termasuk retry policy dan logging traceability (`mqtt_messages`, `manjo_api_logs`).

**Architecture:** `main.go` subscribe `topic/#`, tiap pesan diparse (`qrtopic`) → divalidasi (`validation`) → device di-resolve dari DB (`resolver`, query langsung tanpa cache) → `transaction.Service` mengorkestrasi: generate `transaction_id` (retry-on-collision), ambil `*manjoclient.Client` dari `manjoclient.Registry` (cache per merchant — supaya `TokenManager` ter-reuse), panggil `GenerateQR` dengan retry policy (401 sekali, timeout/5xx maks 3x, 409/400/404 langsung gagal), update DB, publish balik ke topic yang sama.

**Tech Stack:** `sqlc` (query baru), stdlib `crypto/rand` untuk ID generator, tidak ada dependency baru.

## Global Constraints

- `409` (X-EXTERNAL-ID conflict) **disederhanakan** — langsung `FAILED`, **tanpa** Query Payment reconciliation (belum dibangun)
- `transaction_id` = `TRX-{yyyyMMdd}-{6 karakter random alfanumerik}`, retry generate ulang kalau `UNIQUE` violation
- Device Resolver **query DB langsung**, tanpa cache in-memory
- `manjoclient.Client` **wajib** di-cache per `merchant_id` lewat `Registry` — jangan dibuat ulang per request (`TokenManager` jadi percuma kalau tidak)
- QR_RESULT **dipublish ke topic request masuk** (request-reply), bukan lookup `devices.mqtt_topic` — itu khusus Fase 4 (Payment Notification, trigger dari webhook bukan MQTT)
- Migration lama tidak boleh diedit; kalau ada kebutuhan schema baru, migration baru
- Tidak ada auto-expire job, retry queue durable, atau Query Payment client — itu Fase 5+

---

## File Structure

```
internal/database/queries/
├── merchants.sql              (baru)
├── devices.sql                  (baru)
├── transactions.sql              (baru)
├── mqtt_messages.sql              (baru)
└── manjo_api_logs.sql              (baru)
internal/database/sqlc/            (regenerate)

internal/qrtopic/topic.go            (baru)
internal/qrtopic/topic_test.go        (baru)

internal/validation/validator.go       (baru)
internal/validation/validator_test.go   (baru)

internal/manjoclient/token_manager.go    (modify — +Invalidate())
internal/manjoclient/client.go            (modify — +InvalidateToken(), GenerateQR return externalID)
internal/manjoclient/client_test.go        (modify — sesuaikan signature baru)
internal/manjoclient/signature.go           (modify — +WrapPKCS8PEM)
internal/manjoclient/signature_test.go       (modify — +test WrapPKCS8PEM)
internal/manjoclient/registry.go              (baru)
internal/manjoclient/registry_test.go          (baru)
internal/manjoclient/sandbox_test.go            (modify — pakai WrapPKCS8PEM, signature baru)

internal/resolver/resolver.go       (baru)
internal/resolver/resolver_test.go   (baru, integration — Postgres asli)

internal/transaction/idgen.go        (baru)
internal/transaction/idgen_test.go    (baru)
internal/transaction/service.go        (baru)
internal/transaction/service_test.go    (baru, integration — Postgres asli + mock Manjo)

internal/mqttclient/client.go          (modify — +Publish(), +Subscribe())
internal/mqttclient/client_test.go      (modify)

cmd/server/main.go                       (modify — wire semua, subscribe handler)

migrations/000011_seed_sandbox_merchant.up.sql   (baru)
migrations/000011_seed_sandbox_merchant.down.sql  (baru)
```

---

### Task 1: Database Queries — Regenerate sqlc + Query Baru

**Files:**
- Create: `internal/database/queries/merchants.sql`
- Create: `internal/database/queries/devices.sql`
- Create: `internal/database/queries/transactions.sql`
- Create: `internal/database/queries/mqtt_messages.sql`
- Create: `internal/database/queries/manjo_api_logs.sql`
- Regenerate: `internal/database/sqlc/*.go`

**Interfaces:**
- Consumes: skema final dari migration `000001`-`000010` (sudah termasuk `tenants`/`devices`, `merchants` tanpa 3 kolom lama, `transactions.device_id`)
- Produces: `sqlc.Queries` dengan method baru (`GetMerchantByID`, `GetDeviceWithMerchantAndTenant`, `CreateTransaction`, `MarkTransactionQRGenerated`, `MarkTransactionFailed`, `LogMQTTMessage`, `LogManjoAPICall`) — dipakai Task 5 (`resolver`) dan Task 7 (`transaction.Service`) dan Task 9 (`main.go`)

**Precondition:** `docker compose up -d` (Fase 1) jalan, migration `000001`-`000010` applied.

- [x] **Step 1: Tulis `internal/database/queries/merchants.sql`**

```sql
-- name: GetMerchantByID :one
SELECT * FROM merchants WHERE merchant_id = $1;
```

- [x] **Step 2: Tulis `internal/database/queries/devices.sql`**

```sql
-- name: GetDeviceWithMerchantAndTenant :one
SELECT
    d.device_id,
    d.merchant_id,
    d.tenant_id,
    d.mqtt_topic,
    d.manjo_store_id,
    d.manjo_terminal_id,
    d.status AS device_status,
    m.manjo_client_id,
    m.manjo_private_key_ref,
    m.manjo_client_secret_ref,
    m.manjo_merchant_id,
    m.manjo_channel_id,
    m.status AS merchant_active_status,
    t.manjo_sub_merchant_id,
    t.status AS tenant_active_status
FROM devices d
JOIN merchants m ON m.merchant_id = d.merchant_id
LEFT JOIN tenants t ON t.tenant_id = d.tenant_id
WHERE d.device_id = $1;
```

- [x] **Step 3: Tulis `internal/database/queries/transactions.sql`**

```sql
-- name: CreateTransaction :one
INSERT INTO transactions (transaction_id, merchant_id, device_id, amount, status)
VALUES ($1, $2, $3, $4, 'PENDING')
RETURNING *;

-- name: MarkTransactionQRGenerated :one
UPDATE transactions
SET status = 'QR_GENERATED',
    qris_payload = $2,
    reference_no = $3,
    external_id = $4,
    expire_at = $5,
    updated_at = now()
WHERE transaction_id = $1
RETURNING *;

-- name: MarkTransactionFailed :one
UPDATE transactions
SET status = 'FAILED',
    updated_at = now()
WHERE transaction_id = $1
RETURNING *;
```

- [x] **Step 4: Tulis `internal/database/queries/mqtt_messages.sql`**

```sql
-- name: LogMQTTMessage :one
INSERT INTO mqtt_messages (topic, payload, direction, status, transaction_id, error_message, processed_at)
VALUES ($1, $2, $3, $4, $5, $6, now())
RETURNING *;
```

- [x] **Step 5: Tulis `internal/database/queries/manjo_api_logs.sql`**

```sql
-- name: LogManjoAPICall :one
INSERT INTO manjo_api_logs (direction, operation, endpoint, http_status, request_body, response_body, transaction_id, duration_ms)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
RETURNING *;
```

- [x] **Step 6: Regenerate sqlc**

Run:
```bash
cd /home/akbar/Kerjaan/repository/service-payment-bridge
go run github.com/sqlc-dev/sqlc/cmd/sqlc@latest generate
```
Expected: tidak ada error. `internal/database/sqlc/models.go` sekarang punya struct `Merchant` (tanpa `MqttTopic`/`ManjoStoreID`/`ManjoTerminalID`), `Tenant`, `Device`, `Transaction` (dengan `DeviceID`). File baru `merchants.sql.go`, `devices.sql.go`, `transactions.sql.go`, `mqtt_messages.sql.go`, `manjo_api_logs.sql.go` muncul di `internal/database/sqlc/`.

- [x] **Step 7: Baca hasil generate, catat nama struct/field persis**

Run:
```bash
cat internal/database/sqlc/devices.sql.go internal/database/sqlc/transactions.sql.go
```
Expected: catat nama struct hasil query `GetDeviceWithMerchantAndTenant` (kemungkinan `GetDeviceWithMerchantAndTenantRow`) dan parameter struct `CreateTransaction`/`MarkTransactionQRGenerated` (`CreateTransactionParams`, dst). **Task 5 dan 7 di plan ini ditulis dengan asumsi nama-nama berikut — kalau hasil generate beda, sesuaikan nama di Task 5/7, jangan ubah query di Step 1-5 di atas:**
- `GetDeviceWithMerchantAndTenantRow` dengan field: `DeviceID, MerchantID, TenantID pgtype.Text, MqttTopic, ManjoStoreID pgtype.Text, ManjoTerminalID pgtype.Text, DeviceStatus MerchantStatus, ManjoClientID, ManjoPrivateKeyRef, ManjoClientSecretRef, ManjoMerchantID, ManjoChannelID, MerchantActiveStatus MerchantStatus, ManjoSubMerchantID pgtype.Text, TenantActiveStatus NullMerchantStatus`
- `CreateTransactionParams{TransactionID, MerchantID, DeviceID, Amount string}` (Amount kemungkinan `int64` sesuai kolom `BIGINT`)
- `MarkTransactionQRGeneratedParams{TransactionID string, QrisPayload pgtype.Text, ReferenceNo pgtype.Text, ExternalID pgtype.Text, ExpireAt pgtype.Timestamptz}`

- [x] **Step 8: Verifikasi build**

Run: `go build ./... 2>&1`
Expected: sukses (belum ada kode yang pakai query baru, jadi cuma perlu generated code valid secara sintaks).

---

### Task 2: `internal/qrtopic` — Parse/Build Topic

**Files:**
- Create: `internal/qrtopic/topic.go`
- Test: `internal/qrtopic/topic_test.go`

**Interfaces:**
- Produces: `qrtopic.Parse(topic string) (Topic, error)`, `qrtopic.Build(merchantID, tenantSlot, deviceID string) string`, `qrtopic.Topic{MerchantID, TenantSlot, DeviceID string}`, `qrtopic.TenantSlotNone = "_"` — dipakai `cmd/server/main.go` (Task 9)

- [x] **Step 1: Tulis failing test `internal/qrtopic/topic_test.go`**

```go
package qrtopic

import "testing"

func TestParse_WithTenant(t *testing.T) {
	got, err := Parse("topic/MT82419344/TNT-tokoa/DEV-001")
	if err != nil {
		t.Fatalf("Parse() error = %v", err)
	}
	want := Topic{MerchantID: "MT82419344", TenantSlot: "TNT-tokoa", DeviceID: "DEV-001"}
	if got != want {
		t.Errorf("Parse() = %+v, want %+v", got, want)
	}
}

func TestParse_WithoutTenant_Sentinel(t *testing.T) {
	got, err := Parse("topic/MT82419344/_/DEV-001")
	if err != nil {
		t.Fatalf("Parse() error = %v", err)
	}
	if got.TenantSlot != TenantSlotNone {
		t.Errorf("TenantSlot = %q, want %q", got.TenantSlot, TenantSlotNone)
	}
}

func TestParse_InvalidFormat(t *testing.T) {
	cases := []string{
		"topic/MT82419344",
		"topic/MT82419344/DEV-001",
		"topic/MT82419344/_/DEV-001/extra",
		"wrong/MT82419344/_/DEV-001",
		"topic//_/DEV-001",
	}
	for _, tc := range cases {
		if _, err := Parse(tc); err == nil {
			t.Errorf("Parse(%q) expected error, got nil", tc)
		}
	}
}

func TestBuild_WithTenant(t *testing.T) {
	got := Build("MT82419344", "TNT-tokoa", "DEV-001")
	want := "topic/MT82419344/TNT-tokoa/DEV-001"
	if got != want {
		t.Errorf("Build() = %q, want %q", got, want)
	}
}

func TestBuild_EmptyTenantDefaultsToSentinel(t *testing.T) {
	got := Build("MT82419344", "", "DEV-001")
	want := "topic/MT82419344/_/DEV-001"
	if got != want {
		t.Errorf("Build() = %q, want %q", got, want)
	}
}
```

- [x] **Step 2: Jalankan test, verifikasi gagal**

Run: `go test ./internal/qrtopic/... -v`
Expected: FAIL — package `qrtopic` belum ada `topic.go`.

- [x] **Step 3: Tulis `internal/qrtopic/topic.go`**

```go
package qrtopic

import (
	"fmt"
	"strings"
)

// TenantSlotNone is the sentinel used in place of a real tenant_id when a
// device attaches directly to a merchant without going through a tenant.
const TenantSlotNone = "_"

type Topic struct {
	MerchantID string
	TenantSlot string
	DeviceID   string
}

// Parse splits a topic string of the form topic/{merchant_id}/{tenant_slot}/{device_id}
// (always exactly 4 segments — see architecture.md Section 4/7).
func Parse(topic string) (Topic, error) {
	parts := strings.Split(topic, "/")
	if len(parts) != 4 || parts[0] != "topic" {
		return Topic{}, fmt.Errorf("qrtopic: invalid topic format %q, want topic/{merchant_id}/{tenant_slot}/{device_id}", topic)
	}
	if parts[1] == "" || parts[2] == "" || parts[3] == "" {
		return Topic{}, fmt.Errorf("qrtopic: topic %q has an empty segment", topic)
	}
	return Topic{MerchantID: parts[1], TenantSlot: parts[2], DeviceID: parts[3]}, nil
}

func Build(merchantID, tenantSlot, deviceID string) string {
	if tenantSlot == "" {
		tenantSlot = TenantSlotNone
	}
	return fmt.Sprintf("topic/%s/%s/%s", merchantID, tenantSlot, deviceID)
}
```

- [x] **Step 4: Jalankan test, verifikasi lulus**

Run: `go test ./internal/qrtopic/... -v`
Expected: `PASS` untuk semua 5 test.

---

### Task 3: `internal/validation` — Message Validator

**Files:**
- Create: `internal/validation/validator.go`
- Test: `internal/validation/validator_test.go`

**Interfaces:**
- Produces: `validation.GenerateQRMessage{Type string, Amount int64}`, `validation.ParseAndValidateGenerateQR(payload []byte) (GenerateQRMessage, error)` — dipakai `cmd/server/main.go` (Task 9)

- [x] **Step 1: Tulis failing test `internal/validation/validator_test.go`**

```go
package validation

import "testing"

func TestParseAndValidateGenerateQR_Valid(t *testing.T) {
	msg, err := ParseAndValidateGenerateQR([]byte(`{"type":"GENERATE_QR","amount":50000}`))
	if err != nil {
		t.Fatalf("ParseAndValidateGenerateQR() error = %v", err)
	}
	if msg.Amount != 50000 {
		t.Errorf("Amount = %d, want 50000", msg.Amount)
	}
}

func TestParseAndValidateGenerateQR_InvalidJSON(t *testing.T) {
	_, err := ParseAndValidateGenerateQR([]byte(`not json`))
	if err == nil {
		t.Fatal("expected error for invalid JSON, got nil")
	}
}

func TestParseAndValidateGenerateQR_WrongType(t *testing.T) {
	_, err := ParseAndValidateGenerateQR([]byte(`{"type":"SOMETHING_ELSE","amount":50000}`))
	if err == nil {
		t.Fatal("expected error for wrong type, got nil")
	}
}

func TestParseAndValidateGenerateQR_ZeroAmount(t *testing.T) {
	_, err := ParseAndValidateGenerateQR([]byte(`{"type":"GENERATE_QR","amount":0}`))
	if err == nil {
		t.Fatal("expected error for amount=0, got nil")
	}
}

func TestParseAndValidateGenerateQR_NegativeAmount(t *testing.T) {
	_, err := ParseAndValidateGenerateQR([]byte(`{"type":"GENERATE_QR","amount":-1000}`))
	if err == nil {
		t.Fatal("expected error for negative amount, got nil")
	}
}
```

- [x] **Step 2: Jalankan test, verifikasi gagal**

Run: `go test ./internal/validation/... -v`
Expected: FAIL — `ParseAndValidateGenerateQR` undefined.

- [x] **Step 3: Tulis `internal/validation/validator.go`**

```go
package validation

import (
	"encoding/json"
	"fmt"
)

type GenerateQRMessage struct {
	Type   string `json:"type"`
	Amount int64  `json:"amount"`
}

// ParseAndValidateGenerateQR decodes and validates a GENERATE_QR payload
// from Q161 (architecture.md Section 7.1): type must be "GENERATE_QR",
// amount must be a positive integer.
func ParseAndValidateGenerateQR(payload []byte) (GenerateQRMessage, error) {
	var msg GenerateQRMessage
	if err := json.Unmarshal(payload, &msg); err != nil {
		return GenerateQRMessage{}, fmt.Errorf("validation: invalid JSON payload: %w", err)
	}
	if msg.Type != "GENERATE_QR" {
		return GenerateQRMessage{}, fmt.Errorf("validation: unexpected type %q, want GENERATE_QR", msg.Type)
	}
	if msg.Amount <= 0 {
		return GenerateQRMessage{}, fmt.Errorf("validation: amount must be > 0, got %d", msg.Amount)
	}
	return msg, nil
}
```

- [x] **Step 4: Jalankan test, verifikasi lulus**

Run: `go test ./internal/validation/... -v`
Expected: `PASS` untuk semua 5 test.

---

### Task 4: Extend `internal/manjoclient` — Invalidate, Registry, WrapPKCS8PEM, externalID Return

**Files:**
- Modify: `internal/manjoclient/token_manager.go`
- Modify: `internal/manjoclient/client.go`
- Modify: `internal/manjoclient/client_test.go`
- Modify: `internal/manjoclient/signature.go`
- Modify: `internal/manjoclient/signature_test.go`
- Create: `internal/manjoclient/registry.go`
- Test: `internal/manjoclient/registry_test.go`
- Modify: `internal/manjoclient/sandbox_test.go`

**Interfaces:**
- Produces: `(*TokenManager).Invalidate()`, `(*Client).InvalidateToken()`, `manjoclient.WrapPKCS8PEM(raw string) ([]byte, error)`, `manjoclient.NewRegistry() *Registry`, `(*Registry).GetOrCreate(merchantID string, buildConfig func() (Config, error)) (*Client, error)`, **`(*Client).GenerateQR` sekarang return `(*GenerateQRResponse, string, error)`** (externalID sebagai return kedua) — dipakai `internal/transaction/service.go` (Task 7)

**Catatan:** perubahan signature `GenerateQR` dibutuhkan karena `transactions.external_id` (schema) perlu tahu X-EXTERNAL-ID mana yang benar-benar dipakai saat sukses — sebelumnya digenerate internal tanpa diekspos ke caller.

- [x] **Step 1: Tambah `Invalidate()` di `internal/manjoclient/token_manager.go`**

Tambahkan method baru di akhir file (tidak mengubah kode existing):

```go
// Invalidate clears the cached token, forcing the next Get() to fetch a
// fresh one — needed when Manjo rejects a cached-but-not-yet-expired token
// with 401 (architecture.md Section 13.1).
func (tm *TokenManager) Invalidate() {
	tm.mu.Lock()
	defer tm.mu.Unlock()
	tm.token = ""
	tm.expiresAt = time.Time{}
}
```

- [x] **Step 2: Tulis failing test untuk `WrapPKCS8PEM` — tambahkan ke `internal/manjoclient/signature_test.go`**

Tambahkan di akhir file:

```go
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
```

Tambahkan import `"encoding/base64"` ke bagian import `signature_test.go`.

- [x] **Step 3: Jalankan test, verifikasi gagal**

Run: `go test ./internal/manjoclient/... -v -run TestWrapPKCS8PEM`
Expected: FAIL — `WrapPKCS8PEM` undefined.

- [x] **Step 4: Tambah `WrapPKCS8PEM` di `internal/manjoclient/signature.go`**

Tambahkan di akhir file (tambahkan `"encoding/base64"` dan `"strings"` ke import):

```go
// WrapPKCS8PEM wraps a raw base64 PKCS8 key (no PEM headers, possibly with
// embedded whitespace — the format Manjo credentials are typically shared
// in) into a proper PEM block ready for SignAccessToken.
func WrapPKCS8PEM(raw string) ([]byte, error) {
	clean := strings.Join(strings.Fields(raw), "")
	decoded, err := base64.StdEncoding.DecodeString(clean)
	if err != nil {
		return nil, fmt.Errorf("manjoclient: failed to decode base64 private key: %w", err)
	}
	return pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: decoded}), nil
}
```

- [x] **Step 5: Jalankan test, verifikasi lulus**

Run: `go test ./internal/manjoclient/... -v -run TestWrapPKCS8PEM`
Expected: `PASS` untuk kedua test.

- [x] **Step 6: Tulis failing test `internal/manjoclient/registry_test.go`**

```go
package manjoclient

import (
	"sync"
	"sync/atomic"
	"testing"
)

func TestRegistry_CachesClientPerMerchant(t *testing.T) {
	r := NewRegistry()
	var buildCount int32

	build := func() (Config, error) {
		atomic.AddInt32(&buildCount, 1)
		return Config{ClientKey: "k"}, nil
	}

	c1, err := r.GetOrCreate("MT001", build)
	if err != nil {
		t.Fatalf("GetOrCreate() error = %v", err)
	}
	c2, err := r.GetOrCreate("MT001", build)
	if err != nil {
		t.Fatalf("GetOrCreate() error = %v", err)
	}

	if c1 != c2 {
		t.Error("GetOrCreate() returned different *Client for same merchant")
	}
	if atomic.LoadInt32(&buildCount) != 1 {
		t.Errorf("build called %d times, want 1", buildCount)
	}
}

func TestRegistry_DifferentMerchantsGetDifferentClients(t *testing.T) {
	r := NewRegistry()

	c1, _ := r.GetOrCreate("MT001", func() (Config, error) { return Config{ClientKey: "k1"}, nil })
	c2, _ := r.GetOrCreate("MT002", func() (Config, error) { return Config{ClientKey: "k2"}, nil })

	if c1 == c2 {
		t.Error("GetOrCreate() returned same *Client for different merchants")
	}
}

func TestRegistry_ConcurrentGetOrCreate_BuildsOncePerMerchant(t *testing.T) {
	r := NewRegistry()
	var buildCount int32

	var wg sync.WaitGroup
	for i := 0; i < 20; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			_, err := r.GetOrCreate("MT001", func() (Config, error) {
				atomic.AddInt32(&buildCount, 1)
				return Config{ClientKey: "k"}, nil
			})
			if err != nil {
				t.Errorf("GetOrCreate() error = %v", err)
			}
		}()
	}
	wg.Wait()

	if atomic.LoadInt32(&buildCount) != 1 {
		t.Errorf("build called %d times under concurrent load, want 1", buildCount)
	}
}
```

- [x] **Step 7: Jalankan test, verifikasi gagal**

Run: `go test ./internal/manjoclient/... -v -run TestRegistry`
Expected: FAIL — `NewRegistry` undefined.

- [x] **Step 8: Tulis `internal/manjoclient/registry.go`**

```go
package manjoclient

import "sync"

// Registry lazily creates and caches one Client per merchant, so each
// merchant's TokenManager (and its cached access token) is reused across
// requests instead of being rebuilt on every call — rebuilding per request
// would force a fresh access-token fetch every time, defeating the cache.
type Registry struct {
	mu      sync.Mutex
	clients map[string]*Client
}

func NewRegistry() *Registry {
	return &Registry{clients: make(map[string]*Client)}
}

// GetOrCreate returns the cached Client for merchantID, or builds one via
// buildConfig (invoked only on cache miss, while holding the lock so
// concurrent callers for the same merchant never build twice) and caches it.
func (r *Registry) GetOrCreate(merchantID string, buildConfig func() (Config, error)) (*Client, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	if c, ok := r.clients[merchantID]; ok {
		return c, nil
	}

	cfg, err := buildConfig()
	if err != nil {
		return nil, err
	}

	c := New(cfg)
	r.clients[merchantID] = c
	return c, nil
}
```

- [x] **Step 9: Jalankan test, verifikasi lulus dengan `-race`**

Run: `go test ./internal/manjoclient/... -v -race -run TestRegistry`
Expected: `PASS` untuk ketiga test, tanpa race warning.

- [x] **Step 10: Ubah signature `GenerateQR` di `internal/manjoclient/client.go`**

Ganti method `GenerateQR` (signature dan isi) dengan:

```go
// GenerateQR calls POST /v1.0/qr/qr-mpm-generate, obtaining a valid access
// token from the cache automatically. Returns the X-EXTERNAL-ID that was
// used for this attempt (callers persist it for tracing — schema.md
// transactions.external_id) alongside the response.
func (c *Client) GenerateQR(ctx context.Context, req GenerateQRRequest) (*GenerateQRResponse, string, error) {
	token, err := c.tokenManager.Get(ctx)
	if err != nil {
		return nil, "", fmt.Errorf("manjoclient: failed to obtain access token: %w", err)
	}

	externalID, err := generateExternalID(35)
	if err != nil {
		return nil, "", err
	}

	body, err := json.Marshal(req)
	if err != nil {
		return nil, "", fmt.Errorf("manjoclient: failed to marshal generate QR request: %w", err)
	}

	timestamp := NowJakarta()
	const path = "/v1.0/qr/qr-mpm-generate"

	signature, err := SignHMAC(c.cfg.ClientSecret, http.MethodPost, path, token, body, timestamp)
	if err != nil {
		return nil, "", err
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.cfg.BaseURL+path, bytes.NewReader(body))
	if err != nil {
		return nil, "", err
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
		return nil, externalID, fmt.Errorf("manjoclient: generate QR request failed: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, externalID, fmt.Errorf("manjoclient: failed to read generate QR response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return nil, externalID, &APIError{StatusCode: resp.StatusCode, Body: string(respBody)}
	}

	var qrResp GenerateQRResponse
	if err := json.Unmarshal(respBody, &qrResp); err != nil {
		return nil, externalID, fmt.Errorf("manjoclient: failed to parse generate QR response: %w", err)
	}

	return &qrResp, externalID, nil
}
```

Tambahkan method `InvalidateToken` setelah `GenerateQR`:

```go
// InvalidateToken forces the next call through TokenManager to fetch a
// fresh access token — used after a 401 on a call that used a cached token
// (architecture.md Section 13.1: refresh token, retry once).
func (c *Client) InvalidateToken() {
	c.tokenManager.Invalidate()
}
```

- [x] **Step 11: Update `internal/manjoclient/client_test.go` untuk signature baru**

Ganti pemanggilan `c.GenerateQR(...)` di `TestGenerateQR_Success` dari:
```go
	resp, err := c.GenerateQR(context.Background(), GenerateQRRequest{
```
menjadi:
```go
	resp, externalID, err := c.GenerateQR(context.Background(), GenerateQRRequest{
```
dan tambahkan assertion setelah pengecekan `err`:
```go
	if externalID == "" {
		t.Error("externalID is empty")
	}
```

Ganti pemanggilan di `TestGenerateQR_Conflict409` dari:
```go
	_, err := c.GenerateQR(context.Background(), GenerateQRRequest{
```
menjadi:
```go
	_, _, err := c.GenerateQR(context.Background(), GenerateQRRequest{
```

- [x] **Step 12: Jalankan seluruh test manjoclient, verifikasi lulus**

Run: `go test ./internal/manjoclient/... -v -race`
Expected: semua test `PASS` (termasuk yang sudah ada dari Fase 2), tidak ada race warning. Test `TestSandbox_*` di-`SKIP` (belum diset `RUN_SANDBOX_TESTS`) — **akan diperbaiki Step 13** karena belum sesuai signature baru dan masih pakai `wrapPKCS8PEM` lokal.

- [x] **Step 13: Update `internal/manjoclient/sandbox_test.go`**

Ganti bagian ini:
```go
		PrivateKeyPEM: wrapPKCS8PEM(t, privateKeyRaw),
```
menjadi:
```go
		PrivateKeyPEM: mustWrapPKCS8PEM(t, privateKeyRaw),
```

Ganti pemanggilan:
```go
	resp, err := c.GenerateQR(reqCtx, GenerateQRRequest{
```
menjadi:
```go
	resp, externalID, err := c.GenerateQR(reqCtx, GenerateQRRequest{
```
dan tambahkan setelah log `qrContent`:
```go
	t.Logf("externalID used=%s", externalID)
```

Hapus definisi fungsi `wrapPKCS8PEM` yang lama di akhir file, ganti dengan wrapper tipis yang memanggil fungsi exported baru:
```go
func mustWrapPKCS8PEM(t *testing.T, raw string) []byte {
	t.Helper()
	pemBytes, err := WrapPKCS8PEM(raw)
	if err != nil {
		t.Fatalf("WrapPKCS8PEM: %v", err)
	}
	return pemBytes
}
```

Hapus import `"encoding/base64"` dan `"encoding/pem"` dari `sandbox_test.go` kalau sudah tidak dipakai di tempat lain dalam file itu (cek dengan `goimports`/compile error).

- [x] **Step 14: Verifikasi sandbox test masih jalan (real, pakai `.env.sandbox`)**

Run:
```bash
set -a && source .env.sandbox && set +a
export RUN_SANDBOX_TESTS=1
go test ./internal/manjoclient/... -v -run TestSandbox
```
Expected: `PASS`, sama seperti hasil Fase 2 (`qrContent` diterima, sekarang juga log `externalID used=...`).

---

### Task 5: `internal/resolver` — Device Resolver

**Files:**
- Create: `internal/resolver/resolver.go`
- Test: `internal/resolver/resolver_test.go` (integration, Postgres asli dari Fase 1)

**Interfaces:**
- Consumes: `sqlc.Queries.GetDeviceWithMerchantAndTenant` (Task 1)
- Produces: `resolver.ResolvedDevice{DeviceID, MerchantID string; TenantID *string; MQTTTopic, ManjoClientID, ManjoPrivateKeyRef, ManjoClientSecretRef, ManjoMerchantID, ManjoChannelID string; ManjoStoreID, ManjoTerminalID, ManjoSubMerchantID *string; DeviceActive, MerchantActive, TenantActive bool}`, `resolver.New(q *sqlc.Queries) *Resolver`, `(*Resolver).ResolveDevice(ctx, deviceID string) (*ResolvedDevice, error)` — dipakai `cmd/server/main.go` (Task 9)

**Precondition:** Task 1 selesai (sqlc regenerated), Postgres (Fase 1 `docker compose`) jalan dengan skema ter-migrasi.

- [x] **Step 1: Tulis failing test `internal/resolver/resolver_test.go`**

```go
package resolver

import (
	"context"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgtype"

	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
)

func setupTestDB(t *testing.T) *sqlc.Queries {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	pool, err := database.NewPool(ctx, "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable")
	if err != nil {
		t.Fatalf("failed to connect to test database (pastikan `docker compose up -d postgres` jalan): %v", err)
	}
	t.Cleanup(pool.Close)

	return sqlc.New(pool)
}

func insertTestMerchant(t *testing.T, q *sqlc.Queries, merchantID string) {
	t.Helper()
	ctx := context.Background()
	_, err := q.Exec(ctx, `
		INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
		VALUES ($1, 'test-client-id', 'TEST_PRIVATE_KEY_REF', 'TEST_CLIENT_SECRET_REF', $1, '05')
		ON CONFLICT (merchant_id) DO NOTHING`, merchantID)
	if err != nil {
		t.Fatalf("failed to insert test merchant: %v", err)
	}
}
```

**Catatan implementasi:** `sqlc.Queries` tidak punya method generik `Exec` untuk raw SQL setup di test — Step 1 di atas perlu disesuaikan memakai pool `pgx` langsung (bukan lewat `sqlc.Queries`) untuk insert data uji. Tulis ulang helper `insertTestMerchant`/`insertTestDevice` memakai `pool.Exec(ctx, ...)` (pool didapat dari `database.NewPool`, disimpan terpisah dari `sqlc.Queries` di `setupTestDB`, dikembalikan sebagai pasangan `(*pgxpool.Pool, *sqlc.Queries)`), lalu lengkapi test berikut:

```go
func TestResolveDevice_Found(t *testing.T) {
	pool, err := database.NewPool(context.Background(), "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable")
	if err != nil {
		t.Fatalf("failed to connect to test database: %v", err)
	}
	t.Cleanup(pool.Close)
	q := sqlc.New(pool)

	ctx := context.Background()
	merchantID := "RESOLVER-TEST-MERCHANT"
	deviceID := "RESOLVER-TEST-DEVICE"

	_, err = pool.Exec(ctx, `
		INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
		VALUES ($1, 'test-client-id', 'TEST_PRIVATE_KEY_REF', 'TEST_CLIENT_SECRET_REF', $1, '05')
		ON CONFLICT (merchant_id) DO NOTHING`, merchantID)
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
		pool.Exec(context.Background(), `DELETE FROM devices WHERE device_id = $1`, deviceID)
		pool.Exec(context.Background(), `DELETE FROM merchants WHERE merchant_id = $1`, merchantID)
	})

	r := New(q)
	got, err := r.ResolveDevice(ctx, deviceID)
	if err != nil {
		t.Fatalf("ResolveDevice() error = %v", err)
	}
	if got.MerchantID != merchantID {
		t.Errorf("MerchantID = %q, want %q", got.MerchantID, merchantID)
	}
	if got.TenantID != nil {
		t.Errorf("TenantID = %v, want nil (device tanpa tenant)", got.TenantID)
	}
	if !got.DeviceActive || !got.MerchantActive {
		t.Error("DeviceActive/MerchantActive = false, want true")
	}
}

func TestResolveDevice_NotFound(t *testing.T) {
	pool, err := database.NewPool(context.Background(), "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable")
	if err != nil {
		t.Fatalf("failed to connect to test database: %v", err)
	}
	t.Cleanup(pool.Close)
	q := sqlc.New(pool)

	r := New(q)
	_, err = r.ResolveDevice(context.Background(), "DOES-NOT-EXIST-DEVICE-ID")
	if err == nil {
		t.Fatal("ResolveDevice() expected error for unknown device, got nil")
	}
}
```

(Hapus helper `setupTestDB`/`insertTestMerchant` yang di Step 1 di atas kalau tidak dipakai — dua test di atas sudah self-contained.)

Tambahkan import `pgtype` hanya kalau dipakai; kalau tidak, hapus dari import list supaya tidak ada unused import error.

- [x] **Step 2: Jalankan test, verifikasi gagal**

Run: `go test ./internal/resolver/... -v`
Expected: FAIL — `New`/`ResolveDevice` undefined.

- [x] **Step 3: Tulis `internal/resolver/resolver.go`**

Sesuaikan nama field row struct (`GetDeviceWithMerchantAndTenantRow`) dengan hasil aktual dari Task 1 Step 7 kalau berbeda dari yang diasumsikan di bawah.

```go
package resolver

import (
	"context"
	"fmt"

	"service-payment-bridge/internal/database/sqlc"
)

type ResolvedDevice struct {
	DeviceID             string
	MerchantID           string
	TenantID             *string
	MQTTTopic            string
	ManjoClientID        string
	ManjoPrivateKeyRef   string
	ManjoClientSecretRef string
	ManjoMerchantID      string
	ManjoChannelID       string
	ManjoStoreID         *string
	ManjoTerminalID      *string
	ManjoSubMerchantID   *string
	DeviceActive         bool
	MerchantActive       bool
	TenantActive         bool // true kalau tidak ada tenant sama sekali (tidak relevan)
}

type Resolver struct {
	q *sqlc.Queries
}

func New(q *sqlc.Queries) *Resolver {
	return &Resolver{q: q}
}

func (r *Resolver) ResolveDevice(ctx context.Context, deviceID string) (*ResolvedDevice, error) {
	row, err := r.q.GetDeviceWithMerchantAndTenant(ctx, deviceID)
	if err != nil {
		return nil, fmt.Errorf("resolver: device %q not found: %w", deviceID, err)
	}

	result := &ResolvedDevice{
		DeviceID:             row.DeviceID,
		MerchantID:           row.MerchantID,
		MQTTTopic:            row.MqttTopic,
		ManjoClientID:        row.ManjoClientID,
		ManjoPrivateKeyRef:   row.ManjoPrivateKeyRef,
		ManjoClientSecretRef: row.ManjoClientSecretRef,
		ManjoMerchantID:      row.ManjoMerchantID,
		ManjoChannelID:       row.ManjoChannelID,
		DeviceActive:         row.DeviceStatus == sqlc.MerchantStatusACTIVE,
		MerchantActive:       row.MerchantActiveStatus == sqlc.MerchantStatusACTIVE,
		TenantActive:         true,
	}

	if row.TenantID.Valid {
		v := row.TenantID.String
		result.TenantID = &v
	}
	if row.ManjoStoreID.Valid {
		v := row.ManjoStoreID.String
		result.ManjoStoreID = &v
	}
	if row.ManjoTerminalID.Valid {
		v := row.ManjoTerminalID.String
		result.ManjoTerminalID = &v
	}
	if row.ManjoSubMerchantID.Valid {
		v := row.ManjoSubMerchantID.String
		result.ManjoSubMerchantID = &v
	}
	if result.TenantID != nil {
		result.TenantActive = row.TenantActiveStatus.Valid && row.TenantActiveStatus.MerchantStatus == sqlc.MerchantStatusACTIVE
	}

	return result, nil
}
```

- [x] **Step 4: Jalankan test, verifikasi lulus**

Run: `go test ./internal/resolver/... -v`
Expected: `PASS` untuk `TestResolveDevice_Found` dan `TestResolveDevice_NotFound`.

---

### Task 6: `internal/transaction/idgen.go`

**Files:**
- Create: `internal/transaction/idgen.go`
- Test: `internal/transaction/idgen_test.go`

**Interfaces:**
- Produces: `transaction.generateTransactionID(now time.Time) (string, error)` (unexported, dipakai `service.go` di package yang sama, Task 7)

- [x] **Step 1: Tulis failing test `internal/transaction/idgen_test.go`**

```go
package transaction

import (
	"regexp"
	"testing"
	"time"
)

func TestGenerateTransactionID_Format(t *testing.T) {
	now := time.Date(2026, 9, 25, 10, 0, 0, 0, time.UTC)
	id, err := generateTransactionID(now)
	if err != nil {
		t.Fatalf("generateTransactionID() error = %v", err)
	}
	pattern := `^TRX-20260925-[A-Z0-9]{6}$`
	matched, _ := regexp.MatchString(pattern, id)
	if !matched {
		t.Errorf("generateTransactionID() = %q, does not match pattern %q", id, pattern)
	}
}

func TestGenerateTransactionID_Unique(t *testing.T) {
	now := time.Now()
	seen := make(map[string]bool)
	for i := 0; i < 1000; i++ {
		id, err := generateTransactionID(now)
		if err != nil {
			t.Fatalf("generateTransactionID() error = %v", err)
		}
		if seen[id] {
			t.Fatalf("generateTransactionID() produced duplicate: %s", id)
		}
		seen[id] = true
	}
}
```

- [x] **Step 2: Jalankan test, verifikasi gagal**

Run: `go test ./internal/transaction/... -v`
Expected: FAIL — `generateTransactionID` undefined.

- [x] **Step 3: Tulis `internal/transaction/idgen.go`**

```go
package transaction

import (
	"crypto/rand"
	"fmt"
	"time"
)

// generateTransactionID produces TRX-{yyyyMMdd}-{6 char random alphanumeric}.
// Collision probability is negligible (36^6 per day); on the rare UNIQUE
// violation, callers regenerate and retry the insert (process-flow.md
// Flow 1 step 6).
func generateTransactionID(now time.Time) (string, error) {
	const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	randomBytes := make([]byte, 6)
	if _, err := rand.Read(randomBytes); err != nil {
		return "", fmt.Errorf("transaction: failed to generate id suffix: %w", err)
	}
	b := make([]byte, 6)
	for i, rb := range randomBytes {
		b[i] = alphabet[int(rb)%len(alphabet)]
	}
	return fmt.Sprintf("TRX-%s-%s", now.Format("20060102"), string(b)), nil
}
```

- [x] **Step 4: Jalankan test, verifikasi lulus**

Run: `go test ./internal/transaction/... -v`
Expected: `PASS` untuk kedua test.

---

### Task 7: `internal/transaction/service.go` — Transaction Service

**Files:**
- Create: `internal/transaction/service.go`
- Test: `internal/transaction/service_test.go` (integration — Postgres asli + mock Manjo `httptest`)

**Interfaces:**
- Consumes: `sqlc.Queries` (Task 1), `resolver.ResolvedDevice` (Task 5), `manjoclient.Registry`/`Config`/`Client` (Task 4), `secrets.Provider` (Fase 2)
- Produces: `transaction.NewService(q *sqlc.Queries, registry *manjoclient.Registry, secretProvider secrets.Provider) *Service`, `(*Service).GenerateQR(ctx, device resolver.ResolvedDevice, amount int64) (*GenerateQRResult, error)`, `transaction.GenerateQRResult{TransactionID, Status, QRISPayload, ErrorCode string; ExpireAt time.Time}` — dipakai `cmd/server/main.go` (Task 9)

**Precondition:** Task 1, 4, 5, 6 selesai. Postgres (Fase 1) jalan.

- [x] **Step 1: Tulis failing test `internal/transaction/service_test.go`**

```go
package transaction

import (
	"context"
	"encoding/json"
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

	t.Setenv("TXSVC_TEST_PRIVATE_KEY", "") // placeholder env, overridden below via os.Setenv-equivalent
	privateKeyRawEnv := "TXSVC_TEST_PRIVATE_KEY_REF"
	secretEnv := "TXSVC_TEST_SECRET_REF"

	testKeyPEM, testKeyRawBase64 := testPrivateKeyForService(t)
	_ = testKeyPEM
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
```

**Catatan implementasi:** helper `testPrivateKeyForService` dan `NewServiceWithBaseURL` didefinisikan di Step 3/5 di bawah — `NewServiceWithBaseURL` adalah varian `NewService` untuk test yang menyuntikkan `baseURL` custom (mock server) ke `Config` yang dibangun `buildManjoConfig`, karena `manjoclient.Config.BaseURL` tidak datang dari DB. Lanjutkan file test dengan:

```go
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
```

Import list yang dibutuhkan di awal file (lengkapi bagian `import (...)`): `"context"`, `"crypto/rand"`, `"crypto/rsa"`, `"crypto/x509"`, `"encoding/base64"`, `"encoding/json"`, `"encoding/pem"`, `"net/http"`, `"net/http/httptest"`, `"testing"`, `"time"`, plus keempat package internal di atas.

- [x] **Step 2: Jalankan test, verifikasi gagal**

Run: `go test ./internal/transaction/... -v -run TestGenerateQR`
Expected: FAIL — `NewServiceWithBaseURL`, `Service`, `GenerateQR`, `q.GetTransactionByID` undefined.

**Catatan:** `GetTransactionByID` dipakai test tapi belum ada di Task 1 — tambahkan query ini sekarang (bukan pelanggaran urutan task, murni tambahan kecil untuk kebutuhan verifikasi test) ke `internal/database/queries/transactions.sql`:

```sql
-- name: GetTransactionByID :one
SELECT * FROM transactions WHERE transaction_id = $1;
```

Jalankan `go run github.com/sqlc-dev/sqlc/cmd/sqlc@latest generate` ulang sebelum lanjut ke Step 3.

- [x] **Step 3: Tulis `internal/transaction/service.go`**

```go
package transaction

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"time"

	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgtype"

	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/resolver"
	"service-payment-bridge/internal/secrets"
)

const (
	maxCreateRetries    = 5
	maxTransientRetries = 3
)

type Service struct {
	q        *sqlc.Queries
	registry *manjoclient.Registry
	secrets  secrets.Provider
	baseURL  string // "" berarti pakai default manjoclient.Client
	now      func() time.Time
}

func NewService(q *sqlc.Queries, registry *manjoclient.Registry, secretProvider secrets.Provider) *Service {
	return &Service{q: q, registry: registry, secrets: secretProvider, now: time.Now}
}

// NewServiceWithBaseURL is NewService but overrides the Manjo base URL for
// every merchant Config built by this Service — used by tests to point at
// an httptest mock server instead of the real Manjo host.
func NewServiceWithBaseURL(q *sqlc.Queries, registry *manjoclient.Registry, secretProvider secrets.Provider, baseURL string) *Service {
	s := NewService(q, registry, secretProvider)
	s.baseURL = baseURL
	return s
}

type GenerateQRResult struct {
	TransactionID string
	Status        string // "SUCCESS" | "FAILED"
	QRISPayload   string
	ExpireAt      time.Time
	ErrorCode     string // populated when Status == "FAILED"
}

func (s *Service) GenerateQR(ctx context.Context, device resolver.ResolvedDevice, amount int64) (*GenerateQRResult, error) {
	transactionID, err := s.createTransaction(ctx, device, amount)
	if err != nil {
		return nil, err
	}

	client, err := s.registry.GetOrCreate(device.MerchantID, func() (manjoclient.Config, error) {
		return s.buildManjoConfig(ctx, device)
	})
	if err != nil {
		_ = s.markFailed(ctx, transactionID)
		return &GenerateQRResult{TransactionID: transactionID, Status: "FAILED", ErrorCode: "CONFIG_ERROR"}, nil
	}

	req := buildGenerateQRRequest(device, transactionID, amount)

	start := s.now()
	resp, externalID, errCode := s.callGenerateQRWithRetry(ctx, client, req)
	s.logManjoAPICall(ctx, transactionID, req, resp, errCode, s.now().Sub(start))

	if errCode != "" {
		_ = s.markFailed(ctx, transactionID)
		return &GenerateQRResult{TransactionID: transactionID, Status: "FAILED", ErrorCode: errCode}, nil
	}

	expireAt, err := parseExpireDate(resp.AdditionalInfo.ExpireDate, s.now())
	if err != nil {
		expireAt = s.now().Add(time.Hour)
	}

	if err := s.markQRGenerated(ctx, transactionID, externalID, resp, expireAt); err != nil {
		return nil, fmt.Errorf("transaction: failed to persist QR_GENERATED: %w", err)
	}

	return &GenerateQRResult{
		TransactionID: transactionID,
		Status:        "SUCCESS",
		QRISPayload:   resp.QRContent,
		ExpireAt:      expireAt,
	}, nil
}

// logManjoAPICall records one row per GenerateQR() call (summarizing the
// whole retry sequence, not per HTTP attempt — full per-attempt tracing is
// Fase 5 Hardening scope). Request/response bodies never contain secrets
// (signature/token live in headers, not JSON body), so no masking is
// needed here.
func (s *Service) logManjoAPICall(ctx context.Context, transactionID string, req manjoclient.GenerateQRRequest, resp *manjoclient.GenerateQRResponse, errCode string, duration time.Duration) {
	reqBody, _ := json.Marshal(req)

	var respBody []byte
	httpStatus := pgtype.Int4{}
	if errCode == "" && resp != nil {
		respBody, _ = json.Marshal(resp)
		httpStatus = pgtype.Int4{Int32: 200, Valid: true}
	} else {
		respBody, _ = json.Marshal(map[string]string{"error_code": errCode})
	}

	_, err := s.q.LogManjoAPICall(ctx, sqlc.LogManjoAPICallParams{
		Direction:     sqlc.ManjoApiDirectionOUTBOUND,
		Operation:     sqlc.ManjoApiOperationGENERATEQR,
		Endpoint:      "/v1.0/qr/qr-mpm-generate",
		HttpStatus:    httpStatus,
		RequestBody:   reqBody,
		ResponseBody:  respBody,
		TransactionID: pgtype.Text{String: transactionID, Valid: true},
		DurationMs:    pgtype.Int4{Int32: int32(duration.Milliseconds()), Valid: true},
	})
	if err != nil {
		// Logging failure must not break the Generate QR flow itself. Service
		// has no logger reference in this phase (kept minimal) — the failure
		// is silently swallowed; acceptable trade-off for Fase 3 scope.
		_ = err
	}
}

func (s *Service) createTransaction(ctx context.Context, device resolver.ResolvedDevice, amount int64) (string, error) {
	for attempt := 0; attempt < maxCreateRetries; attempt++ {
		id, err := generateTransactionID(s.now())
		if err != nil {
			return "", err
		}

		_, err = s.q.CreateTransaction(ctx, sqlc.CreateTransactionParams{
			TransactionID: id,
			MerchantID:    device.MerchantID,
			DeviceID:      device.DeviceID,
			Amount:        amount,
		})
		if err == nil {
			return id, nil
		}
		if !isUniqueViolation(err) {
			return "", fmt.Errorf("transaction: failed to create transaction: %w", err)
		}
	}
	return "", fmt.Errorf("transaction: exhausted %d attempts to generate unique transaction_id", maxCreateRetries)
}

func isUniqueViolation(err error) bool {
	var pgErr *pgconn.PgError
	return errors.As(err, &pgErr) && pgErr.Code == "23505"
}

func (s *Service) buildManjoConfig(ctx context.Context, device resolver.ResolvedDevice) (manjoclient.Config, error) {
	privateKeyRaw, err := s.secrets.Resolve(ctx, device.ManjoPrivateKeyRef)
	if err != nil {
		return manjoclient.Config{}, fmt.Errorf("transaction: failed to resolve private key: %w", err)
	}
	clientSecret, err := s.secrets.Resolve(ctx, device.ManjoClientSecretRef)
	if err != nil {
		return manjoclient.Config{}, fmt.Errorf("transaction: failed to resolve client secret: %w", err)
	}
	privateKeyPEM, err := manjoclient.WrapPKCS8PEM(privateKeyRaw)
	if err != nil {
		return manjoclient.Config{}, err
	}

	return manjoclient.Config{
		BaseURL:       s.baseURL,
		ClientKey:     device.ManjoClientID,
		PrivateKeyPEM: privateKeyPEM,
		ClientSecret:  clientSecret,
		PartnerID:     device.ManjoMerchantID,
		ChannelID:     device.ManjoChannelID,
	}, nil
}

func buildGenerateQRRequest(device resolver.ResolvedDevice, transactionID string, amount int64) manjoclient.GenerateQRRequest {
	req := manjoclient.GenerateQRRequest{
		PartnerReferenceNo: transactionID,
		Amount:             manjoclient.Amount{Value: fmt.Sprintf("%d.00", amount), Currency: "IDR"},
		MerchantID:         device.ManjoMerchantID,
		ValidityPeriod:     "3600",
		AdditionalInfo: manjoclient.GenerateQRAdditionalInfo{
			PaymentID:     "99",
			DynamicAmount: "N",
			ProdDesc:      "Q161 Payment",
		},
	}
	if device.ManjoSubMerchantID != nil {
		req.SubMerchantID = *device.ManjoSubMerchantID
	}
	if device.ManjoStoreID != nil {
		req.StoreID = *device.ManjoStoreID
	}
	if device.ManjoTerminalID != nil {
		req.TerminalID = *device.ManjoTerminalID
	}
	return req
}

// callGenerateQRWithRetry implements architecture.md Section 13.1: 401 ->
// invalidate cached token, retry once; 400/404/409 -> no retry, fail
// immediately; timeout/5xx/network error -> retry up to maxTransientRetries
// total attempts.
func (s *Service) callGenerateQRWithRetry(ctx context.Context, client *manjoclient.Client, req manjoclient.GenerateQRRequest) (*manjoclient.GenerateQRResponse, string, string) {
	resp, externalID, err := client.GenerateQR(ctx, req)
	if err == nil {
		return resp, externalID, ""
	}

	if isUnauthorized(err) {
		client.InvalidateToken()
		resp, externalID, err = client.GenerateQR(ctx, req)
		if err == nil {
			return resp, externalID, ""
		}
	}

	if code := nonRetryableCode(err); code != "" {
		return nil, externalID, code
	}

	for attempt := 1; attempt < maxTransientRetries; attempt++ {
		resp, externalID, err = client.GenerateQR(ctx, req)
		if err == nil {
			return resp, externalID, ""
		}
		if code := nonRetryableCode(err); code != "" {
			return nil, externalID, code
		}
	}

	return nil, externalID, "MANJO_TIMEOUT"
}

func isUnauthorized(err error) bool {
	var apiErr *manjoclient.APIError
	return errors.As(err, &apiErr) && apiErr.StatusCode == http.StatusUnauthorized
}

// nonRetryableCode returns a non-empty error code if err should NOT be
// retried (400/401/404/409), or "" if it is transient (timeout/5xx/network)
// and the caller should retry.
func nonRetryableCode(err error) string {
	var apiErr *manjoclient.APIError
	if !errors.As(err, &apiErr) {
		return ""
	}
	switch apiErr.StatusCode {
	case http.StatusBadRequest:
		return "INVALID_REQUEST"
	case http.StatusUnauthorized:
		return "UNAUTHORIZED"
	case http.StatusNotFound:
		return "INVALID_MERCHANT"
	case http.StatusConflict:
		return "CONFLICT"
	}
	if apiErr.StatusCode >= 500 {
		return ""
	}
	return "MANJO_ERROR"
}

func parseExpireDate(expireDate string, fallbackNow time.Time) (time.Time, error) {
	loc := time.FixedZone("WIB", 7*60*60)
	t, err := time.ParseInLocation("20060102150405", expireDate, loc)
	if err != nil {
		return fallbackNow, fmt.Errorf("transaction: failed to parse expireDate %q: %w", expireDate, err)
	}
	return t, nil
}

func (s *Service) markFailed(ctx context.Context, transactionID string) error {
	_, err := s.q.MarkTransactionFailed(ctx, transactionID)
	return err
}

func (s *Service) markQRGenerated(ctx context.Context, transactionID, externalID string, resp *manjoclient.GenerateQRResponse, expireAt time.Time) error {
	_, err := s.q.MarkTransactionQRGenerated(ctx, sqlc.MarkTransactionQRGeneratedParams{
		TransactionID: transactionID,
		QrisPayload:   pgtype.Text{String: resp.QRContent, Valid: true},
		ReferenceNo:   pgtype.Text{String: resp.ReferenceNo, Valid: true},
		ExternalID:    pgtype.Text{String: externalID, Valid: true},
		ExpireAt:      pgtype.Timestamptz{Time: expireAt, Valid: true},
	})
	return err
}
```

Sesuaikan nama field `sqlc.CreateTransactionParams`/`sqlc.MarkTransactionQRGeneratedParams` dengan hasil aktual Task 1 Step 7 kalau berbeda.

- [x] **Step 4: Jalankan test, verifikasi lulus**

Run: `go test ./internal/transaction/... -v -run TestGenerateQR`
Expected: `PASS` untuk `TestGenerateQR_Success`, `TestGenerateQR_Conflict409_MarksFailed`, `TestGenerateQR_401_RefreshesTokenAndRetriesOnce`.

- [x] **Step 5: Jalankan seluruh test package**

Run: `go test ./internal/transaction/... -v -race`
Expected: semua test (Task 6 + Task 7) `PASS`.

---

### Task 8: Extend `internal/mqttclient` — Publish & Subscribe

**Files:**
- Modify: `internal/mqttclient/client.go`
- Modify: `internal/mqttclient/client_test.go`

**Interfaces:**
- Produces: `(*Client).Publish(topic string, payload []byte) error`, `(*Client).Subscribe(topic string, handler mqtt.MessageHandler) error` (tipe `mqtt.MessageHandler` dari `github.com/eclipse/paho.mqtt.golang`) — dipakai `cmd/server/main.go` (Task 9)

- [x] **Step 1: Tulis failing test — tambahkan ke `internal/mqttclient/client_test.go`**

```go
func TestPublishAndSubscribe_RoundTrip(t *testing.T) {
	c, err := Connect("tcp://localhost:11883", "", "")
	if err != nil {
		t.Fatalf("Connect() error = %v (pastikan `docker compose up -d mosquitto` sedang jalan)", err)
	}
	defer c.Disconnect()

	received := make(chan string, 1)
	testTopic := "test/publish-subscribe-roundtrip"

	if err := c.Subscribe(testTopic, func(client mqtt.Client, msg mqtt.Message) {
		received <- string(msg.Payload())
	}); err != nil {
		t.Fatalf("Subscribe() error = %v", err)
	}

	if err := c.Publish(testTopic, []byte("hello")); err != nil {
		t.Fatalf("Publish() error = %v", err)
	}

	select {
	case payload := <-received:
		if payload != "hello" {
			t.Errorf("received payload = %q, want %q", payload, "hello")
		}
	case <-time.After(5 * time.Second):
		t.Fatal("timed out waiting for published message")
	}
}
```

Tambahkan import `mqtt "github.com/eclipse/paho.mqtt.golang"` ke `client_test.go` kalau belum ada.

- [x] **Step 2: Jalankan test, verifikasi gagal**

Run: `go test ./internal/mqttclient/... -v -run TestPublishAndSubscribe`
Expected: FAIL — `Publish`/`Subscribe` undefined.

- [x] **Step 3: Tambahkan `Publish`/`Subscribe` di `internal/mqttclient/client.go`**

Tambahkan di akhir file:

```go
// Publish sends payload to topic at QoS 1 (at-least-once — reasonable
// default per brainstorm-q161-updated.md Section 25.1).
func (c *Client) Publish(topic string, payload []byte) error {
	token := c.client.Publish(topic, 1, false, payload)
	if !token.WaitTimeout(5 * time.Second) {
		return fmt.Errorf("mqttclient: publish to %q timed out", topic)
	}
	return token.Error()
}

// Subscribe registers handler for topic at QoS 1.
func (c *Client) Subscribe(topic string, handler mqtt.MessageHandler) error {
	token := c.client.Subscribe(topic, 1, handler)
	if !token.WaitTimeout(10 * time.Second) {
		return fmt.Errorf("mqttclient: subscribe to %q timed out", topic)
	}
	return token.Error()
}
```

- [x] **Step 4: Jalankan test, verifikasi lulus**

Run: `go test ./internal/mqttclient/... -v -run TestPublishAndSubscribe`
Expected: `PASS`.

- [x] **Step 5: Jalankan seluruh test package**

Run: `go test ./internal/mqttclient/... -v -race`
Expected: semua test (Fase 1 + Task 8) `PASS`.

---

### Task 9: Wire Semua di `cmd/server/main.go`

**Files:**
- Modify: `cmd/server/main.go`

**Interfaces:**
- Consumes: semua Task 1-8

**Precondition:** Task 1-8 selesai.

- [x] **Step 1: Tulis ulang `cmd/server/main.go`**

Ganti seluruh isi file dengan:

```go
package main

import (
	"context"
	"encoding/json"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/eclipse/paho.mqtt.golang"
	"github.com/jackc/pgx/v5/pgtype"
	"github.com/labstack/echo/v4"

	"service-payment-bridge/internal/config"
	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/httpserver"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/mqttclient"
	"service-payment-bridge/internal/qrtopic"
	"service-payment-bridge/internal/resolver"
	"service-payment-bridge/internal/secrets"
	"service-payment-bridge/internal/transaction"
	"service-payment-bridge/internal/validation"
)

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, nil))
	slog.SetDefault(logger)

	cfg, err := config.Load()
	if err != nil {
		logger.Error("failed to load config", "error", err)
		os.Exit(1)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	pool, err := database.NewPool(ctx, cfg.DatabaseURL)
	cancel()
	if err != nil {
		logger.Error("failed to connect to database", "error", err)
		os.Exit(1)
	}
	defer pool.Close()

	q := sqlc.New(pool)
	deviceResolver := resolver.New(q)
	registry := manjoclient.NewRegistry()
	txService := transaction.NewService(q, registry, secrets.EnvProvider{})

	mqttClient, err := mqttclient.Connect(cfg.MQTTBrokerURL, cfg.MQTTUsername, cfg.MQTTPassword)
	if err != nil {
		logger.Error("failed to connect to mqtt broker", "error", err)
		os.Exit(1)
	}
	defer mqttClient.Disconnect()

	handler := newGenerateQRHandler(logger, q, deviceResolver, txService, mqttClient)
	if err := mqttClient.Subscribe("topic/#", handler); err != nil {
		logger.Error("failed to subscribe to topic/#", "error", err)
		os.Exit(1)
	}

	e := echo.New()
	e.HideBanner = true
	e.GET("/healthz", httpserver.HealthzHandler(pool))

	go func() {
		if err := e.Start(":" + cfg.HTTPPort); err != nil && err != http.ErrServerClosed {
			logger.Error("http server error", "error", err)
			os.Exit(1)
		}
	}()

	logger.Info("service started", "port", cfg.HTTPPort)

	quit := make(chan os.Signal, 1)
	signal.Notify(quit, os.Interrupt, syscall.SIGTERM)
	<-quit

	logger.Info("shutting down")
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer shutdownCancel()
	if err := e.Shutdown(shutdownCtx); err != nil {
		logger.Error("http server shutdown error", "error", err)
	}
}

func newGenerateQRHandler(logger *slog.Logger, q *sqlc.Queries, deviceResolver *resolver.Resolver, txService *transaction.Service, mqttClient *mqttclient.Client) mqtt.MessageHandler {
	return func(_ mqtt.Client, msg mqtt.Message) {
		ctx := context.Background()
		topicStr := msg.Topic()
		payload := msg.Payload()

		parsedTopic, err := qrtopic.Parse(topicStr)
		if err != nil {
			logger.Warn("dropping message with invalid topic format", "topic", topicStr, "error", err)
			return
		}

		qrMsg, err := validation.ParseAndValidateGenerateQR(payload)
		if err != nil {
			logger.Warn("invalid GENERATE_QR payload", "topic", topicStr, "error", err)
			logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusFAILED, "", err.Error())
			return
		}

		device, err := deviceResolver.ResolveDevice(ctx, parsedTopic.DeviceID)
		if err != nil || !device.DeviceActive || !device.MerchantActive || !device.TenantActive {
			errMsg := "UNKNOWN_DEVICE"
			switch {
			case err == nil && !device.DeviceActive:
				errMsg = "DEVICE_INACTIVE"
			case err == nil && !device.MerchantActive:
				errMsg = "MERCHANT_INACTIVE"
			case err == nil && !device.TenantActive:
				errMsg = "TENANT_INACTIVE"
			}
			logger.Warn("device resolve failed", "device_id", parsedTopic.DeviceID, "reason", errMsg)
			logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusFAILED, "", errMsg)
			return
		}

		result, err := txService.GenerateQR(ctx, *device, qrMsg.Amount)
		if err != nil {
			logger.Error("GenerateQR orchestration error", "device_id", device.DeviceID, "error", err)
			logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusFAILED, "", err.Error())
			return
		}

		logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusPROCESSED, result.TransactionID, "")

		outPayload := buildQRResultPayload(result)
		outBytes, _ := json.Marshal(outPayload)

		if err := mqttClient.Publish(topicStr, outBytes); err != nil {
			logger.Error("failed to publish QR_RESULT", "topic", topicStr, "transaction_id", result.TransactionID, "error", err)
			logMQTTMessage(ctx, q, logger, topicStr, outBytes, sqlc.MqttDirectionOUTBOUND, sqlc.MqttMessageStatusFAILED, result.TransactionID, err.Error())
			return
		}
		logMQTTMessage(ctx, q, logger, topicStr, outBytes, sqlc.MqttDirectionOUTBOUND, sqlc.MqttMessageStatusPROCESSED, result.TransactionID, "")
	}
}

type qrResultPayload struct {
	Type          string `json:"type"`
	TransactionID string `json:"transaction_id"`
	Status        string `json:"status"`
	QRISPayload   string `json:"qris_payload,omitempty"`
	ExpireAt      string `json:"expire_at,omitempty"`
	Error         string `json:"error,omitempty"`
}

func buildQRResultPayload(result *transaction.GenerateQRResult) qrResultPayload {
	p := qrResultPayload{
		Type:          "QR_RESULT",
		TransactionID: result.TransactionID,
		Status:        result.Status,
	}
	if result.Status == "SUCCESS" {
		p.QRISPayload = result.QRISPayload
		p.ExpireAt = result.ExpireAt.Format(time.RFC3339)
	} else {
		p.Error = result.ErrorCode
	}
	return p
}

func logMQTTMessage(ctx context.Context, q *sqlc.Queries, logger *slog.Logger, topic string, payload []byte, direction sqlc.MqttDirection, status sqlc.MqttMessageStatus, transactionID, errMsg string) {
	params := sqlc.LogMQTTMessageParams{
		Topic:     topic,
		Payload:   payload,
		Direction: direction,
		Status:    status,
	}
	if transactionID != "" {
		params.TransactionID = pgtype.Text{String: transactionID, Valid: true}
	}
	if errMsg != "" {
		params.ErrorMessage = pgtype.Text{String: errMsg, Valid: true}
	}
	if _, err := q.LogMQTTMessage(ctx, params); err != nil {
		logger.Error("failed to log mqtt_messages", "error", err)
	}
}
```

Sesuaikan nama field `sqlc.LogMQTTMessageParams` dengan hasil aktual Task 1 Step 7 kalau berbeda. Import `"github.com/eclipse/paho.mqtt.golang"` **tanpa alias** dipakai sebagai `mqtt.Client`/`mqtt.Message`/`mqtt.MessageHandler` — samakan dengan konvensi alias `mqtt` yang sudah dipakai `internal/mqttclient/client.go` (`mqtt "github.com/eclipse/paho.mqtt.golang"`) supaya konsisten; gunakan alias yang sama di `main.go`.

- [x] **Step 2: Verifikasi build**

Run: `go build ./... 2>&1`
Expected: sukses. Perbaiki mismatch nama field/tipe sesuai hasil sqlc aktual kalau ada error kompilasi.

- [x] **Step 3: Jalankan seluruh test suite project**

Run: `go build ./... && go vet ./... && gofmt -l . && go test ./... -v -race`
Expected: semua test PASS (Fase 1 + Fase 2 + Task 1-8 Fase 3), tidak ada race warning, `gofmt -l .` kosong.

---

### Task 10: Integration Test End-to-End (Mock Manjo, Postgres & Mosquitto Asli)

**Files:**
- Create: `internal/qrflow_test.go` (di root `internal/`, package `qrflow_test`, black-box test yang mem-boot komponen manual — **bukan** `cmd/server` karena `main()` tidak bisa dites langsung)

**Interfaces:**
- Consumes: semua Task 1-8

- [x] **Step 1: Tulis `internal/qrflow_test.go`**

```go
package internal_test

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"

	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/mqttclient"
	"service-payment-bridge/internal/qrtopic"
	"service-payment-bridge/internal/resolver"
	"service-payment-bridge/internal/secrets"
	"service-payment-bridge/internal/transaction"
	"service-payment-bridge/internal/validation"
)

// TestGenerateQRFlow_EndToEnd wires the same components as cmd/server/main.go
// (minus HTTP server) against real Postgres + Mosquitto (docker-compose,
// Fase 1) and a mock Manjo (httptest), publishing a real MQTT message and
// asserting a real QR_RESULT comes back on the same topic.
func TestGenerateQRFlow_EndToEnd(t *testing.T) {
	manjoServer := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/v1.0/access-token/b2b":
			json.NewEncoder(w).Encode(manjoclient.AccessTokenResponse{
				TokenType: "Bearer", AccessToken: "test-token", ExpiresIn: "900",
			})
		case "/v1.0/qr/qr-mpm-generate":
			json.NewEncoder(w).Encode(manjoclient.GenerateQRResponse{
				ReferenceNo: "A0000099", QRContent: "00020101-e2e-test",
				AdditionalInfo: manjoclient.GenerateQRResponseAdditionalInfo{
					ExpireDate: time.Now().Add(time.Hour).Format("20060102150405"),
				},
			})
		}
	}))
	defer manjoServer.Close()

	ctx := context.Background()
	pool, err := database.NewPool(ctx, "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable")
	if err != nil {
		t.Fatalf("failed to connect to test database: %v", err)
	}
	defer pool.Close()
	q := sqlc.New(pool)

	merchantID := "E2E-TEST-MERCHANT"
	deviceID := "E2E-TEST-DEVICE"
	topic := qrtopic.Build(merchantID, "", deviceID)

	t.Setenv("E2E_PRIVATE_KEY_REF", mustGenerateTestKeyBase64(t))
	t.Setenv("E2E_SECRET_REF", "e2e-test-secret")

	_, err = pool.Exec(ctx, `
		INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
		VALUES ($1, 'e2e-client-id', 'E2E_PRIVATE_KEY_REF', 'E2E_SECRET_REF', $1, '05')
		ON CONFLICT (merchant_id) DO NOTHING`, merchantID)
	if err != nil {
		t.Fatalf("failed to seed merchant: %v", err)
	}
	_, err = pool.Exec(ctx, `
		INSERT INTO devices (device_id, merchant_id, mqtt_topic)
		VALUES ($1, $2, $3)
		ON CONFLICT (device_id) DO NOTHING`, deviceID, merchantID, topic)
	if err != nil {
		t.Fatalf("failed to seed device: %v", err)
	}
	t.Cleanup(func() {
		pool.Exec(context.Background(), `DELETE FROM mqtt_messages WHERE topic = $1`, topic)
		pool.Exec(context.Background(), `DELETE FROM manjo_api_logs`)
		pool.Exec(context.Background(), `DELETE FROM transactions WHERE merchant_id = $1`, merchantID)
		pool.Exec(context.Background(), `DELETE FROM devices WHERE device_id = $1`, deviceID)
		pool.Exec(context.Background(), `DELETE FROM merchants WHERE merchant_id = $1`, merchantID)
	})

	deviceResolver := resolver.New(q)
	registry := manjoclient.NewRegistry()
	txService := transaction.NewServiceWithBaseURL(q, registry, secrets.EnvProvider{}, manjoServer.URL)

	mqttClient, err := mqttclient.Connect("tcp://localhost:11883", "", "")
	if err != nil {
		t.Fatalf("failed to connect to mosquitto (pastikan docker compose up -d jalan): %v", err)
	}
	defer mqttClient.Disconnect()

	handlerDone := make(chan struct{}, 1)
	err = mqttClient.Subscribe(topic, func(_ mqtt.Client, msg mqtt.Message) {
		var payload struct {
			Type   string `json:"type"`
			Amount int64  `json:"amount"`
		}
		if json.Unmarshal(msg.Payload(), &payload) == nil && payload.Type == "GENERATE_QR" {
			// simulate the consumer handler inline (same logic as main.go)
			qrMsg, err := validation.ParseAndValidateGenerateQR(msg.Payload())
			if err != nil {
				return
			}
			device, err := deviceResolver.ResolveDevice(ctx, deviceID)
			if err != nil {
				return
			}
			result, err := txService.GenerateQR(ctx, *device, qrMsg.Amount)
			if err != nil {
				return
			}
			out, _ := json.Marshal(map[string]interface{}{
				"type": "QR_RESULT", "transaction_id": result.TransactionID,
				"status": result.Status, "qris_payload": result.QRISPayload,
			})
			mqttClient.Publish(topic, out)
			return
		}

		// this is the QR_RESULT reply
		handlerDone <- struct{}{}
	})
	if err != nil {
		t.Fatalf("Subscribe() error = %v", err)
	}

	reqPayload, _ := json.Marshal(map[string]interface{}{"type": "GENERATE_QR", "amount": 50000})
	if err := mqttClient.Publish(topic, reqPayload); err != nil {
		t.Fatalf("Publish() error = %v", err)
	}

	select {
	case <-handlerDone:
		// success
	case <-time.After(10 * time.Second):
		t.Fatal("timed out waiting for QR_RESULT reply")
	}

	rows, err := pool.Query(ctx, `SELECT status FROM transactions WHERE merchant_id = $1`, merchantID)
	if err != nil {
		t.Fatalf("failed to query transactions: %v", err)
	}
	defer rows.Close()
	found := false
	for rows.Next() {
		var status string
		rows.Scan(&status)
		if status == "QR_GENERATED" {
			found = true
		}
	}
	if !found {
		t.Error("no transaction with status QR_GENERATED found in database")
	}
}

func mustGenerateTestKeyBase64(t *testing.T) string {
	t.Helper()
	pemBytes, err := manjoclient.WrapPKCS8PEM("")
	_ = pemBytes
	_ = err
	// Generate a fresh key and return its raw base64 (matching the format
	// WrapPKCS8PEM expects — see internal/manjoclient/signature_test.go
	// generateTestPrivateKeyPEM for the same pattern).
	key, kerr := rsa.GenerateKey(rand.Reader, 2048)
	if kerr != nil {
		t.Fatalf("failed to generate test key: %v", kerr)
	}
	der, derErr := x509.MarshalPKCS8PrivateKey(key)
	if derErr != nil {
		t.Fatalf("failed to marshal test key: %v", derErr)
	}
	return base64.StdEncoding.EncodeToString(der)
}
```

**Catatan:** hapus baris `pemBytes, err := manjoclient.WrapPKCS8PEM("")` yang jadi bug sisa penulisan (memanggil dengan string kosong akan error) — itu bukan bagian fungsional, hapus 3 baris itu (`pemBytes, err :=`, `_ = pemBytes`, `_ = err`), langsung mulai dari `key, kerr := rsa.GenerateKey(...)`. Tambahkan import `"crypto/rand"`, `"crypto/rsa"`, `"crypto/x509"`, `"encoding/base64"`.

- [x] **Step 2: Jalankan test**

Run: `go test ./internal/... -v -run TestGenerateQRFlow_EndToEnd`
Expected: `PASS` — publish MQTT asli, `QR_RESULT` diterima balik di topic yang sama, row `transactions` di DB berstatus `QR_GENERATED`.

---

### Task 11: Seed Data Sandbox & Verifikasi End-to-End Sandbox Nyata

**Files:**
- Create: `migrations/000011_seed_sandbox_merchant.up.sql`
- Create: `migrations/000011_seed_sandbox_merchant.down.sql`

**Interfaces:**
- Produces: satu row `merchants` + `devices` di database, kredensial merujuk ke `.env.sandbox` (Fase 2) lewat `secrets.EnvProvider` — dipakai untuk verifikasi manual end-to-end sandbox

- [x] **Step 1: Tulis `migrations/000011_seed_sandbox_merchant.up.sql`**

```sql
INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
VALUES (
    'SANDBOX-MERCHANT',
    'MANJO_SANDBOX_CLIENT_KEY_ISPLACEHOLDER',
    'MANJO_SANDBOX_PRIVATE_KEY',
    'MANJO_SANDBOX_CLIENT_SECRET',
    'MANJO_SANDBOX_MERCHANT_ID_ISPLACEHOLDER',
    '05'
);

INSERT INTO devices (device_id, merchant_id, mqtt_topic)
VALUES ('SANDBOX-DEVICE-001', 'SANDBOX-MERCHANT', 'topic/SANDBOX-MERCHANT/_/SANDBOX-DEVICE-001');
```

**Catatan penting:** `manjo_client_id` dan `manjo_merchant_id` di skema **bukan referensi** (bukan nama env var) — itu **nilai literal** yang langsung dipakai di header/body request (lihat `schema.md` Section 4: `manjo_client_id` = `X-CLIENT-KEY` langsung, beda dari `manjo_private_key_ref`/`manjo_client_secret_ref` yang memang referensi). Ganti `MANJO_SANDBOX_CLIENT_KEY_ISPLACEHOLDER` dan `MANJO_SANDBOX_MERCHANT_ID_ISPLACEHOLDER` di atas dengan **nilai asli** dari `.env.sandbox` (`MANJO_SANDBOX_CLIENT_KEY=EQ1WYMK9calE` dan `MANJO_SANDBOX_MERCHANT_ID=MT58530503`) sebelum menjalankan migration — baca `.env.sandbox` dan tulis ulang file ini dengan nilai literal tsb sebelum Step 3.

- [x] **Step 2: Tulis `migrations/000011_seed_sandbox_merchant.down.sql`**

```sql
DELETE FROM devices WHERE device_id = 'SANDBOX-DEVICE-001';
DELETE FROM merchants WHERE merchant_id = 'SANDBOX-MERCHANT';
```

- [x] **Step 3: Jalankan migration**

Run:
```bash
export DATABASE_URL="postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
migrate -path migrations -database "$DATABASE_URL" up
```
Expected: `11/u seed_sandbox_merchant` berhasil.

- [x] **Step 4: Build & jalankan binary dengan kredensial sandbox**

Run:
```bash
go build -o bin/server ./cmd/server
set -a && source .env.sandbox && set +a
export HTTP_PORT=18080
export DATABASE_URL="postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
export MQTT_BROKER_URL="tcp://localhost:11883"
export LOG_LEVEL=info
./bin/server &
sleep 2
```
Expected: log `service started`, tidak exit dengan error.

- [x] **Step 5: Publish pesan GENERATE_QR asli via `mosquitto_pub`, verifikasi balasan**

Run (di terminal lain, subscribe dulu untuk melihat balasan):
```bash
timeout 15 mosquitto_sub -h localhost -p 11883 -t 'topic/SANDBOX-MERCHANT/_/SANDBOX-DEVICE-001' -C 1 &
sleep 1
mosquitto_pub -h localhost -p 11883 -t 'topic/SANDBOX-MERCHANT/_/SANDBOX-DEVICE-001' -m '{"type":"GENERATE_QR","amount":1000}'
wait
```
Expected: pesan `QR_RESULT` dengan `status":"SUCCESS"` dan `qris_payload` asli dari Manjo UAT diterima di topic yang sama.

- [x] **Step 6: Verifikasi data di database**

Run:
```bash
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c "SELECT transaction_id, status, qris_payload IS NOT NULL AS has_qr, external_id FROM transactions WHERE merchant_id = 'SANDBOX-MERCHANT' ORDER BY created_at DESC LIMIT 1;"
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c "SELECT direction, operation, http_status FROM manjo_api_logs ORDER BY created_at DESC LIMIT 5;"
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c "SELECT direction, status FROM mqtt_messages WHERE topic = 'topic/SANDBOX-MERCHANT/_/SANDBOX-DEVICE-001' ORDER BY created_at DESC LIMIT 5;"
```
Expected: transaksi `status=QR_GENERATED`, `has_qr=t`, `external_id` terisi. `manjo_api_logs` mencatat panggilan `ACCESS_TOKEN`/`GENERATE_QR` dengan `http_status=200`. `mqtt_messages` mencatat INBOUND dan OUTBOUND `PROCESSED`.

- [x] **Step 7: Matikan binary**

Run:
```bash
kill %1 2>/dev/null || pkill -f "bin/server"
```

---

## Selesai

Setelah Task 1-11 lulus, Flow A (Generate QR) lengkap berfungsi dari MQTT masuk sampai QRIS tampil — teruji lewat unit test, integration test (mock Manjo + Postgres/Mosquitto asli), **dan** end-to-end nyata melawan sandbox Manjo UAT. `manjo_api_logs`/`mqtt_messages` mencatat seluruh trafik untuk traceability. Fase 4 (Payment Notification) menyusul sebagai plan terpisah.

## Execution Notes (dieksekusi 2026-09-25)

**Prediksi nama struct/field sqlc (Task 1 Step 7) 100% akurat** — `GetDeviceWithMerchantAndTenantRow`, `CreateTransactionParams`, `MarkTransactionQRGeneratedParams`, `LogMQTTMessageParams`, `LogManjoAPICallParams` semua cocok persis dengan hasil generate aktual, termasuk `TenantActiveStatus NullMerchantStatus` untuk kolom `LEFT JOIN`. Tidak ada penyesuaian nama field yang diperlukan di Task 5/7/9.

**Satu gap ditemukan & diperbaiki saat eksekusi Task 11 (di luar apa yang tertulis di plan):** `cmd/server/main.go` (Task 9) memanggil `transaction.NewService(...)` — **tidak** meneruskan `cfg.ManjoBaseURL` sama sekali. Field config itu sudah ada sejak Fase 1 tapi tidak pernah benar-benar disambungkan ke `transaction.Service`. Kalau tidak diperbaiki, binary asli akan selalu memakai `defaultBaseURL` (host non-`-uat`) dan gagal melawan sandbox UAT dengan error yang sama seperti temuan awal Fase 2 ("Public Key Not Found"). Diperbaiki dengan mengganti ke `transaction.NewServiceWithBaseURL(q, registry, secrets.EnvProvider{}, cfg.ManjoBaseURL)` di `main.go`.

**Temuan menarik (bukan bug) saat sandbox test nyata (Task 11 Step 5):** server subscribe ke `topic/#` (mencakup topic-nya sendiri), sehingga `QR_RESULT` yang dipublish balik ikut diterima lagi sebagai pesan "inbound". `validation.ParseAndValidateGenerateQR` menolaknya dengan jelas (`unexpected type "QR_RESULT", want GENERATE_QR`), tercatat rapi di `mqtt_messages` sebagai `FAILED` dengan `error_message` yang informatif — tidak ada infinite loop atau korupsi data. Validator bekerja defensif persis seperti didesain.

**Hasil end-to-end sandbox nyata:** `qris_payload` asli diterima dari Manjo UAT untuk merchant "Pupuk Kalteng" (`MT58530503`), `transaction_id` format benar (`TRX-20260925-81MLD0`), status `QR_GENERATED` di DB, `external_id` tersimpan, `manjo_api_logs` mencatat `GENERATE_QR` dengan `http_status=200`.

**Verifikasi akhir:**
- `go build ./...`, `go vet ./...`, `gofmt -l .` — bersih
- `go test ./... -v -race` — **48 test PASS** di 13 package (naik dari 23 di akhir Fase 2 → 47 setelah Task 1-9 → 48 setelah integration test Task 10)
- Retry policy (`401` invalidate+retry sekali, `409` langsung `FAILED`) tervalidasi lewat `TestGenerateQR_401_RefreshesTokenAndRetriesOnce` dan `TestGenerateQR_Conflict409_MarksFailed` dengan mock Manjo — belum pernah teruji langsung ke sandbox nyata (sandbox tidak dikonfigurasi untuk memicu error tsb on-demand), konsisten dengan `qa.md` matriks mock-vs-sandbox (idempotency/concurrency "tidak wajib" diuji di sandbox).
