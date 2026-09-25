package qrtopic

// RequestTopic is the single, shared inbound topic every Q161 Pro device
// publishes GENERATE_QR requests to (firmware: mqtt.c:236, def.h:36).
const RequestTopic = "qris/request"

// BuildDeviceTopic returns the topic a device listens on for its own
// reply, matching the firmware's subscription "topic_{MQTT_MERCHANT_ID}"
// set up in applyMqttParam() (param.c, mqtt.c:192).
func BuildDeviceTopic(deviceID string) string {
	return "topic_" + deviceID
}
