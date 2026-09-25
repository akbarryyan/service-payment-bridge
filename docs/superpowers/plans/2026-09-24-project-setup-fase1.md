# Project Setup (Fase 1 — Fondasi) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Membangun fondasi project Go untuk Payment Bridge Service — struktur folder, migration database, koneksi dasar ke PostgreSQL & MQTT broker, config loading, dan HTTP server minimal dengan health check — tanpa business logic apa pun.

**Architecture:** `cmd/server/main.go` sebagai entrypoint yang wiring empat komponen independen (`internal/config`, `internal/database`, `internal/mqttclient`, `internal/httpserver`) secara berurutan saat startup, lalu menjalankan Echo HTTP server sampai menerima sinyal shutdown. Setiap komponen punya interface kecil yang testable secara terisolasi (mis. `Pinger` untuk health check) tanpa perlu seluruh aplikasi jalan.

**Tech Stack:** Go 1.23, `pgx/v5` + `pgxpool`, `sqlc`, `eclipse/paho.mqtt.golang`, `labstack/echo/v4`, `kelseyhightower/envconfig`, `golang-migrate`, `log/slog` (stdlib), Docker Compose (Postgres 16 + Mosquitto 2).

## Global Constraints

- Module path: `service-payment-bridge` (tanpa domain — akan di-rename via `go mod edit -module` begitu repo GitLab kantor dibuat)
- Go version: `go 1.23` di go.mod (toolchain lokal terpasang 1.26.5)
- **Tidak ada business logic** di plan ini: signature builder, Manjo Client, MQTT consumer/publisher (subscribe & parse), transaction service, webhook notification handler, merchant resolver, retry queue — semua itu di luar scope (Fase 2-4)
- **Tidak ada langkah `git commit`** di task manapun — repo ini sengaja belum di-git-init (keputusan eksplisit user). Tiap task diakhiri dengan step verifikasi, bukan commit. Centang checkbox task setelah verifikasi terakhir lulus.
- Semua DDL migration harus sama persis dengan `docs/schema.md` (tidak ada perubahan skema) — `manjo_api_operation` sudah termasuk value `QUERY_PAYMENT`
- Docker image yang dipakai: `postgres:16`, `eclipse-mosquitto:2` — jangan ganti tag
- Config field mengikuti `docs/superpowers/specs/2026-09-24-project-setup-fase1-design.md` Section 5 — **tanpa** `SECRET_MANAGER_*` (belum ada pemakainya di fase ini)

---

## File Structure

```
service-payment-bridge/
├── cmd/server/main.go
├── internal/
│   ├── config/
│   │   ├── config.go
│   │   └── config_test.go
│   ├── database/
│   │   ├── db.go
│   │   ├── db_test.go
│   │   └── queries/
│   │       └── healthcheck.sql
│   ├── mqttclient/
│   │   ├── client.go
│   │   └── client_test.go
│   └── httpserver/
│       ├── healthz.go
│       └── healthz_test.go
├── migrations/
│   ├── 000001_extension_and_enums.up.sql
│   ├── 000001_extension_and_enums.down.sql
│   ├── 000002_create_merchants.up.sql
│   ├── 000002_create_merchants.down.sql
│   ├── 000003_create_transactions.up.sql
│   ├── 000003_create_transactions.down.sql
│   ├── 000004_create_mqtt_messages.up.sql
│   ├── 000004_create_mqtt_messages.down.sql
│   ├── 000005_create_manjo_api_logs.up.sql
│   ├── 000005_create_manjo_api_logs.down.sql
│   ├── 000006_prevent_final_status_change_trigger.up.sql
│   └── 000006_prevent_final_status_change_trigger.down.sql
├── docker/
│   └── mosquitto.conf
├── docker-compose.yml
├── sqlc.yaml
├── .env.example
├── .gitignore
├── go.mod
└── go.sum
```

`internal/database/sqlc/` (generated code dari `sqlc generate`) dibuat otomatis di Task 5 — tidak ditulis manual.

---

### Task 1: Go Module Init + Directory Skeleton

**Files:**
- Create: `go.mod` (via `go mod init`, bukan ditulis manual)
- Create: `.gitignore`
- Create: `cmd/server/.gitkeep`, `internal/config/.gitkeep`, `internal/database/.gitkeep`, `internal/mqttclient/.gitkeep`, `internal/httpserver/.gitkeep`, `migrations/.gitkeep` *(dihapus lagi begitu ada file asli di folder tsb — hanya menjaga struktur direktori kosong terlihat di file listing)*

**Interfaces:**
- Produces: module path `service-payment-bridge`, dipakai sebagai prefix import di semua task berikutnya (`service-payment-bridge/internal/config`, dst.)

- [x] **Step 1: Init Go module**

Run:
```bash
cd /home/akbar/Kerjaan/repository/service-payment-bridge
go mod init service-payment-bridge
```
Expected: file `go.mod` dibuat berisi `module service-payment-bridge` dan `go 1.26.5` (atau versi toolchain lokal — akan disamakan ke `go 1.23` di Step 2).

- [x] **Step 2: Set minimum Go version ke 1.23**

Edit `go.mod`, ubah baris `go 1.26.5` (atau apa pun versi yang di-generate) menjadi:
```
go 1.23
```

- [x] **Step 3: Buat struktur folder**

Run:
```bash
mkdir -p cmd/server internal/config internal/database/queries internal/mqttclient internal/httpserver migrations docker
```

- [x] **Step 4: Buat `.gitignore`**

Isi file `.gitignore`:
```
/bin/
*.exe
*.test
.env
.DS_Store
```

- [x] **Step 5: Verifikasi**

Run:
```bash
go build ./...
```
Expected: tidak ada error (belum ada file `.go` sama sekali, jadi output kosong/`no Go files` — itu normal, yang penting exit code `0`). Cek juga struktur folder dengan `find . -type d -not -path './.git*'` menunjukkan semua folder di atas ada.

---

### Task 2: Docker Compose — Local Dev Environment

**Files:**
- Create: `docker-compose.yml`
- Create: `docker/mosquitto.conf`
- Create: `.env.example`

**Interfaces:**
- Produces: Postgres reachable di `localhost:15432` (host port digeser dari default `5432` karena sudah dipakai instance Postgres native yang tidak terkait project ini; user/password/db: `payment_bridge`/`payment_bridge`/`payment_bridge`), Mosquitto reachable di `localhost:11883` (host port digeser dari default `1883` karena sudah dipakai container mosquitto lain yang tidak terkait project ini; anonymous, dev only) — dipakai sebagai target koneksi oleh Task 3, 5, 6, 8

- [x] **Step 1: Buat `docker/mosquitto.conf`**

```
listener 1883
allow_anonymous true
persistence false
```

> Catatan: `allow_anonymous true` **hanya untuk dev lokal**. Production wajib TLS + auth per-device sesuai `architecture.md` Section 14 — itu bukan scope Fase 1.

- [x] **Step 2: Buat `docker-compose.yml`**

```yaml
services:
  postgres:
    image: postgres:16
    environment:
      POSTGRES_USER: payment_bridge
      POSTGRES_PASSWORD: payment_bridge
      POSTGRES_DB: payment_bridge
    ports:
      - "5432:5432"
    volumes:
      - postgres_data:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U payment_bridge"]
      interval: 5s
      timeout: 3s
      retries: 5

  mosquitto:
    image: eclipse-mosquitto:2
    ports:
      - "11883:1883"
    volumes:
      - ./docker/mosquitto.conf:/mosquitto/config/mosquitto.conf

volumes:
  postgres_data:
```

- [x] **Step 3: Buat `.env.example`**

```
HTTP_PORT=8080
DATABASE_URL=postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable
MQTT_BROKER_URL=tcp://localhost:11883
MQTT_USERNAME=
MQTT_PASSWORD=
MANJO_BASE_URL=https://api.manjo.com
WEBHOOK_PUBLIC_URL=https://your-domain.example.com/webhooks/manjo/qr-mpm-notify
LOG_LEVEL=info
```

- [x] **Step 4: Verifikasi — jalankan stack**

Run:
```bash
docker compose up -d
docker compose ps
```
Expected: dua service (`postgres`, `mosquitto`) berstatus `running`/`healthy`.

Run:
```bash
docker compose exec postgres pg_isready -U payment_bridge
```
Expected: output `accepting connections`.

**Biarkan stack ini tetap jalan** — dibutuhkan oleh Task 3, 5, 6, 8 selanjutnya.

---

### Task 3: Database Migrations

**Files:**
- Create: `migrations/000001_extension_and_enums.up.sql`
- Create: `migrations/000001_extension_and_enums.down.sql`
- Create: `migrations/000002_create_merchants.up.sql`
- Create: `migrations/000002_create_merchants.down.sql`
- Create: `migrations/000003_create_transactions.up.sql`
- Create: `migrations/000003_create_transactions.down.sql`
- Create: `migrations/000004_create_mqtt_messages.up.sql`
- Create: `migrations/000004_create_mqtt_messages.down.sql`
- Create: `migrations/000005_create_manjo_api_logs.up.sql`
- Create: `migrations/000005_create_manjo_api_logs.down.sql`
- Create: `migrations/000006_prevent_final_status_change_trigger.up.sql`
- Create: `migrations/000006_prevent_final_status_change_trigger.down.sql`

**Interfaces:**
- Consumes: Postgres dari Task 2 (`localhost:15432`, db `payment_bridge`)
- Produces: skema database lengkap (tabel `merchants`, `transactions`, `mqtt_messages`, `manjo_api_logs`, trigger `trg_prevent_final_status_change`) — dipakai sebagai target `internal/database/queries/healthcheck.sql` di Task 5, dan seluruh business logic Fase 2-4 nantinya

**Precondition:** Task 2 selesai, `docker compose ps` menunjukkan `postgres` healthy.

- [x] **Step 1: Install golang-migrate CLI**

Run:
```bash
go install -tags 'postgres' github.com/golang-migrate/migrate/v4/cmd/migrate@latest
```
Expected: binary `migrate` terpasang di `$(go env GOPATH)/bin` (pastikan direktori ini ada di `$PATH`).

- [x] **Step 2: Tulis `000001_extension_and_enums.up.sql`**

```sql
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TYPE merchant_status AS ENUM (
    'ACTIVE',
    'INACTIVE'
);

CREATE TYPE transaction_status AS ENUM (
    'PENDING',
    'QR_GENERATED',
    'PAID',
    'FAILED',
    'EXPIRED',
    'CANCELLED',
    'REFUNDED'
);

CREATE TYPE mqtt_direction AS ENUM (
    'INBOUND',
    'OUTBOUND'
);

CREATE TYPE mqtt_message_status AS ENUM (
    'RECEIVED',
    'PROCESSED',
    'FAILED'
);

CREATE TYPE manjo_api_direction AS ENUM (
    'OUTBOUND',
    'INBOUND'
);

CREATE TYPE manjo_api_operation AS ENUM (
    'ACCESS_TOKEN',
    'GENERATE_QR',
    'QUERY_PAYMENT',
    'PAYMENT_NOTIFY'
);
```

- [x] **Step 3: Tulis `000001_extension_and_enums.down.sql`**

```sql
DROP TYPE IF EXISTS manjo_api_operation;
DROP TYPE IF EXISTS manjo_api_direction;
DROP TYPE IF EXISTS mqtt_message_status;
DROP TYPE IF EXISTS mqtt_direction;
DROP TYPE IF EXISTS transaction_status;
DROP TYPE IF EXISTS merchant_status;
DROP EXTENSION IF EXISTS "pgcrypto";
```

- [x] **Step 4: Tulis `000002_create_merchants.up.sql`**

```sql
CREATE TABLE merchants (
    id                        UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    merchant_id               VARCHAR(36)  NOT NULL UNIQUE,
    mqtt_topic                VARCHAR(64)  NOT NULL UNIQUE,

    manjo_client_id           VARCHAR(64)  NOT NULL,
    manjo_private_key_ref     VARCHAR(255) NOT NULL,
    manjo_client_secret_ref   VARCHAR(255) NOT NULL,
    manjo_merchant_id         VARCHAR(64)  NOT NULL,
    manjo_channel_id          VARCHAR(5)   NOT NULL,
    manjo_store_id            VARCHAR(64),
    manjo_terminal_id         VARCHAR(16),

    status                    merchant_status NOT NULL DEFAULT 'ACTIVE',

    created_at                TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at                TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_merchants_status ON merchants (status);
```

- [x] **Step 5: Tulis `000002_create_merchants.down.sql`**

```sql
DROP TABLE IF EXISTS merchants;
```

- [x] **Step 6: Tulis `000003_create_transactions.up.sql`**

```sql
CREATE TABLE transactions (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    transaction_id      VARCHAR(64)  NOT NULL UNIQUE,
    reference_no        VARCHAR(64),
    merchant_id         VARCHAR(36)  NOT NULL REFERENCES merchants (merchant_id),

    amount              BIGINT       NOT NULL CHECK (amount > 0),
    status              transaction_status NOT NULL DEFAULT 'PENDING',
    manjo_status_code   VARCHAR(2),

    qris_payload        TEXT,
    external_id         VARCHAR(36),
    expire_at           TIMESTAMPTZ,

    created_at          TIMESTAMPTZ NOT NULL DEFAULT now(),
    paid_at             TIMESTAMPTZ,
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_transactions_merchant_status ON transactions (merchant_id, status);
CREATE INDEX idx_transactions_reference_no ON transactions (reference_no);
CREATE INDEX idx_transactions_expire_at ON transactions (expire_at) WHERE status = 'QR_GENERATED';
```

- [x] **Step 7: Tulis `000003_create_transactions.down.sql`**

```sql
DROP TABLE IF EXISTS transactions;
```

- [x] **Step 8: Tulis `000004_create_mqtt_messages.up.sql`**

```sql
CREATE TABLE mqtt_messages (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    topic           VARCHAR(64) NOT NULL,
    payload         JSONB       NOT NULL,
    direction       mqtt_direction NOT NULL,
    status          mqtt_message_status NOT NULL DEFAULT 'RECEIVED',
    transaction_id  VARCHAR(64) REFERENCES transactions (transaction_id),

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    processed_at    TIMESTAMPTZ,
    error_message   TEXT
);

CREATE INDEX idx_mqtt_messages_transaction_id ON mqtt_messages (transaction_id);
CREATE INDEX idx_mqtt_messages_topic_created_at ON mqtt_messages (topic, created_at DESC);
```

- [x] **Step 9: Tulis `000004_create_mqtt_messages.down.sql`**

```sql
DROP TABLE IF EXISTS mqtt_messages;
```

- [x] **Step 10: Tulis `000005_create_manjo_api_logs.up.sql`**

```sql
CREATE TABLE manjo_api_logs (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    direction       manjo_api_direction NOT NULL,
    operation       manjo_api_operation NOT NULL,
    endpoint        VARCHAR(255) NOT NULL,
    http_status     INTEGER,
    request_body    JSONB,
    response_body   JSONB,
    transaction_id  VARCHAR(64) REFERENCES transactions (transaction_id),
    duration_ms     INTEGER,

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_manjo_api_logs_transaction_id ON manjo_api_logs (transaction_id);
CREATE INDEX idx_manjo_api_logs_operation_created_at ON manjo_api_logs (operation, created_at DESC);
```

- [x] **Step 11: Tulis `000005_create_manjo_api_logs.down.sql`**

```sql
DROP TABLE IF EXISTS manjo_api_logs;
```

- [x] **Step 12: Tulis `000006_prevent_final_status_change_trigger.up.sql`**

```sql
CREATE OR REPLACE FUNCTION prevent_final_status_change()
RETURNS TRIGGER AS $$
BEGIN
    IF OLD.status IN ('PAID', 'FAILED', 'EXPIRED', 'CANCELLED', 'REFUNDED')
       AND NEW.status IS DISTINCT FROM OLD.status THEN
        RAISE EXCEPTION 'Cannot change status from final state % to %', OLD.status, NEW.status;
    END IF;
    NEW.updated_at := now();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_prevent_final_status_change
    BEFORE UPDATE ON transactions
    FOR EACH ROW
    EXECUTE FUNCTION prevent_final_status_change();
```

- [x] **Step 13: Tulis `000006_prevent_final_status_change_trigger.down.sql`**

```sql
DROP TRIGGER IF EXISTS trg_prevent_final_status_change ON transactions;
DROP FUNCTION IF EXISTS prevent_final_status_change();
```

- [x] **Step 14: Jalankan migration up**

Run:
```bash
export DATABASE_URL="postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
migrate -path migrations -database "$DATABASE_URL" up
```
Expected: output menunjukkan 6 migration berhasil (`1/u extension_and_enums ... 6/u prevent_final_status_change_trigger`), tanpa error.

- [x] **Step 15: Verifikasi tabel & trigger benar-benar ada**

Run:
```bash
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c "\dt"
```
Expected: 4 tabel terdaftar (`merchants`, `transactions`, `mqtt_messages`, `manjo_api_logs`).

Run:
```bash
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c "\dT" 
```
Expected: 6 enum type terdaftar termasuk `manjo_api_operation`.

- [x] **Step 16: Verifikasi migration down bekerja (lalu naikkan lagi)**

Run:
```bash
migrate -path migrations -database "$DATABASE_URL" down 6
```
Expected: semua 6 migration di-rollback tanpa error (tabel/enum terhapus).

Run:
```bash
migrate -path migrations -database "$DATABASE_URL" up
```
Expected: 6 migration berhasil lagi — DB kembali ke state siap pakai untuk task selanjutnya.

---

### Task 4: Config Loading

**Files:**
- Create: `internal/config/config.go`
- Test: `internal/config/config_test.go`

**Interfaces:**
- Produces: `config.Config` struct (field: `HTTPPort`, `DatabaseURL`, `MQTTBrokerURL`, `MQTTUsername`, `MQTTPassword`, `ManjoBaseURL`, `WebhookPublicURL`, `LogLevel`, semua `string`), fungsi `config.Load() (*Config, error)` — dipakai oleh `cmd/server/main.go` di Task 8

- [x] **Step 1: Tambah dependency envconfig**

Run:
```bash
go get github.com/kelseyhightower/envconfig@latest
```

- [x] **Step 2: Tulis failing test `internal/config/config_test.go`**

```go
package config

import "testing"

func TestLoad_RequiredFieldsPresent(t *testing.T) {
	t.Setenv("DATABASE_URL", "postgres://user:pass@localhost:5432/db")
	t.Setenv("MQTT_BROKER_URL", "tcp://localhost:1883")

	cfg, err := Load()
	if err != nil {
		t.Fatalf("Load() returned error: %v", err)
	}
	if cfg.DatabaseURL != "postgres://user:pass@localhost:5432/db" {
		t.Errorf("DatabaseURL = %q, want %q", cfg.DatabaseURL, "postgres://user:pass@localhost:5432/db")
	}
	if cfg.MQTTBrokerURL != "tcp://localhost:1883" {
		t.Errorf("MQTTBrokerURL = %q, want %q", cfg.MQTTBrokerURL, "tcp://localhost:1883")
	}
	if cfg.HTTPPort != "8080" {
		t.Errorf("HTTPPort default = %q, want %q", cfg.HTTPPort, "8080")
	}
	if cfg.LogLevel != "info" {
		t.Errorf("LogLevel default = %q, want %q", cfg.LogLevel, "info")
	}
}

func TestLoad_MissingRequiredField(t *testing.T) {
	t.Setenv("MQTT_BROKER_URL", "tcp://localhost:1883")

	_, err := Load()
	if err == nil {
		t.Fatal("Load() expected error for missing DATABASE_URL, got nil")
	}
}
```

- [x] **Step 3: Jalankan test, verifikasi gagal**

Run:
```bash
go test ./internal/config/... -v
```
Expected: FAIL — `config.Load` undefined (package `config` belum ada `config.go`).

- [x] **Step 4: Tulis `internal/config/config.go`**

```go
package config

import "github.com/kelseyhightower/envconfig"

type Config struct {
	HTTPPort         string `envconfig:"HTTP_PORT" default:"8080"`
	DatabaseURL      string `envconfig:"DATABASE_URL" required:"true"`
	MQTTBrokerURL    string `envconfig:"MQTT_BROKER_URL" required:"true"`
	MQTTUsername     string `envconfig:"MQTT_USERNAME"`
	MQTTPassword     string `envconfig:"MQTT_PASSWORD"`
	ManjoBaseURL     string `envconfig:"MANJO_BASE_URL"`
	WebhookPublicURL string `envconfig:"WEBHOOK_PUBLIC_URL"`
	LogLevel         string `envconfig:"LOG_LEVEL" default:"info"`
}

func Load() (*Config, error) {
	var cfg Config
	if err := envconfig.Process("", &cfg); err != nil {
		return nil, err
	}
	return &cfg, nil
}
```

- [x] **Step 5: Jalankan test, verifikasi lulus**

Run:
```bash
go test ./internal/config/... -v
```
Expected: `PASS` untuk `TestLoad_RequiredFieldsPresent` dan `TestLoad_MissingRequiredField`.

---

### Task 5: Database Connection + sqlc Setup

**Files:**
- Create: `internal/database/db.go`
- Test: `internal/database/db_test.go`
- Create: `internal/database/queries/healthcheck.sql`
- Create: `sqlc.yaml`
- Create (generated): `internal/database/sqlc/*.go`

**Interfaces:**
- Consumes: Postgres dari Task 2 & skema dari Task 3
- Produces: `database.NewPool(ctx context.Context, databaseURL string) (*pgxpool.Pool, error)` — `*pgxpool.Pool` punya method `Ping(ctx context.Context) error` yang dipakai sebagai implementasi interface `httpserver.Pinger` di Task 7/8. Package `sqlc` (`internal/database/sqlc`) berisi generated code, siap dipakai query nyata di Fase 2+.

**Precondition:** Task 2 & 3 selesai, Postgres jalan dengan skema ter-migrasi.

- [x] **Step 1: Tambah dependency pgx**

Run:
```bash
go get github.com/jackc/pgx/v5@latest
go get github.com/jackc/pgx/v5/pgxpool@latest
```

- [x] **Step 2: Tulis failing test `internal/database/db_test.go`**

```go
package database

import (
	"context"
	"testing"
	"time"
)

func TestNewPool_ConnectsAndPings(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	pool, err := NewPool(ctx, "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable")
	if err != nil {
		t.Fatalf("NewPool() error = %v (pastikan `docker compose up -d postgres` sedang jalan)", err)
	}
	defer pool.Close()

	if err := pool.Ping(ctx); err != nil {
		t.Fatalf("pool.Ping() error = %v", err)
	}
}

func TestNewPool_InvalidURL(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	_, err := NewPool(ctx, "postgres://baduser:badpass@localhost:15432/nonexistent?sslmode=disable")
	if err == nil {
		t.Fatal("NewPool() expected error for invalid connection, got nil")
	}
}
```

- [x] **Step 3: Jalankan test, verifikasi gagal**

Run:
```bash
go test ./internal/database/... -v
```
Expected: FAIL — `database.NewPool` undefined.

- [x] **Step 4: Tulis `internal/database/db.go`**

```go
package database

import (
	"context"

	"github.com/jackc/pgx/v5/pgxpool"
)

func NewPool(ctx context.Context, databaseURL string) (*pgxpool.Pool, error) {
	pool, err := pgxpool.New(ctx, databaseURL)
	if err != nil {
		return nil, err
	}
	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		return nil, err
	}
	return pool, nil
}
```

- [x] **Step 5: Jalankan test, verifikasi lulus**

Run:
```bash
go test ./internal/database/... -v
```
Expected: `PASS` untuk kedua test (`TestNewPool_ConnectsAndPings`, `TestNewPool_InvalidURL`).

- [x] **Step 6: Tulis query awal `internal/database/queries/healthcheck.sql`**

```sql
-- name: Healthcheck :one
SELECT 1::int AS ok;
```

- [x] **Step 7: Tulis `sqlc.yaml`**

```yaml
version: "2"
sql:
  - engine: "postgresql"
    queries: "internal/database/queries"
    schema: "migrations"
    gen:
      go:
        package: "sqlc"
        out: "internal/database/sqlc"
        sql_package: "pgx/v5"
        emit_json_tags: true
```

- [x] **Step 8: Generate kode sqlc**

Run:
```bash
go run github.com/sqlc-dev/sqlc/cmd/sqlc@latest generate
```
Expected: folder `internal/database/sqlc/` terisi file generated (`db.go`, `models.go`, `healthcheck.sql.go` atau nama serupa), tanpa error.

- [x] **Step 9: Verifikasi build**

Run:
```bash
go build ./...
```
Expected: sukses tanpa error — generated code dari sqlc valid dan bisa di-compile.

---

### Task 6: MQTT Client Connection Wrapper

**Files:**
- Create: `internal/mqttclient/client.go`
- Test: `internal/mqttclient/client_test.go`

**Interfaces:**
- Consumes: Mosquitto dari Task 2 (`tcp://localhost:11883`)
- Produces: `mqttclient.Connect(brokerURL, username, password string) (*mqttclient.Client, error)`, method `(*Client).Disconnect()` dan `(*Client).IsConnected() bool` — dipakai `cmd/server/main.go` di Task 8

**Precondition:** Task 2 selesai, Mosquitto jalan.

- [x] **Step 1: Tambah dependency paho**

Run:
```bash
go get github.com/eclipse/paho.mqtt.golang@latest
```

- [x] **Step 2: Tulis failing test `internal/mqttclient/client_test.go`**

```go
package mqttclient

import "testing"

func TestConnect_Success(t *testing.T) {
	c, err := Connect("tcp://localhost:11883", "", "")
	if err != nil {
		t.Fatalf("Connect() error = %v (pastikan `docker compose up -d mosquitto` sedang jalan)", err)
	}
	defer c.Disconnect()

	if !c.IsConnected() {
		t.Fatal("IsConnected() = false, want true")
	}
}

func TestConnect_UnreachableBroker(t *testing.T) {
	_, err := Connect("tcp://localhost:19999", "", "")
	if err == nil {
		t.Fatal("Connect() expected error for unreachable broker, got nil")
	}
}
```

- [x] **Step 3: Jalankan test, verifikasi gagal**

Run:
```bash
go test ./internal/mqttclient/... -v
```
Expected: FAIL — `mqttclient.Connect` undefined.

- [x] **Step 4: Tulis `internal/mqttclient/client.go`**

```go
package mqttclient

import (
	"fmt"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
)

type Client struct {
	client mqtt.Client
}

func Connect(brokerURL, username, password string) (*Client, error) {
	opts := mqtt.NewClientOptions().
		AddBroker(brokerURL).
		SetConnectTimeout(10 * time.Second).
		SetAutoReconnect(true)

	if username != "" {
		opts.SetUsername(username)
	}
	if password != "" {
		opts.SetPassword(password)
	}

	client := mqtt.NewClient(opts)
	token := client.Connect()
	if !token.WaitTimeout(10 * time.Second) {
		return nil, fmt.Errorf("mqtt connect timeout after 10s")
	}
	if err := token.Error(); err != nil {
		return nil, fmt.Errorf("mqtt connect failed: %w", err)
	}

	return &Client{client: client}, nil
}

func (c *Client) Disconnect() {
	c.client.Disconnect(250)
}

func (c *Client) IsConnected() bool {
	return c.client.IsConnected()
}
```

- [x] **Step 5: Jalankan test, verifikasi lulus**

Run:
```bash
go test ./internal/mqttclient/... -v
```
Expected: `PASS` untuk `TestConnect_Success` dan `TestConnect_UnreachableBroker`.

> Catatan: `TestConnect_UnreachableBroker` butuh waktu sampai ~10 detik (connect timeout) — ini normal, bukan test yang hang.

---

### Task 7: HTTP Healthz Handler

**Files:**
- Create: `internal/httpserver/healthz.go`
- Test: `internal/httpserver/healthz_test.go`

**Interfaces:**
- Consumes: tidak ada (pure function + interface, testable tanpa dependency eksternal)
- Produces: interface `httpserver.Pinger` (`Ping(ctx context.Context) error`), fungsi `httpserver.HealthzHandler(pinger Pinger) echo.HandlerFunc` — dipakai `cmd/server/main.go` di Task 8 dengan `*pgxpool.Pool` dari Task 5 sebagai `Pinger` (pgxpool.Pool sudah punya method `Ping(ctx) error` yang cocok secara struktural)

- [x] **Step 1: Tambah dependency Echo**

Run:
```bash
go get github.com/labstack/echo/v4@latest
```

- [x] **Step 2: Tulis failing test `internal/httpserver/healthz_test.go`**

```go
package httpserver

import (
	"context"
	"errors"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/labstack/echo/v4"
)

type fakePinger struct {
	err error
}

func (f fakePinger) Ping(ctx context.Context) error {
	return f.err
}

func TestHealthzHandler_Healthy(t *testing.T) {
	e := echo.New()
	req := httptest.NewRequest(http.MethodGet, "/healthz", nil)
	rec := httptest.NewRecorder()
	c := e.NewContext(req, rec)

	handler := HealthzHandler(fakePinger{err: nil})
	if err := handler(c); err != nil {
		t.Fatalf("handler returned error: %v", err)
	}
	if rec.Code != http.StatusOK {
		t.Errorf("status = %d, want %d", rec.Code, http.StatusOK)
	}
}

func TestHealthzHandler_Unhealthy(t *testing.T) {
	e := echo.New()
	req := httptest.NewRequest(http.MethodGet, "/healthz", nil)
	rec := httptest.NewRecorder()
	c := e.NewContext(req, rec)

	handler := HealthzHandler(fakePinger{err: errors.New("db down")})
	if err := handler(c); err != nil {
		t.Fatalf("handler returned error: %v", err)
	}
	if rec.Code != http.StatusServiceUnavailable {
		t.Errorf("status = %d, want %d", rec.Code, http.StatusServiceUnavailable)
	}
}
```

- [x] **Step 3: Jalankan test, verifikasi gagal**

Run:
```bash
go test ./internal/httpserver/... -v
```
Expected: FAIL — `httpserver.HealthzHandler` undefined.

- [x] **Step 4: Tulis `internal/httpserver/healthz.go`**

```go
package httpserver

import (
	"context"
	"net/http"

	"github.com/labstack/echo/v4"
)

type Pinger interface {
	Ping(ctx context.Context) error
}

func HealthzHandler(pinger Pinger) echo.HandlerFunc {
	return func(c echo.Context) error {
		if err := pinger.Ping(c.Request().Context()); err != nil {
			return c.JSON(http.StatusServiceUnavailable, map[string]string{
				"status": "unavailable",
				"error":  err.Error(),
			})
		}
		return c.JSON(http.StatusOK, map[string]string{"status": "ok"})
	}
}
```

- [x] **Step 5: Jalankan test, verifikasi lulus**

Run:
```bash
go test ./internal/httpserver/... -v
```
Expected: `PASS` untuk `TestHealthzHandler_Healthy` dan `TestHealthzHandler_Unhealthy`.

---

### Task 8: main.go Wiring + End-to-End Verification

**Files:**
- Create: `cmd/server/main.go`

**Interfaces:**
- Consumes: `config.Load()` (Task 4), `database.NewPool()` (Task 5), `mqttclient.Connect()` (Task 6), `httpserver.HealthzHandler()` (Task 7)
- Produces: binary `cmd/server` yang bisa dijalankan end-to-end — deliverable akhir Fase 1

**Precondition:** Task 1-7 selesai, Postgres & Mosquitto (Task 2) masih jalan dengan skema ter-migrasi (Task 3).

- [x] **Step 1: Tulis `cmd/server/main.go`**

```go
package main

import (
	"context"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/labstack/echo/v4"

	"service-payment-bridge/internal/config"
	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/httpserver"
	"service-payment-bridge/internal/mqttclient"
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

	mqttClient, err := mqttclient.Connect(cfg.MQTTBrokerURL, cfg.MQTTUsername, cfg.MQTTPassword)
	if err != nil {
		logger.Error("failed to connect to mqtt broker", "error", err)
		os.Exit(1)
	}
	defer mqttClient.Disconnect()

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
```

- [x] **Step 2: Verifikasi build**

Run:
```bash
go build -o bin/server ./cmd/server
```
Expected: sukses, binary `bin/server` dihasilkan tanpa error.

- [x] **Step 3: Jalankan seluruh unit/integration test**

Run:
```bash
go test ./... -v
```
Expected: semua test dari Task 4, 5, 6, 7 `PASS`.

- [x] **Step 4: Jalankan binary dan verifikasi `/healthz`**

Run (di terminal terpisah, biarkan berjalan):
```bash
export HTTP_PORT=8080
export DATABASE_URL="postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
export MQTT_BROKER_URL="tcp://localhost:11883"
export LOG_LEVEL=info
./bin/server
```
Expected: log `service started` muncul, proses tidak langsung exit.

Di terminal lain:
```bash
curl -i http://localhost:8080/healthz
```
Expected: `HTTP/1.1 200 OK`, body `{"status":"ok"}`.

- [x] **Step 5: Verifikasi graceful shutdown**

Di terminal tempat `./bin/server` berjalan, kirim `Ctrl+C` (SIGINT).
Expected: log `shutting down` muncul, proses keluar bersih (exit code `0`), tidak ada goroutine leak/panic di output.

- [x] **Step 6: Verifikasi unhealthy path (opsional tapi disarankan)**

Run:
```bash
docker compose stop postgres
```
Jalankan ulang `./bin/server` (akan gagal start karena `database.NewPool` fatal saat DB down — ini **expected behavior** sesuai desain: service tidak boleh start dalam keadaan setengah siap).

Expected: log `failed to connect to database`, proses exit dengan kode `1`.

Nyalakan lagi:
```bash
docker compose start postgres
```

---

## Selesai

Setelah Task 1-8 lulus semua verifikasi, Fase 1 — Fondasi selesai: project Go punya struktur final, skema database ter-migrasi, koneksi dasar ke Postgres & MQTT broker bekerja, dan HTTP server minimal jalan dengan health check yang benar-benar mengecek konektivitas DB. Business logic (signature builder, Manjo Client, MQTT consumer/publisher, transaction service, webhook handler) dimulai di Fase 2 sebagai plan terpisah.

## Execution Notes (dieksekusi 2026-09-24)

Deviasi dari plan yang terjadi saat eksekusi nyata di mesin dev ini — bukan perubahan desain, murni penyesuaian karena mesin dev sudah punya service lain yang bentrok port:

- **Postgres host port**: `5432` → `15432`. Mesin ini punya instance Postgres 18 native (bukan Docker) yang sudah listen di `127.0.0.1:5432`, sehingga publish port container gagal *diam-diam* (tidak error eksplisit — perlu dicek manual via `docker inspect`/`docker ps` untuk sadar portnya kosong). Semua `DATABASE_URL` di `.env.example` dan plan ini sudah disesuaikan ke `15432`.
- **Mosquitto host port**: `1883` → `11883`. Ada container `mosquitto` lain (tidak terkait project ini, sudah jalan 7 minggu) yang sudah pakai port itu — kali ini Docker Compose error eksplisit ("port is already allocated"). Semua `MQTT_BROKER_URL` disesuaikan ke `11883`.
- **`go.mod` `go` directive**: `1.23` → `1.25.0`. Ter-upgrade otomatis oleh `go get` saat menambah `pgx/v5` (dependency itu punya minimum version lebih tinggi dari yang di-set di plan). Toolchain lokal (1.26.5) tetap kompatibel, tidak ada isu.
- **`HTTP_PORT` verifikasi manual**: default `8080` di kode/config **tidak diubah** (itu tetap default yang wajar untuk deployment lain), tapi verifikasi manual Task 8 Step 4 dijalankan dengan `HTTP_PORT=18080` karena `8080` juga dipakai proses lain di mesin dev ini.

Semua deviasi di atas murni environment-specific mesin dev ini, bukan keputusan desain — di environment lain (CI, staging, mesin dev lain) port default `5432`/`1883`/`8080` kemungkinan besar tetap bisa dipakai tanpa masalah.

**Hasil verifikasi akhir:**
- `go build ./...`, `go vet ./...` — bersih
- `go test ./... -v` — 8 test PASS di 6 package
- `gofmt -l .` — tidak ada file yang perlu diformat ulang
- Migration `up` → `down -all` → `up` — round-trip bersih, 4 tabel + `manjo_api_operation` (dengan `QUERY_PAYMENT`) + 5 enum lain semua ter-buat
- Binary `bin/server` start, `/healthz` balas `200 {"status":"ok"}`, SIGTERM → graceful shutdown bersih, DB down → exit code `1` dengan log jelas (fail-fast by design, sesuai Prinsip Desain #1 di `architecture.md`: service tidak boleh start dalam keadaan setengah siap)
