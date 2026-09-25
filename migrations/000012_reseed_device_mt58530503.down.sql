DELETE FROM devices WHERE device_id = 'MT58530503';

INSERT INTO devices (device_id, merchant_id, mqtt_topic)
VALUES ('SANDBOX-DEVICE-001', 'SANDBOX-MERCHANT', 'topic/SANDBOX-MERCHANT/_/SANDBOX-DEVICE-001');
