-- name: LogMQTTMessage :one
INSERT INTO mqtt_messages (topic, payload, direction, status, transaction_id, error_message, processed_at)
VALUES ($1, $2, $3, $4, $5, $6, now())
RETURNING *;
