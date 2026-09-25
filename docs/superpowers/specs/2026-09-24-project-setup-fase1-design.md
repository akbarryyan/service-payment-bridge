# Design Spec — Project Setup (Fase 1: Fondasi)

**Tanggal:** 2026-09-24
**Status:** Disetujui, siap masuk writing-plans
**Scope:** Fase 1 — Fondasi dari `PRD.md` Section 15 (Milestone / Fase Implementasi)
**Dokumen terkait:** `architecture.md` (Section 17-19), `schema.md`, `PRD.md`

---

## 1. Tujuan

Menyiapkan fondasi project Go untuk Payment Bridge Service: struktur folder, skema database (migration), koneksi dasar ke PostgreSQL dan MQTT broker, config loading, dan HTTP server minimal (health check). **Tidak** mencakup business logic apa pun (signature builder, Manjo Client, transaction service, message parser/validator, webhook handler) — itu Fase 2-4 sesuai milestone di `PRD.md`.

## 2. Keputusan Teknis (hasil brainstorming)

| Area | Pilihan | Alasan |
|---|---|---|
| Module path | `service-payment-bridge` (tanpa domain) | Repo GitLab kantor belum dibuat & belum ada nama final — tinggal `go mod edit -module <path-gitlab>` begitu tersedia |
| Go version | `go 1.23` di go.mod | Toolchain lokal 1.26.5, target minimum yang masih modern (slog, dll.) |
| Data access | `pgx` (driver) + `sqlc` (codegen query) | Kontrol penuh atas query eksplisit (dibutuhkan untuk `SELECT ... FOR UPDATE`, partial index `expire_at`), performa lebih baik dari ORM |
| HTTP framework | **Echo** (`labstack/echo/v4`) | Standar tim kantor untuk service Go lain — prioritas konsistensi lintas service di atas minimalisme |
| MQTT client | `eclipse/paho.mqtt.golang` | Sudah direkomendasikan di `architecture.md` Section 17, connect-only di fase ini (belum subscribe/consume) |
| Migration tool | `golang-migrate` | Sesuai `schema.md` Section 10 |
| Config loading | `envconfig` (`kelseyhightower/envconfig`) | Kebutuhan config Fase 1 relatif flat (env var saja), lebih ringan dari viper |
| Local dev environment | Docker Compose (Postgres 16 + Mosquitto 2) | Konsisten di semua mesin dev, satu perintah `docker compose up` |
| Logging | `log/slog` (stdlib) | Cukup untuk kebutuhan structured logging, tidak perlu dependency tambahan |
| Git init | **Tidak** di fase ini | Remote GitLab belum ada; git init menyusul begitu repo dibuat |

## 3. Struktur Folder

Dipangkas dari `architecture.md` Section 18 — hanya folder yang benar-benar dipakai Fase 1. Folder untuk business logic (`manjoclient/`, `transaction/`, `httpapi/` handler notifikasi, `merchant/`, `validation/`, `retryqueue/`) **belum dibuat** di fase ini supaya tidak ada folder kosong tanpa isi (YAGNI) — akan ditambahkan sesuai kebutuhan masing-masing di Fase 2-4.

```
service-payment-bridge/
├── cmd/
│   └── server/
│       └── main.go              # entrypoint: load config → connect DB → connect MQTT →
│                                 # start Echo server → graceful shutdown
├── internal/
│   ├── config/
│   │   └── config.go            # struct config (envconfig)
│   ├── database/
│   │   ├── db.go                # pgxpool.New, ping wrapper
│   │   ├── queries/
│   │   │   └── healthcheck.sql  # query awal untuk validasi wiring sqlc (SELECT 1)
│   │   └── sqlc/                # generated code (sqlc generate), committed ke repo
│   └── mqttclient/
│       └── client.go            # paho.NewClient + Connect wrapper (belum ada subscribe/consume)
├── migrations/                   # golang-migrate, 6 file naik/turun
├── docker-compose.yml             # postgres:16 + eclipse-mosquitto:2
├── sqlc.yaml
├── .env.example
├── .gitignore
├── go.mod
└── go.sum
```

## 4. Migration Files

Mengikuti urutan di `schema.md` Section 10 (Migration Order), DDL disalin apa adanya dari `schema.md` — tidak ada perubahan skema, hanya dipecah jadi file migration `golang-migrate` (`NNNNNN_<name>.up.sql` / `.down.sql`):

| # | File | Isi |
|---|---|---|
| 1 | `000001_extension_and_enums` | `CREATE EXTENSION pgcrypto`, 6 `CREATE TYPE` enum (termasuk `manjo_api_operation` dengan value `QUERY_PAYMENT` yang sudah ditambahkan) |
| 2 | `000002_create_merchants` | Tabel `merchants` + index `idx_merchants_status` |
| 3 | `000003_create_transactions` | Tabel `transactions` + 3 index |
| 4 | `000004_create_mqtt_messages` | Tabel `mqtt_messages` + 2 index |
| 5 | `000005_create_manjo_api_logs` | Tabel `manjo_api_logs` + 2 index |
| 6 | `000006_prevent_final_status_change_trigger` | Function + trigger `trg_prevent_final_status_change` |

`.down.sql` masing-masing melakukan `DROP` kebalikan dari `.up.sql` (urutan terbalik untuk FK dependency).

## 5. Config

`internal/config/config.go`, struct dengan tag `envconfig`, field diambil dari `architecture.md` Section 19 **minus** `SECRET_MANAGER_*` (belum relevan — baru dipakai begitu Manjo Client/kredensial per-merchant diimplementasikan di Fase 2, jadi kalau ditambahkan sekarang akan jadi field tanpa pemakai):

```go
type Config struct {
    HTTPPort       string `envconfig:"HTTP_PORT" default:"8080"`
    DatabaseURL    string `envconfig:"DATABASE_URL" required:"true"`
    MQTTBrokerURL  string `envconfig:"MQTT_BROKER_URL" required:"true"`
    MQTTUsername   string `envconfig:"MQTT_USERNAME"`
    MQTTPassword   string `envconfig:"MQTT_PASSWORD"`
    ManjoBaseURL   string `envconfig:"MANJO_BASE_URL"`
    WebhookPublicURL string `envconfig:"WEBHOOK_PUBLIC_URL"`
    LogLevel       string `envconfig:"LOG_LEVEL" default:"info"`
}
```

`HTTP_PORT` ditambahkan di luar daftar `architecture.md` karena Echo server butuh port bind eksplisit — dokumen aslinya belum menyebutkan ini.

## 6. Docker Compose

Dua service:
- `postgres` — image `postgres:16`, port host `15432` → container `5432` (digeser dari default karena port `5432` di mesin dev sudah dipakai instance Postgres native yang tidak terkait project ini), volume named untuk persist data lokal, env `POSTGRES_DB/USER/PASSWORD` untuk dev.
- `mosquitto` — image `eclipse-mosquitto:2`, port host `11883` → container `1883` (digeser dari default karena port `1883` di mesin dev sudah dipakai container lain yang tidak terkait project ini), config minimal (allow anonymous untuk dev lokal saja — **bukan** untuk production, `architecture.md` Section 14 mewajibkan TLS + auth per-device di production).

## 7. main.go — Alur Startup

```text
1. Load config (envconfig) — exit fatal kalau required field kosong
2. Connect pgx pool ke DATABASE_URL, ping — exit fatal kalau gagal
3. Connect MQTT client (paho) ke MQTT_BROKER_URL — exit fatal kalau gagal connect
   (belum subscribe topic apa pun — itu logic MQTT Consumer di Fase 3)
4. Start Echo server di HTTP_PORT, satu route: GET /healthz → cek DB ping, balas 200/503
5. Listen SIGINT/SIGTERM → graceful shutdown (Echo Shutdown + close DB pool + disconnect MQTT)
```

## 8. Non-Goals (Eksplisit, agar Batas Fase Ini Jelas)

Tidak termasuk di deliverable ini — akan masuk Fase 2-4 sesuai `PRD.md` Section 15:
- Signature Builder (SHA256withRSA, HMAC-SHA512)
- Manjo Client (Access Token, Generate QR, Query Payment)
- MQTT Consumer/Publisher (subscribe topic, parse, validate)
- Transaction Service & state machine
- Notification HTTP Endpoint (`/webhooks/manjo/qr-mpm-notify`)
- Merchant Resolver, Message Validator
- Retry queue

## 9. Testing

Tidak ada unit test bermakna di fase ini karena belum ada business logic untuk diuji (selaras `qa.md` — semua kategori TC dimulai dari Signature Builder dkk yang baru ada di Fase 2+). Verifikasi keberhasilan Fase 1 cukup:
- `go build ./...` sukses tanpa error.
- `docker compose up -d` berhasil menjalankan Postgres + Mosquitto.
- Migration berhasil dijalankan (`migrate up`) ke test DB.
- Binary bisa start, `/healthz` membalas `200 OK` saat DB & MQTT broker up.

## 10. Referensi

- `architecture.md` Section 17 (Tech Stack), 18 (Struktur Proyek), 19 (Env Vars)
- `schema.md` Section 3-10 (DDL & Migration Order)
- `PRD.md` Section 15 (Milestone Fase 1)
