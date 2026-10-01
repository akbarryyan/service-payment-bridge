// Package announcer plays a payment announcement on a soundbox over MQTT.
package announcer

import (
	"context"
	"fmt"
	"log/slog"
	"time"

	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/mqttlog"
	"service-payment-bridge/internal/qrtopic"
	"service-payment-bridge/internal/voice"
)

// publishAttempts bounds retries of a failed publish: a payment the cashier never hears
// about costs more than a slightly late announcement.
const publishAttempts = 3

const defaultRetryDelay = time.Second

// Publisher publishes one MQTT message at QoS 1 (satisfied by *mqttclient.Client).
type Publisher interface {
	Publish(topic string, payload []byte) error
}

// Request is one payment to announce.
type Request struct {
	TransactionID string
	MerchantID    string // Manjo merchant ID; with DeviceID it names the device's topic
	DeviceID      string
	Rupiah        int64
}

// Announcer builds the audio payload for a payment and publishes it to the device.
type Announcer struct {
	pub        Publisher
	q          *sqlc.Queries
	logger     *slog.Logger
	retryDelay time.Duration
}

// New creates an Announcer.
func New(pub Publisher, q *sqlc.Queries, logger *slog.Logger) *Announcer {
	return &Announcer{pub: pub, q: q, logger: logger, retryDelay: defaultRetryDelay}
}

// Announce publishes the spoken amount to the device's topic/{merchant}/{sn} topic,
// retrying a failed publish, and records the final outcome in mqtt_messages.
func (a *Announcer) Announce(ctx context.Context, req Request) error {
	if req.MerchantID == "" || req.DeviceID == "" {
		return fmt.Errorf("announcer: merchant and device are required, got %q/%q", req.MerchantID, req.DeviceID)
	}
	payload, err := voice.Payload(req.Rupiah)
	if err != nil {
		return fmt.Errorf("announcer: %w", err)
	}
	topic := qrtopic.DeviceTopic(req.MerchantID, req.DeviceID)

	pubErr := a.publishWithRetry(ctx, topic, []byte(payload))

	entry := mqttlog.Entry{
		Topic:         topic,
		Payload:       []byte(payload),
		Direction:     sqlc.MqttDirectionOUTBOUND,
		Status:        sqlc.MqttMessageStatusPROCESSED,
		TransactionID: req.TransactionID,
	}
	if pubErr != nil {
		entry.Status = sqlc.MqttMessageStatusFAILED
		entry.ErrorMessage = pubErr.Error()
	}
	mqttlog.Record(ctx, a.q, a.logger, entry)

	if pubErr != nil {
		return fmt.Errorf("announcer: publish to %s failed after %d attempts: %w", topic, publishAttempts, pubErr)
	}
	return nil
}

func (a *Announcer) publishWithRetry(ctx context.Context, topic string, payload []byte) error {
	var err error
	for attempt := 1; attempt <= publishAttempts; attempt++ {
		if err = a.pub.Publish(topic, payload); err == nil {
			return nil
		}
		if attempt == publishAttempts {
			break
		}
		select {
		case <-ctx.Done():
			return fmt.Errorf("%w (retry aborted: %v)", err, ctx.Err())
		case <-time.After(a.retryDelay):
		}
	}
	return err
}
