package validation

import (
	"fmt"
	"strconv"
	"strings"
)

// GenerateQRMessage is a parsed device GENERATE_QR request. Amount is
// already converted from sen (firmware wire unit) to Rupiah.
type GenerateQRMessage struct {
	DeviceID string
	Amount   int64 // Rupiah
}

// ParseGenerateQRMessage parses the pipe-delimited plain-text payload the
// Q161 Pro firmware publishes to qrtopic.RequestTopic: "{device_id}|{amount_sen}"
// (mqtt.c:228). amount_sen is divided by 100 to get Rupiah; a non-zero
// remainder is truncated rather than rejected, since the firmware's own
// input flow (GetAmount) always sends multiples of 100 in practice — see
// docs/superpowers/specs/2026-09-25-mqtt-contract-reconciliation-design.md Section 6.
func ParseGenerateQRMessage(payload []byte) (GenerateQRMessage, error) {
	parts := strings.Split(string(payload), "|")
	if len(parts) != 2 {
		return GenerateQRMessage{}, fmt.Errorf("validation: expected exactly one '|' separator, got payload %q", payload)
	}

	deviceID := parts[0]
	if deviceID == "" {
		return GenerateQRMessage{}, fmt.Errorf("validation: device_id is empty in payload %q", payload)
	}

	amountSen, err := strconv.ParseInt(parts[1], 10, 64)
	if err != nil {
		return GenerateQRMessage{}, fmt.Errorf("validation: invalid amount %q: %w", parts[1], err)
	}
	if amountSen <= 0 {
		return GenerateQRMessage{}, fmt.Errorf("validation: amount must be > 0, got %d", amountSen)
	}

	return GenerateQRMessage{DeviceID: deviceID, Amount: amountSen / 100}, nil
}
