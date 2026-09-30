package transaction

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgtype"

	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/manjoclient"
	"service-payment-bridge/internal/resolver"
)

// deadlineGrace: a transaction still QR_GENERATED this long after expire_at is expired
// locally, so a query that keeps failing cannot keep it polled forever.
const deadlineGrace = 2 * time.Minute

// expiredResponseCode is qr-mpm-query's answer (with HTTP 403) once the QR has expired.
const expiredResponseCode = "4035100"

// PaymentCheckResult is the outcome of one CheckPayment call.
type PaymentCheckResult struct {
	// Transitioned is true only for the caller whose UPDATE moved the transaction out of
	// QR_GENERATED — at most one caller per transaction, even across instances.
	Transitioned bool
	// Transaction is the updated row when Transitioned, otherwise the input row.
	Transaction sqlc.Transaction
	// ExpiredByDeadline marks an EXPIRED transition made by the deadline safety net
	// rather than by Manjo. Only meaningful when Transitioned.
	ExpiredByDeadline bool
	// QueryErr is set when the query failed or Manjo's answer was unusable; the
	// transaction stays QR_GENERATED and is retried on the next poll.
	QueryErr error
	// ManjoAmount is amount.value of a successful query in Rupiah, 0 when unknown.
	ManjoAmount int64
}

// CheckPayment asks Manjo for tx's payment status and moves tx out of QR_GENERATED when
// the answer is final or the deadline has passed. Only database failures are returned
// as an error; Manjo or configuration problems end up in PaymentCheckResult.QueryErr.
func (s *Service) CheckPayment(ctx context.Context, tx sqlc.Transaction) (*PaymentCheckResult, error) {
	result := &PaymentCheckResult{Transaction: tx}

	outcome := s.queryPayment(ctx, tx)
	result.QueryErr = outcome.err
	result.ManjoAmount = outcome.amount

	if outcome.status == "" && s.now().After(expiryDeadline(tx)) {
		outcome = queryOutcome{status: sqlc.TransactionStatusEXPIRED}
		result.ExpiredByDeadline = true
	}
	if outcome.status == "" {
		return result, nil
	}

	row, err := s.q.TransitionFromQRGenerated(ctx, sqlc.TransitionFromQRGeneratedParams{
		Status:          outcome.status,
		ManjoStatusCode: pgtype.Text{String: outcome.code, Valid: outcome.code != ""},
		PaidAt:          pgtype.Timestamptz{Time: outcome.paidAt, Valid: !outcome.paidAt.IsZero()},
		TransactionID:   tx.TransactionID,
	})
	if errors.Is(err, pgx.ErrNoRows) {
		return result, nil // already moved by another poller or instance
	}
	if err != nil {
		return nil, fmt.Errorf("transaction: failed to move %s to %s: %w", tx.TransactionID, outcome.status, err)
	}

	result.Transitioned = true
	result.Transaction = row
	return result, nil
}

func expiryDeadline(tx sqlc.Transaction) time.Time {
	expireAt := tx.CreatedAt.Time.Add(fallbackQRValidity)
	if tx.ExpireAt.Valid {
		expireAt = tx.ExpireAt.Time
	}
	return expireAt.Add(deadlineGrace)
}

// queryPayment resolves tx's merchant credentials and calls qr-mpm-query. Every outcome
// except "still pending" is recorded in manjo_api_logs.
func (s *Service) queryPayment(ctx context.Context, tx sqlc.Transaction) queryOutcome {
	device, err := resolver.New(s.q).ResolveDevice(ctx, tx.DeviceID)
	if err != nil {
		return queryOutcome{err: err}
	}
	client, err := s.registry.GetOrCreate(device.MerchantID, func() (manjoclient.Config, error) {
		return s.buildManjoConfig(ctx, *device)
	})
	if err != nil {
		return queryOutcome{err: fmt.Errorf("transaction: failed to build manjo config: %w", err)}
	}

	params := manjoclient.QueryPaymentParams{
		OriginalReferenceNo:        tx.ReferenceNo.String,
		OriginalPartnerReferenceNo: tx.TransactionID,
		OriginalExternalID:         tx.ExternalID.String,
		MerchantID:                 device.ManjoMerchantID,
	}

	start := s.now()
	resp, err := client.QueryPayment(ctx, params)
	if isUnauthorized(err) {
		client.InvalidateToken()
	}

	outcome := mapQueryResult(resp, err, s.now())
	if !outcome.pending() {
		s.logQueryCall(ctx, tx.TransactionID, params, resp, err, s.now().Sub(start))
	}
	return outcome
}

// queryOutcome is what one qr-mpm-query answer means for a QR_GENERATED transaction.
type queryOutcome struct {
	status sqlc.TransactionStatus // "" = no transition
	code   string                 // manjo_status_code to store; "" leaves it unchanged
	paidAt time.Time              // zero leaves paid_at unchanged
	amount int64                  // amount.value in Rupiah, 0 when unknown
	err    error                  // failed or unusable answer; retry
}

// pending reports Manjo's "not paid yet" — the only outcome neither logged nor acted on.
func (o queryOutcome) pending() bool {
	return o.status == "" && o.err == nil
}

// mapQueryResult interprets a qr-mpm-query result with the Query status scheme
// (manjo-api-docs.md 5.10). Never reuse it for Payment Notification codes: the same code
// means something else there ("03" is Pending here but Paid in notifications).
func mapQueryResult(resp *manjoclient.QueryPaymentResponse, err error, now time.Time) queryOutcome {
	if err != nil {
		var apiErr *manjoclient.APIError
		if errors.As(err, &apiErr) && apiErr.StatusCode == http.StatusForbidden && apiErr.ResponseCode() == expiredResponseCode {
			return queryOutcome{status: sqlc.TransactionStatusEXPIRED}
		}
		return queryOutcome{err: err}
	}

	switch code := resp.LatestTransactionStatus; code {
	case "00":
		return queryOutcome{status: sqlc.TransactionStatusPAID, code: code, paidAt: parsePaidTime(resp.PaidTime, now), amount: parseRupiah(resp.Amount.Value)}
	case "01", "02", "03":
		return queryOutcome{}
	case "04":
		return queryOutcome{status: sqlc.TransactionStatusREFUNDED, code: code}
	case "05":
		return queryOutcome{status: sqlc.TransactionStatusCANCELLED, code: code}
	case "06":
		return queryOutcome{status: sqlc.TransactionStatusFAILED, code: code}
	case "07":
		return queryOutcome{err: errors.New("transaction: manjo reports the transaction as not found (07)")}
	default:
		return queryOutcome{err: fmt.Errorf("transaction: unknown query status %q", code)}
	}
}

func parsePaidTime(paidTime string, fallback time.Time) time.Time {
	if t, err := time.Parse(time.RFC3339, paidTime); err == nil {
		return t
	}
	return fallback
}

// parseRupiah converts Manjo's "50000.00" to 50000, or 0 when unparseable.
func parseRupiah(value string) int64 {
	whole, _, _ := strings.Cut(value, ".")
	n, err := strconv.ParseInt(whole, 10, 64)
	if err != nil {
		return 0
	}
	return n
}

func (s *Service) logQueryCall(ctx context.Context, transactionID string, params manjoclient.QueryPaymentParams, resp *manjoclient.QueryPaymentResponse, callErr error, duration time.Duration) {
	reqBody, _ := json.Marshal(params)

	var respBody []byte
	httpStatus := pgtype.Int4{}
	var apiErr *manjoclient.APIError
	switch {
	case callErr == nil:
		respBody, _ = json.Marshal(resp)
		httpStatus = pgtype.Int4{Int32: http.StatusOK, Valid: true}
	case errors.As(callErr, &apiErr):
		respBody = []byte(apiErr.Body)
		httpStatus = pgtype.Int4{Int32: int32(apiErr.StatusCode), Valid: true}
	}
	if !json.Valid(respBody) {
		respBody = nil
		if callErr != nil {
			respBody, _ = json.Marshal(map[string]string{"error": callErr.Error()})
		}
	}

	// Logging failure must not break payment detection (same trade-off as logManjoAPICall).
	_, _ = s.q.LogManjoAPICall(ctx, sqlc.LogManjoAPICallParams{
		Direction:     sqlc.ManjoApiDirectionOUTBOUND,
		Operation:     sqlc.ManjoApiOperationQUERYPAYMENT,
		Endpoint:      "/v1.0/qr/qr-mpm-query",
		HttpStatus:    httpStatus,
		RequestBody:   reqBody,
		ResponseBody:  respBody,
		TransactionID: pgtype.Text{String: transactionID, Valid: true},
		DurationMs:    pgtype.Int4{Int32: int32(duration.Milliseconds()), Valid: true},
	})
}
