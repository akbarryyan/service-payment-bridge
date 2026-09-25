# Design Spec — Rekonsiliasi Kontrak MQTT ke Firmware Q161 Pro Asli

**Tanggal:** 2026-09-25
**Status:** Disetujui, siap masuk writing-plans
**Scope:** Mengganti skema topic & format payload MQTT di Payment Bridge Service (dibangun di Fase 3) supaya cocok dengan kontrak yang benar-benar diimplementasikan firmware Q161 Pro asli (`docs/eclipse/`), bukan asumsi desain di `architecture.md`/`brainstorm-q161-updated.md`.
**Dokumen terkait:** `docs/eclipse/src/mqtt.c`, `docs/eclipse/src/display.c`, `docs/eclipse/inc/def.h` (ground truth firmware), plan Fase 3 (`2026-09-25-fase3-generate-qr-flow.md`), plan multi-tenant (`2026-09-25-multi-tenant-device-topic.md`)

---

## 1. Latar Belakang

Saat mempersiapkan test dengan Q161 Pro fisik, pembacaan source firmware (`docs/eclipse/`) mengungkap kontrak MQTT yang **berbeda total** dari yang diasumsikan saat membangun Fase 3 dan skema multi-tenant. Firmware adalah kenyataan yang sudah di-compile untuk hardware — Service yang harus menyesuaikan diri, bukan sebaliknya.

## 2. Ground Truth dari Firmware (`docs/eclipse/`)

| Aspek | Ditemukan di | Perilaku nyata |
|---|---|---|
| Topic subscribe (device dengar balasan) | `param.c` `applyMqttParam()`, `mqtt.c:192` | `topic_{MQTT_MERCHANT_ID}` — flat, contoh `topic_MT58530503`. Tidak ada level tenant/device. |
| Topic publish (device kirim request) | `def.h:36`, `mqtt.c:236` | `qris/request` — **topic tetap & shared** untuk semua device, tidak spesifik per merchant. |
| Payload request | `mqtt.c:228` | Plain text `"{MQTT_MERCHANT_ID}\|{amount}"`, contoh `"MT58530503\|5000000"`. |
| Unit amount | `display.c:167,175,183` | **Sen** (Rupiah × 100) — `amt/100` dipakai buat tampilkan "Rp X.YY". |
| Payload balasan sukses | `mqtt.c:73-83`, `def.h:37` | Plain text `"QR:{qrContent}"` (prefix `QR:`). |
| Notifikasi audio pembayaran | `mqtt.c:87-124` | Topic **sama** dengan balasan QR (`topic_{merchant_id}`), dikenali dari payload mengandung `.mp3`, `+`-separated (pola Q181). |
| Balasan gagal | `mqtt.c:125-128` | **Tidak ada skema eksplisit** — apa pun selain `QR:`/`.mp3` dibaca teks biasa via `AppPlayTip` (TTS). |
| Identitas device | `def.h:11` | `MQTT_MERCHANT_ID` = constant compile-time, satu binary = satu identitas. Saat ini `"MT58530503"` (literal Manjo merchant_id). |
| Client ID / sesi | `mqtt.c:171,179` | `clientId-{SN}`, `cleansession=0` (sesi persisten, QoS1 mengantre saat device offline), QoS 1 pub & sub. |

## 3. Keputusan (hasil brainstorming)

1. **`MQTT_MERCHANT_ID` (`"MT58530503"`) diperlakukan sebagai `device_id`** di backend — tidak ada perubahan firmware lagi untuk sekarang. Backend resolve device_id ini ke merchant/tenant lewat DB seperti biasa (Device Resolver sudah device_id-sentris, tidak berubah).
2. **Payment Bridge Service menyesuaikan diri ke kontrak firmware**, bukan minta firmware diubah — firmware adalah kenyataan hardware yang sudah di-compile.
3. **Balasan gagal pakai plain text manusiawi** (bukan kode error terstruktur) — firmware tidak punya skema untuk itu; kode error asli tetap dicatat di `manjo_api_logs`/`mqtt_messages` untuk debugging internal.

## 4. Skema Topic Baru

- **Inbound**: topic tetap `qris/request`, subscribe sekali (bukan wildcard `topic/#`).
- **Outbound**: `topic_{device_id}` (flat, underscore — persis pola firmware), diambil dari `devices.mqtt_topic` di DB, **bukan** hasil parsing topic pesan masuk (karena inbound sekarang shared, bukan per-device).

**Efek samping positif:** masalah "self-echo" yang ditemukan saat sandbox testing Fase 3 (subscribe `topic/#` menangkap publish balik sendiri) otomatis hilang — subscribe dan publish sekarang topic yang berbeda.

## 5. Format Payload Baru

- **Inbound**: `"{device_id}|{amount_sen}"` — split di `|`; bagian pertama = `device_id` (dipakai Device Resolver, bukan dari topic lagi); bagian kedua = integer sen, dibagi 100 → Rupiah sebelum diteruskan ke `transaction.Service.GenerateQR` (yang tetap menerima Rupiah, tidak berubah).
- **Outbound sukses**: `"QR:{qrContent}"`.
- **Outbound gagal**: string manusiawi berbahasa Indonesia, contoh `"Gagal membuat QR, coba lagi"` — jatuh ke fallback TTS generik firmware (`else { AppPlayTip(buf); }`).

## 6. Komponen yang Berubah

| Komponen | Perubahan |
|---|---|
| `internal/qrtopic` | Hapus `Parse`/`Topic` hierarkis. Ganti dengan `BuildDeviceTopic(deviceID string) string` → `"topic_" + deviceID`. Konstanta `RequestTopic = "qris/request"`. |
| `internal/validation` | Ganti `ParseAndValidateGenerateQR` (JSON) dengan parser pipe-delimited: `ParseGenerateQRMessage(payload []byte) (deviceID string, amountRupiah int64, err error)` — validasi: ada tepat 1 `|`, device_id tidak kosong, amount (sen) positif & habis dibagi 100 dengan bersih *(kalau tidak, tetap proses dengan pembulatan ke bawah — bukan error, karena firmware sendiri selalu kirim kelipatan 100 dari alur input `GetAmount`)*. |
| `cmd/server/main.go` | Subscribe cuma `qris/request` (bukan wildcard). Handler: parse payload → dapat `device_id` & amount → Device Resolver by `device_id` → Transaction Service → build `"QR:..."` atau pesan gagal plain text → publish ke `qrtopic.BuildDeviceTopic(device.DeviceID)`. |
| `internal/transaction`, `internal/resolver`, `internal/manjoclient` | **Tidak berubah** — sudah device_id-sentris, cuma sumber device_id-nya pindah dari topic ke payload. |
| Migration DB | Seed baru: `devices.device_id='MT58530503'`, `mqtt_topic='topic_MT58530503'` — menggantikan seed sintetik `SANDBOX-DEVICE-001` (sekarang device_id cocok persis device fisik). |

## 7. Dampak ke Desain & Dokumen Sebelumnya

- **Skema DB multi-tenant (`tenants`/`devices`) tetap valid** — cuma skema TOPIC hierarkisnya yang usang (firmware tidak mendukung level tenant di topic). Diferensiasi tenant sungguhan (kalau dibutuhkan nanti) tetap lewat `device_id` unik per unit fisik, bukan topic bertingkat.
- `architecture.md` Section 3/7 (skema topic), Section 4.4 (Device Resolver — sumber device_id), `brainstorm-q161-updated.md` Section 3, `process-flow.md` Flow 1 step 1-2 perlu update mengikuti kontrak baru ini (task terpisah di implementation plan, bukan didesain ulang di sini).
- Test Fase 3 yang mengasumsikan JSON+topic hierarkis (`qrtopic_test.go`, `validation_test.go`, `qrflow_test.go`) perlu ditulis ulang mengikuti kontrak baru.

## 8. Non-Goals

- **Tidak** mengubah firmware lagi di luar perubahan broker address yang sudah dilakukan (`param.c` → IP lokal).
- **Tidak** membangun ulang skema multi-tenant/multi-device — tabelnya tetap dipakai apa adanya.
- **Tidak** menambah skema error terstruktur ke protokol device — firmware tidak mendukungnya, plain text sudah cukup untuk fase ini.

## 9. Referensi

- `docs/eclipse/src/mqtt.c`, `src/display.c`, `src/param.c`, `inc/def.h` — ground truth firmware.
- Plan Fase 3 (`docs/superpowers/plans/2026-09-25-fase3-generate-qr-flow.md`) — kode yang diganti/disesuaikan.
- Plan multi-tenant (`docs/superpowers/plans/2026-09-25-multi-tenant-device-topic.md`) — skema DB yang tetap dipertahankan.
