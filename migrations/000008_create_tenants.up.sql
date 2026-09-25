CREATE TABLE tenants (
    id                     UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    tenant_id              VARCHAR(64)  NOT NULL UNIQUE,
    merchant_id            VARCHAR(36)  NOT NULL REFERENCES merchants (merchant_id),
    manjo_sub_merchant_id  VARCHAR(64),
    name                   VARCHAR(100),
    status                 merchant_status NOT NULL DEFAULT 'ACTIVE',

    created_at             TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at             TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_tenants_merchant_id ON tenants (merchant_id);
CREATE INDEX idx_tenants_status ON tenants (status);
