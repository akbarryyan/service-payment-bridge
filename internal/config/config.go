package config

import "github.com/kelseyhightower/envconfig"

type Config struct {
	HTTPPort         string `envconfig:"HTTP_PORT" default:"8080"`
	DatabaseURL      string `envconfig:"DATABASE_URL" required:"true"`
	MQTTBrokerURL    string `envconfig:"MQTT_BROKER_URL" required:"true"`
	MQTTUsername     string `envconfig:"MQTT_USERNAME"`
	MQTTPassword     string `envconfig:"MQTT_PASSWORD"`
	ManjoBaseURL     string `envconfig:"MANJO_BASE_URL"`
	WebhookPublicURL string `envconfig:"WEBHOOK_PUBLIC_URL"`
	LogLevel         string `envconfig:"LOG_LEVEL" default:"info"`
}

func Load() (*Config, error) {
	var cfg Config
	if err := envconfig.Process("", &cfg); err != nil {
		return nil, err
	}
	return &cfg, nil
}
