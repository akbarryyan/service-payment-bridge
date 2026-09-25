# Design Spec — Fase 2: Manjo Client

**Tanggal:** 2026-09-25
**Status:** Disetujui, siap masuk writing-plans
**Scope:** Fase 2 dari `PRD.md` Section 15 — Access Token Manager, Signature Builder (RSA & HMAC), Generate QR client, uji coba ke sandbox Manjo asli.
**Dokumen terkait:** `architecture.md` Section 4.6/13/17/18, `manjo-api-docs.md` Section 2-3, `qa.md` TC-TOKEN/TC-SIG/TC-ERR, `docs/manjo-collection/` (ground truth signature encoding)

---

## 1. Tujuan

Membangun komponen Manjo Client sebagai package Go mandiri: mampu request Access Token B2B, generate signature (dua varian), cache & auto-refresh token, dan memanggil Generate QRIS MPM ke Manjo sungguhan (sandbox). Tidak ada integrasi ke MQTT/database/Transaction Service di fase ini — itu Fase 3. Manjo Client adalah **pure component**: menerima kredensial yang sudah di-resolve (bukan "ref") lewat `Config`, tidak query DB sendiri.

## 2. Temuan dari Ground Truth (`docs/manjo-collection/`) — Berbeda dari Contoh Statis Dokumentasi

Ditemukan saat membaca script Bruno collection asli (`BI SNAP/access-token.yml`, `qr-mpm-generate.yml`), yang **dipakai sebagai acuan implementasi** (bukan `manjo-api-docs.md` yang sifatnya contoh statis):

| # | Temuan | Sumber | Implikasi |
|---|---|---|---|
| 1 | Signature Access Token (RSA-SHA256) di-encode **hex**, bukan base64 | `access-token.yml`: `sign.sign(privateKeyPem, 'hex')` | `SignAccessToken` di Go harus `hex.EncodeToString`, bukan base64 |
| 2 | Private key disimpan sebagai base64 **PKCS8 tanpa header PEM** — header `-----BEGIN PRIVATE KEY-----` ditambahkan manual saat runtime | `access-token.yml` baris 41-44 | Parsing di Go pakai `x509.ParsePKCS8PrivateKey`, bukan PKCS1 |
| 3 | Timestamp format pakai milidetik (`yyyy-MM-ddTHH:mm:ss.SSS+07:00`) di script yang benar-benar jalan, beda dari contoh di `manjo-api-docs.md` yang tanpa ms | `access-token.yml` `toJakartaISOString` | `NowJakarta()` ikut format dengan ms |
| 4 | Script lama (yang di-comment sebagai bug) pakai `.toISOString().replace('Z','+07:00')` — **salah**, itu cuma relabel UTC jadi WIB tanpa geser jam. Versi yang benar geser manual +7 jam | Comment di `access-token.yml` & `qr-mpm-query.yml` | `NowJakarta()` **wajib** benar-benar geser waktu +7 jam, bukan string replace |
| 5 | `X-PARTNER-ID` di request Generate QR nyatanya diisi `mcCodePayId` (client key), **bukan** `merchantId` seperti contoh di `manjo-api-docs.md` Section 3.3 | `qr-mpm-generate.yml` header | **Belum bisa dipastikan mana yang benar tanpa test sandbox nyata** — jadi field ini dibuat configurable di `Config`, nilai default ikut ground truth (mcCodePayId), diverifikasi lewat sandbox test |
| 6 | Base URL tidak konsisten antar file (`snapqris.manjo.co.id` vs `snapqris-uat.manjo.co.id`) | Semua file `.yml` di `BI SNAP/` | Default ke `snapqris.manjo.co.id` (dipakai 3 dari 4 file), configurable lewat `Config.BaseURL` |

Item #5 dan #6 **tidak final** — kebenarannya baru terbukti lewat sandbox test (Task terakhir plan). Kalau sandbox menolak (`401`), swap ke alternatif (`merchantId` untuk #5, `-uat` host untuk #6) adalah langkah debug pertama.

## 3. Struktur Package

```
internal/secrets/
└── provider.go       # Provider interface, EnvProvider implementasi

internal/manjoclient/
├── timestamp.go       # NowJakarta() string
├── signature.go        # SignAccessToken, SignHMAC
├── token_manager.go    # TokenManager (cache + refresh, thread-safe)
├── client.go            # Client (AccessToken, GenerateQR)
└── types.go              # Config, GenerateQRRequest/Response, error types
```

## 4. `internal/secrets` — SecretProvider

```go
type Provider interface {
    Resolve(ctx context.Context, ref string) (string, error)
}
```

`EnvProvider` — implementasi pertama: `ref` adalah nama environment variable, `Resolve` melakukan `os.LookupEnv`. Interface kecil ini dirancang supaya swap ke Vault/cloud secret manager nanti (Fase 5 Hardening, sesuai `architecture.md` Section 14) tidak mengubah kode pemanggil — hanya ganti implementasi.

**Catatan:** package ini **tidak dipakai langsung** oleh `manjoclient.Client` di fase ini (Client menerima kredensial ter-resolve lewat `Config`) — dipakai oleh test harness sandbox untuk resolve `.env.sandbox` jadi `Config`, dan nanti dipakai Device Resolver (Fase 3) untuk resolve `merchants.manjo_private_key_ref`/`manjo_client_secret_ref`.

## 5. `internal/manjoclient` — Signature Builder

```go
// SignAccessToken menghasilkan X-SIGNATURE untuk Access Token B2B (RSA-SHA256, hex).
func SignAccessToken(privateKeyPEM []byte, clientKey, timestamp string) (string, error)

// SignHMAC menghasilkan X-SIGNATURE untuk Generate QR/Query/Refund/Notify (HMAC-SHA512, hex).
func SignHMAC(clientSecret, method, path, accessToken string, body []byte, timestamp string) (string, error)
```

`SignAccessToken`: `stringToSign = clientKey + "|" + timestamp`, parse `privateKeyPEM` via `x509.ParsePKCS8PrivateKey`, sign pakai `rsa.SignPKCS1v15` dengan `crypto.SHA256`, output `hex.EncodeToString`.

`SignHMAC`: `bodyHash = hex(sha256(body))` (lowercase — `body` diasumsikan sudah hasil `json.Marshal`, otomatis minified oleh Go, tidak perlu minify manual), `stringToSign = method + ":" + path + ":" + accessToken + ":" + bodyHash + ":" + timestamp`, `signature = hex(hmac_sha512(clientSecret, stringToSign))`.

`NowJakarta() string` — geser `time.Now().UTC()` +7 jam manual (bukan string replace), format `2006-01-02T15:04:05.000-07:00` (WIB eksplisit, bukan label kosong).

## 6. `internal/manjoclient` — Token Manager

```go
type TokenManager struct { /* unexported: mutex, cached token, expiresAt, fetch func */ }

func NewTokenManager(fetch func(ctx context.Context) (token string, expiresIn time.Duration, err error)) *TokenManager
func (tm *TokenManager) Get(ctx context.Context) (string, error)
```

`Get` mengembalikan token dari cache kalau masih valid (sisa waktu ≥ 60 detik), atau panggil `fetch` (dan cache hasilnya) kalau belum ada/mendekati expired. `fetch` dan seluruh state dilindungi satu `sync.Mutex` — refresh concurrent dari beberapa goroutine otomatis ter-serialize (goroutine kedua dst. menunggu lock, lalu langsung dapat token yang baru saja di-fetch goroutine pertama tanpa fetch ulang). Ini memenuhi requirement `architecture.md` Section 12 (Concurrency) dan `qa.md` TC-CONC-04/TC-TOKEN-03.

## 7. `internal/manjoclient` — Client

```go
type Config struct {
    BaseURL       string // default: https://snapqris.manjo.co.id/api
    ClientKey     string // X-CLIENT-KEY / mcCodePayId
    PrivateKeyPEM []byte // PKCS8 PEM (sudah termasuk header BEGIN/END)
    ClientSecret  string // vkey, dipakai HMAC-SHA512
    PartnerID     string // X-PARTNER-ID — default: sama dengan ClientKey (lihat temuan #5)
    ChannelID     string // CHANNEL-ID
    HTTPClient    *http.Client // opsional, default http.Client dengan timeout wajar
}

func New(cfg Config) *Client

// AccessToken melakukan POST /v1.0/access-token/b2b, dipanggil TokenManager.fetch.
func (c *Client) AccessToken(ctx context.Context) (token string, expiresIn time.Duration, err error)

// GenerateQR melakukan POST /v1.0/qr/qr-mpm-generate, ambil token dari TokenManager otomatis.
func (c *Client) GenerateQR(ctx context.Context, req GenerateQRRequest) (*GenerateQRResponse, error)
```

`GenerateQRRequest`/`GenerateQRResponse` — field sesuai `manjo-api-docs.md` Section 3.5/3.9 (`PartnerReferenceNo`, `Amount{Value,Currency}`, `MerchantID`, `SubMerchantID` *(opsional, `omitempty`)*, `StoreID`/`TerminalID` *(opsional)*, `ValidityPeriod`, `AdditionalInfo{PaymentID,DynamicAmount,ProdDesc}` / `QRContent`, `ReferenceNo`, `AdditionalInfo.ExpireDate`, dll).

**Error handling** (`GenerateQR`): pada `401`, `Client` **tidak** retry sendiri — itu tanggung jawab pemanggil (Transaction Service, Fase 3) sesuai `architecture.md` Section 13. `Client` hanya mengembalikan error yang membawa HTTP status code (custom error type) supaya pemanggil bisa membedakan `400`/`401`/`404`/`409`/timeout/`5xx`.

`X-EXTERNAL-ID` digenerate di dalam `GenerateQR` per call (35 karakter alphanumeric, `crypto/rand` — bukan `math/rand`, untuk menghindari collision yang predictable).

## 8. Testing Plan (TDD)

| File | Jenis | Cakupan |
|---|---|---|
| `signature_test.go` | Unit | RSA: generate key pair sendiri di test, sign lalu verify pakai public key pasangannya (self-consistent, tidak butuh secret asli). HMAC: known-answer test vector (hitung expected hex manual, hardcode di test). |
| `token_manager_test.go` | Unit | Cache hit (fetch dipanggil 1x untuk 2 `Get()` berurutan), refresh saat sisa <60 detik (clock injection), concurrent `Get()` dari banyak goroutine → `fetch` cuma dipanggil 1x (jalankan dengan `-race`). |
| `client_test.go` | Unit + mock | `httptest.Server` untuk `AccessToken`/`GenerateQR` happy path, dan tiap kode error (`400`,`401`,`404`,`409`,timeout,`5xx`) — assert error type/HTTP status ter-propagate benar. |
| `sandbox_test.go` | Integration, gated | Build tag / env var `RUN_SANDBOX_TESTS=1` — kalau tidak diset, `t.Skip()` dengan pesan jelas (bukan gagal). Kalau diset: baca kredensial dari env (`.env.sandbox`), `AccessToken()` asli (assert `expiresIn≈900s`), `GenerateQR()` asli amount kecil (assert `200` + `qrContent` terisi) — ini juga yang memvalidasi temuan #5/#6. |

## 9. Kredensial Sandbox & Keamanan

- Kredensial UAT Pupuk Kalteng (`mcCodePayId=EQ1WYMK9calE`, `vkey=ZlVza91Aik`, `merchantId=MT58530503`, RSA private key) dipindah dari `docs/manjo-collection/environments/UAT Pupuk Kalteng.yml` ke `.env.sandbox` baru di root project — **gitignored**.
- `.gitignore` ditambah entry `docs/manjo-collection/environments/` supaya kredensial plaintext yang sudah ada di sana tidak ikut ter-track begitu `git init` dilakukan.
- `.env.sandbox.example` (tanpa nilai asli, cuma nama variable) dibuat sebagai referensi, mengikuti pola `.env.example` dari Fase 1.

## 10. Non-Goals (Eksplisit)

- **Tidak** ada integrasi ke MQTT/database/Transaction Service — Client murni terima `Config` dan panggil HTTP, tidak tahu apa-apa soal `merchants`/`devices`/`tenants`.
- **Tidak** ada retry logic di dalam `Client` — itu tanggung jawab pemanggil (Fase 3), `Client` cuma melapor error dengan informasi cukup untuk pemanggil memutuskan.
- **Tidak** ada Query Payment (`qr-mpm-query`) atau Refund (`qr-mpm-refund`) client di fase ini — scope Fase 2 PRD eksplisit cuma "Generate QR client". Keduanya bisa jadi task tambahan di fase ini kalau efisien untuk dikerjakan sekalian (pola HTTP-nya identik), tapi **tidak wajib** untuk deliverable Fase 2 — keputusan final ada di implementation plan.

## 11. Referensi

- `architecture.md` Section 4.6 (Manjo Client), 13 (Signature detail lama di brainstorm doc), 17 (Tech Stack — `crypto/rsa`, `crypto/sha256`, `crypto/hmac` stdlib), 18 (Struktur Proyek).
- `manjo-api-docs.md` Section 2 (Access Token B2B), 3 (Generate QRIS MPM).
- `docs/manjo-collection/BI SNAP/access-token.yml`, `qr-mpm-generate.yml` — ground truth signature encoding (Section 2 dokumen ini).
- `qa.md` Section 14 (TC-TOKEN), 13 (TC-SIG), 15 (TC-ERR), 19 (TC-SB).
