# MQTT Contract Reconciliation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Payment Bridge Service's MQTT topic scheme and payload format match what the real Q161 Pro firmware actually sends/expects, so a physical device's GENERATE_QR request is received and answered instead of timing out.

**Architecture:** Replace the assumed hierarchical-JSON contract (`topic/{merchant}/{tenant}/{device}` + `{"type":"GENERATE_QR","amount":N}`) with the firmware's real flat-topic/plain-text contract (`qris/request` shared inbound topic + `"{device_id}|{amount_sen}"` payload, `topic_{device_id}` outbound, `"QR:{content}"` / plain Indonesian text replies). No firmware changes — the backend adapts to the hardware.

**Tech Stack:** Go, `github.com/eclipse/paho.mqtt.golang`, PostgreSQL (`golang-migrate`), existing `internal/transaction`/`internal/resolver` packages (unchanged).

## Global Constraints

- Amount unit conversion: firmware sends **sen** (Rupiah × 100); backend business logic (`transaction.Service.GenerateQR`) takes **Rupiah** — divide by 100, truncating any remainder (firmware always sends clean multiples of 100; a remainder is not an error per the design spec).
- Outbound topic is always `"topic_" + deviceID`, computed directly — never read from `devices.mqtt_topic` for routing (inbound topic is now shared, so there's nothing to parse a device out of).
- Outbound payload is plain text only: `"QR:{qrContent}"` on success, a human-readable Indonesian sentence on any failure (firmware has no structured error schema — falls back to TTS via `AppPlayTip`).
- Source design doc: `docs/superpowers/specs/2026-09-25-mqtt-contract-reconciliation-design.md` (status: disetujui). Every task below implements a specific section of it — cited inline.

---

### Task 1: Rewrite `internal/qrtopic` — flat topic scheme

**Files:**
- Modify: `internal/qrtopic/topic.go` (currently: hierarchical `Parse`/`Build`/`Topic`/`TenantSlotNone`)
- Modify: `internal/qrtopic/topic_test.go`

**Interfaces:**
- Produces: `qrtopic.RequestTopic` (string constant, `"qris/request"`), `qrtopic.BuildDeviceTopic(deviceID string) string` (returns `"topic_" + deviceID`).
- Consumed by: Task 3 (`cmd/server/main.go`), Task 4 (`internal/qrflow_test.go`).

- [ ] **Step 1: Write the failing tests**

Replace the entire contents of `internal/qrtopic/topic_test.go` with:

```go
package qrtopic

import "testing"

func TestRequestTopic(t *testing.T) {
	if RequestTopic != "qris/request" {
		t.Errorf("RequestTopic = %q, want %q", RequestTopic, "qris/request")
	}
}

func TestBuildDeviceTopic(t *testing.T) {
	got := BuildDeviceTopic("MT58530503")
	want := "topic_MT58530503"
	if got != want {
		t.Errorf("BuildDeviceTopic(%q) = %q, want %q", "MT58530503", got, want)
	}
}

func TestBuildDeviceTopic_EmptyDeviceID(t *testing.T) {
	got := BuildDeviceTopic("")
	want := "topic_"
	if got != want {
		t.Errorf("BuildDeviceTopic(%q) = %q, want %q", "", got, want)
	}
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run: `go test ./internal/qrtopic/... -v`
Expected: FAIL — `undefined: RequestTopic` / `undefined: BuildDeviceTopic` (old `Parse`/`Build`/`Topic`/`TenantSlotNone` symbols still exist at this point, that's fine, they'll be removed in Step 3).

- [ ] **Step 3: Replace the implementation**

Replace the entire contents of `internal/qrtopic/topic.go` with:

```go
package qrtopic

// RequestTopic is the single, shared inbound topic every Q161 Pro device
// publishes GENERATE_QR requests to (firmware: mqtt.c:236, def.h:36).
const RequestTopic = "qris/request"

// BuildDeviceTopic returns the topic a device listens on for its own
// reply, matching the firmware's subscription "topic_{MQTT_MERCHANT_ID}"
// set up in applyMqttParam() (param.c, mqtt.c:192).
func BuildDeviceTopic(deviceID string) string {
	return "topic_" + deviceID
}
```

This deletes `Parse`, `Build`, `Topic`, and `TenantSlotNone` — confirmed unused outside `internal/qrtopic` and `cmd/server/main.go`/`internal/qrflow_test.go` (both rewritten in Tasks 3–4).

- [ ] **Step 4: Run tests to verify they pass**

Run: `go test ./internal/qrtopic/... -v`
Expected: PASS (all 3 tests)

- [ ] **Step 5: Commit**

```bash
git add internal/qrtopic/topic.go internal/qrtopic/topic_test.go
git commit -m "$(cat <<'EOF'
refactor(qrtopic): switch to flat firmware topic scheme

Replace hierarchical topic/{merchant}/{tenant}/{device} parsing with the
firmware's real flat scheme: shared inbound "qris/request", outbound
"topic_{device_id}".

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Rewrite `internal/validation` — pipe-delimited payload parser

**Files:**
- Modify: `internal/validation/validator.go` (currently: JSON `GenerateQRMessage`/`ParseAndValidateGenerateQR`)
- Modify: `internal/validation/validator_test.go`

**Interfaces:**
- Produces: `validation.GenerateQRMessage{DeviceID string, Amount int64}` (Amount already converted to Rupiah), `validation.ParseGenerateQRMessage(payload []byte) (GenerateQRMessage, error)`.
- Consumed by: Task 3 (`cmd/server/main.go`), Task 4 (`internal/qrflow_test.go`).

- [ ] **Step 1: Write the failing tests**

Replace the entire contents of `internal/validation/validator_test.go` with:

```go
package validation

import "testing"

func TestParseGenerateQRMessage_Valid(t *testing.T) {
	msg, err := ParseGenerateQRMessage([]byte("MT58530503|5000000"))
	if err != nil {
		t.Fatalf("ParseGenerateQRMessage() error = %v", err)
	}
	if msg.DeviceID != "MT58530503" {
		t.Errorf("DeviceID = %q, want %q", msg.DeviceID, "MT58530503")
	}
	if msg.Amount != 50000 {
		t.Errorf("Amount = %d, want 50000", msg.Amount)
	}
}

func TestParseGenerateQRMessage_TruncatesUncleanSen(t *testing.T) {
	// 50050 sen / 100 = 500.5 -> truncated to 500, not an error.
	msg, err := ParseGenerateQRMessage([]byte("MT58530503|50050"))
	if err != nil {
		t.Fatalf("ParseGenerateQRMessage() error = %v", err)
	}
	if msg.Amount != 500 {
		t.Errorf("Amount = %d, want 500", msg.Amount)
	}
}

func TestParseGenerateQRMessage_NoSeparator(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("MT58530503"))
	if err == nil {
		t.Fatal("expected error for missing '|' separator, got nil")
	}
}

func TestParseGenerateQRMessage_TooManySeparators(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("MT58530503|5000000|extra"))
	if err == nil {
		t.Fatal("expected error for extra '|' separator, got nil")
	}
}

func TestParseGenerateQRMessage_EmptyDeviceID(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("|5000000"))
	if err == nil {
		t.Fatal("expected error for empty device_id, got nil")
	}
}

func TestParseGenerateQRMessage_NonNumericAmount(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("MT58530503|abc"))
	if err == nil {
		t.Fatal("expected error for non-numeric amount, got nil")
	}
}

func TestParseGenerateQRMessage_ZeroAmount(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("MT58530503|0"))
	if err == nil {
		t.Fatal("expected error for amount=0, got nil")
	}
}

func TestParseGenerateQRMessage_NegativeAmount(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("MT58530503|-100"))
	if err == nil {
		t.Fatal("expected error for negative amount, got nil")
	}
}
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run: `go test ./internal/validation/... -v`
Expected: FAIL — `undefined: ParseGenerateQRMessage`

- [ ] **Step 3: Replace the implementation**

Replace the entire contents of `internal/validation/validator.go` with:

```go
package validation

import (
	"fmt"
	"strconv"
	"strings"
)

// GenerateQRMessage is a parsed device GENERATE_QR request. Amount is
// already converted from sen (firmware wire unit) to Rupiah.
type GenerateQRMessage struct {
	DeviceID string
	Amount   int64 // Rupiah
}

// ParseGenerateQRMessage parses the pipe-delimited plain-text payload the
// Q161 Pro firmware publishes to qrtopic.RequestTopic: "{device_id}|{amount_sen}"
// (mqtt.c:228). amount_sen is divided by 100 to get Rupiah; a non-zero
// remainder is truncated rather than rejected, since the firmware's own
// input flow (GetAmount) always sends multiples of 100 in practice — see
// docs/superpowers/specs/2026-09-25-mqtt-contract-reconciliation-design.md Section 6.
func ParseGenerateQRMessage(payload []byte) (GenerateQRMessage, error) {
	parts := strings.Split(string(payload), "|")
	if len(parts) != 2 {
		return GenerateQRMessage{}, fmt.Errorf("validation: expected exactly one '|' separator, got payload %q", payload)
	}

	deviceID := parts[0]
	if deviceID == "" {
		return GenerateQRMessage{}, fmt.Errorf("validation: device_id is empty in payload %q", payload)
	}

	amountSen, err := strconv.ParseInt(parts[1], 10, 64)
	if err != nil {
		return GenerateQRMessage{}, fmt.Errorf("validation: invalid amount %q: %w", parts[1], err)
	}
	if amountSen <= 0 {
		return GenerateQRMessage{}, fmt.Errorf("validation: amount must be > 0, got %d", amountSen)
	}

	return GenerateQRMessage{DeviceID: deviceID, Amount: amountSen / 100}, nil
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `go test ./internal/validation/... -v`
Expected: PASS (all 8 tests)

- [ ] **Step 5: Commit**

```bash
git add internal/validation/validator.go internal/validation/validator_test.go
git commit -m "$(cat <<'EOF'
refactor(validation): parse pipe-delimited firmware payload

Replace JSON GENERATE_QR parsing with the plain-text
"{device_id}|{amount_sen}" format the firmware actually sends, converting
sen to Rupiah.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Rewrite `cmd/server/main.go` handler

**Files:**
- Modify: `cmd/server/main.go:1-146` (imports, `main()` subscribe call, `newGenerateQRHandler`, delete `buildQRResultPayload`/`qrResultPayload`)

**Interfaces:**
- Consumes: `qrtopic.RequestTopic`, `qrtopic.BuildDeviceTopic(string) string` (Task 1); `validation.ParseGenerateQRMessage([]byte) (validation.GenerateQRMessage, error)` (Task 2); `resolver.Resolver.ResolveDevice(ctx, deviceID string) (*resolver.ResolvedDevice, error)`, `transaction.Service.GenerateQR(ctx, device resolver.ResolvedDevice, amount int64) (*transaction.GenerateQRResult, error)`, `mqttclient.Client.Publish(topic string, payload []byte) error` (all pre-existing, unchanged).
- Produces: nothing new consumed elsewhere — this is the top-level wiring.

- [ ] **Step 1: Replace the imports**

In `cmd/server/main.go`, remove the now-unused `"encoding/json"` import (outbound payload becomes plain text, no more JSON marshaling):

```go
import (
	"context"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/jackc/pgx/v5/pgtype"
	"github.com/labstack/echo/v4"

	"service-payment-bridge/internal/config"
	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/httpserver"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/mqttclient"
	"service-payment-bridge/internal/qrtopic"
	"service-payment-bridge/internal/resolver"
	"service-payment-bridge/internal/secrets"
	"service-payment-bridge/internal/transaction"
	"service-payment-bridge/internal/validation"
)
```

- [ ] **Step 2: Replace the subscribe call**

Find in `main()`:

```go
	handler := newGenerateQRHandler(logger, q, deviceResolver, txService, mqttClient)
	if err := mqttClient.Subscribe("topic/#", handler); err != nil {
		logger.Error("failed to subscribe to topic/#", "error", err)
		os.Exit(1)
	}
```

Replace with:

```go
	handler := newGenerateQRHandler(logger, q, deviceResolver, txService, mqttClient)
	if err := mqttClient.Subscribe(qrtopic.RequestTopic, handler); err != nil {
		logger.Error("failed to subscribe to "+qrtopic.RequestTopic, "error", err)
		os.Exit(1)
	}
```

- [ ] **Step 3: Replace the handler and remove the old JSON reply builder**

Replace the entire `newGenerateQRHandler` function through the end of `buildQRResultPayload` (i.e. everything from `func newGenerateQRHandler(...)` down to the closing brace of `buildQRResultPayload`) with:

```go
func newGenerateQRHandler(logger *slog.Logger, q *sqlc.Queries, deviceResolver *resolver.Resolver, txService *transaction.Service, mqttClient *mqttclient.Client) mqtt.MessageHandler {
	return func(_ mqtt.Client, msg mqtt.Message) {
		ctx := context.Background()
		topicStr := msg.Topic()
		payload := msg.Payload()

		qrMsg, err := validation.ParseGenerateQRMessage(payload)
		if err != nil {
			logger.Warn("invalid GENERATE_QR payload", "topic", topicStr, "error", err)
			logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusFAILED, "", err.Error())
			return
		}

		replyTopic := qrtopic.BuildDeviceTopic(qrMsg.DeviceID)

		device, err := deviceResolver.ResolveDevice(ctx, qrMsg.DeviceID)
		if err != nil || !device.DeviceActive || !device.MerchantActive || !device.TenantActive {
			errMsg := "UNKNOWN_DEVICE"
			switch {
			case err == nil && !device.DeviceActive:
				errMsg = "DEVICE_INACTIVE"
			case err == nil && !device.MerchantActive:
				errMsg = "MERCHANT_INACTIVE"
			case err == nil && !device.TenantActive:
				errMsg = "TENANT_INACTIVE"
			}
			logger.Warn("device resolve failed", "device_id", qrMsg.DeviceID, "reason", errMsg)
			logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusFAILED, "", errMsg)
			publishFailureReply(ctx, q, logger, mqttClient, replyTopic, "")
			return
		}

		result, err := txService.GenerateQR(ctx, *device, qrMsg.Amount)
		if err != nil {
			logger.Error("GenerateQR orchestration error", "device_id", device.DeviceID, "error", err)
			logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusFAILED, "", err.Error())
			publishFailureReply(ctx, q, logger, mqttClient, replyTopic, "")
			return
		}
		logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusPROCESSED, result.TransactionID, "")

		if result.Status != "SUCCESS" {
			publishFailureReply(ctx, q, logger, mqttClient, replyTopic, result.TransactionID)
			return
		}

		outPayload := []byte("QR:" + result.QRISPayload)
		if err := mqttClient.Publish(replyTopic, outPayload); err != nil {
			logger.Error("failed to publish QR reply", "topic", replyTopic, "transaction_id", result.TransactionID, "error", err)
			logMQTTMessage(ctx, q, logger, replyTopic, outPayload, sqlc.MqttDirectionOUTBOUND, sqlc.MqttMessageStatusFAILED, result.TransactionID, err.Error())
			return
		}
		logMQTTMessage(ctx, q, logger, replyTopic, outPayload, sqlc.MqttDirectionOUTBOUND, sqlc.MqttMessageStatusPROCESSED, result.TransactionID, "")
	}
}

// publishFailureReply sends the firmware's fallback plain-text message
// (no structured error schema exists on-device — it falls through to TTS
// via AppPlayTip) and logs the outbound attempt. transactionID may be "".
func publishFailureReply(ctx context.Context, q *sqlc.Queries, logger *slog.Logger, mqttClient *mqttclient.Client, replyTopic, transactionID string) {
	const failureMessage = "Gagal membuat QR, coba lagi"
	if err := mqttClient.Publish(replyTopic, []byte(failureMessage)); err != nil {
		logger.Error("failed to publish failure reply", "topic", replyTopic, "error", err)
		logMQTTMessage(ctx, q, logger, replyTopic, []byte(failureMessage), sqlc.MqttDirectionOUTBOUND, sqlc.MqttMessageStatusFAILED, transactionID, err.Error())
		return
	}
	logMQTTMessage(ctx, q, logger, replyTopic, []byte(failureMessage), sqlc.MqttDirectionOUTBOUND, sqlc.MqttMessageStatusPROCESSED, transactionID, "")
}
```

`logMQTTMessage` at the bottom of the file is unchanged — keep it as-is.

- [ ] **Step 4: Verify it builds**

Run: `go build ./...`
Expected: builds cleanly, no unused-import or undefined-symbol errors.

- [ ] **Step 5: Commit**

```bash
git add cmd/server/main.go
git commit -m "$(cat <<'EOF'
refactor(server): wire handler to firmware MQTT contract

Subscribe only to qris/request (was wildcard topic/#, which never matched
the firmware's actual publish topic — the root cause of QR requests
always timing out on physical Q161 Pro hardware). Reply plain-text
"QR:{content}" or a human-readable failure message to topic_{device_id}.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Rewrite `internal/qrflow_test.go` end-to-end test

**Files:**
- Modify: `internal/qrflow_test.go` (full rewrite of the MQTT-facing parts; DB/httptest setup stays structurally the same)

**Interfaces:**
- Consumes: `qrtopic.RequestTopic`, `qrtopic.BuildDeviceTopic` (Task 1); `validation.ParseGenerateQRMessage` (Task 2); same `resolver`/`transaction`/`mqttclient` APIs as before.

- [ ] **Step 1: Replace the test**

Replace the entire contents of `internal/qrflow_test.go` with:

```go
package internal_test

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"

	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/mqttclient"
	"service-payment-bridge/internal/qrtopic"
	"service-payment-bridge/internal/resolver"
	"service-payment-bridge/internal/secrets"
	"service-payment-bridge/internal/transaction"
	"service-payment-bridge/internal/validation"
)

// TestGenerateQRFlow_EndToEnd wires the same components as cmd/server/main.go
// (minus HTTP server) against real Postgres + Mosquitto (docker-compose,
// Fase 1) and a mock Manjo (httptest), publishing a real MQTT message on
// the firmware's actual request topic/format and asserting a real "QR:"
// reply comes back on the device's own topic.
func TestGenerateQRFlow_EndToEnd(t *testing.T) {
	manjoServer := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/v1.0/access-token/b2b":
			json.NewEncoder(w).Encode(manjoclient.AccessTokenResponse{
				TokenType: "Bearer", AccessToken: "test-token", ExpiresIn: "900",
			})
		case "/v1.0/qr/qr-mpm-generate":
			json.NewEncoder(w).Encode(manjoclient.GenerateQRResponse{
				ReferenceNo: "A0000099", QRContent: "00020101-e2e-test",
				AdditionalInfo: manjoclient.GenerateQRResponseAdditionalInfo{
					ExpireDate: time.Now().Add(time.Hour).Format("20060102150405"),
				},
			})
		}
	}))
	defer manjoServer.Close()

	ctx := context.Background()
	pool, err := database.NewPool(ctx, "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable")
	if err != nil {
		t.Fatalf("failed to connect to test database: %v", err)
	}
	defer pool.Close()
	q := sqlc.New(pool)

	merchantID := "E2E-TEST-MERCHANT"
	deviceID := "E2E-TEST-DEVICE"
	replyTopic := qrtopic.BuildDeviceTopic(deviceID)

	t.Setenv("E2E_PRIVATE_KEY_REF", mustGenerateTestKeyBase64(t))
	t.Setenv("E2E_SECRET_REF", "e2e-test-secret")

	_, err = pool.Exec(ctx, `
		INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
		VALUES ($1, 'e2e-client-id', 'E2E_PRIVATE_KEY_REF', 'E2E_SECRET_REF', $1, '05')
		ON CONFLICT (merchant_id) DO NOTHING`, merchantID)
	if err != nil {
		t.Fatalf("failed to seed merchant: %v", err)
	}
	_, err = pool.Exec(ctx, `
		INSERT INTO devices (device_id, merchant_id, mqtt_topic)
		VALUES ($1, $2, $3)
		ON CONFLICT (device_id) DO NOTHING`, deviceID, merchantID, replyTopic)
	if err != nil {
		t.Fatalf("failed to seed device: %v", err)
	}
	t.Cleanup(func() {
		pool.Exec(context.Background(), `DELETE FROM mqtt_messages WHERE topic IN ($1, $2)`, qrtopic.RequestTopic, replyTopic)
		pool.Exec(context.Background(), `DELETE FROM manjo_api_logs WHERE transaction_id IN (SELECT transaction_id FROM transactions WHERE merchant_id = $1)`, merchantID)
		pool.Exec(context.Background(), `DELETE FROM transactions WHERE merchant_id = $1`, merchantID)
		pool.Exec(context.Background(), `DELETE FROM devices WHERE device_id = $1`, deviceID)
		pool.Exec(context.Background(), `DELETE FROM merchants WHERE merchant_id = $1`, merchantID)
	})

	deviceResolver := resolver.New(q)
	registry := manjoclient.NewRegistry()
	txService := transaction.NewServiceWithBaseURL(q, registry, secrets.EnvProvider{}, manjoServer.URL)

	mqttClient, err := mqttclient.Connect("tcp://localhost:11883", "", "")
	if err != nil {
		t.Fatalf("failed to connect to mosquitto (pastikan docker compose up -d jalan): %v", err)
	}
	defer mqttClient.Disconnect()

	handlerDone := make(chan string, 1)
	err = mqttClient.Subscribe(replyTopic, func(_ mqtt.Client, msg mqtt.Message) {
		handlerDone <- string(msg.Payload())
	})
	if err != nil {
		t.Fatalf("Subscribe(%q) error = %v", replyTopic, err)
	}

	err = mqttClient.Subscribe(qrtopic.RequestTopic, func(_ mqtt.Client, msg mqtt.Message) {
		// simulate the consumer handler inline (same logic as cmd/server/main.go)
		qrMsg, err := validation.ParseGenerateQRMessage(msg.Payload())
		if err != nil {
			return
		}
		device, err := deviceResolver.ResolveDevice(ctx, qrMsg.DeviceID)
		if err != nil {
			return
		}
		result, err := txService.GenerateQR(ctx, *device, qrMsg.Amount)
		if err != nil || result.Status != "SUCCESS" {
			return
		}
		mqttClient.Publish(qrtopic.BuildDeviceTopic(device.DeviceID), []byte("QR:"+result.QRISPayload))
	})
	if err != nil {
		t.Fatalf("Subscribe(%q) error = %v", qrtopic.RequestTopic, err)
	}

	// Firmware payload: "{device_id}|{amount_sen}" — 50000 Rupiah = 5000000 sen.
	reqPayload := []byte(deviceID + "|5000000")
	if err := mqttClient.Publish(qrtopic.RequestTopic, reqPayload); err != nil {
		t.Fatalf("Publish() error = %v", err)
	}

	var reply string
	select {
	case reply = <-handlerDone:
		// success
	case <-time.After(10 * time.Second):
		t.Fatal("timed out waiting for QR reply")
	}

	if !strings.HasPrefix(reply, "QR:") {
		t.Errorf("reply = %q, want prefix %q", reply, "QR:")
	}

	rows, err := pool.Query(ctx, `SELECT status FROM transactions WHERE merchant_id = $1`, merchantID)
	if err != nil {
		t.Fatalf("failed to query transactions: %v", err)
	}
	defer rows.Close()
	found := false
	for rows.Next() {
		var status string
		rows.Scan(&status)
		if status == "QR_GENERATED" {
			found = true
		}
	}
	if !found {
		t.Error("no transaction with status QR_GENERATED found in database")
	}
}

func mustGenerateTestKeyBase64(t *testing.T) string {
	t.Helper()
	key, kerr := rsa.GenerateKey(rand.Reader, 2048)
	if kerr != nil {
		t.Fatalf("failed to generate test key: %v", kerr)
	}
	der, derErr := x509.MarshalPKCS8PrivateKey(key)
	if derErr != nil {
		t.Fatalf("failed to marshal test key: %v", derErr)
	}
	return base64.StdEncoding.EncodeToString(der)
}
```

- [ ] **Step 2: Run the test**

Run: `docker compose up -d && go test ./internal/... -run TestGenerateQRFlow_EndToEnd -v`
Expected: PASS. (Requires `docker compose up -d` — Postgres + Mosquitto — already running; this is an integration test, not unit.)

- [ ] **Step 3: Commit**

```bash
git add internal/qrflow_test.go
git commit -m "$(cat <<'EOF'
test(qrflow): rewrite E2E test for firmware MQTT contract

Publish on qris/request with the real "{device_id}|{amount_sen}" payload
and assert a "QR:" reply on topic_{device_id}, replacing the old
hierarchical-topic/JSON assumptions.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Re-seed sandbox device to match the physical unit

**Files:**
- Create: `migrations/000012_reseed_device_mt58530503.up.sql`
- Create: `migrations/000012_reseed_device_mt58530503.down.sql`

**Interfaces:**
- Produces: a `devices` row with `device_id='MT58530503'` (matching the physical Q161 Pro's compiled-in `MQTT_MERCHANT_ID`, per `docs/eclipse/inc/def.h:11`) and `mqtt_topic='topic_MT58530503'`, replacing the synthetic `SANDBOX-DEVICE-001` seeded in migration 000011.
- Assumes no `transactions` rows reference `SANDBOX-DEVICE-001` yet (pre-launch dev DB) — the FK on `transactions.device_id` has no `ON UPDATE`/`ON DELETE` cascade, so this would fail on a DB with real transaction history against that device. If `migrate up` fails with a foreign-key violation here, stop and ask the user whether to reassign existing transactions' `device_id` first — don't force it.

- [ ] **Step 1: Write the up migration**

`migrations/000012_reseed_device_mt58530503.up.sql`:

```sql
DELETE FROM devices WHERE device_id = 'SANDBOX-DEVICE-001';

INSERT INTO devices (device_id, merchant_id, mqtt_topic)
VALUES ('MT58530503', 'SANDBOX-MERCHANT', 'topic_MT58530503');
```

- [ ] **Step 2: Write the down migration**

`migrations/000012_reseed_device_mt58530503.down.sql`:

```sql
DELETE FROM devices WHERE device_id = 'MT58530503';

INSERT INTO devices (device_id, merchant_id, mqtt_topic)
VALUES ('SANDBOX-DEVICE-001', 'SANDBOX-MERCHANT', 'topic/SANDBOX-MERCHANT/_/SANDBOX-DEVICE-001');
```

- [ ] **Step 3: Apply and verify**

Run:
```bash
export DATABASE_URL="postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable"
migrate -path migrations -database "$DATABASE_URL" up
```
Expected: migration `12` applies cleanly (`.../u 12`). Then verify:
```bash
psql "$DATABASE_URL" -c "SELECT device_id, mqtt_topic FROM devices WHERE device_id = 'MT58530503';"
```
Expected: one row, `mqtt_topic = topic_MT58530503`.

- [ ] **Step 4: Commit**

```bash
git add migrations/000012_reseed_device_mt58530503.up.sql migrations/000012_reseed_device_mt58530503.down.sql
git commit -m "$(cat <<'EOF'
db: reseed sandbox device as the physical Q161 Pro unit

Replace synthetic SANDBOX-DEVICE-001 with device_id=MT58530503 (the
firmware's compiled-in MQTT_MERCHANT_ID) so the physical test unit
resolves against real DB data.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Full verification pass

**Files:** none (verification only)

- [ ] **Step 1: Run the full test suite**

Run: `docker compose up -d && go build ./... && go test ./... -v`
Expected: all packages build and all tests pass, including `TestGenerateQRFlow_EndToEnd` (Task 4) and the untouched `resolver`/`transaction` DB-backed tests (unaffected — they only used `mqtt_topic` as an arbitrary unique string, never `qrtopic.Build`).

- [ ] **Step 2: Manual device test**

With the rebuilt service running against the Windows/WSL2 setup (`docs/windows-testing-setup.md`), trigger "QRIS Dinamis" on the physical Q161 Pro and confirm:
- `docker compose logs mosquitto` shows a client with ID `clientId-{SN}` connecting (the real device, not `auto-...`).
- The device's screen shows a QR code rather than "QR Request Timeout".

This step has no automated pass/fail — report back what the device shows and the mosquitto/service logs say if it still fails, so the next debugging pass has fresh evidence.

---

### Task 7: Update architecture docs to match the reconciled contract

Per design spec Section 7: `architecture.md`, `process-flow.md`, and `brainstorm-q161-updated.md` still describe the hierarchical-topic/JSON contract this plan replaces — left uncorrected they'll mislead the next reader. Scoped to the GENERATE_QR flow only (Task 1–5's actual scope); Section 7.3/Flow 3 (payment notification) is untouched — it isn't implemented in code yet and the design spec's non-goals don't cover it.

**Files:**
- Modify: `docs/architecture.md` (Sections 4.1, 4.4, 4.7, 7.1, 7.2)
- Modify: `docs/process-flow.md` (Flow 1 table, steps 1–3 and 13a/13b)
- Modify: `docs/brainstorm-q161-updated.md` (Section 3)

- [ ] **Step 1: `architecture.md` Section 4.1 — MQTT Consumer**

Find:
```
### 4.1 MQTT Consumer

- Subscribe ke wildcard topic (contoh: `topic_+` atau `topic_#` tergantung broker) supaya tidak perlu re-subscribe tiap ada merchant baru.
- Ekstrak `merchant_id` dari nama topic.
- Teruskan raw payload + `merchant_id` ke Message Parser.
- **Tidak** melakukan validasi bisnis — murni transport layer.
```

Replace with:
```
### 4.1 MQTT Consumer

- Subscribe sekali ke topic tetap `qris/request` — **shared** oleh semua device, bukan wildcard per-merchant (firmware publish semua request ke topic yang sama; lihat `docs/eclipse/src/mqtt.c:236`, `inc/def.h:36`).
- `device_id` datang dari payload (lihat Section 7.1), bukan dari nama topic.
- Teruskan raw payload ke Message Parser.
- **Tidak** melakukan validasi bisnis — murni transport layer.
```

- [ ] **Step 2: `architecture.md` Section 4.4 — Device Resolver**

Find:
```
### 4.4 Device Resolver

- Mapping topic MQTT (`topic/{merchant_id}/{tenant_slot}/{device_id}`) → konfigurasi lengkap: kredensial Manjo (dari `merchants`, lewat `device.merchant_id`), `manjo_sub_merchant_id` (dari `tenants`, kalau `device.tenant_id` tidak null), `manjo_store_id`/`manjo_terminal_id` (dari `devices`).
- **Kunci lookup utama adalah `device_id`** (unik global) — bukan kombinasi merchant_id+tenant_id+device_id. Segmen `merchant_id`/`tenant_slot` di topic terutama untuk keperluan debugging/filtering manual, bukan bagian dari logic lookup.
```

Replace with:
```
### 4.4 Device Resolver

- Mapping `device_id` (diambil dari payload request, Section 7.1) → konfigurasi lengkap: kredensial Manjo (dari `merchants`, lewat `device.merchant_id`), `manjo_sub_merchant_id` (dari `tenants`, kalau `device.tenant_id` tidak null), `manjo_store_id`/`manjo_terminal_id` (dari `devices`).
- **Kunci lookup adalah `device_id`** (unik global, sekarang satu-satunya sumbernya — topic inbound tidak lagi mengandung device_id karena sekarang shared di semua device).
```

- [ ] **Step 3: `architecture.md` Section 4.7 — MQTT Publisher**

Find:
```
- Terima payload internal dari Transaction Service (hasil generate QR atau hasil update payment).
- Serialize ke format final ([Section 7](#7-kontrak-internal-q161--service)) dan publish ke `devices.mqtt_topic` milik `transactions.device_id` terkait (**bukan** hasil parsing ulang dari topic request masuk — selalu lookup DB, supaya konsisten walau ada perubahan config device di tengah siklus transaksi).
```

Replace with:
```
- Terima payload internal dari Transaction Service (hasil generate QR atau hasil update payment).
- Serialize ke format final ([Section 7](#7-kontrak-internal-q161--service)) dan publish ke `"topic_" + device_id` (`internal/qrtopic.BuildDeviceTopic`), dihitung langsung dari `device_id` — **bukan** hasil parsing topic request masuk (topic inbound sekarang shared, tidak per-device lagi).
```

- [ ] **Step 4: `architecture.md` Section 7.1/7.2 — replace payload contract**

Find (Section 7.1 through end of 7.2, i.e. from `### 7.1 Q161 → Service` through the `FAILED` JSON example just before `### 7.3`):
```
### 7.1 Q161 → Service (Request Generate QR)

**Topic:** `topic/{merchant_id}/{tenant_slot}/{device_id}` — `tenant_slot` = `tenant_id` asli, atau literal `_` kalau device tanpa tenant

```json
{
  "type": "GENERATE_QR",
  "amount": 50000
}
```

| Field | Wajib | Keterangan |
|---|---|---|
| `type` | Ya | Pembeda jenis pesan, untuk future-proof kalau nanti ada jenis pesan lain di topic yang sama |
| `amount` | Ya | Integer Rupiah (bukan string, bukan desimal) |

`transaction_id` **tidak dikirim device** — direkomendasikan digenerate Service saat menerima pesan ini, format: `TRX-{yyyyMMdd}-{sequence}`, supaya penomoran konsisten dan uniqueness terjamin di satu tempat.

### 7.2 Service → Q161 (Hasil Generate QR)

**Topic:** `topic/{merchant_id}/{tenant_slot}/{device_id}` — `tenant_slot` = `tenant_id` asli, atau literal `_` kalau device tanpa tenant

```json
{
  "type": "QR_RESULT",
  "transaction_id": "TRX-20260924-000001",
  "status": "SUCCESS",
  "qris_payload": "00020101021226620015ID.CO.MANJO.WWW...",
  "expire_at": "2026-09-24T21:30:00+07:00"
}
```

Kalau gagal:

```json
{
  "type": "QR_RESULT",
  "transaction_id": "TRX-20260924-000001",
  "status": "FAILED",
  "error": "MANJO_TIMEOUT"
}
```
```

Replace with:
```
> **Kontrak berikut adalah kontrak nyata firmware** (ground truth dari `docs/eclipse/src/mqtt.c`, `inc/def.h`), bukan baseline yang bisa disesuaikan — lihat `docs/superpowers/specs/2026-09-25-mqtt-contract-reconciliation-design.md` untuk detail rekonsiliasinya.

### 7.1 Q161 → Service (Request Generate QR)

**Topic:** `qris/request` — tetap, shared oleh semua device (bukan per-device)

**Payload:** plain text, pipe-delimited: `"{device_id}|{amount_sen}"`

Contoh: `"MT58530503|5000000"` (device `MT58530503`, Rp50.000 = 5.000.000 sen)

| Bagian | Wajib | Keterangan |
|---|---|---|
| `device_id` | Ya | String sebelum `\|` — identitas device, dipakai Device Resolver |
| `amount_sen` | Ya | Integer, Rupiah × 100 (firmware kirim dalam sen) |

`transaction_id` **tidak dikirim device** — digenerate Service saat menerima pesan ini, format: `TRX-{yyyyMMdd}-{sequence}`.

### 7.2 Service → Q161 (Hasil Generate QR)

**Topic:** `"topic_" + device_id` (dihitung dari `device_id`, bukan lookup `devices.mqtt_topic`)

**Payload:** plain text — firmware tidak punya skema JSON untuk balasan, hanya mengenali prefix `QR:` untuk sukses; apa pun selain itu jatuh ke TTS generik (`AppPlayTip`).

Sukses: `"QR:{qris_payload}"` — contoh: `"QR:00020101021226620015ID.CO.MANJO.WWW..."`

Gagal: kalimat manusiawi berbahasa Indonesia, contoh: `"Gagal membuat QR, coba lagi"` (kode error asli tetap dicatat di `mqtt_messages`/`manjo_api_logs` untuk debugging internal, tidak dikirim ke device).
```

- [ ] **Step 5: `process-flow.md` Flow 1 — steps 1–3**

Find:
```
| 1 | Q161 Pro | Publish MQTT ke `topic/{merchant_id}/{tenant_slot}/{device_id}` miliknya sendiri, payload `{"type":"GENERATE_QR","amount":50000}` | — | — | — | (di luar scope Service) |
| 2 | MQTT Consumer | Terima pesan, split topic jadi 4 segmen (`topic/{merchant_id}/{tenant_slot}/{device_id}`), ekstrak `device_id` (segmen terakhir) | — | — | Topic harus match pola 4 segmen; `device_id` wajib ada | Pesan diabaikan, tidak dicatat (format topic tidak dikenali sistem) |
| 3 | Message Parser | Decode payload JSON → objek internal `{merchant_id, amount}` | — | — | JSON valid, field `amount` ada | Reject → lanjut ke step 4b |
```

Replace with:
```
| 1 | Q161 Pro | Publish MQTT ke `qris/request` (topic tetap, shared semua device), payload `"{device_id}\|{amount_sen}"` misal `"MT58530503\|5000000"` | — | — | — | (di luar scope Service) |
| 2 | MQTT Consumer | Terima pesan dari `qris/request` | — | — | — | — |
| 3 | Message Parser | Parse payload pipe-delimited → `{device_id, amount_rupiah}` (`amount_sen / 100`) | — | — | Ada tepat satu `\|`; `device_id` tidak kosong; `amount_sen` integer positif | Reject → lanjut ke step 4b |
```

- [ ] **Step 6: `process-flow.md` Flow 1 — steps 13a/13b**

Find:
```
| 13a | MQTT Publisher | Build payload `QR_RESULT` (`status=SUCCESS`, `qris_payload`, `expire_at`), lookup `devices.mqtt_topic` via `transactions.device_id`, publish ke topic tsb | `transactions` (data yang baru di-update), `devices.mqtt_topic` | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `status=PROCESSED`, `transaction_id`) | — | Kalau publish gagal → masuk retry queue (tidak mengubah status transaksi) |
| 13b | MQTT Publisher (kasus gagal) | Build payload `QR_RESULT` (`status=FAILED`, `error`), lookup `devices.mqtt_topic` via `transactions.device_id`, publish ke topic tsb | `devices.mqtt_topic` | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `transaction_id`) | — | — |
```

Replace with:
```
| 13a | MQTT Publisher | Build payload `"QR:{qris_payload}"`, publish ke `"topic_" + device_id` | `transactions` (data yang baru di-update) | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `status=PROCESSED`, `transaction_id`) | — | Kalau publish gagal → catat `FAILED` di `mqtt_messages` (tidak mengubah status transaksi) |
| 13b | MQTT Publisher (kasus gagal) | Build pesan plain text manusiawi (mis. `"Gagal membuat QR, coba lagi"`), publish ke `"topic_" + device_id` | — | `mqtt_messages`: INSERT (`direction=OUTBOUND`, `transaction_id`) | — | — |
```

- [ ] **Step 7: `brainstorm-q161-updated.md` Section 3 — clarify device_id terminology**

Find:
```
## 3. MQTT Topic

Setiap soundbox memiliki topic berdasarkan `merchant_id`.
```

Replace with:
```
## 3. MQTT Topic

Setiap soundbox memiliki topic berdasarkan `merchant_id`.

> **Update pasca-rekonsiliasi kontrak (2026-09-25):** `MQTT_MERCHANT_ID` yang di-compile ke firmware diperlakukan sebagai `device_id` di backend (satu binary firmware = satu device fisik). Topic outbound di bawah ini tetap `topic_{device_id}` persis seperti yang sudah didokumentasikan di sini — bagian ini sudah akurat. Yang berubah adalah topic **inbound** (request dari device ke Service): sekarang topic tetap `qris/request`, shared semua device, bukan per-merchant — lihat `docs/superpowers/specs/2026-09-25-mqtt-contract-reconciliation-design.md`.
```

- [ ] **Step 8: Commit**

```bash
git add docs/architecture.md docs/process-flow.md docs/brainstorm-q161-updated.md
git commit -m "$(cat <<'EOF'
docs: reconcile architecture docs with firmware MQTT contract

Update architecture.md, process-flow.md, and brainstorm-q161-updated.md
to describe the real qris/request + topic_{device_id} + plain-text
contract (Tasks 1-5), not the old hierarchical-topic/JSON assumptions.
Payment notification flow (Section 7.3/Flow 3) untouched — out of scope,
not yet implemented.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```
