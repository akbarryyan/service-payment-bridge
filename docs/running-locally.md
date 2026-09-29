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

- **`.env`** berisi konfigurasi aplikasi: port HTTP, koneksi DB, broker MQTT, dan base URL Manjo. Nilai default di `.env.example` sudah cocok untuk setup lokal (`MANJO_BASE_URL` mengarah ke UAT Manjo).
- **`.env.sandbox`** berisi kredensial Manjo UAT (merchant sandbox "Pupuk Kalteng"): `MANJO_SANDBOX_CLIENT_KEY`, `MANJO_SANDBOX_PRIVATE_KEY`, `MANJO_SANDBOX_CLIENT_SECRET`, `MANJO_SANDBOX_MERCHANT_ID`. Minta nilainya ke pemegang kredensial, jangan dikirim lewat chat atau repo.

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
| Mosquitto | `11883` | Diteruskan ke `1883` di dalam container. Firmware Q161 Pro juga harus pakai `11883`. |

Mematikan: `docker compose down`. Data Postgres tetap tersimpan di volume `postgres_data`. Untuk menghapus data sekalian: `docker compose down -v`, **hati-hati, semua data hilang**, dan setelahnya jalankan migration lagi dari awal.

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
| `"service started"` | Server siap, sudah subscribe ke `qris/request` |
| `"generate QR failed"` | Generate QR gagal. Lihat field `error_code` dan `cause` |
| `"device resolve failed"` | `device_id` dari device tidak ada di tabel `devices`, atau device/merchant/tenant `INACTIVE` |
| `"invalid GENERATE_QR payload"` | Payload dari device tidak sesuai format `"{device_id}\|{amount_sen}"` |

Cek service hidup:

```bash
curl localhost:8080/healthz
# {"status":"ok"}
```

### Simulasi request tanpa device fisik

Di terminal lain, dengarkan balasan untuk device `MT58530503`, lalu kirim request persis seperti yang dikirim firmware (Rp50.000 = `5000000` sen):

```bash
mosquitto_sub -h localhost -p 11883 -t "topic_MT58530503" -v -C 1 -W 20 &
mosquitto_pub -h localhost -p 11883 -t "qris/request" -m "MT58530503|5000000"
```

Kalau sukses, balasannya `topic_MT58530503 QR:00020101...`. Balasan `Gagal membuat QR, coba lagi` berarti gagal, dan penyebabnya ada di log server.

---

## 6. Mengecek Data Saat Debugging

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

---

## 7. Menjalankan Test

Test integrasi memakai Postgres dan Mosquitto asli, jadi stack Docker harus jalan dan migration sudah `up`.

```bash
go test ./...
```

> **Matikan dulu `go run ./cmd/server` sebelum menjalankan test.** `TestGenerateQRFlow_EndToEnd` subscribe ke `qris/request`, topic yang sama dipakai bersama dengan server. Kalau server sedang hidup, server ikut menjawab request test dengan `"Gagal membuat QR, coba lagi"` lebih dulu, dan test gagal walaupun kodenya benar.

Test yang memanggil Manjo UAT sungguhan secara default di-skip. Untuk menjalankannya (butuh `.env.sandbox` terisi, dan membuat transaksi sungguhan di UAT):

```bash
set -a; source .env.sandbox; set +a
RUN_SANDBOX_TESTS=1 go test ./internal/manjoclient/ -run TestSandbox -v
```

---

## 8. Troubleshooting Cepat

| Gejala | Penyebab paling mungkin | Solusi |
|---|---|---|
| Device tidak pernah muncul di `docker compose logs mosquitto` | Port firmware bukan `11883`, IP broker di firmware salah, atau firewall | `docs/windows-testing-setup.md` langkah 1, 5, 6 |
| Device muncul di log broker, tapi QR tidak pernah datang dan device "QR Request Timeout" | Server tidak jalan | Section 5 |
| Log `device resolve failed ... UNKNOWN_DEVICE` | Migration belum `up` (seed device belum masuk), atau `device_id` di firmware (`MQTT_MERCHANT_ID`) tidak ada di tabel `devices` | Section 4.1, lalu cek tabel `devices` |
| Log `generate QR failed ... CONFIG_ERROR` | `.env.sandbox` tidak ada atau isinya kosong | Section 2 |
| Log `generate QR failed ... INVALID_REQUEST` / `INVALID_MERCHANT` / `UNAUTHORIZED` | Request atau kredensial ditolak Manjo | Lihat `response_body` di `manjo_api_logs` (Section 6) |
| Log `failed to log mqtt_messages ... invalid input syntax for type json` | Migration `000013` belum dijalankan | Section 4.1 |
| Device bersuara TTS tidak jelas, lalu "QR Request Timeout" | Server membalas pesan gagal (plain text). Firmware membacakannya lewat TTS, tapi layar tetap menunggu balasan berprefix `QR:` sampai timeout | Cari penyebabnya di log server |
