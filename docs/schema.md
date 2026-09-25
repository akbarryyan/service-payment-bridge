# Database Schema — Q161 Pro × Manjo Payment Bridge Service

**Versi:** 1.0
**Database:** PostgreSQL 14+
**Status:** Siap untuk migration
**Dokumen terkait:** `architecture.md` (Section 9 — versi ringkas), `PRD.md`

---

## Daftar Isi

1. [ERD (Entity Relationship Diagram)](#1-erd-entity-relationship-diagram)
2. [Konvensi](#2-konvensi)
3. [Extension & Enum Types](#3-extension--enum-types)
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

---

## 1. ERD (Entity Relationship Diagram)

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

---

## 2. Konvensi

- **Primary key**: UUID (`gen_random_uuid()`), bukan auto-increment integer — memudahkan penggabungan data lintas environment/replika tanpa konflik ID.
- **Timestamp**: selalu `TIMESTAMPTZ` (timezone-aware), disimpan dalam UTC, konversi ke WIB dilakukan di application layer / saat query.
- **Nama tabel & kolom**: `snake_case`, plural untuk nama tabel.
- **Status/enum**: pakai PostgreSQL `ENUM` type supaya nilai invalid ditolak di level database, bukan hanya di aplikasi.
- **Soft delete**: tidak dipakai di v1 — semua tabel bersifat append/update-only sesuai kebutuhan audit trail; penghapusan data mengikuti kebijakan retention ([Section 11](#11-retention--archival)), bukan flag `deleted_at`.
- **Uang (`amount`)**: disimpan sebagai `BIGINT` dalam satuan **Rupiah utuh** (bukan sen, bukan desimal) — konversi ke format `"50000.00"` yang dibutuhkan Manjo dilakukan di application layer saat build request.

---

## 3. Extension & Enum Types

```sql
-- Extension untuk gen_random_uuid()
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- Status merchant
CREATE TYPE merchant_status AS ENUM (
    'ACTIVE',
    'INACTIVE'
);

-- Status transaksi internal (state machine — lihat architecture.md Section 10)
CREATE TYPE transaction_status AS ENUM (
    'PENDING',       -- request generate QR diterima, belum ada response Manjo
    'QR_GENERATED',  -- qrContent sudah didapat, menunggu pembayaran
    'PAID',          -- pembayaran sukses (final)
    'FAILED',        -- generate QR gagal ATAU pembayaran gagal (final)
    'EXPIRED',       -- QR tidak dibayar sampai validityPeriod lewat (final)
    'CANCELLED',     -- dibatalkan (final)
    'REFUNDED'       -- direfund setelah PAID (final)
);

-- Arah pesan MQTT
CREATE TYPE mqtt_direction AS ENUM (
    'INBOUND',   -- dari Q161 ke Service
    'OUTBOUND'   -- dari Service ke Q161
);

-- Status pemrosesan pesan MQTT
CREATE TYPE mqtt_message_status AS ENUM (
    'RECEIVED',
    'PROCESSED',
    'FAILED'
);

-- Arah panggilan API Manjo
CREATE TYPE manjo_api_direction AS ENUM (
    'OUTBOUND',  -- Service memanggil Manjo (access token, generate QR)
    'INBOUND'    -- Manjo memanggil webhook Service (payment notification)
);

-- Jenis operasi API Manjo
CREATE TYPE manjo_api_operation AS ENUM (
    'ACCESS_TOKEN',
    'GENERATE_QR',
    'QUERY_PAYMENT',
    'PAYMENT_NOTIFY'
);
```

---

## 4. Tabel `merchants`

```sql
CREATE TABLE merchants (
    id                        UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    merchant_id               VARCHAR(36)  NOT NULL UNIQUE,   -- contoh: MT82419344

    -- Kredensial Manjo (nilai sensitif TIDAK disimpan plaintext di sini,
    -- kolom *_ref menyimpan referensi/path ke secret manager)
    manjo_client_id           VARCHAR(64)  NOT NULL,          -- X-CLIENT-KEY
    manjo_private_key_ref     VARCHAR(255) NOT NULL,          -- referensi ke secret manager (SHA256withRSA)
    manjo_client_secret_ref   VARCHAR(255) NOT NULL,          -- referensi ke secret manager (HMAC-SHA512)
    manjo_merchant_id         VARCHAR(64)  NOT NULL,          -- merchantId / X-PARTNER-ID di request Manjo
    manjo_channel_id          VARCHAR(5)   NOT NULL,          -- CHANNEL-ID

    status                    merchant_status NOT NULL DEFAULT 'ACTIVE',

    created_at                TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at                TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_merchants_status ON merchants (status);
```

| Kolom | Keterangan |
|---|---|
| `merchant_id` | ID identitas merchant di sistem kita, dipakai untuk FK dari `tenants`/`devices` dan sebagai `merchantId`/`X-PARTNER-ID` di request Manjo |
| `manjo_merchant_id` | Sengaja dipisah dari `merchant_id` (meski nilainya kemungkinan sama) — mengantisipasi kalau suatu saat penomoran internal berbeda dari penomoran Manjo |
| `manjo_private_key_ref` / `manjo_client_secret_ref` | **Wajib** berupa referensi (path/key ke Vault atau secret manager), bukan nilai asli — lihat `architecture.md` Section 14 (Security) |

> **Catatan desain (resolved):** kredensial Manjo di tabel ini **shared di level merchant** — semua tenant/device di bawahnya (lihat [Section 5](#5-tabel-tenants), [Section 6](#6-tabel-devices)) pakai kredensial yang sama, tidak ada kredensial per-tenant.
>
> **Catatan (multi-tenant):** `mqtt_topic`, `manjo_store_id`, `manjo_terminal_id` **tidak lagi ada di `merchants`** — sekarang jadi properti tabel `devices`, karena satu merchant bisa punya banyak device dengan store/terminal berbeda-beda.

---

## 5. Tabel `tenants`

Merepresentasikan sub-merchant/tenant di bawah satu `merchants` — **opsional**, tidak semua merchant punya tenant. Tenant di-input oleh merchant sendiri lewat sistem lain (di luar scope Payment Bridge Service, yang hanya membaca tabel ini).

```sql
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
```

| Kolom | Keterangan |
|---|---|
| `tenant_id` | Identitas tenant, dipakai sebagai segmen `tenant_slot` di topic MQTT (`topic/{merchant_id}/{tenant_id}/{device_id}`) |
| `manjo_sub_merchant_id` | Dikirim sebagai `subMerchantId` saat Generate QR (Manjo, field optional) — **tidak perlu pre-registrasi ke Manjo**, bebas ditentukan Service/merchant |
| `status` | Reuse enum `merchant_status` (`ACTIVE`/`INACTIVE`) — semantiknya sama, tidak perlu enum baru |

---

## 6. Tabel `devices`

Satu row = satu physical Q161 Pro unit. Device **selalu** terhubung ke satu `merchants` (langsung, atau lewat `tenants` kalau `tenant_id` terisi).

```sql
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
```

| Kolom | Keterangan |
|---|---|
| `device_id` | **Kunci lookup utama Device Resolver** — unik secara global (bukan cuma unik per merchant), diparse dari segmen terakhir topic MQTT |
| `tenant_id` | Nullable — null berarti device nempel langsung ke merchant tanpa tenant. Kalau terisi, topic pakai `tenant_id` asli; kalau null, topic pakai sentinel `_` di posisi `tenant_slot` |
| `mqtt_topic` | Topic lengkap 4 segmen, disimpan untuk kemudahan query (`topic/{merchant_id}/{tenant_slot}/{device_id}`) |
| `manjo_store_id` / `manjo_terminal_id` | Properti per-device (dipindah dari `merchants` — lihat [Section 4](#4-tabel-merchants)) |

---

## 7. Tabel `transactions`

```sql
CREATE TABLE transactions (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    transaction_id      VARCHAR(64)  NOT NULL UNIQUE,   -- format: TRX-{yyyyMMdd}-{sequence}, = partnerReferenceNo
    reference_no        VARCHAR(64),                    -- referenceNo dari response Manjo (diisi setelah QR_GENERATED)
    merchant_id         VARCHAR(36)  NOT NULL REFERENCES merchants (merchant_id),
    device_id           VARCHAR(64)  NOT NULL REFERENCES devices (device_id),

    amount              BIGINT       NOT NULL CHECK (amount > 0),  -- Rupiah utuh
    status              transaction_status NOT NULL DEFAULT 'PENDING',
    manjo_status_code   VARCHAR(2),                     -- latestTransactionStatus mentah (00-06), untuk audit

    qris_payload        TEXT,                           -- qrContent
    external_id         VARCHAR(36),                    -- X-EXTERNAL-ID yang dipakai saat generate QR (tracing)
    expire_at           TIMESTAMPTZ,                     -- dari additionalInfo.expireDate

    created_at          TIMESTAMPTZ NOT NULL DEFAULT now(),
    paid_at             TIMESTAMPTZ,
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_transactions_merchant_status ON transactions (merchant_id, status);
CREATE INDEX idx_transactions_device_id ON transactions (device_id);
CREATE INDEX idx_transactions_reference_no ON transactions (reference_no);
CREATE INDEX idx_transactions_expire_at ON transactions (expire_at) WHERE status = 'QR_GENERATED';
```

| Kolom | Keterangan |
|---|---|
| `transaction_id` | Dikontrol penuh oleh Service, dikirim ke Manjo sebagai `partnerReferenceNo`. Format disarankan: `TRX-20260924-000001` |
| `reference_no` | `referenceNo` dari Manjo — dipakai untuk audit/cross-check ke sisi Manjo, **bukan** untuk matching transaksi (matching selalu via `transaction_id`) |
| `device_id` | **Menentukan topic MQTT tujuan** saat publish hasil generate-QR atau notifikasi pembayaran — lookup `devices.mqtt_topic` dari kolom ini, **bukan** dari echo Manjo (Manjo tidak konsisten mengembalikan info tenant/device di notifikasi) |
| `manjo_status_code` | Simpan kode mentah dari **Payment Notification** (`00`–`06`) terpisah dari `status` internal, supaya kalau ada perbedaan interpretasi (misal isu `00` vs `03` yang belum terkonfirmasi) tetap ada data mentah untuk investigasi. **Catatan:** skema kode ini khusus Payment Notification — Query Payment (Service Code `51`) punya skema `latestTransactionStatus` yang berbeda arti untuk kode yang sama, jangan tulis hasil Query mentah ke kolom ini tanpa mapping ulang (lihat `manjo-api-docs.md` Section 5.10) |
| `idx_transactions_expire_at` (partial index) | Dioptimalkan khusus untuk query job auto-expire ([Section 11](#11-query-umum-reference)) — hanya index baris berstatus `QR_GENERATED` |

**Constraint tambahan yang disarankan** (implementasi bisa di level aplikasi atau trigger, tergantung preferensi):
- Transisi status final (`PAID`, `FAILED`, `EXPIRED`, `CANCELLED`, `REFUNDED`) tidak boleh berubah lagi — lihat `architecture.md` Section 10. Ini **tidak** di-enforce lewat `CHECK` constraint SQL biasa (butuh melihat nilai lama vs baru), disarankan pakai trigger `BEFORE UPDATE` atau validasi di Transaction Service (application layer) sebagai lapisan pertama.

```sql
-- Contoh trigger opsional: cegah update status kalau status lama sudah final
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

> Trigger ini bersifat **opsional tapi direkomendasikan** sebagai safety net tambahan di luar logic aplikasi — kalau ada bug di Transaction Service yang mencoba menimpa status final, database akan menolak alih-alih diam-diam merusak data.

---

## 8. Tabel `mqtt_messages`

```sql
CREATE TABLE mqtt_messages (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    topic           VARCHAR(64) NOT NULL,
    payload         JSONB       NOT NULL,
    direction       mqtt_direction NOT NULL,
    status          mqtt_message_status NOT NULL DEFAULT 'RECEIVED',
    transaction_id  VARCHAR(64) REFERENCES transactions (transaction_id),  -- nullable: pesan invalid mungkin belum ke-link ke transaksi manapun

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    processed_at    TIMESTAMPTZ,
    error_message   TEXT
);

CREATE INDEX idx_mqtt_messages_transaction_id ON mqtt_messages (transaction_id);
CREATE INDEX idx_mqtt_messages_topic_created_at ON mqtt_messages (topic, created_at DESC);
```

- `payload` pakai `JSONB` (bukan `TEXT`) supaya bisa di-query langsung kalau perlu (mis. `WHERE payload->>'amount' = '50000'`) tanpa parse ulang di aplikasi.
- `transaction_id` nullable karena pesan yang gagal divalidasi (topic tidak dikenal, format rusak) mungkin belum sempat ter-link ke transaksi manapun — tetap dicatat untuk debugging.

---

## 9. Tabel `manjo_api_logs`

```sql
CREATE TABLE manjo_api_logs (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    direction       manjo_api_direction NOT NULL,
    operation       manjo_api_operation NOT NULL,
    endpoint        VARCHAR(255) NOT NULL,
    http_status     INTEGER,
    request_body    JSONB,    -- header sensitif (X-SIGNATURE, accessToken, private key) WAJIB di-mask sebelum disimpan
    response_body   JSONB,
    transaction_id  VARCHAR(64) REFERENCES transactions (transaction_id),  -- null untuk operasi ACCESS_TOKEN (tidak terkait transaksi spesifik)
    duration_ms     INTEGER,

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_manjo_api_logs_transaction_id ON manjo_api_logs (transaction_id);
CREATE INDEX idx_manjo_api_logs_operation_created_at ON manjo_api_logs (operation, created_at DESC);
```

> **Peringatan keamanan:** `request_body` dan `response_body` **tidak boleh** menyimpan `X-SIGNATURE`, `accessToken`, atau isi private key secara utuh. Masking wajib dilakukan di application layer sebelum insert (contoh: ganti nilai signature/token dengan `"***MASKED***"` atau simpan hanya beberapa karakter terakhir untuk keperluan pencocokan visual).

---

## 10. Ringkasan Relasi & Index

| Tabel | Foreign Key | Referensi | Index Utama |
|---|---|---|---|
| `tenants` | `merchant_id` | `merchants.merchant_id` | `merchant_id`, `status` |
| `devices` | `merchant_id`, `tenant_id` (nullable) | `merchants.merchant_id`, `tenants.tenant_id` | `merchant_id`, `tenant_id`, `status` |
| `transactions` | `merchant_id`, `device_id` | `merchants.merchant_id`, `devices.device_id` | `(merchant_id, status)`, `device_id`, `reference_no`, partial index `expire_at` |
| `mqtt_messages` | `transaction_id` (nullable) | `transactions.transaction_id` | `transaction_id`, `(topic, created_at)` |
| `manjo_api_logs` | `transaction_id` (nullable) | `transactions.transaction_id` | `transaction_id`, `(operation, created_at)` |

Semua foreign key memakai `transaction_id` (string bisnis) bukan `id` (UUID surrogate) — ini pilihan desain supaya query tracing (`WHERE transaction_id = 'TRX-...'`) tidak perlu join tambahan untuk menerjemahkan UUID, karena `transaction_id` adalah identifier yang paling sering dipakai untuk investigasi manual.

---

## 11. Query Umum (Reference)

**Cek idempotency saat notifikasi masuk:**

```sql
SELECT status, manjo_status_code
FROM transactions
WHERE transaction_id = $1
FOR UPDATE;  -- row lock untuk hindari race condition antar notifikasi bersamaan
```

**Auto-expire job (dijalankan periodik, misal tiap 1 menit):**

```sql
UPDATE transactions
SET status = 'EXPIRED', updated_at = now()
WHERE status = 'QR_GENERATED'
  AND expire_at < now()
RETURNING transaction_id, merchant_id;
```

**Tracing lengkap satu transaksi (untuk kasus "sudah bayar tidak bunyi"):**

```sql
SELECT 'mqtt' AS source, direction::text, status::text, created_at, payload::text AS detail
FROM mqtt_messages WHERE transaction_id = $1
UNION ALL
SELECT 'manjo_api' AS source, direction::text, http_status::text, created_at, endpoint AS detail
FROM manjo_api_logs WHERE transaction_id = $1
ORDER BY created_at ASC;
```

**Cari transaksi aktif per merchant (dashboard/monitoring):**

```sql
SELECT transaction_id, amount, status, created_at
FROM transactions
WHERE merchant_id = $1
  AND status IN ('PENDING', 'QR_GENERATED')
ORDER BY created_at DESC;
```

---

## 12. Migration Order

Urutan pembuatan tabel (penting karena foreign key dependency):

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

Disarankan pakai tool migration seperti **`golang-migrate`** (selaras dengan tech stack Go di `architecture.md`), dengan satu file migration per langkah di atas supaya rollback granular kalau ada masalah di tengah deployment.

---

## 13. Retention & Archival

- `mqtt_messages` dan `manjo_api_logs` berpotensi tumbuh besar seiring volume transaksi — pertimbangkan **partisi per bulan** (`PARTITION BY RANGE (created_at)`) setelah volume signifikan, atau job archival ke cold storage untuk data lebih dari N bulan.
- `transactions` **tidak** direkomendasikan untuk di-purge/dihapus — ini data transaksional inti yang dibutuhkan untuk audit jangka panjang (rekonsiliasi, dispute, laporan keuangan).
- Kalau ada regulasi terkait retensi data pembayaran (mis. kebutuhan compliance QRIS/Bank Indonesia), sesuaikan durasi retention `manjo_api_logs` dengan requirement tersebut — belum dicakup di dokumen ini karena di luar scope teknis.

---

## 14. Catatan Terbuka

Item yang mempengaruhi bentuk skema ini dan masih perlu diputuskan (carry-over dari `PRD.md` Section 14):

1. ~~**Kredensial per-merchant vs per-partner**~~ — **Sudah diputuskan**: kredensial Manjo shared di level `merchants`, tenant/device di bawahnya tidak punya kredensial sendiri. Model multi-tenant lengkap ada di [Section 5](#5-tabel-tenants) dan [Section 6](#6-tabel-devices).
2. **Kebutuhan tabel `REFUNDED` lebih detail** — saat ini `REFUNDED` hanya status, belum ada tabel `refunds` terpisah untuk menyimpan detail refund (jumlah, alasan, tanggal). Kalau refund handling aktif ditambahkan di versi mendatang (saat ini Non-Goal di PRD), tabel baru perlu ditambahkan.
3. **Partisi tabel log** — keputusan kapan mulai partisi `mqtt_messages`/`manjo_api_logs` tergantung proyeksi volume transaksi, belum ditentukan di dokumen ini.
4. **Reconciliation job via Query Payment** — belum diputuskan apakah v1 mengimplementasikan job berkala yang memanggil Query Payment (Service Code `51`) untuk transaksi `QR_GENERATED` mendekati/lewat `expire_at`, sebagai pengaman tambahan sebelum divonis `EXPIRED` (lihat `architecture.md` Section 20 Open Decision #7). Kalau diimplementasikan, hasil query (skema status `00`–`07` yang berbeda dari Payment Notification) perlu dipetakan lewat fungsi mapping terpisah sebelum ditulis ke `transactions.manjo_status_code`/`status`.
5. **Provisioning device_id** — bagaimana `device_id` pertama kali ditetapkan ke unit Q161 fisik (manual input, QR provisioning, dsb.) belum ditentukan di dokumen ini — di luar scope teknis Service (Service hanya membaca tabel `devices` yang sudah terisi).