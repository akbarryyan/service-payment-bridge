ALTER TABLE transactions
    ADD COLUMN device_id VARCHAR(64) NOT NULL REFERENCES devices (device_id);

CREATE INDEX idx_transactions_device_id ON transactions (device_id);
