DELETE FROM mqtt_messages WHERE transaction_id IN (SELECT transaction_id FROM transactions WHERE device_id = 'SANDBOX-DEVICE-001');
DELETE FROM manjo_api_logs WHERE transaction_id IN (SELECT transaction_id FROM transactions WHERE device_id = 'SANDBOX-DEVICE-001');
DELETE FROM transactions WHERE device_id = 'SANDBOX-DEVICE-001';
DELETE FROM devices WHERE device_id = 'SANDBOX-DEVICE-001';

INSERT INTO devices (device_id, merchant_id, mqtt_topic)
VALUES ('MT58530503', 'SANDBOX-MERCHANT', 'topic_MT58530503');
