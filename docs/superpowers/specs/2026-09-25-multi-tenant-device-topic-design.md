# Design Spec — Multi-Tenant / Multi-Device Topic & Data Model

**Tanggal:** 2026-09-25
**Status:** Disetujui, siap masuk writing-plans
**Scope:** Perluasan skema topic MQTT dan data model `merchants` agar mendukung sub-merchant/tenant dan multi-device per tenant, tanpa menyebabkan satu device ikut bunyi untuk transaksi device lain.
**Dokumen terkait:** `architecture.md`, `schema.md`, `brainstorm-q161-updated.md`, `process-flow.md`, `PRD.md`, `docs/manjo-collection/` (Bruno collection resmi Manjo)

---

## 1. Masalah

Skema saat ini (`topic_{merchant_id}`, satu topic per merchant, diasumsikan 1 merchant = 1 device) tidak menangani kasus merchant yang punya banyak sub-merchant/tenant, di mana tiap tenant bisa punya device Q161 Pro sendiri-sendiri (bahkan bisa lebih dari satu device per tenant). Kalau topic tetap di-scope per merchant, **semua device di bawah merchant itu akan menerima notifikasi pembayaran milik tenant lain** — soundbox bunyi untuk transaksi yang bukan miliknya.

## 2. Temuan Teknis Tambahan (ditemukan saat riset, bukan bagian dari masalah awal)

Skema topic lama (`topic_{merchant_id}`, flat, underscore) dikombinasikan dengan rencana "subscribe pakai wildcard `topic_+`" (`architecture.md` Section 4.1) **secara teknis tidak valid** — wildcard MQTT (`+`/`#`) hanya bekerja pada level yang dipisah `/`, bukan pada string yang digabung underscore. Karena topic scheme toh harus didesain ulang untuk menambah level tenant+device, sekalian dibetulkan di desain ini dengan beralih ke topic hierarkis `/`-delimited.

## 3. Keputusan (hasil brainstorming)

| # | Keputusan | Rasional |
|---|---|---|
| 1 | Kredensial Manjo (client_id, private key, client secret) **shared di level merchant** — satu set kredensial dipakai semua tenant di bawahnya | Konfirmasi user: model bisnisnya begitu, bukan tiap tenant punya kredensial Manjo sendiri |
| 2 | Satu tenant **bisa punya lebih dari satu device** Q161 Pro | Konfirmasi user — jadi granularitas topic/routing harus di level **device**, bukan cuma tenant |
| 3 | Tenant **opsional** — device boleh langsung nempel ke merchant tanpa tenant | Konfirmasi user: tidak semua merchant punya sub-tenant |
| 4 | Topic pakai depth **tetap 4 segmen** (bukan variable-length) | Menghindari percabangan kode di MQTT Consumer berdasarkan panjang topic — parser selalu split jadi 4 bagian |
| 5 | `subMerchantId` **tidak perlu pre-registrasi ke Manjo** — bebas dikirim sesuai tenant yang di-input merchant | Konfirmasi user: tenant di-input oleh merchant sendiri (lewat sistem lain, di luar scope Service ini), bukan proses onboarding manual ke Manjo |
| 6 | Payment Bridge Service **hanya membaca** tabel `tenants`/`devices` (sama seperti pola `merchants` sekarang) — tidak menyediakan CRUD/API untuk mengelola tenant | Konsisten dengan Non-Goal `PRD.md`: "Dashboard/UI monitoring untuk merchant" di luar scope v1 |

## 4. Skema Topic MQTT

```
topic/{merchant_id}/{tenant_slot}/{device_id}
```

- `merchant_id` — sama seperti sebelumnya, identifier merchant (`merchants.merchant_id`).
- `tenant_slot` — `tenant_id` asli kalau device di bawah tenant; **literal sentinel `_`** (satu karakter underscore) kalau device nempel langsung ke merchant tanpa tenant. Sentinel dipilih (bukan segmen kosong) karena topic level kosong secara teknis legal di MQTT tapi membingungkan untuk debugging manual dan tidak konsisten ditangani semua tooling/broker.
- `device_id` — identifier unik per **physical Q161 unit**, ditetapkan saat provisioning device. **Unik secara global** (bukan cuma unik per merchant) — ini kunci lookup utama di Device Resolver, bukan kombinasi merchant_id+tenant_id+device_id.

**Subscription:** `topic/#` (multi-level wildcard) — sekarang valid karena benar-benar pakai `/`. Alternatif lebih sempit per merchant: `topic/{merchant_id}/#`. Karena depth topic dibuat tetap 4 segmen (keputusan #4), kasus "tanpa tenant" tetap punya 4 segmen dengan `tenant_slot=_` (bukan 3 segmen) — jadi `topic/{merchant_id}/#` selalu menangkap persis 2 segmen sisanya (`tenant_slot` dan `device_id`) tanpa ambiguitas, baik device itu punya tenant atau tidak.

**Contoh:**
- Device tanpa tenant: `topic/MT82419344/_/DEV-001`
- Device dengan tenant: `topic/MT82419344/TNT-toko-a/DEV-002`

## 5. Data Model

### 5.1 Tabel baru: `tenants`

| Kolom | Tipe | Keterangan |
|---|---|---|
| `id` | UUID PK | |
| `tenant_id` | VARCHAR, UNIQUE | dipakai di segmen `tenant_slot` topic |
| `merchant_id` | VARCHAR, FK → `merchants.merchant_id` | |
| `manjo_sub_merchant_id` | VARCHAR, nullable | dikirim sebagai `subMerchantId` (Generate QR request) — bebas, tidak perlu pre-registrasi Manjo |
| `name` | VARCHAR | label bisnis, mis. "Toko A" — untuk keperluan audit/support, bukan dipakai logic |
| `status` | enum(`ACTIVE`,`INACTIVE`) | |
| `created_at` / `updated_at` | TIMESTAMPTZ | |

### 5.2 Tabel baru: `devices`

| Kolom | Tipe | Keterangan |
|---|---|---|
| `id` | UUID PK | |
| `device_id` | VARCHAR, UNIQUE | dipakai di segmen `device_id` topic — **kunci lookup utama Device Resolver** |
| `merchant_id` | VARCHAR, FK → `merchants.merchant_id`, NOT NULL | selalu ada, baik device di bawah tenant atau langsung ke merchant |
| `tenant_id` | VARCHAR, FK → `tenants.tenant_id`, **NULLABLE** | null = device langsung ke merchant, tanpa tenant |
| `mqtt_topic` | VARCHAR, UNIQUE | topic lengkap (disimpan untuk kemudahan query, sama seperti pola `merchants.mqtt_topic` sebelumnya) |
| `manjo_store_id` | VARCHAR, nullable | dikirim sebagai `storeId` — **dipindah dari `merchants`**, sekarang properti device |
| `manjo_terminal_id` | VARCHAR, nullable | dikirim sebagai `terminalId` — **dipindah dari `merchants`** |
| `status` | enum(`ACTIVE`,`INACTIVE`) | |
| `created_at` / `updated_at` | TIMESTAMPTZ | |

### 5.3 Perubahan tabel existing

**`merchants`** — kolom `mqtt_topic`, `manjo_store_id`, `manjo_terminal_id` **dihapus** (dipindah ke `devices`, karena sekarang itu properti per-device, bukan per-merchant — satu merchant bisa punya banyak device dengan store/terminal berbeda-beda).

**`transactions`** — tambah kolom `device_id VARCHAR NOT NULL REFERENCES devices(device_id)`. Ini yang menentukan ke topic mana hasil generate-QR/notifikasi pembayaran di-routing. Kolom `merchant_id` **tetap dipertahankan** (denormalisasi) untuk query cepat "semua transaksi milik merchant X" tanpa join ke `devices`.

## 6. Perilaku Komponen yang Berubah

**Merchant/Device Resolver** (`architecture.md` Section 4.4, `brainstorm-q161-updated.md` Section 12): parse topic (split by `/`, selalu 4 bagian: `["topic", merchant_id, tenant_slot, device_id]`) → lookup `devices` by `device_id` (kunci utama, unique global) → dari situ dapat `merchant_id` (untuk kredensial) dan `tenant_id` (kalau ada, untuk `manjo_sub_merchant_id`). Cache in-memory di-key oleh `device_id`.

**Request Builder — Generate QR**: `merchantId` dari merchant (`manjo_merchant_id`), `subMerchantId` dari tenant (**omit** field ini kalau `device.tenant_id` null — field Optional di Manjo), `storeId`/`terminalId` dari device (**omit** kalau null).

**Payment Notification routing** — **tidak berubah pola dasarnya**, hanya diperpanjang satu level: lookup `transaction_id` (dari `originalPartnerReferenceNo`) → `transactions.device_id` → `devices.mqtt_topic` → publish. **Tetap tidak bergantung pada echo Manjo** untuk routing (Payment Notification API tidak konsisten mengembalikan `subMerchantId` sama sekali menurut skema resminya) — DB internal kita tetap satu-satunya source of truth untuk routing, sama seperti desain merchant-level yang sudah ada.

## 7. Dampak ke Kode Fase 1 yang Sudah Jalan

Migration `000002_create_merchants` sudah dieksekusi di database dev (belum ada data produksi). Perubahan dilakukan lewat **migration baru** (bukan edit migration lama — immutable migration principle):
- `ALTER TABLE merchants DROP COLUMN mqtt_topic, DROP COLUMN manjo_store_id, DROP COLUMN manjo_terminal_id`
- `CREATE TABLE tenants` (+ index)
- `CREATE TABLE devices` (+ index)
- `ALTER TABLE transactions ADD COLUMN device_id ... REFERENCES devices(device_id)`

Kode Fase 1 (`internal/database`, `internal/httpserver`, `internal/mqttclient`, `cmd/server/main.go`) tidak menyentuh tabel-tabel ini sama sekali (Fase 1 murni fondasi tanpa business logic) — jadi tidak ada kode Go yang perlu diubah, hanya migration.

## 8. Dokumen yang Perlu Diupdate (follow-up, bukan bagian spec teknis ini)

Desain ini mengubah beberapa bagian dokumen yang sudah ada dan **perlu disinkronkan** sebagai task terpisah di implementation plan:
- `schema.md` — DDL `merchants`/`transactions`, tambah `tenants`/`devices`, ERD, migration order
- `architecture.md` — Section 3 (skema topic), Section 4.4 (Merchant Resolver), Section 8 (kontrak Manjo Client), Section 9 (data model)
- `brainstorm-q161-updated.md` — Section 3 (MQTT Topic), Section 12 (Merchant Resolver)
- `process-flow.md` — Flow 1 step 5 (resolver), step 9-10 (request builder), Flow 3 step 12 (routing notifikasi)
- `PRD.md` — kemungkinan FR baru untuk multi-tenant support

## 9. Non-Goals (Eksplisit)

- **Tidak** menyediakan API/UI untuk merchant mengelola tenant/device miliknya — itu sistem lain, di luar scope Payment Bridge Service (Service hanya membaca tabel `tenants`/`devices`).
- **Tidak** menangani proses provisioning device (bagaimana `device_id` pertama kali ditetapkan ke unit Q161 fisik) — asumsinya sudah ada di luar scope, sama seperti asumsi kredensial merchant sudah tersedia lewat proses onboarding terpisah.
- **Tidak** mengubah kontrak Payment Notification dari Manjo — perubahan murni di sisi internal Service (data model + routing), tidak ada dependency baru ke field Manjo yang belum ada di dokumentasi resmi.

## 10. Referensi

- `docs/manjo-collection/` — Bruno collection resmi Manjo, dipakai untuk verifikasi field `subMerchantId`/`storeId`/`terminalId`/`externalStoreId` benar-benar ada di request/response nyata (bukan cuma di dokumentasi).
- `manjo-api-docs.md` Section 3.6 (Generate QRIS MPM request params — `subMerchantId`, `storeId`, `terminalId`), Section 4.7 (Payment Notification params — `externalStoreId`, tidak ada `subMerchantId`).
