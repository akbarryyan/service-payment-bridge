package validation

import "testing"

func TestParseGenerateQRMessage_Valid(t *testing.T) {
	msg, err := ParseGenerateQRMessage([]byte("MT58530503|5000000"))
	if err != nil {
		t.Fatalf("ParseGenerateQRMessage() error = %v", err)
	}
	if msg.DeviceID != "MT58530503" {
		t.Errorf("DeviceID = %q, want %q", msg.DeviceID, "MT58530503")
	}
	if msg.Amount != 50000 {
		t.Errorf("Amount = %d, want 50000", msg.Amount)
	}
}

func TestParseGenerateQRMessage_TruncatesUncleanSen(t *testing.T) {
	// 50050 sen / 100 = 500.5 -> truncated to 500, not an error.
	msg, err := ParseGenerateQRMessage([]byte("MT58530503|50050"))
	if err != nil {
		t.Fatalf("ParseGenerateQRMessage() error = %v", err)
	}
	if msg.Amount != 500 {
		t.Errorf("Amount = %d, want 500", msg.Amount)
	}
}

func TestParseGenerateQRMessage_NoSeparator(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("MT58530503"))
	if err == nil {
		t.Fatal("expected error for missing '|' separator, got nil")
	}
}

func TestParseGenerateQRMessage_TooManySeparators(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("MT58530503|5000000|extra"))
	if err == nil {
		t.Fatal("expected error for extra '|' separator, got nil")
	}
}

func TestParseGenerateQRMessage_EmptyDeviceID(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("|5000000"))
	if err == nil {
		t.Fatal("expected error for empty device_id, got nil")
	}
}

func TestParseGenerateQRMessage_NonNumericAmount(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("MT58530503|abc"))
	if err == nil {
		t.Fatal("expected error for non-numeric amount, got nil")
	}
}

func TestParseGenerateQRMessage_ZeroAmount(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("MT58530503|0"))
	if err == nil {
		t.Fatal("expected error for amount=0, got nil")
	}
}

func TestParseGenerateQRMessage_NegativeAmount(t *testing.T) {
	_, err := ParseGenerateQRMessage([]byte("MT58530503|-100"))
	if err == nil {
		t.Fatal("expected error for negative amount, got nil")
	}
}
