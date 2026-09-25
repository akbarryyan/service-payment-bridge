CREATE TABLE devices (
    id                   UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id            VARCHAR(64)  NOT NULL UNIQUE,
    merchant_id          VARCHAR(36)  NOT NULL REFERENCES merchants (merchant_id),
    tenant_id            VARCHAR(64)  REFERENCES tenants (tenant_id),
    mqtt_topic           VARCHAR(255) NOT NULL UNIQUE,
    manjo_store_id       VARCHAR(64),
    manjo_terminal_id    VARCHAR(16),
    status               merchant_status NOT NULL DEFAULT 'ACTIVE',

    created_at           TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at           TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_devices_merchant_id ON devices (merchant_id);
CREATE INDEX idx_devices_tenant_id ON devices (tenant_id);
CREATE INDEX idx_devices_status ON devices (status);
