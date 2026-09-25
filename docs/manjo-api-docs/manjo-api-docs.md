# Manjo QRIS MPM API Documentation

**Version:** 1.0
**Service:** QRIS Merchant Presented Mode (MPM)
**Provider:** Manjo
**Last Updated:** September 2026

---

## 1. Overview

Dokumentasi ini menjelaskan integrasi API QRIS **Merchant Presented Mode (MPM)** antara **Merchant** dan **Manjo**.

Integrasi terdiri dari tiga tahap utama:

```text
┌──────────────────────┐
│ 1. Access Token B2B  │
│ Merchant → Manjo     │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│ 2. Generate QRIS MPM │
│ Merchant → Manjo     │
└──────────┬───────────┘
           │
           ▼
     QRIS QR Content
           │
           ▼
┌──────────────────────┐
│ Customer Scan & Pay  │
└──────────┬───────────┘
           │
           ▼
┌─────────────────────────┐
│ 3. Payment Notification │
│ Manjo → Merchant        │
└─────────────────────────┘
```

### Flow Singkat

1. Merchant melakukan autentikasi ke Manjo untuk mendapatkan **Access Token**.
2. Merchant menggunakan Access Token untuk melakukan request **Generate QRIS MPM**.
3. Manjo mengembalikan `qrContent`.
4. Merchant menampilkan QRIS kepada customer.
5. Customer melakukan pembayaran menggunakan aplikasi pembayaran yang mendukung QRIS.
6. Setelah pembayaran diproses, Manjo mengirimkan **Payment Notification** ke Merchant.
7. Merchant memproses status transaksi berdasarkan notification tersebut.

---

# 2. API Access Token B2B

## 2.1 Overview

API Access Token B2B digunakan oleh Merchant untuk mendapatkan **OAuth 2.0 Access Token** dari Manjo.

Autentikasi menggunakan grant type:

```text
client_credentials
```

Flow ini digunakan untuk komunikasi **machine-to-machine** tanpa melibatkan user secara langsung.

Access Token memiliki masa berlaku terbatas, yaitu **900 detik atau 15 menit**.

### Karakteristik

* HTTP Method: `POST`
* Grant Type: `client_credentials`
* Token Type: `Bearer`
* Token Expiry: `900 seconds`
* Signature: `SHA256withRSA`
* Direction: `Merchant → Manjo`

---

## 2.2 General Information

| Field        | Value                    |
| ------------ | ------------------------ |
| Service Code | `73`                     |
| Name         | Access Token B2B         |
| Direction    | Merchant → Manjo         |
| Version      | `1.0`                    |
| HTTP Method  | `POST`                   |
| Path         | `/v1.0/access-token/b2b` |

---

## 2.3 Authentication Flow

```text
Merchant
   │
   │ X-CLIENT-KEY
   │ X-TIMESTAMP
   │ X-SIGNATURE
   │ grantType=client_credentials
   ▼
Manjo
   │
   │ Validate Signature
   │ Validate Client
   │
   ▼
Access Token
   │
   │ expiresIn = 900
   ▼
Merchant
```

### Sequence Diagram

![Flow Access Token B2B](assets/flow-access-token-b2b.png)

Merchant menyiapkan `X-CLIENT-KEY` dan private key, lalu men-generate `X-SIGNATURE` (string to sign: `Client_ID + "|" + X-TIMESTAMP`) sebelum mengirim `POST /v1.0/access-token/b2b`. Manjo memvalidasi client key dan signature — jika kredensial tidak valid, Manjo membalas `401 Unauthorized`; jika valid, Manjo men-generate access token dan mengembalikannya (`accessToken`, `expiresIn`). Merchant menyimpan `accessToken` secara aman untuk dipakai sebagai Bearer token pada request API selanjutnya.

---

## 2.4 Request Headers

| Field          | Required | Type   | Description           |
| -------------- | -------- | ------ | ---------------------- |
| `Content-Type` | M        | String | Media type request    |
| `X-TIMESTAMP`  | M        | String | Timestamp client      |
| `X-CLIENT-KEY` | M        | String | Client ID / PJP Name  |
| `X-SIGNATURE`  | M        | String | RSA SHA-256 signature |

### Content-Type

```http
Content-Type: application/json
```

### X-TIMESTAMP

Format:

```text
yyyy-MM-ddTHH:mm:ssTZD
```

Contoh:

```text
2025-11-27T08:05:41+07:00
```

Timestamp harus menyertakan timezone.

### X-CLIENT-KEY

Berisi `client_id` yang diberikan ketika proses registrasi.

Contoh:

```text
EP9613058999
```

### X-SIGNATURE

Signature menggunakan:

```text
SHA256withRSA
```

Formula:

```text
stringToSign = X-CLIENT-KEY + "|" + X-TIMESTAMP
```

Kemudian:

```text
X-SIGNATURE = RSA_SHA256_SIGN(PrivateKey, stringToSign)
```

Private key diperoleh pada proses registrasi.

> Signature harus dibuat ulang pada setiap request karena signature menggunakan timestamp.

---

## 2.5 Request Body

Request body hanya membutuhkan satu parameter:

```json
{
  "grantType": "client_credentials"
}
```

### Parameter

| Parameter   | Required | Type   | Description          |
| ----------- | -------- | ------ | --------------------- |
| `grantType` | M        | String | OAuth 2.0 grant type |

Nilai yang diperbolehkan:

```text
client_credentials
```

---

## 2.6 Example Request

```http
POST /v1.0/access-token/b2b HTTP/1.1
X-CLIENT-KEY: EP9613058999
X-SIGNATURE: b634c01386f0234c984e142bb72a50894336fd1ffebb67f3db16f16c776d02cd661c4e5e50169b04d5f06d7c4c53f924289d378a1f790287ef7f975e4bb2b16c74f5afea4a590bcb7d45ea6ad6e1ba85a589bee411cb15da497bd880f7fa4a52caf435714f726ed3e325268ea01e682e1ae82592758368ccc24c9b3bffc4ec0a19f41147f955a891acc8dd29f47485da7e6454b87c477f08b669c4dd8b5b96e2dca612e7cc1c742efd2f39db719948a3355198ba1de206f942b809999fd6cebcba8d9844b9d65543e48b0c5a9dfa81f3d481773ff547f875d58c9ab9271de8241386d35ae8224ad14d6442a57220f09d57c5247d8bf1eda28d5d5afe4eb07fe0
X-TIMESTAMP: 2025-11-27T08:05:41+07:00
Content-Type: application/json

{
  "grantType": "client_credentials"
}
```

---

## 2.7 Success Response

HTTP Status:

```text
200 OK
```

Example:

```json
{
  "responseCode": "2007300",
  "responseMessage": "Successful",
  "tokenType": "Bearer",
  "accessToken": "YWFlNTQ4NTEyZTE5ZjZiY2MxN2E2Y2Y5NDVkY2UzNmU6MDA6Zjg1YTI5MjhlYjU2NDg4Zjk3YzIwODFkYTI3ZTY4ZWRnQ1Vid2hRanJkOGx5Y1owNHlMMmxTMEFTenN2bnZUaA==",
  "expiresIn": "900"
}
```

### Response Parameters

| Parameter         | Required | Type   | Description              |
| ------------------ | -------- | ------ | ------------------------ |
| `responseCode`    | C        | String | Response code            |
| `responseMessage` | C        | String | Response description     |
| `tokenType`       | M        | String | Token type               |
| `accessToken`     | M        | String | Bearer access token      |
| `expiresIn`       | M        | String | Token expiry dalam detik |

Contoh:

```text
tokenType = Bearer
expiresIn = 900
```

Artinya token berlaku selama:

```text
900 seconds = 15 minutes
```

---

## 2.8 Access Token Usage

Access Token digunakan pada API yang membutuhkan autentikasi.

Format:

```http
Authorization: Bearer <ACCESS_TOKEN>
```

Contoh:

```http
Authorization: Bearer YWFlNTQ4NTEyZTE5ZjZiY2MxN2E2Y2Y5...
```

### Security

Access Token merupakan credential sensitif.

Jangan:

* Menyimpan token di source code.
* Commit token ke Git.
* Menampilkan token di log.
* Mengirim token melalui HTTP tanpa TLS.
* Membagikan token kepada pihak lain.

---

## 2.9 Error Codes

Format response code:

```text
httpcode(3) + servicecode(2) + casecode(2)
```

| HTTP | Service | Case | Description             |
| ---: | ------: | ---: | ----------------------- |
|  200 |      73 |   00 | Success                 |
|  400 |      73 |   01 | Invalid Field Format    |
|  400 |      73 |   02 | Invalid Mandatory Field |
|  401 |      73 |   00 | Unauthorized            |
|  401 |      73 |   01 | Invalid Token           |
|  409 |      51 |   00 | Conflict                |

### Common 401 Causes

* `X-CLIENT-KEY` salah.
* Private key tidak sesuai.
* Signature tidak valid.
* Timestamp tidak sesuai.
* Timestamp terlalu lama.
* Credential tidak valid.

---

# 3. API Generate QRIS MPM

## 3.1 Overview

API Generate QRIS MPM digunakan Merchant untuk membuat **QRIS Merchant Presented Mode (MPM)** yang kemudian dapat ditampilkan kepada customer untuk melakukan pembayaran.

Dalam mode MPM:

```text
Merchant
   │
   │ Generate QR
   ▼
Manjo
   │
   │ QR Content
   ▼
Merchant Device
   │
   │ Display QR
   ▼
Customer
   │
   │ Scan & Pay
   ▼
QRIS Payment
```

### Sequence Diagram

![Flow Generate QRIS MPM](assets/flow-generate-qris-mpm.png)

Merchant men-generate `X-SIGNATURE` lalu mengirim `POST /v1.0/qr/qr-mpm-generate` dengan header `Authorization`, `X-TIMESTAMP`, `X-SIGNATURE`, dll, serta body berisi `amount`, `merchantId`, dll. Manjo memvalidasi signature dan token — jika tidak valid, Manjo membalas `40X Error`; jika valid, Manjo men-generate string QRIS MPM dan membalas `200 OK` berisi `qrContent` dan `referenceNo`. Merchant kemudian me-render `qrContent` menjadi gambar QR dan menampilkannya untuk di-scan oleh Customer.

---

## 3.2 General Information

| Field        | Value                      |
| ------------ | -------------------------- |
| Service Code | `47`                       |
| Name         | Generate QRIS MPM          |
| Direction    | Merchant → Manjo           |
| Version      | `1.0`                      |
| HTTP Method  | `POST`                     |
| Path         | `/v1.0/qr/qr-mpm-generate` |

---

## 3.3 Request Headers

| Field           | Required | Type       | Description              |
| --------------- | -------- | ---------- | ------------------------ |
| `Content-Type`  | M        | String     | Media type               |
| `Authorization` | M        | String     | Bearer Access Token      |
| `X-TIMESTAMP`   | M        | String     | Client timestamp         |
| `X-SIGNATURE`   | M        | String     | HMAC SHA-512 signature   |
| `ORIGIN`        | O        | String     | Merchant domain          |
| `X-IP-ADDRESS`  | M        | String     | Merchant IP address      |
| `X-PARTNER-ID`  | M        | String(36) | Merchant ID              |
| `X-EXTERNAL-ID` | M        | String(36) | Unique reference per day |
| `CHANNEL-ID`    | M        | String(5)  | Device identification    |

### Example

```http
Content-Type: application/json
Authorization: Bearer <ACCESS_TOKEN>
X-TIMESTAMP: 2025-11-27T08:10:57+07:00
X-SIGNATURE: <CALCULATED_SIGNATURE>
ORIGIN: www.yourdomain.com
X-IP-ADDRESS: 172.24.281.24
X-PARTNER-ID: MT60169117
X-EXTERNAL-ID: 5dfixGSe93fkfFVzV7qZCafHEbjPIpKAAiy
CHANNEL-ID: 05
```

---

## 3.4 X-SIGNATURE

Signature menggunakan:

```text
HMAC_SHA512
```

### Step 1 — Minify Request Body

Request body terlebih dahulu diminify.

### Step 2 — SHA-256

```text
bodyHash = SHA256(minify(RequestBody))
```

Kemudian encode ke hexadecimal lowercase:

```text
hexBodyHash = Lowercase(HexEncode(bodyHash))
```

### Step 3 — Build String to Sign

```text
stringToSign =
    HTTPMethod
    + ":"
    + EndpointUrl
    + ":"
    + AccessToken
    + ":"
    + hexBodyHash
    + ":"
    + X-TIMESTAMP
```

### Step 4 — Generate Signature

```text
signature = HMAC_SHA512(clientSecret, stringToSign)
```

---

## 3.5 Request Body

```json
{
  "partnerReferenceNo": "DIRECT-API-NMS-6giumc6rde",
  "amount": {
    "value": "30000.00",
    "currency": "IDR"
  },
  "merchantId": "MT60169117",
  "subMerchantId": "test",
  "storeId": "123",
  "terminalId": "456",
  "validityPeriod": "111",
  "additionalInfo": {
    "paymentId": "99",
    "dynamicAmount": "N",
    "prodDesc": "mobile phone purchase"
  }
}
```

---

## 3.6 Request Parameters

| Parameter                      | Required | Type         | Description                  |
| ------------------------------- | -------- | ------------ | ----------------------------- |
| `partnerReferenceNo`           | M        | String(64)   | Transaction ID dari Merchant |
| `amount.value`                 | M        | String(16,2) | Nominal transaksi            |
| `amount.currency`              | M        | String(3)    | ISO 4217 currency            |
| `merchantId`                   | M        | String(64)   | Merchant identifier          |
| `subMerchantId`                | O        | String(32)   | Sub merchant ID              |
| `storeId`                      | O        | String(64)   | Store code                   |
| `terminalId`                   | O        | String(16)   | Terminal ID                  |
| `validityPeriod`               | M        | String       | Masa berlaku QR              |
| `additionalInfo.paymentId`     | M        | String(2)    | Transaction code             |
| `additionalInfo.dynamicAmount` | M        | String(1)    | Dynamic amount flag          |
| `additionalInfo.prodDesc`      | M        | String(100)  | Product description          |

---

## 3.7 Dynamic Amount

Parameter:

```text
additionalInfo.dynamicAmount
```

memiliki dua kemungkinan:

### `N` — Fixed Amount

Nominal sudah ditentukan oleh Merchant.

Contoh:

```json
{
  "amount": {
    "value": "50000.00",
    "currency": "IDR"
  },
  "additionalInfo": {
    "dynamicAmount": "N"
  }
}
```

Flow:

```text
Merchant menentukan Rp50.000
        ↓
Generate QR
        ↓
Customer scan
        ↓
Customer membayar Rp50.000
```

### `Y` — Customer Input Amount

Customer dapat menentukan nominal ketika melakukan pembayaran.

```json
{
  "amount": {
    "value": "0.00",
    "currency": "IDR"
  },
  "additionalInfo": {
    "dynamicAmount": "Y"
  }
}
```

Dalam mode ini, nominal pada parameter `amount` diabaikan sesuai spesifikasi.

---

## 3.8 Success Response

HTTP Status:

```text
200 OK
```

Example:

```json
{
  "responseCode": "2004700",
  "responseMessage": "Successful",
  "referenceNo": "A0000001702",
  "partnerReferenceNo": "DIRECT-API-NMS-6giumc6rde",
  "qrContent": "00020101021226620015ID.CO.MANJO.WWW01189360085801751257030210MT601691170303UMI51530014ID.CO.QRIS.WWW0215ID102106515191704121.0.27.11.25520448165303360540850000.0055020357040.905802ID5910Prodaction6013JAKARTA PUSAT61059997362560525DIRECT-API-NMS-6giumc6rde0702450817Iphone 17 Pro Max6304FE20",
  "qrUrl": "-",
  "qrImage": "-",
  "redirectUrl": "-",
  "merchantName": "Prodaction",
  "storeId": "abcd",
  "terminalId": "45",
  "additionalInfo": {
    "paymentId": "99",
    "merchantCode": "MT60169117",
    "expiryDuration": "3600",
    "expireDate": "20251127144448",
    "amount": "50450.00"
  }
}
```

---

## 3.9 Response Parameters

| Parameter                       | Required | Type         | Description               |
| -------------------------------- | -------- | ------------ | -------------------------- |
| `responseCode`                  | M        | String(7)    | Response code             |
| `responseMessage`               | M        | String(150)  | Response description      |
| `referenceNo`                   | M        | String(64)   | Transaction ID dari Manjo |
| `partnerReferenceNo`            | M        | String(64)   | Reference dari Merchant   |
| `qrContent`                     | C        | String(512)  | QRIS MPM string           |
| `qrUrl`                         | O        | String(256)  | URL QR image              |
| `qrImage`                       | O        | String       | Base64 QR image           |
| `redirectUrl`                   | O        | String(512)  | Redirect URL              |
| `merchantName`                  | M        | String(25)   | Merchant name             |
| `storeId`                       | O        | String(64)   | Store code                |
| `terminalId`                    | O        | String(16)   | Terminal ID               |
| `additionalInfo.merchantCode`   | O        | String(64)   | Merchant ID               |
| `additionalInfo.paymentId`      | O        | String(2)    | Payment transaction code  |
| `additionalInfo.amount`         | M        | String(16,2) | Transaction amount        |
| `additionalInfo.expiryDuration` | M        | String(6)    | Expiry dalam detik        |
| `additionalInfo.expireDate`     | M        | String(20)   | Expiry date               |

Format `expireDate`:

```text
yyyyMMddHHmmss
```

---

## 3.10 QR Content

Field:

```text
qrContent
```

merupakan string QRIS MPM yang dapat digunakan untuk menghasilkan QR Code.

Contoh:

```text
00020101021226620015ID.CO.MANJO.WWW...
```

Merchant dapat menggunakan QR Code generator untuk mengubah `qrContent` menjadi gambar QR.

Flow:

```text
qrContent
    │
    ▼
QR Code Generator
    │
    ▼
QR Image
    │
    ▼
Display pada Merchant Device
```

---

## 3.11 Generate QR Example

```bash
curl -X POST \
  'https://api.manjo.com/v1.0/qr/qr-mpm-generate' \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer <ACCESS_TOKEN>' \
  -H 'X-TIMESTAMP: 2025-11-27T08:10:57+07:00' \
  -H 'X-SIGNATURE: <CALCULATED_SIGNATURE>' \
  -H 'X-PARTNER-ID: MT60169117' \
  -H 'X-EXTERNAL-ID: 5dfixGSe93fkfFVzV7qZCafHEbjPIpKAAiy' \
  -H 'X-IP-ADDRESS: 172.24.281.24' \
  -H 'CHANNEL-ID: 05' \
  -d '{
    "partnerReferenceNo": "TRX-001",
    "amount": {
      "value": "50000.00",
      "currency": "IDR"
    },
    "merchantId": "MT60169117",
    "validityPeriod": "3600",
    "additionalInfo": {
      "paymentId": "99",
      "dynamicAmount": "N",
      "prodDesc": "Laptop Purchase"
    }
  }'
```

---

## 3.12 Error Codes

| HTTP | Service | Case | Description                              |
| ---: | ------: | ---: | ------------------------------------------ |
|  200 |      47 |   00 | Success                                  |
|  400 |      47 |   01 | Invalid Field Format                     |
|  400 |      47 |   02 | Invalid Mandatory Field                  |
|  401 |      47 |   00 | Unauthorized                             |
|  401 |      47 |   01 | Invalid Token                            |
|  404 |      47 |   08 | Invalid Merchant                         |
|  409 |      47 |   00 | Conflict / X-EXTERNAL-ID sudah digunakan |

---

# 4. API Payment Notification

## 4.1 Overview

Payment Notification digunakan Manjo untuk mengirimkan informasi status pembayaran kepada Merchant.

Berbeda dengan API sebelumnya, direction API ini adalah:

```text
Manjo → Merchant
```

Merchant harus menyediakan endpoint yang dapat menerima HTTP POST dari Manjo.

---

## 4.2 General Information

| Field        | Value                         |
| ------------ | ------------------------------ |
| Service Code | `52`                          |
| Name         | QRIS MPM Payment Notification |
| Direction    | Manjo → Merchant              |
| Version      | `1.0`                         |
| HTTP Method  | `POST`                        |
| Path         | `/v1.0/qr/qr-mpm-notify`      |

> Endpoint tujuan notification disediakan oleh Merchant dan harus dapat diakses oleh Manjo sesuai konfigurasi integrasi.

---

## 4.3 Notification Flow

```text
Customer
   │
   │ Scan QRIS
   ▼
Payment Provider
   │
   │ Payment Processing
   ▼
Manjo
   │
   │ Payment Notification
   ▼
Merchant
   │
   │ Update Transaction
   ▼
Transaction Status
```

### Sequence Diagram

![Flow Payment Notification](assets/flow-payment-notification.png)

Diagram di atas menggambarkan dua tahap lengkap: **Tahap 1 — Request Access Token B2B** (Manjo men-generate timestamp & asymmetric signature, memanggil `POST /access-token/b2b`, Security Middleware memverifikasi signature memakai public key klien, lalu mengembalikan `accessToken`), dan **Tahap 2 — Kirim Notifikasi (Hit Webhook)** ke endpoint Merchant. Pada tahap kedua, Manjo memakai `accessToken` sebagai Bearer token, membuat `X-EXTERNAL-ID` unik dan symmetric signature (HMAC dari payload JSON + client secret), lalu memanggil `POST /qr/qr-mpm-notify`. Security Middleware di sisi Merchant melakukan validasi berlapis secara berurutan — (1) validasi Bearer token, (2) cek idempotency berdasarkan `X-EXTERNAL-ID` per hari, (3) verifikasi symmetric signature untuk memastikan integritas payload — sebelum meneruskan request ke handler notifikasi dan membalas `200 OK`.

---

## 4.4 Request Headers

| Field           | Required | Type       | Description              |
| --------------- | -------- | ---------- | ------------------------- |
| `Content-Type`  | M        | String     | Media type               |
| `Authorization` | C        | String     | Bearer Access Token      |
| `X-TIMESTAMP`   | M        | String     | Timestamp                |
| `X-SIGNATURE`   | M        | String     | HMAC SHA-512 signature   |
| `ORIGIN`        | O        | String     | Merchant domain          |
| `X-IP-ADDRESS`  | M        | String     | Sender IP                |
| `X-PARTNER-ID`  | M        | String(36) | Partner ID               |
| `X-EXTERNAL-ID` | M        | String(36) | Unique reference per day |
| `CHANNEL-ID`    | M        | String(5)  | Channel/device ID        |

---

## 4.5 X-SIGNATURE

Signature menggunakan:

```text
HMAC_SHA512
```

Formula:

```text
stringToSign =
    HTTPMethod
    + ":"
    + EndpointUrl
    + ":"
    + AccessToken
    + ":"
    + Lowercase(HexEncode(SHA256(minify(RequestBody))))
    + ":"
    + X-TIMESTAMP
```

Kemudian:

```text
signature = HMAC_SHA512(clientSecret, stringToSign)
```

---

## 4.6 Request Body

```json
{
  "originalReferenceNo": "A0000021383",
  "originalPartnerReferenceNo": "DIRECT-API-NMS-dt5ykh4sae",
  "latestTransactionStatus": "06",
  "transactionStatusDesc": "SUCCESS",
  "customerNumber": "9360082112345678919",
  "accountType": "UNSPECIFIED",
  "destinationNumber": "9360085801764127112",
  "destinationAccountName": "AYOBORONG",
  "amount": {
    "value": "10000.00",
    "currency": "IDR"
  },
  "sessionId": "E36933",
  "bankCode": "93600821",
  "externalStoreId": "427",
  "additionalInfo": {
    "acqName": "manjo",
    "issuerName": "Midazpay",
    "custName": "TES",
    "tips": "0.00",
    "merchantCode": "MT60169117",
    "trxTime": "2026-01-27T13:13:54+07:00",
    "amountTrx": "10000.00",
    "rrn": "1l62d1b01796"
  }
}
```

---

## 4.7 Request Parameters

| Parameter                    | Required | Type         | Description                  |
| ------------------------------ | -------- | ------------ | ------------------------------ |
| `originalReferenceNo`        | M        | String(64)   | Transaction ID pada Manjo    |
| `originalPartnerReferenceNo` | M        | String(64)   | Transaction ID pada Merchant |
| `latestTransactionStatus`    | M        | String(2)    | Status transaksi             |
| `transactionStatusDesc`      | O        | String(100)  | Deskripsi status             |
| `customerNumber`             | O        | String(64)   | Customer account number      |
| `accountType`                | O        | String(2)    | Tipe account customer        |
| `destinationAccountNumber`   | O        | String(25)   | Destination account number   |
| `destinationAccountName`     | O        | String(25)   | Destination account name     |
| `amount`                     | O        | Object       | Transaction amount           |
| `amount.value`               | M        | String(16,2) | Net transaction amount       |
| `amount.currency`            | M        | String(3)    | ISO 4217 currency            |
| `externalStoreId`            | O        | String(25)   | External store ID            |
| `additionalInfo`             | O        | Object       | Informasi tambahan           |
| `additionalInfo.amountTrx`   | M        | String(16,2) | Transaction amount           |
| `additionalInfo.tips`        | M        | String(16,2) | Tips amount                  |
| `additionalInfo.custName`    | M        | String(36)   | Customer name                |
| `additionalInfo.acqName`     | M        | String(36)   | Acquirer name                |
| `additionalInfo.issuerName`  | M        | String(25)   | Issuer name                  |
| `additionalInfo.rrn`         | M        | String(25)   | Retrieval Reference Number   |
| `additionalInfo.trxTime`     | M        | String       | Transaction date/time        |

---

## 4.8 Transaction Status

`latestTransactionStatus` menunjukkan status transaksi.

| Code | Status    | Description               |
| ---- | --------- | -------------------------- |
| `00` | Success   | Pembayaran berhasil       |
| `01` | Failed    | Pembayaran gagal          |
| `02` | Not Found | Transaksi tidak ditemukan |
| `03` | Paid      | Transaksi sudah dibayar   |
| `04` | Pending   | Pembayaran masih pending  |
| `05` | Refunded  | Transaksi telah direfund  |
| `06` | Cancelled | Transaksi dibatalkan      |

> Gunakan status dari field `latestTransactionStatus` sebagai acuan utama untuk melakukan update status transaksi pada sistem Merchant.

---

## 4.9 Payment Notification Example

```bash
curl -X POST \
  'https://merchant.example.com/v1.0/qr/qr-mpm-notify' \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer <ACCESS_TOKEN>' \
  -H 'X-TIMESTAMP: 2025-01-15T17:01:11+07:00' \
  -H 'X-SIGNATURE: <CALCULATED_SIGNATURE>' \
  -H 'X-PARTNER-ID: 821508239190...' \
  -H 'X-EXTERNAL-ID: 418075935899...' \
  -H 'CHANNEL-ID: 05' \
  -d '{
    "originalReferenceNo": "A0000021383",
    "originalPartnerReferenceNo": "DIRECT-API-NMS-dt5ykh4sae",
    "latestTransactionStatus": "03",
    "transactionStatusDesc": "SUCCESS",
    "customerNumber": "9360082112345678919",
    "accountType": "UNSPECIFIED",
    "destinationNumber": "9360085801764127112",
    "destinationAccountName": "AYOBORONG",
    "amount": {
      "value": "10000.00",
      "currency": "IDR"
    },
    "sessionId": "E36933",
    "bankCode": "93600821",
    "externalStoreId": "427",
    "additionalInfo": {
      "acqName": "manjo",
      "issuerName": "Midazpay",
      "custName": "TES",
      "tips": "0.00",
      "merchantCode": "MT60169117",
      "trxTime": "2026-01-27T13:13:54+07:00",
      "amountTrx": "10000.00",
      "rrn": "1l62d1b01796"
    }
  }'
```

---

## 4.10 Error Codes

| HTTP | Service | Case | Description                              |
| ---: | ------: | ---: | ------------------------------------------ |
|  200 |      52 |   00 | Success                                  |
|  400 |      52 |   01 | Invalid Field Format                     |
|  400 |      52 |   02 | Invalid Mandatory Field                  |
|  401 |      52 |   00 | Unauthorized                             |
|  401 |      52 |   01 | Invalid Token                            |
|  409 |      52 |   00 | Conflict / X-EXTERNAL-ID sudah digunakan |

---

# 5. API Query Payment

## 5.1 Overview

API Query Payment digunakan Merchant untuk mengecek status transaksi QRIS MPM secara langsung ke Manjo (model **pull**), sebagai pelengkap Payment Notification (model **push** di Section 4).

Endpoint ini berguna untuk dua skenario utama:

* Payment Notification tidak kunjung diterima (webhook gagal/timeout/hilang), sehingga Merchant perlu mengecek status secara aktif.
* Rekonsiliasi/audit manual terhadap transaksi tertentu.

> **Penting:** endpoint ini menggunakan field `latestTransactionStatus` dengan **kode status yang berbeda** dari Payment Notification (Section 4.8). Jangan gunakan mapping status yang sama untuk kedua endpoint — lihat [Section 5.10](#510-transaction-status-codes).

---

## 5.2 General Information

| Field        | Value                   |
| ------------ | ------------------------ |
| Service Code | `51`                     |
| Name         | Query Payment            |
| Direction    | Merchant → Manjo        |
| Version      | `1.0`                    |
| HTTP Method  | `POST`                   |
| Path         | `/v1.0/qr/qr-mpm-query` |

---

## 5.3 Query Flow

```text
Merchant
   │
   │ Siapkan originalReferenceNo
   ▼
Merchant
   │
   │ Generate X-SIGNATURE
   ▼
POST /v1.0/qr/qr-mpm-query
   │
   │ Headers: Authorization, X-CLIENT-KEY, X-SIGNATURE, dll.
   │ Body: originalReferenceNo, serviceCode, dll.
   ▼
Manjo
   │
   │ Cari data transaksi di sistem
   │
   ├── Transaksi Tidak Ditemukan ──▶ 404 Transaction Not Found
   │
   └── Transaksi Ditemukan ──▶ 200 OK (latestTransactionStatus, amount, paidTime)
```

Merchant menyiapkan `originalReferenceNo` (dan `originalPartnerReferenceNo`) dari transaksi yang ingin dicek, men-generate `X-SIGNATURE`, lalu mengirim `POST /v1.0/qr/qr-mpm-query`. Manjo mencari data transaksi berdasarkan reference yang dikirim — jika tidak ditemukan, Manjo membalas `404 Transaction Not Found`; jika ditemukan, Manjo membalas `200 OK` berisi `latestTransactionStatus`, `amount`, dan `paidTime` (jika sudah dibayar).

---

## 5.4 Request Headers

| Field           | Required | Type       | Description                              |
| --------------- | -------- | ---------- | ------------------------------------------ |
| `Content-Type`  | M        | String     | Media type request                       |
| `Authorization` | M        | String     | Bearer Access Token                      |
| `X-TIMESTAMP`   | M        | String     | Client timestamp                         |
| `X-CLIENT-KEY`  | M        | String     | Client ID (`merchantcode` + `paymentid`) |
| `X-SIGNATURE`   | M        | String     | HMAC SHA-512 signature                   |
| `ORIGIN`        | O        | String     | Merchant domain                          |
| `X-IP-ADDRESS`  | O        | String     | Merchant IP address                      |
| `X-PARTNER-ID`  | M        | String(36) | Merchant ID                              |
| `X-EXTERNAL-ID` | M        | String(36) | Unique reference per day                 |
| `CHANNEL-ID`    | M        | String(5)  | Device identification                    |

> **Catatan:** berbeda dari Generate QRIS MPM (Section 3.3) yang hanya butuh `Authorization`, endpoint ini **juga** mewajibkan header `X-CLIENT-KEY` (seperti Access Token B2B) di samping `Authorization` Bearer. Pastikan Manjo Client mengirim kedua-duanya untuk operasi Query.

### Example

```http
Content-Type: application/json
Authorization: Bearer YWFlNTQ4NTEyZTE5ZjZi...
X-TIMESTAMP: 2025-11-27T07:14:27.609+07:00
X-CLIENT-KEY: EP9613058999
X-SIGNATURE: 26200f8e8fcd11a7e881044a86213f8d...
X-PARTNER-ID: EP9613058999
X-EXTERNAL-ID: XTIDQ-0u87ji0pjk
X-IP-ADDRESS: 172.24.281.24
CHANNEL-ID: 05
```

---

## 5.5 X-SIGNATURE

Signature menggunakan:

```text
HMAC_SHA512
```

Formula (identik dengan Generate QRIS MPM dan Payment Notification):

```text
stringToSign =
    HTTPMethod
    + ":"
    + EndpointUrl
    + ":"
    + AccessToken
    + ":"
    + Lowercase(HexEncode(SHA256(minify(RequestBody))))
    + ":"
    + X-TIMESTAMP
```

Kemudian:

```text
signature = HMAC_SHA512(clientSecret, stringToSign)
```

---

## 5.6 Request Body

```json
{
  "originalReferenceNo": "A0000001703",
  "originalPartnerReferenceNo": "DIRECT-API-NMS-12ajd1mir8",
  "originalExternalId": "30443786930722726463280097920912",
  "serviceCode": "99",
  "merchantId": "EP96130589",
  "additionalInfo": {
    "currency": "IDR"
  }
}
```

---

## 5.7 Request Parameters

| Parameter                     | Required | Type       | Description                                          |
| ------------------------------- | -------- | ---------- | ------------------------------------------------------ |
| `originalReferenceNo`         | M        | String(64) | `referenceNo` dari response Generate QRIS MPM         |
| `originalPartnerReferenceNo`  | M        | String(64) | `partnerReferenceNo` yang dipakai saat generate QR     |
| `originalExternalId`          | O        | String(32) | `X-EXTERNAL-ID` yang dipakai saat generate QR          |
| `serviceCode`                 | M        | String(2)  | Transaction type indicator — gunakan `"99"` untuk transaksi QRIS Manjo |
| `merchantId`                  | M        | String(64) | Merchant identifier                                   |
| `subMerchantId`               | O        | String(32) | Sub merchant ID                                       |
| `externalStoreId`             | O        | String(64) | External store ID                                     |
| `additionalInfo.currency`     | M        | String(20) | Currency, selalu `"IDR"`                              |

---

## 5.8 Success Response

HTTP Status:

```text
200 OK
```

Example:

```json
{
  "responseCode": "2005100",
  "responseMessage": "Successful",
  "originalReferenceNo": "A0000001703",
  "originalPartnerReferenceNo": "DIRECT-API-NMS-12ajd1mir8",
  "originalExternalId": "30443786930722726463280097920912",
  "serviceCode": "99",
  "latestTransactionStatus": "00",
  "transactionStatusDesc": "Success",
  "paidTime": "2025-11-27T14:13:54+07:00",
  "amount": {
    "value": "50000.00",
    "currency": "IDR"
  },
  "feeAmount": {
    "value": "450.00",
    "currency": "IDR"
  },
  "terminalId": "45",
  "additionalInfo": {
    "currency": "IDR"
  }
}
```

Example — transaksi tidak ditemukan:

```json
{
  "responseCode": "4045101",
  "responseMessage": "Transaction Not Found",
  "originalReferenceNo": "A0000001703",
  "originalPartnerReferenceNo": "DIRECT-API-NMS-12ajd1mir8",
  "serviceCode": "99",
  "latestTransactionStatus": "07",
  "transactionStatusDesc": "Not found"
}
```

---

## 5.9 Response Parameters

| Parameter                      | Required | Type          | Description                                    |
| --------------------------------- | -------- | ------------- | ------------------------------------------------ |
| `responseCode`                  | M        | String(7)     | Response code                                  |
| `responseMessage`               | M        | String(150)   | Response description                           |
| `originalReferenceNo`           | C        | String(64)    | Echo dari request                              |
| `originalPartnerReferenceNo`    | M        | String(64)    | Echo dari request                              |
| `originalExternalId`            | O        | String(32)    | Echo dari request                              |
| `serviceCode`                   | M        | String(2)     | Transaction type indicator                     |
| `latestTransactionStatus`       | M        | String(2)     | Kode status terbaru — **skema Query**, lihat Section 5.10 |
| `transactionStatusDesc`         | O        | String(50)    | Deskripsi status                               |
| `paidTime`                      | C        | String(25)    | Timestamp pembayaran, **hanya terisi jika status `00` Success** |
| `amount.value`                  | M        | String(16,2)  | Nominal pembayaran                             |
| `amount.currency`               | M        | String(3)     | ISO 4217 currency                              |
| `feeAmount.value`               | M        | String(16,2)  | Biaya transaksi yang dikenakan payment provider |
| `feeAmount.currency`            | M        | String(3)     | ISO 4217 currency                              |
| `terminalId`                    | O        | String(16)    | Terminal ID                                    |
| `additionalInfo.currency`       | M        | String(20)    | Currency                                       |

---

## 5.10 Transaction Status Codes

`latestTransactionStatus` pada response Query Payment **TIDAK memakai skema kode yang sama** dengan Payment Notification (Section 4.8), meskipun nama field-nya identik.

| Code | Status (Query Payment) |
| ---- | ----------------------- |
| `00` | Success                 |
| `01` | Initiated                |
| `02` | Paying                   |
| `03` | Pending                  |
| `04` | Refunded                 |
| `05` | Canceled                 |
| `06` | Failed                   |
| `07` | Not found                |

Perbandingan langsung dengan skema Payment Notification (Section 4.8):

| Code | Payment Notification (Service `52`) | Query Payment (Service `51`) |
| ---- | ------------------------------------- | ------------------------------ |
| `00` | Success                               | Success                        |
| `01` | Failed                                 | Initiated                      |
| `02` | Not Found                              | Paying                         |
| `03` | Paid                                   | Pending                        |
| `04` | Pending                                | Refunded                       |
| `05` | Refunded                               | Canceled                       |
| `06` | Cancelled                              | Failed                         |
| `07` | *(tidak ada)*                          | Not found                      |

> **⚠️ Kode identik, arti berbeda.** Kode `03` misalnya berarti **Paid** di Payment Notification tapi **Pending** di Query Payment — kalau kedua response ini diproses lewat satu fungsi mapping status yang sama, transaksi pending bisa salah dibaca sebagai lunas (atau sebaliknya). **Wajib** pakai dua fungsi/tabel mapping terpisah untuk kedua endpoint ini. Lihat dampaknya ke source-of-truth mapping di `brainstorm-q161-updated.md` Section 22 dan `architecture.md` Section 10.

---

## 5.11 Query Payment Example

```bash
curl -X POST \
  'https://api.manjo.com/v1.0/qr/qr-mpm-query' \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer YWFlNTQ4NTEyZTE5ZjZi...' \
  -H 'X-TIMESTAMP: 2025-11-27T07:14:27.609+07:00' \
  -H 'X-CLIENT-KEY: EP9613058999' \
  -H 'X-SIGNATURE: 26200f8e8fcd11a7e881044a86213f8d...' \
  -H 'X-PARTNER-ID: EP9613058999' \
  -H 'X-EXTERNAL-ID: XTIDQ-0u87ji0pjk' \
  -H 'X-IP-ADDRESS: 172.24.281.24' \
  -H 'CHANNEL-ID: 05' \
  -d '{
    "originalReferenceNo": "A0000001703",
    "originalPartnerReferenceNo": "DIRECT-API-NMS-12ajd1mir8",
    "originalExternalId": "30443786930722726463280097920912",
    "serviceCode": "99",
    "merchantId": "EP96130589",
    "additionalInfo": {
      "currency": "IDR"
    }
  }'
```

---

## 5.12 Error Codes

| HTTP | Service | Case | Description                              |
| ---: | ------: | ---: | ------------------------------------------ |
|  200 |      51 |   00 | Success                                  |
|  400 |      51 |   02 | Invalid Mandatory Field `{field name}`   |
|  401 |      51 |   00 | Unauthorized — invalid signature/client key |
|  401 |      51 |   01 | Invalid token                            |
|  404 |      51 |   01 | Transaction Not Found                    |
|  409 |      51 |   00 | Conflict / `X-EXTERNAL-ID` sudah digunakan |

---

## 5.13 Important Notes

1. `X-EXTERNAL-ID` harus unique per merchant per hari (sama seperti endpoint lain).
2. `X-CLIENT-KEY` wajib ada dan formatnya `merchantcode + paymentid`.
3. `originalReferenceNo` = `referenceNo` dari response Generate QRIS MPM (Section 3.9).
4. `originalPartnerReferenceNo` = `partnerReferenceNo` yang dipakai saat generate QR (Section 3.6).
5. `paidTime` hanya terisi untuk transaksi dengan status `00` (Success).
6. `feeAmount` merepresentasikan biaya transaksi yang dikenakan payment provider.
7. `serviceCode` untuk transaksi QRIS Manjo selalu `"99"` pada request (berbeda dari Service Code endpoint `51` itu sendiri — jangan tertukar).

---

# 6. End-to-End QRIS MPM Flow

Berikut flow integrasi secara keseluruhan:

```text
                    MERCHANT
                       │
                       │
              ┌────────▼────────┐
              │ Get Access Token │
              │ Service Code 73  │
              └────────┬────────┘
                       │
                       │ Access Token
                       ▼
              ┌──────────────────┐
              │ Generate QRIS MPM│
              │ Service Code 47  │
              └────────┬────────┘
                       │
                       │ qrContent
                       ▼
              ┌──────────────────┐
              │ Display QRIS     │
              │ pada Device      │
              └────────┬────────┘
                       │
                       │ Customer Scan
                       ▼
                 ┌───────────┐
                 │  Customer │
                 │   Pay     │
                 └─────┬─────┘
                       │
                       │ Payment
                       ▼
                    MANJO
                       │
                       │ Notification
                       │ Service Code 52
                       ▼
              ┌──────────────────┐
              │ Merchant Backend │
              └────────┬────────┘
                       │
                       ▼
              Update Transaction
                       │
                       ▼
               Payment Success
```

---

# 7. Recommended Transaction Mapping

Merchant sebaiknya menyimpan minimal dua reference:

| Field                | Source   | Fungsi                         |
| --------------------- | -------- | ------------------------------- |
| `partnerReferenceNo` | Merchant | ID transaksi internal Merchant |
| `referenceNo`        | Manjo    | ID transaksi pada Manjo        |

Contoh:

```text
Merchant:
partnerReferenceNo = TRX-001

Manjo:
referenceNo = A0000001703
```

Ketika Payment Notification diterima:

```text
originalPartnerReferenceNo
        │
        ▼
Cari transaksi Merchant
        │
        ▼
Update status transaksi
```

---

# 8. X-EXTERNAL-ID

`X-EXTERNAL-ID` merupakan unique reference yang digunakan pada request.

Format:

```text
X-EXTERNAL-ID: <UNIQUE_REFERENCE>
```

### Ketentuan

* Harus unique untuk periode yang ditentukan oleh API.
* Dokumentasi menyebutkan tidak boleh menggunakan `X-EXTERNAL-ID` yang sama pada hari yang sama.
* Gunakan generator ID yang aman untuk menghindari collision.

Contoh:

```text
5dfixGSe93fkfFVzV7qZCafHEbjPIpKAAiy
```

Jika ID yang sama digunakan kembali, API dapat mengembalikan:

```text
HTTP 409
```

---

# 9. Timestamp

Header timestamp menggunakan format ISO 8601:

```text
yyyy-MM-ddTHH:mm:ssTZD
```

Contoh WIB:

```text
2026-09-24T20:30:00+07:00
```

Sedangkan `trxTime` pada beberapa response/notification dapat menggunakan format:

```text
yyyy-MM-ddTHH:mm:ss+07:00
```

atau format:

```text
yyyyMMddHHmmss
```

Ikuti format sesuai field yang didefinisikan oleh endpoint.

---

# 10. Currency

Untuk transaksi QRIS menggunakan:

```text
IDR
```

Nominal menggunakan dua digit desimal.

Contoh:

```json
{
  "value": "50000.00",
  "currency": "IDR"
}
```

Bukan:

```json
{
  "value": "50000",
  "currency": "IDR"
}
```

---

# 11. Security Guidelines

## 11.1 HTTPS

Seluruh komunikasi API wajib menggunakan HTTPS.

```text
HTTPS
  ↓
TLS Encryption
  ↓
Manjo API
```

---

## 11.2 Private Key

Private key untuk Access Token:

```text
SHA256withRSA
```

harus disimpan secara aman.

Jangan menyimpan private key:

* Di Git repository.
* Di frontend.
* Di public environment variable.
* Di source code yang dapat diakses client.

---

## 11.3 Client Secret

`clientSecret` digunakan untuk HMAC SHA-512 pada API tertentu.

Simpan pada backend/server.

Jangan expose ke:

* Browser.
* Mobile application.
* QRIS frontend.
* Public repository.

---

## 11.4 Access Token

Access Token bersifat temporary dan memiliki expiry:

```text
900 seconds
```

Simpan token secara aman dan jangan log nilai token secara penuh.

---

# 12. Token Management

Merchant dapat menggunakan mekanisme berikut:

```text
Request API
    │
    ▼
Check Access Token
    │
    ├── Valid ──────────────► Continue Request
    │
    └── Expired / Missing
              │
              ▼
       Request New Token
              │
              ▼
        Save in Memory
              │
              ▼
        Continue Request
```

Disarankan melakukan refresh sebelum token benar-benar expired untuk mengurangi kemungkinan request gagal karena expired token.

---

# 13. API Summary

| Service              | Code | Direction        | Method | Endpoint                   |
| --------------------- | ---: | ----------------- | ------ | ---------------------------- |
| Access Token B2B     | `73` | Merchant → Manjo | POST   | `/v1.0/access-token/b2b`   |
| Generate QRIS MPM    | `47` | Merchant → Manjo | POST   | `/v1.0/qr/qr-mpm-generate` |
| Query Payment         | `51` | Merchant → Manjo | POST   | `/v1.0/qr/qr-mpm-query`    |
| Payment Notification | `52` | Manjo → Merchant | POST   | `/v1.0/qr/qr-mpm-notify`   |

---

# 14. Complete Integration Sequence

```text
┌─────────────────────────────────────────────────────────┐
│                    1. AUTHENTICATION                    │
└─────────────────────────────────────────────────────────┘

Merchant
   │
   │ POST /v1.0/access-token/b2b
   │ grantType=client_credentials
   ▼
Manjo
   │
   │ accessToken
   ▼
Merchant


┌─────────────────────────────────────────────────────────┐
│                    2. GENERATE QR                       │
└─────────────────────────────────────────────────────────┘

Merchant
   │
   │ POST /v1.0/qr/qr-mpm-generate
   │ Authorization: Bearer <token>
   ▼
Manjo
   │
   │ qrContent
   ▼
Merchant
   │
   │ Render QR
   ▼
Customer


┌─────────────────────────────────────────────────────────┐
│                    3. PAYMENT                           │
└─────────────────────────────────────────────────────────┘

Customer
   │
   │ Scan QRIS
   ▼
Payment Provider
   │
   │ Payment Processing
   ▼
Manjo


┌─────────────────────────────────────────────────────────┐
│                    4. NOTIFICATION                      │
└─────────────────────────────────────────────────────────┘

Manjo
   │
   │ POST /v1.0/qr/qr-mpm-notify
   │ latestTransactionStatus
   ▼
Merchant
   │
   ▼
Update Transaction
   │
   ├── Success
   ├── Failed
   ├── Pending
   ├── Paid
   ├── Refunded
   └── Cancelled


┌─────────────────────────────────────────────────────────┐
│              5. QUERY (OPTIONAL — RECONCILIATION)       │
└─────────────────────────────────────────────────────────┘

Merchant
   │
   │ POST /v1.0/qr/qr-mpm-query
   │ (dipakai kalau notifikasi tidak kunjung diterima,
   │  atau untuk audit/rekonsiliasi manual)
   ▼
Manjo
   │
   │ latestTransactionStatus (skema Query, BEDA dari skema Notification)
   ▼
Merchant
```

---

# 15. Implementation Checklist

### Authentication

* [ ] Merchant sudah mendapatkan `client_id`.
* [ ] Merchant sudah mendapatkan RSA private key.
* [ ] Merchant sudah mendapatkan `clientSecret`.
* [ ] Implementasi SHA256withRSA sudah tersedia.
* [ ] Implementasi HMAC SHA-512 sudah tersedia.
* [ ] Timestamp menggunakan timezone yang benar.
* [ ] Access Token disimpan secara aman.
* [ ] Access Token tidak di-log.

### Generate QRIS

* [ ] Access Token valid.
* [ ] `X-PARTNER-ID` valid.
* [ ] `X-EXTERNAL-ID` unique.
* [ ] Signature dihitung berdasarkan body yang sudah diminify.
* [ ] `partnerReferenceNo` unique.
* [ ] Amount menggunakan dua digit desimal.
* [ ] Currency menggunakan `IDR`.
* [ ] `validityPeriod` sudah ditentukan.
* [ ] `dynamicAmount` sudah sesuai kebutuhan.
* [ ] `qrContent` berhasil diterima.
* [ ] QR Code berhasil ditampilkan.

### Payment Notification

* [ ] Endpoint notification tersedia.
* [ ] Endpoint dapat diakses oleh Manjo.
* [ ] Signature dapat diverifikasi.
* [ ] `originalReferenceNo` dapat dicatat.
* [ ] `originalPartnerReferenceNo` dapat dicocokkan dengan transaksi internal.
* [ ] `latestTransactionStatus` diproses.
* [ ] Duplicate notification ditangani secara idempotent.
* [ ] Transaction status tidak boleh berubah secara sembarangan setelah status final.

### Query Payment

* [ ] `X-CLIENT-KEY` dikirim bersama `Authorization` Bearer (dua-duanya wajib).
* [ ] `originalReferenceNo`/`originalPartnerReferenceNo` diambil dari record transaksi internal, bukan di-generate ulang.
* [ ] `serviceCode` pada request diisi `"99"`.
* [ ] Mapping `latestTransactionStatus` untuk Query **terpisah** dari mapping Payment Notification — tidak boleh reuse fungsi yang sama.
* [ ] `paidTime` hanya dibaca ketika status `00` Success.

---

# 16. Important Notes

1. **Access Token** memiliki masa berlaku `900` detik atau `15 menit`.
2. Access Token harus dikirim menggunakan format `Bearer`.
3. Signature harus dibuat ulang sesuai timestamp dan request.
4. `X-EXTERNAL-ID` harus unique sesuai ketentuan API.
5. `partnerReferenceNo` digunakan sebagai reference transaksi dari sisi Merchant.
6. `referenceNo` digunakan sebagai reference transaksi dari sisi Manjo.
7. `qrContent` merupakan data QRIS MPM yang dapat di-render menjadi QR Code.
8. `dynamicAmount = N` berarti nominal transaksi ditentukan Merchant.
9. `dynamicAmount = Y` berarti nominal dapat diinput customer sesuai ketentuan API.
10. `validityPeriod` menentukan masa berlaku QR.
11. Payment Notification dikirim dari Manjo ke endpoint Merchant.
12. Merchant harus memproses `latestTransactionStatus`.
13. Semua credential, private key, client secret, dan access token harus disimpan secara aman.
14. Gunakan HTTPS untuk seluruh komunikasi API.
15. Untuk transaksi production, implementasikan **idempotency** agar duplicate notification tidak menyebabkan transaksi diproses lebih dari satu kali.
16. **Query Payment** (Service Code `51`) memakai skema `latestTransactionStatus` yang **berbeda** dari Payment Notification (Service Code `52`) — lihat Section 5.10. Jangan pakai satu tabel mapping status untuk kedua endpoint.

---

## 17. Reference

### OAuth 2.0

* OAuth 2.0 Authorization Framework — RFC 6749
* OAuth 2.0 Bearer Token Usage — RFC 6750

### Cryptography

* SHA256withRSA
* HMAC-SHA512
* SHA-256

### QRIS

* Merchant Presented Mode (MPM)
* Dynamic Amount
* Static/Customer Input Amount

---

**Document Version:** 1.0
**Service Owner:** Manjo QRIS Team
**Integration:** QRIS MPM
