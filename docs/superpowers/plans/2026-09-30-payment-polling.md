# Payment Polling & Audio Announcement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Detect QRIS payments by polling Manjo's `qr-mpm-query` every 3 seconds and play a spoken amount on the Q161 Pro soundbox the moment a transaction becomes `PAID`; fix the +7h `expire_at` bug; add an `altosim` dev tool that pays a UAT QR through Alto's simulator.

**Architecture:** A DB-driven poller goroutine inside `cmd/server` claims due `QR_GENERATED` transactions each second (`next_query_at` column, `FOR UPDATE SKIP LOCKED`), asks `transaction.Service.CheckPayment` to query Manjo and apply a guarded status transition, and hands `PAID` transactions to an `announcer` that publishes a `+`-separated `.mp3` path list (built by `internal/voice`) to `topic_{device_id}`. A guarded `UPDATE … WHERE status = 'QR_GENERATED'` guarantees one announcement per payment even with several instances.

**Tech Stack:** Go 1.25, PostgreSQL + `golang-migrate`, `sqlc` v1.31.1 (pgx/v5), `github.com/eclipse/paho.mqtt.golang`, Mosquitto, stdlib `net/http`/`crypto`.

**Spec:** `docs/superpowers/specs/2026-09-30-payment-polling-design.md` — read it before starting any task.

## Global Constraints

- **No new third-party dependencies.** Everything uses stdlib plus modules already in `go.mod`.
- **Never hand-edit `internal/database/sqlc/`.** Change `internal/database/queries/*.sql` or `migrations/`, then run `sqlc generate` (v1.31.1 is installed at `$(go env GOPATH)/bin/sqlc`). `sqlc diff` must print nothing at the end of every task that touches SQL.
- **Query request format follows the working UAT collection, not the docs:** no `X-CLIENT-KEY` header, `X-PARTNER-ID` = client key, body `serviceCode: "47"`, `additionalInfo.currency: "IDR"`, signed path `/v1.0/qr/qr-mpm-query`.
- **Query status mapping is its own function** — never shared with Payment Notification codes (`manjo-api-docs.md` 5.10).
- **QR expired** = HTTP `403` with body `responseCode` `"4035100"`.
- **Audio payload** for Rp50.000 is exactly `/ext/awal-qris.mp3+/ext/lima.mp3+/ext/puluh.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3`. No `rupiah` clip, no `_adr` suffix.
- **Only `PAID` is announced.**
- Constants: poll tick `1s`, claim batch `20`, workers `8`, `PAYMENT_POLL_INTERVAL` default `3s`, publish attempts `3` with `1s` delay, deadline grace `2m`, fallback QR validity `10m`.
- **Integration tests use their own merchant/device IDs and must never modify other rows** in the shared dev DB. Mock Manjo access-token responses must leave `responseCode` empty (test cleanup identifies mock `ACCESS_TOKEN` log rows by that).
- **Stop any running `go run ./cmd/server` before `go test ./...`.** The server subscribes to `qris/request` and (after Task 8) polls the same DB, so it interferes with integration tests.
- Stack must be up for tests: `docker compose up -d`, migrations applied (`migrate -path migrations -database "$DATABASE_URL" up`, with `DATABASE_URL=postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable`).
- Code and code comments in English; user-facing docs (`docs/*.md`) in Indonesian.
- Every commit message ends with the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

---

### Task 1: `next_query_at` column, poller queries, and scheduling at QR generation

**Files:**
- Create: `migrations/000014_add_next_query_at.up.sql`
- Create: `migrations/000014_add_next_query_at.down.sql`
- Modify: `internal/database/queries/transactions.sql`
- Regenerate: `internal/database/sqlc/` (via `sqlc generate`)
- Modify: `cmd/server/main.go` (`logMQTTMessage`: `Payload` becomes `string` after regeneration)
- Modify: `internal/transaction/service.go`
- Test: `internal/transaction/service_test.go`

**Interfaces:**
- Produces: column `transactions.next_query_at TIMESTAMPTZ NULL`; `sqlc.Transaction.NextQueryAt pgtype.Timestamptz`; `(*sqlc.Queries).ClaimDueTransactions(ctx, sqlc.ClaimDueTransactionsParams{NextQueryAt pgtype.Timestamptz; MerchantID pgtype.Text; BatchSize int32}) ([]sqlc.Transaction, error)`; `(*sqlc.Queries).TransitionFromQRGenerated(ctx, sqlc.TransitionFromQRGeneratedParams{Status sqlc.TransactionStatus; ManjoStatusCode pgtype.Text; PaidAt pgtype.Timestamptz; TransactionID string}) (sqlc.Transaction, error)` (returns `pgx.ErrNoRows` when the row is no longer `QR_GENERATED`); `sqlc.MarkTransactionQRGeneratedParams.NextQueryAt`; `transaction.DefaultPollInterval = 3 * time.Second`; `(*transaction.Service).SetPollInterval(d time.Duration)`.
- Note: regeneration also turns `sqlc.LogMQTTMessageParams.Payload` / `sqlc.MqttMessage.Payload` from `[]byte` into `string` — pending drift from migration `000013` that was never regenerated.

- [ ] **Step 1: Write the failing test**

In `internal/transaction/service_test.go`, inside `TestGenerateQR_Success`, directly after the block

```go
	if !tx.ExternalID.Valid || tx.ExternalID.String == "" {
		t.Error("stored external_id is empty")
	}
```

insert:

```go
	if !tx.NextQueryAt.Valid {
		t.Fatal("next_query_at is NULL, want it scheduled for the payment poller")
	}
	if d := time.Until(tx.NextQueryAt.Time); d <= 0 || d > DefaultPollInterval {
		t.Errorf("next_query_at is %v from now, want within (0, %v]", d, DefaultPollInterval)
	}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `go test ./internal/transaction/ -run TestGenerateQR_Success`
Expected: build FAIL — `tx.NextQueryAt undefined` and `undefined: DefaultPollInterval`.

- [ ] **Step 3: Write the migration**

`migrations/000014_add_next_query_at.up.sql`:

```sql
ALTER TABLE transactions ADD COLUMN next_query_at TIMESTAMPTZ;

CREATE INDEX idx_transactions_next_query_at
    ON transactions (next_query_at)
    WHERE status = 'QR_GENERATED';

-- Existing QR_GENERATED rows carry an expire_at that is 7 hours too late (Manjo's
-- expireDate bug, see the spec). Correct it to the observed 7.5-minute validity and
-- make them due now: the poller asks Manjo once (-> 403 "Transaction Expire" ->
-- EXPIRED) or, failing that, the deadline safety net expires them immediately.
UPDATE transactions
SET expire_at = created_at + interval '7 minutes 30 seconds',
    next_query_at = now()
WHERE status = 'QR_GENERATED';
```

`migrations/000014_add_next_query_at.down.sql`:

```sql
-- The expire_at correction made by the up migration is not reverted.
DROP INDEX IF EXISTS idx_transactions_next_query_at;
ALTER TABLE transactions DROP COLUMN IF EXISTS next_query_at;
```

- [ ] **Step 4: Update the SQL queries**

In `internal/database/queries/transactions.sql`, replace the `MarkTransactionQRGenerated` query with:

```sql
-- name: MarkTransactionQRGenerated :one
UPDATE transactions
SET status = 'QR_GENERATED',
    qris_payload = $2,
    reference_no = $3,
    external_id = $4,
    expire_at = $5,
    next_query_at = $6,
    updated_at = now()
WHERE transaction_id = $1
RETURNING *;
```

Append at the end of the file:

```sql

-- name: ClaimDueTransactions :many
-- Claims up to batch_size QR_GENERATED transactions whose next check is due and pushes
-- their next check to next_query_at, so a failed or crashed check is retried next time.
-- SKIP LOCKED keeps two poller instances from claiming the same row. merchant_id NULL
-- means all merchants (tests pass their own merchant to stay off real rows).
UPDATE transactions
SET next_query_at = sqlc.arg(next_query_at)::timestamptz
WHERE transaction_id IN (
    SELECT t.transaction_id FROM transactions t
    WHERE t.status = 'QR_GENERATED'
      AND t.next_query_at <= now()
      AND (sqlc.narg(merchant_id)::varchar IS NULL OR t.merchant_id = sqlc.narg(merchant_id)::varchar)
    ORDER BY t.next_query_at
    LIMIT sqlc.arg(batch_size)::int
    FOR UPDATE SKIP LOCKED
)
RETURNING *;

-- name: TransitionFromQRGenerated :one
-- Moves a transaction out of QR_GENERATED. Returns no row when it already left that
-- state, so exactly one caller — across instances — performs each transition.
-- NULL manjo_status_code / paid_at leave the stored values unchanged.
UPDATE transactions
SET status = sqlc.arg(status)::transaction_status,
    manjo_status_code = COALESCE(sqlc.narg(manjo_status_code)::varchar, manjo_status_code),
    paid_at = COALESCE(sqlc.narg(paid_at)::timestamptz, paid_at),
    next_query_at = NULL,
    updated_at = now()
WHERE transaction_id = sqlc.arg(transaction_id) AND status = 'QR_GENERATED'
RETURNING *;
```

- [ ] **Step 5: Apply the migration and regenerate sqlc**

Run:
```bash
export DATABASE_URL="postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
migrate -path migrations -database "$DATABASE_URL" up
sqlc generate
sqlc diff
```
Expected: `14/u add_next_query_at`, then `sqlc diff` prints nothing.

- [ ] **Step 6: Fix the `Payload` type in `cmd/server/main.go`**

In `logMQTTMessage`, change

```go
		Payload:   payload,
```

to

```go
		Payload:   string(payload),
```

- [ ] **Step 7: Schedule the first check when the QR is generated**

In `internal/transaction/service.go`:

Add below the existing `const ( maxCreateRetries … )` block:

```go
// DefaultPollInterval is how long after QR generation, and between checks, the payment
// poller asks Manjo for a transaction's status (config PAYMENT_POLL_INTERVAL).
const DefaultPollInterval = 3 * time.Second
```

Add a field to `Service`:

```go
type Service struct {
	q            *sqlc.Queries
	registry     *manjoclient.Registry
	secrets      secrets.Provider
	baseURL      string // "" berarti pakai default manjoclient.Client
	now          func() time.Time
	pollInterval time.Duration
}
```

Change `NewService` to:

```go
func NewService(q *sqlc.Queries, registry *manjoclient.Registry, secretProvider secrets.Provider) *Service {
	return &Service{q: q, registry: registry, secrets: secretProvider, now: time.Now, pollInterval: DefaultPollInterval}
}

// SetPollInterval overrides DefaultPollInterval.
func (s *Service) SetPollInterval(d time.Duration) {
	s.pollInterval = d
}
```

In `markQRGenerated`, add the new field to the params literal:

```go
		ExpireAt:      pgtype.Timestamptz{Time: expireAt, Valid: true},
		NextQueryAt:   pgtype.Timestamptz{Time: s.now().Add(s.pollInterval), Valid: true},
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `go build ./... && go vet ./... && go test ./internal/transaction/ ./internal/database/...`
Expected: PASS (including `TestGenerateQR_Success`).

- [ ] **Step 9: Commit**

```bash
git add migrations/000014_add_next_query_at.up.sql migrations/000014_add_next_query_at.down.sql internal/database/queries/transactions.sql internal/database/sqlc cmd/server/main.go internal/transaction/service.go internal/transaction/service_test.go
git commit -m "$(cat <<'EOF'
feat(db): schedule payment checks with transactions.next_query_at

Adds the column, a SKIP LOCKED claim query and a guarded
QR_GENERATED transition for the payment poller, and schedules the
first check when a QR is generated. Regenerating sqlc also picks up
the TEXT payload column from migration 000013.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Derive `expire_at` from `expiryDuration`

**Files:**
- Modify: `internal/transaction/service.go` (`GenerateQR`, `parseExpireDate`)
- Create: `internal/transaction/expire_test.go`

**Interfaces:**
- Consumes: `manjoclient.GenerateQRResponseAdditionalInfo{ExpiryDuration, ExpireDate string}` (existing).
- Produces: unexported `computeExpireAt(info manjoclient.GenerateQRResponseAdditionalInfo, receivedAt time.Time) time.Time` and `fallbackQRValidity = 10 * time.Minute` (Task 5 uses `fallbackQRValidity`).

- [ ] **Step 1: Write the failing test**

`internal/transaction/expire_test.go`:

```go
package transaction

import (
	"testing"
	"time"

	"service-payment-bridge/internal/manjoclient"
)

func TestComputeExpireAt(t *testing.T) {
	wib := time.FixedZone("WIB", 7*60*60)
	receivedAt := time.Date(2026, 9, 29, 9, 34, 54, 0, wib)
	expireDateWIB := time.Date(2026, 9, 29, 9, 42, 24, 0, wib)

	tests := []struct {
		name string
		info manjoclient.GenerateQRResponseAdditionalInfo
		want time.Time
	}{
		{
			// Real UAT response: expireDate is 7h too late, expiryDuration is right.
			name: "expiryDuration wins over the +7h expireDate",
			info: manjoclient.GenerateQRResponseAdditionalInfo{ExpiryDuration: "450000", ExpireDate: "20260929164224"},
			want: receivedAt.Add(7*time.Minute + 30*time.Second),
		},
		{
			name: "missing expiryDuration falls back to expireDate in WIB",
			info: manjoclient.GenerateQRResponseAdditionalInfo{ExpireDate: "20260929094224"},
			want: expireDateWIB,
		},
		{
			name: "non-numeric expiryDuration falls back to expireDate",
			info: manjoclient.GenerateQRResponseAdditionalInfo{ExpiryDuration: "abc", ExpireDate: "20260929094224"},
			want: expireDateWIB,
		},
		{
			name: "zero expiryDuration falls back to expireDate",
			info: manjoclient.GenerateQRResponseAdditionalInfo{ExpiryDuration: "0", ExpireDate: "20260929094224"},
			want: expireDateWIB,
		},
		{
			name: "nothing usable falls back to 10 minutes",
			info: manjoclient.GenerateQRResponseAdditionalInfo{},
			want: receivedAt.Add(10 * time.Minute),
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := computeExpireAt(tt.info, receivedAt); !got.Equal(tt.want) {
				t.Errorf("computeExpireAt() = %v, want %v", got, tt.want)
			}
		})
	}
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `go test ./internal/transaction/ -run TestComputeExpireAt`
Expected: build FAIL — `undefined: computeExpireAt`.

- [ ] **Step 3: Implement**

In `internal/transaction/service.go`:

Replace in `GenerateQR`

```go
	expireAt, err := parseExpireDate(resp.AdditionalInfo.ExpireDate, s.now())
	if err != nil {
		expireAt = s.now().Add(time.Hour)
	}
```

with

```go
	expireAt := computeExpireAt(resp.AdditionalInfo, s.now())
```

Replace the whole `parseExpireDate` function with:

```go
// fallbackQRValidity is used when Manjo's response carries no usable expiry. It is a bit
// above the ~7.5 minutes observed in UAT so the poller never stops before the QR expires.
const fallbackQRValidity = 10 * time.Minute

// computeExpireAt prefers expiryDuration (milliseconds; UAT sends "450000" = 7m30s) over
// expireDate, which UAT returns 7 hours too late (the WIB offset applied twice).
func computeExpireAt(info manjoclient.GenerateQRResponseAdditionalInfo, receivedAt time.Time) time.Time {
	if ms, err := strconv.ParseInt(info.ExpiryDuration, 10, 64); err == nil && ms > 0 {
		return receivedAt.Add(time.Duration(ms) * time.Millisecond)
	}
	if t, err := parseExpireDate(info.ExpireDate); err == nil {
		return t
	}
	return receivedAt.Add(fallbackQRValidity)
}

func parseExpireDate(expireDate string) (time.Time, error) {
	loc := time.FixedZone("WIB", 7*60*60)
	t, err := time.ParseInLocation("20060102150405", expireDate, loc)
	if err != nil {
		return time.Time{}, fmt.Errorf("transaction: failed to parse expireDate %q: %w", expireDate, err)
	}
	return t, nil
}
```

Add `"strconv"` to the imports of `service.go`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `go vet ./internal/transaction/ && go test ./internal/transaction/`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add internal/transaction/service.go internal/transaction/expire_test.go
git commit -m "$(cat <<'EOF'
fix(transaction): derive expire_at from expiryDuration

Manjo UAT returns expireDate 7 hours too late, so every stored
expire_at was 7h off (QRs really expire after ~7.5 minutes). Use
expiryDuration (milliseconds), falling back to expireDate, then to
10 minutes.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: `internal/voice` — amount to audio clip list

**Files:**
- Create: `internal/voice/clip.go`
- Create: `internal/voice/voice.go`
- Create: `internal/voice/voice_test.go`

**Interfaces:**
- Produces: `voice.Payload(rupiah int64) (string, error)` (used by Task 6); `voice.Compose(rupiah int64) ([]voice.Clip, error)`; `voice.MaxRupiah int64 = 999_999_999_999`. Rejects `rupiah <= 0` and `> MaxRupiah`.

Adapted from `C:\Users\Hi\Downloads\Kerjaan\Soundbox\mqtt-poc\publisher\internal\voice` (tested there). Differences: opening clip `awal-qris` instead of `notiftest`, closing clip `akhir-berhasil` instead of `rupiah`, amounts `<= 0` rejected (no `nol`).

- [ ] **Step 1: Write the failing test**

`internal/voice/voice_test.go`:

```go
package voice

import (
	"strings"
	"testing"
)

// words renders clips as a sentence so failures read as Indonesian, not file names.
func words(clips []Clip) string {
	parts := make([]string, len(clips))
	for i, c := range clips {
		parts[i] = string(c)
	}
	return strings.Join(parts, " ")
}

func TestCompose(t *testing.T) {
	tests := []struct {
		rupiah int64
		want   string
	}{
		{7, "awal-qris tujuh akhir-berhasil"},
		// "se-" forms must not be built from "satu" + multiplier.
		{10, "awal-qris sepuluh akhir-berhasil"},
		{11, "awal-qris sebelas akhir-berhasil"},
		{100, "awal-qris seratus akhir-berhasil"},
		{1_000, "awal-qris seribu akhir-berhasil"},
		{12, "awal-qris dua belas akhir-berhasil"},
		{17, "awal-qris tujuh belas akhir-berhasil"},
		{40, "awal-qris empat puluh akhir-berhasil"},
		{45, "awal-qris empat puluh lima akhir-berhasil"},
		{125, "awal-qris seratus dua puluh lima akhir-berhasil"},
		{375, "awal-qris tiga ratus tujuh puluh lima akhir-berhasil"},
		{1_500, "awal-qris seribu lima ratus akhir-berhasil"},
		{50_000, "awal-qris lima puluh ribu akhir-berhasil"},
		{125_500, "awal-qris seratus dua puluh lima ribu lima ratus akhir-berhasil"},
		// "satu juta", not "sejuta": "se-" only applies up to thousands.
		{1_000_000, "awal-qris satu juta akhir-berhasil"},
		{1_250_000, "awal-qris satu juta dua ratus lima puluh ribu akhir-berhasil"},
		{2_000_000_000, "awal-qris dua miliar akhir-berhasil"},
	}

	for _, tt := range tests {
		clips, err := Compose(tt.rupiah)
		if err != nil {
			t.Fatalf("Compose(%d) error = %v", tt.rupiah, err)
		}
		if got := words(clips); got != tt.want {
			t.Errorf("Compose(%d)\n  got:  %s\n  want: %s", tt.rupiah, got, tt.want)
		}
	}
}

func TestComposeRejects(t *testing.T) {
	for _, rupiah := range []int64{0, -1, MaxRupiah + 1} {
		if _, err := Compose(rupiah); err == nil {
			t.Errorf("Compose(%d) error = nil, want error", rupiah)
		}
	}
}

func TestPayload_MatchesDeviceFormat(t *testing.T) {
	got, err := Payload(50_000)
	if err != nil {
		t.Fatalf("Payload() error = %v", err)
	}
	want := "/ext/awal-qris.mp3+/ext/lima.mp3+/ext/puluh.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3"
	if got != want {
		t.Errorf("Payload(50000)\n  got:  %s\n  want: %s", got, want)
	}
}

// mp3APIClips are the (non-_adr) files in Soundbox/docs/mp3/mp3-api, the set loaded on
// the Q161 Pro. A clip outside this set would be silently skipped by the device.
var mp3APIClips = map[Clip]bool{
	"akhir-berhasil": true, "awal-qris": true, "belas": true, "delapan": true, "dua": true,
	"empat": true, "enam": true, "juta": true, "lima": true, "miliar": true, "minus": true,
	"nol": true, "puluh": true, "ratus": true, "ribu": true, "satu": true, "sebelas": true,
	"sembilan": true, "sepuluh": true, "seratus": true, "seribu": true, "tiga": true,
	"triliun": true, "tujuh": true,
}

func TestEveryClipExistsOnDevice(t *testing.T) {
	amounts := []int64{12_345, 999_999, 1_000_000, 987_654_321, MaxRupiah}
	for rupiah := int64(1); rupiah <= 2_000; rupiah++ {
		amounts = append(amounts, rupiah)
	}
	for _, rupiah := range amounts {
		clips, err := Compose(rupiah)
		if err != nil {
			t.Fatalf("Compose(%d) error = %v", rupiah, err)
		}
		for _, c := range clips {
			if !mp3APIClips[c] {
				t.Fatalf("Compose(%d) uses clip %q, which is not on the device", rupiah, c)
			}
		}
	}
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `go test ./internal/voice/`
Expected: build FAIL — `undefined: Compose`, `undefined: Clip`, …

- [ ] **Step 3: Implement**

`internal/voice/clip.go`:

```go
// Package voice turns a Rupiah amount into the "+"-separated list of audio files the
// Q161 Pro firmware plays (mqtt.c: a payload containing ".mp3" is split on "+" and each
// file is played in order).
//
// Adapted from Soundbox/mqtt-poc/publisher/internal/voice and re-targeted at the mp3-api
// clip set loaded on the device: "awal-qris" opens, "akhir-berhasil" (which already says
// "rupiah") closes.
package voice

// Clip is an audio file's base name on the device, without directory or extension.
type Clip string

// Opening and closing clips.
const (
	ClipAwalQris      Clip = "awal-qris"
	ClipAkhirBerhasil Clip = "akhir-berhasil"
)

// Digit clips.
const (
	ClipSatu     Clip = "satu"
	ClipDua      Clip = "dua"
	ClipTiga     Clip = "tiga"
	ClipEmpat    Clip = "empat"
	ClipLima     Clip = "lima"
	ClipEnam     Clip = "enam"
	ClipTujuh    Clip = "tujuh"
	ClipDelapan  Clip = "delapan"
	ClipSembilan Clip = "sembilan"
)

// Irregular forms. Indonesian uses the prefix "se-" for the first unit of these levels,
// so they cannot be built from "satu" plus a multiplier.
const (
	ClipSepuluh Clip = "sepuluh" // 10, not "satu puluh"
	ClipSebelas Clip = "sebelas" // 11, not "satu belas"
	ClipSeratus Clip = "seratus" // 100, not "satu ratus"
	ClipSeribu  Clip = "seribu"  // 1.000, not "satu ribu"
)

// Multiplier clips.
const (
	ClipBelas  Clip = "belas"
	ClipPuluh  Clip = "puluh"
	ClipRatus  Clip = "ratus"
	ClipRibu   Clip = "ribu"
	ClipJuta   Clip = "juta"
	ClipMiliar Clip = "miliar"
)

// digitClips maps 1-9 to their clip. Index 0 is intentionally empty: zero is never
// spoken inside a number.
var digitClips = [10]Clip{
	1: ClipSatu,
	2: ClipDua,
	3: ClipTiga,
	4: ClipEmpat,
	5: ClipLima,
	6: ClipEnam,
	7: ClipTujuh,
	8: ClipDelapan,
	9: ClipSembilan,
}
```

`internal/voice/voice.go`:

```go
package voice

import (
	"fmt"
	"strings"
)

// MaxRupiah is the largest speakable amount: above it a "triliun" level would be needed.
const MaxRupiah int64 = 999_999_999_999

const (
	// dir is where the audio files live on the device (Downtool StartAddr=/ext/).
	dir = "/ext/"
	// ext must stay ".mp3": the firmware tells an audio list from TTS text by it.
	ext       = ".mp3"
	separator = "+"
)

// scale pairs one number level with its multiplier clip. solo is the special form used
// when the level's value is exactly one ("seribu"); empty means the regular form
// ("satu juta").
type scale struct {
	value int64
	mult  Clip
	solo  Clip
}

// scales is ordered from the largest level down.
var scales = []scale{
	{value: 1_000_000_000, mult: ClipMiliar},
	{value: 1_000_000, mult: ClipJuta},
	{value: 1_000, mult: ClipRibu, solo: ClipSeribu},
}

// Compose returns the clips announcing a successful payment of rupiah.
func Compose(rupiah int64) ([]Clip, error) {
	if rupiah <= 0 {
		return nil, fmt.Errorf("voice: amount must be > 0, got %d", rupiah)
	}
	if rupiah > MaxRupiah {
		return nil, fmt.Errorf("voice: amount %d exceeds %d", rupiah, MaxRupiah)
	}

	clips := []Clip{ClipAwalQris}
	clips = append(clips, numberClips(rupiah)...)
	return append(clips, ClipAkhirBerhasil), nil
}

// Payload returns the MQTT payload announcing rupiah, e.g. for Rp50.000:
// "/ext/awal-qris.mp3+/ext/lima.mp3+/ext/puluh.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3".
func Payload(rupiah int64) (string, error) {
	clips, err := Compose(rupiah)
	if err != nil {
		return "", err
	}
	paths := make([]string, len(clips))
	for i, c := range clips {
		paths[i] = dir + string(c) + ext
	}
	return strings.Join(paths, separator), nil
}

// numberClips spells a positive integer, level by level from the highest.
func numberClips(n int64) []Clip {
	if n < 100 {
		return belowHundred(n)
	}
	for _, s := range scales {
		if n >= s.value {
			return withScale(n, s)
		}
	}
	return hundreds(n)
}

// belowHundred spells 1-99, including "sepuluh", "sebelas" and "<digit> belas".
func belowHundred(n int64) []Clip {
	switch {
	case n < 10:
		return []Clip{digitClips[n]}
	case n == 10:
		return []Clip{ClipSepuluh}
	case n == 11:
		return []Clip{ClipSebelas}
	case n < 20:
		return []Clip{digitClips[n-10], ClipBelas}
	}

	clips := []Clip{digitClips[n/10], ClipPuluh}
	if rest := n % 10; rest > 0 {
		clips = append(clips, digitClips[rest])
	}
	return clips
}

// hundreds spells 100-999: "seratus" for the first hundred, "<digit> ratus" otherwise.
func hundreds(n int64) []Clip {
	var clips []Clip
	if h := n / 100; h == 1 {
		clips = []Clip{ClipSeratus}
	} else {
		clips = []Clip{digitClips[h], ClipRatus}
	}
	if rest := n % 100; rest > 0 {
		clips = append(clips, belowHundred(rest)...)
	}
	return clips
}

// withScale spells one level (ribu, juta, miliar) and then the remainder.
func withScale(n int64, s scale) []Clip {
	var clips []Clip
	if part := n / s.value; part == 1 && s.solo != "" {
		clips = []Clip{s.solo}
	} else {
		clips = append(numberClips(part), s.mult)
	}
	if rest := n % s.value; rest > 0 {
		clips = append(clips, numberClips(rest)...)
	}
	return clips
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `go vet ./internal/voice/ && go test ./internal/voice/ -v`
Expected: PASS (4 tests).

- [ ] **Step 5: Commit**

```bash
git add internal/voice
git commit -m "$(cat <<'EOF'
feat(voice): compose payment announcements from the device clip set

Ports the tested amount-to-clips logic from mqtt-poc and targets the
mp3-api set on the Q161 Pro (awal-qris ... akhir-berhasil), producing
the "+"-separated /ext/*.mp3 payload the firmware plays.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: `manjoclient.QueryPayment`

**Files:**
- Modify: `internal/manjoclient/types.go`
- Modify: `internal/manjoclient/client.go` (extract shared signed POST; add `QueryPayment`)
- Test: `internal/manjoclient/client_test.go`

**Interfaces:**
- Produces:
  - `manjoclient.QueryServiceCode = "47"`
  - `manjoclient.QueryPaymentParams{OriginalReferenceNo, OriginalPartnerReferenceNo, OriginalExternalID, MerchantID string}` (JSON tags `originalReferenceNo`, `originalPartnerReferenceNo`, `originalExternalId`, `merchantId`)
  - `manjoclient.QueryPaymentResponse{ResponseCode, ResponseMessage, OriginalReferenceNo, OriginalPartnerReferenceNo, LatestTransactionStatus, TransactionStatusDesc, PaidTime string; Amount manjoclient.Amount}`
  - `(*manjoclient.Client).QueryPayment(ctx context.Context, p QueryPaymentParams) (*QueryPaymentResponse, error)` — non-200 returns `*manjoclient.APIError`.
  - `(*manjoclient.APIError).ResponseCode() string` — `responseCode` from the JSON body, `""` if not JSON.

- [ ] **Step 1: Write the failing tests**

Append to `internal/manjoclient/client_test.go`:

```go
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
```

Add `"errors"` and `"io"` to the imports of `client_test.go`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `go test ./internal/manjoclient/ -run 'QueryPayment|APIErrorResponseCode'`
Expected: build FAIL — `undefined: QueryPaymentParams`, `apiErr.ResponseCode undefined`.

- [ ] **Step 3: Add the types**

Append to `internal/manjoclient/types.go`:

```go
// ResponseCode extracts Manjo's 7-digit responseCode from the error body, or "" when the
// body is not JSON (e.g. "4035100" = QR expired, returned by qr-mpm-query with HTTP 403).
func (e *APIError) ResponseCode() string {
	var body struct {
		ResponseCode string `json:"responseCode"`
	}
	if json.Unmarshal([]byte(e.Body), &body) != nil {
		return ""
	}
	return body.ResponseCode
}

// QueryServiceCode is the body serviceCode sent by the BI SNAP UAT collection. The docs
// say "99"; the collection works in UAT and is confirmed for production, so it wins.
const QueryServiceCode = "47"

// QueryPaymentParams identifies the transaction to look up with qr-mpm-query.
type QueryPaymentParams struct {
	OriginalReferenceNo        string `json:"originalReferenceNo"`        // referenceNo from qr-mpm-generate
	OriginalPartnerReferenceNo string `json:"originalPartnerReferenceNo"` // our transaction_id
	OriginalExternalID         string `json:"originalExternalId"`         // X-EXTERNAL-ID used at generate time
	MerchantID                 string `json:"merchantId"`
}

type queryPaymentRequest struct {
	QueryPaymentParams
	ServiceCode    string              `json:"serviceCode"`
	AdditionalInfo queryAdditionalInfo `json:"additionalInfo"`
}

type queryAdditionalInfo struct {
	Currency string `json:"currency"`
}

// QueryPaymentResponse is a 200 answer from qr-mpm-query. LatestTransactionStatus uses
// the Query status scheme (manjo-api-docs.md 5.10), NOT the Payment Notification one.
type QueryPaymentResponse struct {
	ResponseCode               string `json:"responseCode"`
	ResponseMessage            string `json:"responseMessage"`
	OriginalReferenceNo        string `json:"originalReferenceNo"`
	OriginalPartnerReferenceNo string `json:"originalPartnerReferenceNo"`
	LatestTransactionStatus    string `json:"latestTransactionStatus"`
	TransactionStatusDesc      string `json:"transactionStatusDesc"`
	PaidTime                   string `json:"paidTime"`
	Amount                     Amount `json:"amount"`
}
```

Add `"encoding/json"` to the imports of `types.go`.

(`queryPaymentRequest` embeds `QueryPaymentParams`; `encoding/json` flattens the embedded struct's tagged fields into the top-level object.)

- [ ] **Step 4: Extract the signed POST and add `QueryPayment`**

In `internal/manjoclient/client.go`, replace the whole `GenerateQR` function with:

```go
// GenerateQR calls POST /v1.0/qr/qr-mpm-generate, obtaining a valid access
// token from the cache automatically. Returns the X-EXTERNAL-ID that was
// used for this attempt (callers persist it for tracing — schema.md
// transactions.external_id) alongside the response.
func (c *Client) GenerateQR(ctx context.Context, req GenerateQRRequest) (*GenerateQRResponse, string, error) {
	body, err := json.Marshal(req)
	if err != nil {
		return nil, "", fmt.Errorf("manjoclient: failed to marshal generate QR request: %w", err)
	}

	respBody, externalID, err := c.postSigned(ctx, "/v1.0/qr/qr-mpm-generate", body, c.cfg.PartnerID)
	if err != nil {
		return nil, externalID, err
	}

	var qrResp GenerateQRResponse
	if err := json.Unmarshal(respBody, &qrResp); err != nil {
		return nil, externalID, fmt.Errorf("manjoclient: failed to parse generate QR response: %w", err)
	}
	return &qrResp, externalID, nil
}

// QueryPayment calls POST /v1.0/qr/qr-mpm-query in the format of the working BI SNAP UAT
// collection: no X-CLIENT-KEY header, X-PARTNER-ID = client key, serviceCode "47".
func (c *Client) QueryPayment(ctx context.Context, p QueryPaymentParams) (*QueryPaymentResponse, error) {
	body, err := json.Marshal(queryPaymentRequest{
		QueryPaymentParams: p,
		ServiceCode:        QueryServiceCode,
		AdditionalInfo:     queryAdditionalInfo{Currency: "IDR"},
	})
	if err != nil {
		return nil, fmt.Errorf("manjoclient: failed to marshal query payment request: %w", err)
	}

	respBody, _, err := c.postSigned(ctx, "/v1.0/qr/qr-mpm-query", body, c.cfg.ClientKey)
	if err != nil {
		return nil, err
	}

	var queryResp QueryPaymentResponse
	if err := json.Unmarshal(respBody, &queryResp); err != nil {
		return nil, fmt.Errorf("manjoclient: failed to parse query payment response: %w", err)
	}
	return &queryResp, nil
}

// postSigned POSTs body to path with the HMAC-signed headers shared by the transactional
// endpoints and returns the raw 200 body plus the X-EXTERNAL-ID used. Non-200 answers
// become *APIError. partnerID differs per endpoint: generate sends the merchant ID,
// query sends the client key.
func (c *Client) postSigned(ctx context.Context, path string, body []byte, partnerID string) ([]byte, string, error) {
	token, err := c.tokenManager.Get(ctx)
	if err != nil {
		return nil, "", fmt.Errorf("manjoclient: failed to obtain access token: %w", err)
	}

	externalID, err := generateExternalID(35)
	if err != nil {
		return nil, "", err
	}

	timestamp := NowJakarta()
	signature, err := SignHMAC(c.cfg.ClientSecret, http.MethodPost, path, token, body, timestamp)
	if err != nil {
		return nil, "", err
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.cfg.BaseURL+path, bytes.NewReader(body))
	if err != nil {
		return nil, "", err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+token)
	req.Header.Set("X-TIMESTAMP", timestamp)
	req.Header.Set("X-SIGNATURE", signature)
	req.Header.Set("X-PARTNER-ID", partnerID)
	req.Header.Set("X-EXTERNAL-ID", externalID)
	req.Header.Set("CHANNEL-ID", c.cfg.ChannelID)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, externalID, fmt.Errorf("manjoclient: POST %s failed: %w", path, err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, externalID, fmt.Errorf("manjoclient: failed to read %s response: %w", path, err)
	}
	if resp.StatusCode != http.StatusOK {
		return nil, externalID, &APIError{StatusCode: resp.StatusCode, Body: string(respBody)}
	}
	return respBody, externalID, nil
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `go vet ./internal/manjoclient/ && go test ./internal/manjoclient/ ./internal/transaction/`
Expected: PASS — the new tests, and the existing `GenerateQR` tests (unchanged behavior after the extraction).

- [ ] **Step 6: Commit**

```bash
git add internal/manjoclient
git commit -m "$(cat <<'EOF'
feat(manjoclient): add QueryPayment in the UAT collection format

qr-mpm-query is called like the working BI SNAP UAT collection (no
X-CLIENT-KEY, X-PARTNER-ID = client key, serviceCode "47"). The signed
POST shared with GenerateQR is extracted, and APIError exposes Manjo's
responseCode (e.g. 4035100 = QR expired).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: `transaction.CheckPayment` — query, map, guarded transition

**Files:**
- Create: `internal/transaction/payment.go`
- Create: `internal/transaction/payment_test.go`

**Interfaces:**
- Consumes: `(*manjoclient.Client).QueryPayment`, `manjoclient.QueryPaymentParams`, `(*manjoclient.APIError).ResponseCode()` (Task 4); `(*sqlc.Queries).TransitionFromQRGenerated` (Task 1); `fallbackQRValidity` (Task 2); existing `s.buildManjoConfig`, `isUnauthorized`, `resolver.New`.
- Produces (used by Task 7):

```go
type PaymentCheckResult struct {
	Transitioned      bool             // this call moved the transaction out of QR_GENERATED
	Transaction       sqlc.Transaction // updated row when Transitioned, else the input row
	ExpiredByDeadline bool             // the EXPIRED transition came from the safety net, not Manjo
	QueryErr          error            // query failed or answer unusable; retried next poll
	ManjoAmount       int64            // amount.value of a successful query in Rupiah, 0 if unknown
}

func (s *Service) CheckPayment(ctx context.Context, tx sqlc.Transaction) (*PaymentCheckResult, error)
```

`CheckPayment` returns a non-nil error only for database failures.

- [ ] **Step 1: Write the failing tests**

`internal/transaction/payment_test.go`:

```go
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `go test ./internal/transaction/ -run 'MapQueryResult|CheckPayment'`
Expected: build FAIL — `undefined: mapQueryResult`, `svc.CheckPayment undefined`.

- [ ] **Step 3: Implement**

`internal/transaction/payment.go`:

```go
package transaction

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgtype"

	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/resolver"
)

// deadlineGrace: a transaction still QR_GENERATED this long after expire_at is expired
// locally, so a query that keeps failing cannot keep it polled forever.
const deadlineGrace = 2 * time.Minute

// expiredResponseCode is qr-mpm-query's answer (with HTTP 403) once the QR has expired.
const expiredResponseCode = "4035100"

// PaymentCheckResult is the outcome of one CheckPayment call.
type PaymentCheckResult struct {
	// Transitioned is true only for the caller whose UPDATE moved the transaction out of
	// QR_GENERATED — at most one caller per transaction, even across instances.
	Transitioned bool
	// Transaction is the updated row when Transitioned, otherwise the input row.
	Transaction sqlc.Transaction
	// ExpiredByDeadline marks an EXPIRED transition made by the deadline safety net
	// rather than by Manjo. Only meaningful when Transitioned.
	ExpiredByDeadline bool
	// QueryErr is set when the query failed or Manjo's answer was unusable; the
	// transaction stays QR_GENERATED and is retried on the next poll.
	QueryErr error
	// ManjoAmount is amount.value of a successful query in Rupiah, 0 when unknown.
	ManjoAmount int64
}

// CheckPayment asks Manjo for tx's payment status and moves tx out of QR_GENERATED when
// the answer is final or the deadline has passed. Only database failures are returned
// as an error; Manjo or configuration problems end up in PaymentCheckResult.QueryErr.
func (s *Service) CheckPayment(ctx context.Context, tx sqlc.Transaction) (*PaymentCheckResult, error) {
	result := &PaymentCheckResult{Transaction: tx}

	outcome := s.queryPayment(ctx, tx)
	result.QueryErr = outcome.err
	result.ManjoAmount = outcome.amount

	if outcome.status == "" && s.now().After(expiryDeadline(tx)) {
		outcome = queryOutcome{status: sqlc.TransactionStatusEXPIRED}
		result.ExpiredByDeadline = true
	}
	if outcome.status == "" {
		return result, nil
	}

	row, err := s.q.TransitionFromQRGenerated(ctx, sqlc.TransitionFromQRGeneratedParams{
		Status:          outcome.status,
		ManjoStatusCode: pgtype.Text{String: outcome.code, Valid: outcome.code != ""},
		PaidAt:          pgtype.Timestamptz{Time: outcome.paidAt, Valid: !outcome.paidAt.IsZero()},
		TransactionID:   tx.TransactionID,
	})
	if errors.Is(err, pgx.ErrNoRows) {
		return result, nil // already moved by another poller or instance
	}
	if err != nil {
		return nil, fmt.Errorf("transaction: failed to move %s to %s: %w", tx.TransactionID, outcome.status, err)
	}

	result.Transitioned = true
	result.Transaction = row
	return result, nil
}

func expiryDeadline(tx sqlc.Transaction) time.Time {
	expireAt := tx.CreatedAt.Time.Add(fallbackQRValidity)
	if tx.ExpireAt.Valid {
		expireAt = tx.ExpireAt.Time
	}
	return expireAt.Add(deadlineGrace)
}

// queryPayment resolves tx's merchant credentials and calls qr-mpm-query. Every outcome
// except "still pending" is recorded in manjo_api_logs.
func (s *Service) queryPayment(ctx context.Context, tx sqlc.Transaction) queryOutcome {
	device, err := resolver.New(s.q).ResolveDevice(ctx, tx.DeviceID)
	if err != nil {
		return queryOutcome{err: err}
	}
	client, err := s.registry.GetOrCreate(device.MerchantID, func() (manjoclient.Config, error) {
		return s.buildManjoConfig(ctx, *device)
	})
	if err != nil {
		return queryOutcome{err: fmt.Errorf("transaction: failed to build manjo config: %w", err)}
	}

	params := manjoclient.QueryPaymentParams{
		OriginalReferenceNo:        tx.ReferenceNo.String,
		OriginalPartnerReferenceNo: tx.TransactionID,
		OriginalExternalID:         tx.ExternalID.String,
		MerchantID:                 device.ManjoMerchantID,
	}

	start := s.now()
	resp, err := client.QueryPayment(ctx, params)
	if isUnauthorized(err) {
		client.InvalidateToken()
	}

	outcome := mapQueryResult(resp, err, s.now())
	if !outcome.pending() {
		s.logQueryCall(ctx, tx.TransactionID, params, resp, err, s.now().Sub(start))
	}
	return outcome
}

// queryOutcome is what one qr-mpm-query answer means for a QR_GENERATED transaction.
type queryOutcome struct {
	status sqlc.TransactionStatus // "" = no transition
	code   string                 // manjo_status_code to store; "" leaves it unchanged
	paidAt time.Time              // zero leaves paid_at unchanged
	amount int64                  // amount.value in Rupiah, 0 when unknown
	err    error                  // failed or unusable answer; retry
}

// pending reports Manjo's "not paid yet" — the only outcome neither logged nor acted on.
func (o queryOutcome) pending() bool {
	return o.status == "" && o.err == nil
}

// mapQueryResult interprets a qr-mpm-query result with the Query status scheme
// (manjo-api-docs.md 5.10). Never reuse it for Payment Notification codes: the same code
// means something else there ("03" is Pending here but Paid in notifications).
func mapQueryResult(resp *manjoclient.QueryPaymentResponse, err error, now time.Time) queryOutcome {
	if err != nil {
		var apiErr *manjoclient.APIError
		if errors.As(err, &apiErr) && apiErr.StatusCode == http.StatusForbidden && apiErr.ResponseCode() == expiredResponseCode {
			return queryOutcome{status: sqlc.TransactionStatusEXPIRED}
		}
		return queryOutcome{err: err}
	}

	switch code := resp.LatestTransactionStatus; code {
	case "00":
		return queryOutcome{status: sqlc.TransactionStatusPAID, code: code, paidAt: parsePaidTime(resp.PaidTime, now), amount: parseRupiah(resp.Amount.Value)}
	case "01", "02", "03":
		return queryOutcome{}
	case "04":
		return queryOutcome{status: sqlc.TransactionStatusREFUNDED, code: code}
	case "05":
		return queryOutcome{status: sqlc.TransactionStatusCANCELLED, code: code}
	case "06":
		return queryOutcome{status: sqlc.TransactionStatusFAILED, code: code}
	case "07":
		return queryOutcome{err: errors.New("transaction: manjo reports the transaction as not found (07)")}
	default:
		return queryOutcome{err: fmt.Errorf("transaction: unknown query status %q", code)}
	}
}

func parsePaidTime(paidTime string, fallback time.Time) time.Time {
	if t, err := time.Parse(time.RFC3339, paidTime); err == nil {
		return t
	}
	return fallback
}

// parseRupiah converts Manjo's "50000.00" to 50000, or 0 when unparseable.
func parseRupiah(value string) int64 {
	whole, _, _ := strings.Cut(value, ".")
	n, err := strconv.ParseInt(whole, 10, 64)
	if err != nil {
		return 0
	}
	return n
}

func (s *Service) logQueryCall(ctx context.Context, transactionID string, params manjoclient.QueryPaymentParams, resp *manjoclient.QueryPaymentResponse, callErr error, duration time.Duration) {
	reqBody, _ := json.Marshal(params)

	var respBody []byte
	httpStatus := pgtype.Int4{}
	var apiErr *manjoclient.APIError
	switch {
	case callErr == nil:
		respBody, _ = json.Marshal(resp)
		httpStatus = pgtype.Int4{Int32: http.StatusOK, Valid: true}
	case errors.As(callErr, &apiErr):
		respBody = []byte(apiErr.Body)
		httpStatus = pgtype.Int4{Int32: int32(apiErr.StatusCode), Valid: true}
	}
	if !json.Valid(respBody) {
		respBody = nil
		if callErr != nil {
			respBody, _ = json.Marshal(map[string]string{"error": callErr.Error()})
		}
	}

	// Logging failure must not break payment detection (same trade-off as logManjoAPICall).
	_, _ = s.q.LogManjoAPICall(ctx, sqlc.LogManjoAPICallParams{
		Direction:     sqlc.ManjoApiDirectionOUTBOUND,
		Operation:     sqlc.ManjoApiOperationQUERYPAYMENT,
		Endpoint:      "/v1.0/qr/qr-mpm-query",
		HttpStatus:    httpStatus,
		RequestBody:   reqBody,
		ResponseBody:  respBody,
		TransactionID: pgtype.Text{String: transactionID, Valid: true},
		DurationMs:    pgtype.Int4{Int32: int32(duration.Milliseconds()), Valid: true},
	})
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `go vet ./internal/transaction/ && go test ./internal/transaction/ -v`
Expected: PASS — all `TestMapQueryResult` subtests, the four `TestCheckPayment_*` tests, and the existing tests.

- [ ] **Step 5: Commit**

```bash
git add internal/transaction/payment.go internal/transaction/payment_test.go
git commit -m "$(cat <<'EOF'
feat(transaction): check payment status via qr-mpm-query

CheckPayment queries Manjo, maps the answer with the Query status
scheme (separate from Payment Notification codes), and applies a
guarded QR_GENERATED transition so each payment transitions once.
QR expiry is 403/4035100; a 2-minute deadline past expire_at stops
endless polling. Only non-pending answers are logged.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: `internal/mqttlog` + `internal/announcer`

**Files:**
- Create: `internal/mqttlog/mqttlog.go`
- Modify: `cmd/server/main.go` (`logMQTTMessage` delegates to `mqttlog.Record`)
- Create: `internal/announcer/announcer.go`
- Create: `internal/announcer/announcer_test.go`

**Interfaces:**
- Consumes: `voice.Payload` (Task 3), `qrtopic.BuildDeviceTopic` (existing), `sqlc.LogMQTTMessageParams.Payload string` (Task 1).
- Produces (used by Tasks 7–8):

```go
// package mqttlog
type Entry struct {
	Topic         string
	Payload       []byte
	Direction     sqlc.MqttDirection
	Status        sqlc.MqttMessageStatus
	TransactionID string // "" = not linked
	ErrorMessage  string // "" = none
}
func Record(ctx context.Context, q *sqlc.Queries, logger *slog.Logger, e Entry)

// package announcer
type Publisher interface{ Publish(topic string, payload []byte) error } // satisfied by *mqttclient.Client
type Request struct {
	TransactionID string
	DeviceID      string
	Rupiah        int64
}
func New(pub Publisher, q *sqlc.Queries, logger *slog.Logger) *Announcer
func (a *Announcer) Announce(ctx context.Context, req Request) error
```

- [ ] **Step 1: Write the failing tests**

`internal/announcer/announcer_test.go`:

```go
package announcer

import (
	"context"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"sync"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgtype"
	"github.com/jackc/pgx/v5/pgxpool"

	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
)

const testDatabaseURL = "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"

const (
	testMerchantID = "ANN-TEST-MERCHANT"
	testDeviceID   = "ANN-TEST-DEVICE"
)

const rp50000Payload = "/ext/awal-qris.mp3+/ext/lima.mp3+/ext/puluh.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3"

type fakePublisher struct {
	mu           sync.Mutex
	failuresLeft int
	topics       []string
	payloads     []string
}

func (f *fakePublisher) Publish(topic string, payload []byte) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.topics = append(f.topics, topic)
	f.payloads = append(f.payloads, string(payload))
	if f.failuresLeft > 0 {
		f.failuresLeft--
		return errors.New("broker unreachable")
	}
	return nil
}

// setup returns an Announcer over a fake publisher and a PAID transaction to announce
// (mqtt_messages.transaction_id has a foreign key to transactions).
func setup(t *testing.T, failures int) (*Announcer, *fakePublisher, *pgxpool.Pool, string) {
	t.Helper()
	ctx := context.Background()

	pool, err := database.NewPool(ctx, testDatabaseURL)
	if err != nil {
		t.Fatalf("connect test database: %v", err)
	}
	t.Cleanup(pool.Close)

	txID := fmt.Sprintf("ANN-TEST-%d", time.Now().UnixNano())
	seed := []string{
		`INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
		 VALUES ('` + testMerchantID + `', 'ann-client', 'ANN_KEY_REF', 'ANN_SECRET_REF', '` + testMerchantID + `', '05') ON CONFLICT (merchant_id) DO NOTHING`,
		`INSERT INTO devices (device_id, merchant_id, mqtt_topic)
		 VALUES ('` + testDeviceID + `', '` + testMerchantID + `', 'topic_` + testDeviceID + `') ON CONFLICT (device_id) DO NOTHING`,
	}
	for _, stmt := range seed {
		if _, err := pool.Exec(ctx, stmt); err != nil {
			t.Fatalf("seed: %v", err)
		}
	}
	if _, err := pool.Exec(ctx,
		`INSERT INTO transactions (transaction_id, merchant_id, device_id, amount, status) VALUES ($1, $2, $3, 50000, 'PAID')`,
		txID, testMerchantID, testDeviceID); err != nil {
		t.Fatalf("seed transaction: %v", err)
	}
	t.Cleanup(func() {
		ctx := context.Background()
		pool.Exec(ctx, `DELETE FROM mqtt_messages WHERE transaction_id IN (SELECT transaction_id FROM transactions WHERE merchant_id = $1)`, testMerchantID)
		pool.Exec(ctx, `DELETE FROM transactions WHERE merchant_id = $1`, testMerchantID)
		pool.Exec(ctx, `DELETE FROM devices WHERE device_id = $1`, testDeviceID)
		pool.Exec(ctx, `DELETE FROM merchants WHERE merchant_id = $1`, testMerchantID)
	})

	pub := &fakePublisher{failuresLeft: failures}
	a := New(pub, sqlc.New(pool), slog.New(slog.NewTextHandler(io.Discard, nil)))
	a.retryDelay = 0
	return a, pub, pool, txID
}

func outboundRow(t *testing.T, pool *pgxpool.Pool, txID string) (status, payload string, errMsg pgtype.Text) {
	t.Helper()
	err := pool.QueryRow(context.Background(),
		`SELECT status, payload, error_message FROM mqtt_messages WHERE transaction_id = $1 AND direction = 'OUTBOUND'`, txID).
		Scan(&status, &payload, &errMsg)
	if err != nil {
		t.Fatalf("read mqtt_messages row: %v", err)
	}
	return status, payload, errMsg
}

func TestAnnounce_PublishesAudioForAmount(t *testing.T) {
	a, pub, pool, txID := setup(t, 0)

	if err := a.Announce(context.Background(), Request{TransactionID: txID, DeviceID: testDeviceID, Rupiah: 50000}); err != nil {
		t.Fatalf("Announce() error = %v", err)
	}
	if len(pub.topics) != 1 || pub.topics[0] != "topic_"+testDeviceID || pub.payloads[0] != rp50000Payload {
		t.Fatalf("published %v %v, want one %q to topic_%s", pub.topics, pub.payloads, rp50000Payload, testDeviceID)
	}
	if status, payload, _ := outboundRow(t, pool, txID); status != "PROCESSED" || payload != rp50000Payload {
		t.Errorf("mqtt_messages = %s %q, want PROCESSED with the payload", status, payload)
	}
}

func TestAnnounce_RetriesTransientFailures(t *testing.T) {
	a, pub, pool, txID := setup(t, 2)

	if err := a.Announce(context.Background(), Request{TransactionID: txID, DeviceID: testDeviceID, Rupiah: 50000}); err != nil {
		t.Fatalf("Announce() error = %v", err)
	}
	if len(pub.topics) != 3 {
		t.Errorf("publish attempts = %d, want 3", len(pub.topics))
	}
	if status, _, _ := outboundRow(t, pool, txID); status != "PROCESSED" {
		t.Errorf("mqtt_messages status = %s, want PROCESSED", status)
	}
}

func TestAnnounce_GivesUpAfterThreeAttempts(t *testing.T) {
	a, pub, pool, txID := setup(t, 5)

	if err := a.Announce(context.Background(), Request{TransactionID: txID, DeviceID: testDeviceID, Rupiah: 50000}); err == nil {
		t.Fatal("Announce() error = nil, want error after exhausting retries")
	}
	if len(pub.topics) != 3 {
		t.Errorf("publish attempts = %d, want 3", len(pub.topics))
	}
	if status, _, errMsg := outboundRow(t, pool, txID); status != "FAILED" || errMsg.String == "" {
		t.Errorf("mqtt_messages = %s %q, want FAILED with an error message", status, errMsg.String)
	}
}

func TestAnnounce_RejectsInvalidAmount(t *testing.T) {
	a, pub, _, txID := setup(t, 0)

	if err := a.Announce(context.Background(), Request{TransactionID: txID, DeviceID: testDeviceID, Rupiah: 0}); err == nil {
		t.Fatal("Announce() error = nil, want error for amount 0")
	}
	if len(pub.topics) != 0 {
		t.Errorf("published %d times, want 0", len(pub.topics))
	}
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `go test ./internal/announcer/`
Expected: build FAIL — `undefined: New`, `undefined: Announcer`, `undefined: Request`.

- [ ] **Step 3: Implement `mqttlog`**

`internal/mqttlog/mqttlog.go`:

```go
// Package mqttlog records MQTT traffic in mqtt_messages — the trail used to trace
// "paid, but the soundbox stayed silent" (architecture.md principle 6).
package mqttlog

import (
	"context"
	"log/slog"

	"github.com/jackc/pgx/v5/pgtype"

	"service-payment-bridge/internal/database/sqlc"
)

// Entry is one MQTT message to record.
type Entry struct {
	Topic         string
	Payload       []byte
	Direction     sqlc.MqttDirection
	Status        sqlc.MqttMessageStatus
	TransactionID string // "" = not linked to a transaction
	ErrorMessage  string // "" = none
}

// Record inserts e. A failure is logged, never returned: tracing must not break the flow
// it traces.
func Record(ctx context.Context, q *sqlc.Queries, logger *slog.Logger, e Entry) {
	params := sqlc.LogMQTTMessageParams{
		Topic:     e.Topic,
		Payload:   string(e.Payload),
		Direction: e.Direction,
		Status:    e.Status,
	}
	if e.TransactionID != "" {
		params.TransactionID = pgtype.Text{String: e.TransactionID, Valid: true}
	}
	if e.ErrorMessage != "" {
		params.ErrorMessage = pgtype.Text{String: e.ErrorMessage, Valid: true}
	}
	if _, err := q.LogMQTTMessage(ctx, params); err != nil {
		logger.Error("failed to log mqtt_messages", "topic", e.Topic, "error", err)
	}
}
```

In `cmd/server/main.go`, replace the body of `logMQTTMessage` so the function reads:

```go
func logMQTTMessage(ctx context.Context, q *sqlc.Queries, logger *slog.Logger, topic string, payload []byte, direction sqlc.MqttDirection, status sqlc.MqttMessageStatus, transactionID, errMsg string) {
	mqttlog.Record(ctx, q, logger, mqttlog.Entry{
		Topic:         topic,
		Payload:       payload,
		Direction:     direction,
		Status:        status,
		TransactionID: transactionID,
		ErrorMessage:  errMsg,
	})
}
```

Add the import `"service-payment-bridge/internal/mqttlog"` and remove the now-unused `"github.com/jackc/pgx/v5/pgtype"` import from `main.go`.

- [ ] **Step 4: Implement `announcer`**

`internal/announcer/announcer.go`:

```go
// Package announcer plays a payment announcement on a soundbox over MQTT.
package announcer

import (
	"context"
	"fmt"
	"log/slog"
	"time"

	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/mqttlog"
	"service-payment-bridge/internal/qrtopic"
	"service-payment-bridge/internal/voice"
)

// publishAttempts bounds retries of a failed publish: a payment the cashier never hears
// about costs more than a slightly late announcement.
const publishAttempts = 3

const defaultRetryDelay = time.Second

// Publisher publishes one MQTT message at QoS 1 (satisfied by *mqttclient.Client).
type Publisher interface {
	Publish(topic string, payload []byte) error
}

// Request is one payment to announce.
type Request struct {
	TransactionID string
	DeviceID      string
	Rupiah        int64
}

// Announcer builds the audio payload for a payment and publishes it to the device.
type Announcer struct {
	pub        Publisher
	q          *sqlc.Queries
	logger     *slog.Logger
	retryDelay time.Duration
}

// New creates an Announcer.
func New(pub Publisher, q *sqlc.Queries, logger *slog.Logger) *Announcer {
	return &Announcer{pub: pub, q: q, logger: logger, retryDelay: defaultRetryDelay}
}

// Announce publishes the spoken amount to topic_{device_id}, retrying a failed publish,
// and records the final outcome in mqtt_messages.
func (a *Announcer) Announce(ctx context.Context, req Request) error {
	payload, err := voice.Payload(req.Rupiah)
	if err != nil {
		return fmt.Errorf("announcer: %w", err)
	}
	topic := qrtopic.BuildDeviceTopic(req.DeviceID)

	pubErr := a.publishWithRetry(ctx, topic, []byte(payload))

	entry := mqttlog.Entry{
		Topic:         topic,
		Payload:       []byte(payload),
		Direction:     sqlc.MqttDirectionOUTBOUND,
		Status:        sqlc.MqttMessageStatusPROCESSED,
		TransactionID: req.TransactionID,
	}
	if pubErr != nil {
		entry.Status = sqlc.MqttMessageStatusFAILED
		entry.ErrorMessage = pubErr.Error()
	}
	mqttlog.Record(ctx, a.q, a.logger, entry)

	if pubErr != nil {
		return fmt.Errorf("announcer: publish to %s failed after %d attempts: %w", topic, publishAttempts, pubErr)
	}
	return nil
}

func (a *Announcer) publishWithRetry(ctx context.Context, topic string, payload []byte) error {
	var err error
	for attempt := 1; attempt <= publishAttempts; attempt++ {
		if err = a.pub.Publish(topic, payload); err == nil {
			return nil
		}
		if attempt == publishAttempts {
			break
		}
		select {
		case <-ctx.Done():
			return fmt.Errorf("%w (retry aborted: %v)", err, ctx.Err())
		case <-time.After(a.retryDelay):
		}
	}
	return err
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `go build ./... && go vet ./... && go test ./internal/announcer/ -v`
Expected: PASS (4 tests).

- [ ] **Step 6: Commit**

```bash
git add internal/mqttlog internal/announcer cmd/server/main.go
git commit -m "$(cat <<'EOF'
feat(announcer): publish payment announcements to the soundbox

Announcer turns an amount into the device's .mp3 path list, publishes
it to topic_{device_id} with up to 3 attempts, and records the outcome
in mqtt_messages. mqtt_messages recording moves to internal/mqttlog so
the server and the announcer share it.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: `internal/paymentpoller`

**Files:**
- Create: `internal/paymentpoller/poller.go`
- Create: `internal/paymentpoller/poller_test.go`

**Interfaces:**
- Consumes: `(*sqlc.Queries).ClaimDueTransactions` (Task 1); `(*transaction.Service).CheckPayment`, `transaction.PaymentCheckResult` (Task 5); `(*announcer.Announcer).Announce`, `announcer.Request` (Task 6).
- Produces (used by Task 8):

```go
func New(q *sqlc.Queries, checker PaymentChecker, a PaymentAnnouncer, interval time.Duration, logger *slog.Logger) *Poller
func (p *Poller) OnlyMerchant(merchantID string) *Poller // tests: never touch other merchants' rows
func (p *Poller) Run(ctx context.Context)     // blocks; returns after ctx is cancelled and the in-flight batch finished
func (p *Poller) RunOnce(ctx context.Context) // one claim + process cycle
```

- [ ] **Step 1: Write the failing tests**

`internal/paymentpoller/poller_test.go`:

```go
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `go test ./internal/paymentpoller/`
Expected: build FAIL — package `paymentpoller` has no non-test Go files / `undefined: paymentpoller.New`.

- [ ] **Step 3: Implement**

`internal/paymentpoller/poller.go`:

```go
// Package paymentpoller detects QRIS payments by regularly asking Manjo about every
// QR_GENERATED transaction whose next check is due, and announces the ones that got paid.
package paymentpoller

import (
	"context"
	"log/slog"
	"sync"
	"time"

	"github.com/jackc/pgx/v5/pgtype"

	"service-payment-bridge/internal/announcer"
	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/transaction"
)

const (
	tick      = time.Second
	batchSize = 20
	workers   = 8
)

// PaymentChecker is satisfied by *transaction.Service.
type PaymentChecker interface {
	CheckPayment(ctx context.Context, tx sqlc.Transaction) (*transaction.PaymentCheckResult, error)
}

// PaymentAnnouncer is satisfied by *announcer.Announcer.
type PaymentAnnouncer interface {
	Announce(ctx context.Context, req announcer.Request) error
}

// Poller claims due transactions every second and checks them concurrently.
type Poller struct {
	q          *sqlc.Queries
	checker    PaymentChecker
	announcer  PaymentAnnouncer
	interval   time.Duration
	logger     *slog.Logger
	merchantID string // "" = all merchants
}

// New creates a Poller that re-checks a transaction every interval.
func New(q *sqlc.Queries, checker PaymentChecker, a PaymentAnnouncer, interval time.Duration, logger *slog.Logger) *Poller {
	return &Poller{q: q, checker: checker, announcer: a, interval: interval, logger: logger}
}

// OnlyMerchant restricts claims to one merchant. Integration tests use it so a poller run
// never touches other merchants' transactions in the shared dev database.
func (p *Poller) OnlyMerchant(merchantID string) *Poller {
	p.merchantID = merchantID
	return p
}

// Run polls every second until ctx is cancelled. The batch in flight when that happens
// is finished — announcements included — before Run returns, so a normal shutdown never
// leaves a transaction PAID but unannounced.
func (p *Poller) Run(ctx context.Context) {
	ticker := time.NewTicker(tick)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			p.RunOnce(context.WithoutCancel(ctx))
		}
	}
}

// RunOnce claims one batch of due transactions and processes it to completion.
func (p *Poller) RunOnce(ctx context.Context) {
	txs, err := p.q.ClaimDueTransactions(ctx, sqlc.ClaimDueTransactionsParams{
		NextQueryAt: pgtype.Timestamptz{Time: time.Now().Add(p.interval), Valid: true},
		MerchantID:  pgtype.Text{String: p.merchantID, Valid: p.merchantID != ""},
		BatchSize:   batchSize,
	})
	if err != nil {
		p.logger.Error("payment poll claim failed", "error", err)
		return
	}

	sem := make(chan struct{}, workers)
	var wg sync.WaitGroup
	for _, tx := range txs {
		wg.Add(1)
		sem <- struct{}{}
		go func() {
			defer wg.Done()
			defer func() { <-sem }()
			p.process(ctx, tx)
		}()
	}
	wg.Wait()
}

func (p *Poller) process(ctx context.Context, tx sqlc.Transaction) {
	res, err := p.checker.CheckPayment(ctx, tx)
	if err != nil {
		p.logger.Error("payment check failed", "transaction_id", tx.TransactionID, "error", err)
		return
	}
	if res.QueryErr != nil {
		p.logger.Warn("payment query failed", "transaction_id", tx.TransactionID, "error", res.QueryErr)
	}
	if !res.Transitioned {
		return
	}

	done := res.Transaction
	switch done.Status {
	case sqlc.TransactionStatusPAID:
		p.announcePaid(ctx, done, res.ManjoAmount)
	case sqlc.TransactionStatusEXPIRED:
		via := "manjo"
		if res.ExpiredByDeadline {
			via = "deadline"
		}
		p.logger.Info("transaction expired", "transaction_id", done.TransactionID, "via", via)
	case sqlc.TransactionStatusREFUNDED:
		p.logger.Warn("unpaid QR reported as refunded", "transaction_id", done.TransactionID)
	default:
		p.logger.Info("transaction closed", "transaction_id", done.TransactionID, "status", done.Status)
	}
}

func (p *Poller) announcePaid(ctx context.Context, tx sqlc.Transaction, manjoAmount int64) {
	p.logger.Info("payment detected", "transaction_id", tx.TransactionID, "device_id", tx.DeviceID, "amount", tx.Amount)
	if manjoAmount != 0 && manjoAmount != tx.Amount {
		p.logger.Warn("amount mismatch", "transaction_id", tx.TransactionID, "amount", tx.Amount, "manjo_amount", manjoAmount)
	}

	err := p.announcer.Announce(ctx, announcer.Request{TransactionID: tx.TransactionID, DeviceID: tx.DeviceID, Rupiah: tx.Amount})
	if err != nil {
		p.logger.Error("announcement failed", "transaction_id", tx.TransactionID, "device_id", tx.DeviceID, "error", err)
	}
}
```

- [ ] **Step 4: Run tests to verify they pass**

Make sure no `go run ./cmd/server` is running, then:
Run: `go vet ./internal/paymentpoller/ && go test ./internal/paymentpoller/ -v -race`
Expected: PASS (3 tests), no race reports.

- [ ] **Step 5: Commit**

```bash
git add internal/paymentpoller
git commit -m "$(cat <<'EOF'
feat(paymentpoller): poll due transactions and announce payments

Every second the poller claims up to 20 due QR_GENERATED transactions
(SKIP LOCKED), checks them with 8 workers, and announces the ones that
became PAID. Tests prove two concurrent pollers announce a payment
exactly once.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: Wire the poller into the server + config + run docs

**Files:**
- Modify: `internal/config/config.go`, `internal/config/config_test.go`
- Modify: `cmd/server/main.go`
- Modify: `.env.example`
- Modify: `docs/running-locally.md`

**Interfaces:**
- Consumes: `transaction.(*Service).SetPollInterval` (Task 1), `announcer.New` (Task 6), `paymentpoller.New`, `(*Poller).Run` (Task 7).
- Produces: `config.Config.PaymentPollInterval time.Duration` (env `PAYMENT_POLL_INTERVAL`, default `3s`).

- [ ] **Step 1: Write the failing tests**

In `internal/config/config_test.go`, add `"time"` to the imports, and at the end of `TestLoad_RequiredFieldsPresent` add:

```go
	if cfg.PaymentPollInterval != 3*time.Second {
		t.Errorf("PaymentPollInterval default = %v, want 3s", cfg.PaymentPollInterval)
	}
```

Append:

```go
func TestLoad_PaymentPollIntervalOverride(t *testing.T) {
	t.Setenv("DATABASE_URL", "postgres://user:pass@localhost:5432/db")
	t.Setenv("MQTT_BROKER_URL", "tcp://localhost:1883")
	t.Setenv("PAYMENT_POLL_INTERVAL", "5s")

	cfg, err := Load()
	if err != nil {
		t.Fatalf("Load() returned error: %v", err)
	}
	if cfg.PaymentPollInterval != 5*time.Second {
		t.Errorf("PaymentPollInterval = %v, want 5s", cfg.PaymentPollInterval)
	}
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `go test ./internal/config/`
Expected: build FAIL — `cfg.PaymentPollInterval undefined`.

- [ ] **Step 3: Add the config field**

In `internal/config/config.go`, add `"time"` to the imports and the field:

```go
	LogLevel            string        `envconfig:"LOG_LEVEL" default:"info"`
	PaymentPollInterval time.Duration `envconfig:"PAYMENT_POLL_INTERVAL" default:"3s"`
```

(Re-align the struct tags of the other fields with `gofmt`.)

In `.env.example`, append:

```
PAYMENT_POLL_INTERVAL=3s
```

- [ ] **Step 4: Run the config tests**

Run: `go test ./internal/config/`
Expected: PASS.

- [ ] **Step 5: Start the poller in `cmd/server/main.go`**

Add imports `"service-payment-bridge/internal/announcer"` and `"service-payment-bridge/internal/paymentpoller"`.

After

```go
	txService := transaction.NewServiceWithBaseURL(q, registry, secrets.EnvProvider{}, cfg.ManjoBaseURL)
```

add

```go
	txService.SetPollInterval(cfg.PaymentPollInterval)
```

After the `mqttClient.Subscribe(qrtopic.RequestTopic, handler)` block, add:

```go
	poller := paymentpoller.New(q, txService, announcer.New(mqttClient, q, logger), cfg.PaymentPollInterval, logger)
	pollCtx, stopPolling := context.WithCancel(context.Background())
	pollerDone := make(chan struct{})
	go func() {
		defer close(pollerDone)
		poller.Run(pollCtx)
	}()
```

Change

```go
	logger.Info("service started", "port", cfg.HTTPPort)
```

to

```go
	logger.Info("service started", "port", cfg.HTTPPort, "payment_poll_interval", cfg.PaymentPollInterval.String())
```

At the end of `main`, after the `e.Shutdown` block, add:

```go
	// Let the batch in flight finish (and announce) before the MQTT client disconnects.
	stopPolling()
	select {
	case <-pollerDone:
	case <-shutdownCtx.Done():
		logger.Error("payment poller did not finish before the shutdown timeout")
	}
```

- [ ] **Step 6: Verify the server starts and stops cleanly**

Run:
```bash
go build ./... && go vet ./...
go run ./cmd/server
```
Expected within a few seconds: a JSON log `"service started"` with `"payment_poll_interval":"3s"`. Because migration `000014` made the old `QR_GENERATED` rows due, expect one `"transaction expired"` log per old row (`"via":"manjo"` or `"via":"deadline"`). Then press `Ctrl+C`: `"shutting down"` and the process exits without `"payment poller did not finish"`.

- [ ] **Step 7: Update `docs/running-locally.md`**

In Section 5, in the log table, add these rows after the `"invalid GENERATE_QR payload"` row:

```markdown
| `"payment detected"` | Poller menemukan transaksi yang sudah dibayar (`status` → `PAID`), lalu mengirim pengumuman audio ke device |
| `"announcement failed"` | Pengumuman audio gagal dikirim ke broker setelah 3 percobaan. Transaksi tetap `PAID` |
| `"transaction expired"` | QR kedaluwarsa. `via: "manjo"` = dijawab Manjo, `via: "deadline"` = jaring pengaman (2 menit lewat `expire_at`) |
| `"payment query failed"` | Query status ke Manjo gagal atau jawabannya tidak dikenal. Otomatis dicoba lagi 3 detik kemudian |
| `"amount mismatch"` | Nominal dari Manjo berbeda dengan nominal transaksi. Pengumuman tetap memakai nominal transaksi |
```

After that table, add:

```markdown
Setelah generate QR, service mengecek status pembayaran ke Manjo tiap `PAYMENT_POLL_INTERVAL` (default `3s`, di `.env`) sampai QR dibayar atau kedaluwarsa (~7,5 menit). Begitu dibayar, soundbox membunyikan nominalnya.
```

In Section 6, after the paragraph starting "Di `manjo_api_logs`, baris `ACCESS_TOKEN`", add:

````markdown
Baris `QUERY_PAYMENT` hanya dicatat untuk hasil yang bukan "masih pending" (dibayar, kedaluwarsa, error), supaya log tidak dibanjiri ratusan poll per QR.

Transaksi yang sudah `PAID` tapi pengumumannya tidak pernah terkirim (kasus "sudah bayar tapi soundbox diam"):

```bash
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c \
  "SELECT t.transaction_id, t.device_id, t.amount, t.paid_at FROM transactions t
   WHERE t.status = 'PAID' AND NOT EXISTS (
     SELECT 1 FROM mqtt_messages m WHERE m.transaction_id = t.transaction_id
       AND m.direction = 'OUTBOUND' AND m.status = 'PROCESSED' AND m.payload LIKE '%.mp3%')
   ORDER BY t.paid_at DESC;"
```
````

In Section 7, replace the blockquote starting "> **Matikan dulu `go run ./cmd/server` sebelum menjalankan test.**" with:

```markdown
> **Matikan dulu `go run ./cmd/server` sebelum menjalankan test.** Server subscribe ke `qris/request` (topic yang sama dengan `TestGenerateQRFlow_EndToEnd`) dan menjalankan poller pembayaran di DB yang sama dengan test. Kalau server hidup, server bisa menjawab request test lebih dulu atau mengklaim transaksi test, sehingga test gagal walaupun kodenya benar.
```

- [ ] **Step 8: Commit**

```bash
git add internal/config cmd/server/main.go .env.example docs/running-locally.md
git commit -m "$(cat <<'EOF'
feat(server): run the payment poller

The server now polls payment status every PAYMENT_POLL_INTERVAL
(default 3s), announces PAID transactions, and lets the in-flight batch
finish on shutdown. Run docs cover the new logs and a query for
paid-but-silent transactions.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: `internal/altosim` — EMV parser, Alto request, signature, client

**Files:**
- Create: `internal/altosim/emv.go`, `internal/altosim/emv_test.go`
- Create: `internal/altosim/request.go`, `internal/altosim/request_test.go`
- Create: `internal/altosim/client.go`, `internal/altosim/client_test.go`

**Interfaces:**
- Produces (used by Task 10):

```go
type TLV map[string]string
func ParseEMV(s string) (TLV, error)

type Identifiers struct{ CustomerReferenceNumber, ForwardingReferenceNumber, AuthorizationID string }
func NewIdentifiers() (Identifiers, error)
func BuildPaymentRequest(qrContent, timestamp string, ids Identifiers) (PaymentRequest, error)
func (r PaymentRequest) ReferenceLabel() string // tag 62.05 = Manjo referenceNo

const PaymentPath = "/altopay/qr-payment/payment"
func FormatTimestamp(t time.Time) string // UTC "2006-01-02 15:04:05.000Z"
func Sign(apiKey, validationKey string, body []byte, timestamp string) string
type Client struct{ BaseURL, APIKey, ValidationKey string; HTTPClient *http.Client }
func (c *Client) Pay(ctx context.Context, req PaymentRequest) (status int, body []byte, err error)
```

Fixture: a real UAT QR generated by this service (transaction `TRX-20260928-EENWGN`). Its values were verified by an independent parse; the signature vector below was computed with `openssl`.

- [ ] **Step 1: Write the failing tests**

`internal/altosim/emv_test.go`:

```go
package altosim

import "testing"

// realQR is a real UAT qrContent for merchant Pupuk Kalteng, Rp50.000.
const realQR = "00020101021226620015ID.CO.MANJO.WWW01189360085802876978760210MT585305030303UKE51430014ID.CO.QRIS.WWW0205Manjo04121.0.28.09.26520452515303360540850000.005802ID5913Pupuk Kalteng6013JAKARTA BARAT61051155062470520A503321013864160FF4107036590812Q161 Payment6304A037"

func TestParseEMV_RealUATQR(t *testing.T) {
	top, err := ParseEMV(realQR)
	if err != nil {
		t.Fatalf("ParseEMV() error = %v", err)
	}
	wantTop := map[string]string{
		"52": "5251", "54": "50000.00", "58": "ID", "59": "Pupuk Kalteng",
		"60": "JAKARTA BARAT", "61": "11550", "63": "A037",
		"62": "0520A503321013864160FF4107036590812Q161 Payment",
	}
	for tag, want := range wantTop {
		if top[tag] != want {
			t.Errorf("tag %s = %q, want %q", tag, top[tag], want)
		}
	}

	merchant, err := ParseEMV(top["26"])
	if err != nil {
		t.Fatalf("ParseEMV(tag 26) error = %v", err)
	}
	for tag, want := range map[string]string{"00": "ID.CO.MANJO.WWW", "01": "936008580287697876", "02": "MT58530503", "03": "UKE"} {
		if merchant[tag] != want {
			t.Errorf("tag 26.%s = %q, want %q", tag, merchant[tag], want)
		}
	}

	additional, err := ParseEMV(top["62"])
	if err != nil {
		t.Fatalf("ParseEMV(tag 62) error = %v", err)
	}
	for tag, want := range map[string]string{"05": "A503321013864160FF41", "07": "659", "08": "Q161 Payment"} {
		if additional[tag] != want {
			t.Errorf("tag 62.%s = %q, want %q", tag, additional[tag], want)
		}
	}
}

func TestParseEMV_RejectsMalformedInput(t *testing.T) {
	for _, s := range []string{"000", "0005ab", "00X2ab"} {
		if _, err := ParseEMV(s); err == nil {
			t.Errorf("ParseEMV(%q) error = nil, want error", s)
		}
	}
}
```

`internal/altosim/request_test.go`:

```go
package altosim

import (
	"reflect"
	"strings"
	"testing"
)

func TestBuildPaymentRequest_FromRealQR(t *testing.T) {
	ids := Identifiers{CustomerReferenceNumber: "ALTO-API-NMS-aaaaaaaaaaaa", ForwardingReferenceNumber: "bbbbbbbbbbbb", AuthorizationID: "CCCCCC"}

	got, err := BuildPaymentRequest(realQR, "2026-09-30 03:00:00.000Z", ids)
	if err != nil {
		t.Fatalf("BuildPaymentRequest() error = %v", err)
	}

	want := PaymentRequest{
		Command: "qr-payment-credit",
		Data: PaymentData{
			DateTime:                          "2026-09-30 03:00:00.000Z",
			CustomerReferenceNumber:           "ALTO-API-NMS-aaaaaaaaaaaa",
			AuthorizationID:                   "CCCCCC",
			CurrencyCode:                      "IDR",
			Amount:                            50000,
			Fee:                               0,
			IssuerNNS:                         "93600821",
			AcquirerNNS:                       "93600858",
			NationalMID:                       "MT58530503",
			AdditionalData:                    "0520A503321013864160FF4107036590812Q161 Payment",
			TerminalLabel:                     "659",
			ForwardingCustomerReferenceNumber: "bbbbbbbbbbbb",
			Merchant: Merchant{
				PAN: "936008580287697876", ID: "MT58530503", Criteria: "UKE", Name: "Pupuk Kalteng",
				City: "JAKARTA BARAT", MCC: "5251", PostalCode: "11550", CountryCode: "ID",
			},
			Customer: Customer{PAN: "936008580287697876", Name: "Tes", AccountType: "UNSPECIFIED"},
		},
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("BuildPaymentRequest()\n  got:  %+v\n  want: %+v", got, want)
	}
	if ref := got.ReferenceLabel(); ref != "A503321013864160FF41" {
		t.Errorf("ReferenceLabel() = %q, want A503321013864160FF41", ref)
	}
}

func TestBuildPaymentRequest_RejectsQRWithoutAmount(t *testing.T) {
	noAmount := strings.Replace(realQR, "540850000.00", "", 1)
	if _, err := BuildPaymentRequest(noAmount, "2026-09-30 03:00:00.000Z", Identifiers{}); err == nil {
		t.Error("BuildPaymentRequest() error = nil, want error for a QR without tag 54")
	}
}

func TestNewIdentifiers_Formats(t *testing.T) {
	ids, err := NewIdentifiers()
	if err != nil {
		t.Fatalf("NewIdentifiers() error = %v", err)
	}
	if !strings.HasPrefix(ids.CustomerReferenceNumber, "ALTO-API-NMS-") || len(ids.CustomerReferenceNumber) != len("ALTO-API-NMS-")+12 {
		t.Errorf("CustomerReferenceNumber = %q", ids.CustomerReferenceNumber)
	}
	if len(ids.ForwardingReferenceNumber) != 12 || len(ids.AuthorizationID) != 6 || ids.AuthorizationID != strings.ToUpper(ids.AuthorizationID) {
		t.Errorf("ForwardingReferenceNumber %q / AuthorizationID %q have wrong format", ids.ForwardingReferenceNumber, ids.AuthorizationID)
	}
}
```

`internal/altosim/client_test.go`:

```go
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `go test ./internal/altosim/`
Expected: build FAIL — package has no non-test Go files / `undefined: ParseEMV`.

- [ ] **Step 3: Implement**

`internal/altosim/emv.go`:

```go
// Package altosim pays a UAT QRIS through Alto's issuer simulator
// (POST /altopay/qr-payment/payment, see docs/manjo-collection/QR Payment.yml), so the
// generate → pay → announce flow can be exercised without the Alto website.
// Test environment only.
package altosim

import (
	"fmt"
	"strconv"
)

// TLV is one level of an EMV QR payload: two-digit tag → value.
type TLV map[string]string

// ParseEMV parses one level of EMV "tag(2) length(2) value" data. Nested templates such
// as tags 26 and 62 are parsed by calling ParseEMV again on their value.
func ParseEMV(s string) (TLV, error) {
	out := TLV{}
	for i := 0; i < len(s); {
		if i+4 > len(s) {
			return nil, fmt.Errorf("altosim: truncated EMV field at offset %d", i)
		}
		tag := s[i : i+2]
		n, err := strconv.Atoi(s[i+2 : i+4])
		if err != nil {
			return nil, fmt.Errorf("altosim: invalid length for tag %s: %w", tag, err)
		}
		start := i + 4
		if start+n > len(s) {
			return nil, fmt.Errorf("altosim: tag %s length %d overruns the payload", tag, n)
		}
		out[tag] = s[start : start+n]
		i = start + n
	}
	return out, nil
}
```

`internal/altosim/request.go`:

```go
package altosim

import (
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"strconv"
	"strings"
)

// issuerNNS is the simulated paying bank, as in the collection.
const issuerNNS = "93600821"

// PaymentRequest is the qr-payment-credit body.
type PaymentRequest struct {
	Command string      `json:"command"`
	Data    PaymentData `json:"data"`
}

type PaymentData struct {
	DateTime                          string   `json:"date_time"`
	CustomerReferenceNumber           string   `json:"customer_reference_number"`
	AuthorizationID                   string   `json:"authorization_id"`
	CurrencyCode                      string   `json:"currency_code"`
	Amount                            int64    `json:"amount"`
	Fee                               int64    `json:"fee"`
	IssuerNNS                         string   `json:"issuer_nns"`
	AcquirerNNS                       string   `json:"acquirer_nns"`
	NationalMID                       string   `json:"national_mid"`
	AdditionalData                    string   `json:"additional_data"`
	TerminalLabel                     string   `json:"terminal_label"`
	ForwardingCustomerReferenceNumber string   `json:"forwarding_customer_reference_number"`
	Merchant                          Merchant `json:"merchant"`
	Customer                          Customer `json:"customer"`
}

type Merchant struct {
	PAN         string `json:"pan"`
	ID          string `json:"id"`
	Criteria    string `json:"criteria"`
	Name        string `json:"name"`
	City        string `json:"city"`
	MCC         string `json:"mcc"`
	PostalCode  string `json:"postal_code"`
	CountryCode string `json:"country_code"`
}

type Customer struct {
	PAN         string `json:"pan"`
	Name        string `json:"name"`
	AccountType string `json:"account_type"`
}

// Identifiers are the per-payment random references Alto expects.
type Identifiers struct {
	CustomerReferenceNumber   string
	ForwardingReferenceNumber string
	AuthorizationID           string
}

// NewIdentifiers returns fresh random identifiers in the collection's formats.
func NewIdentifiers() (Identifiers, error) {
	b := make([]byte, 15)
	if _, err := rand.Read(b); err != nil {
		return Identifiers{}, fmt.Errorf("altosim: failed to generate identifiers: %w", err)
	}
	h := hex.EncodeToString(b) // 30 hex characters
	return Identifiers{
		CustomerReferenceNumber:   "ALTO-API-NMS-" + h[:12],
		ForwardingReferenceNumber: h[12:24],
		AuthorizationID:           strings.ToUpper(h[24:30]),
	}, nil
}

// BuildPaymentRequest pays qrContent in full. Merchant fields come from the QR itself —
// never from the collection's hard-coded Ayoborong example.
func BuildPaymentRequest(qrContent, timestamp string, ids Identifiers) (PaymentRequest, error) {
	top, err := ParseEMV(qrContent)
	if err != nil {
		return PaymentRequest{}, err
	}
	account, err := ParseEMV(top["26"])
	if err != nil {
		return PaymentRequest{}, fmt.Errorf("altosim: merchant account (tag 26): %w", err)
	}
	additional, err := ParseEMV(top["62"])
	if err != nil {
		return PaymentRequest{}, fmt.Errorf("altosim: additional data (tag 62): %w", err)
	}

	pan, merchantID := account["01"], account["02"]
	if len(pan) < 8 || merchantID == "" {
		return PaymentRequest{}, fmt.Errorf("altosim: QR lacks merchant PAN/ID (tag 26.01/26.02)")
	}
	if additional["05"] == "" {
		return PaymentRequest{}, fmt.Errorf("altosim: QR lacks a reference label (tag 62.05)")
	}
	amount, err := parseAmount(top["54"])
	if err != nil {
		return PaymentRequest{}, err
	}

	return PaymentRequest{
		Command: "qr-payment-credit",
		Data: PaymentData{
			DateTime:                          timestamp,
			CustomerReferenceNumber:           ids.CustomerReferenceNumber,
			AuthorizationID:                   ids.AuthorizationID,
			CurrencyCode:                      "IDR",
			Amount:                            amount,
			Fee:                               0,
			IssuerNNS:                         issuerNNS,
			AcquirerNNS:                       pan[:8],
			NationalMID:                       merchantID,
			AdditionalData:                    top["62"],
			TerminalLabel:                     additional["07"],
			ForwardingCustomerReferenceNumber: ids.ForwardingReferenceNumber,
			Merchant: Merchant{
				PAN: pan, ID: merchantID, Criteria: account["03"], Name: top["59"],
				City: top["60"], MCC: top["52"], PostalCode: top["61"], CountryCode: top["58"],
			},
			Customer: Customer{PAN: pan, Name: "Tes", AccountType: "UNSPECIFIED"},
		},
	}, nil
}

// ReferenceLabel returns the QR's reference label (tag 62.05), which is Manjo's referenceNo.
func (r PaymentRequest) ReferenceLabel() string {
	additional, err := ParseEMV(r.Data.AdditionalData)
	if err != nil {
		return ""
	}
	return additional["05"]
}

// parseAmount converts tag 54 ("50000.00") to whole Rupiah.
func parseAmount(value string) (int64, error) {
	whole, _, _ := strings.Cut(value, ".")
	n, err := strconv.ParseInt(whole, 10, 64)
	if err != nil || n <= 0 {
		return 0, fmt.Errorf("altosim: invalid transaction amount (tag 54) %q", value)
	}
	return n, nil
}
```

`internal/altosim/client.go`:

```go
package altosim

import (
	"bytes"
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
)

// PaymentPath is Alto's pay-a-QR endpoint, and the canonical path it signs.
const PaymentPath = "/altopay/qr-payment/payment"

// FormatTimestamp renders t the way Alto signs it: UTC "2006-01-02 15:04:05.000Z".
func FormatTimestamp(t time.Time) string {
	return t.UTC().Format("2006-01-02 15:04:05.000Z")
}

// Sign computes X-Alto-Signature: hex HMAC-SHA256 keyed with validationKey over
// "POST:" + PaymentPath + ":" + apiKey + ":" + hex(sha256(body)) + ":" + timestamp.
func Sign(apiKey, validationKey string, body []byte, timestamp string) string {
	bodyHash := sha256.Sum256(body)
	plain := http.MethodPost + ":" + PaymentPath + ":" + apiKey + ":" + hex.EncodeToString(bodyHash[:]) + ":" + timestamp
	mac := hmac.New(sha256.New, []byte(validationKey))
	mac.Write([]byte(plain))
	return hex.EncodeToString(mac.Sum(nil))
}

// Client calls Alto's simulator.
type Client struct {
	BaseURL       string
	APIKey        string
	ValidationKey string
	HTTPClient    *http.Client // nil = 15s timeout default
}

// Pay sends req, signed with req.Data.DateTime as the timestamp, and returns Alto's
// HTTP status and raw body.
func (c *Client) Pay(ctx context.Context, req PaymentRequest) (int, []byte, error) {
	body, err := json.Marshal(req)
	if err != nil {
		return 0, nil, fmt.Errorf("altosim: failed to marshal payment: %w", err)
	}
	timestamp := req.Data.DateTime

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, strings.TrimRight(c.BaseURL, "/")+PaymentPath, bytes.NewReader(body))
	if err != nil {
		return 0, nil, err
	}
	httpReq.Header.Set("Content-Type", "application/json")
	httpReq.Header.Set("X-Alto-Timestamp", timestamp)
	httpReq.Header.Set("X-Alto-Key", c.APIKey)
	httpReq.Header.Set("X-Alto-Signature", Sign(c.APIKey, c.ValidationKey, body, timestamp))

	hc := c.HTTPClient
	if hc == nil {
		hc = &http.Client{Timeout: 15 * time.Second}
	}
	resp, err := hc.Do(httpReq)
	if err != nil {
		return 0, nil, fmt.Errorf("altosim: payment request failed: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return resp.StatusCode, nil, fmt.Errorf("altosim: failed to read response: %w", err)
	}
	return resp.StatusCode, respBody, nil
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `go vet ./internal/altosim/ && go test ./internal/altosim/ -v`
Expected: PASS (all 8 tests, including the `openssl` known-answer signature).

- [ ] **Step 5: Commit**

```bash
git add internal/altosim
git commit -m "$(cat <<'EOF'
feat(altosim): build and sign Alto simulator payments from a QRIS

Parses the EMV payload of our own qrContent (merchant, amount,
reference label in tag 62.05) into Alto's qr-payment-credit request
and signs it with HMAC-SHA256 like the collection does.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: `cmd/altosim` CLI + sandbox env + docs

**Files:**
- Create: `cmd/altosim/main.go`
- Modify: `.env.sandbox.example`
- Modify: `docs/running-locally.md`

**Interfaces:**
- Consumes: everything from Task 9; `database.NewPool`, `sqlc.New(pool).GetTransactionByID` (existing).
- Produces: `go run ./cmd/altosim -tx <transaction_id>` / `-qr <qrContent>`; env `ALTO_BASE_URL` (default `https://mmsapi-test.manjo.co.id`), `ALTO_API_KEY`, `ALTO_VALIDATION_KEY`.

- [ ] **Step 1: Implement the CLI**

`cmd/altosim/main.go`:

```go
// Command altosim pays a UAT QRIS through Alto's simulator, to exercise the
// generate → pay → soundbox flow end to end. Test environment only.
//
//	go run ./cmd/altosim -tx TRX-20260930-XXXXXX   # qris_payload read from DATABASE_URL
//	go run ./cmd/altosim -qr "00020101021226..."
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io/fs"
	"os"
	"time"

	"github.com/joho/godotenv"

	"service-payment-bridge/internal/altosim"
	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
)

const defaultBaseURL = "https://mmsapi-test.manjo.co.id"

func main() {
	txID := flag.String("tx", "", "transaction_id whose qris_payload to pay (read from DATABASE_URL)")
	qr := flag.String("qr", "", "qrContent to pay (alternative to -tx)")
	flag.Parse()
	if (*txID == "") == (*qr == "") {
		fmt.Fprintln(os.Stderr, `usage: altosim -tx TRX-... | -qr "000201..."`)
		os.Exit(2)
	}

	if err := run(*txID, *qr); err != nil {
		fmt.Fprintln(os.Stderr, "altosim:", err)
		os.Exit(1)
	}
}

func run(txID, qrContent string) error {
	for _, file := range []string{".env", ".env.sandbox"} {
		if err := godotenv.Load(file); err != nil && !errors.Is(err, fs.ErrNotExist) {
			return fmt.Errorf("load %s: %w", file, err)
		}
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	if txID != "" {
		var err error
		if qrContent, err = loadQR(ctx, txID); err != nil {
			return err
		}
	}

	apiKey, validationKey := os.Getenv("ALTO_API_KEY"), os.Getenv("ALTO_VALIDATION_KEY")
	if apiKey == "" || validationKey == "" {
		return errors.New("ALTO_API_KEY and ALTO_VALIDATION_KEY must be set (see .env.sandbox.example)")
	}
	baseURL := os.Getenv("ALTO_BASE_URL")
	if baseURL == "" {
		baseURL = defaultBaseURL
	}

	ids, err := altosim.NewIdentifiers()
	if err != nil {
		return err
	}
	req, err := altosim.BuildPaymentRequest(qrContent, altosim.FormatTimestamp(time.Now()), ids)
	if err != nil {
		return err
	}

	fmt.Printf("paying %s (%s) Rp%d, reference %s via %s\n",
		req.Data.Merchant.Name, req.Data.NationalMID, req.Data.Amount, req.ReferenceLabel(), baseURL)

	client := &altosim.Client{BaseURL: baseURL, APIKey: apiKey, ValidationKey: validationKey}
	status, body, err := client.Pay(ctx, req)
	if err != nil {
		return err
	}
	fmt.Printf("HTTP %d\n%s\n", status, body)
	if status >= 300 {
		return fmt.Errorf("alto answered HTTP %d", status)
	}
	return nil
}

func loadQR(ctx context.Context, txID string) (string, error) {
	dbURL := os.Getenv("DATABASE_URL")
	if dbURL == "" {
		return "", errors.New("DATABASE_URL must be set to use -tx")
	}
	pool, err := database.NewPool(ctx, dbURL)
	if err != nil {
		return "", err
	}
	defer pool.Close()

	tx, err := sqlc.New(pool).GetTransactionByID(ctx, txID)
	if err != nil {
		return "", fmt.Errorf("transaction %s: %w", txID, err)
	}
	if !tx.QrisPayload.Valid {
		return "", fmt.Errorf("transaction %s has no qris_payload (status %s)", txID, tx.Status)
	}
	return tx.QrisPayload.String, nil
}
```

- [ ] **Step 2: Verify the CLI's offline behavior**

Run:
```bash
go build ./... && go vet ./cmd/altosim/
go run ./cmd/altosim; echo "exit=$?"
go run ./cmd/altosim -qr "garbage"; echo "exit=$?"
```
Expected: first prints the usage line with `exit=2`; second prints `altosim: ...` (either the missing `ALTO_*` keys or an EMV parse error) with `exit=1`. No network call is made in either case.

- [ ] **Step 3: Update `.env.sandbox.example`**

Replace the whole file with:

```
MANJO_SANDBOX_BASE_URL=https://snap-uat.manjo.co.id/api
MANJO_SANDBOX_CLIENT_KEY=
MANJO_SANDBOX_PRIVATE_KEY=
MANJO_SANDBOX_CLIENT_SECRET=
MANJO_SANDBOX_MERCHANT_ID=
ALTO_BASE_URL=https://mmsapi-test.manjo.co.id
ALTO_API_KEY=
ALTO_VALIDATION_KEY=
```

- [ ] **Step 4: Document `altosim` in `docs/running-locally.md`**

In Section 2, append to the bullet that starts with "- **`.env.sandbox`** berisi kredensial Manjo UAT":

```markdown
 File ini juga berisi `ALTO_API_KEY` dan `ALTO_VALIDATION_KEY` untuk simulator pembayaran Alto (Section 5). Nilainya ada di `docs/manjo-collection/QR Payment.yml` (`API_KEY` dan `VALIDATION_KEY`).
```

In Section 5, after the subsection "### Simulasi request tanpa device fisik" (after its closing paragraph "Kalau sukses, balasannya …"), add:

````markdown
### Simulasi pembayaran (bayar → soundbox bunyi)

Pembayaran di UAT bisa disimulasikan tanpa m-banking lewat simulator Alto:

1. Generate QR dari device (menu QRIS Dinamis), atau lewat `mosquitto_pub` seperti di atas.
2. Ambil `transaction_id` terbaru:
   ```bash
   docker compose exec postgres psql -U payment_bridge -d payment_bridge -c \
     "SELECT transaction_id, amount, status FROM transactions ORDER BY created_at DESC LIMIT 1;"
   ```
3. Bayar QR-nya (harus sebelum QR kedaluwarsa, ~7,5 menit):
   ```bash
   go run ./cmd/altosim -tx TRX-20260930-XXXXXX
   ```
   Output-nya menampilkan merchant, nominal, reference, dan respons Alto.
4. Dalam ~3 detik soundbox berbunyi "…lima puluh ribu…", log server menampilkan `"payment detected"`, dan status transaksinya menjadi `PAID`.

Kalau QR dibiarkan tanpa dibayar, sekitar 7,5 menit kemudian log menampilkan `"transaction expired"` dan soundbox tidak berbunyi.

`-qr "<qrContent>"` bisa dipakai sebagai pengganti `-tx` kalau `qrContent`-nya sudah ada di tangan.
````

- [ ] **Step 5: Commit**

```bash
git add cmd/altosim .env.sandbox.example docs/running-locally.md
git commit -m "$(cat <<'EOF'
feat(altosim): add CLI to pay UAT QRs through Alto's simulator

go run ./cmd/altosim -tx <id> reads the transaction's qrContent and
pays it via Alto, so the generate -> pay -> soundbox flow can be
repeated with one command. Keys come from .env.sandbox.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Architecture/flow docs + full verification + UAT end-to-end

**Files:**
- Modify: `docs/process-flow.md`
- Modify: `docs/architecture.md`

**Interfaces:** none (docs and verification only).

- [ ] **Step 1: `docs/architecture.md` Section 7.3**

Replace everything from the line `### 7.3 Service → Q161 (Payment Notification)` up to (not including) the `---` line before `## 8. Kontrak Eksternal (Service ↔ Manjo)` with:

````markdown
### 7.3 Service → Q161 (Payment Notification)

**Topic:** `"topic_" + device_id`, topic yang sama dengan balasan QR (Section 7.2).

**Payload:** plain text berisi daftar path file audio di device, dipisah `+`. Firmware mengenali payload ini dari substring `.mp3`, lalu memutar tiap file berurutan (`docs/eclipse/src/mqtt.c:87-124`).

Contoh Rp50.000:

```text
/ext/awal-qris.mp3+/ext/lima.mp3+/ext/puluh.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3
```

Disusun oleh `internal/voice` dari `transactions.amount`:
- pembuka `awal-qris`;
- klip nominal (aturan "se-": `sepuluh`/`sebelas`/`seratus`/`seribu`);
- penutup `akhir-berhasil`, yang sudah mengandung kata "rupiah".

Nama file memakai set `mp3-api` tanpa akhiran `_adr`. Payload ini hanya dikirim untuk transaksi yang menjadi `PAID`. Saat ini pembayaran dideteksi lewat polling Query Payment (`docs/process-flow.md` Flow 5).
````

- [ ] **Step 2: `docs/process-flow.md` — table of contents**

Replace

```markdown
5. [Flow 4 — Auto-Expire Job](#5-flow-4--auto-expire-job)
6. [Ringkasan Data Touchpoint per Tabel](#6-ringkasan-data-touchpoint-per-tabel)
```

with

```markdown
5. [Flow 4 — Auto-Expire Job](#5-flow-4--auto-expire-job)
6. [Flow 5 — Payment Polling](#6-flow-5--payment-polling)
7. [Ringkasan Data Touchpoint per Tabel](#7-ringkasan-data-touchpoint-per-tabel)
```

- [ ] **Step 3: `docs/process-flow.md` — Flow 4 and Flow 5**

Replace the whole `## 5. Flow 4 — Auto-Expire Job` section (from that heading up to, not including, the `---` before `## 6. Ringkasan Data Touchpoint per Tabel`) with:

```markdown
## 5. Flow 4 — Auto-Expire Job

> **Status:** tidak dibangun sebagai job terpisah. Kedaluwarsa ditangani oleh Flow 5:
> - Selama QR masih berlaku, transaksi dicek ke Manjo tiap 3 detik.
> - Begitu QR kedaluwarsa (~7,5 menit), Manjo menjawab `403` dengan `responseCode` `4035100`, lalu transaksi ditandai `EXPIRED`.
> - Kalau query terus gagal, jaring pengaman menandai `EXPIRED` transaksi yang masih `QR_GENERATED` 2 menit setelah `expire_at`.
>
> Seperti sebelumnya, kedaluwarsa **tidak** dikirim ke device.

---

## 6. Flow 5 — Payment Polling

**Trigger:** Payment Poller di dalam service, tiap 1 detik.
**Berakhir saat:** Transaksi keluar dari `QR_GENERATED` (`PAID`/`EXPIRED`/`FAILED`/`CANCELLED`/`REFUNDED`). Kalau `PAID`, berakhir saat soundbox memutar pengumuman.

| # | Aktor/Komponen | Aksi | Data Dibaca | Data Ditulis | Validasi/Kondisi | Kalau Gagal |
|---|---|---|---|---|---|---|
| 1 | Payment Poller | Klaim ≤20 transaksi yang jatuh tempo | `transactions` WHERE `status='QR_GENERATED' AND next_query_at <= now()` (`FOR UPDATE SKIP LOCKED`) | `transactions.next_query_at = now() + PAYMENT_POLL_INTERVAL` | Satu transaksi tidak pernah diklaim dua instance | Klaim gagal → log, coba lagi detik berikutnya |
| 2 | Transaction Service | Resolve kredensial merchant lewat `device_id` | `devices`, `merchants`, `tenants` | — | Tetap dicek walau device/merchant `INACTIVE` | Dicatat sebagai query error, dicoba lagi 3 detik kemudian |
| 3 | Manjo Client | `POST /v1.0/qr/qr-mpm-query` (format collection: tanpa `X-CLIENT-KEY`, `X-PARTNER-ID` = client key, `serviceCode: "47"`) | `transactions.reference_no`, `transaction_id`, `external_id` | `manjo_api_logs` (`QUERY_PAYMENT`), **kecuali** hasil masih pending | — | Timeout/5xx/401/kode tak dikenal → dicoba lagi |
| 4 | Transaction Service | Petakan hasil dengan **skema Query** (bukan skema Notification) | — | — | `00`→`PAID`; `01`/`02`/`03`→tetap; `05`→`CANCELLED`; `06`→`FAILED`; `04`→`REFUNDED` (anomali); `403`+`4035100`→`EXPIRED` | — |
| 5 | Transaction Service | Terapkan transisi bersyarat | — | `transactions`: `status`, `manjo_status_code`, `paid_at` (= `paidTime`), `next_query_at = NULL` WHERE `status = 'QR_GENERATED'` | 0 baris berubah → sudah diproses instance lain, berhenti | Error DB → log, dicoba lagi |
| 6 | Transaction Service (jaring pengaman) | Masih `QR_GENERATED` dan `now() > expire_at + 2 menit` → `EXPIRED` | `transactions.expire_at` | `transactions.status = 'EXPIRED'` | — | — |
| 7 | Announcer | Hanya kalau baru menjadi `PAID`: susun audio dari `amount`, publish ke `topic_{device_id}` (QoS 1), maksimal 3 percobaan | `transactions.amount`, `device_id` | `mqtt_messages` (OUTBOUND, `PROCESSED`/`FAILED`) | — | Gagal 3× → `mqtt_messages` `FAILED` + log `"announcement failed"` |
| 8 | Q161 Pro | Putar audio, misalnya "…lima puluh ribu…" | — | — | — | (di luar scope Service) |
```

Then change the heading `## 6. Ringkasan Data Touchpoint per Tabel` to `## 7. Ringkasan Data Touchpoint per Tabel`, and in its table replace the rows for `transactions`, `mqtt_messages` and `manjo_api_logs` with:

```markdown
| `transactions` | Flow 1 (step 6, 9, 11a/11b, 12b), Flow 3 (step 9), Flow 5 (step 1, 5, 6) | Flow 1 (step 5 — via `devices`, step 13a), Flow 3 (step 5, 6a, 6b, 8, 11, 12 — via `device_id`), Flow 5 (step 1, 3, 6, 7) |
| `mqtt_messages` | Flow 1 (step 4b, 7, 13a, 13b), Flow 3 (step 12), Flow 5 (step 7) | (dibaca terpisah saat tracing manual, bukan bagian dari flow otomatis) |
| `manjo_api_logs` | Flow 1 (step 10, 11a, 11b), Flow 2 (step 3, 4a, 4b), Flow 3 (step 3, 10), Flow 5 (step 3) | (dibaca terpisah saat tracing manual) |
```

- [ ] **Step 4: Full automated verification**

Stop any `go run ./cmd/server`, then:

```bash
export DATABASE_URL="postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
migrate -path migrations -database "$DATABASE_URL" version
sqlc diff
gofmt -l ./cmd ./internal
go vet ./...
go test ./... -race
```
Expected: version `14`; `sqlc diff` and `gofmt -l` print nothing; every package `ok` (including `TestGenerateQRFlow_EndToEnd` and `internal/paymentpoller`).

Check that tests left nothing behind:

```bash
docker compose exec -T postgres psql -U payment_bridge -d payment_bridge -t -c \
  "SELECT count(*) FROM transactions WHERE merchant_id IN ('E2E-TEST-MERCHANT','TXSVC-TEST-MERCHANT','ANN-TEST-MERCHANT','POLL-TEST-MERCHANT');" -c \
  "SELECT count(*) FROM manjo_api_logs WHERE operation='ACCESS_TOKEN' AND coalesce(response_body->>'responseCode','') = '';"
```
Expected: `0` and `0`.

- [ ] **Step 5: Manual UAT end-to-end (with the user and the physical device)**

1. `go run ./cmd/server` (with `.env.sandbox` including the `ALTO_*` keys).
2. On the Q161 Pro: QRIS Dinamis → e.g. Rp50.000 → the QR is shown.
3. `go run ./cmd/altosim -tx <newest transaction_id>` → Alto answers `HTTP 200`.
4. Within ~3 s the soundbox says the amount; the server logs `"payment detected"`; the transaction is `PAID` with a `PROCESSED` OUTBOUND row in `mqtt_messages` whose payload is the `.mp3` list.
5. Generate a second QR and leave it unpaid → ~7.5 min later `"transaction expired"` with `"via":"manjo"`, and no sound.

If step 3 is rejected by Alto, report its response verbatim: the request mapping (spec Section 13) is inferred and is validated here for the first time.

- [ ] **Step 6: Commit**

```bash
git add docs/architecture.md docs/process-flow.md
git commit -m "$(cat <<'EOF'
docs: document payment polling flow and audio payload

architecture.md 7.3 now describes the real .mp3 payload sent on
PAID; process-flow.md gains Flow 5 (payment polling) and Flow 4 notes
that expiry is handled there.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```
