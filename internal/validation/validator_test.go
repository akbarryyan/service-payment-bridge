package validation

import "testing"

func TestParseAndValidateGenerateQR_Valid(t *testing.T) {
	msg, err := ParseAndValidateGenerateQR([]byte(`{"type":"GENERATE_QR","amount":50000}`))
	if err != nil {
		t.Fatalf("ParseAndValidateGenerateQR() error = %v", err)
	}
	if msg.Amount != 50000 {
		t.Errorf("Amount = %d, want 50000", msg.Amount)
	}
}

func TestParseAndValidateGenerateQR_InvalidJSON(t *testing.T) {
	_, err := ParseAndValidateGenerateQR([]byte(`not json`))
	if err == nil {
		t.Fatal("expected error for invalid JSON, got nil")
	}
}

func TestParseAndValidateGenerateQR_WrongType(t *testing.T) {
	_, err := ParseAndValidateGenerateQR([]byte(`{"type":"SOMETHING_ELSE","amount":50000}`))
	if err == nil {
		t.Fatal("expected error for wrong type, got nil")
	}
}

func TestParseAndValidateGenerateQR_ZeroAmount(t *testing.T) {
	_, err := ParseAndValidateGenerateQR([]byte(`{"type":"GENERATE_QR","amount":0}`))
	if err == nil {
		t.Fatal("expected error for amount=0, got nil")
	}
}

func TestParseAndValidateGenerateQR_NegativeAmount(t *testing.T) {
	_, err := ParseAndValidateGenerateQR([]byte(`{"type":"GENERATE_QR","amount":-1000}`))
	if err == nil {
		t.Fatal("expected error for negative amount, got nil")
	}
}
