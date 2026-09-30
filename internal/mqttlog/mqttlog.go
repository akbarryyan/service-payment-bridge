// Package mqttlog records MQTT traffic in mqtt_messages — the trail used to trace
// "paid, but the soundbox stayed silent" (architecture.md principle 6).
package mqttlog

import (
	"context"
	"log/slog"

	"github.com/jackc/pgx/v5/pgtype"

	"service-payment-bridge/internal/database/sqlc"
)

// Entry is one MQTT message to record.
type Entry struct {
	Topic         string
	Payload       []byte
	Direction     sqlc.MqttDirection
	Status        sqlc.MqttMessageStatus
	TransactionID string // "" = not linked to a transaction
	ErrorMessage  string // "" = none
}

// Record inserts e. A failure is logged, never returned: tracing must not break the flow
// it traces.
func Record(ctx context.Context, q *sqlc.Queries, logger *slog.Logger, e Entry) {
	params := sqlc.LogMQTTMessageParams{
		Topic:     e.Topic,
		Payload:   string(e.Payload),
		Direction: e.Direction,
		Status:    e.Status,
	}
	if e.TransactionID != "" {
		params.TransactionID = pgtype.Text{String: e.TransactionID, Valid: true}
	}
	if e.ErrorMessage != "" {
		params.ErrorMessage = pgtype.Text{String: e.ErrorMessage, Valid: true}
	}
	if _, err := q.LogMQTTMessage(ctx, params); err != nil {
		logger.Error("failed to log mqtt_messages", "topic", e.Topic, "error", err)
	}
}
