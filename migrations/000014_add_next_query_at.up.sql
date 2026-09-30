ALTER TABLE transactions ADD COLUMN next_query_at TIMESTAMPTZ;

CREATE INDEX idx_transactions_next_query_at
    ON transactions (next_query_at)
    WHERE status = 'QR_GENERATED';

-- Existing QR_GENERATED rows carry an expire_at that is 7 hours too late (Manjo's
-- expireDate bug, see the spec). Correct it to the observed 7.5-minute validity and
-- make them due now: the poller asks Manjo once (-> 403 "Transaction Expire" ->
-- EXPIRED) or, failing that, the deadline safety net expires them immediately.
UPDATE transactions
SET expire_at = created_at + interval '7 minutes 30 seconds',
    next_query_at = now()
WHERE status = 'QR_GENERATED';
