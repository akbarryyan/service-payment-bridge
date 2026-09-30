package altosim

import (
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"strconv"
	"strings"
)

// issuerNNS is the simulated paying bank, as in the collection.
const issuerNNS = "93600821"

// PaymentRequest is the qr-payment-credit body.
type PaymentRequest struct {
	Command string      `json:"command"`
	Data    PaymentData `json:"data"`
}

type PaymentData struct {
	DateTime                          string   `json:"date_time"`
	CustomerReferenceNumber           string   `json:"customer_reference_number"`
	AuthorizationID                   string   `json:"authorization_id"`
	CurrencyCode                      string   `json:"currency_code"`
	Amount                            int64    `json:"amount"`
	Fee                               int64    `json:"fee"`
	IssuerNNS                         string   `json:"issuer_nns"`
	AcquirerNNS                       string   `json:"acquirer_nns"`
	NationalMID                       string   `json:"national_mid"`
	AdditionalData                    string   `json:"additional_data"`
	TerminalLabel                     string   `json:"terminal_label"`
	ForwardingCustomerReferenceNumber string   `json:"forwarding_customer_reference_number"`
	Merchant                          Merchant `json:"merchant"`
	Customer                          Customer `json:"customer"`
}

type Merchant struct {
	PAN         string `json:"pan"`
	ID          string `json:"id"`
	Criteria    string `json:"criteria"`
	Name        string `json:"name"`
	City        string `json:"city"`
	MCC         string `json:"mcc"`
	PostalCode  string `json:"postal_code"`
	CountryCode string `json:"country_code"`
}

type Customer struct {
	PAN         string `json:"pan"`
	Name        string `json:"name"`
	AccountType string `json:"account_type"`
}

// Identifiers are the per-payment random references Alto expects.
type Identifiers struct {
	CustomerReferenceNumber   string
	ForwardingReferenceNumber string
	AuthorizationID           string
}

// NewIdentifiers returns fresh random identifiers in the collection's formats.
func NewIdentifiers() (Identifiers, error) {
	b := make([]byte, 15)
	if _, err := rand.Read(b); err != nil {
		return Identifiers{}, fmt.Errorf("altosim: failed to generate identifiers: %w", err)
	}
	h := hex.EncodeToString(b) // 30 hex characters
	return Identifiers{
		CustomerReferenceNumber:   "ALTO-API-NMS-" + h[:12],
		ForwardingReferenceNumber: h[12:24],
		AuthorizationID:           strings.ToUpper(h[24:30]),
	}, nil
}

// BuildPaymentRequest pays qrContent in full. Merchant fields come from the QR itself —
// never from the collection's hard-coded Ayoborong example.
func BuildPaymentRequest(qrContent, timestamp string, ids Identifiers) (PaymentRequest, error) {
	top, err := ParseEMV(qrContent)
	if err != nil {
		return PaymentRequest{}, err
	}
	account, err := ParseEMV(top["26"])
	if err != nil {
		return PaymentRequest{}, fmt.Errorf("altosim: merchant account (tag 26): %w", err)
	}
	additional, err := ParseEMV(top["62"])
	if err != nil {
		return PaymentRequest{}, fmt.Errorf("altosim: additional data (tag 62): %w", err)
	}

	pan, merchantID := account["01"], account["02"]
	if len(pan) < 8 || merchantID == "" {
		return PaymentRequest{}, fmt.Errorf("altosim: QR lacks merchant PAN/ID (tag 26.01/26.02)")
	}
	if additional["05"] == "" {
		return PaymentRequest{}, fmt.Errorf("altosim: QR lacks a reference label (tag 62.05)")
	}
	amount, err := parseAmount(top["54"])
	if err != nil {
		return PaymentRequest{}, err
	}

	return PaymentRequest{
		Command: "qr-payment-credit",
		Data: PaymentData{
			DateTime:                          timestamp,
			CustomerReferenceNumber:           ids.CustomerReferenceNumber,
			AuthorizationID:                   ids.AuthorizationID,
			CurrencyCode:                      "IDR",
			Amount:                            amount,
			Fee:                               0,
			IssuerNNS:                         issuerNNS,
			AcquirerNNS:                       pan[:8],
			NationalMID:                       merchantID,
			AdditionalData:                    top["62"],
			TerminalLabel:                     additional["07"],
			ForwardingCustomerReferenceNumber: ids.ForwardingReferenceNumber,
			Merchant: Merchant{
				PAN: pan, ID: merchantID, Criteria: account["03"], Name: top["59"],
				City: top["60"], MCC: top["52"], PostalCode: top["61"], CountryCode: top["58"],
			},
			Customer: Customer{PAN: pan, Name: "Tes", AccountType: "UNSPECIFIED"},
		},
	}, nil
}

// ReferenceLabel returns the QR's reference label (tag 62.05), which is Manjo's referenceNo.
func (r PaymentRequest) ReferenceLabel() string {
	additional, err := ParseEMV(r.Data.AdditionalData)
	if err != nil {
		return ""
	}
	return additional["05"]
}

// parseAmount converts tag 54 ("50000.00") to whole Rupiah.
func parseAmount(value string) (int64, error) {
	whole, _, _ := strings.Cut(value, ".")
	n, err := strconv.ParseInt(whole, 10, 64)
	if err != nil || n <= 0 {
		return 0, fmt.Errorf("altosim: invalid transaction amount (tag 54) %q", value)
	}
	return n, nil
}
