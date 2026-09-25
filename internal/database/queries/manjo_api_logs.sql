-- name: LogManjoAPICall :one
INSERT INTO manjo_api_logs (direction, operation, endpoint, http_status, request_body, response_body, transaction_id, duration_ms)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
RETURNING *;
