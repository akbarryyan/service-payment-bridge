-- name: CreateTransaction :one
INSERT INTO transactions (transaction_id, merchant_id, device_id, amount, status)
VALUES ($1, $2, $3, $4, 'PENDING')
RETURNING *;

-- name: MarkTransactionQRGenerated :one
UPDATE transactions
SET status = 'QR_GENERATED',
    qris_payload = $2,
    reference_no = $3,
    external_id = $4,
    expire_at = $5,
    updated_at = now()
WHERE transaction_id = $1
RETURNING *;

-- name: MarkTransactionFailed :one
UPDATE transactions
SET status = 'FAILED',
    updated_at = now()
WHERE transaction_id = $1
RETURNING *;

-- name: GetTransactionByID :one
SELECT * FROM transactions WHERE transaction_id = $1;
