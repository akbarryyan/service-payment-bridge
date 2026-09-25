CREATE TABLE mqtt_messages (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    topic           VARCHAR(64) NOT NULL,
    payload         JSONB       NOT NULL,
    direction       mqtt_direction NOT NULL,
    status          mqtt_message_status NOT NULL DEFAULT 'RECEIVED',
    transaction_id  VARCHAR(64) REFERENCES transactions (transaction_id),

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    processed_at    TIMESTAMPTZ,
    error_message   TEXT
);

CREATE INDEX idx_mqtt_messages_transaction_id ON mqtt_messages (transaction_id);
CREATE INDEX idx_mqtt_messages_topic_created_at ON mqtt_messages (topic, created_at DESC);
