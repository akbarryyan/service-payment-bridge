// Package testbroker gives integration tests the local broker address and the dev-only
// `test` account that scripts/broker-bootstrap.sh --dev creates.
package testbroker

import "os"

// URL is the broker's internal listener.
func URL() string { return getenv("MQTT_TEST_BROKER_URL", "tcp://localhost:11883") }

// Username is the dev-only test account.
func Username() string { return getenv("MQTT_TEST_USERNAME", "test") }

// Password is the dev-only test account's password.
func Password() string { return getenv("MQTT_TEST_PASSWORD", "test-dev-only") }

func getenv(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}
