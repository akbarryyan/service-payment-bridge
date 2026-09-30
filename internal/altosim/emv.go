// Package altosim pays a UAT QRIS through Alto's issuer simulator
// (POST /altopay/qr-payment/payment, see docs/manjo-collection/QR Payment.yml), so the
// generate → pay → announce flow can be exercised without the Alto website.
// Test environment only.
package altosim

import (
	"fmt"
	"strconv"
)

// TLV is one level of an EMV QR payload: two-digit tag → value.
type TLV map[string]string

// ParseEMV parses one level of EMV "tag(2) length(2) value" data. Nested templates such
// as tags 26 and 62 are parsed by calling ParseEMV again on their value.
func ParseEMV(s string) (TLV, error) {
	out := TLV{}
	for i := 0; i < len(s); {
		if i+4 > len(s) {
			return nil, fmt.Errorf("altosim: truncated EMV field at offset %d", i)
		}
		tag := s[i : i+2]
		n, err := strconv.Atoi(s[i+2 : i+4])
		if err != nil {
			return nil, fmt.Errorf("altosim: invalid length for tag %s: %w", tag, err)
		}
		start := i + 4
		if start+n > len(s) {
			return nil, fmt.Errorf("altosim: tag %s length %d overruns the payload", tag, n)
		}
		out[tag] = s[start : start+n]
		i = start + n
	}
	return out, nil
}
