package qrtopic

import (
	"fmt"
	"strings"
)

// RequestTopicFilter subscribes the backend to every device's request topic.
const RequestTopicFilter = "qris/request/+/+"

// DeviceTopic is the only topic a device receives QR replies and payment announcements on.
// merchantID is the Manjo merchant ID; sn is the device's hardware serial number.
func DeviceTopic(merchantID, sn string) string {
	return "topic/" + merchantID + "/" + sn
}

// DeviceRequestTopic is the only topic a device may publish QR requests to.
func DeviceRequestTopic(merchantID, sn string) string {
	return "qris/request/" + merchantID + "/" + sn
}

// ParseRequestTopic splits "qris/request/{merchantID}/{sn}". The broker's ACL guarantees that
// only the device {merchantID}-{sn} can publish there, so these are the sender's identity.
func ParseRequestTopic(topic string) (merchantID, sn string, err error) {
	parts := strings.Split(topic, "/")
	if len(parts) != 4 || parts[0] != "qris" || parts[1] != "request" || parts[2] == "" || parts[3] == "" {
		return "", "", fmt.Errorf("qrtopic: %q is not qris/request/{merchantId}/{sn}", topic)
	}
	return parts[2], parts[3], nil
}
