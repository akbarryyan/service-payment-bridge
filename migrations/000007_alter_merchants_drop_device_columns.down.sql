ALTER TABLE merchants
    ADD COLUMN mqtt_topic VARCHAR(64) UNIQUE,
    ADD COLUMN manjo_store_id VARCHAR(64),
    ADD COLUMN manjo_terminal_id VARCHAR(16);
