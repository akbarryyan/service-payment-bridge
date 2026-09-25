# Q161 Pro - Manjo MQTT Payment Bridge

> **Update:** Dokumen ini telah diperbarui dengan detail konkret dari dokumentasi resmi **Manjo QRIS MPM API** (lihat `manjo-api-docs.md`). Bagian yang diperbarui ditandai dengan blok `> **[Update dari API Manjo]**`. Ringkasan lengkap perubahan ada di [Section 27](#27-ringkasan-update-berdasarkan-api-manjo).

## Daftar Isi

1. [Overview](#1-overview)
2. [High-Level Architecture](#2-high-level-architecture)
3. [MQTT Topic](#3-mqtt-topic)
4. [Fase 1 - Generate QRIS Dynamic](#4-fase-1---generate-qris-dynamic)
5. [Fase 2 - Payment](#5-fase-2---payment)
6. [Payment Success Flow](#6-payment-success-flow)
7. [Dua Arah Komunikasi](#7-dua-arah-komunikasi)
8. [Payment Bridge Service](#8-payment-bridge-service)
9. [MQTT Consumer](#9-mqtt-consumer)
10. [Message Parser](#10-message-parser)
11. [Message Validator](#11-message-validator)
12. [Merchant Resolver](#12-merchant-resolver)
13. [Manjo Client](#13-manjo-client)
14. [MQTT Publisher](#14-mqtt-publisher)
15. [Transaction Management](#15-transaction-management)
16. [Idempotency](#16-idempotency)
17. [Concurrency](#17-concurrency)
18. [Database](#18-database)
19. [MQTT Message Logging](#19-mqtt-message-logging)
20. [Retry Mechanism](#20-retry-mechanism)
21. [Error Handling](#21-error-handling)
22. [Source of Truth](#22-source-of-truth)
23. [Full End-to-End Flow](#23-full-end-to-end-flow)
24. [Arsitektur Konseptual](#24-arsitektur-konseptual)
25. [Hal yang Harus Dipastikan Sebelum Coding](#25-hal-yang-harus-dipastikan-sebelum-coding)
26. [Kesimpulan Arsitektur](#26-kesimpulan-arsitektur)
27. [Ringkasan Update Berdasarkan API Manjo](#27-ringkasan-update-berdasarkan-api-manjo)

---

## 1. Overview

Sistem ini merupakan middleware/bridge yang menghubungkan **Q161 Pro Soundbox** dengan **Payment Gateway Manjo** melalui **MQTT Broker**.

Setiap Q161 Pro milik merchant memiliki MQTT topic sendiri dengan format:

```text
topic_{merchant_id}
```

Contoh:

```text
topic_MT82419344
```

> **[Update multi-tenant]** Skema di atas diperluas untuk mendukung sub-merchant/tenant dan multi-device per tenant — satu merchant bisa punya beberapa tenant (opsional, tidak semua merchant punya), dan satu tenant bisa punya lebih dari satu device Q161 Pro. Format topic final: `topic/{merchant_id}/{tenant_slot}/{device_id}` (hierarkis `/`-delimited, selalu 4 segmen; `tenant_slot` = `_` kalau device tanpa tenant). Detail lengkap: `architecture.md` Section 4.4, 7.1-7.3, 9.3-9.4, dan `schema.md` Section 5-6 (`tenants`, `devices`).

Service yang dikembangkan berfungsi sebagai penghubung dua arah:

```text
Q161 Pro
    ↕
MQTT Broker
    ↕
Payment Bridge Service
    ↕
Manjo Payment Gateway
```

Service harus dapat menangani dua fase utama:

1. **Generate QRIS Dynamic**
2. **Payment Notification / Payment Success**

> **[Update dari API Manjo]** Manjo menyebut API generate QR sebagai **Generate QRIS MPM (Merchant Presented Mode)** — Service Code `47`. "Dynamic" pada konteks Q161 berkaitan dengan `additionalInfo.dynamicAmount` di request body Manjo: `"N"` = nominal ditentukan Merchant (sesuai kebutuhan Q161, karena merchant input amount duluan di device), `"Y"` = nominal ditentukan customer saat scan. Untuk Q161, gunakan `dynamicAmount = "N"` karena amount sudah diinput merchant sebelum QR digenerate.

---

## 2. High-Level Architecture

```text
                         ┌─────────────────┐
                         │      MANJO      │
                         │ Payment Gateway │
                         └────────┬────────┘
                                  │
                         API Request/Response
                                  │
                                  ▼
                    ┌─────────────────────────┐
                    │     PAYMENT BRIDGE      │
                    │        SERVICE          │
                    │                         │
                    │ - MQTT Consumer         │
                    │ - Message Processor     │
                    │ - Transaction Service   │
                    │ - Merchant Resolver     │
                    │ - Manjo Client          │
                    │ - MQTT Publisher        │
                    └───────────┬─────────────┘
                                │
                               MQTT
                                │
                                ▼
                       ┌────────────────┐
                       │  MQTT BROKER   │
                       └───────┬────────┘
                               │
                ┌──────────────┼──────────────┐
                │              │              │
                ▼              ▼              ▼
          topic_MT001    topic_MT002    topic_MT003
                │              │              │
                ▼              ▼              ▼
             Q161 #1        Q161 #2        Q161 #3
                │              │              │
                ▼              ▼              ▼
           Merchant 1      Merchant 2      Merchant 3
```

---

## 3. MQTT Topic

Setiap soundbox memiliki topic berdasarkan `merchant_id`.

> **Update pasca-rekonsiliasi kontrak (2026-09-25):** `MQTT_MERCHANT_ID` yang di-compile ke firmware diperlakukan sebagai `device_id` di backend (satu binary firmware = satu device fisik). Topic outbound di bawah ini tetap `topic_{device_id}` persis seperti yang sudah didokumentasikan di sini — bagian ini sudah akurat. Yang berubah adalah topic **inbound** (request dari device ke Service): sekarang topic tetap `qris/request`, shared semua device, bukan per-merchant — lihat `docs/superpowers/specs/2026-09-25-mqtt-contract-reconciliation-design.md`.

**Format:**

```text
topic_{merchant_id}
```

**Contoh:**

```text
topic_MT82419344
topic_MT82419345
topic_MT82419346
```

**Mapping:**

```text
topic_MT82419344
        ↓
merchant_id = MT82419344
        ↓
Merchant 1
```

Service harus mampu menangani banyak merchant dan banyak Q161 Pro secara bersamaan.

> **Catatan:** Topic tidak boleh di-hardcode hanya untuk satu merchant.

> **[Update dari API Manjo]** `merchant_id` pada topic MQTT (contoh: `MT82419344`) selaras dengan field `merchantId`/`X-PARTNER-ID` pada API Manjo (contoh dokumentasi: `MT60169117`). Merchant Resolver harus memetakan `merchant_id` di topic ke `merchantId` yang dipakai di request body Manjo, dan ke `X-PARTNER-ID` yang dipakai di header.

---

## 4. Fase 1 - Generate QRIS Dynamic

### 4.1 Flow

Merchant memasukkan nominal pembayaran melalui Q161 Pro.

Contoh:

```text
Amount = Rp50.000
```

Flow:

```text
Merchant
   │
   │ Input Rp50.000
   ▼
┌──────────────┐
│   Q161 Pro   │
└──────┬───────┘
       │
       │ MQTT Publish
       │ topic_MT82419344
       ▼
┌──────────────┐
│ MQTT Broker  │
└──────┬───────┘
       │
       ▼
┌─────────────────────────┐
│ Payment Bridge Service  │
│                         │
│ - Read Topic            │
│ - Identify Merchant     │
│ - Parse Message         │
│ - Validate Amount       │
└────────────┬────────────┘
             │
             │ API Request
             ▼
┌─────────────────────────┐
│         MANJO           │
│                         │
│ Generate Dynamic QRIS   │
└────────────┬────────────┘
             │
             │ QRIS Payload/String
             ▼
┌─────────────────────────┐
│ Payment Bridge Service  │
└────────────┬────────────┘
             │
             │ MQTT Publish
             ▼
┌──────────────┐
│ MQTT Broker  │
└──────┬───────┘
       │
       │ topic_MT82419344
       ▼
┌──────────────┐
│   Q161 Pro   │
│              │
│ QR Payload   │
│      ↓       │
│ QR Image     │
└──────┬───────┘
       │
       ▼
    QRIS tampil
```

> **[Update dari API Manjo]** "API Request" ke Manjo di atas sebenarnya **dua panggilan berurutan**, bukan satu:
> 1. `POST /v1.0/access-token/b2b` (kalau access token belum ada / sudah expired) → dapat `accessToken`.
> 2. `POST /v1.0/qr/qr-mpm-generate` (pakai `accessToken` sebagai Bearer) → dapat `qrContent`.
>
> Detail lengkap ada di [Section 13 Manjo Client](#13-manjo-client).

### 4.2 Peran Service

Service **tidak perlu** membuat gambar QRIS.

Service hanya bertanggung jawab untuk:

1. Menerima request dari Q161 melalui MQTT.
2. Mengidentifikasi merchant berdasarkan topic.
3. Membaca nominal transaksi.
4. Membuat/mengirim request ke Manjo.
5. Menerima QRIS payload/string dari Manjo.
6. Mengirim kembali QRIS payload ke Q161 melalui MQTT.

Q161 Pro bertanggung jawab untuk mengubah QRIS payload/string menjadi gambar QR dan menampilkannya.

Contoh payload QRIS:

```text
00020101021226670016COM.NOBUBANK.WWW...
```

Payload tersebut dikirim kembali ke Q161 dan diproses oleh device menjadi QR Code.

> **[Update dari API Manjo]** Field ini pada response Manjo bernama `qrContent` (bukan istilah generik "QRIS payload"). Contoh nyata dari dokumentasi:
> ```text
> 00020101021226620015ID.CO.MANJO.WWW...6304FE20
> ```
> Response Manjo juga menyertakan `referenceNo` (ID transaksi versi Manjo) dan `additionalInfo.expireDate`/`expiryDuration` — keduanya sebaiknya disimpan di tabel `transactions` (lihat [Section 18](#18-database)) karena berguna untuk menentukan kapan status `PENDING` harus di-expire otomatis.

---

## 5. Fase 2 - Payment

Setelah QRIS tampil, customer melakukan scan menggunakan aplikasi pembayaran.

Flow:

```text
Customer
   │
   │ Scan QRIS
   ▼
Payment App
   │
   │ Payment
   ▼
QRIS / Payment Ecosystem
   │
   ▼
MANJO
   │
   │ Payment Success
   ▼
Payment Bridge Service
   │
   │ MQTT Publish
   ▼
MQTT Broker
   │
   │ topic_MT82419344
   ▼
Q161 Pro
   │
   ▼
Soundbox
   │
   ▼
"Pembayaran berhasil..."
```

> **[Update dari API Manjo]** "MANJO → Payment Success" di atas adalah Manjo yang **memanggil endpoint milik Service** (bukan Service yang polling Manjo). Ini adalah **Payment Notification** (Service Code `52`), `POST` ke `/v1.0/qr/qr-mpm-notify` **versi Merchant** (path ini harus diimplementasikan di sisi Payment Bridge Service sebagai endpoint HTTP yang bisa diakses publik oleh Manjo). Lihat [Section 6](#6-payment-success-flow) dan [Section 25.3](#253-manjo--service).

---

## 6. Payment Success Flow

Ketika Manjo mendapatkan status pembayaran berhasil, service menerima event payment.

Contoh konsep event:

```text
Payment Success
       ↓
      Manjo
       ↓
Payment Bridge Service
       ↓
MQTT Broker
       ↓
topic_MT82419344
       ↓
Q161 Pro
       ↓
Soundbox
       ↓
"Pembayaran berhasil..."
```

Service harus memastikan payment event dikirim ke topic merchant yang sesuai.

Contoh:

```text
Transaction:
merchant_id = MT82419344
amount      = 50000
status      = PAID
```

Maka event harus dikirim ke:

```text
topic_MT82419344
```

> **[Update dari API Manjo]** Event nyata dari Manjo (contoh dari dokumentasi API, disederhanakan):
> ```json
> {
>   "originalReferenceNo": "A0000021383",
>   "originalPartnerReferenceNo": "TRX-001",
>   "latestTransactionStatus": "00",
>   "transactionStatusDesc": "SUCCESS",
>   "amount": { "value": "50000.00", "currency": "IDR" },
>   "additionalInfo": {
>     "merchantCode": "MT82419344",
>     "amountTrx": "50000.00",
>     "trxTime": "2026-09-24T20:30:00+07:00",
>     "rrn": "1l62d1b01796"
>   }
> }
> ```
> Service **wajib mencocokkan** `originalPartnerReferenceNo` dengan `transaction_id` internal (yang dikirim sebagai `partnerReferenceNo` saat generate QR) untuk menemukan `merchant_id` tujuan — bukan mengandalkan field merchant langsung dari notif, karena `additionalInfo.merchantCode` bersifat opsional (`O`) di skema Manjo. Lihat [Section 15](#15-transaction-management).
>
> Perhatikan juga: kode status Manjo `"00"` = Success — **beda numbering** dari status internal `PAID` yang dipakai di dokumen ini. Mapping status ada di [Section 22](#22-source-of-truth).

---

## 7. Dua Arah Komunikasi

Service memiliki komunikasi dua arah.

### Direction A - Q161 → Manjo

Digunakan untuk membuat Dynamic QRIS.

```text
Q161
  ↓
MQTT
  ↓
Payment Bridge
  ↓
Manjo
  ↓
QRIS Payload
  ↓
Payment Bridge
  ↓
MQTT
  ↓
Q161
```

### Direction B - Manjo → Q161

Digunakan untuk mengirim status pembayaran.

```text
Manjo
  ↓
Payment Bridge
  ↓
MQTT
  ↓
Q161
  ↓
Soundbox Notification
```

> **[Update dari API Manjo]** Direction A sebenarnya lebih tepat digambarkan sebagai **Payment Bridge → Manjo** (bukan Q161 langsung ke Manjo — Q161 tidak pernah bicara langsung ke Manjo, selalu lewat Service). Direction B juga demikian: Manjo memanggil **HTTP endpoint Service** (bukan MQTT), baru Service yang menerjemahkannya ke MQTT publish. Kedua arah komunikasi Q161↔Manjo selalu melalui Service sebagai satu-satunya penghubung — ini penting supaya kredensial Manjo (private key, client secret) tidak pernah tersimpan/terekspos di device Q161.

---

## 8. Payment Bridge Service

Service sebaiknya diposisikan sebagai **Transaction Bridge**, bukan sekadar MQTT listener.

Komponen utama:

```text
Payment Bridge Service
│
├── MQTT Consumer
│
├── Message Parser
│
├── Message Validator
│
├── Merchant Resolver
│
├── Transaction Service
│
├── Manjo Client
│
├── MQTT Publisher
│
└── Logging / Monitoring
```

> **[Update dari API Manjo]** Perlu ditambahkan satu komponen baru: **Notification HTTP Endpoint** — server HTTP (bukan MQTT) yang menerima `POST /v1.0/qr/qr-mpm-notify` dari Manjo, memverifikasi header (`Authorization`, `X-SIGNATURE`, `X-EXTERNAL-ID`), lalu meneruskan ke Transaction Service. Ini komponen terpisah dari MQTT Consumer karena protokolnya HTTP, bukan MQTT. Juga perlu **Access Token Manager** — modul kecil di dalam Manjo Client yang menyimpan `accessToken` di memory/cache dan me-refresh sebelum expired (900 detik). Lihat [Section 13](#13-manjo-client).

---

## 9. MQTT Consumer

Tugas MQTT Consumer:

1. Connect ke MQTT Broker.
2. Subscribe ke topic yang diperlukan.
3. Menerima message dari Q161.
4. Mengambil merchant ID dari topic.
5. Meneruskan message ke Message Processor.

Contoh:

```text
MQTT Message

Topic:
topic_MT82419344

Payload:
{ ... }
```

Consumer membaca:

```text
merchant_id = MT82419344
```

Kemudian meneruskannya ke processor.

---

## 10. Message Parser

Message Parser bertanggung jawab membaca format payload dari Q161.

Contoh konsep:

```json
{
  "amount": 50000
}
```

Parser mengubah payload menjadi object internal:

```text
merchant_id = MT82419344
amount      = 50000
```

> **Catatan:** Parser tidak boleh melakukan logic payment secara langsung.

---

## 11. Message Validator

Sebelum request dikirim ke Manjo, data harus divalidasi.

Minimal:

- Topic valid
- Merchant valid
- Amount tersedia
- Amount > 0
- Payload valid
- Transaction/reference tersedia jika diperlukan

Contoh:

```text
topic:
topic_MT82419344

amount:
50000

merchant:
MT82419344

status:
VALID
```

Jika invalid:

```text
Q161
 ↓
MQTT
 ↓
Service
 ↓
Validation Failed
```

> **Catatan:** Service tidak boleh meneruskan request invalid ke Manjo.

> **[Update dari API Manjo]** Tambahan validasi wajib sebelum request ke Manjo (mengikuti requirement API):
> - `amount` harus diformat dua digit desimal sebagai string, misal `"50000.00"` (Manjo pakai `amount.value` bertipe `String(16,2)`, bukan angka biasa).
> - `currency` selalu `"IDR"`.
> - `partnerReferenceNo` (transaction_id internal) harus unique.
> - `X-EXTERNAL-ID` yang akan dipakai di header harus unique **per hari** — kalau device retry generate QR untuk transaksi yang sama di hari yang sama dengan ID yang sama, Manjo akan balas `409 Conflict`.

---

## 12. Merchant Resolver

Merchant Resolver bertanggung jawab melakukan mapping:

```text
MQTT Topic
    ↓
Merchant ID
    ↓
Merchant Configuration
```

Contoh:

```text
topic_MT82419344
        ↓
MT82419344
        ↓
Merchant Configuration
```

Data merchant minimal dapat berisi:

- `merchant_id`
- `mqtt_topic`
- `manjo_configuration`
- `status`

Contoh:

```text
merchant_id:
MT82419344

mqtt_topic:
topic_MT82419344

status:
ACTIVE
```

> **[Update dari API Manjo]** `manjo_configuration` sekarang bisa dirinci berdasarkan credential yang diminta API Manjo. Per merchant (atau per akun partner, tergantung apakah kredensial di-share semua merchant atau per-merchant), minimal perlu disimpan:
>
> ```text
> manjo_configuration:
>   client_id           # X-CLIENT-KEY, dipakai saat request access token
>   private_key_ref      # referensi ke RSA private key (SHA256withRSA), JANGAN simpan plaintext di DB
>   client_secret_ref    # referensi ke clientSecret (HMAC-SHA512), JANGAN simpan plaintext di DB
>   merchant_id_manjo    # merchantId / X-PARTNER-ID di request Manjo
>   channel_id           # CHANNEL-ID (device identification, String(5))
>   store_id             # opsional, storeId
>   terminal_id          # opsional, terminalId
> ```
>
> `private_key_ref` dan `client_secret_ref` sebaiknya berupa referensi ke secret manager (bukan kolom plaintext), sesuai catatan security di API Manjo (Section 11 API docs): private key & client secret tidak boleh ada di tempat yang mudah diakses.

> **[Update multi-tenant]** Merchant Resolver berkembang jadi **Device Resolver**: kunci lookup utama bukan lagi `merchant_id` dari topic, melainkan `device_id` (unik global, segmen terakhir topic). Dari `device_id`, resolver mengambil: kredensial Manjo (lewat `device.merchant_id` → `merchants`), `subMerchantId` (lewat `device.tenant_id` → `tenants`, kalau ada), dan `storeId`/`terminalId` (langsung dari `devices`). `merchant_id`/`tenant_slot` di topic terutama untuk debugging manual, bukan kunci lookup. Lihat `architecture.md` Section 4.4.

---

## 13. Manjo Client

Manjo Client bertanggung jawab berkomunikasi dengan API Manjo.

Tugasnya:

```text
Service
   ↓
Manjo API
   ↓
Response
   ↓
Service
```

Manjo Client tidak perlu mengetahui detail MQTT.

Dengan begitu terdapat pemisahan tanggung jawab:

```text
MQTT Layer
     ↓
Transaction Layer
     ↓
Manjo API Layer
```

> **[Update dari API Manjo]** Manjo Client sekarang punya kontrak konkret — empat endpoint:
>
> | Fungsi | Method | Path | Auth |
> |---|---|---|---|
> | Get Access Token | POST | `/v1.0/access-token/b2b` | `X-SIGNATURE` (SHA256withRSA) |
> | Generate QRIS MPM | POST | `/v1.0/qr/qr-mpm-generate` | Bearer token + `X-SIGNATURE` (HMAC-SHA512) |
> | Query Payment | POST | `/v1.0/qr/qr-mpm-query` | Bearer token + `X-CLIENT-KEY` + `X-SIGNATURE` (HMAC-SHA512) |
> | *(terima, bukan panggil)* Payment Notification | POST | `/v1.0/qr/qr-mpm-notify` | Bearer token + `X-SIGNATURE` (HMAC-SHA512), diverifikasi dari sisi Service |
>
> **Query Payment** (Service Code `51`, `manjo-api-docs.md` Section 5) adalah operasi **pull** pelengkap Payment Notification (yang bersifat **push**) — dipakai Service untuk mengecek status transaksi ke Manjo secara aktif, terutama saat notifikasi tidak kunjung diterima. **Peringatan penting:** response Query memakai skema `latestTransactionStatus` yang **berbeda** dari Payment Notification (kode `03` = "Paid" di Notification tapi = "Pending" di Query) — Signature Builder & Request Builder boleh reuse HMAC-SHA512, tapi **status mapping wajib punya fungsi terpisah** per endpoint. Lihat detail lengkap di [Section 22](#22-source-of-truth).
>
> **Sub-komponen yang perlu ditambahkan di Manjo Client:**
>
> 1. **Access Token Manager** — cache `accessToken` in-memory per merchant (kalau kredensial per-merchant) dengan `expiresIn = 900` detik; refresh proaktif sebelum expired (misal refresh saat sisa < 60 detik) supaya request generate-QR tidak gagal di tengah jalan.
> 2. **Signature Builder — Access Token** (`SHA256withRSA`):
>    ```text
>    stringToSign = X-CLIENT-KEY + "|" + X-TIMESTAMP
>    X-SIGNATURE = RSA_SHA256_SIGN(privateKey, stringToSign)
>    ```
> 3. **Signature Builder — Generate QR & lainnya** (`HMAC_SHA512`):
>    ```text
>    hexBodyHash = Lowercase(HexEncode(SHA256(minify(RequestBody))))
>    stringToSign = HTTPMethod + ":" + EndpointUrl + ":" + AccessToken + ":" + hexBodyHash + ":" + X-TIMESTAMP
>    signature = HMAC_SHA512(clientSecret, stringToSign)
>    ```
> 4. **X-EXTERNAL-ID Generator** — ID unik per request per hari (misal UUID atau `{merchant_id}-{yyyyMMdd}-{counter}`), dipakai untuk generate QR dan wajib ada di setiap request.
> 5. **Request Builder** — menyusun body sesuai skema Manjo (`partnerReferenceNo`, `amount.value` string 2 desimal, `merchantId`, `validityPeriod`, `additionalInfo.paymentId`, `additionalInfo.dynamicAmount = "N"`, `additionalInfo.prodDesc`).
>
> Detail lengkap format signature & contoh payload ada di `manjo-api-docs.md` Section 2–4.

---

## 14. MQTT Publisher

MQTT Publisher digunakan untuk mengirim response/event kembali ke Q161.

**Contoh Generate QR:**

```text
Manjo
  ↓
QRIS Payload
  ↓
Service
  ↓
MQTT Publisher
  ↓
topic_MT82419344
  ↓
Q161
```

**Contoh Payment Success:**

```text
Manjo
  ↓
Payment Success
  ↓
Service
  ↓
MQTT Publisher
  ↓
topic_MT82419344
  ↓
Q161
  ↓
Soundbox Notification
```

---

## 15. Transaction Management

Setiap transaksi sebaiknya memiliki identifier yang dapat digunakan untuk melakukan correlation.

Contoh:

```text
transaction_id:
TRX-20260924-000001

merchant_id:
MT82419344

amount:
50000

status:
PENDING
```

Setelah customer membayar:

```text
transaction_id:
TRX-20260924-000001

status:
PAID
```

Dengan adanya transaction ID, service dapat menghubungkan:

```text
Generate QR
      ↓
QRIS Payload
      ↓
Customer Payment
      ↓
Payment Success
```

> **[Update dari API Manjo]** Manjo membedakan **dua reference ID berbeda** (lihat API docs Section 7 "Recommended Transaction Mapping") — dokumen ini perlu mengadopsi keduanya, bukan cuma satu `transaction_id`:
>
> | Field internal | Asal | Fungsi |
> |---|---|---|
> | `transaction_id` (`TRX-20260924-000001`) | Digenerate Service | Dikirim ke Manjo sebagai `partnerReferenceNo` saat generate QR |
> | `reference_no` (contoh: `A0000001702`) | Dikembalikan Manjo | Disimpan sebagai `referenceNo`, dipakai untuk pengecekan/audit ke sisi Manjo bila diperlukan |
>
> Saat Payment Notification masuk, Manjo mengirim `originalPartnerReferenceNo` (= `transaction_id` yang Service kirim sebelumnya) dan `originalReferenceNo` (= `reference_no` dari Manjo). **Matching wajib pakai `originalPartnerReferenceNo` terhadap `transaction_id`** karena itu ID yang sepenuhnya dikontrol Service.
>
> Update flow:
> ```text
> Generate QR (kirim partnerReferenceNo = transaction_id)
>       ↓
> Manjo balas referenceNo → simpan sebagai reference_no
>       ↓
> QRIS Payload (qrContent)
>       ↓
> Customer Payment
>       ↓
> Payment Notification (originalPartnerReferenceNo = transaction_id, originalReferenceNo = reference_no)
>       ↓
> Cocokkan transaction_id → update status
> ```

---

## 16. Idempotency

Idempotency merupakan aspek penting karena message MQTT atau payment event berpotensi diterima lebih dari satu kali.

Contoh:

```text
Payment Success
      ↓
Service
      ↓
MQTT
```

Kemudian event yang sama diterima lagi:

```text
Payment Success
      ↓
Service
      ↓
MQTT
```

Service harus dapat mengenali bahwa event tersebut sudah pernah diproses.

Contoh:

```text
transaction_id = TRX-001
status = PAID
```

Jika event `TRX-001` diterima lagi, service tidak boleh membuat transaksi baru atau memproses payment kedua kali.

Konsep:

```text
if transaction already processed:
    do not process again
```

> **[Update dari API Manjo]** Idempotency di sistem ini perlu ditangani di **dua level berbeda**, karena Manjo sendiri sudah punya mekanisme idempotency-nya sendiri di level request, tapi itu tidak otomatis melindungi level notifikasi:
>
> 1. **Level outbound (Service → Manjo, saat generate QR):** Manjo menolak `X-EXTERNAL-ID` yang dipakai ulang di hari yang sama dengan `HTTP 409`. Ini melindungi dari double-generate-QR yang tidak disengaja, tapi **bukan pengganti** idempotency internal Service — Service tetap harus generate `X-EXTERNAL-ID` baru per percobaan yang sah (retry beda dari duplicate request).
> 2. **Level inbound (Manjo → Service, saat payment notification):** API Manjo secara eksplisit mencatat kemungkinan **duplicate notification** (lihat Implementation Checklist API docs: "Duplicate notification ditangani secara idempotent"). Manjo bisa mengirim ulang notifikasi yang sama (misal karena endpoint Service timeout saat pertama kali). Service **wajib**:
>    - Cek `originalPartnerReferenceNo` (= `transaction_id`) terhadap status transaksi tersimpan.
>    - Kalau status sudah `PAID`/final dan notifikasi masuk lagi dengan `latestTransactionStatus` yang sama → balas `200 OK` tanpa memproses ulang (jangan publish ulang ke MQTT agar soundbox tidak bunyi dua kali).
>    - Simpan setiap notifikasi mentah di `mqtt_messages`-equivalent log (lihat [Section 19](#19-mqtt-message-logging)) sebelum diproses, supaya replay/debug bisa dilakukan.

---

## 17. Concurrency

Service harus mampu menangani beberapa transaksi secara bersamaan.

Contoh:

```text
Merchant 1
    │
    ├── Transaction A → Rp50.000
    │
    └── Transaction B → Rp100.000


Merchant 2
    │
    └── Transaction C → Rp75.000
```

Semua transaksi dapat berjalan secara bersamaan.

Service tidak boleh mengasumsikan:

```text
1 Merchant = 1 Transaction aktif
```

kecuali hal tersebut memang dijamin oleh protokol Q161 atau Manjo.

---

## 18. Database

Database disarankan untuk menyimpan data merchant dan transaksi.

Minimal tabel:

- `merchants`
- `transactions`
- `mqtt_messages`

### Tabel `merchants`

| Kolom                 |
| --------------------- |
| `id`                  |
| `merchant_id`         |
| `mqtt_topic`          |
| `manjo_configuration` |
| `status`              |
| `created_at`          |
| `updated_at`          |

> **[Update dari API Manjo]** Rincian `manjo_configuration` — bisa disimpan sebagai kolom terpisah atau JSON, tapi minimal berisi field berikut (lihat [Section 12](#12-merchant-resolver)):
>
> | Kolom tambahan | Keterangan |
> |---|---|
> | `manjo_client_id` | `X-CLIENT-KEY` |
> | `manjo_private_key_ref` | referensi ke RSA private key (secret manager) |
> | `manjo_client_secret_ref` | referensi ke `clientSecret` (secret manager) |
> | `manjo_merchant_id` | `merchantId` / `X-PARTNER-ID` di request Manjo |
> | `manjo_channel_id` | `CHANNEL-ID` |
> | `manjo_store_id` | opsional |
> | `manjo_terminal_id` | opsional |

> **[Update multi-tenant]** `manjo_store_id`/`manjo_terminal_id` **dipindah** dari `merchants` ke tabel baru `devices` (satu merchant bisa punya banyak device dengan store/terminal berbeda). Dua tabel baru ditambahkan: `tenants` (sub-merchant, opsional) dan `devices` (physical Q161 unit, kunci lookup utama Device Resolver). `transactions` mendapat kolom `device_id` sebagai penentu routing MQTT publish. Skema DDL lengkap: `schema.md` Section 5-7.

### Tabel `transactions`

| Kolom            |
| ---------------- |
| `id`             |
| `transaction_id` |
| `merchant_id`    |
| `amount`         |
| `status`         |
| `qris_payload`   |
| `created_at`     |
| `paid_at`        |
| `updated_at`     |

> **[Update dari API Manjo]** Tambahan kolom yang dibutuhkan agar selaras dengan response/notifikasi Manjo:
>
> | Kolom tambahan | Asal | Keterangan |
> |---|---|---|
> | `reference_no` | `referenceNo` (response generate QR) | ID transaksi versi Manjo, lihat [Section 15](#15-transaction-management) |
> | `manjo_status_code` | `latestTransactionStatus` (notifikasi) | Kode asli dari Manjo (`00`–`06`), disimpan terpisah dari `status` internal untuk audit — lihat [Section 22](#22-source-of-truth) |
> | `expire_at` | `additionalInfo.expireDate` (response generate QR) | Dipakai untuk auto-expire transaksi `PENDING`/`QR_GENERATED` yang tidak dibayar |
> | `external_id` | `X-EXTERNAL-ID` yang dikirim saat generate QR | Untuk tracing/debug bila ada isu ke pihak Manjo |
>
> Kolom `qris_payload` di atas isinya adalah `qrContent` dari response Manjo.

Status transaksi dapat berupa:

- `PENDING`
- `QR_GENERATED`
- `PAID`
- `FAILED`
- `EXPIRED`
- `CANCELLED`

> **Catatan:** Status final harus disesuaikan dengan status yang tersedia dari Manjo.

> **[Update dari API Manjo]** Mapping konkret status internal ⇄ `latestTransactionStatus` Manjo ada di [Section 22](#22-source-of-truth).

### Tabel `mqtt_messages`

| Kolom           |
| --------------- |
| `id`            |
| `topic`         |
| `payload`       |
| `direction`     |
| `status`        |
| `created_at`    |
| `processed_at`  |
| `error_message` |

`direction`:

- `INBOUND`
- `OUTBOUND`

> **[Update dari API Manjo]** Karena Payment Notification masuk lewat **HTTP**, bukan MQTT, disarankan tabel log terpisah `manjo_api_logs` (di luar `mqtt_messages`) untuk mencatat seluruh request/response HTTP ke/dari Manjo (access token, generate QR, notification) — kolomnya mirip `mqtt_messages` tapi menyimpan header (tanpa credential sensitif), endpoint, HTTP status, dan durasi request. Ini penting untuk debug kasus timeout/signature-mismatch yang tidak akan tertangkap di log MQTT.

---

## 19. MQTT Message Logging

Raw MQTT message sebaiknya disimpan untuk kebutuhan debugging dan tracing.

Contoh:

```text
mqtt_messages

id:
12345

topic:
topic_MT82419344

payload:
{ ... }

direction:
INBOUND

status:
PROCESSED

created_at:
2026-09-24 12:41:03
```

Hal ini membantu ketika terjadi masalah seperti:

> **Merchant:** "Pembayaran sudah dilakukan tetapi soundbox tidak berbunyi."

Service dapat melakukan tracing:

```text
Q161
 ↓
MQTT
 ↓
Service
 ↓
Transaction
 ↓
Manjo
 ↓
Payment Event
 ↓
MQTT
 ↓
Q161
```

> **[Update dari API Manjo]** Dengan adanya `manjo_api_logs` ([Section 18](#18-database)), tracing kasus di atas bisa lebih presisi: cek apakah notifikasi HTTP dari Manjo memang diterima (`manjo_api_logs`) sebelum masuk MQTT — kalau notifikasi tidak pernah sampai ke endpoint Service, masalahnya ada di sisi konfigurasi webhook URL/network, bukan di MQTT layer.

---

## 20. Retry Mechanism

Service perlu mempertimbangkan retry ketika komunikasi dengan Manjo atau MQTT mengalami kegagalan.

Contoh:

```text
Q161
 ↓
MQTT
 ↓
Service
 ↓
Manjo
 ↓
Request Failed
```

Service dapat melakukan retry sesuai aturan yang ditentukan.

Contoh konsep:

```text
Attempt 1
    ↓
Failed
    ↓
Attempt 2
    ↓
Failed
    ↓
Attempt 3
    ↓
Failed
    ↓
Mark as FAILED
```

> **Catatan:** Retry tidak boleh menyebabkan transaksi diproses dua kali.

Karena itu retry harus dikombinasikan dengan:

```text
Idempotency
+
Transaction ID
```

> **[Update dari API Manjo]** Setiap retry ke `qr-mpm-generate` **wajib pakai `X-EXTERNAL-ID` baru** (karena ID lama akan ditolak `409` jika dipakai ulang di hari yang sama), tapi tetap pakai `partnerReferenceNo`/`transaction_id` yang sama supaya transaksi tidak terduplikasi di sisi Service. Untuk kegagalan access token (`401`), retry harus request token baru dulu sebelum retry request utama — jangan retry dengan token yang sama karena hasilnya pasti gagal lagi.

---

## 21. Error Handling

Kemungkinan error:

- MQTT Connection Failed
- MQTT Message Invalid
- Unknown Merchant
- Invalid Amount
- Manjo API Error
- Manjo Timeout
- QR Generation Failed
- Duplicate Transaction
- Payment Event Unknown
- MQTT Publish Failed

Setiap error harus memiliki logging yang jelas.

Contoh:

```text
ERROR
merchant_id: MT82419344
transaction_id: TRX-001
operation: GENERATE_QR
error: MANJO_TIMEOUT
timestamp: 2026-09-24 12:41:03
```

> **[Update dari API Manjo]** Error code Manjo yang konkret dan perlu di-handle spesifik oleh Service:
>
> | HTTP | Service | Case | Arti | Penanganan di Service |
> |---:|---:|---:|---|---|
> | 400 | 73/47/52 | 01 | Invalid Field Format | Bug di Request Builder — log & alert, jangan retry otomatis |
> | 400 | 73/47/52 | 02 | Invalid Mandatory Field | Sama seperti di atas |
> | 401 | 73/47/52 | 00/01 | Unauthorized / Invalid Token | Access token expired/invalid → refresh token lalu retry sekali |
> | 404 | 47 | 08 | Invalid Merchant | `merchantId` salah/tidak terdaftar → cek Merchant Resolver, jangan retry |
> | 409 | 47/52 | 00 | Conflict (`X-EXTERNAL-ID` dipakai ulang) | Generate `X-EXTERNAL-ID` baru, transaksi jangan dianggap gagal — bisa jadi transaksi sebenarnya sukses tapi response sebelumnya hilang; **cek status transaksi dulu sebelum retry**, sekarang bisa pakai **Query Payment** (Service Code `51`) untuk memastikan status asli di sisi Manjo sebelum retry generate baru |
>
> Tambahan skenario error khusus Payment Notification (Manjo → Service): kalau **signature verification gagal** saat menerima notifikasi, Service harus balas `401`/`400` ke Manjo (bukan memproses payload) — ini mencegah notifikasi palsu memicu bunyi soundbox.

---

## 22. Source of Truth

Hal yang perlu dipastikan adalah sistem mana yang menjadi sumber kebenaran status transaksi.

Untuk flow ini, status pembayaran idealnya berasal dari sistem payment gateway/Manjo.

Contoh:

```text
Q161
  ↓
Generate QR
  ↓
Manjo
  ↓
QRIS
  ↓
Customer Payment
  ↓
Manjo
  ↓
Payment Success
```

Q161 tidak seharusnya menentukan sendiri bahwa pembayaran berhasil hanya karena QR telah ditampilkan.

Status:

```text
PAID
```

harus berdasarkan payment event/status yang berasal dari sistem pembayaran yang relevan.

> **[Update dari API Manjo]** Source of truth konkretnya adalah field `latestTransactionStatus` pada Payment Notification. Mapping ke status internal:
>
> | `latestTransactionStatus` (Manjo) | Deskripsi Manjo | Status internal |
> |---|---|---|
> | `00` | Success | `PAID` |
> | `01` | Failed | `FAILED` |
> | `02` | Not Found | `FAILED` (atau log khusus, transaksi tidak dikenali Manjo) |
> | `03` | Paid | `PAID` |
> | `04` | Pending | `PENDING` (tetap tunggu notifikasi berikutnya) |
> | `05` | Refunded | `REFUNDED` *(status baru, belum ada di daftar status Section 18 — perlu ditambahkan)* |
> | `06` | Cancelled | `CANCELLED` |
>
> Catatan penting: dokumentasi Manjo mencantumkan **dua kode berbeda untuk "berhasil"** (`00` Success dan `03` Paid) tanpa penjelasan eksplisit kapan masing-masing dipakai — **ini wajib dikonfirmasi ke tim Manjo** sebelum coding, karena kalau Service hanya menangani salah satu kode, ada risiko notifikasi sukses tidak memicu bunyi soundbox. Sarannya: treat **keduanya** (`00` dan `03`) sebagai `PAID` di awal implementasi, sambil menunggu konfirmasi resmi.
>
> **⚠️ [Update dari API Manjo] Skema status kedua — jangan tertukar.** Selain Payment Notification, Manjo juga punya **Query Payment** (Service Code `51`, `manjo-api-docs.md` Section 5) yang dipakai untuk pengecekan status manual/reconciliation. Response-nya **juga** memakai field `latestTransactionStatus`, tapi dengan **skema kode yang sama sekali berbeda**:
>
> | Code | Payment Notification (`52`) | Query Payment (`51`) |
> |---|---|---|
> | `00` | Success | Success |
> | `01` | Failed | Initiated |
> | `02` | Not Found | Paying |
> | `03` | Paid | Pending |
> | `04` | Pending | Refunded |
> | `05` | Refunded | Canceled |
> | `06` | Cancelled | Failed |
> | `07` | *(tidak ada)* | Not found |
>
> Kode `03` contohnya berarti **Paid** di satu endpoint tapi **Pending** di endpoint lain. Kalau Service menerapkan satu fungsi mapping status untuk kedua endpoint (godaan yang wajar karena nama field sama), transaksi pending berisiko salah dibaca sebagai lunas. **Transaction Service wajib punya dua fungsi mapping status terpisah** — satu untuk hasil Payment Notification, satu untuk hasil Query Payment — dan keduanya baru menulis ke kolom `status`/`manjo_status_code` internal yang sama setelah melalui mapping yang benar.
>
> Query Payment juga membuka opsi **reconciliation job**: transaksi yang masih `QR_GENERATED` mendekati `expire_at` (atau sudah lewat tapi mencurigakan) bisa di-query dulu ke Manjo sebelum divonis `EXPIRED`, untuk menangkap kasus notifikasi hilang. Ini **belum diputuskan** apakah masuk scope v1 — lihat `architecture.md` Section 20 dan `PRD.md` Section 14.

---

## 23. Full End-to-End Flow

### Generate QR

```text
Merchant
   ↓
Q161 Pro
   ↓
Input Amount
   ↓
MQTT Publish
   ↓
MQTT Broker
   ↓
Payment Bridge Service
   ↓
Identify Merchant
   ↓
Validate Request
   ↓
Create Transaction
   ↓
Manjo API
   ↓
Generate Dynamic QRIS
   ↓
QRIS Payload
   ↓
Payment Bridge Service
   ↓
MQTT Publish
   ↓
MQTT Broker
   ↓
Q161 Pro
   ↓
Generate QR Image
   ↓
QRIS tampil
```

> **[Update dari API Manjo]** Langkah "Manjo API → Generate Dynamic QRIS" di atas mencakup: (a) cek/ambil access token yang valid (request baru ke `/v1.0/access-token/b2b` kalau perlu), (b) build signature HMAC-SHA512, (c) `POST /v1.0/qr/qr-mpm-generate`. Langkah "Create Transaction" sebaiknya terjadi **sebelum** call ke Manjo (status `PENDING`, simpan `transaction_id`), lalu di-update ke `QR_GENERATED` (simpan `reference_no`, `qris_payload`, `expire_at`) setelah response Manjo sukses.

### Payment

```text
Customer
   ↓
Scan QRIS
   ↓
Payment
   ↓
Payment Ecosystem
   ↓
Manjo
   ↓
Payment Success
   ↓
Payment Bridge Service
   ↓
Find Transaction
   ↓
Validate Transaction
   ↓
Update Transaction = PAID
   ↓
MQTT Publish
   ↓
MQTT Broker
   ↓
topic_MT82419344
   ↓
Q161 Pro
   ↓
Soundbox
   ↓
"Pembayaran berhasil..."
```

> **[Update dari API Manjo]** "Payment Bridge Service" menerima "Payment Success" di atas melalui **HTTP endpoint** (`/v1.0/qr/qr-mpm-notify` versi Service), bukan proses background. Urutan detail: (1) verifikasi signature notifikasi, (2) cek idempotency via `originalPartnerReferenceNo`, (3) "Find Transaction" = lookup by `transaction_id` (dari `originalPartnerReferenceNo`), (4) map `latestTransactionStatus` ke status internal ([Section 22](#22-source-of-truth)), (5) update transaksi, (6) balas `200 OK` ke Manjo, (7) baru publish ke MQTT. Balas `200 OK` ke Manjo **tidak harus menunggu** MQTT publish selesai — pisahkan supaya response HTTP ke Manjo tetap cepat walau MQTT broker lambat/down (idealnya publish MQTT tetap dicoba lewat retry queue terpisah kalau gagal).

---

## 24. Arsitektur Konseptual

```text
┌─────────────────────────────────────────────────────────┐
│                       Q161 PRO                          │
│                                                         │
│  Input Amount → Display QR → Payment Notification       │
└───────────────────────┬─────────────────────────────────┘
                        │
                        │ MQTT
                        ▼
┌─────────────────────────────────────────────────────────┐
│                     MQTT BROKER                         │
│                                                         │
│ topic_MT82419344                                        │
│ topic_MT82419345                                        │
│ topic_MT82419346                                        │
│ ...                                                     │
└───────────────────────┬─────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│                PAYMENT BRIDGE SERVICE                   │
│                                                         │
│  ┌──────────────┐     ┌─────────────────────────────┐   │
│  │ MQTT Consumer│ ──→ │ Message / Transaction       │   │
│  └──────────────┘     │ Processor                   │   │
│                       └──────────────┬──────────────┘   │
│                                      │                  │
│                       ┌──────────────┴──────────────┐   │
│                       │                             │   │
│                       ▼                             ▼   │
│                ┌─────────────┐              ┌──────────┐│
│                │ Transaction │              │ Merchant ││
│                │ Service     │              │ Resolver ││
│                └──────┬──────┘              └──────────┘│
│                       │                                 │
│                       ▼                                 │
│                ┌─────────────┐                          │
│                │ Manjo Client│                          │
│                └──────┬──────┘                          │
│                       │                                 │
│                ┌──────▼──────┐                          │
│                │ MQTT        │                          │
│                │ Publisher   │                          │
│                └─────────────┘                          │
│                                                         │
│  ┌───────────────────────────────────────────────────┐ │
│  │ Notification HTTP Endpoint  (baru)                 │ │
│  │ POST /v1.0/qr/qr-mpm-notify  ←── dipanggil Manjo   │ │
│  └───────────────────────┬───────────────────────────┘ │
│                          │ (masuk ke Transaction Service)│
└───────────────────────┬──┴──────────────────────────────┘
                        │
                        │ API (HTTPS)
                        ▼
┌─────────────────────────────────────────────────────────┐
│                         MANJO                           │
│                                                         │
│  POST /v1.0/access-token/b2b     (get token)            │
│  POST /v1.0/qr/qr-mpm-generate   (generate QR)          │
│  POST .../qr-mpm-notify → Service (payment notification)│
└─────────────────────────────────────────────────────────┘
```

> **[Update dari API Manjo]** Diagram di atas menambahkan komponen **Notification HTTP Endpoint** yang tidak eksplisit di versi awal — ini bukan bagian dari alur MQTT, tapi server HTTP terpisah (bisa jadi route berbeda dalam service yang sama) yang harus reachable oleh Manjo dari internet (perlu domain/IP publik + HTTPS, sesuai requirement API Manjo Section 11.1).

---

## 25. Hal yang Harus Dipastikan Sebelum Coding

Sebelum implementasi, perlu mendapatkan kontrak/protocol yang jelas untuk minimal empat jenis message berikut.

### 25.1 Q161 → MQTT

Message ketika merchant memasukkan amount.

> **Status: masih perlu diputuskan sendiri** (Q161 firmware dan Service sama-sama kamu yang bikin) — API Manjo tidak memengaruhi bagian ini karena ini murni internal Q161 ↔ Service. Lihat pembahasan sebelumnya soal opsi JSON vs raw value, dan siapa yang generate `transaction_id`.

Yang perlu diketahui:

- Topic: `topic_{merchant_id}`
- Payload: *(keputusan kamu — draft: `{"amount": 50000}` atau `{"amount": 50000, "transaction_id": "..."}`)*
- Format: JSON (disarankan, untuk future-proof menambah field)
- Amount: integer Rupiah (akan dikonversi Service ke string 2-desimal untuk Manjo, misal `50000` → `"50000.00"`)
- Transaction ID: *(disarankan digenerate Service, bukan device, supaya penomoran konsisten dan gampang di-uniquekan)*
- Timestamp: opsional, berguna untuk audit tapi tidak wajib untuk Manjo
- QoS: keputusan kamu (disarankan minimal QoS 1 agar delivery terjamin)
- Retain: disarankan `false` (perintah generate QR bukan status yang perlu di-retain)
- ACK: opsional, tergantung apakah device perlu konfirmasi "request diterima" sebelum QR datang

### 25.2 Service → Q161 (QRIS Payload)

Message ketika QRIS payload dari Manjo dikirim kembali.

> **Status: masih perlu diputuskan sendiri**, tapi sekarang datanya jelas datang dari response `qr-mpm-generate`.

Yang perlu diketahui:

- Topic: `topic_{merchant_id}`
- Payload: draft —
  ```json
  {
    "type": "QR_RESULT",
    "transaction_id": "TRX-20260924-000001",
    "status": "SUCCESS",
    "qris_payload": "<qrContent dari Manjo>",
    "expire_at": "<dari additionalInfo.expireDate>"
  }
  ```
- Format: JSON
- QRIS String: langsung dari field `qrContent` Manjo (jangan dimodifikasi)
- Transaction ID: sama seperti yang dikirim Q161 di 25.1
- ACK: opsional

### 25.3 Manjo → Service

Event ketika pembayaran berhasil.

> **Status: sudah terjawab lengkap dari API Manjo (Service Code `52`).**

Yang perlu diketahui — semua sudah terjawab:

- Transaction ID: `originalReferenceNo` (versi Manjo) + `originalPartnerReferenceNo` (versi Service/internal)
- Merchant ID: `additionalInfo.merchantCode` (opsional — jangan diandalkan sebagai satu-satunya sumber, cocokkan lewat `originalPartnerReferenceNo` → lookup transaksi → ambil `merchant_id` dari record)
- Amount: `amount.value` (net) dan `additionalInfo.amountTrx` (transaksi) — keduanya string 2-desimal
- Payment Status: `latestTransactionStatus` (kode `00`–`06`, lihat [Section 22](#22-source-of-truth))
- Reference: `additionalInfo.rrn` (Retrieval Reference Number, dari sisi issuer/acquirer)
- Timestamp: `additionalInfo.trxTime`
- Signature: `X-SIGNATURE` di header (HMAC-SHA512) — **wajib diverifikasi** sebelum payload dipercaya, formula sama seperti generate QR tapi dihitung dari sisi Service sebagai penerima

### 25.4 Service → Q161 (Payment Notification)

Message untuk memberikan notifikasi pembayaran.

> **Status: sebagian terjawab.** Sumber `amount` sekarang jelas (dari `additionalInfo.amountTrx` di notifikasi Manjo, sudah divalidasi lewat proses di 25.3). Yang **masih murni keputusan kamu** (karena ini bagian firmware Q161):

Yang perlu diketahui:

- Topic: `topic_{merchant_id}`
- Payload: draft, disesuaikan dengan pola Q181 —
  ```json
  {
    "type": "PAYMENT_NOTIFICATION",
    "transaction_id": "TRX-20260924-000001",
    "status": "PAID",
    "amount": 50000,
    "audio_sequence": "/ext/awal-qris.mp3+/ext/seratus.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3"
  }
  ```
- Payment Status: `PAID` / `FAILED` (status internal, sudah dipetakan dari `latestTransactionStatus`)
- Amount: integer Rupiah, sumber dari `additionalInfo.amountTrx` (dikonversi balik dari string desimal Manjo ke integer)
- Transaction ID: `transaction_id` internal (sama seperti 25.1/25.2)
- Notification Type: **belum diputuskan** — apakah field `audio_sequence` dibangun Service (reuse logic Q181) atau Q161 sendiri yang convert dari `amount` mentah; juga belum dipastikan apakah format string `+`-separated dipertahankan atau diganti array JSON. Ini **satu-satunya bagian dari empat kontrak yang masih butuh keputusan desain murni kamu**, karena tidak ada informasi dari API Manjo yang memengaruhinya.

---

## 26. Kesimpulan Arsitektur

Service yang akan dibuat dapat diposisikan sebagai:

```text
Q161 Pro ↔ MQTT ↔ Payment Bridge Service ↔ Manjo
```

Dengan dua fungsi utama:

### 26.1 QR Generation Bridge

```text
Q161
 ↓
MQTT
 ↓
Service
 ↓
Manjo
 ↓
QRIS Payload
 ↓
Service
 ↓
MQTT
 ↓
Q161
```

### 26.2 Payment Notification Bridge

```text
Customer
 ↓
Payment
 ↓
Manjo
 ↓
Service
 ↓
MQTT
 ↓
Q161
 ↓
Soundbox Notification
```

Service tidak bertanggung jawab untuk membuat gambar QRIS. Service hanya menangani komunikasi, transaksi, dan pertukaran payload/event antara Q161 dan Manjo.

### Prioritas Sebelum Implementasi

1. Pahami MQTT protocol Q161
2. Pahami format payload Q161
3. Pahami API Manjo ✅ *(sudah — lihat `manjo-api-docs.md`)*
4. Tentukan correlation/transaction ID ✅ *(sudah — `transaction_id` = `partnerReferenceNo`, `reference_no` = `referenceNo` Manjo, lihat Section 15)*
5. Tentukan mapping merchant → topic
6. Tentukan mapping transaction → merchant ✅ *(sudah — via `transaction_id`, lihat Section 15)*
7. Tentukan mekanisme idempotency ✅ *(sudah — dua level, lihat Section 16; plus `X-EXTERNAL-ID` di sisi Manjo)*
8. Tentukan retry & error handling ✅ *(sudah — lihat Section 20 & 21)*
9. Tentukan status transaksi ✅ *(sudah, dengan satu open question soal kode `00` vs `03` — lihat Section 22)*
10. Baru menentukan implementasi teknis

---

## 27. Ringkasan Update Berdasarkan API Manjo

Ringkasan seluruh perubahan yang ditambahkan setelah membaca `manjo-api-docs.md`, untuk referensi cepat.

### 27.1 Yang Sudah Terjawab

| Topik | Jawaban |
|---|---|
| Endpoint Manjo | `/v1.0/access-token/b2b`, `/v1.0/qr/qr-mpm-generate`, `/v1.0/qr/qr-mpm-query`, `/v1.0/qr/qr-mpm-notify` |
| Auth flow | Access token B2B (`client_credentials`, expiry 900 detik) → dipakai sebagai Bearer di request lain |
| Signature | `SHA256withRSA` untuk access token; `HMAC_SHA512` untuk generate QR & notification |
| Format field QR | `qrContent` (bukan "QRIS payload" generik), `referenceNo`, `partnerReferenceNo` |
| Dua ID transaksi | `transaction_id` (internal, dikirim sebagai `partnerReferenceNo`) vs `reference_no` (dari Manjo, `referenceNo`) |
| Format payment notification | Field lengkap: `originalReferenceNo`, `originalPartnerReferenceNo`, `latestTransactionStatus`, `amount`, `additionalInfo.*` |
| Status transaksi | Kode `00`–`06`, dengan catatan `00` dan `03` sama-sama berarti "berhasil" (perlu konfirmasi ke Manjo). **Query Payment (`51`) punya skema kode `00`–`07` yang berbeda arti dari Payment Notification (`52`) meski field sama-sama `latestTransactionStatus`** — wajib dua mapping terpisah |
| Idempotency Manjo | `X-EXTERNAL-ID` unique per hari, `409` kalau dipakai ulang |
| Kredensial merchant | `client_id`, private key (RSA), `clientSecret`, `merchantId`, `channelId` — per merchant, disimpan aman |
| Error codes | Mapping lengkap per service code (73/47/52) |

### 27.2 Yang Masih Perlu Diputuskan/Dikonfirmasi

| Topik | Keterangan | Siapa yang putuskan |
|---|---|---|
| Payload Q161 → MQTT (generate QR request) | Format JSON, siapa generate `transaction_id` | Kamu (firmware Q161) |
| Payload Service → Q161 (QRIS result) | Struktur JSON final | Kamu (firmware Q161) |
| Payload Service → Q161 (payment notification / audio) | Format `audio_sequence`, reuse pola Q181 atau tidak | Kamu (firmware Q161) |
| Status `00` vs `03` di Manjo | Kapan masing-masing dipakai, apakah keduanya perlu ditangani sama | Konfirmasi ke tim/dokumentasi Manjo |
| Kredensial per-merchant vs per-partner | Apakah tiap merchant Q161 punya `client_id`/private key sendiri, atau satu kredensial partner untuk semua merchant | Kamu / kesepakatan bisnis dengan Manjo |
| Endpoint publik untuk notification | Domain/IP + HTTPS yang akan didaftarkan ke Manjo sebagai webhook URL | Kamu (infra) |

### 27.3 Komponen Baru yang Ditambahkan ke Arsitektur

- **Notification HTTP Endpoint** — server HTTP terpisah dari MQTT Consumer, menerima webhook dari Manjo.
- **Access Token Manager** — cache & auto-refresh access token (bagian dari Manjo Client).
- **Signature Builder** — dua varian (SHA256withRSA untuk token, HMAC-SHA512 untuk request lain).
- **`manjo_api_logs`** — tabel log terpisah dari `mqtt_messages` untuk mencatat seluruh trafik HTTP ke/dari Manjo.
- **Query Payment Client** (Service Code `51`) — sub-fungsi baru di Manjo Client untuk cek status transaksi secara aktif ke Manjo; berguna sebagai fallback saat notifikasi tidak diterima, dan sebagai langkah verifikasi sebelum retry generate QR pasca-`409`. **Butuh mapping status terpisah dari Payment Notification** karena skema kodenya berbeda (lihat Section 22).