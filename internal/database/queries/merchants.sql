-- name: GetMerchantByID :one
SELECT * FROM merchants WHERE merchant_id = $1;

-- name: ListMerchantsByManjoMerchantID :many
SELECT * FROM merchants WHERE manjo_merchant_id = $1;
