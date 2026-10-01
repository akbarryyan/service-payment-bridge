package mqttclient

import (
	"testing"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"

	"service-payment-bridge/internal/testbroker"
)

func TestConnect_Success(t *testing.T) {
	c, err := Connect(testbroker.URL(), testbroker.Username(), testbroker.Password())
	if err != nil {
		t.Fatalf("Connect() error = %v (pastikan `docker compose up -d mosquitto` sedang jalan)", err)
	}
	defer c.Disconnect()

	if !c.IsConnected() {
		t.Fatal("IsConnected() = false, want true")
	}
}

func TestConnect_UnreachableBroker(t *testing.T) {
	_, err := Connect("tcp://localhost:19999", "", "")
	if err == nil {
		t.Fatal("Connect() expected error for unreachable broker, got nil")
	}
}

func TestPublishAndSubscribe_RoundTrip(t *testing.T) {
	c, err := Connect(testbroker.URL(), testbroker.Username(), testbroker.Password())
	if err != nil {
		t.Fatalf("Connect() error = %v (pastikan `docker compose up -d mosquitto` sedang jalan)", err)
	}
	defer c.Disconnect()

	received := make(chan string, 1)
	testTopic := "test/publish-subscribe-roundtrip"

	if err := c.Subscribe(testTopic, func(client mqtt.Client, msg mqtt.Message) {
		received <- string(msg.Payload())
	}); err != nil {
		t.Fatalf("Subscribe() error = %v", err)
	}

	if err := c.Publish(testTopic, []byte("hello")); err != nil {
		t.Fatalf("Publish() error = %v", err)
	}

	select {
	case payload := <-received:
		if payload != "hello" {
			t.Errorf("received payload = %q, want %q", payload, "hello")
		}
	case <-time.After(5 * time.Second):
		t.Fatal("timed out waiting for published message")
	}
}

func TestConnect_AnonymousRejected(t *testing.T) {
	if _, err := Connect(testbroker.URL(), "", ""); err == nil {
		t.Fatal("anonymous Connect() succeeded, want the broker to refuse it (allow_anonymous false)")
	}
}
