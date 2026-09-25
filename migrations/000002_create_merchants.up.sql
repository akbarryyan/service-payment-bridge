CREATE TABLE merchants (
    id                        UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    merchant_id               VARCHAR(36)  NOT NULL UNIQUE,
    mqtt_topic                VARCHAR(64)  NOT NULL UNIQUE,

    manjo_client_id           VARCHAR(64)  NOT NULL,
    manjo_private_key_ref     VARCHAR(255) NOT NULL,
    manjo_client_secret_ref   VARCHAR(255) NOT NULL,
    manjo_merchant_id         VARCHAR(64)  NOT NULL,
    manjo_channel_id          VARCHAR(5)   NOT NULL,
    manjo_store_id            VARCHAR(64),
    manjo_terminal_id         VARCHAR(16),

    status                    merchant_status NOT NULL DEFAULT 'ACTIVE',

    created_at                TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at                TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_merchants_status ON merchants (status);
