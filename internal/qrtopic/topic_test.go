package qrtopic

import "testing"

func TestRequestTopic(t *testing.T) {
	if RequestTopic != "qris/request" {
		t.Errorf("RequestTopic = %q, want %q", RequestTopic, "qris/request")
	}
}

func TestBuildDeviceTopic(t *testing.T) {
	got := BuildDeviceTopic("MT58530503")
	want := "topic_MT58530503"
	if got != want {
		t.Errorf("BuildDeviceTopic(%q) = %q, want %q", "MT58530503", got, want)
	}
}

func TestBuildDeviceTopic_EmptyDeviceID(t *testing.T) {
	got := BuildDeviceTopic("")
	want := "topic_"
	if got != want {
		t.Errorf("BuildDeviceTopic(%q) = %q, want %q", "", got, want)
	}
}
