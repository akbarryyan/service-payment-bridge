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

## 2. Pindahkan Project ke WSL2

Salin **seluruh folder project** `service-payment-bridge/` (bukan cuma `docs/eclipse/`) ke dalam filesystem WSL2 sendiri — contoh path: `~/service-payment-bridge` di home directory distro WSL2-mu.

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

Expected: migration `1` sampai `11` berhasil (`.../u ...`), tanpa error.

## 5. Izinkan Windows Firewall

Dari PowerShell **as Administrator**:

```powershell
netsh advfirewall firewall add rule name="MQTT-11883" dir=in action=allow protocol=TCP localport=11883
```

> **Catatan:** Docker Desktop otomatis meneruskan port container ke `localhost`/semua-interface di sisi Windows lewat integrasi WSL2 — begitu Step 3 selesai, port `11883` seharusnya sudah reachable dari `<IP-hotspot-Windows>:11883` tanpa perlu `netsh portproxy` manual. Firewall rule di atas cuma memastikan Windows tidak memblokirnya dari perangkat lain di jaringan.

## 6. Update Firmware & Build Ulang

Buka `docs/eclipse/src/param.c`, ganti baris:

```c
strcpy(G_sys_param.mqtt_server, "<IP-hotspot-Windows-dari-Step-1>");
```

Build project di Eclipse (Windows), lalu flash ke Q161 Pro seperti biasa.

## 7. Sambungkan Q161 Pro ke Hotspot Windows

Di device: WiFi setup → pilih SSID hotspot Windows yang baru dibuat di Step 1, masukkan password.

## 8. Verifikasi

Karena semua ini jalan di Windows (di luar jangkauan tool Claude Code yang berjalan di sesi Linux), verifikasi konektivitas perlu dilakukan manual:

- **Cek broker menerima koneksi** — dari terminal WSL2:
  ```bash
  docker compose logs mosquitto --since 5m
  ```
  Cari baris `New client connected ... as clientId-<SN>` dari IP di range hotspot Windows (`192.168.137.x`).

- **Cek traffic mentah** (opsional, untuk debug manual) — install `mosquitto-clients` di WSL2 kalau belum ada (`sudo apt install mosquitto-clients`), lalu:
  ```bash
  mosquitto_sub -h localhost -p 11883 -t '#' -v
  ```
  Jalankan ini SEBELUM trigger "QRIS Dinamis" di device, biarkan berjalan, baru lakukan langkah di device.

## 9. Kalau Ada Kendala

Kirim ke Claude Code (sesi ini masih di Linux, tapi bisa bantu diagnosa dari hasil yang kamu kirim):
- Output `docker compose logs mosquitto --since 5m`
- Output `ipconfig` (bagian adapter hotspot)
- Apa yang tampil di layar Q161 Pro

---

**Referensi terkait:**
- `docs/eclipse/src/mqtt.c`, `src/param.c`, `inc/def.h` — kontrak MQTT firmware asli.
- `docs/superpowers/specs/2026-09-25-mqtt-contract-reconciliation-design.md` — desain rekonsiliasi kontrak MQTT (backend belum diimplementasikan mengikuti kontrak ini, itu langkah setelah testing konektivitas dasar berhasil).
