package config

import "testing"

func TestLoad_RequiredFieldsPresent(t *testing.T) {
	t.Setenv("DATABASE_URL", "postgres://user:pass@localhost:5432/db")
	t.Setenv("MQTT_BROKER_URL", "tcp://localhost:1883")

	cfg, err := Load()
	if err != nil {
		t.Fatalf("Load() returned error: %v", err)
	}
	if cfg.DatabaseURL != "postgres://user:pass@localhost:5432/db" {
		t.Errorf("DatabaseURL = %q, want %q", cfg.DatabaseURL, "postgres://user:pass@localhost:5432/db")
	}
	if cfg.MQTTBrokerURL != "tcp://localhost:1883" {
		t.Errorf("MQTTBrokerURL = %q, want %q", cfg.MQTTBrokerURL, "tcp://localhost:1883")
	}
	if cfg.HTTPPort != "8080" {
		t.Errorf("HTTPPort default = %q, want %q", cfg.HTTPPort, "8080")
	}
	if cfg.LogLevel != "info" {
		t.Errorf("LogLevel default = %q, want %q", cfg.LogLevel, "info")
	}
}

func TestLoad_MissingRequiredField(t *testing.T) {
	t.Setenv("MQTT_BROKER_URL", "tcp://localhost:1883")

	_, err := Load()
	if err == nil {
		t.Fatal("Load() expected error for missing DATABASE_URL, got nil")
	}
}
