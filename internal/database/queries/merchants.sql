-- name: GetMerchantByID :one
SELECT * FROM merchants WHERE merchant_id = $1;
