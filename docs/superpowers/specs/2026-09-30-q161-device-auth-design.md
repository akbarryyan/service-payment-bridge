# Design Spec — Identitas & Autentikasi Per Alat Q161 Pro (Dynamic Security)

**Tanggal:** 2026-09-30
**Status:** Disetujui (brainstorming), siap masuk writing-plans
**Scope:** Setiap soundbox Q161 Pro punya identitas dan kredensial MQTT sendiri. Broker milik service ini memakai Mosquitto Dynamic Security, topic dipisah per merchant/alat, ada CLI provisioning, dan backend serta firmware disesuaikan. Hanya Q161 Pro. Q181 SE dan broker gatebymanjo tidak disentuh.
**Dokumen terkait:** `Kerjaan/Soundbox/mqtt-poc/DYNAMIC-SECURITY.md`, `mqtt-poc/mosquitto/config/acl` (pola Q181 SE), firmware Q181 SE `eclipse-workspace/Q181SoundboxZ` (parser `mqttcfg.dat`), `docs/superpowers/specs/2026-09-30-payment-polling-design.md`

---

## 1. Latar Belakang

Saat ini keamanan MQTT Q161 Pro nol:
- Firmware connect dengan username dan password kosong, tanpa TLS (`mqtt.c:172-173`, `mqtt_ssl = 0`).
- Broker memakai `allow_anonymous true` tanpa ACL (`docker/mosquitto.conf`).
- Identitas alat berasal dari konstanta build `MQTT_MERCHANT_ID`, jadi semua alat milik satu merchant memakai topic yang sama.

Akibatnya, siapa pun yang terhubung ke broker bisa:
1. **Memalsukan pembayaran.** Cukup publish daftar `.mp3` ke topic alat, dan soundbox akan mengumumkan pembayaran serta menampilkan layar "PEMBAYARAN BERHASIL".
2. **Menyadap semua QR dan pengumuman.**
3. **Meminta QR atas nama alat lain.**

Selain itu, merchant yang punya banyak toko tidak bisa dibedakan per alat: semua alat berbunyi untuk setiap pembayaran, dan balasan QR diterima semua alat.

Q161 Pro (QR dinamis) tidak melewati gatebymanjo. Alurnya soundbox ⇄ broker ⇄ payment-bridge ⇄ API Manjo. Karena itu Q161 Pro memakai **broker milik service ini sendiri**, sehingga Dynamic Security bisa dinyalakan penuh tanpa mengganggu Q181 SE, yang memakai `passwd` + `acl` di broker gatebymanjo. Kedua mekanisme itu tidak bisa berdampingan dalam satu broker.

## 2. Fakta yang Sudah Diverifikasi

| Aspek | Sumber | Nilai |
|---|---|---|
| Dynamic Security | `mqtt-poc/DYNAMIC-SECURITY.md` (diuji di Mosquitto 2.1.2) | Plugin bawaan `eclipse-mosquitto:2` (`/usr/lib/mosquitto_dynamic_security.so`). Akun dan izin dikelola lewat `$CONTROL/dynamic-security/v1`, tanpa reload. |
| Pembuatan awal | idem, bagian 2 | **Jangan** memakai `mosquitto_ctrl dynsec init` di Docker, karena file hasilnya milik `root` mode 0640 dan broker tidak bisa membacanya. Biarkan plugin membuat `dynamic-security.json` sendiri saat pertama start. Password `admin` acak ditulis ke `dynamic-security.json.pw`, yang harus dihapus setelah dibaca. Akun `democlient` bawaan dihapus. |
| Dua izin untuk menerima | idem, bagian 4 | Alat butuh `subscribeLiteral` **dan** `publishClientReceive` pada topicnya. Kalau hanya salah satu, subscribe berhasil tapi pesan tidak pernah sampai. |
| Publish yang ditolak | idem, catatan | Dibuang diam-diam. Klien tidak tahu. |
| Berbagi broker | idem, "Migrasi" | Dynamic Security dan `passwd`/`acl` tidak bisa aktif bersamaan di satu broker. |
| Format `mqttcfg.dat` | `Q181SoundboxZ/inc/def.h:100-113`, `src/param.c` | `kunci=nilai` per baris: `server`, `port`, `ssl`, `merchant`, `user`, `pass`. Kunci tak dikenal diabaikan, dan file yang tidak ada bukan error. Dimuat ke `/ext/mqttcfg.dat` lewat Downtool (MergeFile dengan `ext.txt`). |
| Mode TLS firmware Q161 Pro | `Q161ProSoundbox/src/network.c` | `ssl=0` TCP biasa. `ssl=1` TLS tanpa verifikasi server. `ssl=2` TLS dengan `ca.pem` + `cli.crt` + `pri.key` (mutual TLS). |
| SN alat | `param.c` `readSN()`, log mosquitto | SN dibaca dari hardware dan sudah dipakai sebagai `clientId-{SN}`, misalnya `00078020709`. |
| `SYS_PARAM` | `inc/def.h` | Disimpan utuh ke `sys_param.dat`. `initParam()` hanya memakai file itu kalau ukurannya **sama persis** dengan `sizeof(SYS_PARAM)`. Kalau berbeda, parameter di-reset dan **kredensial WiFi di alat hilang**. Ada `reserved[176]`. |
| Skema DB | `migrations/000002`, `000009` | `merchants.manjo_merchant_id` **tidak unik**. `devices.device_id` unik (VARCHAR 64), `devices.mqtt_topic` unik (VARCHAR 255), `devices.status` bertipe `merchant_status`. |

## 3. Keputusan

1. **Broker sendiri**, dengan Mosquitto Dynamic Security aktif penuh dan tanpa akun anonim.
2. **Identitas alat = SN hardware.** Username MQTT `{merchantId}-{SN}`, password acak unik per alat. `merchantId` di sini adalah merchant ID Manjo.
3. **Satu role per alat.** Alat tidak bisa mendengar alat lain, termasuk alat lain milik merchant yang sama.
4. **Topic hierarkis per merchant/alat.** Isi payload tidak berubah.
5. **Firmware membaca kredensial dari `/ext/mqttcfg.dat`** (format Q181 SE). Satu binary firmware untuk semua alat.
6. **TLS `ssl=1`** untuk alat. Mutual TLS (`ssl=2`) menjadi tahap penguatan terpisah.
7. **Provisioning lewat CLI di repo ini** (`cmd/provision`), tanpa endpoint HTTP admin.

## 4. Identitas, Topic, dan Izin

| Arah | Topic | Payload (tidak berubah) |
|---|---|---|
| Alat → backend (minta QR) | `qris/request/{merchantId}/{SN}` | `"{SN}\|{nominal_sen}"` |
| Backend → alat (QR, pengumuman, pesan gagal) | `topic/{merchantId}/{SN}` | `QR:…`, daftar `.mp3`, atau teks gagal |

Topic ini tidak bentrok dengan `topic_{merchant}` milik Q181 SE.

**Role Dynamic Security:**

| Role | Dipasang ke | ACL (semua `allow`) |
|---|---|---|
| `device-{merchantId}-{SN}` | client `{merchantId}-{SN}` (satu per alat) | `subscribeLiteral topic/{m}/{sn}`, `publishClientReceive topic/{m}/{sn}`, `publishClientSend qris/request/{m}/{sn}` |
| `bridge` | client `payment-bridge` (backend) | `subscribePattern qris/request/+/+`, `publishClientReceive qris/request/+/+`, `publishClientSend topic/+/+` |
| `provisioner` | client `provisioner` (CLI) | `publishClientSend $CONTROL/dynamic-security/v1`, `subscribeLiteral $CONTROL/dynamic-security/v1/response`, `publishClientReceive $CONTROL/dynamic-security/v1/response` |
| `test-all` (**dev saja**) | client `test` | `subscribePattern #`, `publishClientSend #`, `publishClientReceive #` |

Akses default Dynamic Security untuk `subscribe` dan `publishClientSend` tetap `deny`. Bootstrap menegaskannya lewat `setDefaultACLAccess`, supaya tidak bergantung pada nilai default versi plugin.

**Pemeriksaan identitas di backend:** ACL menjamin alat hanya bisa publish di topic miliknya sendiri. Di atas itu, backend memeriksa bahwa **SN di payload sama dengan SN di topic**, dan bahwa **di tabel `devices`, SN itu terdaftar dengan `manjo_merchant_id` merchant-nya sama dengan `{merchantId}` di topic**. Request yang tidak lolos pemeriksaan ini dicatat `FAILED` dengan pesan `IDENTITY_MISMATCH` dan tidak dibalas.

## 5. Broker

**`docker/mosquitto.conf`** (dev dan production):

```
per_listener_settings false
allow_anonymous false

plugin /usr/lib/mosquitto_dynamic_security.so
plugin_opt_config_file /mosquitto/data/dynamic-security.json

# Internal: backend, test, CLI provisioning. Tidak pernah dibuka ke jaringan luar.
listener 1883

# Alat: TLS.
listener 8883
certfile /mosquitto/certs/broker.crt
keyfile  /mosquitto/certs/broker.key

persistence true
persistence_location /mosquitto/data/
log_dest stdout
```

**`docker-compose.yml`:**
- Port Mosquitto dipetakan `"127.0.0.1:11883:1883"` (hanya dari laptop sendiri) dan `"18883:8883"` (untuk alat di hotspot).
- Volume `mosquitto_data` di `/mosquitto/data`, berisi `dynamic-security.json` dan data persistence.
- `./docker/certs` di-mount ke `/mosquitto/certs` secara read-only.

**Sertifikat dev:** dibuat oleh `scripts/broker-dev-cert.sh` (openssl, self-signed, CN = IP hotspot). `docker/certs/` masuk `.gitignore`, dan private key tidak pernah di-commit. Di production dipakai sertifikat asli untuk domain broker.

**Bootstrap** (`scripts/broker-bootstrap.sh [--dev]`), dijalankan sekali setelah `docker compose up -d`:
1. Membaca password `admin` dari `/mosquitto/data/dynamic-security.json.pw` hasil plugin. Kalau file itu sudah dihapus, script meminta variabel `MQTT_ADMIN_PASSWORD`.
2. `setDefaultACLAccess`: `subscribe deny`, `publishClientSend deny`.
3. Menghapus client `democlient`.
4. Membuat role `bridge` dan client `payment-bridge` dengan password `MQTT_PASSWORD` dari `.env`.
5. Membuat role `provisioner` dan client `provisioner` dengan password `MQTT_PROVISIONER_PASSWORD` dari `.env`.
6. Hanya dengan `--dev`: membuat role `test-all` dan client `test` dengan password tetap `test-dev-only`.
7. Menghapus file `.pw`, lalu mencetak pengingat untuk menyimpan password admin.

Script bisa dijalankan ulang tanpa masalah. Kalau client atau role sudah ada, script cukup mengatur password dan ACL-nya.

**Production:** port 1883 tidak dipublikasikan. Backend terhubung lewat jaringan Docker (`tcp://mosquitto:1883`). Port 8883 dibuka ke internet. `dynamic-security.json` wajib dicadangkan secara berkala, karena kehilangan file itu berarti semua alat harus didaftarkan ulang.

## 6. CLI Provisioning (`cmd/provision`)

```
go run ./cmd/provision add    -merchant <manjoMerchantId> -sn <SN> [-tenant <id>] [-store-id <id>] [-terminal-id <id>] -out <file>
go run ./cmd/provision revoke -sn <SN> [-delete]
```

Konfigurasi CLI dibaca dari `.env`:
- `DATABASE_URL`;
- `MQTT_BROKER_URL` (listener internal);
- `MQTT_PROVISIONER_USERNAME` (default `provisioner`) dan `MQTT_PROVISIONER_PASSWORD`;
- `MQTT_DEVICE_SERVER` dan `MQTT_DEVICE_PORT`, yaitu alamat broker yang ditulis ke file untuk alat (dev: `192.168.137.1` / `18883`).

**`add`:**
1. **Cari merchant** dengan `manjo_merchant_id = -merchant`. CLI harus menemukan **tepat satu** baris. Kalau tidak ada, atau ada lebih dari satu, CLI berhenti dengan pesan jelas, karena pendaftaran merchant beserta kredensial Manjo-nya di luar scope CLI ini.
2. **Kalau `devices.device_id = SN` sudah ada:**
   - milik merchant **lain**: tolak;
   - milik merchant yang **sama**: set `status = 'ACTIVE'` lalu lanjut. Dengan cara ini `add` sekaligus berfungsi untuk rotasi password dan untuk memulihkan provisioning yang sempat gagal di tengah.
3. **Kalau belum ada:** INSERT `devices` dengan `device_id = SN`, `merchant_id` (ID internal merchant), `tenant_id`, `manjo_store_id`, `manjo_terminal_id`, dan `mqtt_topic = topic/{m}/{sn}`.
4. **Buat password:** 24 karakter alfanumerik dari `crypto/rand`.
5. **Kirim perintah ke broker** lewat `$CONTROL/dynamic-security/v1`, sebagai `provisioner`:
   - `createRole device-{m}-{sn}` beserta tiga ACL dari Section 4;
   - `createClient {m}-{sn}` dengan password tadi;
   - `addClientRole`.

   Kalau client atau role sudah ada, CLI memakai `setClientPassword`, `enableClient`, dan menambahkan ACL yang belum ada, sehingga langkah ini aman diulang.
6. **Tulis `-out`:**
   ```
   server=<MQTT_DEVICE_SERVER>
   port=<MQTT_DEVICE_PORT>
   ssl=1
   merchant=<m>
   user=<m>-<sn>
   pass=<password>
   ```
   File ditulis dengan mode 0600. CLI mencetak ringkasan tanpa password, beserta peringatan bahwa file itu rahasia dan harus dihapus setelah dimuat ke alat.

**`revoke`:** `disableClient {m}-{sn}` dan `devices.status = 'INACTIVE'`. Dengan `-delete`, CLI juga menjalankan `deleteClient` dan `deleteRole`. Kalau client tidak ada di broker (misalnya alat yang dibuat sebelum desain ini), CLI tetap menonaktifkan baris DB dan memberi peringatan.

**Password** hanya tersimpan di file `-out` dan sebagai hash di broker. Password tidak pernah dicetak ke layar, dicatat ke log, atau disimpan di DB.

**Struktur kode:**
- `internal/dynsec`: klien `$CONTROL`. Mengirim `{"commands":[…]}`, menunggu balasan di `…/v1/response`, lalu mengubah field `error` per perintah menjadi Go error.
- `internal/provisioning`: urutan langkah `add` dan `revoke`, di atas `dynsec` dan `sqlc`.
- `cmd/provision`: parsing flag dan env saja.

Perintah Dynamic Security yang tidak mendukung "buat atau perbarui" ditangani dengan membedakan error "already exists" dari error lain. Hanya error lain yang menggagalkan proses.

## 7. Backend

- **`internal/qrtopic`:**
  - `RequestTopicFilter = "qris/request/+/+"`;
  - `ParseRequestTopic(topic string) (merchantID, sn string, err error)`, yang tepat membutuhkan empat segmen dengan dua segmen pertama `qris` dan `request`;
  - `BuildDeviceTopic(merchantID, sn string) string` menghasilkan `topic/{merchantID}/{sn}`.

  Konstanta `RequestTopic` dan `BuildDeviceTopic(deviceID)` yang lama dihapus.
- **Handler generate QR (`cmd/server/main.go`):** subscribe ke `qrtopic.RequestTopicFilter`. Untuk setiap pesan, handler memanggil `ParseRequestTopic` → `validation.ParseGenerateQRMessage` → memastikan `qrMsg.DeviceID == sn` → `ResolveDevice(sn)` → memastikan `device.ManjoMerchantID == merchantID`. Pesan yang gagal pemeriksaan dicatat `FAILED`/`IDENTITY_MISMATCH` tanpa balasan. Balasan dan pesan gagal dikirim ke `BuildDeviceTopic(merchantID, sn)`.
- **Pengumuman:** `transaction.PaymentCheckResult` mendapat field `ManjoMerchantID string`, yang diisi dari device yang sudah di-resolve di `queryPayment`. `announcer.Request` mendapat field `MerchantID`, dan poller meneruskannya. Announcer publish ke `BuildDeviceTopic(req.MerchantID, req.DeviceID)`.
- **Koneksi MQTT:** `MQTT_USERNAME=payment-bridge`, `MQTT_PASSWORD=<rahasia>`. `mqttclient.Connect` sudah menerima keduanya, dan tidak butuh TLS karena koneksinya lewat listener internal.
- **`.env.example`:** `MQTT_USERNAME=payment-bridge`, ditambah `MQTT_PROVISIONER_USERNAME`, `MQTT_PROVISIONER_PASSWORD`, `MQTT_DEVICE_SERVER`, `MQTT_DEVICE_PORT`.
- **Tidak ada migration DB.**
- **Test:**
  - tes `qrtopic` ditulis ulang;
  - semua test yang connect ke Mosquitto (`mqttclient`, `announcer`, `paymentpoller`, E2E `internal`) memakai akun `test`/`test-dev-only`, dibaca dari `MQTT_TEST_USERNAME`/`MQTT_TEST_PASSWORD` dengan nilai tersebut sebagai default;
  - E2E memakai topic baru;
  - tes baru untuk `IDENTITY_MISMATCH`: SN di payload berbeda dengan SN di topic, dan merchant di topic berbeda dengan merchant device;
  - tes integrasi `dynsec` dan `provisioning` terhadap broker dev, dengan merchant dan SN test sendiri, yang dibersihkan setelah selesai.

## 8. Firmware Q161 Pro

Dikerjakan di `C:\Users\Hi\eclipse-workspace\Q161ProSoundbox`, lalu disalin ke `docs/eclipse/`:

- **`inc/def.h`:**
  - `#define MQTT_CFG_FILE "/ext/mqttcfg.dat"`;
  - `SYS_PARAM` ditambah `char merchant[26]; char mqtt_user[64]; char mqtt_pass[48];`, dengan `reserved[176]` dikurangi menjadi `reserved[38]` supaya **`sizeof(SYS_PARAM)` tidak berubah**;
  - deklarasi `extern char G_deviceTopic[96]; extern char G_requestTopic[96];`;
  - `MQTT_MERCHANT_ID` tidak lagi dipakai untuk topic atau identitas.
- **`src/param.c`:**
  - parser `mqttcfg.dat` diporting dari `Q181SoundboxZ/src/param.c`;
  - `applyMqttParam()` mengisi nilai bawaan, lalu nilai dari file menimpa `mqtt_server`, `mqtt_port`, `mqtt_ssl`, `merchant`, `mqtt_user`, dan `mqtt_pass`;
  - topic disusun dengan `snprintf`: `G_deviceTopic = "topic/{merchant}/{sn}"` dan `G_requestTopic = "qris/request/{merchant}/{sn}"`;
  - kalau file tidak ada, atau `merchant`/`user`/`pass` kosong, hal itu dicatat ke log dengan jelas (alat pasti ditolak broker);
  - `mqtt_topic[32]` di `SYS_PARAM` tidak dipakai lagi untuk subscribe.
- **`src/mqtt.c`:**
  - `data.username.cstring = G_sys_param.mqtt_user`, `data.password.cstring = G_sys_param.mqtt_pass`;
  - `MQTTSubscribe(&c, G_deviceTopic, …)`;
  - request dipublish ke `G_requestTopic` dengan payload `"%s|%ld"` berisi `G_sys_param.sn` dan nominal;
  - `QRIS_REQUEST_TOPIC` dihapus.
- **Cek sintaks:** `display.c`, `mqtt.c`, dan `param.c` dicompile dengan compiler ARM dan flag yang sama seperti build Eclipse, ditambah `-fsyntax-only`.
- **Build dan flash** dilakukan user di Eclipse.

## 9. Peralihan (runbook dev)

1. `scripts/broker-dev-cert.sh`, lalu `docker compose up -d` dengan konfigurasi baru. Volume baru membuat plugin menghasilkan `dynamic-security.json`.
2. Isi `MQTT_PASSWORD` dan `MQTT_PROVISIONER_PASSWORD` di `.env`, lalu jalankan `scripts/broker-bootstrap.sh --dev`. Simpan password admin.
3. Daftarkan alat fisik: `go run ./cmd/provision add -merchant MT58530503 -sn 00078020709 -out mqttcfg.dat`.
4. Nonaktifkan baris lama: `go run ./cmd/provision revoke -sn MT58530503`. Baris ini hanya ada di DB, tanpa akun di broker. Riwayat transaksinya tetap utuh.
5. Build dan flash firmware baru, lalu muat `mqttcfg.dat` ke alat lewat Downtool.
6. Buat aturan firewall Windows untuk port **18883**, lalu hapus aturan 11883.
7. Jalankan `go run ./cmd/server`, lalu uji generate QR dan pembayaran lewat Alto seperti sebelumnya.

## 10. Testing

| Level | Cakupan |
|---|---|
| Unit | `ParseRequestTopic` (valid, jumlah segmen salah, awalan salah, segmen kosong), `BuildDeviceTopic`, pembuat password (panjang dan alfabet), penulisan `mqttcfg.dat` (isi persis, mode 0600) |
| Integrasi (DB + broker dev) | `dynsec`: buat, ubah, dan hapus client serta role lewat `$CONTROL`, termasuk error "already exists". `provisioning add` → client bisa login dan subscribe `topic/{m}/{sn}`, tapi **ditolak** subscribe topic alat lain dan **tidak bisa** publish ke `topic/…`. `add` ulang mengganti password (password lama gagal login). `revoke` → login gagal dan status `INACTIVE`. |
| Backend | Handler menolak `IDENTITY_MISMATCH` untuk kedua kasus, dan tetap membalas ke topic yang benar untuk request yang sah. Poller mengumumkan ke `topic/{m}/{sn}`. E2E memakai topic baru. |
| Firmware | Cek sintaks ARM. Uji di device: alat dengan `mqttcfg.dat` login dan menerima QR serta pengumuman. Alat tanpa file, atau dengan password salah, ditolak (terlihat di log mosquitto). Kredensial WiFi tetap ada setelah update firmware (membuktikan ukuran `SYS_PARAM` tidak berubah). |

## 11. Dokumen yang Diperbarui

- `docs/running-locally.md`: broker dengan akun, bootstrap, CLI provisioning, port 18883, dan akun `test` untuk `go test`.
- `docs/windows-testing-setup.md`: port 18883, `mqttcfg.dat`, dan Downtool.
- `docs/architecture.md` Section 7 dan 14: topic baru dan model autentikasi.
- `docs/process-flow.md` Flow 1 dan 5: topic baru dan pemeriksaan identitas.

## 12. Non-Goals

- Mutual TLS (`ssl=2`), sebagai tahap penguatan berikutnya.
- Q181 SE, broker gatebymanjo, dan migrasinya ke Dynamic Security.
- Endpoint HTTP admin atau dashboard provisioning.
- Pendaftaran merchant baru beserta kredensial Manjo-nya.
- Menampilkan SN di layar alat. SN diambil dari label alat atau dari log mosquitto (`clientId-{SN}` tetap tercatat saat login ditolak).

## 13. Risiko & Hal yang Belum Terbukti

- **Pemuatan `/ext/mqttcfg.dat` lewat Downtool** baru terbukti di hardware Q181 SE, belum di Q161 Pro.
- **`ssl=1` di Q161 Pro** belum pernah dipakai terhadap broker kita. Sertifikat self-signed diterima karena mode ini tidak memverifikasi server, sama seperti Q181 SE.
- **Ukuran `SYS_PARAM`:** kesalahan hitung akan menghapus kredensial WiFi di alat. Ukuran saat ini **508 byte**, sudah diverifikasi dengan compiler ARM dan flag build Eclipse. Layout baru (`merchant[26]`, `mqtt_user[64]`, `mqtt_pass[48]`, `reserved[38]`) juga sudah diverifikasi 508 byte. `def.h` diberi `_Static_assert(sizeof(SYS_PARAM) == 508, …)` supaya perubahan ukuran di masa depan langsung gagal saat build.
- **Nilai default Dynamic Security** bisa berbeda antar versi plugin. Karena itu bootstrap menegaskannya secara eksplisit.
