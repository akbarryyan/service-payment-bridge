package altosim

import (
	"bytes"
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"
)

// PaymentPath is Alto's pay-a-QR endpoint, and the canonical path it signs.
const PaymentPath = "/altopay/qr-payment/payment"

// FormatTimestamp renders t the way Alto signs it: UTC "2006-01-02 15:04:05.000Z".
func FormatTimestamp(t time.Time) string {
	return t.UTC().Format("2006-01-02 15:04:05.000Z")
}

// Sign computes X-Alto-Signature: hex HMAC-SHA256 keyed with validationKey over
// "POST:" + PaymentPath + ":" + apiKey + ":" + hex(sha256(body)) + ":" + timestamp.
func Sign(apiKey, validationKey string, body []byte, timestamp string) string {
	bodyHash := sha256.Sum256(body)
	plain := http.MethodPost + ":" + PaymentPath + ":" + apiKey + ":" + hex.EncodeToString(bodyHash[:]) + ":" + timestamp
	mac := hmac.New(sha256.New, []byte(validationKey))
	mac.Write([]byte(plain))
	return hex.EncodeToString(mac.Sum(nil))
}

// Client calls Alto's simulator.
type Client struct {
	BaseURL       string
	APIKey        string
	ValidationKey string
	HTTPClient    *http.Client // nil = 15s timeout default
}

// Pay sends req, signed with req.Data.DateTime as the timestamp, and returns Alto's
// HTTP status and raw body.
func (c *Client) Pay(ctx context.Context, req PaymentRequest) (int, []byte, error) {
	body, err := json.Marshal(req)
	if err != nil {
		return 0, nil, fmt.Errorf("altosim: failed to marshal payment: %w", err)
	}
	timestamp := req.Data.DateTime

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, strings.TrimRight(c.BaseURL, "/")+PaymentPath, bytes.NewReader(body))
	if err != nil {
		return 0, nil, err
	}
	httpReq.Header.Set("Content-Type", "application/json")
	httpReq.Header.Set("X-Alto-Timestamp", timestamp)
	httpReq.Header.Set("X-Alto-Key", c.APIKey)
	httpReq.Header.Set("X-Alto-Signature", Sign(c.APIKey, c.ValidationKey, body, timestamp))

	hc := c.HTTPClient
	if hc == nil {
		hc = &http.Client{Timeout: 15 * time.Second}
	}
	resp, err := hc.Do(httpReq)
	if err != nil {
		return 0, nil, fmt.Errorf("altosim: payment request failed: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return resp.StatusCode, nil, fmt.Errorf("altosim: failed to read response: %w", err)
	}
	return resp.StatusCode, respBody, nil
}
