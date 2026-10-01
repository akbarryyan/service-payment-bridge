# Menjalankan Payment Bridge Service Secara Lokal

Panduan menjalankan service di laptop dev: setup awal, migration database, menjalankan server, test, dan cara mengecek data saat debugging.

Untuk setup jaringan ke Q161 Pro fisik (hotspot Windows, firewall, konfigurasi firmware), lihat `docs/windows-testing-setup.md`.

Semua perintah di bawah dijalankan dari **root folder project** (`service-payment-bridge/`). Contoh perintahnya memakai sintaks Git Bash. Di PowerShell, ganti `export NAMA=nilai` dengan `$env:NAMA = "nilai"`.

---

## 1. Prasyarat

| Tool | Untuk apa | Cara pasang |
|---|---|---|
| Go 1.25+ | Build & jalankan service | https://go.dev/dl/ |
| Docker Desktop | Postgres + Mosquitto (`docker-compose.yml`) | https://www.docker.com/products/docker-desktop/ |
| `golang-migrate` CLI | Menjalankan migration database | `go install -tags 'postgres' github.com/golang-migrate/migrate/v4/cmd/migrate@latest` |
| `mosquitto_pub` / `mosquitto_sub` (opsional) | Simulasi request device tanpa hardware | Installer Mosquitto untuk Windows, atau `sudo apt install mosquitto-clients` di Linux/WSL2 |
| `sqlc` (opsional) | Generate ulang kode Go dari query SQL, **hanya** kalau mengubah `internal/database/queries/` atau skema yang dipakai query | `go install github.com/sqlc-dev/sqlc/cmd/sqlc@latest` |

Pastikan `$(go env GOPATH)/bin` sudah masuk `PATH` supaya `migrate` dan `sqlc` bisa dipanggil langsung.

---

## 2. File Environment (sekali saja)

Service memuat dua file saat start: `.env` lalu `.env.sandbox`. Keduanya ada di `.gitignore`, jadi **jangan di-commit**.

```bash
cp .env.example .env
cp .env.sandbox.example .env.sandbox
```

- **`.env`** berisi konfigurasi aplikasi: port HTTP, koneksi DB, broker MQTT, dan base URL Manjo. Nilai default di `.env.example` sudah cocok untuk setup lokal (`MANJO_BASE_URL` mengarah ke UAT Manjo). Variabel MQTT-nya:
  - `MQTT_USERNAME` — akun broker milik backend (default `payment-bridge`).
  - `MQTT_PASSWORD` — password akun `MQTT_USERNAME`, dibuat lewat `scripts/broker-bootstrap.sh`.
  - `MQTT_PROVISIONER_USERNAME` — akun broker untuk CLI provisioning device (default `provisioner`).
  - `MQTT_PROVISIONER_PASSWORD` — password akun `MQTT_PROVISIONER_USERNAME`.
  - `MQTT_ADMIN_PASSWORD` — password admin broker dari bootstrap pertama (Section 3); hanya dipakai `scripts/broker-bootstrap.sh`.
  - `MQTT_DEVICE_SERVER` — IP broker yang diisi ke firmware Q161 Pro (hotspot Windows, biasanya `192.168.137.1`).
  - `MQTT_DEVICE_PORT` — port TLS broker untuk device (host port `18883`, diteruskan ke `8883` di container).
- **`.env.sandbox`** berisi kredensial Manjo UAT (merchant sandbox "Pupuk Kalteng"): `MANJO_SANDBOX_CLIENT_KEY`, `MANJO_SANDBOX_PRIVATE_KEY`, `MANJO_SANDBOX_CLIENT_SECRET`, `MANJO_SANDBOX_MERCHANT_ID`. Minta nilainya ke pemegang kredensial, jangan dikirim lewat chat atau repo. File ini juga berisi `ALTO_API_KEY` dan `ALTO_VALIDATION_KEY` untuk simulator pembayaran Alto (Section 5). Nilainya ada di `docs/manjo-collection/QR Payment.yml` (`API_KEY` dan `VALIDATION_KEY`).

> **Kenapa kredensial ada di file terpisah?** Tabel `merchants` tidak menyimpan kredensial, hanya **nama env var**-nya (`manjo_private_key_ref`, `manjo_client_secret_ref`). Service lalu membaca nilai env var tersebut saat runtime. Kalau `.env.sandbox` tidak ada atau isinya kosong, setiap generate QR gagal dengan `CONFIG_ERROR`, dan di log server muncul `"generate QR failed"` dengan `cause` berbunyi `environment variable "..." not set`.

---

## 3. Menjalankan Postgres & Mosquitto

```bash
docker compose up -d
docker compose ps
```

Keduanya harus berstatus `Up`, dan Postgres `(healthy)`. Port yang dipakai di host:

| Service | Port host | Keterangan |
|---|---|---|
| Postgres | `15432` | user/password/db: `payment_bridge` |
| Mosquitto | `11883` | Internal, plain (tanpa TLS). Hanya bisa diakses dari `127.0.0.1`; dipakai backend, test, dan CLI provisioning. |
| Mosquitto | `18883` | TLS, diteruskan ke `8883` di container. Dipakai device (Q161 Pro, `ssl=1`). |

Mematikan: `docker compose down`. Data Postgres tetap tersimpan di volume `postgres_data`. Untuk menghapus data sekalian: `docker compose down -v`, **hati-hati, semua data hilang**, dan setelahnya jalankan migration lagi dari awal.

### Broker: akun & sertifikat (sekali saja)

Broker memakai Mosquitto Dynamic Security — tidak ada klien tanpa akun.

```bash
bash scripts/broker-dev-cert.sh            # sertifikat TLS self-signed untuk port 18883
docker compose up -d
bash scripts/broker-bootstrap.sh --dev     # akun payment-bridge, provisioner, dan test (khusus dev)
```

Bootstrap **pertama kali** mencetak **password admin broker sekali saja**. Simpan sebagai `MQTT_ADMIN_PASSWORD` di `.env` (dev; di production simpan di password manager). Script membaca `.env` sendiri, jadi menjalankan ulang cukup:

```bash
bash scripts/broker-bootstrap.sh --dev
```

Password admin hanya dipakai script ini — server dan CLI tidak membutuhkannya.

Kalau password admin hilang, buat ulang **hanya volume broker** (lalu jalankan bootstrap lagi dan provision ulang semua alat):

```bash
docker compose rm -sf mosquitto && docker volume rm service-payment-bridge_mosquitto_data && docker compose up -d mosquitto
```

**Jangan pernah `docker compose down -v`** untuk ini, karena ikut menghapus data Postgres.

Akun `test` (password `test-dev-only`) dipakai `go test` dan **tidak boleh** dibuat di production (jangan pakai `--dev` di sana).

> **Akun berlaku di semua listener.** Akun broker bersifat global, bukan per-listener, jadi semua akun (termasuk `test`, `payment-bridge`, dan `provisioner`) juga bisa login lewat port TLS `18883` yang terbuka ke jaringan hotspot. Karena itu `MQTT_PASSWORD` dan `MQTT_PROVISIONER_PASSWORD` harus kuat, dan akun `test` (`--dev`) tidak boleh ada di production.

> **Jangan beri tanda kutip** pada nilai di `.env`: tulis `MQTT_PASSWORD=abc`, bukan `MQTT_PASSWORD="abc"`. Script bootstrap membaca nilainya apa adanya, sedangkan server (godotenv) membuang tanda kutipnya, sehingga nilai berkutip menghasilkan password yang tidak cocok.
>
> Hal yang sama berlaku untuk karakter lain: server (godotenv) memperluas `$VAR`, membuang kutip, dan memperlakukan `#` sebagai komentar, sedangkan bootstrap membaca `.env` apa adanya. Karena itu password di `.env` harus **hanya huruf dan angka**. Cara membuatnya: `openssl rand -hex 16`.

---

## 4. Migration Database

Set dulu URL database-nya (sama dengan `DATABASE_URL` di `.env`):

```bash
export DATABASE_URL="postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
```

### 4.1 Menjalankan migration (setup awal, atau setelah ada migration baru)

Setiap kali ada file baru di `migrations/` (misalnya setelah `git pull`), jalankan:

```bash
migrate -path migrations -database "$DATABASE_URL" up
```

Perintah ini hanya menjalankan migration yang **belum** pernah dijalankan, jadi aman diulang. Kalau semuanya sudah terpasang, outputnya `no change`.

**Wajib dijalankan sebelum start server.** Kalau migration tertinggal, gejalanya bisa membingungkan. Contohnya, device fisik ditolak dengan `UNKNOWN_DEVICE` karena seed device belum masuk, atau log `mqtt_messages` gagal dengan `invalid input syntax for type json` karena perubahan tipe kolom belum diterapkan.

### 4.2 Cek versi migration saat ini

```bash
migrate -path migrations -database "$DATABASE_URL" version
```

Angka yang keluar harus sama dengan nomor file migration terakhir di `migrations/`.

### 4.3 Membuat migration baru

```bash
migrate create -ext sql -dir migrations -seq nama_perubahan
```

Perintah ini membuat pasangan file `NNNNNN_nama_perubahan.up.sql` dan `.down.sql`. Aturannya:
- Isi **`.up.sql`** dengan perubahan, dan **`.down.sql`** dengan kebalikannya.
- **Jangan pernah mengedit migration yang sudah di-commit.** Selalu buat file baru. Migration lama sudah terlanjur jalan di DB orang lain, jadi editannya tidak akan ikut diterapkan.
- Kalau perubahannya menyentuh tabel/kolom yang dipakai query di `internal/database/queries/`, generate ulang kode Go-nya:
  ```bash
  sqlc generate
  go build ./...
  ```
  `sqlc` membaca skema langsung dari folder `migrations/` (lihat `sqlc.yaml`), jadi tidak perlu koneksi DB.

### 4.4 Rollback & memperbaiki state "dirty"

```bash
# Batalkan 1 migration terakhir
migrate -path migrations -database "$DATABASE_URL" down 1
```

Kalau sebuah migration gagal di tengah jalan, `version` akan menampilkan `N (dirty)` dan migration berikutnya ditolak. Perbaiki dulu penyebabnya (biasanya SQL-nya salah), lalu tandai versi terakhir yang **benar-benar** sukses dan jalankan ulang:

```bash
migrate -path migrations -database "$DATABASE_URL" force <N-1>
migrate -path migrations -database "$DATABASE_URL" up
```

---

## 5. Menjalankan Service

```bash
go run ./cmd/server
```

Biarkan terminal ini terbuka. Service berjalan selama terminal hidup, dan `Ctrl+C` untuk berhenti. Log ditulis ke stdout dalam format JSON. Log yang penting:

| Log | Arti |
|---|---|
| `"service started"` | Server siap, sudah subscribe ke `qris/request/+/+` |
| `"generate QR failed"` | Generate QR gagal. Lihat field `error_code` dan `cause` |
| `"device resolve failed"` | `device_id` dari device tidak ada di tabel `devices`, atau device/merchant/tenant `INACTIVE` |
| `"invalid GENERATE_QR payload"` | Payload dari device tidak sesuai format `"{SN}\|{amount_sen}"` |
| `IDENTITY_MISMATCH` (di `mqtt_messages`, status `FAILED`) | SN di payload berbeda dari SN di topic, atau SN tidak terdaftar di merchant pada topic. Request **tidak dibalas** |
| `"payment detected"` | Poller menemukan transaksi yang sudah dibayar (`status` → `PAID`), lalu mengirim pengumuman audio ke device |
| `"stale payment not announced"` | Transaksi baru menjadi `PAID` lewat dari 10 menit yang lalu (`paid_at`) saat pertama kali diklaim — misalnya sesudah backfill migration atau restart setelah downtime. Status tetap berubah jadi `PAID`, tapi soundbox **tidak** dibunyikan supaya tidak menyebutkan nominal yang sudah basi |
| `"announcement failed"` | Pengumuman audio gagal dikirim ke broker setelah 3 percobaan. Transaksi tetap `PAID` |
| `"transaction expired"` | QR kedaluwarsa. `via: "manjo"` = dijawab Manjo, `via: "deadline"` = jaring pengaman (2 menit lewat `expire_at`) |
| `"payment query failed"` | Query status ke Manjo gagal atau jawabannya tidak dikenal. Otomatis dicoba lagi 3 detik kemudian |
| `"payment check failed"` | Error database saat mengecek satu transaksi (bukan error dari Manjo). Transaksi itu otomatis dicoba lagi di siklus poll berikutnya |
| `"payment check panicked"` | Satu worker poller panic saat mengecek transaksi (mis. bug di checker/announcer). Di-*recover*, transaksi lain tidak terpengaruh, service tetap jalan |
| `"amount mismatch"` | Nominal dari Manjo berbeda dengan nominal transaksi. Pengumuman tetap memakai nominal transaksi |

Setelah generate QR, service mengecek status pembayaran ke Manjo tiap `PAYMENT_POLL_INTERVAL` (default `3s`, di `.env`) sampai QR dibayar atau kedaluwarsa (~7,5 menit). Begitu dibayar, soundbox membunyikan nominalnya.

Cek service hidup:

```bash
curl localhost:8080/healthz
# {"status":"ok"}
```

### Simulasi request tanpa device fisik

Di terminal lain, dengarkan balasan untuk alat `00078020709` milik merchant `MT58530503`, lalu kirim request persis seperti yang dikirim firmware (Rp50.000 = `5000000` sen). Akun `test` ada setelah `scripts/broker-bootstrap.sh --dev`; alat harus sudah terdaftar (Section 6):

```bash
mosquitto_sub -h localhost -p 11883 -u test -P test-dev-only -t "topic/MT58530503/00078020709" -v -C 1 -W 20 &
mosquitto_pub -h localhost -p 11883 -u test -P test-dev-only -t "qris/request/MT58530503/00078020709" -m "00078020709|5000000"
```

Kalau sukses, balasannya `topic/MT58530503/00078020709 QR:00020101...`. Balasan `Gagal membuat QR, coba lagi` berarti gagal, dan penyebabnya ada di log server.

### Simulasi pembayaran (bayar → soundbox bunyi)

Pembayaran di UAT bisa disimulasikan tanpa m-banking lewat simulator Alto:

1. Generate QR dari device (menu QRIS Dinamis), atau lewat `mosquitto_pub` seperti di atas.
2. Ambil `transaction_id` terbaru:
   ```bash
   docker compose exec postgres psql -U payment_bridge -d payment_bridge -c \
     "SELECT transaction_id, amount, status FROM transactions ORDER BY created_at DESC LIMIT 1;"
   ```
3. Bayar QR-nya (harus sebelum QR kedaluwarsa, ~7,5 menit):
   ```bash
   go run ./cmd/altosim -tx TRX-20260930-XXXXXX
   ```
   Output-nya menampilkan merchant, nominal, reference, dan respons Alto.
4. Dalam ~3 detik soundbox berbunyi "…lima puluh ribu…", log server menampilkan `"payment detected"`, dan status transaksinya menjadi `PAID`.

Kalau QR dibiarkan tanpa dibayar, sekitar 7,5 menit kemudian log menampilkan `"transaction expired"` dan soundbox tidak berbunyi.

`-qr "<qrContent>"` bisa dipakai sebagai pengganti `-tx` kalau `qrContent`-nya sudah ada di tangan.

---

## 6. Mendaftarkan Alat Q161 Pro (provisioning)

Setiap soundbox punya akun broker sendiri. Alat baru didaftarkan dengan satu perintah:

```bash
go run ./cmd/provision add -merchant MT58530503 -sn 00078020709
```

- `-merchant` = merchant ID Manjo (`merchants.manjo_merchant_id`; merchant-nya harus sudah terdaftar).
- `-sn` = serial number alat (ada di label alat, atau di log broker sebagai `clientId-<SN>` saat alat mencoba login).
- Opsional: `-tenant`, `-store-id`, `-terminal-id`.

Perintah ini membuat baris `devices`, akun broker `{merchant}-{SN}` beserta izinnya, dan file `mqttcfg.dat` untuk alat. **File itu berisi password alat**: muat ke alat lewat Downtool (MergeFile dengan `ext.txt`, sama seperti berkas audio), lalu hapus.

> File ditulis ke `docs/mqttcfg.dat` (bisa diganti dengan `-out <file>`). `mqttcfg.dat` sudah di-git-ignore di folder mana pun, jadi tidak ikut ter-commit — tetap hapus setelah dimuat ke alat.

Menjalankan ulang `add` untuk SN yang sama = **ganti password** (file lama tidak berlaku lagi).

Mencabut alat (hilang/dipindahtangankan):

```bash
go run ./cmd/provision revoke -sn 00078020709          # alat langsung tidak bisa login; status INACTIVE
go run ./cmd/provision revoke -sn 00078020709 -delete  # sekalian hapus akunnya di broker
```

Butuh di `.env`: `DATABASE_URL`, `MQTT_BROKER_URL`, `MQTT_PROVISIONER_PASSWORD` (akun `provisioner` dari bootstrap), `MQTT_DEVICE_SERVER`, `MQTT_DEVICE_PORT`.

> **Windows:** mode `0600` tidak membatasi akses di Windows, jadi tulis `-out` ke folder pribadi dan hapus file-nya setelah dimuat ke alat. Menimpa file yang sudah ada juga mempertahankan izin file lama, jadi hapus file lama dulu sebelum menulis ulang.

---

## 7. Mengecek Data Saat Debugging

```bash
# Transaksi terbaru
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c \
  "SELECT transaction_id, device_id, amount, status, created_at FROM transactions ORDER BY created_at DESC LIMIT 5;"

# Pesan MQTT masuk/keluar
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c \
  "SELECT topic, left(payload, 60) AS payload, direction, status, error_message, created_at FROM mqtt_messages ORDER BY created_at DESC LIMIT 10;"

# Panggilan ke Manjo (ACCESS_TOKEN & GENERATE_QR)
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c \
  "SELECT operation, http_status, duration_ms, transaction_id, response_body, created_at FROM manjo_api_logs ORDER BY created_at DESC LIMIT 5;"
```

Di `manjo_api_logs`, baris `ACCESS_TOKEN` memang tidak punya `transaction_id`, karena satu token dipakai untuk banyak transaksi. Nilai `accessToken` di `response_body` selalu tersamarkan (`***`).

Baris `QUERY_PAYMENT` hanya dicatat untuk hasil yang bukan "masih pending" (dibayar, kedaluwarsa, error), supaya log tidak dibanjiri ratusan poll per QR.

Transaksi yang sudah `PAID` tapi pengumumannya tidak pernah terkirim (kasus "sudah bayar tapi soundbox diam"):

```bash
docker compose exec postgres psql -U payment_bridge -d payment_bridge -c \
  "SELECT t.transaction_id, t.device_id, t.amount, t.paid_at FROM transactions t
   WHERE t.status = 'PAID' AND NOT EXISTS (
     SELECT 1 FROM mqtt_messages m WHERE m.transaction_id = t.transaction_id
       AND m.direction = 'OUTBOUND' AND m.status = 'PROCESSED' AND m.payload LIKE '%.mp3%')
   ORDER BY t.paid_at DESC;"
```

---

## 8. Menjalankan Test

Test integrasi memakai Postgres dan Mosquitto asli, jadi stack Docker harus jalan dan migration sudah `up`. Koneksi MQTT-nya login sebagai akun `test` / `test-dev-only`, yang ada setelah `scripts/broker-bootstrap.sh --dev` dijalankan (Section 3).

```bash
go test ./...
```

> **Matikan dulu `go run ./cmd/server` sebelum menjalankan test.** Server subscribe ke `qris/request/+/+` (topic yang sama dengan `TestGenerateQRFlow_EndToEnd`) dan menjalankan poller pembayaran di DB yang sama dengan test. Kalau server hidup, server bisa menjawab request test lebih dulu atau mengklaim transaksi test, sehingga test gagal walaupun kodenya benar.

Test yang memanggil Manjo UAT sungguhan secara default di-skip. Untuk menjalankannya (butuh `.env.sandbox` terisi, dan membuat transaksi sungguhan di UAT):

```bash
set -a; source .env.sandbox; set +a
RUN_SANDBOX_TESTS=1 go test ./internal/manjoclient/ -run TestSandbox -v
```

---

## 9. Troubleshooting Cepat

| Gejala | Penyebab paling mungkin | Solusi |
|---|---|---|
| Device tidak pernah muncul di `docker compose logs mosquitto` | `mqttcfg.dat` belum dimuat atau salah (server/port), IP broker salah, atau firewall belum mengizinkan TCP 18883 | `docs/windows-testing-setup.md` langkah 1, 5, 6 dan Section 6 di sini |
| Log broker: `clientId-<SN>` lalu `not authorised` | Alat belum punya `mqttcfg.dat` (default firmware `192.168.137.1:18883`, `ssl=1`, tanpa akun), atau akunnya sudah di-`revoke` / password lama | Daftarkan dengan `go run ./cmd/provision add` (Section 6), muat `mqttcfg.dat` baru |
| Device muncul di log broker, tapi QR tidak pernah datang dan device "QR Request Timeout" | Server tidak jalan | Section 5 |
| Log `device resolve failed ... UNKNOWN_DEVICE` | Migration belum `up` (seed device belum masuk), atau SN alat tidak ada di tabel `devices` (belum di-`provision add`) | Section 4.1, lalu cek tabel `devices` |
| Log `generate QR failed ... CONFIG_ERROR` | `.env.sandbox` tidak ada atau isinya kosong | Section 2 |
| Log `generate QR failed ... INVALID_REQUEST` / `INVALID_MERCHANT` / `UNAUTHORIZED` | Request atau kredensial ditolak Manjo | Lihat `response_body` di `manjo_api_logs` (Section 7) |
| Log `failed to log mqtt_messages ... invalid input syntax for type json` | Migration `000013` belum dijalankan | Section 4.1 |
| Device bersuara TTS tidak jelas, lalu "QR Request Timeout" | Server membalas pesan gagal (plain text). Firmware membacakannya lewat TTS, tapi layar tetap menunggu balasan berprefix `QR:` sampai timeout | Cari penyebabnya di log server |

---

## 10. Peralihan ke akun per alat (sekali, untuk setup yang sudah ada)

1. Matikan server lama (`Ctrl+C`), lalu `bash scripts/broker-dev-cert.sh` dan `docker compose up -d --force-recreate mosquitto`.
2. Di `.env`: `MQTT_USERNAME=payment-bridge`, isi `MQTT_PASSWORD` dan `MQTT_PROVISIONER_PASSWORD` (password acak buatanmu, **tanpa tanda kutip dan hanya huruf/angka**, misal hasil `openssl rand -hex 16`), dan `MQTT_DEVICE_SERVER=192.168.137.1`, `MQTT_DEVICE_PORT=18883`.
3. Jalankan bootstrap. Volume `mosquitto_data` di mesin dev ini sudah pernah di-bootstrap (dan `dynamic-security.json.pw` sudah dihapus), jadi script butuh password admin dari bootstrap pertama dan tidak mencetak password admin lagi. Di mesin dev ini, password itu disimpan oleh proses implementasi di `.superpowers/sdd/2026-09-30-q161-device-auth/bootstrap-output.txt`: salin ke `.env` sebagai `MQTT_ADMIN_PASSWORD=...`, **hapus file itu**, lalu jalankan `bash scripts/broker-bootstrap.sh --dev`. Kalau password admin hilang, lihat Section 3 (buat ulang hanya volume broker; **jangan `docker compose down -v`**, itu menghapus data Postgres juga).
4. Daftarkan alat fisik: `go run ./cmd/provision add -merchant MT58530503 -sn 00078020709` — file tertulis di `docs/mqttcfg.dat` (git-ignored).
5. Nonaktifkan identitas lama: `go run ./cmd/provision revoke -sn MT58530503` (hanya baris DB; riwayat transaksinya tetap).
6. Build & flash firmware baru, muat `mqttcfg.dat` ke alat lewat Downtool, lalu hapus file itu.
7. Firewall: izinkan TCP 18883 (lihat `docs/windows-testing-setup.md`).
8. `go run ./cmd/server`, lalu uji QRIS Dinamis + pembayaran lewat Alto seperti biasa.

> **Cadangan kalau TLS bermasalah.** `ssl=1` di Q161 Pro belum pernah diuji terhadap broker ini. Kalau alat tidak bisa konek, cek `docker compose logs mosquitto` untuk error TLS handshake. Fallback sementara: tambahkan listener plain di port lain (misal `listener 1884` di `docker/mosquitto.conf`), publish di docker-compose (`"18884:1884"`), set `port=18884` dan `ssl=0` di `mqttcfg.dat`, buka firewall untuk port itu, dan **hapus listener itu lagi** setelah TLS berfungsi. Akun dan ACL tetap berlaku di listener ini.

> Firmware tanpa `mqttcfg.dat` memakai default `192.168.137.1:18883` dengan `ssl=1` dan tanpa akun, sehingga **ditolak broker** (`not authorised`). SN-nya bisa dibaca dari log broker (`clientId-<SN>`): `docker compose logs mosquitto --since 5m`.
