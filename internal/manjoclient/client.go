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
	start := time.Now()
	token, expiresIn, statusCode, maskedBody, err := c.requestAccessToken(ctx)
	if c.cfg.OnAccessToken != nil {
		c.cfg.OnAccessToken(ctx, AccessTokenCall{
			StatusCode:   statusCode,
			ResponseBody: maskedBody,
			Duration:     time.Since(start),
			Err:          err,
		})
	}
	return token, expiresIn, err
}

// requestAccessToken returns the HTTP status (0 if none) and the response
// body with the access token masked, alongside the usual results.
func (c *Client) requestAccessToken(ctx context.Context) (string, time.Duration, int, []byte, error) {
	timestamp := NowJakarta()
	signature, err := SignAccessToken(c.cfg.PrivateKeyPEM, c.cfg.ClientKey, timestamp)
	if err != nil {
		return "", 0, 0, nil, err
	}

	body := []byte(`{"grantType":"client_credentials"}`)

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.cfg.BaseURL+"/v1.0/access-token/b2b", bytes.NewReader(body))
	if err != nil {
		return "", 0, 0, nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-TIMESTAMP", timestamp)
	req.Header.Set("X-CLIENT-KEY", c.cfg.ClientKey)
	req.Header.Set("X-SIGNATURE", signature)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return "", 0, 0, nil, fmt.Errorf("manjoclient: access token request failed: %w", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", 0, resp.StatusCode, nil, fmt.Errorf("manjoclient: failed to read access token response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return "", 0, resp.StatusCode, respBody, &APIError{StatusCode: resp.StatusCode, Body: string(respBody)}
	}

	var tokenResp AccessTokenResponse
	if err := json.Unmarshal(respBody, &tokenResp); err != nil {
		// Unparseable 200 body may still hold a token; don't pass it on.
		return "", 0, resp.StatusCode, nil, fmt.Errorf("manjoclient: failed to parse access token response: %w", err)
	}

	masked := tokenResp
	masked.AccessToken = "***"
	maskedBody, _ := json.Marshal(masked)

	var expiresInSeconds int
	if _, err := fmt.Sscanf(tokenResp.ExpiresIn, "%d", &expiresInSeconds); err != nil {
		return "", 0, resp.StatusCode, maskedBody, fmt.Errorf("manjoclient: invalid expiresIn %q: %w", tokenResp.ExpiresIn, err)
	}

	return tokenResp.AccessToken, time.Duration(expiresInSeconds) * time.Second, resp.StatusCode, maskedBody, nil
}

// GenerateQR calls POST /v1.0/qr/qr-mpm-generate, obtaining a valid access
// token from the cache automatically. Returns the X-EXTERNAL-ID that was
// used for this attempt (callers persist it for tracing — schema.md
// transactions.external_id) alongside the response.
func (c *Client) GenerateQR(ctx context.Context, req GenerateQRRequest) (*GenerateQRResponse, string, error) {
	body, err := json.Marshal(req)
	if err != nil {
		return nil, "", fmt.Errorf("manjoclient: failed to marshal generate QR request: %w", err)
	}

	respBody, externalID, err := c.postSigned(ctx, "/v1.0/qr/qr-mpm-generate", body, c.cfg.PartnerID)
	if err != nil {
		return nil, externalID, err
	}

	var qrResp GenerateQRResponse
	if err := json.Unmarshal(respBody, &qrResp); err != nil {
		return nil, externalID, fmt.Errorf("manjoclient: failed to parse generate QR response: %w", err)
	}
	return &qrResp, externalID, nil
}

// QueryPayment calls POST /v1.0/qr/qr-mpm-query in the format of the working BI SNAP UAT
// collection: no X-CLIENT-KEY header, X-PARTNER-ID = client key, serviceCode "47".
func (c *Client) QueryPayment(ctx context.Context, p QueryPaymentParams) (*QueryPaymentResponse, error) {
	body, err := json.Marshal(queryPaymentRequest{
		OriginalReferenceNo:        p.OriginalReferenceNo,
		OriginalPartnerReferenceNo: p.OriginalPartnerReferenceNo,
		OriginalExternalID:         p.OriginalExternalID,
		ServiceCode:                QueryServiceCode,
		MerchantID:                 p.MerchantID,
		AdditionalInfo:             queryAdditionalInfo{Currency: "IDR"},
	})
	if err != nil {
		return nil, fmt.Errorf("manjoclient: failed to marshal query payment request: %w", err)
	}

	respBody, _, err := c.postSigned(ctx, "/v1.0/qr/qr-mpm-query", body, c.cfg.ClientKey)
	if err != nil {
		return nil, err
	}

	var queryResp QueryPaymentResponse
	if err := json.Unmarshal(respBody, &queryResp); err != nil {
		return nil, fmt.Errorf("manjoclient: failed to parse query payment response: %w", err)
	}
	return &queryResp, nil
}

// postSigned POSTs body to path with the HMAC-signed headers shared by the transactional
// endpoints and returns the raw 200 body plus the X-EXTERNAL-ID used. Non-200 answers
// become *APIError. partnerID differs per endpoint: generate sends the merchant ID,
// query sends the client key.
func (c *Client) postSigned(ctx context.Context, path string, body []byte, partnerID string) ([]byte, string, error) {
	token, err := c.tokenManager.Get(ctx)
	if err != nil {
		return nil, "", fmt.Errorf("manjoclient: failed to obtain access token: %w", err)
	}

	externalID, err := generateExternalID(35)
	if err != nil {
		return nil, "", err
	}

	timestamp := NowJakarta()
	signature, err := SignHMAC(c.cfg.ClientSecret, http.MethodPost, path, token, body, timestamp)
	if err != nil {
		return nil, "", err
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.cfg.BaseURL+path, bytes.NewReader(body))
	if err != nil {
		return nil, "", err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+token)
	req.Header.Set("X-TIMESTAMP", timestamp)
	req.Header.Set("X-SIGNATURE", signature)
	req.Header.Set("X-PARTNER-ID", partnerID)
	req.Header.Set("X-EXTERNAL-ID", externalID)
	req.Header.Set("CHANNEL-ID", c.cfg.ChannelID)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, externalID, fmt.Errorf("manjoclient: POST %s failed: %w", path, err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, externalID, fmt.Errorf("manjoclient: failed to read %s response: %w", path, err)
	}
	if resp.StatusCode != http.StatusOK {
		return nil, externalID, &APIError{StatusCode: resp.StatusCode, Body: string(respBody)}
	}
	return respBody, externalID, nil
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
