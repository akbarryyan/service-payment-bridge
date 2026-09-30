package manjoclient

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

type Config struct {
	BaseURL       string
	ClientKey     string // X-CLIENT-KEY / mcCodePayId
	PrivateKeyPEM []byte // PKCS8 PEM, termasuk header BEGIN/END
	ClientSecret  string // vkey, dipakai HMAC-SHA512
	PartnerID     string // X-PARTNER-ID
	ChannelID     string // CHANNEL-ID
	HTTPClient    *http.Client

	// OnAccessToken, if set, is called after every access-token request
	// (success or failure), e.g. to persist it to manjo_api_logs.
	OnAccessToken func(ctx context.Context, call AccessTokenCall)
}

// AccessTokenCall describes one POST /v1.0/access-token/b2b attempt.
// ResponseBody never contains the access token itself (it is masked).
type AccessTokenCall struct {
	StatusCode   int // 0 when no HTTP response was received
	ResponseBody []byte
	Duration     time.Duration
	Err          error
}

type Amount struct {
	Value    string `json:"value"`
	Currency string `json:"currency"`
}

type GenerateQRAdditionalInfo struct {
	PaymentID     string `json:"paymentId"`
	DynamicAmount string `json:"dynamicAmount"`
	ProdDesc      string `json:"prodDesc"`
}

type GenerateQRRequest struct {
	PartnerReferenceNo string                   `json:"partnerReferenceNo"`
	Amount             Amount                   `json:"amount"`
	MerchantID         string                   `json:"merchantId"`
	SubMerchantID      string                   `json:"subMerchantId,omitempty"`
	StoreID            string                   `json:"storeId,omitempty"`
	TerminalID         string                   `json:"terminalId,omitempty"`
	ValidityPeriod     string                   `json:"validityPeriod"`
	AdditionalInfo     GenerateQRAdditionalInfo `json:"additionalInfo"`
}

type GenerateQRResponseAdditionalInfo struct {
	PaymentID      string `json:"paymentId"`
	MerchantCode   string `json:"merchantCode"`
	ExpiryDuration string `json:"expiryDuration"`
	ExpireDate     string `json:"expireDate"`
	Amount         string `json:"amount"`
}

type GenerateQRResponse struct {
	ResponseCode       string                           `json:"responseCode"`
	ResponseMessage    string                           `json:"responseMessage"`
	ReferenceNo        string                           `json:"referenceNo"`
	PartnerReferenceNo string                           `json:"partnerReferenceNo"`
	QRContent          string                           `json:"qrContent"`
	MerchantName       string                           `json:"merchantName"`
	StoreID            string                           `json:"storeId"`
	TerminalID         string                           `json:"terminalId"`
	AdditionalInfo     GenerateQRResponseAdditionalInfo `json:"additionalInfo"`
}

type AccessTokenResponse struct {
	ResponseCode    string `json:"responseCode"`
	ResponseMessage string `json:"responseMessage"`
	TokenType       string `json:"tokenType"`
	AccessToken     string `json:"accessToken"`
	ExpiresIn       string `json:"expiresIn"`
}

// APIError represents a non-2xx response from Manjo, carrying the HTTP
// status code so callers can branch on it (401 -> refresh+retry, 409 ->
// check status before retry, etc. per architecture.md Section 13).
type APIError struct {
	StatusCode int
	Body       string
}

func (e *APIError) Error() string {
	return fmt.Sprintf("manjoclient: manjo returned HTTP %d: %s", e.StatusCode, e.Body)
}

// ResponseCode extracts Manjo's 7-digit responseCode from the error body, or "" when the
// body is not JSON (e.g. "4035100" = QR expired, returned by qr-mpm-query with HTTP 403).
func (e *APIError) ResponseCode() string {
	var body struct {
		ResponseCode string `json:"responseCode"`
	}
	if json.Unmarshal([]byte(e.Body), &body) != nil {
		return ""
	}
	return body.ResponseCode
}

// QueryServiceCode is the body serviceCode sent by the BI SNAP UAT collection. The docs
// say "99"; the collection works in UAT and is confirmed for production, so it wins.
const QueryServiceCode = "47"

// QueryPaymentParams identifies the transaction to look up with qr-mpm-query.
type QueryPaymentParams struct {
	OriginalReferenceNo        string `json:"originalReferenceNo"`        // referenceNo from qr-mpm-generate
	OriginalPartnerReferenceNo string `json:"originalPartnerReferenceNo"` // our transaction_id
	OriginalExternalID         string `json:"originalExternalId"`         // X-EXTERNAL-ID used at generate time
	MerchantID                 string `json:"merchantId"`
}

// queryPaymentRequest's field order must match the collection exactly (not just its
// keys/values): Manjo re-serializes the body before verifying X-SIGNATURE, so a
// differently-ordered-but-equivalent JSON object fails HMAC verification (HTTP 401
// 4015100 "Unauthorized Signature"), even though the plain field values are correct.
type queryPaymentRequest struct {
	OriginalReferenceNo        string              `json:"originalReferenceNo"`
	OriginalPartnerReferenceNo string              `json:"originalPartnerReferenceNo"`
	OriginalExternalID         string              `json:"originalExternalId"`
	ServiceCode                string              `json:"serviceCode"`
	MerchantID                 string              `json:"merchantId"`
	AdditionalInfo             queryAdditionalInfo `json:"additionalInfo"`
}

type queryAdditionalInfo struct {
	Currency string `json:"currency"`
}

// QueryPaymentResponse is a 200 answer from qr-mpm-query. LatestTransactionStatus uses
// the Query status scheme (manjo-api-docs.md 5.10), NOT the Payment Notification one.
type QueryPaymentResponse struct {
	ResponseCode               string `json:"responseCode"`
	ResponseMessage            string `json:"responseMessage"`
	OriginalReferenceNo        string `json:"originalReferenceNo"`
	OriginalPartnerReferenceNo string `json:"originalPartnerReferenceNo"`
	LatestTransactionStatus    string `json:"latestTransactionStatus"`
	TransactionStatusDesc      string `json:"transactionStatusDesc"`
	PaidTime                   string `json:"paidTime"`
	Amount                     Amount `json:"amount"`
}
