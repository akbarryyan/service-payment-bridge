CREATE TABLE manjo_api_logs (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    direction       manjo_api_direction NOT NULL,
    operation       manjo_api_operation NOT NULL,
    endpoint        VARCHAR(255) NOT NULL,
    http_status     INTEGER,
    request_body    JSONB,
    response_body   JSONB,
    transaction_id  VARCHAR(64) REFERENCES transactions (transaction_id),
    duration_ms     INTEGER,

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_manjo_api_logs_transaction_id ON manjo_api_logs (transaction_id);
CREATE INDEX idx_manjo_api_logs_operation_created_at ON manjo_api_logs (operation, created_at DESC);
