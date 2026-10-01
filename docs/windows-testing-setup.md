# Setup Testing di Windows (WSL2 + Mobile Hotspot)

**Tujuan:** Menjalankan Payment Bridge Service (Postgres, Mosquitto, backend Go) di Windows lewat WSL2, dengan Q161 Pro connect ke Windows Mobile Hotspot — supaya laptop tetap punya internet (tidak seperti hotspot dari Linux yang memutus koneksi WiFi utama).

**Prasyarat:** WSL2 + Docker Desktop sudah terpasang (dengan integrasi WSL2 aktif).

---

## 1. Aktifkan Windows Mobile Hotspot

1. Settings → Network & Internet → Mobile hotspot.
2. **"Share my Internet connection from"**: pilih WiFi (atau Ethernet kalau laptop pakai kabel — lebih stabil daripada berbagi dari WiFi yang sama).
3. Turn on. Catat nama & password hotspot yang ditampilkan (atau ganti sesuai keinginan).
4. Cek IP hotspot: buka Command Prompt →
   ```
   ipconfig
   ```
   Cari adapter bernama **"Local Area Connection* X"** atau **"Microsoft Wi-Fi Direct Virtual Adapter"**. IP default biasanya `192.168.137.1` — catat nilai persisnya.

## 2. (Opsional) Pindahkan Project ke WSL2

Project juga bisa dijalankan langsung dari Windows (Git Bash/PowerShell + Docker Desktop), dan setup itulah yang sudah terbukti jalan dengan device fisik. Lihat `docs/running-locally.md`. Kalau lebih suka WSL2, salin **seluruh folder project** `service-payment-bridge/` ke filesystem WSL2 sendiri (mis. `~/service-payment-bridge`).

> **Kenapa di dalam WSL2, bukan di `/mnt/c/...`?** Docker & I/O jauh lebih cepat kalau project ada di filesystem native WSL2, bukan di mount Windows.

## 3. Jalankan Stack Docker

Di terminal WSL2:

```bash
cd ~/service-payment-bridge
docker compose up -d
docker compose ps   # pastikan postgres & mosquitto berstatus healthy/up
```

## 4. Jalankan Migration Database

Kalau `golang-migrate` belum terpasang di WSL2:

```bash
go install -tags 'postgres' github.com/golang-migrate/migrate/v4/cmd/migrate@latest
```

Lalu:

```bash
export DATABASE_URL="postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
migrate -path migrations -database "$DATABASE_URL" up
```

Expected: semua migration di folder `migrations/` berhasil (`.../u ...`), tanpa error. Detailnya di `docs/running-locally.md` Section 4.

## 5. Izinkan Windows Firewall

Dari PowerShell **as Administrator**:

```powershell
netsh advfirewall firewall add rule name="MQTT-TLS-18883" dir=in action=allow protocol=TCP localport=18883 remoteip=192.168.137.0/24
```

`remoteip=192.168.137.0/24` membatasi akses ke subnet hotspot Windows, sehingga hanya alat di hotspot yang bisa menjangkau port 18883 (bukan seluruh LAN).

Kalau sebelumnya sudah ada rule lama `MQTT-11883`, hapus saja (`netsh advfirewall firewall delete rule name="MQTT-11883"`), karena port 11883 sekarang hanya listen di `127.0.0.1`.

> **Catatan:** Docker Desktop otomatis meneruskan port container ke `localhost`/semua-interface di sisi Windows lewat integrasi WSL2 — begitu Step 3 selesai, port `18883` (TLS) seharusnya sudah reachable dari `<IP-hotspot-Windows>:18883` tanpa perlu `netsh portproxy` manual. Firewall rule di atas cuma memastikan Windows tidak memblokirnya dari perangkat lain di jaringan.

## 6. Update Firmware & Build Ulang

Source firmware yang di-build ada di `C:\Users\Hi\eclipse-workspace\Q161ProSoundbox` (`docs/eclipse/` di repo ini cuma salinan untuk referensi). Firmware **tidak perlu diedit per alat** lagi: build sekali, lalu flash ke Q161 Pro seperti biasa.

Server, port, dan kredensial setiap alat dibaca firmware dari `/ext/mqttcfg.dat` — file itu dibuat oleh `go run ./cmd/provision add` (lihat `docs/running-locally.md` Section 6) dan dimuat ke alat lewat Downtool (MergeFile dengan `ext.txt`, lalu Download), persis seperti berkas audio.

## 7. Sambungkan Q161 Pro ke Hotspot Windows

Di device: WiFi setup → pilih SSID hotspot Windows yang baru dibuat di Step 1, masukkan password.

## 8. Verifikasi

- **Cek broker menerima koneksi:**
  ```bash
  docker compose logs mosquitto --since 5m
  ```
  Cari baris `New client connected ... as clientId-<SN> (p4, c0, k60, u'<merchant>-<SN>')`. Alat yang belum punya `mqttcfg.dat` muncul sebagai `clientId-<SN>` diikuti disconnect `not authorised` — begitulah cara membaca SN-nya. IP sumbernya akan tampil sebagai gateway Docker (mis. `172.19.0.1`), bukan `192.168.137.x`. Itu normal karena koneksi lewat port-forward Docker Desktop.

- **Cek traffic mentah** (opsional, untuk debug manual) — install `mosquitto-clients` di WSL2 kalau belum ada (`sudo apt install mosquitto-clients`), lalu:
  ```bash
  mosquitto_sub -h localhost -p 11883 -u test -P test-dev-only -t '#' -v
  ```
  Jalankan ini SEBELUM trigger "QRIS Dinamis" di device, biarkan berjalan, baru lakukan langkah di device.

## 9. Kalau Ada Kendala

Kumpulkan ini untuk diagnosa:
- Output `docker compose logs mosquitto --since 5m`
- Log backend (terminal tempat `go run ./cmd/server` jalan). Setiap kegagalan generate QR tercatat sebagai `"generate QR failed"` lengkap dengan `error_code` dan `cause`.
- Output `ipconfig` (bagian adapter hotspot)
- Apa yang tampil di layar Q161 Pro
- Isi `transactions`, `mqtt_messages`, `manjo_api_logs` terbaru (query-nya ada di `docs/running-locally.md` Section 7)

---

**Referensi terkait:**
- `docs/running-locally.md` — cara menjalankan service, migration, dan test.
- `docs/eclipse/src/mqtt.c`, `src/param.c`, `inc/def.h` — kontrak MQTT firmware asli.
- `docs/superpowers/specs/2026-09-25-mqtt-contract-reconciliation-design.md` — desain rekonsiliasi kontrak MQTT (sudah diimplementasikan dan terverifikasi dengan device fisik).
