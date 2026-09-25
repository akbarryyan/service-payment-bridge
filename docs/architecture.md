# Architecture Document — Q161 Pro × Manjo Payment Bridge Service

**Versi:** 1.0
**Status:** Draft implementasi (turunan dari `brainstorm-q161-updated.md` + `manjo-api-docs.md`)
**Terakhir diperbarui:** September 2026

---

## Daftar Isi

1. [Tujuan Dokumen](#1-tujuan-dokumen)
2. [System Context](#2-system-context)
3. [Prinsip Desain](#3-prinsip-desain)
4. [Komponen Arsitektur](#4-komponen-arsitektur)
5. [Sequence Flow — Generate QR](#5-sequence-flow--generate-qr)
6. [Sequence Flow — Payment Notification](#6-sequence-flow--payment-notification)
7. [Kontrak Internal (Q161 ↔ Service)](#7-kontrak-internal-q161--service)
8. [Kontrak Eksternal (Service ↔ Manjo)](#8-kontrak-eksternal-service--manjo)
9. [Data Model](#9-data-model)
10. [State Machine Transaksi](#10-state-machine-transaksi)
11. [Idempotency Strategy](#11-idempotency-strategy)
12. [Concurrency Model](#12-concurrency-model)
13. [Retry & Error Handling](#13-retry--error-handling)
14. [Security](#14-security)
15. [Observability](#15-observability)
16. [Deployment Topology](#16-deployment-topology)
17. [Rekomendasi Tech Stack](#17-rekomendasi-tech-stack)
18. [Struktur Proyek (Contoh)](#18-struktur-proyek-contoh)
19. [Konfigurasi / Environment Variables](#19-konfigurasi--environment-variables)
20. [Open Decisions](#20-open-decisions)
21. [Referensi](#21-referensi)

---

## 1. Tujuan Dokumen

Dokumen ini adalah **spesifikasi arsitektur siap-implementasi** untuk Payment Bridge Service yang menghubungkan **Q161 Pro Soundbox** dengan **Manjo QRIS MPM API** melalui **MQTT Broker**.

Berbeda dari `brainstorm-q161-updated.md` (yang berisi eksplorasi konsep dan diskusi trade-off), dokumen ini berfokus pada **keputusan final**: komponen apa saja yang dibangun, kontrak data konkret, skema database, dan urutan eksekusi step-by-step — supaya bisa langsung dipakai sebagai acuan coding.

Dua fungsi inti sistem:

1. **QR Generation Bridge** — Q161 minta QRIS dinamis, Service teruskan ke Manjo, hasil `qrContent` dikirim balik ke Q161.
2. **Payment Notification Bridge** — Manjo memberi tahu status pembayaran via webhook HTTP, Service menerjemahkannya jadi notifikasi MQTT yang memicu soundbox bunyi.

---

## 2. System Context

```text
┌────────────┐        MQTT         ┌──────────────┐        MQTT         ┌─────────────────────┐
│  Q161 Pro  │◄───────────────────►│ MQTT Broker  │◄───────────────────►│  Payment Bridge      │
│  (device)  │  topic/mid/tnt/did  │              │  topic/mid/tnt/did  │  Service              │
└────────────┘                     └──────────────┘                     │                       │
                                                                         │  ┌─────────────────┐  │
                                                                         │  │ Manjo Client     │  │
                                                                         │  └────────┬────────┘  │
                                                                         │           │            │
                                                                         │  ┌────────▼────────┐  │
                                                                         │  │ Notification     │  │
                                                                         │  │ HTTP Endpoint    │  │
                                                                         │  └────────┬────────┘  │
                                                                         └───────────┼────────────┘
                                                                                     │
                                                                          HTTPS (bidirectional)
                                                                                     │
                                                                         ┌───────────▼────────────┐
                                                                         │         MANJO           │
                                                                         │   QRIS MPM Payment GW    │
                                                                         └──────────────────────────┘
```

**Aktor & batasan tanggung jawab:**

| Aktor | Tanggung jawab | Bukan tanggung jawab |
|---|---|---|
| **Q161 Pro** | Input amount, render `qrContent` jadi gambar QR, mainkan audio sesuai notifikasi | Tidak pernah bicara langsung ke Manjo, tidak menyimpan kredensial Manjo, tidak menentukan status pembayaran sendiri |
| **MQTT Broker** | Routing pesan berdasarkan topic per-merchant | Tidak menyimpan state transaksi |
| **Payment Bridge Service** | Satu-satunya pihak yang bicara ke Manjo; pemilik source of truth transaksi (cache lokal dari status Manjo) | Tidak membuat gambar QR, tidak menyimpan kredensial customer |
| **Manjo** | Generate QRIS, proses pembayaran, kirim notifikasi status | Tidak tahu apa-apa soal MQTT, topic, atau audio Q161 |

---

## 3. Prinsip Desain

1. **Manjo adalah satu-satunya source of truth status pembayaran.** Q161 tidak boleh mengklaim transaksi berhasil hanya karena QR tampil.
2. **Service adalah satu-satunya pihak yang menyimpan kredensial Manjo.** Private key, client secret, dan access token tidak pernah menyentuh device Q161 atau MQTT broker.
3. **Idempotent by design.** Setiap operasi yang bisa terjadi berulang (generate QR retry, notifikasi duplikat) harus aman dijalankan lebih dari sekali tanpa efek samping ganda.
4. **MQTT dan HTTP adalah dua jalur terpisah.** Q161 ↔ Service selalu lewat MQTT; Service ↔ Manjo selalu lewat HTTPS (baik sebagai client maupun sebagai penerima webhook).
5. **Multi-tenant dari awal.** Tidak ada asumsi 1 merchant = 1 transaksi aktif, dan tidak ada topic yang di-hardcode.
6. **Setiap pesan mentah dicatat** (MQTT maupun HTTP) sebelum diproses, untuk kebutuhan tracing kasus "sudah bayar tapi soundbox tidak bunyi".

---

## 4. Komponen Arsitektur

```text
┌───────────────────────────────────────────────────────────────────────┐
│                      PAYMENT BRIDGE SERVICE                           │
│                                                                       │
│  ┌─────────────────┐                                                 │
│  │  MQTT Consumer   │  subscribe topic_+  (wildcard semua merchant)  │
│  └────────┬─────────┘                                                 │
│           │                                                           │
│           ▼                                                           │
│  ┌─────────────────┐     ┌──────────────────┐                        │
│  │ Message Parser   │────▶│ Message Validator │                        │
│  └─────────────────┘     └────────┬─────────┘                        │
│                                   │                                   │
│                                   ▼                                   │
│                         ┌──────────────────┐     ┌─────────────────┐ │
│                         │ Merchant Resolver │────▶│ Transaction     │ │
│                         └──────────────────┘     │ Service         │ │
│                                                   └────────┬────────┘ │
│                                                            │          │
│                                        ┌───────────────────┼────────┐ │
│                                        ▼                            ▼ │
│                              ┌──────────────────┐         ┌────────────────┐
│                              │  Manjo Client     │         │ MQTT Publisher  │
│                              │  ├ Token Manager  │         └────────┬───────┘
│                              │  ├ Signature      │                  │
│                              │  │  Builder        │                  ▼
│                              │  └ Request Builder│         devices.mqtt_topic
│                              └────────┬──────────┘
│                                       │ HTTPS
│  ┌──────────────────────────┐        │
│  │ Notification HTTP        │◄───────┘ (Manjo call-in, arah sebaliknya)
│  │ Endpoint                 │
│  │ POST /webhooks/manjo/    │
│  │      qr-mpm-notify       │
│  └────────┬──────────────────┘
│           │
│           ▼
│  (masuk lagi ke Transaction Service di atas)
│
│  ┌──────────────────────────────────────────────────────┐
│  │ Cross-cutting: Logging / Monitoring / Config Store    │
│  └──────────────────────────────────────────────────────┘
└───────────────────────────────────────────────────────────────────────┘
```

### 4.1 MQTT Consumer

- Subscribe sekali ke topic tetap `qris/request` — **shared** oleh semua device, bukan wildcard per-merchant (firmware publish semua request ke topic yang sama; lihat `docs/eclipse/src/mqtt.c:236`, `inc/def.h:36`).
- `device_id` datang dari payload (lihat Section 7.1), bukan dari nama topic.
- Teruskan raw payload ke Message Parser.
- **Tidak** melakukan validasi bisnis — murni transport layer.

### 4.2 Message Parser

- Decode payload JSON dari Q161 menjadi objek internal `{ merchant_id, amount, transaction_id? }`.
- Tidak melakukan logic pembayaran apa pun (lihat [Section 7](#7-kontrak-internal-q161--service) untuk skema payload final).

### 4.3 Message Validator

- Validasi: topic valid, merchant dikenal & `ACTIVE`, `amount > 0`, format payload sesuai skema.
- Kalau invalid → reject, catat log, **tidak** diteruskan ke Manjo Client.

### 4.4 Device Resolver

- Mapping `device_id` (diambil dari payload request, Section 7.1) → konfigurasi lengkap: kredensial Manjo (dari `merchants`, lewat `device.merchant_id`), `manjo_sub_merchant_id` (dari `tenants`, kalau `device.tenant_id` tidak null), `manjo_store_id`/`manjo_terminal_id` (dari `devices`).
- **Kunci lookup adalah `device_id`** (unik global, sekarang satu-satunya sumbernya — topic inbound tidak lagi mengandung device_id karena sekarang shared di semua device).
- Sumber data: tabel `merchants`, `tenants`, `devices` ([Section 9](#9-data-model)), di-cache in-memory dengan key `device_id`, TTL pendek atau invalidasi manual saat config berubah.
- Kalau `device_id` tidak ditemukan di `devices` → reject, jangan diteruskan ke Manjo Client (device belum ter-provisioning).

### 4.5 Transaction Service

Komponen inti — mengorkestrasi seluruh siklus hidup transaksi:

- **Create**: terima request valid dari Message Validator → buat record `transactions` status `PENDING` → generate `transaction_id`.
- **Generate QR**: panggil Manjo Client → simpan `reference_no`, `qris_payload`, `expire_at` → update status `QR_GENERATED` → serahkan ke MQTT Publisher.
- **Handle Notification**: terima event dari Notification HTTP Endpoint → cek idempotency → cari transaksi via `transaction_id` → map status Manjo → update record → serahkan ke MQTT Publisher.
- Satu-satunya komponen yang boleh mengubah status transaksi di database.

### 4.6 Manjo Client

Lihat detail lengkap di `brainstorm-q161-updated.md` Section 13. Ringkasan sub-komponen:

- **Token Manager** — cache `accessToken` per merchant (atau per partner, tergantung model kredensial), auto-refresh sebelum `expiresIn` (900 detik) habis.
- **Signature Builder** — dua varian: `SHA256withRSA` (access token) dan `HMAC_SHA512` (generate QR, verifikasi notification).
- **Request Builder** — menyusun body sesuai skema Manjo, termasuk konversi `amount` integer → string 2-desimal.

### 4.7 MQTT Publisher

- Terima payload internal dari Transaction Service (hasil generate QR atau hasil update payment).
- Serialize ke format final ([Section 7](#7-kontrak-internal-q161--service)) dan publish ke `"topic_" + device_id` (`internal/qrtopic.BuildDeviceTopic`), dihitung langsung dari `device_id` — **bukan** hasil parsing topic request masuk (topic inbound sekarang shared, tidak per-device lagi).
- Kalau publish gagal (broker down), masuk ke retry queue terpisah — **tidak boleh memblokir** response HTTP ke Manjo di jalur notification.

### 4.8 Notification HTTP Endpoint

- Server HTTP terpisah dari MQTT layer, route: `POST /webhooks/manjo/qr-mpm-notify` (path internal Service — didaftarkan ke Manjo sebagai webhook URL).
- Verifikasi `Authorization: Bearer`, hitung ulang `X-SIGNATURE` (HMAC-SHA512) dan bandingkan.
- Kalau signature tidak valid → balas `401`, **jangan** proses payload.
- Kalau valid → teruskan ke Transaction Service, balas `200 OK` secepat mungkin (idealnya < beberapa detik, sebelum Manjo timeout dari sisi mereka).

### 4.9 Cross-cutting: Logging / Monitoring

- Semua pesan MQTT inbound/outbound → tabel `mqtt_messages`.
- Semua request/response HTTP ke/dari Manjo → tabel `manjo_api_logs`.
- Structured logging per operasi (`merchant_id`, `transaction_id`, `operation`, `error` bila ada).

---

## 5. Sequence Flow — Generate QR

```text
Merchant          Q161 Pro         MQTT Broker      Payment Bridge          Manjo
   │                 │                  │                  │                  │
   │ Input Rp50.000  │                  │                  │                  │
   │────────────────▶│                  │                  │                  │
   │                 │ publish          │                  │                  │
   │                 │ topic_{mid}      │                  │                  │
   │                 │ {amount:50000}   │                  │                  │
   │                 │─────────────────▶│                  │                  │
   │                 │                  │ forward          │                  │
   │                 │                  │─────────────────▶│                  │
   │                 │                  │                  │ parse + validate │
   │                 │                  │                  │ resolve merchant │
   │                 │                  │                  │ create tx        │
   │                 │                  │                  │ status=PENDING   │
   │                 │                  │                  │                  │
   │                 │                  │                  │ [token cached?]  │
   │                 │                  │                  │  no ──▶ POST /access-token/b2b
   │                 │                  │                  │◀──────── accessToken (900s)
   │                 │                  │                  │                  │
   │                 │                  │                  │ POST /qr/qr-mpm-generate
   │                 │                  │                  │ (partnerReferenceNo=tx_id,
   │                 │                  │                  │  amount, dynamicAmount=N)
   │                 │                  │                  │─────────────────▶│
   │                 │                  │                  │◀──────── qrContent, referenceNo
   │                 │                  │                  │                  │
   │                 │                  │                  │ update tx:       │
   │                 │                  │                  │ status=QR_GENERATED
   │                 │                  │                  │ save reference_no, qris_payload
   │                 │                  │ publish          │                  │
   │                 │                  │ topic_{mid}      │                  │
   │                 │                  │ {qris_payload}   │                  │
   │                 │◀─────────────────│◀─────────────────│                  │
   │                 │ render QR image  │                  │                  │
   │◀────────────────│                  │                  │                  │
   │  QR tampil      │                  │                  │                  │
```

**Catatan implementasi:**
- Jika Manjo balas `409` (X-EXTERNAL-ID conflict) → generate `X-EXTERNAL-ID` baru, **cek dulu** apakah transaksi sebelumnya sebenarnya sudah sukses sebelum retry generate baru.
- Jika Manjo balas `401` → refresh token sekali, retry sekali. Kalau masih gagal → tandai `FAILED`, kirim error ke Q161.
- Timeout ke Manjo → ikuti [Retry Mechanism](#13-retry--error-handling).

---

## 6. Sequence Flow — Payment Notification

```text
Customer        Manjo           Payment Bridge        MQTT Broker      Q161 Pro       Soundbox
   │               │                    │                   │              │             │
   │ scan & bayar  │                    │                   │              │             │
   │──────────────▶│                    │                   │              │             │
   │               │ POST /webhooks/manjo/qr-mpm-notify      │              │             │
   │               │ (X-SIGNATURE, latestTransactionStatus)  │              │             │
   │               │───────────────────▶│                   │              │             │
   │               │                    │ verify signature  │              │             │
   │               │                    │  invalid ──▶ 401 (stop)          │             │
   │               │                    │  valid           │              │             │
   │               │                    │ check idempotency │              │             │
   │               │                    │  (originalPartnerReferenceNo)    │             │
   │               │                    │  already processed?             │             │
   │               │                    │   yes ──▶ 200 OK (no reprocess) │             │
   │               │                    │   no             │              │             │
   │               │                    │ find tx by tx_id  │              │             │
   │               │                    │ map status (00/03=PAID, ...)     │             │
   │               │                    │ update tx status  │              │             │
   │               │◀───────────────────│ 200 OK            │              │             │
   │               │                    │                   │              │             │
   │               │                    │ build audio_sequence             │             │
   │               │                    │ publish            │              │             │
   │               │                    │ topic_{mid}        │              │             │
   │               │                    │──────────────────▶│              │             │
   │               │                    │                   │─────────────▶│             │
   │               │                    │                   │              │ play audio  │
   │               │                    │                   │              │────────────▶│
   │               │                    │                   │              │             │ "Pembayaran
   │               │                    │                   │              │             │  berhasil"
```

**Catatan implementasi:**
- Response `200 OK` ke Manjo **tidak menunggu** hasil publish MQTT selesai — pisahkan agar Manjo tidak timeout kalau broker lambat. Publish MQTT boleh async dengan retry queue sendiri.
- Kalau `latestTransactionStatus` bukan status final (`04` Pending) → update status tapi **jangan** publish notifikasi suara ke Q161 (tunggu notifikasi berikutnya).
- Status `00` dan `03` sama-sama dipetakan ke `PAID` sampai ada konfirmasi resmi dari Manjo soal perbedaannya (lihat [Section 20](#20-open-decisions)).

---

## 7. Kontrak Internal (Q161 ↔ Service)

> Kontrak ini murni keputusan internal (kamu kontrol kedua ujungnya). Skema di bawah adalah **baseline yang direkomendasikan**, silakan sesuaikan dengan firmware yang sudah berjalan di Q181 supaya bisa reuse sebanyak mungkin.

> **Kontrak berikut adalah kontrak nyata firmware** (ground truth dari `docs/eclipse/src/mqtt.c`, `inc/def.h`), bukan baseline yang bisa disesuaikan — lihat `docs/superpowers/specs/2026-09-25-mqtt-contract-reconciliation-design.md` untuk detail rekonsiliasinya.

### 7.1 Q161 → Service (Request Generate QR)

**Topic:** `qris/request` — tetap, shared oleh semua device (bukan per-device)

**Payload:** plain text, pipe-delimited: `"{device_id}|{amount_sen}"`

Contoh: `"MT58530503|5000000"` (device `MT58530503`, Rp50.000 = 5.000.000 sen)

| Bagian | Wajib | Keterangan |
|---|---|---|
| `device_id` | Ya | String sebelum `\|` — identitas device, dipakai Device Resolver |
| `amount_sen` | Ya | Integer, Rupiah × 100 (firmware kirim dalam sen) |

`transaction_id` **tidak dikirim device** — digenerate Service saat menerima pesan ini, format: `TRX-{yyyyMMdd}-{sequence}`.

### 7.2 Service → Q161 (Hasil Generate QR)

**Topic:** `"topic_" + device_id` (dihitung dari `device_id`, bukan lookup `devices.mqtt_topic`)

**Payload:** plain text — firmware tidak punya skema JSON untuk balasan, hanya mengenali prefix `QR:` untuk sukses; apa pun selain itu jatuh ke TTS generik (`AppPlayTip`).

Sukses: `"QR:{qris_payload}"` — contoh: `"QR:00020101021226620015ID.CO.MANJO.WWW..."`

Gagal: kalimat manusiawi berbahasa Indonesia, contoh: `"Gagal membuat QR, coba lagi"` (kode error asli tetap dicatat di `mqtt_messages`/`manjo_api_logs` untuk debugging internal, tidak dikirim ke device).

### 7.3 Service → Q161 (Payment Notification)

**Topic:** `topic/{merchant_id}/{tenant_slot}/{device_id}` — `tenant_slot` = `tenant_id` asli, atau literal `_` kalau device tanpa tenant

```json
{
  "type": "PAYMENT_NOTIFICATION",
  "transaction_id": "TRX-20260924-000001",
  "status": "PAID",
  "amount": 50000,
  "audio_sequence": "/ext/awal-qris.mp3+/ext/seratus.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3"
}
```

`audio_sequence` dibangun oleh Transaction Service memakai modul konversi angka→nama-file yang sama seperti di Q181 (prefix `/ext/awal-qris.mp3`, suffix `/ext/akhir-berhasil.mp3`, digabung `+`). Lihat [Section 20](#20-open-decisions) untuk status keputusan format ini.

---

## 8. Kontrak Eksternal (Service ↔ Manjo)

Ringkasan (detail lengkap: `manjo-api-docs.md`):

| Operasi | Method | Path | Auth | Signature |
|---|---|---|---|---|
| Get Access Token | POST | `/v1.0/access-token/b2b` | `X-CLIENT-KEY` | `SHA256withRSA` |
| Generate QRIS MPM | POST | `/v1.0/qr/qr-mpm-generate` | Bearer token | `HMAC_SHA512` |
| Query Payment | POST | `/v1.0/qr/qr-mpm-query` | Bearer token + `X-CLIENT-KEY` | `HMAC_SHA512` |
| Payment Notification *(inbound)* | POST | `/webhooks/manjo/qr-mpm-notify` *(Service side)* | Bearer token (dari Manjo) | `HMAC_SHA512` (diverifikasi Service) |

**Query Payment** (Service Code `51`) bersifat opsional dalam alur utama — dipakai Manjo Client untuk cek status transaksi secara aktif (pull), sebagai pelengkap Payment Notification (push). Dua pemakaian yang relevan untuk Service ini:
1. Sebelum retry generate QR pasca-error `409` ([Section 13.3](#13-retry--error-handling)), untuk memastikan transaksi sebelumnya belum sebenarnya sukses.
2. Reconciliation opsional untuk transaksi `QR_GENERATED` yang mendekati/lewat `expire_at` tanpa notifikasi masuk (lihat [Open Decision #7](#20-open-decisions)).

> **⚠️ Response Query Payment memakai skema `latestTransactionStatus` yang berbeda dari Payment Notification** (`manjo-api-docs.md` Section 5.10 vs Section 4.8) — kode yang sama berarti status berbeda di tiap endpoint (mis. `03` = Paid di Notification, tapi Pending di Query). Transaction Service **wajib** punya dua fungsi mapping status terpisah, jangan reuse satu mapping untuk kedua endpoint.

**Request generate QR (contoh, `dynamicAmount = "N"` untuk Q161):**

```json
{
  "partnerReferenceNo": "TRX-20260924-000001",
  "amount": { "value": "50000.00", "currency": "IDR" },
  "merchantId": "MT82419344",
  "validityPeriod": "3600",
  "additionalInfo": {
    "paymentId": "99",
    "dynamicAmount": "N",
    "prodDesc": "Q161 Payment"
  }
}
```

`validityPeriod` (detik) sebaiknya disamakan dengan asumsi berapa lama QR ditampilkan di layar Q161 sebelum dianggap expired (mis. `3600` = 1 jam, atau lebih pendek kalau device auto-refresh QR).

---

## 9. Data Model

### 9.1 `merchants`

| Kolom | Tipe | Keterangan |
|---|---|---|
| `id` | UUID/PK | |
| `merchant_id` | string, unique | contoh `MT82419344`, dipakai untuk FK dari `tenants`/`devices` & `merchantId`/`X-PARTNER-ID` Manjo |
| `manjo_client_id` | string | `X-CLIENT-KEY` |
| `manjo_private_key_ref` | string | referensi ke secret manager, bukan plaintext |
| `manjo_client_secret_ref` | string | referensi ke secret manager, bukan plaintext |
| `manjo_channel_id` | string(5) | `CHANNEL-ID` |
| `status` | enum(`ACTIVE`,`INACTIVE`) | |
| `created_at` / `updated_at` | timestamp | |

### 9.2 `transactions`

| Kolom | Tipe | Keterangan |
|---|---|---|
| `id` | UUID/PK | |
| `transaction_id` | string, unique | digenerate Service, dikirim sebagai `partnerReferenceNo` |
| `reference_no` | string, nullable | `referenceNo` dari response Manjo |
| `merchant_id` | FK → `merchants.merchant_id` | |
| `device_id` | FK → `devices.device_id`, NOT NULL | menentukan topic MQTT tujuan publish — **bukan** dari echo Manjo |
| `amount` | integer | Rupiah, integer |
| `status` | enum | `PENDING`, `QR_GENERATED`, `PAID`, `FAILED`, `EXPIRED`, `CANCELLED`, `REFUNDED` |
| `manjo_status_code` | string(2), nullable | `latestTransactionStatus` mentah dari Manjo, untuk audit |
| `qris_payload` | text, nullable | `qrContent` |
| `external_id` | string, nullable | `X-EXTERNAL-ID` yang dipakai saat generate QR |
| `expire_at` | timestamp, nullable | dari `additionalInfo.expireDate` |
| `created_at` | timestamp | |
| `paid_at` | timestamp, nullable | |
| `updated_at` | timestamp | |

Index yang direkomendasikan: `UNIQUE(transaction_id)`, `INDEX(merchant_id, status)`, `INDEX(device_id)`, `INDEX(reference_no)`.

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

### 9.5 `mqtt_messages`

| Kolom | Tipe | Keterangan |
|---|---|---|
| `id` | UUID/PK | |
| `topic` | string | |
| `payload` | text/JSON | |
| `direction` | enum(`INBOUND`,`OUTBOUND`) | |
| `status` | enum(`RECEIVED`,`PROCESSED`,`FAILED`) | |
| `transaction_id` | string, nullable, FK | untuk join cepat ke `transactions` |
| `created_at` / `processed_at` | timestamp | |
| `error_message` | text, nullable | |

### 9.6 `manjo_api_logs`

| Kolom | Tipe | Keterangan |
|---|---|---|
| `id` | UUID/PK | |
| `direction` | enum(`OUTBOUND`,`INBOUND`) | `OUTBOUND` = Service memanggil Manjo; `INBOUND` = Manjo memanggil webhook Service |
| `operation` | enum(`ACCESS_TOKEN`,`GENERATE_QR`,`QUERY_PAYMENT`,`PAYMENT_NOTIFY`) | |
| `endpoint` | string | |
| `http_status` | integer, nullable | |
| `request_body` | JSON | header sensitif (signature, token) di-mask |
| `response_body` | JSON, nullable | |
| `transaction_id` | string, nullable, FK | |
| `duration_ms` | integer, nullable | |
| `created_at` | timestamp | |

---

## 10. State Machine Transaksi

```text
                 ┌─────────┐
                 │ PENDING │  (dibuat saat request generate QR masuk)
                 └────┬────┘
                      │ Manjo balas qrContent sukses
                      ▼
              ┌───────────────┐
              │ QR_GENERATED  │
              └───┬───────┬───┘
                  │       │
   notif PAID/00/03       │ tidak dibayar sampai expire_at
                  │       ▼
                  │   ┌─────────┐
                  │   │ EXPIRED │
                  │   └─────────┘
                  ▼
              ┌───────┐
              │ PAID  │ ── (final, tidak boleh berubah lagi via notifikasi biasa)
              └───┬───┘
                  │ notifikasi refund terpisah (di luar scope notify sukses)
                  ▼
              ┌──────────┐
              │ REFUNDED │
              └──────────┘

  Dari PENDING atau QR_GENERATED, notifikasi status 01 (Failed) atau 06 (Cancelled)
  langsung membawa transaksi ke FAILED / CANCELLED (final).
```

**Aturan transisi:**
- Status final (`PAID`, `FAILED`, `EXPIRED`, `CANCELLED`, `REFUNDED`) **tidak boleh** ditimpa oleh notifikasi duplikat dengan status berbeda — kalau ini terjadi, log sebagai anomali dan jangan auto-update (perlu investigasi manual).
- Transisi ke `EXPIRED` dilakukan oleh job terjadwal (bukan dari notifikasi Manjo), berdasarkan `expire_at` yang lewat dan status masih `QR_GENERATED`.

---

## 11. Idempotency Strategy

| Level | Mekanisme | Detail |
|---|---|---|
| **Generate QR (outbound)** | `X-EXTERNAL-ID` unik per hari | Manjo tolak dengan `409` kalau dipakai ulang. Service generate ID baru tiap percobaan **baru**, tapi tetap pakai `transaction_id`/`partnerReferenceNo` yang sama untuk request yang merepresentasikan transaksi yang sama. |
| **Payment Notification (inbound)** | Cek `originalPartnerReferenceNo` terhadap status transaksi tersimpan | Kalau status sudah final dan notifikasi baru membawa status yang sama → balas `200 OK` tanpa reprocess, **tanpa** publish ulang ke MQTT. |
| **MQTT publish (outbound ke Q161)** | `transaction_id` sebagai correlation key di setiap payload | Q161 (opsional) bisa dedupe di sisi device berdasarkan `transaction_id` yang sudah diproses, sebagai lapisan pertahanan tambahan. |

---

## 12. Concurrency Model

- Transaction Service **stateless** — semua state ada di database, sehingga service bisa dijalankan multi-instance (horizontal scaling) tanpa sticky session.
- Tidak ada asumsi 1 merchant = 1 transaksi aktif. Satu merchant boleh punya banyak transaksi `PENDING`/`QR_GENERATED` bersamaan (misal beberapa Q161 unit di satu tempat, atau retry).
- Row-level locking / `SELECT ... FOR UPDATE` (atau setara di ORM/DB pilihan) saat update status transaksi, untuk menghindari race condition ketika dua notifikasi untuk `transaction_id` yang sama masuk hampir bersamaan.
- Access Token Manager perlu thread-safe/lock saat refresh token bersamaan dari beberapa request concurrent untuk merchant yang sama, supaya tidak request token berkali-kali secara paralel.

---

## 13. Retry & Error Handling

### 13.1 Retry Generate QR

```text
Attempt 1 → Failed (timeout/5xx)
    ↓
Attempt 2 (X-EXTERNAL-ID baru, transaction_id sama) → Failed
    ↓
Attempt 3 → Failed
    ↓
Mark transaction FAILED, kirim QR_RESULT status=FAILED ke Q161
```

- Retry **hanya** untuk error transient (timeout, 5xx, network error).
- Error `400`/`404` (invalid field, invalid merchant) → **tidak** di-retry, langsung `FAILED` + alert (kemungkinan bug config).
- Error `401` → refresh token, retry **sekali** dengan token baru.
- Error `409` → cek status transaksi dulu (kemungkinan request sebelumnya sukses tapi response hilang) sebelum retry dengan `X-EXTERNAL-ID` baru. Gunakan **Query Payment** (`/v1.0/qr/qr-mpm-query`) untuk memastikan status asli di sisi Manjo — ingat skema status Query berbeda dari Notification (lihat [Section 8](#8-kontrak-eksternal-service--manjo)).

### 13.2 Retry MQTT Publish

- Kalau publish ke broker gagal → masuk retry queue in-memory atau job queue terpisah (mis. exponential backoff), supaya tidak memblokir response HTTP ke Manjo di jalur notification.

### 13.3 Error Code Mapping (Manjo)

Lihat tabel lengkap di `brainstorm-q161-updated.md` Section 21.

---

## 14. Security

1. **Kredensial Manjo** (private key RSA, client secret) disimpan di secret manager (mis. AWS Secrets Manager / HashiCorp Vault / environment variable terenkripsi) — **bukan** plaintext di database atau source code.
2. **Access token** hanya di-cache in-memory, tidak pernah di-log penuh, tidak disimpan ke database.
3. **Webhook Manjo → Service** wajib HTTPS, dan setiap request wajib diverifikasi `X-SIGNATURE` sebelum payload dipercaya.
4. **MQTT Broker**: gunakan TLS + autentikasi per-device (username/password atau client cert per Q161), supaya device lain tidak bisa publish/subscribe ke topic merchant yang bukan miliknya.
5. **Endpoint webhook** sebaiknya dibatasi (IP allowlist dari Manjo, kalau Manjo menyediakan daftar IP resmi) sebagai lapisan tambahan di luar signature verification.
6. Log request/response Manjo (`manjo_api_logs`) **wajib mask** field sensitif (`X-SIGNATURE`, `accessToken`, private key) — jangan pernah log nilai penuh.

---

## 15. Observability

- **Structured logging** per event penting: `CREATE_TRANSACTION`, `GENERATE_QR_SUCCESS/FAILED`, `NOTIFICATION_RECEIVED`, `NOTIFICATION_DUPLICATE`, `MQTT_PUBLISH_SUCCESS/FAILED` — masing-masing menyertakan `merchant_id` dan `transaction_id`.
- **Metrics** yang berguna untuk dashboard: rate generate-QR sukses/gagal, latency ke Manjo, jumlah notifikasi duplikat, jumlah transaksi `EXPIRED` per hari, MQTT publish failure rate.
- **Alerting**: token refresh gagal berturut-turut, signature verification gagal (indikasi kemungkinan serangan atau misconfig), MQTT broker disconnect.

---

## 16. Deployment Topology

```text
                         Internet
                             │
                    ┌────────┴────────┐
                    │  Reverse Proxy /  │  ← HTTPS termination,
                    │  Load Balancer    │    perlu domain publik
                    └────────┬────────┘    untuk didaftarkan sbg
                             │              webhook URL ke Manjo
                    ┌────────▼────────┐
                    │ Payment Bridge   │  (bisa multi-instance,
                    │ Service          │   stateless)
                    └───┬──────────┬───┘
                        │          │
              ┌─────────▼──┐   ┌───▼──────────┐
              │ MQTT Broker │   │  Database     │
              │ (TLS)       │   │  (Postgres/   │
              └─────────────┘   │   MySQL, dst) │
                                └───────────────┘
```

**Catatan penting:** endpoint Notification HTTP **wajib reachable dari internet** (Manjo perlu memanggilnya), sehingga service ini tidak bisa full-private-network — minimal komponen yang menerima webhook harus punya jalur publik + HTTPS valid (bukan self-signed).

---

## 17. Rekomendasi Tech Stack

**Bahasa: Go.** Cocok untuk service ini karena sifatnya I/O-bound dengan banyak concurrent connection (ribuan Q161 device via MQTT + webhook HTTP dari Manjo) — goroutine + channel membuat MQTT Consumer, Notification HTTP Endpoint, dan Access Token Manager (yang butuh lock saat refresh concurrent) mudah diimplementasikan tanpa overhead thread OS, plus binary hasil build ringan untuk di-deploy.

| Layer | Pilihan | Alasan |
|---|---|---|
| Bahasa | **Go** (1.22+) | Concurrency primitives (goroutine, channel, `sync.Mutex`/`sync.RWMutex`) pas untuk MQTT Consumer, Token Manager, dan retry queue; static binary memudahkan deployment |
| MQTT client | `eclipse/paho.mqtt.golang` | Library MQTT paling matang di ekosistem Go, dukungan QoS, auto-reconnect, TLS |
| HTTP server (webhook) | `net/http` + router ringan (`chi` atau `gin`) | Cukup untuk satu endpoint kritikal (`/webhooks/manjo/qr-mpm-notify`); `chi` lebih idiomatic-stdlib kalau mau minim dependency |
| Database driver/ORM | `pgx` (driver) + `sqlc` atau `gorm` | `pgx` + `sqlc` untuk query eksplisit dan performa; `gorm` kalau prioritas kecepatan development |
| Database | PostgreSQL | Dukungan `SELECT ... FOR UPDATE` solid untuk concurrency control di [Section 12](#12-concurrency-model) |
| Signature (RSA & HMAC) | `crypto/rsa`, `crypto/sha256`, `crypto/hmac` (stdlib) | Semua primitif yang dibutuhkan (`SHA256withRSA`, `HMAC_SHA512`) sudah ada di standard library, tidak perlu dependency tambahan |
| Secret storage | Vault / cloud secret manager (client resmi Go SDK tersedia untuk AWS/GCP/Vault) | Untuk private key & client secret |
| Queue retry (opsional) | `asynq` (Redis-backed) atau in-process retry dengan `time.AfterFunc`/worker goroutine | Untuk retry MQTT publish & retry Manjo API secara terkelola |
| Config | `viper` atau `envconfig` | Load environment variables ([Section 19](#19-konfigurasi--environment-variables)) dengan validasi |
| Testing | stdlib `testing` + `testify` | Assertion & mocking untuk unit test Signature Builder, State Machine, dsb |

---

## 18. Struktur Proyek (Contoh)

Mengikuti layout Go yang umum dipakai ([Standard Go Project Layout](https://github.com/golang-standards/project-layout)): `cmd/` untuk entrypoint binary, `internal/` untuk kode yang tidak boleh diimpor project lain (semua business logic ada di sini), `pkg/` kalau ada bagian yang memang perlu reusable di luar service ini (misal audio-sequence builder yang mau dipakai bareng Q181).

```text
payment-bridge-service/
├── cmd/
│   └── server/
│       └── main.go                  # entrypoint: init config, DB, MQTT, HTTP server, wiring semua komponen
├── internal/
│   ├── mqtt/
│   │   ├── consumer.go
│   │   └── publisher.go
│   ├── httpapi/
│   │   └── notification_webhook.go  # handler POST /webhooks/manjo/qr-mpm-notify
│   ├── manjoclient/
│   │   ├── token_manager.go         # cache + auto-refresh access token, thread-safe (sync.RWMutex)
│   │   ├── signature.go             # SHA256withRSA & HMAC_SHA512 builder
│   │   └── client.go                # HTTP client ke access-token/b2b, qr-mpm-generate & qr-mpm-query
│   ├── transaction/
│   │   ├── service.go               # orkestrasi create/generate-qr/handle-notification
│   │   ├── state_machine.go         # validasi transisi status ( Section 10 )
│   │   └── audiosequence/
│   │       └── builder.go           # reuse logic dari Q181: amount → []string nama file mp3
│   ├── merchant/
│   │   └── resolver.go              # mapping topic/merchant_id → config, dengan cache in-memory
│   ├── validation/
│   │   └── validator.go
│   ├── repository/                  # data access layer (pakai pgx/sqlc atau gorm)
│   │   ├── merchant_repo.go
│   │   ├── transaction_repo.go
│   │   ├── mqtt_message_repo.go
│   │   └── manjo_log_repo.go
│   ├── retryqueue/
│   │   └── queue.go                 # retry MQTT publish / retry Manjo API
│   └── config/
│       └── config.go                # load & validasi environment variables
├── migrations/                      # SQL migration files (mis. pakai golang-migrate)
├── pkg/                             # (opsional) bagian yang mau di-share ke luar service, mis. audiosequence
├── test/
│   └── ...                          # unit test per package, integration test
├── go.mod
├── go.sum
└── architecture.md                  # dokumen ini
```

---

## 19. Konfigurasi / Environment Variables

| Variable | Keterangan |
|---|---|
| `MQTT_BROKER_URL` | URL broker (dengan TLS) |
| `MQTT_USERNAME` / `MQTT_PASSWORD` | Kredensial Service ke broker |
| `DATABASE_URL` | Connection string database |
| `MANJO_BASE_URL` | Base URL API Manjo (production/sandbox) |
| `SECRET_MANAGER_*` | Konfigurasi akses ke secret manager tempat private key & client secret disimpan |
| `WEBHOOK_PUBLIC_URL` | URL publik yang didaftarkan ke Manjo sebagai notification endpoint |
| `LOG_LEVEL` | `info` / `debug` / dsb |

Kredensial per-merchant (`manjo_client_id`, dll.) **tidak** lewat environment variable — itu data per-tenant yang hidup di tabel `merchants` + secret manager, karena satu Service instance melayani banyak merchant sekaligus.

---

## 20. Open Decisions

Item yang masih perlu diputuskan/dikonfirmasi sebelum implementasi final (carry-over dari `brainstorm-q161-updated.md` Section 27.2):

| # | Keputusan | Pemilik | Dampak kalau belum diputuskan |
|---|---|---|---|
| 1 | Format final `audio_sequence` — string `+`-separated (pola Q181) vs array JSON | Kamu (firmware Q161) | Menentukan struktur payload di [Section 7.3](#7-kontrak-internal-q161--service) |
| 2 | Siapa generate `transaction_id` — Service (rekomendasi) atau Q161 | Kamu | Kalau Q161 yang generate, perlu skema penomoran yang dijamin unique lintas device |
| 3 | Kredensial Manjo per-merchant vs per-partner (satu kredensial untuk semua merchant) | Kamu / kesepakatan bisnis dengan Manjo | Menentukan struktur tabel `merchants` — apakah kolom kredensial per-baris atau di tabel config global terpisah |
| 4 | Perbedaan status `00` (Success) vs `03` (Paid) dari Manjo | Konfirmasi ke tim Manjo | Sementara di-treat sama (`PAID`) — risiko kalau ternyata beda arti |
| 5 | `validityPeriod` QR (detik) — berapa lama QR valid sebelum `EXPIRED` | Kamu | Mempengaruhi UX Q161 (kapan device re-generate QR otomatis) |
| 6 | QoS & retain MQTT untuk tiap jenis pesan | Kamu | Mempengaruhi reliability delivery, terutama utk notifikasi pembayaran yang tidak boleh hilang |
| 7 | Apakah reconciliation job (Query Payment berkala untuk transaksi `QR_GENERATED` mendekati/lewat `expire_at`) masuk scope v1, atau auto-expire murni berdasarkan `expire_at` tanpa cross-check ke Manjo | Kamu | Kalau tidak diimplementasikan, transaksi yang notifikasinya hilang akan salah ditandai `EXPIRED` walau sebenarnya sudah dibayar |

---

## 21. Referensi

- `brainstorm-q161-updated.md` — eksplorasi konsep & rasionale di balik keputusan arsitektur ini.
- `manjo-api-docs.md` — dokumentasi lengkap API Manjo (Access Token B2B, Generate QRIS MPM, Query Payment, Payment Notification) beserta diagram sequence.