package altosim

import (
	"encoding/json"
	"reflect"
	"strings"
	"testing"
)

func TestBuildPaymentRequest_FromRealQR(t *testing.T) {
	ids := Identifiers{CustomerReferenceNumber: "ALTO-API-NMS-aaaaaaaaaaaa", ForwardingReferenceNumber: "bbbbbbbbbbbb", AuthorizationID: "CCCCCC"}

	got, err := BuildPaymentRequest(realQR, "2026-09-30 03:00:00.000Z", ids)
	if err != nil {
		t.Fatalf("BuildPaymentRequest() error = %v", err)
	}

	want := PaymentRequest{
		Command: "qr-payment-credit",
		Data: PaymentData{
			DateTime:                          "2026-09-30 03:00:00.000Z",
			CustomerReferenceNumber:           "ALTO-API-NMS-aaaaaaaaaaaa",
			AuthorizationID:                   "CCCCCC",
			CurrencyCode:                      "IDR",
			Amount:                            50000,
			Fee:                               0,
			IssuerNNS:                         "93600821",
			AcquirerNNS:                       "93600858",
			NationalMID:                       "MT58530503",
			AdditionalData:                    "0520A503321013864160FF4107036590812Q161 Payment",
			TerminalLabel:                     "659",
			ForwardingCustomerReferenceNumber: "bbbbbbbbbbbb",
			Merchant: Merchant{
				PAN: "936008580287697876", ID: "MT58530503", Criteria: "UKE", Name: "Pupuk Kalteng",
				City: "JAKARTA BARAT", MCC: "5251", PostalCode: "11550", CountryCode: "ID",
			},
			Customer: Customer{PAN: "936008580287697876", Name: "Tes", AccountType: "UNSPECIFIED"},
		},
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("BuildPaymentRequest()\n  got:  %+v\n  want: %+v", got, want)
	}
	if ref := got.ReferenceLabel(); ref != "A503321013864160FF41" {
		t.Errorf("ReferenceLabel() = %q, want A503321013864160FF41", ref)
	}
}

func TestBuildPaymentRequest_RejectsQRWithoutAmount(t *testing.T) {
	noAmount := strings.Replace(realQR, "540850000.00", "", 1)
	if _, err := BuildPaymentRequest(noAmount, "2026-09-30 03:00:00.000Z", Identifiers{}); err == nil {
		t.Error("BuildPaymentRequest() error = nil, want error for a QR without tag 54")
	}
}

func TestNewIdentifiers_Formats(t *testing.T) {
	ids, err := NewIdentifiers()
	if err != nil {
		t.Fatalf("NewIdentifiers() error = %v", err)
	}
	if !strings.HasPrefix(ids.CustomerReferenceNumber, "ALTO-API-NMS-") || len(ids.CustomerReferenceNumber) != len("ALTO-API-NMS-")+12 {
		t.Errorf("CustomerReferenceNumber = %q", ids.CustomerReferenceNumber)
	}
	if len(ids.ForwardingReferenceNumber) != 12 || len(ids.AuthorizationID) != 6 || ids.AuthorizationID != strings.ToUpper(ids.AuthorizationID) {
		t.Errorf("ForwardingReferenceNumber %q / AuthorizationID %q have wrong format", ids.ForwardingReferenceNumber, ids.AuthorizationID)
	}
}

// TestPaymentRequest_JSONFieldOrderMatchesCollection pins the wire order of the body to
// docs/manjo-collection/QR Payment.yml: the service may re-serialize before verifying
// X-Alto-Signature, so reordering struct fields would silently break payments.
func TestPaymentRequest_JSONFieldOrderMatchesCollection(t *testing.T) {
	ids := Identifiers{CustomerReferenceNumber: "ALTO-API-NMS-aaaaaaaaaaaa", ForwardingReferenceNumber: "bbbbbbbbbbbb", AuthorizationID: "CCCCCC"}
	req, err := BuildPaymentRequest(realQR, "2026-09-30 03:00:00.000Z", ids)
	if err != nil {
		t.Fatalf("BuildPaymentRequest() error = %v", err)
	}
	got, err := json.Marshal(req)
	if err != nil {
		t.Fatalf("json.Marshal() error = %v", err)
	}
	want := `{"command":"qr-payment-credit","data":{"date_time":"2026-09-30 03:00:00.000Z","customer_reference_number":"ALTO-API-NMS-aaaaaaaaaaaa","authorization_id":"CCCCCC","currency_code":"IDR","amount":50000,"fee":0,"issuer_nns":"93600821","acquirer_nns":"93600858","national_mid":"MT58530503","additional_data":"0520A503321013864160FF4107036590812Q161 Payment","terminal_label":"659","forwarding_customer_reference_number":"bbbbbbbbbbbb","merchant":{"pan":"936008580287697876","id":"MT58530503","criteria":"UKE","name":"Pupuk Kalteng","city":"JAKARTA BARAT","mcc":"5251","postal_code":"11550","country_code":"ID"},"customer":{"pan":"936008580287697876","name":"Tes","account_type":"UNSPECIFIED"}}}`
	if string(got) != want {
		t.Errorf("request JSON\n  got:  %s\n  want: %s", got, want)
	}
}
