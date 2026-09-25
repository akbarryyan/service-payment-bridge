package main

import (
	"context"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/jackc/pgx/v5/pgtype"
	"github.com/labstack/echo/v4"

	"service-payment-bridge/internal/config"
	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/httpserver"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/mqttclient"
	"service-payment-bridge/internal/qrtopic"
	"service-payment-bridge/internal/resolver"
	"service-payment-bridge/internal/secrets"
	"service-payment-bridge/internal/transaction"
	"service-payment-bridge/internal/validation"
)

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, nil))
	slog.SetDefault(logger)

	cfg, err := config.Load()
	if err != nil {
		logger.Error("failed to load config", "error", err)
		os.Exit(1)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	pool, err := database.NewPool(ctx, cfg.DatabaseURL)
	cancel()
	if err != nil {
		logger.Error("failed to connect to database", "error", err)
		os.Exit(1)
	}
	defer pool.Close()

	q := sqlc.New(pool)
	deviceResolver := resolver.New(q)
	registry := manjoclient.NewRegistry()
	txService := transaction.NewServiceWithBaseURL(q, registry, secrets.EnvProvider{}, cfg.ManjoBaseURL)

	mqttClient, err := mqttclient.Connect(cfg.MQTTBrokerURL, cfg.MQTTUsername, cfg.MQTTPassword)
	if err != nil {
		logger.Error("failed to connect to mqtt broker", "error", err)
		os.Exit(1)
	}
	defer mqttClient.Disconnect()

	handler := newGenerateQRHandler(logger, q, deviceResolver, txService, mqttClient)
	if err := mqttClient.Subscribe(qrtopic.RequestTopic, handler); err != nil {
		logger.Error("failed to subscribe to "+qrtopic.RequestTopic, "error", err)
		os.Exit(1)
	}

	e := echo.New()
	e.HideBanner = true
	e.GET("/healthz", httpserver.HealthzHandler(pool))

	go func() {
		if err := e.Start(":" + cfg.HTTPPort); err != nil && err != http.ErrServerClosed {
			logger.Error("http server error", "error", err)
			os.Exit(1)
		}
	}()

	logger.Info("service started", "port", cfg.HTTPPort)

	quit := make(chan os.Signal, 1)
	signal.Notify(quit, os.Interrupt, syscall.SIGTERM)
	<-quit

	logger.Info("shutting down")
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer shutdownCancel()
	if err := e.Shutdown(shutdownCtx); err != nil {
		logger.Error("http server shutdown error", "error", err)
	}
}

func newGenerateQRHandler(logger *slog.Logger, q *sqlc.Queries, deviceResolver *resolver.Resolver, txService *transaction.Service, mqttClient *mqttclient.Client) mqtt.MessageHandler {
	return func(_ mqtt.Client, msg mqtt.Message) {
		ctx := context.Background()
		topicStr := msg.Topic()
		payload := msg.Payload()

		qrMsg, err := validation.ParseGenerateQRMessage(payload)
		if err != nil {
			logger.Warn("invalid GENERATE_QR payload", "topic", topicStr, "error", err)
			logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusFAILED, "", err.Error())
			return
		}

		replyTopic := qrtopic.BuildDeviceTopic(qrMsg.DeviceID)

		device, err := deviceResolver.ResolveDevice(ctx, qrMsg.DeviceID)
		if err != nil || !device.DeviceActive || !device.MerchantActive || !device.TenantActive {
			errMsg := "UNKNOWN_DEVICE"
			switch {
			case err == nil && !device.DeviceActive:
				errMsg = "DEVICE_INACTIVE"
			case err == nil && !device.MerchantActive:
				errMsg = "MERCHANT_INACTIVE"
			case err == nil && !device.TenantActive:
				errMsg = "TENANT_INACTIVE"
			}
			logger.Warn("device resolve failed", "device_id", qrMsg.DeviceID, "reason", errMsg)
			logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusFAILED, "", errMsg)
			publishFailureReply(ctx, q, logger, mqttClient, replyTopic, "")
			return
		}

		result, err := txService.GenerateQR(ctx, *device, qrMsg.Amount)
		if err != nil {
			logger.Error("GenerateQR orchestration error", "device_id", device.DeviceID, "error", err)
			logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusFAILED, "", err.Error())
			publishFailureReply(ctx, q, logger, mqttClient, replyTopic, "")
			return
		}
		logMQTTMessage(ctx, q, logger, topicStr, payload, sqlc.MqttDirectionINBOUND, sqlc.MqttMessageStatusPROCESSED, result.TransactionID, "")

		if result.Status != "SUCCESS" {
			publishFailureReply(ctx, q, logger, mqttClient, replyTopic, result.TransactionID)
			return
		}

		outPayload := []byte("QR:" + result.QRISPayload)
		if err := mqttClient.Publish(replyTopic, outPayload); err != nil {
			logger.Error("failed to publish QR reply", "topic", replyTopic, "transaction_id", result.TransactionID, "error", err)
			logMQTTMessage(ctx, q, logger, replyTopic, outPayload, sqlc.MqttDirectionOUTBOUND, sqlc.MqttMessageStatusFAILED, result.TransactionID, err.Error())
			return
		}
		logMQTTMessage(ctx, q, logger, replyTopic, outPayload, sqlc.MqttDirectionOUTBOUND, sqlc.MqttMessageStatusPROCESSED, result.TransactionID, "")
	}
}

// publishFailureReply sends the firmware's fallback plain-text message
// (no structured error schema exists on-device — it falls through to TTS
// via AppPlayTip) and logs the outbound attempt. transactionID may be "".
func publishFailureReply(ctx context.Context, q *sqlc.Queries, logger *slog.Logger, mqttClient *mqttclient.Client, replyTopic, transactionID string) {
	const failureMessage = "Gagal membuat QR, coba lagi"
	if err := mqttClient.Publish(replyTopic, []byte(failureMessage)); err != nil {
		logger.Error("failed to publish failure reply", "topic", replyTopic, "error", err)
		logMQTTMessage(ctx, q, logger, replyTopic, []byte(failureMessage), sqlc.MqttDirectionOUTBOUND, sqlc.MqttMessageStatusFAILED, transactionID, err.Error())
		return
	}
	logMQTTMessage(ctx, q, logger, replyTopic, []byte(failureMessage), sqlc.MqttDirectionOUTBOUND, sqlc.MqttMessageStatusPROCESSED, transactionID, "")
}

func logMQTTMessage(ctx context.Context, q *sqlc.Queries, logger *slog.Logger, topic string, payload []byte, direction sqlc.MqttDirection, status sqlc.MqttMessageStatus, transactionID, errMsg string) {
	params := sqlc.LogMQTTMessageParams{
		Topic:     topic,
		Payload:   payload,
		Direction: direction,
		Status:    status,
	}
	if transactionID != "" {
		params.TransactionID = pgtype.Text{String: transactionID, Valid: true}
	}
	if errMsg != "" {
		params.ErrorMessage = pgtype.Text{String: errMsg, Valid: true}
	}
	if _, err := q.LogMQTTMessage(ctx, params); err != nil {
		logger.Error("failed to log mqtt_messages", "error", err)
	}
}
