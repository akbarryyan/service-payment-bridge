ALTER TABLE mqtt_messages ALTER COLUMN payload TYPE TEXT USING payload::text;
