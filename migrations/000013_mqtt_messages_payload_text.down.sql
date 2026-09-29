ALTER TABLE mqtt_messages ALTER COLUMN payload TYPE JSONB USING payload::jsonb;
