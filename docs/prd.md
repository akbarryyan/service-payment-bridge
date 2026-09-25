# PRD — Q161 Pro × Manjo Payment Bridge Service

**Versi:** 1.0
**Status:** Draft
**Terakhir diperbarui:** September 2026
**Dokumen terkait:** `architecture.md`, `brainstorm-q161-updated.md`, `manjo-api-docs.md`

---

## Daftar Isi

1. [Ringkasan](#1-ringkasan)
2. [Latar Belakang & Masalah](#2-latar-belakang--masalah)
3. [Tujuan](#3-tujuan)
4. [Non-Goals](#4-non-goals)
5. [Target Pengguna & Stakeholder](#5-target-pengguna--stakeholder)
6. [Ruang Lingkup](#6-ruang-lingkup)
7. [User Stories](#7-user-stories)
8. [Functional Requirements](#8-functional-requirements)
9. [Non-Functional Requirements](#9-non-functional-requirements)
10. [Alur Utama (User Flow)](#10-alur-utama-user-flow)
11. [Success Metrics](#11-success-metrics)
12. [Asumsi & Dependensi](#12-asumsi--dependensi)
13. [Risiko & Mitigasi](#13-risiko--mitigasi)
14. [Open Questions](#14-open-questions)
15. [Milestone / Fase Implementasi](#15-milestone--fase-implementasi)
16. [Acceptance Criteria](#16-acceptance-criteria)
17. [Referensi](#17-referensi)

---

## 1. Ringkasan

Payment Bridge Service adalah sistem middleware yang memungkinkan perangkat **Q161 Pro Soundbox** menerima pembayaran **QRIS dinamis** melalui payment gateway **Manjo**, dengan notifikasi hasil pembayaran otomatis dalam bentuk suara pada soundbox. Service ini menjembatani protokol **MQTT** (dipakai Q161) dengan **REST API** (dipakai Manjo), sekaligus menjadi pemilik data transaksi.

---

## 2. Latar Belakang & Masalah

Q161 Pro adalah generasi soundbox QRIS **dinamis** — berbeda dari Q181 (QRIS statis) yang sudah lebih dulu berjalan, di mana QR sudah tetap dan payment gateway langsung mengirim notifikasi audio siap-pakai ke MQTT broker.

Pada Q161, merchant memasukkan nominal transaksi langsung di device, sehingga QR harus **digenerate secara real-time** untuk setiap transaksi melalui API payment gateway (Manjo). Ini menimbulkan kebutuhan baru yang tidak ada di Q181:

- Device perlu meminta QR ke suatu sistem, bukan menampilkan QR statis.
- Sistem tersebut perlu berkomunikasi dengan API Manjo (autentikasi, generate QR, terima notifikasi pembayaran).
- Karena Manjo adalah gateway generik (tidak tahu soal MQTT atau format audio Q161), dibutuhkan **layer penerjemah** antara dunia HTTP/REST milik Manjo dan dunia MQTT milik Q161.

**Masalah yang diselesaikan:** tidak ada komponen saat ini yang menjembatani Q161 dan Manjo. Tanpa Payment Bridge Service, Q161 tidak bisa menggenerate QRIS dinamis maupun menerima notifikasi pembayaran secara otomatis.

---

## 3. Tujuan

1. Merchant dapat menggenerate QRIS dinamis dari Q161 Pro dengan nominal yang mereka input sendiri, dan QR tampil di device dalam waktu wajar (target: < 5 detik dari input sampai QR tampil, dalam kondisi normal).
2. Setelah customer membayar, soundbox otomatis mengeluarkan notifikasi suara "pembayaran berhasil" **tanpa intervensi manual**, berdasarkan status resmi dari Manjo (bukan asumsi device).
3. Sistem dapat melayani **banyak merchant dan banyak device Q161 secara bersamaan**, tanpa batasan artifisial (1 merchant tidak dibatasi 1 transaksi aktif).
4. Setiap transaksi tercatat dan bisa ditelusuri (traceable) untuk kebutuhan support/troubleshooting saat ada laporan "sudah bayar tapi soundbox tidak bunyi".
5. Kredensial sensitif (private key, client secret Manjo) tidak pernah tersimpan atau melewati device Q161 maupun MQTT broker.

---

## 4. Non-Goals

Hal-hal yang **secara sengaja tidak** menjadi tanggung jawab sistem ini:

- **Rendering gambar QR** — itu tanggung jawab firmware Q161 (Service hanya mengirim `qrContent` mentah).
- **Playback audio** — Service hanya mengirim instruksi/payload notifikasi; eksekusi suara sepenuhnya di sisi device.
- **Manajemen akun merchant di Manjo** (onboarding, registrasi client_id/private key) — itu proses bisnis terpisah di luar scope teknis dokumen ini; Service hanya *mengonsumsi* kredensial yang sudah ada.
- **Refund/dispute handling** secara aktif — Service hanya mencatat status `REFUNDED` bila notifikasi terkait diterima, tidak memicu proses refund.
- **Dashboard/UI monitoring untuk merchant** — di luar scope v1, meskipun data yang dibutuhkan untuk itu (log transaksi) sudah disiapkan di data model.

---

## 5. Target Pengguna & Stakeholder

| Peran | Kebutuhan dari sistem ini |
|---|---|
| **Merchant** (pengguna Q161 Pro) | QR muncul cepat setelah input nominal; soundbox bunyi otomatis saat pembayaran sukses; tidak perlu cek manual |
| **Customer** (pembayar) | Scan QR yang valid dan sesuai nominal |
| **Tim Support/Ops (internal)** | Bisa menelusuri riwayat transaksi & pesan mentah saat ada laporan masalah |
| **Tim Engineering (internal, kamu)** | Kontrak API/payload yang jelas antara Q161, Service, dan Manjo, supaya bisa develop dua sisi (firmware & backend) secara paralel |
| **Manjo** (payment gateway) | Endpoint webhook yang reachable, signature yang bisa diverifikasi, request yang sesuai skema API mereka |

---

## 6. Ruang Lingkup

### In Scope (v1)

- Integrasi API Manjo: Access Token B2B, Generate QRIS MPM, terima Payment Notification.
- MQTT bridge dua arah antara Service dan Q161 Pro (request generate QR, hasil QR, notifikasi pembayaran).
- Penyimpanan data merchant, transaksi, dan log pesan (MQTT + HTTP ke Manjo).
- Idempotency untuk mencegah transaksi diproses dua kali (baik dari retry maupun duplicate notification).
- Auto-expire transaksi yang QR-nya tidak dibayar dalam batas waktu tertentu.
- Multi-merchant, multi-device, transaksi concurrent.

### Out of Scope (v1)

- Dashboard web untuk merchant memantau transaksi.
- Notifikasi selain via soundbox (SMS, email, push notification ke merchant).
- Rekonsiliasi otomatis dengan laporan settlement Manjo.
- Refund/void transaksi secara aktif dari sisi Service.
- Dukungan payment gateway selain Manjo (arsitektur bisa diperluas ke sana nanti, tapi bukan target v1).

---

## 7. User Stories

| # | Sebagai | Saya ingin | Supaya |
|---|---|---|---|
| US-1 | Merchant | memasukkan nominal di Q161 dan langsung melihat QR pembayaran | customer bisa langsung scan tanpa menunggu lama |
| US-2 | Merchant | soundbox otomatis bunyi "pembayaran berhasil" saat customer sudah bayar | saya tidak perlu cek manual apakah pembayaran masuk |
| US-3 | Merchant | QR yang expired tidak lagi bisa dibayar / otomatis tergantikan | tidak ada transaksi ambigu antara nominal lama dan baru |
| US-4 | Tim Support | menelusuri riwayat pesan MQTT & API call Manjo untuk satu transaksi tertentu | bisa menjawab laporan "sudah bayar tapi tidak bunyi" dengan cepat dan akurat |
| US-5 | Tim Engineering | kontrak payload MQTT dan REST yang terdokumentasi jelas | development firmware Q161 dan backend Service bisa jalan paralel tanpa saling menunggu |
| US-6 | Sistem (bukan manusia) | menolak/mengabaikan notifikasi pembayaran duplikat dari Manjo | soundbox tidak bunyi berkali-kali untuk satu transaksi yang sama |

---

## 8. Functional Requirements

| ID | Requirement | Prioritas |
|---|---|---|
| FR-1 | Service dapat menerima request generate QR dari Q161 melalui MQTT, per-merchant berdasarkan topic | Must |
| FR-2 | Service memvalidasi request (amount > 0, merchant dikenal & aktif) sebelum diteruskan ke Manjo | Must |
| FR-3 | Service melakukan autentikasi ke Manjo (Access Token B2B) dan mengelola siklus hidup token (cache, refresh sebelum expired) | Must |
| FR-4 | Service memanggil Generate QRIS MPM Manjo dan mengirimkan `qrContent` kembali ke Q161 via MQTT | Must |
| FR-5 | Service menyediakan endpoint HTTP publik untuk menerima Payment Notification dari Manjo | Must |
| FR-6 | Service memverifikasi signature (`X-SIGNATURE`) setiap Payment Notification yang masuk sebelum memprosesnya | Must |
| FR-7 | Service mencocokkan notifikasi pembayaran dengan transaksi internal via `transaction_id`/`partnerReferenceNo` | Must |
| FR-8 | Service memetakan status pembayaran dari Manjo ke status transaksi internal dan menyimpannya | Must |
| FR-9 | Service mengirim notifikasi hasil pembayaran ke Q161 via MQTT dalam format yang bisa memicu audio playback | Must |
| FR-10 | Service mendeteksi dan mengabaikan notifikasi duplikat tanpa memproses ulang atau mengirim notifikasi MQTT ganda | Must |
| FR-11 | Service mencatat setiap pesan MQTT (inbound/outbound) dan setiap panggilan API Manjo (request/response) untuk kebutuhan tracing | Must |
| FR-12 | Service melakukan retry otomatis untuk kegagalan transient (timeout, 5xx) saat memanggil Manjo, dengan batas percobaan | Should |
| FR-13 | Service menandai transaksi sebagai `EXPIRED` otomatis jika QR tidak dibayar dalam batas waktu (`validityPeriod`) | Should |
| FR-14 | Service mendukung banyak merchant dengan kredensial Manjo yang berbeda-beda secara bersamaan | Must |
| FR-15 | Service menangani banyak transaksi aktif bersamaan untuk satu merchant (tidak dibatasi 1 transaksi/merchant) | Must |
| FR-16 | Kredensial Manjo (private key, client secret) disimpan terenkripsi/di secret manager, bukan plaintext | Must |
| FR-17 | Service dapat memanggil Query Payment (Service Code `51`) untuk mengecek status transaksi langsung ke Manjo, dipakai sebagai verifikasi sebelum retry generate QR pasca-`409` dan/atau reconciliation untuk transaksi yang tidak kunjung menerima notifikasi | Should |
| FR-18 | Service mendukung sub-merchant/tenant opsional per merchant, dan multi-device per tenant, dengan topic MQTT ter-scope per device — transaksi satu device tidak boleh memicu notifikasi ke device lain | Must |

---

## 9. Non-Functional Requirements

| Kategori | Requirement |
|---|---|
| **Performance** | Waktu dari request generate-QR masuk sampai QR terkirim balik ke Q161 idealnya < 5 detik dalam kondisi normal (dominan ditentukan oleh latency API Manjo) |
| **Reliability** | Service harus tetap bisa menerima Payment Notification dari Manjo meskipun MQTT broker sedang bermasalah (publish MQTT dipisah dari response HTTP ke Manjo) |
| **Scalability** | Arsitektur stateless (state di database) agar bisa dijalankan multi-instance seiring pertambahan jumlah merchant/device |
| **Security** | Seluruh komunikasi ke Manjo via HTTPS; signature diverifikasi di setiap notifikasi masuk; MQTT broker pakai TLS + autentikasi per-device |
| **Auditability** | Setiap transaksi bisa ditelusuri ujung-ke-ujung: pesan MQTT masuk → request ke Manjo → response Manjo → notifikasi masuk → pesan MQTT keluar |
| **Idempotency** | Tidak ada transaksi yang diproses dua kali akibat retry atau notifikasi duplikat, dalam kondisi apa pun |
| **Maintainability** | Kontrak payload (MQTT & REST) terdokumentasi dan versioned, supaya perubahan di satu sisi (misal firmware Q161) tidak diam-diam memutus sisi lain |

---

## 10. Alur Utama (User Flow)

### Flow A — Generate QR

```text
1. Merchant input nominal di Q161 Pro
2. Q161 kirim request via MQTT ke Service
3. Service validasi & buat record transaksi (PENDING)
4. Service minta QR ke Manjo (via access token + signature)
5. Manjo balas qrContent
6. Service kirim qrContent ke Q161 via MQTT
7. Q161 render & tampilkan QR
8. Customer scan & bayar
```

### Flow B — Payment Notification

```text
1. Manjo memproses pembayaran, kirim notifikasi HTTP ke Service
2. Service verifikasi signature
3. Service cek apakah notifikasi ini duplikat
4. Service cocokkan ke transaksi internal, update status
5. Service balas 200 OK ke Manjo
6. Service kirim notifikasi ke Q161 via MQTT
7. Q161 mainkan audio "pembayaran berhasil"
```

*(Detail teknis lengkap kedua flow ini — termasuk penanganan error di tiap langkah — ada di `architecture.md` Section 5 dan 6.)*

---

## 11. Success Metrics

| Metrik | Target Awal (v1) |
|---|---|
| Tingkat keberhasilan generate QR (sukses / total request) | ≥ 99% (di luar gangguan sisi Manjo) |
| Waktu rata-rata generate QR sampai tampil di device | < 5 detik |
| Tingkat notifikasi pembayaran yang berhasil memicu bunyi soundbox | ≥ 99.5% |
| Jumlah kasus "duplicate payment processed" | 0 (hard requirement, bukan target toleransi) |
| Waktu rata-rata investigasi kasus "sudah bayar tidak bunyi" (dengan bantuan log tracing) | < 15 menit |

---

## 12. Asumsi & Dependensi

**Asumsi:**
- Q161 Pro dan firmware-nya dikembangkan oleh tim yang sama dengan Service ini, sehingga kontrak payload MQTT bisa disepakati bebas (bukan mengikuti spesifikasi vendor eksternal).
- Kredensial Manjo (client_id, private key, client secret) untuk tiap merchant sudah/akan tersedia melalui proses onboarding bisnis dengan Manjo, di luar scope dokumen ini.
- MQTT broker yang dipakai sudah ada (dipakai bersama dengan Q181) atau akan disiapkan terpisah — dokumen ini tidak menentukan pilihan broker.
- Service akan memiliki domain/endpoint publik yang bisa didaftarkan ke Manjo sebagai webhook URL.
- Data `tenants`/`devices` (termasuk `device_id` per unit Q161 fisik) diisi lewat proses/sistem di luar Payment Bridge Service — Service ini hanya membaca, tidak menyediakan CRUD untuk mengelolanya (konsisten dengan asumsi kredensial merchant di atas).

**Dependensi eksternal:**
- Ketersediaan & stabilitas API Manjo (Access Token B2B, Generate QRIS MPM, Payment Notification).
- Konfirmasi dari Manjo terkait perbedaan status `00` (Success) vs `03` (Paid) — lihat [Section 14](#14-open-questions).
- Firmware Q161 Pro harus mengimplementasikan sisi device dari kontrak MQTT yang didefinisikan di `architecture.md` Section 7.

---

## 13. Risiko & Mitigasi

| Risiko | Dampak | Mitigasi |
|---|---|---|
| Notifikasi pembayaran dari Manjo tidak sampai (network/webhook down) | Soundbox tidak bunyi meskipun sudah dibayar | Logging lengkap (`manjo_api_logs`) untuk deteksi cepat; endpoint webhook di-monitor uptime-nya; opsional reconciliation via Query Payment (`FR-17`) untuk transaksi yang mendekati/lewat expiry tanpa notifikasi masuk |
| Notifikasi duplikat memicu bunyi berkali-kali atau update ganda | Pengalaman buruk merchant, data tidak akurat | Idempotency check wajib di setiap notifikasi ([FR-10](#8-functional-requirements)) |
| Access token expired di tengah proses generate QR | Kegagalan generate QR yang seharusnya bisa dihindari | Token Manager dengan auto-refresh proaktif sebelum expired |
| Kredensial Manjo bocor (private key/client secret) | Risiko keamanan serius, bisa disalahgunakan pihak lain | Wajib disimpan di secret manager, tidak pernah di-log penuh ([FR-16](#8-functional-requirements)) |
| Ambiguitas status `00` vs `03` menyebabkan sebagian notifikasi sukses tidak terproses sebagai `PAID` | Soundbox tidak bunyi meski pembayaran sebenarnya berhasil | Sementara treat keduanya sebagai `PAID` (lihat `architecture.md` Section 20), sambil menunggu konfirmasi resmi Manjo |
| MQTT broker down saat notifikasi pembayaran masuk | Notifikasi tidak sampai ke device meski Service sudah proses | Retry queue terpisah untuk publish MQTT, tidak memblokir response ke Manjo |

---

## 14. Open Questions

| # | Pertanyaan | Pemilik | Status |
|---|---|---|---|
| 1 | Apa perbedaan makna status `00` (Success) dan `03` (Paid) dari Manjo? | Tim Manjo | Belum terjawab — di-treat sama untuk sementara |
| 2 | Format final payload notifikasi audio ke Q161 — string `+`-separated (pola Q181) atau array JSON? | Kamu (firmware Q161) | Belum diputuskan |
| 3 | Apakah kredensial Manjo per-merchant atau satu kredensial partner untuk semua merchant? | Kamu / kesepakatan bisnis | Belum diputuskan |
| 4 | Berapa lama `validityPeriod` QR sebelum dianggap expired? | Kamu | Belum diputuskan |
| 5 | Apakah perlu dashboard monitoring transaksi di v1, atau cukup akses langsung ke database/log? | Kamu | Diasumsikan tidak perlu untuk v1 (lihat Non-Goals) |
| 6 | Apakah reconciliation job berbasis Query Payment (FR-17) masuk scope v1, atau auto-expire murni berdasarkan `expire_at` tanpa cross-check ke Manjo? | Kamu | Belum diputuskan — lihat `architecture.md` Section 20 Open Decision #7 |

---

## 15. Milestone / Fase Implementasi

> Estimasi waktu sengaja tidak dicantumkan (tergantung kapasitas tim) — urutan di bawah adalah urutan logis berdasarkan dependensi antar bagian.

1. **Fase 1 — Fondasi:** setup project Go, skema database (`merchants`, `transactions`, `mqtt_messages`, `manjo_api_logs`), koneksi MQTT & DB dasar.
2. **Fase 2 — Manjo Client:** implementasi Access Token Manager, Signature Builder (RSA & HMAC), Generate QR client. Uji coba ke sandbox Manjo (kalau tersedia).
3. **Fase 3 — Generate QR Flow:** MQTT Consumer → Validator → Merchant Resolver → Transaction Service → Manjo Client → MQTT Publisher (Flow A lengkap).
4. **Fase 4 — Payment Notification Flow:** Notification HTTP Endpoint, signature verification, idempotency check, state machine update, MQTT Publisher untuk notifikasi (Flow B lengkap).
5. **Fase 5 — Hardening:** retry mechanism, auto-expire job, logging/observability lengkap, security review (audit penyimpanan kredensial).
6. **Fase 6 — Integrasi Firmware Q161:** uji end-to-end dengan device Q161 sungguhan, finalisasi format `audio_sequence`.
7. **Fase 7 — UAT & Go-Live:** pilot dengan beberapa merchant terbatas sebelum rollout penuh.

---

## 16. Acceptance Criteria

Sistem dianggap siap rilis (v1) apabila:

- [ ] Merchant bisa input amount di Q161 dan QR tampil dalam waktu wajar, untuk minimal 3 merchant berbeda secara bersamaan.
- [ ] Pembayaran yang berhasil (real, lewat sandbox/production Manjo) memicu bunyi soundbox otomatis tanpa intervensi manual.
- [ ] Notifikasi pembayaran yang dikirim ulang (disimulasikan) oleh Manjo tidak menyebabkan soundbox bunyi dua kali atau status transaksi berubah tidak semestinya.
- [ ] Transaksi yang QR-nya tidak dibayar otomatis berubah status `EXPIRED` setelah `validityPeriod` lewat.
- [ ] Semua kredensial Manjo tersimpan aman (tidak ada plaintext private key/client secret di database maupun log).
- [ ] Tim support bisa menelusuri riwayat lengkap satu transaksi (dari MQTT masuk sampai MQTT keluar) hanya dengan `transaction_id`.
- [ ] Error dari Manjo (401, 404, 409, timeout) ditangani sesuai skema di `architecture.md` Section 13 tanpa membuat sistem crash atau transaksi nyangkut di status ambigu.

---

## 17. Referensi

- `architecture.md` — spesifikasi teknis detail (komponen, kontrak payload, skema database, tech stack).
- `brainstorm-q161-updated.md` — eksplorasi konsep dan rasionale keputusan arsitektur.
- `manjo-api-docs.md` — dokumentasi resmi API Manjo QRIS MPM.