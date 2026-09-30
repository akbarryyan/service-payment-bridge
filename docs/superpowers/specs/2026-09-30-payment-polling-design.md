# Design Spec — Payment Polling (Query Payment) & Pengumuman Audio

**Tanggal:** 2026-09-30
**Status:** Disetujui (brainstorming), siap masuk writing-plans
**Scope:** Mendeteksi pembayaran QRIS dengan polling `qr-mpm-query` ke Manjo, lalu membunyikan soundbox Q161 Pro lewat MQTT. Termasuk perbaikan bug `expire_at` dan alat dev `altosim` untuk mensimulasikan pembayaran di UAT.
**Dokumen terkait:** `docs/manjo-api-docs/manjo-api-docs.md` Section 5 (Query Payment), `docs/manjo-collection/BI SNAP UAT/qr-mpm-query.yml`, `docs/manjo-collection/QR Payment.yml` (simulator Alto), `docs/process-flow.md` Flow 1 & 4, `docs/architecture.md` Section 7.3 & 10, `docs/draft-pertanyaan-manjo.md` (pertanyaan terbuka ke Manjo)

---

## 1. Latar Belakang

Generate QR sudah berjalan end-to-end dengan device fisik. Yang belum ada adalah langkah sesudahnya: mengetahui bahwa customer sudah membayar, lalu membunyikan soundbox.

Jalur "resmi" untuk ini adalah Payment Notification (webhook, service code 52). Jalur itu belum bisa dibangun karena masih menunggu jawaban Manjo (URL webhook yang terdaftar, autentikasi, IP sumber), dan service belum punya URL publik. Sebaliknya, **polling Query Payment sudah terbukti bekerja secara manual**: lewat collection BI SNAP UAT, query mengembalikan status pending, lalu `00` Success setelah `qrContent` dibayar di simulator Alto.

Polling hanya butuh panggilan keluar (service → Manjo), sama seperti generate QR. Jadi alur bayar → soundbox bunyi bisa jalan dan diuji dari laptop sekarang juga. Webhook nanti menjadi jalur yang lebih cepat di atasnya, sedangkan polling tetap berfungsi sebagai rekonsiliasi untuk notifikasi yang hilang (Open Decision #7 di `architecture.md`).

## 2. Fakta yang Sudah Diverifikasi

| Aspek | Sumber | Nilai |
|---|---|---|
| Format request Query | Collection `BI SNAP UAT/qr-mpm-query.yml`, diuji user di UAT. Dikonfirmasi berlaku juga di production. | `POST {MANJO_BASE_URL}/v1.0/qr/qr-mpm-query`. Header: `Authorization: Bearer`, `X-TIMESTAMP`, `X-SIGNATURE` (HMAC-SHA512, path `/v1.0/qr/qr-mpm-query`), `X-PARTNER-ID` = **client key** (bukan merchant ID), `X-EXTERNAL-ID`, `CHANNEL-ID`. **Tanpa `X-CLIENT-KEY`**. Body: `originalReferenceNo`, `originalPartnerReferenceNo`, `originalExternalId`, `serviceCode: "47"`, `merchantId`, `additionalInfo.currency: "IDR"`. Berbeda dari dokumentasi (`X-CLIENT-KEY` wajib, `serviceCode: "99"`); collection yang diikuti. |
| Respons sukses | Uji user di UAT | HTTP 200, `latestTransactionStatus: "00"`, `transactionStatusDesc: "Success"`, `paidTime: "2026-09-30T10:57:53+07:00"`, `amount.value`, `terminalId` |
| Respons belum dibayar | Uji user di UAT | HTTP 200, status pending (skema Query: `01`/`02`/`03`) |
| Respons QR kedaluwarsa | Uji user di UAT | **HTTP 403**, `{"responseCode": "4035100", "responseMessage": "Transaction Expire"}`, tanpa `latestTransactionStatus` |
| Masa berlaku QR | User + `manjo_api_logs` | ~7 menit. Respons generate berisi `expiryDuration: "450000"` (milidetik = 7,5 menit), walaupun kita mengirim `validityPeriod: "3600"`. |
| Bug `expireDate` | `manjo_api_logs` | `expireDate` dari Manjo **7 jam lebih lambat** dari seharusnya (QR pukul 09:34 WIB → `20260929164224`). Akibatnya `transactions.expire_at` kita selama ini salah +7 jam. |
| Host | User | UAT: `snap-uat.manjo.co.id` untuk semua endpoint. Production: `snap.manjo.co.id`. Cukup satu `MANJO_BASE_URL`. |
| File audio di device | User + `Kerjaan/Soundbox/docs/mp3/mp3-api/` | Pembuka `awal-qris`, penutup `akhir-berhasil` (sudah mengandung "rupiah"), angka `nol`–`sembilan`, `sepuluh`, `sebelas`, `seratus`, `seribu`, `belas`, `puluh`, `ratus`, `ribu`, `juta`, `miliar`. Payload memakai nama **tanpa** akhiran `_adr` (versi `_adr` hanya format untuk proses flash). |
| Contoh payload audio | User | Rp50.000 → `/ext/awal-qris.mp3+/ext/lima.mp3+/ext/puluh.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3` |
| Logika penyusun audio | `Kerjaan/Soundbox/mqtt-poc/publisher/internal/voice` | Implementasi Go yang sudah teruji: aturan `se-` (sepuluh/sebelas/seratus/seribu), belas/puluh, tingkatan ribu/juta/miliar. Di sana klip pembukanya `notiftest` dan ada klip `rupiah`, jadi keduanya disesuaikan. |
| Simulator Alto | `docs/manjo-collection/QR Payment.yml` | `POST https://mmsapi-test.manjo.co.id/altopay/qr-payment/payment`, `command: "qr-payment-credit"`, signature HMAC-SHA256 atas `POST:/altopay/qr-payment/payment:{apiKey}:{sha256(body)}:{timestamp}` dengan timestamp UTC `YYYY-MM-DD HH:MM:SS.mmmZ`. Header `X-Alto-Timestamp`, `X-Alto-Key`, `X-Alto-Signature`. |

## 3. Keputusan

1. **Polling DB-driven di dalam service** (bukan timer in-memory, bukan job queue eksternal). Jadwal disimpan di DB supaya tahan restart dan aman untuk multi-instance.
2. **Interval 3 detik sepanjang masa berlaku QR.** Dengan masa berlaku ~7,5 menit, satu QR yang tidak dibayar menghasilkan ±150 query. Tidak perlu backoff bertingkat.
3. **Format Query mengikuti collection**, bukan dokumentasi.
4. **Pemetaan status Query dibuat terpisah** dari pemetaan Payment Notification (skema kodenya berbeda, dokumen Manjo Section 5.10).
5. **Audio disusun oleh service** dengan logika `voice` dari `mqtt-poc` yang diadaptasi ke set file `mp3-api`.
6. **Hanya `PAID` yang diumumkan.** `EXPIRED`/`FAILED`/`CANCELLED`/`REFUNDED` tidak mengirim apa pun ke device (konsisten dengan `process-flow.md` Flow 4).
7. **`expire_at` dihitung dari `expiryDuration`**, dengan `expireDate` sebagai cadangan.
8. **Alat dev `altosim`** masuk scope, supaya pengujian bayar → bunyi bisa diulang dengan satu perintah.

## 4. Komponen

| Komponen | Status | Tanggung jawab |
|---|---|---|
| `internal/manjoclient` | Diubah | Method `QueryPayment(ctx, QueryPaymentRequest) (*QueryPaymentResponse, error)`. Non-2xx dikembalikan sebagai `*APIError` (sudah membawa `StatusCode` dan `Body`), yang lalu ditafsirkan oleh `transaction`. |
| `internal/transaction` | Diubah | (a) `CheckPayment(ctx, sqlc.Transaction) (*PaymentCheckResult, error)`: resolve kredensial merchant lewat `device_id`, panggil `QueryPayment`, petakan hasilnya, lalu terapkan transisi status. (b) Perbaikan perhitungan `expire_at` di `GenerateQR`. (c) `MarkTransactionQRGenerated` ikut mengisi `next_query_at`. Tetap satu-satunya komponen yang mengubah status transaksi (`architecture.md` 4.5). |
| `internal/voice` | Baru | `Compose(rupiah) ([]Clip, error)` dan `Payload(rupiah) (string, error)` → `"/ext/awal-qris.mp3+…+/ext/akhir-berhasil.mp3"`. |
| `internal/announcer` | Baru | `Announce(ctx, AnnounceRequest{TransactionID, DeviceID, Rupiah}) error`: susun payload, publish ke `qrtopic.BuildDeviceTopic(deviceID)`, catat ke `mqtt_messages`. Dipisah karena nanti dipakai juga oleh webhook. |
| `internal/paymentpoller` | Baru | `Run(ctx)`: tiap 1 detik klaim transaksi jatuh tempo → `CheckPayment` → kalau baru menjadi `PAID`, panggil `Announce`. |
| `cmd/server` | Diubah | Menjalankan poller sebagai goroutine dan menunggunya selesai saat shutdown. |
| `internal/altosim` + `cmd/altosim` | Baru | Parser EMV/TLV, penyusun request Alto, signature, dan HTTP client. CLI dev, tidak ikut jalan di server. |
| `internal/config` | Diubah | `PAYMENT_POLL_INTERVAL` (default `3s`). |

## 5. Data Model — Migration `000014`

```sql
ALTER TABLE transactions ADD COLUMN next_query_at TIMESTAMPTZ;

CREATE INDEX idx_transactions_next_query_at
    ON transactions (next_query_at)
    WHERE status = 'QR_GENERATED';

-- Transaksi QR_GENERATED lama punya expire_at yang salah (+7 jam, lihat Section 2).
-- expire_at dibetulkan ke masa berlaku yang diamati (7,5 menit), lalu dicek sekali oleh
-- poller: Manjo menjawab 403 "Transaction Expire" → EXPIRED; kalau jawabannya lain,
-- jaring pengaman (Section 7) langsung menandainya EXPIRED karena deadline sudah lewat.
UPDATE transactions
SET expire_at = created_at + interval '7 minutes 30 seconds',
    next_query_at = now()
WHERE status = 'QR_GENERATED';
```

- Diisi `now() + PAYMENT_POLL_INTERVAL` oleh `MarkTransactionQRGenerated`.
- Dimajukan lagi setiap kali transaksi diklaim poller.
- Di-`NULL`-kan saat transaksi pindah ke status final.
- Down migration: `DROP INDEX` lalu `DROP COLUMN`.

Tidak ada kolom "sudah diumumkan". Jejak pengumuman cukup dari baris `mqtt_messages` (OUTBOUND, dengan `transaction_id`).

## 6. Alur Poller

Setiap 1 detik:

1. **Klaim** hingga 20 transaksi dalam satu statement. Jadwal berikutnya langsung dimajukan, jadi transaksi yang gagal diproses (error atau crash) otomatis dicoba lagi pada siklus berikutnya:
   ```sql
   UPDATE transactions SET next_query_at = now() + @interval
   WHERE transaction_id IN (
     SELECT transaction_id FROM transactions
     WHERE status = 'QR_GENERATED' AND next_query_at <= now()
     ORDER BY next_query_at LIMIT 20
     FOR UPDATE SKIP LOCKED)
   RETURNING *;
   ```
   `SKIP LOCKED` menjamin satu transaksi tidak diklaim oleh dua instance sekaligus.
2. **Proses** maksimal 8 transaksi paralel. Tiap transaksi dijalankan lewat `CheckPayment`. Error di satu transaksi tidak memengaruhi transaksi lain.
3. **Terapkan** perubahan status dengan UPDATE bersyarat:
   ```sql
   UPDATE transactions SET status = @new_status, manjo_status_code = @code,
          paid_at = @paid_at, next_query_at = NULL
   WHERE transaction_id = @id AND status = 'QR_GENERATED'
   RETURNING *;
   ```
   Hanya pemanggil yang mendapat 1 baris hasil yang dianggap melakukan transisi, dan hanya dia yang memicu pengumuman. **Jaminan: satu pembayaran = satu pengumuman**, termasuk kalau service dijalankan beberapa instance.
4. **Umumkan** kalau hasilnya transisi ke `PAID` (Section 9).

Resolve kredensial tetap dilakukan untuk device atau merchant yang sudah `INACTIVE`: pembayaran yang sudah terjadi harus tetap terdeteksi.

## 7. Pemetaan Hasil Query

Fungsi `mapQueryResult`, terpisah dari pemetaan Payment Notification:

| Respons Manjo | Hasil | `manjo_status_code` | Diumumkan |
|---|---|---|---|
| 200, `00` | `PAID`, `paid_at` = `paidTime` (fallback `now()`) | `00` | Ya |
| 200, `01` / `02` / `03` | Tetap `QR_GENERATED` | — | — |
| 200, `05` | `CANCELLED` | `05` | Tidak |
| 200, `06` | `FAILED` | `06` | Tidak |
| 200, `04` | `REFUNDED` + log anomali (tidak wajar untuk QR yang belum lunas) | `04` | Tidak |
| 403 dengan `responseCode` `4035100` | `EXPIRED` | tidak diubah | Tidak |
| 200 `07`, atau 404 | Tetap, dicoba lagi | — | — |
| 401 | Token di-invalidate, dicoba lagi di siklus berikutnya | — | — |
| Timeout / 5xx / kode lain / body tidak terbaca | Tetap, dicoba lagi + log warning | — | — |

**Jaring pengaman:** kalau setelah hasil diterapkan transaksi masih `QR_GENERATED` dan `now() > expire_at + 2 menit`, transaksi ditandai `EXPIRED` dengan log warning. Ini mencegah polling tanpa akhir kalau query terus error atau Manjo terus menjawab not found.

`manjo_status_code` menyimpan kode mentah dari skema Query. Kolom ini nanti juga dipakai untuk kode dari Payment Notification, yang skemanya berbeda. Asal kodenya tetap bisa dilacak dari `manjo_api_logs.operation`.

**Nominal yang diumumkan** memakai `transactions.amount`. Kalau `amount.value` dari Manjo berbeda (setelah dibulatkan ke Rupiah), pengumuman tetap memakai nominal kita, dan ketidakcocokannya dicatat sebagai warning `"amount mismatch"`.

## 8. Perbaikan `expire_at`

Di `transaction.GenerateQR`, dengan `receivedAt` = waktu respons generate diterima:

1. `expiryDuration` berupa angka > 0 → `expire_at = receivedAt + expiryDuration milidetik`.
2. Kalau tidak ada atau tidak valid → parse `expireDate` sebagai WIB (perilaku lama).
3. Kalau dua-duanya gagal → `receivedAt + 10 menit` (sedikit di atas masa berlaku yang diamati, supaya poller tidak berhenti terlalu cepat).

Satuan milidetik adalah kesimpulan dari data (450000 ≈ 7,5 menit ≈ "7 menit" yang dikonfirmasi user). Kepastiannya masih ditanyakan ke Manjo di `docs/draft-pertanyaan-manjo.md`.

## 9. Pengumuman Audio

- Payload dari `voice.Payload(rupiah)`:
  - Pola: `/ext/awal-qris.mp3` + klip nominal + `/ext/akhir-berhasil.mp3`, dipisah `+`.
  - Contoh: Rp1.000 → `…awal-qris…+/ext/seribu.mp3+…akhir-berhasil…`; Rp125.500 → `seratus dua puluh lima ribu lima ratus`.
  - Nominal ≤ 0 atau di atas batas `voice` (999.999.999.999) → error. Tidak ada yang dipublish, dan kejadiannya di-log.
- Publish ke `topic_{device_id}` dengan QoS 1 lewat `mqttclient.Publish`. Device subscribe QoS 1 dengan `cleansession=0`, jadi broker menyimpan pesan selama device sempat offline.
- Kalau publish gagal, dicoba ulang maksimal 3 kali dengan jeda 1 detik. Hasil akhirnya dicatat ke `mqtt_messages` (OUTBOUND, `PROCESSED`/`FAILED`, `transaction_id`).
- **Celah yang diterima:** kalau service mati tepat di antara transisi `PAID` dan publish, pengumuman hilang. Jendelanya milidetik, dan kejadiannya tetap bisa dilacak sebagai transaksi `PAID` tanpa baris OUTBOUND di `mqtt_messages`. Query pelacaknya didokumentasikan di `docs/running-locally.md`. Menutup celah ini butuh pola outbox, dan itu di luar scope.

## 10. Logging & Observability

- **`manjo_api_logs` (operasi `QUERY_PAYMENT`, dengan `transaction_id`)** hanya mencatat hasil yang **bukan** "masih pending": transisi status, error HTTP/jaringan, 401, dan kode tak dikenal. Poll pending tidak dicatat, supaya satu QR yang tidak dibayar tidak menghasilkan ~150 baris log.
- **Log service (slog):**
  - `"payment detected"` (transaction_id, device_id, amount)
  - `"transaction expired"` (transaction_id, via = `manjo` / `deadline`)
  - `"payment query failed"` (transaction_id, error)
  - `"announcement failed"` (transaction_id, device_id, error)
  - `"amount mismatch"`
  - `"payment poll claim failed"` (error)

## 11. Error Handling & Shutdown

- Klaim dari DB gagal → log error, lanjut ke siklus berikutnya. Poller tidak pernah mematikan server.
- `CheckPayment` error untuk satu transaksi → log, lalu transaksi itu dicoba lagi 3 detik kemudian (jadwal sudah dimajukan saat klaim).
- **Shutdown:** konteks poller dibatalkan, jadi tidak ada klaim baru. Transaksi yang sedang diproses ditunggu sampai selesai, termasuk pengumumannya, dalam batas 10 detik shutdown yang sudah ada di `main.go`.

## 12. Konfigurasi

| Env var | Default | Keterangan |
|---|---|---|
| `PAYMENT_POLL_INTERVAL` | `3s` | Jarak antar query untuk satu transaksi |
| `ALTO_BASE_URL` | `https://mmsapi-test.manjo.co.id` | Khusus `altosim` |
| `ALTO_API_KEY`, `ALTO_VALIDATION_KEY` | — | Khusus `altosim`, di `.env.sandbox` |

Tick (1 detik), ukuran batch (20), jumlah worker (8), jumlah retry publish (3), dan jaring pengaman (2 menit) dibuat sebagai konstanta.

## 13. Alat `altosim`

```
go run ./cmd/altosim -tx TRX-20260930-XXXXXX   # qris_payload diambil dari DB (DATABASE_URL)
go run ./cmd/altosim -qr "00020101021226..."    # atau qrContent langsung
```

Memuat `.env` dan `.env.sandbox` seperti server. Body request disusun dari tag EMV `qrContent`:

| Field Alto | Sumber |
|---|---|
| `amount` | Tag `54`, dibulatkan ke integer Rupiah |
| `additional_data` | Nilai tag `62` utuh (berisi reference label `05` = `referenceNo` Manjo) |
| `terminal_label` | Tag `62` sub `07` |
| `national_mid`, `merchant.id` | Tag `26` sub `02` |
| `merchant.pan` | Tag `26` sub `01` |
| `merchant.criteria` | Tag `26` sub `03` |
| `merchant.name` / `city` / `mcc` / `postal_code` / `country_code` | Tag `59` / `60` / `52` / `61` / `58` |
| `acquirer_nns` | 8 digit awal `merchant.pan` |
| `issuer_nns`, `customer.*`, `fee`, `currency_code` | Meniru collection: `93600821`, customer `pan` = PAN merchant, `name: "Tes"`, `account_type: "UNSPECIFIED"`, `fee: 0`, `IDR` |
| `date_time` | Sama dengan `X-Alto-Timestamp` |
| `customer_reference_number` | `"ALTO-API-NMS-"` + 12 karakter hex acak |
| `forwarding_customer_reference_number` | 12 karakter hex acak |
| `authorization_id` | 6 karakter hex kapital acak |

Signature dihitung dari byte body yang benar-benar dikirim. Output CLI: ringkasan (merchant, nominal, reference label) dan respons Alto mentah.

Pemetaan ini disimpulkan dari collection dan isi QR asli. Kebenarannya divalidasi saat uji manual pertama di UAT.

## 14. Testing

| Level | Cakupan |
|---|---|
| Unit | `voice` (Rp50.000, Rp1.000, Rp125.500, Rp10/11/12/20/100/1.000.000, error untuk ≤ 0). `mapQueryResult` (table-driven, semua baris Section 7). Perhitungan `expire_at` (sampel UAT `450000` → 7m30s, fallback `expireDate`, fallback 10 menit). Parser EMV `altosim` dengan **QR asli dari DB** sebagai fixture. Signature Alto (known-answer). |
| `manjoclient` (httptest) | Header dan body `QueryPayment` sesuai Section 2. Signature path `/v1.0/qr/qr-mpm-query`. Respons 200 dan 403 `4035100` terbaca. |
| `transaction` (DB + httptest) | `00` → `PAID` hanya sekali (panggilan kedua tidak ada transisi). 403 `4035100` → `EXPIRED`. Pending → tidak berubah. Lewat jaring pengaman → `EXPIRED`. `GenerateQR` mengisi `next_query_at` dan `expire_at` yang benar. |
| `paymentpoller` (DB + Mosquitto + mock Manjo) | Transaksi dibayar → payload audio yang benar diterima di `topic_{device}`, status `PAID`, dan tercatat di `mqtt_messages`. **Dua poller berjalan bersamaan pada data yang sama → tepat satu pengumuman.** |
| Manual UAT | Generate dari device → `go run ./cmd/altosim -tx …` → soundbox bunyi dalam ~3 detik dan status `PAID`. Juga: QR dibiarkan sampai ~7,5 menit → `EXPIRED` tanpa bunyi. |

Semua test integrasi memakai merchant/device test sendiri, dan cleanup-nya tidak menyentuh data asli (pola dari commit `aca684d`). Server harus dimatikan saat test dijalankan, karena poller server bisa ikut mengklaim transaksi test.

## 15. Dokumen yang Diperbarui

- `docs/running-locally.md`: env var baru, cara memakai `altosim`, alur uji manual bayar → bunyi, dan query pelacak transaksi `PAID` tanpa pengumuman.
- `.env.sandbox.example`: `ALTO_BASE_URL`, `ALTO_API_KEY`, `ALTO_VALIDATION_KEY`.
- `docs/process-flow.md`: flow baru "Payment Polling", dan Flow 4 (auto-expire) disesuaikan karena kedaluwarsa kini dideteksi lewat query.
- `docs/architecture.md` Section 7.3: format payload notifikasi pembayaran yang dipakai (plain text path `.mp3` dipisah `+` ke `topic_{device_id}`), menggantikan draf JSON lama.

## 16. Non-Goals

- Webhook Payment Notification (service code 52) dan endpoint token Tahap 1. Menunggu jawaban Manjo.
- Suara untuk status selain `PAID`.
- Pola outbox untuk menutup celah crash di Section 9.
- Memakai Query Payment untuk menangani 409 di generate QR. Sekarang jadi mungkin, tapi dikerjakan terpisah.
- Memindahkan kredensial simulator Alto yang sudah ter-commit di `docs/manjo-collection/QR Payment.yml`. Sudah dicatat sebagai temuan keamanan ke user.

## 17. Pertanyaan Terbuka (ke Manjo)

Dari `docs/draft-pertanyaan-manjo.md` Section 2. Desain ini tidak terblokir oleh pertanyaan-pertanyaan ini:

- Rate limit polling. Interval bisa diubah lewat `PAYMENT_POLL_INTERVAL`.
- Satuan `expiryDuration` dan bug +7 jam pada `expireDate`.
- Apakah `validityPeriod` punya pengaruh.
