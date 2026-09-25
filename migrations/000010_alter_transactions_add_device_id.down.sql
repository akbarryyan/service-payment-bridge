DROP INDEX IF EXISTS idx_transactions_device_id;
ALTER TABLE transactions DROP COLUMN device_id;
