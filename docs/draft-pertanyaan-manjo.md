# Pertanyaan Integrasi QRIS MPM — Payment Bridge Soundbox Q161 Pro

**Lingkungan:** UAT, merchant Pupuk Kalteng (`merchantId` MT58530503)

Halo tim Manjo,

Kami sedang membangun layanan yang menghubungkan soundbox Q161 Pro dengan API QRIS MPM (BI SNAP). Di UAT, alur **generate QR sudah berjalan end-to-end**: access token B2B → `qr-mpm-generate` → QR tampil di soundbox, dan QR-nya bisa di-scan dari m-banking. Dengan collection BI SNAP UAT kami juga sudah mencoba `qr-mpm-query`: statusnya pending sebelum dibayar, lalu berubah menjadi successful setelah `qrContent` dibayar lewat simulator Alto.

Langkah berikutnya adalah menerima status pembayaran, supaya soundbox bisa berbunyi setelah customer membayar. Ada beberapa hal yang perlu kami konfirmasi, kami urutkan dari yang paling menentukan.

---

## 1. Webhook Payment Notification (service code 52)

- URL webhook apa yang saat ini terdaftar untuk merchant ini di UAT?
- Bagaimana prosedur mendaftarkan atau mengubah URL webhook ke endpoint kami? Apakah path-nya bebas, atau harus persis `/v1.0/qr/qr-mpm-notify`?
- Jika pembayaran di UAT disimulasikan lewat Alto, apakah Payment Notification tetap dikirim ke webhook seperti di production?
- Dari IP mana saja Manjo mengirim notifikasi (untuk allowlist firewall)?
- Berapa lama timeout Manjo menunggu respons kami, dan bagaimana kebijakan retry-nya jika kami tidak membalas `200`?

## 2. Query Payment & masa berlaku QR

Sambil menunggu jalur notifikasi siap, kami mengecek status transaksi secara berkala lewat `qr-mpm-query`.

- Berapa frekuensi polling maksimal yang diperbolehkan (rate limit per merchant)? Rencana kami: tiap 3 detik selama masa berlaku QR.
- Respons `qr-mpm-generate` di UAT berisi `expiryDuration: "450000"` (kami baca sebagai milidetik, yaitu 7,5 menit) dan `expireDate` yang **7 jam lebih lambat** dari seharusnya. Contoh: QR dibuat pukul 09:34 WIB, `expireDate` = `20260929164224`. Apakah `expireDate` memang dalam WIB, dan apakah selisih 7 jam ini bug di UAT? Sementara ini kami memakai `expiryDuration` sebagai acuan.
- Kami mengirim `validityPeriod: "3600"`, tapi QR tetap kedaluwarsa sekitar 7 menit. Apakah masa berlaku QR ditentukan sepenuhnya oleh Manjo, atau ada format `validityPeriod` yang benar?

## 3. Arti status notifikasi

- Pada Payment Notification, apa perbedaan status `00` (Success) dan `03` (Paid)? Apakah keduanya berarti dana sudah diterima?
- Contoh body Payment Notification di dokumentasi (Section 4.6) berisi `latestTransactionStatus: "06"` (Cancelled) dengan `transactionStatusDesc: "SUCCESS"`. Mana yang benar? (Untuk Query Payment sudah jelas: pembayaran sukses mengembalikan `"00"` / `"Success"`.)

---

Terima kasih. Kalau ada dokumentasi atau contoh (misalnya collection Postman/Bruno untuk sisi penerima notifikasi) yang bisa dibagikan, itu akan sangat membantu.
