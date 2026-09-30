package transaction

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"time"

	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgtype"

	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/resolver"
	"service-payment-bridge/internal/secrets"
)

const (
	maxCreateRetries    = 5
	maxTransientRetries = 3
)

// DefaultPollInterval is how long after QR generation, and between checks, the payment
// poller asks Manjo for a transaction's status (config PAYMENT_POLL_INTERVAL).
const DefaultPollInterval = 3 * time.Second

type Service struct {
	q            *sqlc.Queries
	registry     *manjoclient.Registry
	secrets      secrets.Provider
	baseURL      string // "" berarti pakai default manjoclient.Client
	now          func() time.Time
	pollInterval time.Duration
}

func NewService(q *sqlc.Queries, registry *manjoclient.Registry, secretProvider secrets.Provider) *Service {
	return &Service{q: q, registry: registry, secrets: secretProvider, now: time.Now, pollInterval: DefaultPollInterval}
}

// SetPollInterval overrides DefaultPollInterval.
func (s *Service) SetPollInterval(d time.Duration) {
	s.pollInterval = d
}

// NewServiceWithBaseURL is NewService but overrides the Manjo base URL for
// every merchant Config built by this Service — used by tests to point at
// an httptest mock server instead of the real Manjo host.
func NewServiceWithBaseURL(q *sqlc.Queries, registry *manjoclient.Registry, secretProvider secrets.Provider, baseURL string) *Service {
	s := NewService(q, registry, secretProvider)
	s.baseURL = baseURL
	return s
}

type GenerateQRResult struct {
	TransactionID string
	Status        string // "SUCCESS" | "FAILED"
	QRISPayload   string
	ExpireAt      time.Time
	ErrorCode     string // populated when Status == "FAILED"
	Cause         error  // underlying error when Status == "FAILED", for logging only
}

func (s *Service) GenerateQR(ctx context.Context, device resolver.ResolvedDevice, amount int64) (*GenerateQRResult, error) {
	transactionID, err := s.createTransaction(ctx, device, amount)
	if err != nil {
		return nil, err
	}

	client, err := s.registry.GetOrCreate(device.MerchantID, func() (manjoclient.Config, error) {
		return s.buildManjoConfig(ctx, device)
	})
	if err != nil {
		_ = s.markFailed(ctx, transactionID)
		return &GenerateQRResult{TransactionID: transactionID, Status: "FAILED", ErrorCode: "CONFIG_ERROR", Cause: err}, nil
	}

	req := buildGenerateQRRequest(device, transactionID, amount)

	start := s.now()
	resp, externalID, errCode, callErr := s.callGenerateQRWithRetry(ctx, client, req)
	s.logManjoAPICall(ctx, transactionID, req, resp, errCode, s.now().Sub(start))

	if errCode != "" {
		_ = s.markFailed(ctx, transactionID)
		return &GenerateQRResult{TransactionID: transactionID, Status: "FAILED", ErrorCode: errCode, Cause: callErr}, nil
	}

	expireAt, err := parseExpireDate(resp.AdditionalInfo.ExpireDate, s.now())
	if err != nil {
		expireAt = s.now().Add(time.Hour)
	}

	if err := s.markQRGenerated(ctx, transactionID, externalID, resp, expireAt); err != nil {
		return nil, fmt.Errorf("transaction: failed to persist QR_GENERATED: %w", err)
	}

	return &GenerateQRResult{
		TransactionID: transactionID,
		Status:        "SUCCESS",
		QRISPayload:   resp.QRContent,
		ExpireAt:      expireAt,
	}, nil
}

// logManjoAPICall records one row per GenerateQR() call (summarizing the
// whole retry sequence, not per HTTP attempt — full per-attempt tracing is
// Fase 5 Hardening scope). Request/response bodies never contain secrets
// (signature/token live in headers, not JSON body), so no masking is
// needed here.
func (s *Service) logManjoAPICall(ctx context.Context, transactionID string, req manjoclient.GenerateQRRequest, resp *manjoclient.GenerateQRResponse, errCode string, duration time.Duration) {
	reqBody, _ := json.Marshal(req)

	var respBody []byte
	httpStatus := pgtype.Int4{}
	if errCode == "" && resp != nil {
		respBody, _ = json.Marshal(resp)
		httpStatus = pgtype.Int4{Int32: 200, Valid: true}
	} else {
		respBody, _ = json.Marshal(map[string]string{"error_code": errCode})
	}

	_, err := s.q.LogManjoAPICall(ctx, sqlc.LogManjoAPICallParams{
		Direction:     sqlc.ManjoApiDirectionOUTBOUND,
		Operation:     sqlc.ManjoApiOperationGENERATEQR,
		Endpoint:      "/v1.0/qr/qr-mpm-generate",
		HttpStatus:    httpStatus,
		RequestBody:   reqBody,
		ResponseBody:  respBody,
		TransactionID: pgtype.Text{String: transactionID, Valid: true},
		DurationMs:    pgtype.Int4{Int32: int32(duration.Milliseconds()), Valid: true},
	})
	if err != nil {
		// Logging failure must not break the Generate QR flow itself. Service
		// has no logger reference in this phase (kept minimal) — the failure
		// is silently swallowed; acceptable trade-off for Fase 3 scope.
		_ = err
	}
}

func (s *Service) createTransaction(ctx context.Context, device resolver.ResolvedDevice, amount int64) (string, error) {
	for attempt := 0; attempt < maxCreateRetries; attempt++ {
		id, err := generateTransactionID(s.now())
		if err != nil {
			return "", err
		}

		_, err = s.q.CreateTransaction(ctx, sqlc.CreateTransactionParams{
			TransactionID: id,
			MerchantID:    device.MerchantID,
			DeviceID:      device.DeviceID,
			Amount:        amount,
		})
		if err == nil {
			return id, nil
		}
		if !isUniqueViolation(err) {
			return "", fmt.Errorf("transaction: failed to create transaction: %w", err)
		}
	}
	return "", fmt.Errorf("transaction: exhausted %d attempts to generate unique transaction_id", maxCreateRetries)
}

func isUniqueViolation(err error) bool {
	var pgErr *pgconn.PgError
	return errors.As(err, &pgErr) && pgErr.Code == "23505"
}

func (s *Service) buildManjoConfig(ctx context.Context, device resolver.ResolvedDevice) (manjoclient.Config, error) {
	privateKeyRaw, err := s.secrets.Resolve(ctx, device.ManjoPrivateKeyRef)
	if err != nil {
		return manjoclient.Config{}, fmt.Errorf("transaction: failed to resolve private key: %w", err)
	}
	clientSecret, err := s.secrets.Resolve(ctx, device.ManjoClientSecretRef)
	if err != nil {
		return manjoclient.Config{}, fmt.Errorf("transaction: failed to resolve client secret: %w", err)
	}
	privateKeyPEM, err := manjoclient.WrapPKCS8PEM(privateKeyRaw)
	if err != nil {
		return manjoclient.Config{}, err
	}

	return manjoclient.Config{
		BaseURL:       s.baseURL,
		ClientKey:     device.ManjoClientID,
		PrivateKeyPEM: privateKeyPEM,
		ClientSecret:  clientSecret,
		PartnerID:     device.ManjoMerchantID,
		ChannelID:     device.ManjoChannelID,
		OnAccessToken: s.logAccessTokenCall,
	}, nil
}

// logAccessTokenCall records every access-token attempt (process-flow.md
// Flow 2 steps 3-4). transaction_id stays NULL: a cached token serves many
// transactions, so it is not tied to the one that triggered the fetch.
func (s *Service) logAccessTokenCall(ctx context.Context, call manjoclient.AccessTokenCall) {
	respBody := call.ResponseBody
	if !json.Valid(respBody) {
		respBody = nil
		if call.Err != nil {
			respBody, _ = json.Marshal(map[string]string{"error": call.Err.Error()})
		}
	}

	httpStatus := pgtype.Int4{}
	if call.StatusCode != 0 {
		httpStatus = pgtype.Int4{Int32: int32(call.StatusCode), Valid: true}
	}

	// Logging failure must not break the Generate QR flow (same trade-off
	// as logManjoAPICall).
	_, _ = s.q.LogManjoAPICall(ctx, sqlc.LogManjoAPICallParams{
		Direction:    sqlc.ManjoApiDirectionOUTBOUND,
		Operation:    sqlc.ManjoApiOperationACCESSTOKEN,
		Endpoint:     "/v1.0/access-token/b2b",
		HttpStatus:   httpStatus,
		RequestBody:  []byte(`{"grantType":"client_credentials"}`),
		ResponseBody: respBody,
		DurationMs:   pgtype.Int4{Int32: int32(call.Duration.Milliseconds()), Valid: true},
	})
}

func buildGenerateQRRequest(device resolver.ResolvedDevice, transactionID string, amount int64) manjoclient.GenerateQRRequest {
	req := manjoclient.GenerateQRRequest{
		PartnerReferenceNo: transactionID,
		Amount:             manjoclient.Amount{Value: fmt.Sprintf("%d.00", amount), Currency: "IDR"},
		MerchantID:         device.ManjoMerchantID,
		ValidityPeriod:     "3600",
		AdditionalInfo: manjoclient.GenerateQRAdditionalInfo{
			PaymentID:     "99",
			DynamicAmount: "N",
			ProdDesc:      "Q161 Payment",
		},
	}
	if device.ManjoSubMerchantID != nil {
		req.SubMerchantID = *device.ManjoSubMerchantID
	}
	if device.ManjoStoreID != nil {
		req.StoreID = *device.ManjoStoreID
	}
	if device.ManjoTerminalID != nil {
		req.TerminalID = *device.ManjoTerminalID
	}
	return req
}

// callGenerateQRWithRetry implements architecture.md Section 13.1: 401 ->
// invalidate cached token, retry once; 400/404/409 -> no retry, fail
// immediately; timeout/5xx/network error -> retry up to maxTransientRetries
// total attempts. The last error is returned alongside the error code so
// callers can log what actually went wrong.
func (s *Service) callGenerateQRWithRetry(ctx context.Context, client *manjoclient.Client, req manjoclient.GenerateQRRequest) (*manjoclient.GenerateQRResponse, string, string, error) {
	resp, externalID, err := client.GenerateQR(ctx, req)
	if err == nil {
		return resp, externalID, "", nil
	}

	if isUnauthorized(err) {
		client.InvalidateToken()
		resp, externalID, err = client.GenerateQR(ctx, req)
		if err == nil {
			return resp, externalID, "", nil
		}
	}

	if code := nonRetryableCode(err); code != "" {
		return nil, externalID, code, err
	}

	for attempt := 1; attempt < maxTransientRetries; attempt++ {
		resp, externalID, err = client.GenerateQR(ctx, req)
		if err == nil {
			return resp, externalID, "", nil
		}
		if code := nonRetryableCode(err); code != "" {
			return nil, externalID, code, err
		}
	}

	return nil, externalID, "MANJO_TIMEOUT", err
}

func isUnauthorized(err error) bool {
	var apiErr *manjoclient.APIError
	return errors.As(err, &apiErr) && apiErr.StatusCode == http.StatusUnauthorized
}

// nonRetryableCode returns a non-empty error code if err should NOT be
// retried (400/401/404/409), or "" if it is transient (timeout/5xx/network)
// and the caller should retry.
func nonRetryableCode(err error) string {
	var apiErr *manjoclient.APIError
	if !errors.As(err, &apiErr) {
		return ""
	}
	switch apiErr.StatusCode {
	case http.StatusBadRequest:
		return "INVALID_REQUEST"
	case http.StatusUnauthorized:
		return "UNAUTHORIZED"
	case http.StatusNotFound:
		return "INVALID_MERCHANT"
	case http.StatusConflict:
		return "CONFLICT"
	}
	if apiErr.StatusCode >= 500 {
		return ""
	}
	return "MANJO_ERROR"
}

func parseExpireDate(expireDate string, fallbackNow time.Time) (time.Time, error) {
	loc := time.FixedZone("WIB", 7*60*60)
	t, err := time.ParseInLocation("20060102150405", expireDate, loc)
	if err != nil {
		return fallbackNow, fmt.Errorf("transaction: failed to parse expireDate %q: %w", expireDate, err)
	}
	return t, nil
}

func (s *Service) markFailed(ctx context.Context, transactionID string) error {
	_, err := s.q.MarkTransactionFailed(ctx, transactionID)
	return err
}

func (s *Service) markQRGenerated(ctx context.Context, transactionID, externalID string, resp *manjoclient.GenerateQRResponse, expireAt time.Time) error {
	_, err := s.q.MarkTransactionQRGenerated(ctx, sqlc.MarkTransactionQRGeneratedParams{
		TransactionID: transactionID,
		QrisPayload:   pgtype.Text{String: resp.QRContent, Valid: true},
		ReferenceNo:   pgtype.Text{String: resp.ReferenceNo, Valid: true},
		ExternalID:    pgtype.Text{String: externalID, Valid: true},
		ExpireAt:      pgtype.Timestamptz{Time: expireAt, Valid: true},
		NextQueryAt:   pgtype.Timestamptz{Time: s.now().Add(s.pollInterval), Valid: true},
	})
	return err
}
