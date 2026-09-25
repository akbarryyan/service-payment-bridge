# Design Spec — Fase 3: Generate QR Flow

**Tanggal:** 2026-09-25
**Status:** Disetujui, siap masuk writing-plans
**Scope:** Fase 3 dari `PRD.md` Section 15 — Flow A lengkap: MQTT Consumer → Message Parser/Validator → Device Resolver → Transaction Service → Manjo Client → MQTT Publisher.
**Dokumen terkait:** `architecture.md` Section 4-9/13, `process-flow.md` Flow 1 & 2, `schema.md`, `qa.md` TC-GQR/TC-IDM/TC-CONC/TC-ERR, plan Fase 1 & Fase 2 (kode yang sudah ada)

---

## 1. Tujuan

Mengimplementasikan siklus penuh "merchant input amount di Q161 → QRIS tampil di device" — dari pesan MQTT masuk sampai `qris_payload` terkirim balik — terintegrasi dengan database (Fase 1) dan Manjo Client (Fase 2). Auto-expire job, retry queue durable, dan reconciliation via Query Payment **bukan** scope fase ini (lihat Non-Goals).

## 2. Keputusan (hasil brainstorming)

| # | Keputusan | Rasional |
|---|---|---|
| 1 | Error `409` (X-EXTERNAL-ID conflict) **disederhanakan** — langsung `FAILED` + log jelas, **tanpa** Query Payment reconciliation | Query Payment client belum dibangun (masih "Should" priority, belum final untuk v1). Reconciliation penuh menyusul fase terpisah. |
| 2 | `transaction_id` = `TRX-{yyyyMMdd}-{6 karakter random alfanumerik}`, retry generate ulang kalau kena `UNIQUE` constraint violation | Sesuai proses yang sudah didokumentasikan `process-flow.md` step 6 — collision sangat jarang, tidak perlu koordinasi/lock tambahan |
| 3 | Device Resolver **query DB langsung**, tanpa cache in-memory | YAGNI — optimasi performa menyusul Fase 5 Hardening kalau terbukti perlu |

## 3. Temuan Tambahan Saat Desain (Belum Pernah Dibahas Eksplisit)

**`manjoclient.Client` harus di-cache per merchant, bukan dibuat ulang per request.** Kalau tidak, `TokenManager` di dalamnya (dengan cache access token-nya) jadi percuma — setiap request akan fetch access token baru, padahal Fase 2 dirancang supaya token di-reuse selama masih valid. Solusi: `manjoclient.Registry` — cache `*Client` in-memory keyed by `merchant_id`, dibuat lazy saat pertama kali dibutuhkan, bertahan sepanjang hidup proses (tidak perlu TTL/invalidasi — kredensial merchant jarang berubah, dan kalaupun berubah, restart service sudah cukup untuk fase ini).

**`TokenManager` butuh method `Invalidate()` baru** (tambahan kecil ke kode Fase 2) — dibutuhkan untuk retry-sekali setelah `401` (`architecture.md` Section 13.1): token yang di-cache mungkin sudah di-revoke di sisi Manjo meski belum melewati threshold expiry kita sendiri, jadi perlu cara paksa refresh.

## 4. Struktur Komponen

```
internal/database/queries/
├── merchants.sql          # GetMerchantByID (baru)
├── devices.sql              # GetDeviceWithMerchantAndTenant — join devices+merchants+tenants (baru)
├── transactions.sql          # CreateTransaction, MarkQRGenerated, MarkFailed (baru)
├── mqtt_messages.sql          # LogMQTTMessage (baru)
└── manjo_api_logs.sql          # LogManjoAPICall (baru)

internal/qrtopic/topic.go        # Parse/Build topic/{merchant_id}/{tenant_slot}/{device_id}

internal/resolver/resolver.go     # ResolveDevice(ctx, deviceID) — query DB langsung, join merchant+tenant

internal/validation/validator.go   # ParseAndValidateGenerateQR(payload) — type & amount check

internal/manjoclient/registry.go    # Registry — cache *Client per merchant_id
internal/manjoclient/token_manager.go # +Invalidate() (modify, kode Fase 2)

internal/transaction/service.go      # Service.GenerateQR() — orkestrasi penuh + retry logic (401/timeout/5xx)
internal/transaction/idgen.go          # generateTransactionID()

internal/mqttclient/client.go           # +Publish(), +Subscribe() (modify, kode Fase 1)

cmd/server/main.go                       # wire semua komponen, Subscribe("topic/#", handler) (modify, kode Fase 1)
```

## 5. Kontrak Payload (Sudah Final dari Dokumen Sebelumnya)

**Inbound** (`architecture.md` Section 7.1): `{"type":"GENERATE_QR","amount":50000}` di topic milik device sendiri.

**Outbound sukses** (Section 7.2): `{"type":"QR_RESULT","transaction_id":"...","status":"SUCCESS","qris_payload":"...","expire_at":"..."}` — **dipublish ke topic yang sama** dengan request masuk (request-reply pattern, bukan lookup `devices.mqtt_topic` — itu khusus Payment Notification di Fase 4 yang triggernya dari webhook Manjo, bukan dari pesan MQTT masuk).

**Outbound gagal**: `{"type":"QR_RESULT","transaction_id":"...","status":"FAILED","error":"..."}`.

## 6. Alur Eksekusi (Ringkas — Detail Presisi di Plan)

1. MQTT Consumer terima pesan di `topic/#`, `qrtopic.Parse` topic → dapat `device_id` (kalau format topic invalid → drop, tidak dicatat, sesuai `process-flow.md` step 2).
2. `validation.ParseAndValidateGenerateQR` payload → kalau invalid → `LogMQTTMessage` (INBOUND, FAILED) → stop.
3. `resolver.ResolveDevice(deviceID)` → kalau device/merchant/tenant tidak ditemukan atau tidak `ACTIVE` → `LogMQTTMessage` FAILED dengan error code spesifik (`UNKNOWN_DEVICE`/`DEVICE_INACTIVE`/`MERCHANT_INACTIVE`/`TENANT_INACTIVE`) → stop.
4. `transaction.Service.GenerateQR`:
   - Generate `transaction_id`, `CreateTransaction` (retry kalau collision) → `LogMQTTMessage` INBOUND PROCESSED (link `transaction_id`).
   - `manjoclient.Registry.GetOrCreate(merchantID, ...)` → resolve `manjo_private_key_ref`/`manjo_client_secret_ref` lewat `secrets.Provider` (Fase 2) → dapat `*Client`.
   - Panggil `GenerateQR()`, `LogManjoAPICall` (request/response di-mask). Retry policy (`architecture.md` 13.1): `401` → `TokenManager.Invalidate()` + retry 1x; timeout/`5xx` → retry maks 3x; `409` → langsung `FAILED` (keputusan #1); `400`/`404` → `FAILED` tanpa retry.
   - Sukses → `MarkQRGenerated` (`qris_payload`, `reference_no`, `expire_at` dari `additionalInfo.expireDate`). Gagal → `MarkFailed`.
5. MQTT Publisher build `QR_RESULT`, `Publish` ke topic asal → `LogMQTTMessage` OUTBOUND. Publish gagal → retry in-memory beberapa kali dengan backoff (non-durable, cukup untuk fase ini — **tidak** mengubah status transaksi, sesuai `architecture.md`).

## 7. Testing Plan

| Level | Cakupan |
|---|---|
| Unit | `qrtopic` (parse valid/invalid/sentinel tenant), `validation` (amount<=0, JSON invalid, type salah), `idgen` (format, collision → generate ulang), `manjoclient.Registry` (cache hit, tidak create Client dobel per merchant — `-race`), `TokenManager.Invalidate` (Get() sesudahnya fetch baru) |
| Integration (mock Manjo + Postgres/Mosquitto dari `docker-compose` Fase 1) | Happy path penuh (publish MQTT → `QR_RESULT` SUCCESS diterima balik), tiap kode error `qa.md` TC-ERR (400/401/404/409/timeout/5xx), TC-IDM (retry tidak duplikat transaksi), TC-CONC (banyak merchant/device paralel) |
| Sandbox nyata | Seed 1 row `merchants`+`devices` di DB (kredensial merujuk `.env.sandbox` lewat `secrets.EnvProvider`, sama pola Fase 2), publish MQTT asli via Mosquitto lokal, verifikasi `QR_RESULT` dengan `qris_payload` nyata diterima balik di topic yang benar |

## 8. Non-Goals (Eksplisit)

- **Payment Notification flow (Fase 4)** — webhook HTTP endpoint, routing via `transactions.device_id` untuk notifikasi pembayaran — sama sekali di luar scope fase ini.
- **Query Payment / reconciliation** — sesuai keputusan #1, `409` disederhanakan tanpa Query Payment.
- **Retry queue durable** (persisted, survive restart) untuk MQTT publish failure — versi in-memory sederhana cukup untuk fase ini, hardening penuh di Fase 5.
- **Device Resolver caching** — sesuai keputusan #3, query DB langsung.
- **Auto-expire job** — Fase 5.
- **CRUD untuk `merchants`/`tenants`/`devices`** — tabel ini dibaca saja (konsisten dengan Non-Goal yang sudah ditetapkan sebelumnya), data seed untuk testing dimasukkan manual/lewat migration seed, bukan lewat API.

## 9. Referensi

- `architecture.md` Section 4 (Komponen), 7 (Kontrak Internal), 9 (Data Model), 13.1 (Retry Generate QR).
- `process-flow.md` Flow 1 (Generate QR) & Flow 2 (Token Refresh sub-flow).
- `schema.md` Section 5-9 (`tenants`, `devices`, `transactions`, `mqtt_messages`, `manjo_api_logs`).
- Plan Fase 1 (`2026-09-25-project-setup-fase1.md`) — struktur `internal/database`, `internal/mqttclient` yang di-extend.
- Plan Fase 2 (`2026-09-25-fase2-manjo-client.md`) — `internal/manjoclient`, `internal/secrets` yang dipakai/di-extend.
