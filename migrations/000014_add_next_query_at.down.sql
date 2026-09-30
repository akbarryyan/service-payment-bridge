-- The expire_at correction made by the up migration is not reverted.
DROP INDEX IF EXISTS idx_transactions_next_query_at;
ALTER TABLE transactions DROP COLUMN IF EXISTS next_query_at;
