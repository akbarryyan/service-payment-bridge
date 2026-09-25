# Process Flow & Data Trace — Q161 Pro × Manjo Payment Bridge Service

**Versi:** 1.0
**Tujuan:** Narasi step-by-step tiap proses utama sistem, beserta data apa yang dibaca/ditulis di setiap langkah (tabel & kolom spesifik). Dipakai sebagai referensi cepat saat development maupun debugging ("di step mana seharusnya data ini ter-update?").
**Dokumen terkait:** `architecture.md` (sequence diagram tingkat tinggi), `schema.md` (struktur tabel lengkap)

---

## Daftar Isi

1. [Cara Membaca Dokumen Ini](#1-cara-membaca-dokumen-ini)
2. [Flow 1 — Generate QR](#2-flow-1--generate-qr)
3. [Flow 2 — Token Refresh (Sub-flow)](#3-flow-2--token-refresh-sub-flow)
4. [Flow 3 — Payment Notification](#4-flow-3--payment-notification)
5. [Flow 4 — Auto-Expire Job](#5-flow-4--auto-expire-job)
6. [Ringkasan Data Touchpoint per Tabel](#6-ringkasan-data-touchpoint-per-tabel)

---

## 1. Cara Membaca Dokumen Ini

Setiap flow dipecah jadi step bernomor, dengan kolom:

| Kolom | Arti |
|---|---|
| **Aktor/Komponen** | Siapa yang menjalankan step ini (lihat daftar komponen di `architecture.md` Section 4) |
| **Aksi** | Apa yang terjadi |
| **Data Dibaca** | Tabel.kolom yang di-`SELECT` (kalau ada) |
| **Data Ditulis** | Tabel.kolom yang di-`INSERT`/`UPDATE` (kalau ada) |
| **Validasi/Kondisi** | Aturan yang dicek di step ini |
| **Kalau Gagal** | Apa yang terjadi kalau validasi/step ini gagal |

---

## 2. Flow 1 — Generate QR

**Trigger:** Merchant input nominal di Q161 Pro.
**Berakhir saat:** QR tampil di layar Q161, atau transaksi ditandai `FAILED`.

| # | Aktor/Komponen | Aksi | Data Dibaca | Data Ditulis | Validasi/Kondisi | Kalau Gagal |
|---|---|---|---|---|---|---|
| 1 | Q161 Pro | Publish MQTT ke `qris/request` (topic tetap, shared semua device), payload `"{device_id}\|{amount_sen}"` misal `"MT58530503\|5000000"` | — | — | — | (di luar scope Service) |
| 2 | MQTT Consumer | Terima pesan dari `qris/request` | — | — | — | — |
| 3 | Message Parser | Parse payload pipe-delimited → `{device_id, amount_rupiah}` (`amount_sen / 100`) | — | — | Ada tepat satu `\|`; `device_id` tidak kosong; `amount_sen` integer positif | Reject → lanjut ke step 4b |
| 4a | Message Validator | Validasi: `amount > 0`, format sesuai skema | — | — | Amount integer positif | — |
| 4b | Message Validator (invalid path) | Catat pesan invalid | — | `mqtt_messages`: INSERT (`direction=INBOUND`, `status=FAILED`, `error_message`, `transaction_id=NULL`) | — | **Flow berhenti di sini** — tidak diteruskan ke Manjo |
| 5 | Device Resolver | Lookup `devices` berdasarkan `device_id` (dari cache in-memory, fallback ke DB); dari situ ambil `merchants` (kredensial, lewat `device.merchant_id`) dan `tenants` (`manjo_sub_merchant_id`, kalau `device.tenant_id` tidak null) | `devices` (`device_id`, `merchant_id`, `tenant_id`, `status`, `manjo_store_id`, `manjo_terminal_id`); `merchants` (`status`, `manjo_*`); `tenants` (`manjo_sub_merchant_id`, `status`, kalau ada) | — | Device harus ada & `status='ACTIVE'`; merchant terkait harus `status='ACTIVE'`; kalau `tenant_id` ada, tenant terkait juga harus `status='ACTIVE'` | Reject, catat di `mqtt_messages` sebagai `FAILED` dengan `error_message='UNKNOWN_DEVICE'`/`'DEVICE_INACTIVE'`/`'MERCHANT_INACTIVE'`/`'TENANT_INACTIVE'`. **Flow berhenti.** |
| 6 | Transaction Service | Generate `transaction_id` (format `TRX-{yyyyMMdd}-{seq}`) | — | `transactions`: INSERT (`transaction_id`, `merchant_id`, `device_id`, `amount`, `status='PENDING'`, `created_at=now()`) | `transaction_id` harus unique | Retry generate ID kalau collision (jarang terjadi dgn UUID/sequence yang benar) |
| 7 | Transaction Service | Catat pesan MQTT inbound, link ke transaksi | — | `mqtt_messages`: INSERT (`topic`, `payload`, `direction=INBOUND`, `status=PROCESSED`, `transaction_id`) | — | — |
| 8 | Manjo Client → Token Manager | Cek apakah access token untuk merchant ini masih valid di cache | Token cache in-memory (bukan DB) | — | Token ada & belum mendekati expired | Kalau tidak ada/mau expired → jalankan **Flow 2 (Token Refresh)**, lanjut setelah token didapat |
| 9 | Manjo Client → Signature Builder | Generate `X-EXTERNAL-ID` baru, hitung `X-SIGNATURE` (HMAC-SHA512) dari body request | `merchants.manjo_client_secret_ref` (ambil secret dari secret manager) | `transactions.external_id` = `X-EXTERNAL-ID` yang dipakai (UPDATE) | — | — |
| 10 | Manjo Client | `POST /v1.0/qr/qr-mpm-generate` ke Manjo | `merchants` (`manjo_merchant_id`, `manjo_channel_id`) untuk `merchantId`/`CHANNEL-ID`; `devices` (`manjo_store_id`, `manjo_terminal_id`) untuk `storeId`/`terminalId` (omit kalau null); `tenants` (`manjo_sub_merchant_id`, kalau `device.tenant_id` ada) untuk `subMerchantId` (omit kalau device tanpa tenant) | `manjo_api_logs`: INSERT (`direction=OUTBOUND`, `operation=GENERATE_QR`, `endpoint`, `request_body` *(signature di-mask)*, `transaction_id`) | Amount diformat `"50000.00"`, `dynamicAmount="N"` | — |
| 11a | Manjo Client (respons sukses) | Terima `qrContent`, `referenceNo`, `additionalInfo.expireDate` | — | `manjo_api_logs.response_body`, `manjo_api_logs.http_status=200` (UPDATE record step 10); `transactions`: UPDATE `status='QR_GENERATED'`, `qris_payload=qrContent`, `reference_no`, `expire_at` | — | — |
| 11b | Manjo Client (respons error) | Terima error (`400`/`401`/`404`/`409`/timeout/`5xx`) | — | `manjo_api_logs.http_status`, `response_body` (UPDATE record step 10) | Ikuti mapping error di `architecture.md` Section 13 | `401`→refresh token & retry sekali (kembali ke step 8); `409`→panggil **Query Payment** (`POST /v1.0/qr/qr-mpm-query`, Service Code `51`, `manjo-api-docs.md` Section 5) dengan `originalPartnerReferenceNo=transaction_id` untuk verifikasi status asli di Manjo **sebelum** retry — kalau hasil Query menunjukkan transaksi sudah sukses (mapping skema Query, bukan skema Notification — lihat `manjo-api-docs.md` Section 5.10), update transaksi langsung ke `QR_GENERATED`/`PAID` tanpa retry; kalau Query menunjukkan belum ada transaksi tercatat, retry ke step 9 dgn `X-EXTERNAL-ID` baru; timeout/`5xx`→retry maks 3x; lainnya→langsung ke step 12b |
| 11c | Manjo Client (query fallback pasca-`409`) | `POST /v1.0/qr/qr-mpm-query` | `merchants.manjo_client_secret_ref` (secret untuk signature Query) | `manjo_api_logs`: INSERT (`direction=OUTBOUND`, `operation=QUERY_PAYMENT`, `endpoint`, `request_body` *(signature di-mask)*, `transaction_id`) | Header `X-CLIENT-KEY` + `Authorization` Bearer wajib dua-duanya (beda dari generate QR) | Query gagal/timeout → treat seperti percobaan generate QR gagal, lanjut ke batas retry step 11b |
| 12a | Transaction Service | Transaksi berhasil punya QR | — | (sudah ter-update di step 11a) | — | — |
| 12b | Transaction Service | Semua percobaan gagal | — | `transactions`: UPDATE `status='FAILED'` | — | — |
| 13a | MQTT Publisher | Build payload `"QR:{qris_payload}"`, publish ke `"topic_" + device_id` | `transactions` (data yang baru di-update) | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `status=PROCESSED`, `transaction_id`) | — | Kalau publish gagal → catat `FAILED` di `mqtt_messages` (tidak mengubah status transaksi) |
| 13b | MQTT Publisher (kasus gagal) | Build pesan plain text manusiawi (mis. `"Gagal membuat QR, coba lagi"`), publish ke `"topic_" + device_id` | — | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `transaction_id`) | — | — |
| 14 | Q161 Pro | Terima `qris_payload`, render jadi gambar QR, tampilkan | — | — | — | (di luar scope Service) |

---

## 3. Flow 2 — Token Refresh (Sub-flow)

**Trigger:** Dipanggil dari Flow 1 step 8, saat token tidak ada di cache atau mendekati expired.
**Berakhir saat:** Token baru tersimpan di cache, kontrol kembali ke flow pemanggil.

| # | Aktor/Komponen | Aksi | Data Dibaca | Data Ditulis | Validasi/Kondisi | Kalau Gagal |
|---|---|---|---|---|---|---|
| 1 | Token Manager | Cek cache in-memory untuk merchant terkait | Token cache (in-memory, bukan DB) | — | Token tidak ada, atau sisa waktu < threshold refresh (mis. < 60 detik) | — |
| 2 | Signature Builder | Ambil `manjo_client_id` dan private key, hitung `X-SIGNATURE` (`SHA256withRSA`) dari `stringToSign = X-CLIENT-KEY + "|" + X-TIMESTAMP` | `merchants.manjo_client_id`, `merchants.manjo_private_key_ref` (ambil private key dari secret manager) | — | — | — |
| 3 | Manjo Client | `POST /v1.0/access-token/b2b` | — | `manjo_api_logs`: INSERT (`direction=OUTBOUND`, `operation=ACCESS_TOKEN`, `endpoint`, `request_body` *(X-SIGNATURE di-mask)*, `transaction_id=NULL` — tidak terkait transaksi spesifik) | — | — |
| 4a | Manjo Client (sukses) | Terima `accessToken`, `expiresIn=900` | — | Token Manager cache: simpan `accessToken` + waktu expired (in-memory); `manjo_api_logs.response_body` *(accessToken di-mask)*, `http_status=200` (UPDATE record step 3) | — | — |
| 4b | Manjo Client (gagal) | Terima error (`401` kredensial salah, timeout, `5xx`) | — | `manjo_api_logs.http_status`, `response_body` (UPDATE record step 3) | — | Propagate error ke flow pemanggil (Flow 1 step 11b menangani retry/kegagalan) |

> **Catatan concurrency:** kalau beberapa request generate-QR untuk merchant yang sama datang bersamaan dan sama-sama butuh refresh token, Token Manager harus memastikan hanya **satu** request token yang benar-benar terkirim ke Manjo (pakai lock/mutex per-merchant), request lain menunggu dan reuse hasilnya — lihat `qa.md` TC-TOKEN dan TC-CONC-04.

---

## 4. Flow 3 — Payment Notification

**Trigger:** Manjo memanggil webhook Service setelah customer menyelesaikan pembayaran.
**Berakhir saat:** Soundbox Q161 memutar audio notifikasi, atau notifikasi ditolak/diabaikan (kasus signature invalid/duplikat).

| # | Aktor/Komponen | Aksi | Data Dibaca | Data Ditulis | Validasi/Kondisi | Kalau Gagal |
|---|---|---|---|---|---|---|
| 1 | Manjo | `POST /webhooks/manjo/qr-mpm-notify` dengan body notifikasi + header `X-SIGNATURE` | — | — | — | — |
| 2 | Notification HTTP Endpoint | Terima request | — | — | — | — |
| 3 | Signature Verifier | Hitung ulang HMAC-SHA512 dari body, bandingkan dengan `X-SIGNATURE` header | `merchants.manjo_client_secret_ref` (identifikasi merchant dulu dari `additionalInfo.merchantCode` **atau** dari lookup transaksi terkait, lalu ambil secret-nya) | `manjo_api_logs`: INSERT (`direction=INBOUND`, `operation=PAYMENT_NOTIFY`, `request_body` *(signature di-mask)*) | Signature harus cocok | **Balas `401`, flow berhenti**, `manjo_api_logs.http_status=401` |
| 4 | Transaction Service | Parse payload: `originalPartnerReferenceNo`, `originalReferenceNo`, `latestTransactionStatus`, `amount.value`, `additionalInfo.amountTrx`, `additionalInfo.trxTime`, `additionalInfo.rrn` | — | — | Field wajib ada | Balas `400`, log error |
| 5 | Transaction Service | Lookup transaksi dengan row lock | `transactions` WHERE `transaction_id = originalPartnerReferenceNo` — **`SELECT ... FOR UPDATE`** | — | Transaksi harus ditemukan | Tidak ditemukan → log anomali, balas `200 OK` (supaya Manjo tidak retry terus) tapi **tidak** ada proses lanjutan |
| 6a | Transaction Service (idempotency check) | Bandingkan status transaksi saat ini dengan status final | `transactions.status`, `transactions.manjo_status_code` | — | Kalau status **sudah final** dan `latestTransactionStatus` yang masuk sama dengan yang tersimpan → duplikat | **Balas `200 OK` langsung, skip ke step 12 (tanpa reprocess, tanpa MQTT publish ulang)** |
| 6b | Transaction Service (anomali) | Status sudah final tapi `latestTransactionStatus` baru **berbeda** dari yang tersimpan | — | Log sebagai **anomali** (butuh investigasi manual, bukan auto-update) | — | Balas `200 OK` (supaya Manjo tidak retry), **tidak** update status, alert tim |
| 7 | Transaction Service | Map `latestTransactionStatus` → status internal (`00`/`03`→`PAID`, `01`→`FAILED`, `02`→`FAILED`, `04`→ tetap `PENDING`/`QR_GENERATED`, `05`→`REFUNDED`, `06`→`CANCELLED`) | — | — | Lihat tabel mapping di `architecture.md` Section 22 | Kode tidak dikenal → log warning, treat sebagai tidak final (tidak update, tunggu notifikasi berikutnya) |
| 8 | Transaction Service (state machine) | Validasi transisi status lama → status baru diizinkan | — | — | Status final tidak boleh berubah kecuali `PAID`→`REFUNDED` (di-enforce juga oleh trigger DB `trg_prevent_final_status_change`) | Trigger DB menolak UPDATE → exception ditangkap, log, balas `200 OK` tapi status tidak berubah |
| 9 | Transaction Service | Update transaksi | — | `transactions`: UPDATE `status`, `manjo_status_code=latestTransactionStatus`, `paid_at=now()` (khusus kalau jadi `PAID`), `updated_at=now()` | — | — |
| 10 | Notification HTTP Endpoint | Balas `200 OK` ke Manjo **segera** (tidak menunggu step 11-13) | — | `manjo_api_logs.http_status=200`, `response_body` (UPDATE record step 3) | Response harus cepat supaya Manjo tidak timeout | — |
| 11 | Audio Sequence Builder | Kalau status baru = `PAID`: konversi `amount` (dari `additionalInfo.amountTrx`, dikonversi balik ke integer Rupiah) jadi array nama file mp3 | `transactions.amount` (atau ambil dari payload notifikasi) | — | Hanya jalan kalau status final `PAID`/`FAILED` (bukan `PENDING`) | — |
| 12 | MQTT Publisher | Build payload `PAYMENT_NOTIFICATION` (`status`, `amount`, `audio_sequence`), lookup `devices.mqtt_topic` via `transactions.device_id`, publish ke topic tsb | `transactions.device_id`, `devices.mqtt_topic` | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `status=PROCESSED`, `transaction_id`) | — | Publish gagal (broker down) → masuk retry queue terpisah, **tidak** mempengaruhi response yang sudah dikirim ke Manjo di step 10. **Catatan:** routing selalu lewat `transactions.device_id` internal, **tidak pernah** dari field Manjo (Payment Notification tidak konsisten membawa info tenant/device) |
| 13 | Q161 Pro | Terima notifikasi, mainkan audio sesuai `audio_sequence` | — | — | — | (di luar scope Service) |

---

## 5. Flow 4 — Auto-Expire Job

**Trigger:** Scheduled job berjalan periodik (mis. tiap 1 menit).
**Berakhir saat:** Semua transaksi yang lewat `expire_at` sudah ditandai `EXPIRED`.

| # | Aktor/Komponen | Aksi | Data Dibaca | Data Ditulis | Validasi/Kondisi | Kalau Gagal |
|---|---|---|---|---|---|---|
| 1 | Scheduler (cron/worker) | Trigger job berjalan | — | — | — | — |
| 2 | Expire Job | Query transaksi kandidat | `transactions` WHERE `status='QR_GENERATED' AND expire_at < now()` (pakai index `idx_transactions_expire_at`) | — | — | — |
| 3 | Expire Job | Untuk tiap kandidat, update status | — | `transactions`: UPDATE `status='EXPIRED'`, `updated_at=now()` WHERE kondisi step 2 (idealnya dalam satu statement `UPDATE ... WHERE ... RETURNING`, bukan loop per-row, untuk menghindari race dengan Flow 3 step 5 yang pakai row lock) | Transaksi yang di tengah jalan sedang di-lock oleh Flow 3 (notifikasi masuk bersamaan) akan menunggu lock lepas — hasil akhir tetap konsisten (salah satu menang) | — |
| 4 | Expire Job | Log ringkasan (berapa transaksi di-expire) | — | (opsional) `manjo_api_logs` tidak relevan di sini — cukup structured log aplikasi, bukan tabel DB | — | — |

> **Catatan:** job ini **tidak** mengirim notifikasi MQTT ke Q161 — transaksi yang expired cukup "hilang" dari sisi status internal; kalau merchant butuh generate QR baru, itu jadi transaksi baru lewat Flow 1. Kalau ke depannya dibutuhkan notifikasi "QR expired" ke device, ini perlu ditambahkan sebagai step baru (belum ada di scope v1, lihat `PRD.md` Non-Goals).

---

## 6. Ringkasan Data Touchpoint per Tabel

| Tabel | Ditulis oleh Flow | Dibaca oleh Flow |
|---|---|---|
| `merchants` | (tidak ditulis oleh flow runtime — diisi lewat proses onboarding merchant terpisah, di luar scope 4 flow ini) | Flow 1 (step 5, 9, 10), Flow 2 (step 2), Flow 3 (step 3) |
| `tenants` | (diisi dari luar, di luar scope 4 flow ini) | Flow 1 (step 5, 10) |
| `devices` | (diisi dari luar, di luar scope 4 flow ini) | Flow 1 (step 5, 10, 13a, 13b), Flow 3 (step 12) |
| `transactions` | Flow 1 (step 6, 9, 11a/11b, 12b), Flow 3 (step 9), Flow 4 (step 3) | Flow 1 (step 5 — via `devices`, step 13a), Flow 3 (step 5, 6a, 6b, 8, 11, 12 — via `device_id`), Flow 4 (step 2) |
| `mqtt_messages` | Flow 1 (step 4b, 7, 13a, 13b), Flow 3 (step 12) | (dibaca terpisah saat tracing manual, bukan bagian dari flow otomatis) |
| `manjo_api_logs` | Flow 1 (step 10, 11a, 11b), Flow 2 (step 3, 4a, 4b), Flow 3 (step 3, 10) | (dibaca terpisah saat tracing manual) |

Tabel di atas berguna untuk menjawab cepat pertanyaan seperti *"kalau saya mau tahu semua tempat yang menulis ke `transactions`, saya harus cek flow mana saja?"* tanpa perlu membaca ulang seluruh dokumen.