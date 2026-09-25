package validation

import (
	"encoding/json"
	"fmt"
)

type GenerateQRMessage struct {
	Type   string `json:"type"`
	Amount int64  `json:"amount"`
}

// ParseAndValidateGenerateQR decodes and validates a GENERATE_QR payload
// from Q161 (architecture.md Section 7.1): type must be "GENERATE_QR",
// amount must be a positive integer.
func ParseAndValidateGenerateQR(payload []byte) (GenerateQRMessage, error) {
	var msg GenerateQRMessage
	if err := json.Unmarshal(payload, &msg); err != nil {
		return GenerateQRMessage{}, fmt.Errorf("validation: invalid JSON payload: %w", err)
	}
	if msg.Type != "GENERATE_QR" {
		return GenerateQRMessage{}, fmt.Errorf("validation: unexpected type %q, want GENERATE_QR", msg.Type)
	}
	if msg.Amount <= 0 {
		return GenerateQRMessage{}, fmt.Errorf("validation: amount must be > 0, got %d", msg.Amount)
	}
	return msg, nil
}
