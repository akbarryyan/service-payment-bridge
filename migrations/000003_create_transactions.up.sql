CREATE TABLE transactions (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    transaction_id      VARCHAR(64)  NOT NULL UNIQUE,
    reference_no        VARCHAR(64),
    merchant_id         VARCHAR(36)  NOT NULL REFERENCES merchants (merchant_id),

    amount              BIGINT       NOT NULL CHECK (amount > 0),
    status              transaction_status NOT NULL DEFAULT 'PENDING',
    manjo_status_code   VARCHAR(2),

    qris_payload        TEXT,
    external_id         VARCHAR(36),
    expire_at           TIMESTAMPTZ,

    created_at          TIMESTAMPTZ NOT NULL DEFAULT now(),
    paid_at             TIMESTAMPTZ,
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_transactions_merchant_status ON transactions (merchant_id, status);
CREATE INDEX idx_transactions_reference_no ON transactions (reference_no);
CREATE INDEX idx_transactions_expire_at ON transactions (expire_at) WHERE status = 'QR_GENERATED';
