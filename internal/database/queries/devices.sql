-- name: GetDeviceWithMerchantAndTenant :one
SELECT
    d.device_id,
    d.merchant_id,
    d.tenant_id,
    d.mqtt_topic,
    d.manjo_store_id,
    d.manjo_terminal_id,
    d.status AS device_status,
    m.manjo_client_id,
    m.manjo_private_key_ref,
    m.manjo_client_secret_ref,
    m.manjo_merchant_id,
    m.manjo_channel_id,
    m.status AS merchant_active_status,
    t.manjo_sub_merchant_id,
    t.status AS tenant_active_status
FROM devices d
JOIN merchants m ON m.merchant_id = d.merchant_id
LEFT JOIN tenants t ON t.tenant_id = d.tenant_id
WHERE d.device_id = $1;
