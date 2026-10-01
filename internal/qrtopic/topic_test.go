package qrtopic

import "testing"

func TestDeviceTopics(t *testing.T) {
	if got := DeviceTopic("MT58530503", "00078020709"); got != "topic/MT58530503/00078020709" {
		t.Errorf("DeviceTopic() = %q", got)
	}
	if got := DeviceRequestTopic("MT58530503", "00078020709"); got != "qris/request/MT58530503/00078020709" {
		t.Errorf("DeviceRequestTopic() = %q", got)
	}
	if RequestTopicFilter != "qris/request/+/+" {
		t.Errorf("RequestTopicFilter = %q", RequestTopicFilter)
	}
}

func TestParseRequestTopic(t *testing.T) {
	merchant, sn, err := ParseRequestTopic("qris/request/MT58530503/00078020709")
	if err != nil || merchant != "MT58530503" || sn != "00078020709" {
		t.Fatalf("ParseRequestTopic() = %q, %q, %v", merchant, sn, err)
	}

	for _, bad := range []string{
		"qris/request",
		"qris/request/MT58530503",
		"qris/request/MT58530503/00078020709/extra",
		"qris/reply/MT58530503/00078020709",
		"topic/request/MT58530503/00078020709",
		"qris/request//00078020709",
		"qris/request/MT58530503/",
	} {
		if _, _, err := ParseRequestTopic(bad); err == nil {
			t.Errorf("ParseRequestTopic(%q) error = nil, want error", bad)
		}
	}
}
