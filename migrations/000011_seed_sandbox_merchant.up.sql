INSERT INTO merchants (merchant_id, manjo_client_id, manjo_private_key_ref, manjo_client_secret_ref, manjo_merchant_id, manjo_channel_id)
VALUES (
    'SANDBOX-MERCHANT',
    'EQ1WYMK9calE',
    'MANJO_SANDBOX_PRIVATE_KEY',
    'MANJO_SANDBOX_CLIENT_SECRET',
    'MT58530503',
    '05'
);

INSERT INTO devices (device_id, merchant_id, mqtt_topic)
VALUES ('SANDBOX-DEVICE-001', 'SANDBOX-MERCHANT', 'topic/SANDBOX-MERCHANT/_/SANDBOX-DEVICE-001');
