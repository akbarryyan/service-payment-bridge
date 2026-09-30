package config

import (
	"fmt"
	"time"

	"github.com/kelseyhightower/envconfig"
)

type Config struct {
	HTTPPort            string        `envconfig:"HTTP_PORT" default:"8080"`
	DatabaseURL         string        `envconfig:"DATABASE_URL" required:"true"`
	MQTTBrokerURL       string        `envconfig:"MQTT_BROKER_URL" required:"true"`
	MQTTUsername        string        `envconfig:"MQTT_USERNAME"`
	MQTTPassword        string        `envconfig:"MQTT_PASSWORD"`
	ManjoBaseURL        string        `envconfig:"MANJO_BASE_URL"`
	WebhookPublicURL    string        `envconfig:"WEBHOOK_PUBLIC_URL"`
	LogLevel            string        `envconfig:"LOG_LEVEL" default:"info"`
	PaymentPollInterval time.Duration `envconfig:"PAYMENT_POLL_INTERVAL" default:"3s"`
}

func Load() (*Config, error) {
	var cfg Config
	if err := envconfig.Process("", &cfg); err != nil {
		return nil, err
	}
	if cfg.PaymentPollInterval <= 0 {
		return nil, fmt.Errorf("config: PAYMENT_POLL_INTERVAL must be positive, got %s", cfg.PaymentPollInterval)
	}
	return &cfg, nil
}
