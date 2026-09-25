package manjoclient

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"time"
)

const defaultBaseURL = "https://snapqris.manjo.co.id/api"

type Client struct {
	cfg          Config
	httpClient   *http.Client
	tokenManager *TokenManager
}

func New(cfg Config) *Client {
	if cfg.BaseURL == "" {
		cfg.BaseURL = defaultBaseURL
	}
	if cfg.HTTPClient == nil {
		cfg.HTTPClient = &http.Client{Timeout: 15 * time.Second}
	}

	c := &Client{
		cfg:        cfg,
		httpClient: cfg.HTTPClient,
	}
	c.tokenManager = NewTokenManager(c.fetchAccessToken)
	return c
}

func (c *Client) fetchAccessToken(ctx context.Context) (string, time.Duration, error) {
	return c.AccessToken(ctx)
}

// AccessToken calls POST /v1.0/access-token/b2b directly, bypassing the
// cache. Normally reached indirectly via GenerateQR through TokenManager.
func (c *Client) AccessToken(ctx context.Context) (string, time.Duration, error) {
	timestamp := NowJakarta()
	signature, err := SignAccessToken(c.cfg.PrivateKeyPEM, c.cfg.ClientKey, timestamp)
	if err != nil {
		return "", 0, err
	}

	body := []byte(`{"grantType":"client_credentials"}`)

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.cfg.BaseURL+"/v1.0/access-token/b2b", bytes.NewReader(body))
	if err != nil {
		return "", 0, err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-TIMESTAMP", timestamp)
	req.Header.Set("X-CLIENT-KEY", c.cfg.ClientKey)
	req.Header.Set("X-SIGNATURE", signature)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return "", 0, fmt.Errorf("manjoclient: access token request failed: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", 0, fmt.Errorf("manjoclient: failed to read access token response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return "", 0, &APIError{StatusCode: resp.StatusCode, Body: string(respBody)}
	}

	var tokenResp AccessTokenResponse
	if err := json.Unmarshal(respBody, &tokenResp); err != nil {
		return "", 0, fmt.Errorf("manjoclient: failed to parse access token response: %w", err)
	}

	var expiresInSeconds int
	if _, err := fmt.Sscanf(tokenResp.ExpiresIn, "%d", &expiresInSeconds); err != nil {
		return "", 0, fmt.Errorf("manjoclient: invalid expiresIn %q: %w", tokenResp.ExpiresIn, err)
	}

	return tokenResp.AccessToken, time.Duration(expiresInSeconds) * time.Second, nil
}

// GenerateQR calls POST /v1.0/qr/qr-mpm-generate, obtaining a valid access
// token from the cache automatically. Returns the X-EXTERNAL-ID that was
// used for this attempt (callers persist it for tracing — schema.md
// transactions.external_id) alongside the response.
func (c *Client) GenerateQR(ctx context.Context, req GenerateQRRequest) (*GenerateQRResponse, string, error) {
	token, err := c.tokenManager.Get(ctx)
	if err != nil {
		return nil, "", fmt.Errorf("manjoclient: failed to obtain access token: %w", err)
	}

	externalID, err := generateExternalID(35)
	if err != nil {
		return nil, "", err
	}

	body, err := json.Marshal(req)
	if err != nil {
		return nil, "", fmt.Errorf("manjoclient: failed to marshal generate QR request: %w", err)
	}

	timestamp := NowJakarta()
	const path = "/v1.0/qr/qr-mpm-generate"

	signature, err := SignHMAC(c.cfg.ClientSecret, http.MethodPost, path, token, body, timestamp)
	if err != nil {
		return nil, "", err
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.cfg.BaseURL+path, bytes.NewReader(body))
	if err != nil {
		return nil, "", err
	}
	httpReq.Header.Set("Content-Type", "application/json")
	httpReq.Header.Set("Authorization", "Bearer "+token)
	httpReq.Header.Set("X-TIMESTAMP", timestamp)
	httpReq.Header.Set("X-SIGNATURE", signature)
	httpReq.Header.Set("X-PARTNER-ID", c.cfg.PartnerID)
	httpReq.Header.Set("X-EXTERNAL-ID", externalID)
	httpReq.Header.Set("CHANNEL-ID", c.cfg.ChannelID)

	resp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return nil, externalID, fmt.Errorf("manjoclient: generate QR request failed: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, externalID, fmt.Errorf("manjoclient: failed to read generate QR response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return nil, externalID, &APIError{StatusCode: resp.StatusCode, Body: string(respBody)}
	}

	var qrResp GenerateQRResponse
	if err := json.Unmarshal(respBody, &qrResp); err != nil {
		return nil, externalID, fmt.Errorf("manjoclient: failed to parse generate QR response: %w", err)
	}

	return &qrResp, externalID, nil
}

// InvalidateToken forces the next call through TokenManager to fetch a
// fresh access token — used after a 401 on a call that used a cached token
// (architecture.md Section 13.1: refresh token, retry once).
func (c *Client) InvalidateToken() {
	c.tokenManager.Invalidate()
}

func generateExternalID(n int) (string, error) {
	const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz"
	randomBytes := make([]byte, n)
	if _, err := rand.Read(randomBytes); err != nil {
		return "", fmt.Errorf("manjoclient: failed to generate X-EXTERNAL-ID: %w", err)
	}
	b := make([]byte, n)
	for i, rb := range randomBytes {
		b[i] = alphabet[int(rb)%len(alphabet)]
	}
	return string(b), nil
}
