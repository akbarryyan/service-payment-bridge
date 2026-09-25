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
