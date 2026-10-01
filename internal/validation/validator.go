package validation

import (
	"errors"
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
// Q161 Pro firmware publishes to its qris/request/{merchantId}/{sn} topic: "{device_id}|{amount_sen}"
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

// ErrIdentityMismatch means a request's topic, payload and device registration disagree.
var ErrIdentityMismatch = errors.New("validation: identity mismatch")

// CheckRequestIdentity makes sure a request published on qris/request/{topicMerchant}/{topicSN}
// really is that device's: the payload names the same SN, and the SN is registered to that
// merchant. The broker ACL already stops devices publishing on other devices' topics; this is
// defense in depth against a misconfigured ACL.
func CheckRequestIdentity(topicMerchant, topicSN, payloadSN, deviceManjoMerchantID string) error {
	if payloadSN != topicSN {
		return fmt.Errorf("%w: payload device %q, topic device %q", ErrIdentityMismatch, payloadSN, topicSN)
	}
	if deviceManjoMerchantID != topicMerchant {
		return fmt.Errorf("%w: device %q belongs to merchant %q, topic says %q", ErrIdentityMismatch, topicSN, deviceManjoMerchantID, topicMerchant)
	}
	return nil
}
