package manjoclient

import (
	"fmt"
	"net/http"
)

type Config struct {
	BaseURL       string
	ClientKey     string // X-CLIENT-KEY / mcCodePayId
	PrivateKeyPEM []byte // PKCS8 PEM, termasuk header BEGIN/END
	ClientSecret  string // vkey, dipakai HMAC-SHA512
	PartnerID     string // X-PARTNER-ID
	ChannelID     string // CHANNEL-ID
	HTTPClient    *http.Client
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
