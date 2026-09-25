# QA Strategy & Test Plan — Q161 Pro × Manjo Payment Bridge Service

**Versi:** 1.0
**Status:** Draft
**Scope:** Backend Payment Bridge Service (tidak mencakup device Q161 fisik)
**Dokumen terkait:** `architecture.md`, `schema.md`, `PRD.md`, `manjo-api-docs.md`

---

## Daftar Isi

**Bagian A — QA Strategy**
1. [Tujuan & Ruang Lingkup](#1-tujuan--ruang-lingkup)
2. [Test Levels](#2-test-levels)
3. [Test Environment](#3-test-environment)
4. [Tools & Framework](#4-tools--framework)
5. [Entry & Exit Criteria](#5-entry--exit-criteria)
6. [Defect Severity Classification](#6-defect-severity-classification)
7. [Test Data Management](#7-test-data-management)
8. [Matriks Mock vs Sandbox](#8-matriks-mock-vs-sandbox)

**Bagian B — Test Cases**
9. [TC-GQR — Generate QR Flow](#9-tc-gqr--generate-qr-flow)
10. [TC-PN — Payment Notification Flow](#10-tc-pn--payment-notification-flow)
11. [TC-QRY — Query Payment Flow](#11-tc-qry--query-payment-flow)
12. [TC-IDM — Idempotency](#12-tc-idm--idempotency)
13. [TC-CONC — Concurrency & Race Condition](#13-tc-conc--concurrency--race-condition)
14. [TC-SIG — Signature Verification](#14-tc-sig--signature-verification)
15. [TC-TOKEN — Token Lifecycle](#15-tc-token--token-lifecycle)
16. [TC-ERR — Error Handling](#16-tc-err--error-handling)
17. [TC-SM — State Machine Transitions](#17-tc-sm--state-machine-transitions)
18. [TC-EXP — Auto-Expire Job](#18-tc-exp--auto-expire-job)
19. [TC-SB — Sandbox Manjo (End-to-End Nyata)](#19-tc-sb--sandbox-manjo-end-to-end-nyata)
20. [Ringkasan Cakupan Test Case](#20-ringkasan-cakupan-test-case)

---

# Bagian A — QA Strategy

## 1. Tujuan & Ruang Lingkup

Dokumen ini mendefinisikan strategi pengujian dan daftar test case untuk **Payment Bridge Service**, memastikan sistem memenuhi acceptance criteria di `PRD.md` sebelum rilis.

**In Scope:**
- Seluruh komponen backend Service: MQTT Consumer/Publisher, Manjo Client, Transaction Service, Notification HTTP Endpoint, Merchant Resolver, Message Validator.
- Interaksi dengan Manjo API — baik lewat mock maupun sandbox asli (lihat [Section 8](#8-matriks-mock-vs-sandbox)).
- Interaksi dengan MQTT — disimulasikan lewat broker test lokal (mis. Mosquitto di Docker) atau in-memory test client, payload dari sisi Q161 dikirim/dibaca sebagai simulasi, **bukan** dari device fisik.

**Out of Scope:**
- Perilaku fisik device Q161 (rendering QR, playback audio) — itu ranah pengujian firmware terpisah.
- Uji beban skala besar (load testing ribuan device bersamaan) — bisa jadi dokumen QA terpisah kalau dibutuhkan nanti.
- Uji keamanan penetrasi formal (pentest) — di luar scope QA fungsional ini.

## 2. Test Levels

| Level | Cakupan | Contoh |
|---|---|---|
| **Unit Test** | Fungsi/komponen individual, terisolasi dari DB/network | Signature Builder (SHA256withRSA, HMAC-SHA512), state machine transition validator, audio-sequence builder, amount formatter (`50000` → `"50000.00"`) |
| **Integration Test** | Interaksi antar komponen + database nyata (test DB), Manjo API di-mock | Transaction Service lengkap (create → generate QR → update dari notifikasi), Merchant Resolver dengan DB, idempotency check dengan DB nyata |
| **End-to-End (E2E) Test** | Seluruh flow dari pesan MQTT masuk sampai MQTT keluar, termasuk panggilan API Manjo | Generate QR flow penuh, Payment Notification flow penuh — baik dengan mock Manjo maupun sandbox asli |

## 3. Test Environment

```text
┌─────────────────────────────────────────────────────────┐
│                   Test Environment                      │
│                                                          │
│  ┌──────────────┐      ┌────────────────────────────┐   │
│  │ Mock MQTT     │◄────►│  Payment Bridge Service      │   │
│  │ Broker         │      │  (build test / test binary) │   │
│  │ (Mosquitto     │      └───────────┬──────────────────┘   │
│  │  di Docker,    │                  │                     │
│  │  atau in-proc  │                  ▼                     │
│  │  test client)  │      ┌────────────────────┐            │
│  └──────────────┘      │  Test Database       │            │
│                          │  (PostgreSQL,        │            │
│                          │   schema.md, di-reset │            │
│                          │   tiap test run)      │            │
│                          └────────────────────┘            │
│                                      │                     │
│                          ┌───────────▼──────────┐          │
│                          │  Manjo — dua mode:     │          │
│                          │  1. Mock HTTP server   │          │
│                          │     (httptest, respons │          │
│                          │      dikontrol test)   │          │
│                          │  2. Sandbox Manjo asli │          │
│                          │     (kredensial sandbox)│          │
│                          └───────────────────────┘          │
└─────────────────────────────────────────────────────────┘
```

- **Mock MQTT Broker**: broker lokal (Mosquitto via Docker Compose) yang di-spin-up khusus untuk test run, atau in-process MQTT test client untuk unit/integration test yang tidak butuh broker sungguhan.
- **Mock Manjo Server**: HTTP server tiruan (`httptest.Server` di Go) yang meniru response Manjo — dipakai untuk skenario yang butuh kontrol penuh atas response (sukses, error spesifik, timeout, delay).
- **Sandbox Manjo**: environment sandbox resmi dari Manjo (kalau sudah tersedia aksesnya) — dipakai untuk test case yang ditandai `[SANDBOX]`, memverifikasi kontrak API benar-benar sesuai implementasi nyata Manjo, bukan asumsi dari dokumentasi.
- **Test Database**: instance PostgreSQL terpisah dari production/development, di-reset (migration ulang / truncate) sebelum tiap test suite run untuk memastikan isolasi antar test.

## 4. Tools & Framework

| Kebutuhan | Tools |
|---|---|
| Test runner & assertion | `testing` (stdlib Go) + `testify/assert`, `testify/require` |
| Mocking | `testify/mock` atau `gomock`, `httptest` (stdlib) untuk mock Manjo HTTP |
| MQTT test broker | Mosquitto (Docker), atau `testcontainers-go` untuk spin-up otomatis |
| Database test | `testcontainers-go` (Postgres container) atau test DB dedicated + `golang-migrate` |
| Concurrency test | Go `-race` flag (race detector) wajib aktif di seluruh test run yang menyentuh Transaction Service |
| CI Integration | Test suite (unit + integration) jalan otomatis di setiap PR; E2E dengan sandbox dijalankan terpisah (manual trigger atau schedule, karena tergantung ketersediaan sandbox eksternal) |

## 5. Entry & Exit Criteria

**Entry Criteria** (sebelum test dimulai):
- Kode sudah lulus lint/build tanpa error.
- Test database & mock MQTT broker berhasil di-provision.
- Kredensial sandbox Manjo (untuk test bertanda `[SANDBOX]`) sudah tersedia di environment test — kalau belum ada, test tersebut di-skip dengan catatan jelas, bukan dianggap gagal.

**Exit Criteria** (sebelum dianggap siap rilis, selaras dengan Acceptance Criteria `PRD.md` Section 16):
- 100% unit & integration test **Must**-priority lulus.
- Semua test di [Section 12 Idempotency](#12-tc-idm--idempotency) dan [Section 13 Concurrency](#13-tc-conc--concurrency--race-condition) lulus tanpa exception — ini non-negotiable karena menyangkut duplikasi transaksi.
- Minimal satu kali full run E2E lawan sandbox Manjo asli ([Section 19](#19-tc-sb--sandbox-manjo-end-to-end-nyata)) sukses tanpa defect Critical/High terbuka.
- Tidak ada defect **Critical** yang belum di-resolve.

## 6. Defect Severity Classification

| Severity | Definisi | Contoh |
|---|---|---|
| **Critical** | Menyebabkan transaksi diproses ganda, kehilangan data transaksi, atau kredensial bocor | Notifikasi duplikat memicu MQTT publish dua kali; private key ter-log plaintext |
| **High** | Fungsi utama gagal tapi tidak menyebabkan duplikasi/kebocoran | Generate QR gagal terus-menerus akibat bug signature; status transaksi salah map |
| **Medium** | Fungsi sekunder terganggu, ada workaround | Auto-expire job telat jalan; log tracing tidak lengkap |
| **Low** | Kosmetik atau tidak berdampak fungsional | Format log kurang rapi, pesan error kurang deskriptif |

## 7. Test Data Management

- **Kredensial sandbox Manjo** (client_id, private key sandbox, client secret sandbox) disimpan di secret store khusus test environment (mis. `.env.test` yang di-gitignore, atau secret manager test), **tidak pernah** di-commit ke repository maupun ditulis di dalam dokumen test case ini.
- **Data merchant test**: gunakan `merchant_id` dengan prefix jelas (mis. `MT_TEST_...`) untuk membedakan dari data production, memudahkan cleanup.
- **Amount test**: gunakan nominal kecil dan bervariasi (mis. `1000`, `50000`, `1234567`) termasuk edge case (`amount = 0` harus ditolak validator, amount sangat besar untuk cek batas `String(16,2)` di Manjo).
- Test data di-*seed* ulang / dibersihkan otomatis di awal tiap test run (tidak ada dependency antar test run).

## 8. Matriks Mock vs Sandbox

| Kategori Test | Mock (wajib) | Sandbox Manjo (tambahan) |
|---|---|---|
| Generate QR — happy path | ✅ | ✅ `[SANDBOX]` — verifikasi `qrContent` benar-benar valid & bisa di-decode |
| Generate QR — error handling | ✅ (semua kode error disimulasikan) | Sebagian — hanya kalau sandbox bisa memicu error tertentu (mis. invalid merchant) |
| Payment Notification — happy path | ✅ (payload notifikasi disimulasikan manual) | ✅ `[SANDBOX]` — kalau sandbox mendukung simulasi pembayaran end-to-end |
| Idempotency & Concurrency | ✅ (kontrol penuh timing lewat mock) | Tidak wajib — sulit direproduksi konsisten di sandbox nyata |
| Signature verification | ✅ | ✅ `[SANDBOX]` — memastikan formula signature cocok dengan implementasi nyata Manjo (bukan hanya sesuai dokumentasi) |
| Token lifecycle | ✅ (expiry disimulasikan dengan clock injection) | ✅ `[SANDBOX]` — verifikasi `expiresIn` sungguhan = 900 detik dan token benar-benar bisa dipakai |
| Query Payment (status pull) | ✅ (status Manjo disimulasikan lewat mock, termasuk skema kode `00`–`07` yang berbeda dari Notification) | ✅ `[SANDBOX]` — verifikasi format request/response nyata & signature diterima Manjo |
| Auto-expire job | ✅ | Tidak relevan (murni logic internal) |

---

# Bagian B — Test Cases

## 9. TC-GQR — Generate QR Flow

| ID | Judul | Precondition | Langkah | Expected Result | Prioritas | Mode |
|---|---|---|---|---|---|---|
| TC-GQR-01 | Generate QR sukses, happy path | Merchant `ACTIVE` terdaftar, token belum ada di cache | Kirim pesan MQTT `GENERATE_QR` amount=50000 | Transaksi dibuat `PENDING`→`QR_GENERATED`; `qris_payload`, `reference_no`, `expire_at` terisi; MQTT `QR_RESULT` status `SUCCESS` terkirim ke topic yang benar | Must | Mock + Sandbox |
| TC-GQR-02 | Amount = 0 ditolak | — | Kirim `GENERATE_QR` amount=0 | Request ditolak di Message Validator, tidak ada panggilan ke Manjo, tidak ada record transaksi dibuat | Must | Mock |
| TC-GQR-03 | Amount negatif ditolak | — | Kirim `GENERATE_QR` amount=-1000 | Sama seperti TC-GQR-02 | Must | Mock |
| TC-GQR-04 | Merchant tidak dikenal | Topic `topic_MT_UNKNOWN` tidak ada di tabel `merchants` | Kirim `GENERATE_QR` ke topic tsb | Pesan direject di Merchant Resolver, dicatat di `mqtt_messages` dengan `status=FAILED` | Must | Mock |
| TC-GQR-05 | Merchant `INACTIVE` | Merchant ada tapi `status=INACTIVE` | Kirim `GENERATE_QR` | Request ditolak, tidak diteruskan ke Manjo | Must | Mock |
| TC-GQR-06 | Format payload rusak | — | Kirim payload JSON invalid/field `amount` hilang | Message Parser/Validator reject, log error tercatat, tidak crash | Must | Mock |
| TC-GQR-07 | `transaction_id` unik antar request | Kirim 2 request generate QR berurutan dari merchant sama | — | Dua `transaction_id` berbeda dihasilkan, tidak ada collision | Must | Mock |
| TC-GQR-08 | Konversi amount ke format Manjo | amount=50000 (integer) | Cek request body yang dikirim ke Manjo | `amount.value = "50000.00"`, `amount.currency = "IDR"` (string, 2 desimal) | Must | Mock |
| TC-GQR-09 | `dynamicAmount` selalu `"N"` untuk Q161 | — | Cek request body generate QR | `additionalInfo.dynamicAmount = "N"` | Must | Mock |

## 10. TC-PN — Payment Notification Flow

| ID | Judul | Precondition | Langkah | Expected Result | Prioritas | Mode |
|---|---|---|---|---|---|---|
| TC-PN-01 | Notifikasi sukses (`latestTransactionStatus=00`), happy path | Transaksi ada, status `QR_GENERATED` | Kirim POST notifikasi valid dengan status `00` | Status transaksi → `PAID`, `paid_at` terisi, MQTT `PAYMENT_NOTIFICATION` terkirim ke Q161, response `200 OK` ke Manjo | Must | Mock + Sandbox |
| TC-PN-02 | Notifikasi sukses (`latestTransactionStatus=03`) | Sama seperti di atas | Kirim dengan status `03` | Sama seperti TC-PN-01 (`03` di-treat sebagai `PAID` — lihat catatan open question) | Must | Mock |
| TC-PN-03 | Notifikasi gagal (`01` Failed) | Transaksi `QR_GENERATED` | Kirim notifikasi status `01` | Status → `FAILED`, MQTT notifikasi status `FAILED` terkirim | Must | Mock |
| TC-PN-04 | Notifikasi pending (`04`) tidak trigger MQTT | Transaksi `QR_GENERATED` | Kirim notifikasi status `04` | `manjo_status_code` terupdate, status internal tetap `PENDING`/`QR_GENERATED` (bukan final), **tidak ada** publish MQTT ke Q161 | Must | Mock |
| TC-PN-05 | `transaction_id` tidak ditemukan | `originalPartnerReferenceNo` tidak match transaksi manapun | Kirim notifikasi dengan reference asing | Ditolak dengan error jelas (bukan crash), dicatat di log, **tidak** membuat transaksi baru | Must | Mock |
| TC-PN-06 | Response ke Manjo tidak menunggu MQTT publish | Broker MQTT disimulasikan lambat/delay tinggi | Kirim notifikasi valid | Response `200 OK` ke Manjo tetap cepat (< batas waktu yang ditentukan), MQTT publish tetap terjadi di background/retry queue | Should | Mock |
| TC-PN-07 | Refund (`05`) dipetakan dengan benar | Transaksi berstatus `PAID` | Kirim notifikasi status `05` | Status → `REFUNDED` | Should | Mock |
| TC-PN-08 | Cancelled (`06`) dipetakan dengan benar | Transaksi `QR_GENERATED` | Kirim notifikasi status `06` | Status → `CANCELLED` | Should | Mock |

## 11. TC-QRY — Query Payment Flow

> Mencakup Query Payment (Service Code `51`, `manjo-api-docs.md` Section 5) — dipakai sebagai verifikasi status sebelum retry generate QR pasca-`409` (lihat TC-IDM-04/TC-ERR-05) dan/atau reconciliation opsional (FR-17, `PRD.md`). **Fokus utama kategori ini: memastikan skema status Query (`00`–`07`) tidak pernah tertukar dengan skema status Payment Notification (`00`–`06`)** — keduanya sama-sama memakai field `latestTransactionStatus` tapi arti kodenya berbeda (lihat `manjo-api-docs.md` Section 5.10).

| ID | Judul | Precondition | Langkah | Expected Result | Prioritas | Mode |
|---|---|---|---|---|---|---|
| TC-QRY-01 | Query sukses, transaksi ditemukan berstatus Success | Transaksi `PAID` di DB internal, `reference_no`/`external_id` tersimpan | Panggil Query Payment dengan `originalReferenceNo`/`originalPartnerReferenceNo` transaksi tsb | Response `200 OK`, `latestTransactionStatus=00` dipetakan ke `PAID` lewat mapping **khusus Query** (bukan mapping Notification) | Must | Mock + Sandbox |
| TC-QRY-02 | Query transaksi tidak ditemukan | `originalReferenceNo` asing/tidak pernah ada | Panggil Query Payment | Response `404`/`latestTransactionStatus=07` ("Not found"), Service tidak mengubah status transaksi manapun, tidak crash | Must | Mock |
| TC-QRY-03 | Query dipakai untuk verifikasi sebelum retry pasca-`409` | Mock Manjo balas `409` saat generate QR (skenario sama dengan TC-IDM-04/TC-ERR-05) | Service memanggil Query Payment sebelum memutuskan retry dengan `X-EXTERNAL-ID` baru | Kalau Query menunjukkan transaksi sebelumnya sudah sukses (`00`) → Service **tidak** membuat transaksi duplikat, langsung update status dari hasil Query; kalau belum ada transaksi tercatat di Manjo → retry generate QR baru dilanjutkan | Must | Mock |
| TC-QRY-04 | Mapping status Query dan Notification tidak boleh memakai fungsi yang sama (regression test) | — | Panggil fungsi mapping status Query dengan kode `03` (harus jadi `PENDING`), dan fungsi mapping status Notification dengan kode `03` (harus jadi `PAID`), di unit test yang sama | Kedua fungsi menghasilkan output **berbeda** untuk kode input yang sama (`03`); assert eksplisit bahwa keduanya bukan referensi ke fungsi/tabel mapping yang sama | Must | Mock (unit test) |
| TC-QRY-05 | Header `X-CLIENT-KEY` wajib dikirim bersama `Authorization` Bearer | — | Cek request yang dikirim Manjo Client ke `/v1.0/qr/qr-mpm-query` | Header `X-CLIENT-KEY` dan `Authorization: Bearer <token>` sama-sama ada (beda dari Generate QR yang hanya butuh `Authorization`) | Must | Mock |
| TC-QRY-06 | `paidTime` hanya diproses untuk status `00` Success | Mock Manjo balas status `03` (Pending) tanpa `paidTime` | Panggil Query Payment | Service **tidak** mengisi/menimpa `paid_at` dari response yang bukan status Success | Should | Mock |
| TC-QRY-07 | Reconciliation job memanggil Query untuk transaksi mendekati/lewat `expire_at` | Transaksi `QR_GENERATED`, `expire_at` lewat, belum ada notifikasi masuk, reconciliation job **diimplementasikan** (tergantung Open Decision #7 `architecture.md` Section 20) | Jalankan reconciliation job | Job memanggil Query Payment dulu sebelum job auto-expire (TC-EXP-01) menandai `EXPIRED`; kalau Query menunjukkan sudah `PAID`, transaksi diupdate `PAID` bukan `EXPIRED` | Should | Mock — *(skip kalau reconciliation job tidak masuk scope v1)* |
| TC-QRY-08 | Signature & format request Query diterima sandbox asli | — | Kirim request Query Payment ke sandbox dengan signature dari implementasi Service | Sandbox membalas `200`/`404` sesuai kondisi transaksi (bukan `401`), memverifikasi formula HMAC-SHA512 dan header `X-CLIENT-KEY` cocok dengan implementasi nyata Manjo | Must | `[SANDBOX]` |

## 12. TC-IDM — Idempotency

> Kategori paling kritis — exit criteria mewajibkan seluruh test di bagian ini lulus tanpa pengecualian.

| ID | Judul | Precondition | Langkah | Expected Result | Prioritas | Mode |
|---|---|---|---|---|---|---|
| TC-IDM-01 | Notifikasi identik dikirim 2x berurutan | Transaksi `QR_GENERATED` | Kirim notifikasi status `00` dua kali (payload identik, berurutan) | Status berubah `PAID` di percobaan pertama; percobaan kedua: `200 OK` dibalas, **tidak ada** MQTT publish kedua, **tidak ada** perubahan `paid_at` | Must | Mock |
| TC-IDM-02 | Notifikasi dikirim ulang setelah status sudah final berbeda | Transaksi sudah `FAILED` | Kirim notifikasi status `00` (mencoba override) | Ditolak/diabaikan (status final tidak berubah), dicatat sebagai anomali di log untuk investigasi manual | Must | Mock |
| TC-IDM-03 | Retry generate QR dengan `X-EXTERNAL-ID` baru, `transaction_id` sama | Request generate QR pertama timeout (disimulasikan) | Service retry dengan `X-EXTERNAL-ID` baru | `transaction_id` tetap sama, tidak ada record transaksi duplikat di DB | Must | Mock |
| TC-IDM-04 | Manjo balas `409` saat generate QR | Mock Manjo dikonfigurasi balas `409` | Kirim `GENERATE_QR` | Service cek status transaksi dulu sebelum retry (bukan langsung anggap gagal) via Query Payment — lihat [TC-QRY-03](#11-tc-qry--query-payment-flow); tidak membuat transaksi duplikat | Must | Mock |
| TC-IDM-05 | Idempotency check pakai row lock | Dua notifikasi identik dikirim nyaris bersamaan (lihat juga TC-CONC-01) | — | Hanya satu yang berhasil memproses & publish MQTT, yang kedua terdeteksi sebagai duplikat | Must | Mock |

## 13. TC-CONC — Concurrency & Race Condition

| ID | Judul | Precondition | Langkah | Expected Result | Prioritas | Mode |
|---|---|---|---|---|---|---|
| TC-CONC-01 | Dua notifikasi untuk `transaction_id` sama, dikirim benar-benar paralel (goroutine terpisah) | Transaksi `QR_GENERATED` | Fire dua request HTTP notifikasi secara simultan | Hanya satu yang mengubah status & publish MQTT (row lock/`SELECT FOR UPDATE` bekerja); tidak ada dua kali `PAID` update atau dua kali MQTT publish | Must | Mock, jalankan dengan `-race` |
| TC-CONC-02 | Banyak merchant generate QR bersamaan | 5+ merchant `ACTIVE` | Fire request generate QR paralel dari tiap merchant | Semua berhasil diproses independen, tidak ada data merchant tertukar (cross-contamination) | Must | Mock |
| TC-CONC-03 | Satu merchant, banyak transaksi aktif bersamaan | 1 merchant | Fire 3+ request generate QR berurutan cepat dari merchant sama, amount berbeda-beda | 3+ record transaksi terpisah dibuat, tidak ada asumsi "1 merchant = 1 transaksi aktif" yang membatasi | Must | Mock |
| TC-CONC-04 | Access Token Manager — refresh token bersamaan dari beberapa request | Token belum ada di cache / sudah expired | Fire 5 request generate QR paralel untuk merchant yang sama | Hanya satu request token baru yang benar-benar terkirim ke Manjo (bukan 5x), request lain menunggu/reuse token yang sama | Should | Mock, jalankan dengan `-race` |

## 14. TC-SIG — Signature Verification

| ID | Judul | Precondition | Langkah | Expected Result | Prioritas | Mode |
|---|---|---|---|---|---|---|
| TC-SIG-01 | Signature valid diterima | Payload & signature dihitung benar sesuai formula HMAC-SHA512 | Kirim notifikasi | Diproses normal | Must | Mock + Sandbox |
| TC-SIG-02 | Signature invalid ditolak | Signature diubah/dipalsukan | Kirim notifikasi dengan signature salah | Response `401`, payload **tidak** diproses, tidak ada perubahan status transaksi maupun MQTT publish | Must | Mock |
| TC-SIG-03 | Header `X-SIGNATURE` hilang | — | Kirim notifikasi tanpa header signature | Ditolak dengan `400`/`401`, bukan crash | Must | Mock |
| TC-SIG-04 | Body dimodifikasi setelah signature dihitung (tamper) | Signature valid untuk body A, body diganti jadi body B sebelum dikirim | Kirim | Verifikasi gagal (hash body tidak cocok), ditolak | Must | Mock |
| TC-SIG-05 | Formula signature cocok dengan implementasi nyata Manjo | — | Bandingkan hasil generate signature request ke Manjo sandbox | Request diterima Manjo (bukan `401` dari sisi Manjo) | Must | `[SANDBOX]` |

## 15. TC-TOKEN — Token Lifecycle

| ID | Judul | Precondition | Langkah | Expected Result | Prioritas | Mode |
|---|---|---|---|---|---|---|
| TC-TOKEN-01 | Token baru diminta saat cache kosong | Belum ada token di cache | Trigger generate QR | Service panggil `/access-token/b2b` dulu sebelum generate QR | Must | Mock |
| TC-TOKEN-02 | Token dari cache dipakai ulang | Token valid ada di cache | Trigger generate QR lagi | **Tidak** ada panggilan ulang ke `/access-token/b2b` | Must | Mock |
| TC-TOKEN-03 | Auto-refresh sebelum expired | Token di cache, sisa waktu < threshold refresh (mis. < 60 detik, simulasi dengan clock injection) | Trigger generate QR | Token baru diminta secara proaktif sebelum dipakai | Should | Mock (clock injection) |
| TC-TOKEN-04 | Response `401` saat generate QR (token invalid/expired di sisi Manjo) | Mock Manjo balas `401` di percobaan pertama | Trigger generate QR | Service refresh token, retry sekali, request kedua pakai token baru | Must | Mock |
| TC-TOKEN-05 | Token sungguhan dari sandbox valid & bisa dipakai | — | Request token ke sandbox, lalu pakai untuk generate QR | Generate QR sukses dengan token tsb; `expiresIn` = `900` | Must | `[SANDBOX]` |

## 16. TC-ERR — Error Handling

| ID | Judul | Kode Manjo | Expected Behavior | Prioritas | Mode |
|---|---|---|---|---|---|
| TC-ERR-01 | Invalid Field Format | `400` (case 01) | Log & alert, **tidak** retry otomatis, transaksi `FAILED` | Must | Mock |
| TC-ERR-02 | Invalid Mandatory Field | `400` (case 02) | Sama seperti di atas | Must | Mock |
| TC-ERR-03 | Unauthorized / Invalid Token | `401` | Refresh token, retry sekali — lihat TC-TOKEN-04 | Must | Mock |
| TC-ERR-04 | Invalid Merchant | `404` | **Tidak** retry, log jelas untuk investigasi config merchant | Must | Mock |
| TC-ERR-05 | Conflict (`X-EXTERNAL-ID` dipakai ulang) | `409` | Cek status transaksi dulu via Query Payment — lihat TC-IDM-04 dan [TC-QRY-03](#11-tc-qry--query-payment-flow) | Must | Mock |
| TC-ERR-06 | Timeout ke Manjo | (network timeout) | Retry sesuai [strategi retry](#2-test-levels) di `architecture.md`, maksimal 3x, lalu `FAILED` | Must | Mock (simulasi delay) |
| TC-ERR-07 | Manjo balas 5xx | `500`/`502`/`503` | Retry (transient error), sama seperti timeout | Must | Mock |
| TC-ERR-08 | MQTT publish gagal (broker down) | — | Masuk retry queue, tidak memblokir response HTTP ke Manjo — lihat TC-PN-06 | Should | Mock |

## 17. TC-SM — State Machine Transitions

| ID | Judul | Precondition | Langkah | Expected Result | Prioritas | Mode |
|---|---|---|---|---|---|---|
| TC-SM-01 | `PENDING` → `QR_GENERATED` valid | Transaksi baru | Generate QR sukses | Transisi berhasil | Must | Mock |
| TC-SM-02 | `QR_GENERATED` → `PAID` valid | — | Notifikasi sukses masuk | Transisi berhasil | Must | Mock |
| TC-SM-03 | `QR_GENERATED` → `EXPIRED` valid | `expire_at` sudah lewat | Auto-expire job jalan | Transisi berhasil | Must | Mock |
| TC-SM-04 | `PAID` → status lain ditolak (kecuali `REFUNDED`) | Transaksi `PAID` | Coba update ke `FAILED` | Ditolak oleh trigger DB / application layer, exception dilempar, data tidak berubah | Must | Mock |
| TC-SM-05 | `FAILED` → status lain ditolak | Transaksi `FAILED` | Coba update ke `PAID` | Ditolak, data tidak berubah | Must | Mock |
| TC-SM-06 | `EXPIRED` → status lain ditolak | Transaksi `EXPIRED` | Coba update status apapun | Ditolak | Must | Mock |
| TC-SM-07 | `PAID` → `REFUNDED` diizinkan | Transaksi `PAID` | Notifikasi status `05` masuk | Transisi berhasil (satu-satunya pengecualian dari status final yang "terkunci") | Should | Mock |

## 18. TC-EXP — Auto-Expire Job

| ID | Judul | Precondition | Langkah | Expected Result | Prioritas | Mode |
|---|---|---|---|---|---|---|
| TC-EXP-01 | Transaksi expired ditandai otomatis | Transaksi `QR_GENERATED`, `expire_at` < now() | Jalankan expire job | Status → `EXPIRED` | Must | Mock |
| TC-EXP-02 | Transaksi belum expired tidak tersentuh | Transaksi `QR_GENERATED`, `expire_at` > now() | Jalankan expire job | Status tetap `QR_GENERATED` | Must | Mock |
| TC-EXP-03 | Transaksi `PAID` tidak tersentuh meski `expire_at` lewat | Transaksi `PAID`, `expire_at` lewat | Jalankan expire job | Status tetap `PAID` (query job hanya menyasar `QR_GENERATED`) | Must | Mock |
| TC-EXP-04 | Race antara expire job dan notifikasi yang datang bersamaan | `expire_at` baru saja lewat, notifikasi `PAID` datang di waktu hampir sama | Jalankan keduanya nyaris bersamaan | Hasil akhir konsisten (salah satu menang, tidak ada state korup / kedua-duanya berhasil update) — perlu row lock yang sama seperti TC-CONC-01 | Should | Mock, jalankan dengan `-race` |

## 19. TC-SB — Sandbox Manjo (End-to-End Nyata)

Test case ini **wajib dijalankan minimal sekali** sebelum rilis (bagian dari Exit Criteria), memverifikasi bahwa implementasi benar-benar cocok dengan Manjo sungguhan — bukan cuma sesuai dokumentasi/mock.

| ID | Judul | Langkah | Expected Result | Prioritas |
|---|---|---|---|---|
| TC-SB-01 | Full flow generate QR lawan sandbox | Request access token sandbox → generate QR sandbox dengan amount kecil | `qrContent` valid diterima, bisa di-decode jadi QR yang benar secara format QRIS | Must |
| TC-SB-02 | Full flow payment notification lawan sandbox | Kalau sandbox mendukung simulasi pembayaran: selesaikan pembayaran, tunggu webhook masuk | Webhook diterima di endpoint Service, signature valid, status ter-update | Must (kalau sandbox mendukung) |
| TC-SB-03 | Signature request generate QR diterima sandbox | Kirim request generate QR ke sandbox dengan signature dihitung dari implementasi Service | Sandbox membalas `200`, bukan `401` | Must |
| TC-SB-04 | Verifikasi `expiresIn` token sungguhan | Request access token | `expiresIn = "900"` sesuai dokumentasi | Should |
| TC-SB-05 | Webhook endpoint reachable dari luar | Deploy Notification HTTP Endpoint ke environment staging dengan domain publik | Kirim test call manual (curl) dari luar jaringan internal | Endpoint merespons, TLS valid | Must |
| TC-SB-06 | Full flow query payment lawan sandbox | Transaksi sudah pernah digenerate QR-nya di sandbox (dari TC-SB-01) | Panggil Query Payment ke sandbox dengan `originalReferenceNo` transaksi tsb | Response sesuai kondisi transaksi nyata; kode status yang diterima dipetakan lewat mapping Query (bukan mapping Notification) — lihat [TC-QRY-04](#11-tc-qry--query-payment-flow) | Should |

---

## 20. Ringkasan Cakupan Test Case

| Kategori | Jumlah Test Case | Must | Should |
|---|---|---|---|
| Generate QR Flow | 9 | 9 | 0 |
| Payment Notification Flow | 8 | 6 | 2 |
| Query Payment Flow | 8 | 6 | 2 |
| Idempotency | 5 | 5 | 0 |
| Concurrency & Race Condition | 4 | 3 | 1 |
| Signature Verification | 5 | 5 | 0 |
| Token Lifecycle | 5 | 4 | 1 |
| Error Handling | 8 | 7 | 1 |
| State Machine | 7 | 6 | 1 |
| Auto-Expire Job | 4 | 3 | 1 |
| Sandbox E2E | 6 | 4 | 2 |
| **Total** | **69** | **58** | **11** |

**Catatan penutup:** dokumen ini adalah baseline test plan — seiring implementasi berjalan dan ada perilaku tak terduga yang ditemukan (terutama saat integrasi sandbox Manjo asli, atau saat firmware Q161 mulai terintegrasi), test case baru sebaiknya ditambahkan ke kategori yang relevan, bukan dianggap dokumen final yang tertutup.