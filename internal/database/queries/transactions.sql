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
    next_query_at = $6,
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

-- name: ClaimDueTransactions :many
-- Claims up to batch_size QR_GENERATED transactions whose next check is due — including
-- rows whose next_query_at was never scheduled (NULL) — and pushes their next check out
-- by poll_interval from the DB clock, so a failed or crashed check is retried next time.
-- SKIP LOCKED keeps two poller instances from claiming the same row. merchant_id NULL
-- means all merchants (tests pass their own merchant to stay off real rows).
UPDATE transactions
SET next_query_at = now() + sqlc.arg(poll_interval)::interval
WHERE transaction_id IN (
    SELECT t.transaction_id FROM transactions t
    WHERE t.status = 'QR_GENERATED'
      AND (t.next_query_at IS NULL OR t.next_query_at <= now())
      AND (sqlc.narg(merchant_id)::varchar IS NULL OR t.merchant_id = sqlc.narg(merchant_id)::varchar)
    ORDER BY t.next_query_at NULLS FIRST
    LIMIT sqlc.arg(batch_size)::int
    FOR UPDATE SKIP LOCKED
)
RETURNING *;

-- name: TransitionFromQRGenerated :one
-- Moves a transaction out of QR_GENERATED. Returns no row when it already left that
-- state, so exactly one caller — across instances — performs each transition.
-- NULL manjo_status_code / paid_at leave the stored values unchanged.
UPDATE transactions
SET status = sqlc.arg(status)::transaction_status,
    manjo_status_code = COALESCE(sqlc.narg(manjo_status_code)::varchar, manjo_status_code),
    paid_at = COALESCE(sqlc.narg(paid_at)::timestamptz, paid_at),
    next_query_at = NULL,
    updated_at = now()
WHERE transaction_id = sqlc.arg(transaction_id) AND status = 'QR_GENERATED'
RETURNING *;
