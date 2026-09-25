CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TYPE merchant_status AS ENUM (
    'ACTIVE',
    'INACTIVE'
);

CREATE TYPE transaction_status AS ENUM (
    'PENDING',
    'QR_GENERATED',
    'PAID',
    'FAILED',
    'EXPIRED',
    'CANCELLED',
    'REFUNDED'
);

CREATE TYPE mqtt_direction AS ENUM (
    'INBOUND',
    'OUTBOUND'
);

CREATE TYPE mqtt_message_status AS ENUM (
    'RECEIVED',
    'PROCESSED',
    'FAILED'
);

CREATE TYPE manjo_api_direction AS ENUM (
    'OUTBOUND',
    'INBOUND'
);

CREATE TYPE manjo_api_operation AS ENUM (
    'ACCESS_TOKEN',
    'GENERATE_QR',
    'QUERY_PAYMENT',
    'PAYMENT_NOTIFY'
);
