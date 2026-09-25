# Multi-Tenant / Multi-Device Topic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Memperluas skema database dan seluruh dokumen desain agar Payment Bridge Service mendukung sub-merchant/tenant (opsional) dan multi-device per tenant, dengan topic MQTT hierarkis per-device supaya notifikasi pembayaran tidak pernah "bocor" ke device lain.

**Architecture:** Dua tabel baru (`tenants`, `devices`) ditambahkan lewat migration baru (bukan edit migration lama), `merchants` kehilangan 3 kolom yang sekarang jadi properti device, `transactions` mendapat FK `device_id` sebagai penentu routing MQTT publish. Tidak ada kode Go yang berubah — Fase 1 belum punya business logic (Merchant Resolver, Request Builder, dll. belum diimplementasikan), jadi scope kerja murni migration + sinkronisasi enam dokumen desain (`schema.md`, `architecture.md`, `brainstorm-q161-updated.md`, `process-flow.md`, `PRD.md`) yang sudah lebih dulu ada.

**Tech Stack:** PostgreSQL 16 (via `docker compose` yang sudah jalan di Fase 1), `golang-migrate` CLI (sudah terpasang dari Fase 1).

## Global Constraints

- Topic MQTT: `topic/{merchant_id}/{tenant_slot}/{device_id}` — **selalu 4 segmen** (`tenant_slot` = `tenant_id` asli atau literal sentinel `_` kalau device tanpa tenant)
- Kredensial Manjo tetap **shared di level merchant** — tidak ada kredensial per-tenant
- `tenants` bersifat **opsional** — `devices.tenant_id` nullable
- Migration lama (`000001`-`000006`) **tidak boleh diedit** — perubahan lewat migration baru (`000007` dst.)
- Database dev (`docker compose` dari Fase 1, port `15432`) belum punya data apa pun — aman untuk `ADD COLUMN ... NOT NULL` tanpa default
- Payment Bridge Service **hanya membaca** `tenants`/`devices` — tidak ada CRUD/API untuk mengelolanya di scope ini

---

## File Structure

```
migrations/
├── 000007_alter_merchants_drop_device_columns.up.sql   (baru)
├── 000007_alter_merchants_drop_device_columns.down.sql (baru)
├── 000008_create_tenants.up.sql                        (baru)
├── 000008_create_tenants.down.sql                      (baru)
├── 000009_create_devices.up.sql                        (baru)
├── 000009_create_devices.down.sql                      (baru)
├── 000010_alter_transactions_add_device_id.up.sql      (baru)
└── 000010_alter_transactions_add_device_id.down.sql    (baru)

docs/
├── schema.md                    (modify — insert 2 section baru, renumber, update DDL)
├── architecture.md              (modify — topic scheme, resolver, data model)
├── brainstorm-q161-updated.md   (modify — append update block, pola existing)
├── process-flow.md              (modify — update step resolver & routing)
└── prd.md                       (modify — tambah FR baru)
```

---

### Task 1: Migration — `tenants`, `devices`, Perubahan `merchants`/`transactions`

**Files:**
- Create: `migrations/000007_alter_merchants_drop_device_columns.up.sql`
- Create: `migrations/000007_alter_merchants_drop_device_columns.down.sql`
- Create: `migrations/000008_create_tenants.up.sql`
- Create: `migrations/000008_create_tenants.down.sql`
- Create: `migrations/000009_create_devices.up.sql`
- Create: `migrations/000009_create_devices.down.sql`
- Create: `migrations/000010_alter_transactions_add_device_id.up.sql`
- Create: `migrations/000010_alter_transactions_add_device_id.down.sql`

**Interfaces:**
- Consumes: skema Fase 1 (`merchants`, `transactions` sudah ada, migration `000001`-`000006` sudah applied)
- Produces: skema final — `merchants` tanpa `mqtt_topic`/`manjo_store_id`/`manjo_terminal_id`; tabel `tenants` (`tenant_id` unique, `merchant_id` FK, `manjo_sub_merchant_id` nullable, `status` pakai enum `merchant_status` yang sudah ada); tabel `devices` (`device_id` unique, `merchant_id` FK NOT NULL, `tenant_id` FK nullable, `mqtt_topic` unique, `manjo_store_id`/`manjo_terminal_id` nullable); `transactions.device_id` FK NOT NULL ke `devices` — dipakai sebagai target routing MQTT publish di Fase 2+

- [x] **Step 1: Tulis `000007_alter_merchants_drop_device_columns.up.sql`**

```sql
ALTER TABLE merchants
    DROP COLUMN mqtt_topic,
    DROP COLUMN manjo_store_id,
    DROP COLUMN manjo_terminal_id;
```

- [x] **Step 2: Tulis `000007_alter_merchants_drop_device_columns.down.sql`**

```sql
ALTER TABLE merchants
    ADD COLUMN mqtt_topic VARCHAR(64) UNIQUE,
    ADD COLUMN manjo_store_id VARCHAR(64),
    ADD COLUMN manjo_terminal_id VARCHAR(16);
```

- [x] **Step 3: Tulis `000008_create_tenants.up.sql`**

```sql
CREATE TABLE tenants (
    id                     UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    tenant_id              VARCHAR(64)  NOT NULL UNIQUE,
    merchant_id            VARCHAR(36)  NOT NULL REFERENCES merchants (merchant_id),
    manjo_sub_merchant_id  VARCHAR(64),
    name                   VARCHAR(100),
    status                 merchant_status NOT NULL DEFAULT 'ACTIVE',

    created_at             TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at             TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_tenants_merchant_id ON tenants (merchant_id);
CREATE INDEX idx_tenants_status ON tenants (status);
```

- [x] **Step 4: Tulis `000008_create_tenants.down.sql`**

```sql
DROP TABLE IF EXISTS tenants;
```

- [x] **Step 5: Tulis `000009_create_devices.up.sql`**

```sql
CREATE TABLE devices (
    id                   UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id            VARCHAR(64)  NOT NULL UNIQUE,
    merchant_id          VARCHAR(36)  NOT NULL REFERENCES merchants (merchant_id),
    tenant_id            VARCHAR(64)  REFERENCES tenants (tenant_id),
    mqtt_topic           VARCHAR(255) NOT NULL UNIQUE,
    manjo_store_id       VARCHAR(64),
    manjo_terminal_id    VARCHAR(16),
    status               merchant_status NOT NULL DEFAULT 'ACTIVE',

    created_at           TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at           TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_devices_merchant_id ON devices (merchant_id);
CREATE INDEX idx_devices_tenant_id ON devices (tenant_id);
CREATE INDEX idx_devices_status ON devices (status);
```

- [x] **Step 6: Tulis `000009_create_devices.down.sql`**

```sql
DROP TABLE IF EXISTS devices;
```

- [x] **Step 7: Tulis `000010_alter_transactions_add_device_id.up.sql`**

```sql
ALTER TABLE transactions
    ADD COLUMN device_id VARCHAR(64) NOT NULL REFERENCES devices (device_id);

CREATE INDEX idx_transactions_device_id ON transactions (device_id);
```

- [x] **Step 8: Tulis `000010_alter_transactions_add_device_id.down.sql`**

```sql
DROP INDEX IF EXISTS idx_transactions_device_id;
ALTER TABLE transactions DROP COLUMN device_id;
```

- [x] **Step 9: Jalankan migration up**

Precondition: `docker compose up -d` (dari Fase 1) sedang jalan, migration `000001`-`000006` sudah applied.

Run:
```bash
export DATABASE_URL="postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
migrate -path migrations -database "$DATABASE_URL" up
```
Expected: output menunjukkan 4 migration baru berhasil (`7/u ... 8/u ... 9/u ... 10/u ...`), tanpa error.

- [x] **Step 10: Verifikasi struktur tabel**

Run:
```bash
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c "\d merchants"
```
Expected: kolom `mqtt_topic`, `manjo_store_id`, `manjo_terminal_id` **tidak ada lagi**.

Run:
```bash
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c "\d tenants" -c "\d devices" -c "\d transactions"
```
Expected: `tenants` dan `devices` ada dengan kolom sesuai Step 3/5; `transactions` punya kolom `device_id` dengan FK ke `devices`.

- [x] **Step 11: Verifikasi round-trip down/up**

Run:
```bash
migrate -path migrations -database "$DATABASE_URL" down 4
```
Expected: 4 migration (`10/d`, `9/d`, `8/d`, `7/d`) di-rollback tanpa error — `merchants` kembali punya 3 kolom lama, `tenants`/`devices` hilang, `transactions.device_id` hilang.

Run:
```bash
migrate -path migrations -database "$DATABASE_URL" up
```
Expected: 4 migration berhasil lagi — DB kembali ke state final (Step 10) untuk task selanjutnya.

---

### Task 2: `schema.md` — Tabel Baru, Renumber, Update DDL

**Files:**
- Modify: `docs/schema.md`

**Interfaces:**
- Consumes: hasil Task 1 (DDL final)
- Produces: `schema.md` jadi sumber kebenaran skema yang cocok 1:1 dengan migration final — dipakai acuan Fase 2+ saat implementasi Merchant/Device Resolver

- [x] **Step 1: Update Daftar Isi**

Ganti:
```markdown
4. [Tabel `merchants`](#4-tabel-merchants)
5. [Tabel `transactions`](#5-tabel-transactions)
6. [Tabel `mqtt_messages`](#6-tabel-mqtt_messages)
7. [Tabel `manjo_api_logs`](#7-tabel-manjo_api_logs)
8. [Ringkasan Relasi & Index](#8-ringkasan-relasi--index)
9. [Query Umum (Reference)](#9-query-umum-reference)
10. [Migration Order](#10-migration-order)
11. [Retention & Archival](#11-retention--archival)
12. [Catatan Terbuka](#12-catatan-terbuka)
```

Menjadi:
```markdown
4. [Tabel `merchants`](#4-tabel-merchants)
5. [Tabel `tenants`](#5-tabel-tenants)
6. [Tabel `devices`](#6-tabel-devices)
7. [Tabel `transactions`](#7-tabel-transactions)
8. [Tabel `mqtt_messages`](#8-tabel-mqtt_messages)
9. [Tabel `manjo_api_logs`](#9-tabel-manjo_api_logs)
10. [Ringkasan Relasi & Index](#10-ringkasan-relasi--index)
11. [Query Umum (Reference)](#11-query-umum-reference)
12. [Migration Order](#12-migration-order)
13. [Retention & Archival](#13-retention--archival)
14. [Catatan Terbuka](#14-catatan-terbuka)
```

- [x] **Step 2: Update ERD (Section 1)**

Ganti seluruh blok ERD (dari ` ```text` sampai ` ``` ` penutup di Section 1) dengan:

```text
┌──────────────────────┐
│      merchants        │
├──────────────────────┤
│ id (PK)               │
│ merchant_id  (UNIQUE) │◄──────┬──────────────┐
│ manjo_client_id        │       │              │
│ manjo_private_key_ref  │       │              │
│ manjo_client_secret_ref│       │              │
│ manjo_merchant_id      │       │              │
│ manjo_channel_id       │       │              │
│ status                 │       │              │
│ created_at / updated_at│       │              │
└──────────────────────┘       │              │
                                 │ FK            │ FK
                                 │ (merchant_id) │ (merchant_id)
                    ┌────────────┴──────┐       │
                    │      tenants       │       │
                    ├───────────────────┤       │
                    │ id (PK)            │       │
                    │ tenant_id (UNIQUE) │◄──┐   │
                    │ merchant_id (FK)   │   │   │
                    │ manjo_sub_merchant_id│ │   │
                    │ name               │   │   │
                    │ status             │   │   │
                    │ created_at/updated_at│ │   │
                    └───────────────────┘   │   │
                                             │FK  │
                                             │(tenant_id, nullable)
                                        ┌────┴───┴──────────┐
                                        │      devices        │
                                        ├────────────────────┤
                                        │ id (PK)              │
                                        │ device_id (UNIQUE)   │◄──────────────┐
                                        │ merchant_id (FK)      │              │
                                        │ tenant_id (FK, null)  │              │
                                        │ mqtt_topic            │              │
                                        │ manjo_store_id        │              │
                                        │ manjo_terminal_id     │              │
                                        │ status                │              │
                                        │ created_at/updated_at │              │
                                        └────────────────────┘              │
                                                                              │ FK (device_id)
                                                                              │
┌──────────────────────────────┐                                            │
│         transactions          │                                            │
├──────────────────────────────┤                                            │
│ id (PK)                       │                                            │
│ transaction_id  (UNIQUE)       │◄───┐                                      │
│ reference_no                   │    │                                      │
│ merchant_id                    │    │                                      │
│ device_id  (FK) ───────────────┼────┼──────────────────────────────────────┘
│ amount                         │    │
│ status                         │    │
│ manjo_status_code              │    │
│ qris_payload                   │    │
│ external_id                    │    │
│ expire_at                      │    │
│ created_at / paid_at / updated_at │ │
└──────────────────────────────┘    │
        ▲                    ▲       │
        │ FK (transaction_id)│ FK (transaction_id, nullable)
        │                    │       │
┌───────┴────────┐  ┌────────┴──────────┐
│  mqtt_messages  │  │  manjo_api_logs    │
├────────────────┤  ├───────────────────┤
│ id (PK)         │  │ id (PK)            │
│ topic           │  │ direction          │
│ payload         │  │ operation          │
│ direction       │  │ endpoint           │
│ status          │  │ http_status        │
│ transaction_id  │  │ request_body       │
│  (FK, nullable) │  │ response_body      │
│ created_at      │  │ transaction_id     │
│ processed_at    │  │  (FK, nullable)    │
│ error_message   │  │ duration_ms        │
└────────────────┘  │ created_at         │
                     └───────────────────┘
```

**Ringkasan relasi:**
- `merchants` 1 — N `tenants` (opsional — merchant boleh tidak punya tenant sama sekali).
- `merchants` 1 — N `devices` (selalu ada — device selalu terhubung ke satu merchant, baik langsung atau lewat tenant).
- `tenants` 1 — N `devices` (opsional — `devices.tenant_id` nullable).
- `devices` 1 — N `transactions` (satu device bisa punya banyak transaksi — ini yang menentukan topic MQTT tujuan publish).
- `transactions` 1 — N `mqtt_messages` (satu transaksi bisa punya beberapa pesan MQTT: request, hasil QR, notifikasi).
- `transactions` 1 — N `manjo_api_logs` (satu transaksi bisa punya beberapa panggilan API: get token tidak terkait transaksi tertentu, tapi generate-QR dan notify terkait).

- [x] **Step 3: Update Section 4 `merchants`**

Di blok `CREATE TABLE merchants (...)` pada Section 4, hapus 3 baris berikut dari kode SQL:

```sql
    mqtt_topic                VARCHAR(64)  NOT NULL UNIQUE,   -- contoh: topic_MT82419344
```

dan

```sql
    manjo_store_id            VARCHAR(64),                    -- opsional
    manjo_terminal_id         VARCHAR(16),                    -- opsional
```

Di tabel penjelasan kolom bawahnya, hapus baris `merchant_id | ID yang dipakai di topic MQTT...` dan ganti dengan:

```markdown
| `merchant_id` | ID identitas merchant di sistem kita, dipakai untuk FK dari `tenants`/`devices` dan sebagai `merchantId`/`X-PARTNER-ID` di request Manjo |
```

Tambahkan catatan baru di akhir Section 4 (sebelum `---`):

```markdown
> **Catatan (multi-tenant):** `mqtt_topic`, `manjo_store_id`, `manjo_terminal_id` **tidak lagi ada di `merchants`** — sekarang jadi properti tabel `devices` ([Section 6](#6-tabel-devices)), karena satu merchant bisa punya banyak device dengan store/terminal berbeda-beda. Lihat [Section 5](#5-tabel-tenants) dan [Section 6](#6-tabel-devices) untuk model sub-merchant/tenant dan device.
```

- [x] **Step 4: Sisipkan Section 5 `tenants` (baru) setelah Section 4**

Sisipkan setelah akhir Section 4 (setelah `---` penutup Section 4 lama, sebelum yang dulunya Section 5 `transactions`):

```markdown
## 5. Tabel `tenants`

Merepresentasikan sub-merchant/tenant di bawah satu `merchants` — **opsional**, tidak semua merchant punya tenant. Tenant di-input oleh merchant sendiri lewat sistem lain (di luar scope Payment Bridge Service, yang hanya membaca tabel ini).

\`\`\`sql
CREATE TABLE tenants (
    id                     UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    tenant_id              VARCHAR(64)  NOT NULL UNIQUE,   -- dipakai di segmen topic MQTT
    merchant_id            VARCHAR(36)  NOT NULL REFERENCES merchants (merchant_id),
    manjo_sub_merchant_id  VARCHAR(64),                    -- dikirim sebagai subMerchantId ke Manjo, opsional
    name                   VARCHAR(100),                   -- label bisnis, mis. "Toko A"

    status                 merchant_status NOT NULL DEFAULT 'ACTIVE',

    created_at             TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at             TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_tenants_merchant_id ON tenants (merchant_id);
CREATE INDEX idx_tenants_status ON tenants (status);
\`\`\`

| Kolom | Keterangan |
|---|---|
| `tenant_id` | Identitas tenant, dipakai sebagai segmen `tenant_slot` di topic MQTT (`topic/{merchant_id}/{tenant_id}/{device_id}`) |
| `manjo_sub_merchant_id` | Dikirim sebagai `subMerchantId` saat Generate QR (Manjo, field optional) — **tidak perlu pre-registrasi ke Manjo**, bebas ditentukan Service/merchant |
| `status` | Reuse enum `merchant_status` (`ACTIVE`/`INACTIVE`) — semantiknya sama, tidak perlu enum baru |

---

## 6. Tabel `devices`

Satu row = satu physical Q161 Pro unit. Device **selalu** terhubung ke satu `merchants` (langsung, atau lewat `tenants` kalau `tenant_id` terisi).

\`\`\`sql
CREATE TABLE devices (
    id                   UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id            VARCHAR(64)  NOT NULL UNIQUE,      -- ditetapkan saat provisioning device
    merchant_id          VARCHAR(36)  NOT NULL REFERENCES merchants (merchant_id),
    tenant_id            VARCHAR(64)  REFERENCES tenants (tenant_id),  -- nullable: device boleh langsung ke merchant
    mqtt_topic            VARCHAR(255) NOT NULL UNIQUE,      -- topic/{merchant_id}/{tenant_id atau "_"}/{device_id}
    manjo_store_id        VARCHAR(64),                       -- dikirim sebagai storeId, opsional
    manjo_terminal_id     VARCHAR(16),                       -- dikirim sebagai terminalId, opsional

    status                merchant_status NOT NULL DEFAULT 'ACTIVE',

    created_at            TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at            TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_devices_merchant_id ON devices (merchant_id);
CREATE INDEX idx_devices_tenant_id ON devices (tenant_id);
CREATE INDEX idx_devices_status ON devices (status);
\`\`\`

| Kolom | Keterangan |
|---|---|
| `device_id` | **Kunci lookup utama Device Resolver** — unik secara global (bukan cuma unik per merchant), diparse dari segmen terakhir topic MQTT |
| `tenant_id` | Nullable — null berarti device nempel langsung ke merchant tanpa tenant. Kalau terisi, topic pakai `tenant_id` asli; kalau null, topic pakai sentinel `_` di posisi `tenant_slot` |
| `mqtt_topic` | Topic lengkap 4 segmen, disimpan untuk kemudahan query (`topic/{merchant_id}/{tenant_slot}/{device_id}`) |
| `manjo_store_id` / `manjo_terminal_id` | Properti per-device (dipindah dari `merchants` — lihat [Section 4](#4-tabel-merchants)) |

---
```

- [x] **Step 5: Renumber Section `transactions` (5→7) dan update DDL-nya**

Ganti heading:
```markdown
## 5. Tabel `transactions`
```
menjadi:
```markdown
## 7. Tabel `transactions`
```

Di blok `CREATE TABLE transactions (...)`, setelah baris `merchant_id VARCHAR(36) NOT NULL REFERENCES merchants (merchant_id),`, tambahkan baris baru:

```sql
    device_id           VARCHAR(64)  NOT NULL REFERENCES devices (device_id),
```

Setelah baris index `CREATE INDEX idx_transactions_expire_at ...`, tambahkan:

```sql
CREATE INDEX idx_transactions_device_id ON transactions (device_id);
```

Di tabel penjelasan kolom, tambahkan baris baru setelah baris `merchant_id`:

```markdown
| `device_id` | **Menentukan topic MQTT tujuan** saat publish hasil generate-QR atau notifikasi pembayaran — lookup `devices.mqtt_topic` dari kolom ini, **bukan** dari echo Manjo (Manjo tidak konsisten mengembalikan info tenant/device di notifikasi) |
```

- [x] **Step 6: Renumber sisa section**

Ganti heading berikut satu per satu (sebelumnya → sesudahnya):
- `## 6. Tabel \`mqtt_messages\`` → `## 8. Tabel \`mqtt_messages\``
- `## 7. Tabel \`manjo_api_logs\`` → `## 9. Tabel \`manjo_api_logs\``
- `## 8. Ringkasan Relasi & Index` → `## 10. Ringkasan Relasi & Index`
- `## 9. Query Umum (Reference)` → `## 11. Query Umum (Reference)`
- `## 10. Migration Order` → `## 12. Migration Order`
- `## 11. Retention & Archival` → `## 13. Retention & Archival`
- `## 12. Catatan Terbuka` → `## 14. Catatan Terbuka`

- [x] **Step 7: Update Section 10 (dulu Section 8) Ringkasan Relasi & Index**

Ganti tabel di section ini menjadi:

```markdown
| Tabel | Foreign Key | Referensi | Index Utama |
|---|---|---|---|
| `tenants` | `merchant_id` | `merchants.merchant_id` | `merchant_id`, `status` |
| `devices` | `merchant_id`, `tenant_id` (nullable) | `merchants.merchant_id`, `tenants.tenant_id` | `merchant_id`, `tenant_id`, `status` |
| `transactions` | `merchant_id`, `device_id` | `merchants.merchant_id`, `devices.device_id` | `(merchant_id, status)`, `device_id`, `reference_no`, partial index `expire_at` |
| `mqtt_messages` | `transaction_id` (nullable) | `transactions.transaction_id` | `transaction_id`, `(topic, created_at)` |
| `manjo_api_logs` | `transaction_id` (nullable) | `transactions.transaction_id` | `transaction_id`, `(operation, created_at)` |
```

- [x] **Step 8: Update Section 12 (dulu Section 10) Migration Order**

Ganti blok urutan migration menjadi:

```text
1. CREATE EXTENSION pgcrypto
2. CREATE TYPE ... (semua enum)
3. CREATE TABLE merchants
4. CREATE TABLE transactions       (FK ke merchants)
5. CREATE TABLE mqtt_messages      (FK ke transactions)
6. CREATE TABLE manjo_api_logs     (FK ke transactions)
7. CREATE TRIGGER trg_prevent_final_status_change
8. ALTER TABLE merchants           (drop mqtt_topic, manjo_store_id, manjo_terminal_id)
9. CREATE TABLE tenants            (FK ke merchants)
10. CREATE TABLE devices           (FK ke merchants, tenants)
11. ALTER TABLE transactions       (add device_id, FK ke devices)
```

- [x] **Step 9: Update Section 14 (dulu Section 12) Catatan Terbuka**

Ganti item #1 (yang sebelumnya open question soal kredensial per-merchant vs per-partner) menjadi catatan resolved:

```markdown
1. ~~**Kredensial per-merchant vs per-partner**~~ — **Sudah diputuskan**: kredensial Manjo shared di level `merchants`, tenant/device di bawahnya tidak punya kredensial sendiri. Model multi-tenant lengkap ada di [Section 5](#5-tabel-tenants) dan [Section 6](#6-tabel-devices).
```

Tambahkan item baru di akhir daftar:

```markdown
5. **Provisioning device_id** — bagaimana `device_id` pertama kali ditetapkan ke unit Q161 fisik (manual input, QR provisioning, dsb.) belum ditentukan di dokumen ini — di luar scope teknis Service (Service hanya membaca tabel `devices` yang sudah terisi).
```

- [x] **Step 10: Verifikasi**

Run:
```bash
grep -n "^## [0-9]" /home/akbar/Kerjaan/repository/service-payment-bridge/docs/schema.md
```
Expected: heading `## 1.` sampai `## 14.` berurutan tanpa duplikat/lompat nomor.

---

### Task 3: `architecture.md` — Topic Scheme, Resolver, Data Model

**Files:**
- Modify: `docs/architecture.md`

**Interfaces:**
- Consumes: hasil Task 1 & 2 (skema final)
- Produces: `architecture.md` Section 4.4, 7.1-7.3, 9 konsisten dengan skema baru — jadi acuan Fase 2+ untuk implementasi Device Resolver

- [x] **Step 1: Update System Context diagram (Section 2)**

Ganti baris:
```
│  (device)  │   topic_{merchant}  │              │   topic_{merchant}  │  Service              │
```
menjadi:
```
│  (device)  │  topic/mid/tnt/did  │              │  topic/mid/tnt/did  │  Service              │
```

- [x] **Step 2: Update Section 4.4 Merchant Resolver**

Ganti seluruh Section 4.4:

```markdown
### 4.4 Device Resolver

- Mapping topic MQTT (`topic/{merchant_id}/{tenant_slot}/{device_id}`) → konfigurasi lengkap: kredensial Manjo (dari `merchants`, lewat `device.merchant_id`), `manjo_sub_merchant_id` (dari `tenants`, kalau `device.tenant_id` tidak null), `manjo_store_id`/`manjo_terminal_id` (dari `devices`).
- **Kunci lookup utama adalah `device_id`** (unik global) — bukan kombinasi merchant_id+tenant_id+device_id. Segmen `merchant_id`/`tenant_slot` di topic terutama untuk keperluan debugging/filtering manual, bukan bagian dari logic lookup.
- Sumber data: tabel `merchants`, `tenants`, `devices` ([Section 9](#9-data-model)), di-cache in-memory dengan key `device_id`, TTL pendek atau invalidasi manual saat config berubah.
- Kalau `device_id` tidak ditemukan di `devices` → reject, jangan diteruskan ke Manjo Client (device belum ter-provisioning).
```

- [x] **Step 3: Update Section 7.1-7.3 (topic references)**

Ganti ketiga baris berikut (muncul di Section 7.1, 7.2, 7.3):
```
**Topic:** `topic_{merchant_id}`
```
menjadi (di ketiga tempat):
```
**Topic:** `topic/{merchant_id}/{tenant_slot}/{device_id}` — `tenant_slot` = `tenant_id` asli, atau literal `_` kalau device tanpa tenant
```

- [x] **Step 4: Update Section 4.7 MQTT Publisher & System Context**

Ganti baris:
```
- Serialize ke format final ([Section 7](#7-kontrak-internal-q161--service)) dan publish ke `topic_{merchant_id}`.
```
menjadi:
```
- Serialize ke format final ([Section 7](#7-kontrak-internal-q161--service)) dan publish ke `devices.mqtt_topic` milik `transactions.device_id` terkait (**bukan** hasil parsing ulang dari topic request masuk — selalu lookup DB, supaya konsisten walau ada perubahan config device di tengah siklus transaksi).
```

- [x] **Step 5: Update Section 9.1 `merchants` dan tambahkan 9.3/9.4**

Di Section 9.1, hapus baris:
```markdown
| `mqtt_topic` | string | `topic_{merchant_id}` (derivable, tapi disimpan untuk kemudahan query) |
```
dan:
```markdown
| `manjo_store_id` | string, nullable | opsional |
| `manjo_terminal_id` | string, nullable | opsional |
```

Setelah Section 9.2 (`transactions`), sisipkan sebelum Section 10:

```markdown
### 9.3 `tenants`

| Kolom | Tipe | Keterangan |
|---|---|---|
| `id` | UUID/PK | |
| `tenant_id` | string, unique | dipakai di segmen `tenant_slot` topic MQTT |
| `merchant_id` | FK → `merchants.merchant_id` | |
| `manjo_sub_merchant_id` | string, nullable | dikirim sebagai `subMerchantId` (Generate QR, field optional Manjo) |
| `name` | string | label bisnis |
| `status` | enum(`ACTIVE`,`INACTIVE`) | |
| `created_at` / `updated_at` | timestamp | |

### 9.4 `devices`

| Kolom | Tipe | Keterangan |
|---|---|---|
| `id` | UUID/PK | |
| `device_id` | string, unique | **kunci lookup utama Device Resolver**, unik global |
| `merchant_id` | FK → `merchants.merchant_id`, NOT NULL | selalu ada |
| `tenant_id` | FK → `tenants.tenant_id`, nullable | null = device langsung ke merchant |
| `mqtt_topic` | string, unique | `topic/{merchant_id}/{tenant_slot}/{device_id}` |
| `manjo_store_id` / `manjo_terminal_id` | string, nullable | dipindah dari `merchants` — sekarang properti device |
| `status` | enum(`ACTIVE`,`INACTIVE`) | |
| `created_at` / `updated_at` | timestamp | |
```

Update juga baris index di bawah Section 9.2:
```markdown
Index yang direkomendasikan: `UNIQUE(transaction_id)`, `INDEX(merchant_id, status)`, `INDEX(device_id)`, `INDEX(reference_no)`.
```

Tambahkan kolom `device_id` ke tabel Section 9.2 (`transactions`), setelah baris `merchant_id`:
```markdown
| `device_id` | FK → `devices.device_id`, NOT NULL | menentukan topic MQTT tujuan publish — **bukan** dari echo Manjo |
```

- [x] **Step 6: Verifikasi**

Run:
```bash
grep -n "topic_{merchant" /home/akbar/Kerjaan/repository/service-payment-bridge/docs/architecture.md
```
Expected: tidak ada match (semua sudah diganti ke skema baru).

---

### Task 4: `brainstorm-q161-updated.md` — Update Block Multi-Tenant

**Files:**
- Modify: `docs/brainstorm-q161-updated.md`

**Interfaces:**
- Consumes: hasil Task 1-3
- Produces: dokumen ini tetap konsisten dengan pola existing-nya (append `[Update]` block, bukan rewrite total teks asli) — sekarang juga mencatat update multi-tenant

- [x] **Step 1: Tambahkan update block di Section 3 (MQTT Topic)**

Cari teks:
```markdown
Setiap Q161 Pro milik merchant memiliki MQTT topic sendiri dengan format:

\`\`\`text
topic_{merchant_id}
\`\`\`

Contoh:

\`\`\`text
topic_MT82419344
\`\`\`
```

Tambahkan tepat setelah blok itu (sebelum baris "Mapping:" yang sudah ada):

```markdown
> **[Update multi-tenant]** Skema di atas diperluas untuk mendukung sub-merchant/tenant dan multi-device per tenant — satu merchant bisa punya beberapa tenant (opsional, tidak semua merchant punya), dan satu tenant bisa punya lebih dari satu device Q161 Pro. Format topic final: `topic/{merchant_id}/{tenant_slot}/{device_id}` (hierarkis `/`-delimited, selalu 4 segmen; `tenant_slot` = `_` kalau device tanpa tenant). Detail lengkap: `architecture.md` Section 4.4, 7.1-7.3, 9.3-9.4, dan `schema.md` Section 5-6 (`tenants`, `devices`).
```

- [x] **Step 2: Tambahkan update block di Section 12 (Merchant Resolver)**

Setelah blok update terakhir di Section 12 (yang diakhiri dengan "...tidak boleh ada di tempat yang mudah diakses."), tambahkan:

```markdown
> **[Update multi-tenant]** Merchant Resolver berkembang jadi **Device Resolver**: kunci lookup utama bukan lagi `merchant_id` dari topic, melainkan `device_id` (unik global, segmen terakhir topic). Dari `device_id`, resolver mengambil: kredensial Manjo (lewat `device.merchant_id` → `merchants`), `subMerchantId` (lewat `device.tenant_id` → `tenants`, kalau ada), dan `storeId`/`terminalId` (langsung dari `devices`). `merchant_id`/`tenant_slot` di topic terutama untuk debugging manual, bukan kunci lookup. Lihat `architecture.md` Section 4.4.
```

- [x] **Step 3: Tambahkan update block di Section 18 (Database, tabel merchants)**

Setelah tabel "Kolom tambahan" di Section 18 (yang diakhiri baris `manjo_terminal_id | opsional`), tambahkan:

```markdown
> **[Update multi-tenant]** `manjo_store_id`/`manjo_terminal_id` **dipindah** dari `merchants` ke tabel baru `devices` (satu merchant bisa punya banyak device dengan store/terminal berbeda). Dua tabel baru ditambahkan: `tenants` (sub-merchant, opsional) dan `devices` (physical Q161 unit, kunci lookup utama Device Resolver). `transactions` mendapat kolom `device_id` sebagai penentu routing MQTT publish. Skema DDL lengkap: `schema.md` Section 5-7.
```

- [x] **Step 4: Verifikasi**

Run:
```bash
grep -c "\[Update multi-tenant\]" /home/akbar/Kerjaan/repository/service-payment-bridge/docs/brainstorm-q161-updated.md
```
Expected: `3`.

---

### Task 5: `process-flow.md` — Update Step Resolver & Routing

**Files:**
- Modify: `docs/process-flow.md`

**Interfaces:**
- Consumes: hasil Task 1-4
- Produces: Flow 1 & Flow 3 mencerminkan device-level routing, bukan merchant-level

- [x] **Step 1: Update Flow 1 step 2 (MQTT Consumer)**

Ganti baris:
```markdown
| 2 | MQTT Consumer | Terima pesan, ekstrak `merchant_id` dari nama topic | — | — | Topic harus match pola `topic_{merchant_id}` | Pesan diabaikan, tidak dicatat (topic tidak dikenali sistem) |
```
menjadi:
```markdown
| 2 | MQTT Consumer | Terima pesan, split topic jadi 4 segmen (`topic/{merchant_id}/{tenant_slot}/{device_id}`), ekstrak `device_id` (segmen terakhir) | — | — | Topic harus match pola 4 segmen; `device_id` wajib ada | Pesan diabaikan, tidak dicatat (format topic tidak dikenali sistem) |
```

- [x] **Step 2: Update Flow 1 step 1 (payload publish)**

Ganti baris:
```markdown
| 1 | Q161 Pro | Publish MQTT ke `topic_{merchant_id}`, payload `{"type":"GENERATE_QR","amount":50000}` | — | — | — | (di luar scope Service) |
```
menjadi:
```markdown
| 1 | Q161 Pro | Publish MQTT ke `topic/{merchant_id}/{tenant_slot}/{device_id}` miliknya sendiri, payload `{"type":"GENERATE_QR","amount":50000}` | — | — | — | (di luar scope Service) |
```

- [x] **Step 3: Update Flow 1 step 5 (Merchant Resolver → Device Resolver)**

Ganti baris:
```markdown
| 5 | Merchant Resolver | Lookup config merchant berdasarkan `merchant_id` (dari cache in-memory, fallback ke DB) | `merchants` (`merchant_id`, `status`, `manjo_*`) | — | Merchant harus ada & `status='ACTIVE'` | Reject, catat di `mqtt_messages` sebagai `FAILED` dengan `error_message='UNKNOWN_MERCHANT'` atau `'MERCHANT_INACTIVE'`. **Flow berhenti.** |
```
menjadi:
```markdown
| 5 | Device Resolver | Lookup `devices` berdasarkan `device_id` (dari cache in-memory, fallback ke DB); dari situ ambil `merchants` (kredensial, lewat `device.merchant_id`) dan `tenants` (`manjo_sub_merchant_id`, kalau `device.tenant_id` tidak null) | `devices` (`device_id`, `merchant_id`, `tenant_id`, `status`, `manjo_store_id`, `manjo_terminal_id`); `merchants` (`status`, `manjo_*`); `tenants` (`manjo_sub_merchant_id`, `status`, kalau ada) | — | Device harus ada & `status='ACTIVE'`; merchant terkait harus `status='ACTIVE'`; kalau `tenant_id` ada, tenant terkait juga harus `status='ACTIVE'` | Reject, catat di `mqtt_messages` sebagai `FAILED` dengan `error_message='UNKNOWN_DEVICE'`/`'DEVICE_INACTIVE'`/`'MERCHANT_INACTIVE'`/`'TENANT_INACTIVE'`. **Flow berhenti.** |
```

- [x] **Step 4: Update Flow 1 step 6 (Transaction Service create) — tambah device_id**

Ganti baris:
```markdown
| 6 | Transaction Service | Generate `transaction_id` (format `TRX-{yyyyMMdd}-{seq}`) | — | `transactions`: INSERT (`transaction_id`, `merchant_id`, `amount`, `status='PENDING'`, `created_at=now()`) | `transaction_id` harus unique | Retry generate ID kalau collision (jarang terjadi dgn UUID/sequence yang benar) |
```
menjadi:
```markdown
| 6 | Transaction Service | Generate `transaction_id` (format `TRX-{yyyyMMdd}-{seq}`) | — | `transactions`: INSERT (`transaction_id`, `merchant_id`, `device_id`, `amount`, `status='PENDING'`, `created_at=now()`) | `transaction_id` harus unique | Retry generate ID kalau collision (jarang terjadi dgn UUID/sequence yang benar) |
```

- [x] **Step 5: Update Flow 1 step 9-10 (Request Builder — subMerchantId/storeId/terminalId)**

Ganti baris:
```markdown
| 10 | Manjo Client | `POST /v1.0/qr/qr-mpm-generate` ke Manjo | `merchants` (`manjo_merchant_id`, `manjo_channel_id`, `manjo_store_id`, `manjo_terminal_id`) untuk build request body | `manjo_api_logs`: INSERT (`direction=OUTBOUND`, `operation=GENERATE_QR`, `endpoint`, `request_body` *(signature di-mask)*, `transaction_id`) | Amount diformat `"50000.00"`, `dynamicAmount="N"` | — |
```
menjadi:
```markdown
| 10 | Manjo Client | `POST /v1.0/qr/qr-mpm-generate` ke Manjo | `merchants` (`manjo_merchant_id`, `manjo_channel_id`) untuk `merchantId`/`CHANNEL-ID`; `devices` (`manjo_store_id`, `manjo_terminal_id`) untuk `storeId`/`terminalId` (omit kalau null); `tenants` (`manjo_sub_merchant_id`, kalau `device.tenant_id` ada) untuk `subMerchantId` (omit kalau device tanpa tenant) | `manjo_api_logs`: INSERT (`direction=OUTBOUND`, `operation=GENERATE_QR`, `endpoint`, `request_body` *(signature di-mask)*, `transaction_id`) | Amount diformat `"50000.00"`, `dynamicAmount="N"` | — |
```

- [x] **Step 6: Update Flow 1 step 13a/13b (MQTT Publisher routing)**

Ganti baris:
```markdown
| 13a | MQTT Publisher | Build payload `QR_RESULT` (`status=SUCCESS`, `qris_payload`, `expire_at`), publish ke `topic_{merchant_id}` | `transactions` (data yang baru di-update) | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `status=PROCESSED`, `transaction_id`) | — | Kalau publish gagal → masuk retry queue (tidak mengubah status transaksi) |
| 13b | MQTT Publisher (kasus gagal) | Build payload `QR_RESULT` (`status=FAILED`, `error`), publish ke `topic_{merchant_id}` | — | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `transaction_id`) | — | — |
```
menjadi:
```markdown
| 13a | MQTT Publisher | Build payload `QR_RESULT` (`status=SUCCESS`, `qris_payload`, `expire_at`), lookup `devices.mqtt_topic` via `transactions.device_id`, publish ke topic tsb | `transactions` (data yang baru di-update), `devices.mqtt_topic` | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `status=PROCESSED`, `transaction_id`) | — | Kalau publish gagal → masuk retry queue (tidak mengubah status transaksi) |
| 13b | MQTT Publisher (kasus gagal) | Build payload `QR_RESULT` (`status=FAILED`, `error`), lookup `devices.mqtt_topic` via `transactions.device_id`, publish ke topic tsb | `devices.mqtt_topic` | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `transaction_id`) | — | — |
```

- [x] **Step 7: Update Flow 3 step 12 (Payment Notification routing)**

Ganti baris:
```markdown
| 12 | MQTT Publisher | Build payload `PAYMENT_NOTIFICATION` (`status`, `amount`, `audio_sequence`), publish ke `topic_{merchant_id}` | `transactions.merchant_id` | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `status=PROCESSED`, `transaction_id`) | — | Publish gagal (broker down) → masuk retry queue terpisah, **tidak** mempengaruhi response yang sudah dikirim ke Manjo di step 10 |
```
menjadi:
```markdown
| 12 | MQTT Publisher | Build payload `PAYMENT_NOTIFICATION` (`status`, `amount`, `audio_sequence`), lookup `devices.mqtt_topic` via `transactions.device_id`, publish ke topic tsb | `transactions.device_id`, `devices.mqtt_topic` | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `status=PROCESSED`, `transaction_id`) | — | Publish gagal (broker down) → masuk retry queue terpisah, **tidak** mempengaruhi response yang sudah dikirim ke Manjo di step 10. **Catatan:** routing selalu lewat `transactions.device_id` internal, **tidak pernah** dari field Manjo (Payment Notification tidak konsisten membawa info tenant/device) |
```

- [x] **Step 8: Update Ringkasan Data Touchpoint (Section 6)**

Ganti baris `transactions` di tabel Section 6 menjadi:
```markdown
| `transactions` | Flow 1 (step 6, 9, 11a/11b, 12b), Flow 3 (step 9), Flow 4 (step 3) | Flow 1 (step 5 — via `devices`, step 13a), Flow 3 (step 5, 6a, 6b, 8, 11, 12 — via `device_id`), Flow 4 (step 2) |
```

Tambahkan baris baru setelah baris `merchants`:
```markdown
| `tenants` | (diisi dari luar, di luar scope 4 flow ini) | Flow 1 (step 5, 10) |
| `devices` | (diisi dari luar, di luar scope 4 flow ini) | Flow 1 (step 5, 10, 13a, 13b), Flow 3 (step 12) |
```

- [x] **Step 9: Verifikasi**

Run:
```bash
grep -n "topic_{merchant" /home/akbar/Kerjaan/repository/service-payment-bridge/docs/process-flow.md
```
Expected: tidak ada match.

---

### Task 6: `PRD.md` — Functional Requirement Baru + Verifikasi Konsistensi Akhir

**Files:**
- Modify: `docs/prd.md`

**Interfaces:**
- Consumes: hasil Task 1-5
- Produces: `PRD.md` mencatat kapabilitas multi-tenant sebagai requirement resmi; seluruh 6 dokumen ter-verifikasi konsisten

- [x] **Step 1: Tambahkan FR baru di Section 8**

Setelah baris `FR-17` (Query Payment) di tabel Functional Requirements, tambahkan:

```markdown
| FR-18 | Service mendukung sub-merchant/tenant opsional per merchant, dan multi-device per tenant, dengan topic MQTT ter-scope per device — transaksi satu device tidak boleh memicu notifikasi ke device lain | Must |
```

- [x] **Step 2: Tambahkan ke Asumsi & Dependensi (Section 12)**

Setelah baris asumsi terakhir di Section 12, tambahkan:

```markdown
- Data `tenants`/`devices` (termasuk `device_id` per unit Q161 fisik) diisi lewat proses/sistem di luar Payment Bridge Service — Service ini hanya membaca, tidak menyediakan CRUD untuk mengelolanya (konsisten dengan asumsi kredensial merchant di atas).
```

- [x] **Step 3: Verifikasi konsistensi akhir lintas 6 dokumen**

Run:
```bash
cd /home/akbar/Kerjaan/repository/service-payment-bridge/docs
grep -rn "topic_{merchant" *.md
```
Expected: **tidak ada match** di `architecture.md`, `process-flow.md` (sudah diganti Task 3 & 5). Match yang tersisa (kalau ada) hanya boleh di `brainstorm-q161-updated.md` sebagai bagian narasi ASLI yang sengaja dipertahankan (Task 4 menambahkan update block, bukan menghapus teks asli) — cek manual tiap match untuk pastikan itu teks lama yang memang sudah dijelaskan sudah diperbarui oleh update block di sebelahnya, bukan referensi yang lolos tak ter-update.

Run:
```bash
grep -c "device_id" schema.md architecture.md process-flow.md
```
Expected: semua tiga file punya match > 0.

---

## Selesai

Setelah Task 1-6 lulus semua verifikasi: skema database mendukung sub-merchant/tenant opsional dan multi-device per tenant, topic MQTT hierarkis 4-segmen valid secara wildcard MQTT, dan keenam dokumen desain (`schema.md`, `architecture.md`, `brainstorm-q161-updated.md`, `process-flow.md`, `PRD.md`, plus migration files) konsisten satu sama lain. Implementasi kode Go untuk Device Resolver/Request Builder/routing logic menyusul di plan Fase 2+ (belum ada kode business logic yang perlu diubah di plan ini).
