// Command altosim pays a UAT QRIS through Alto's simulator, to exercise the
// generate → pay → soundbox flow end to end. Test environment only.
//
//	go run ./cmd/altosim -tx TRX-20260930-XXXXXX   # qris_payload read from DATABASE_URL
//	go run ./cmd/altosim -qr "00020101021226..."
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io/fs"
	"os"
	"time"

	"github.com/joho/godotenv"

	"service-payment-bridge/internal/altosim"
	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
)

const defaultBaseURL = "https://mmsapi-test.manjo.co.id"

func main() {
	txID := flag.String("tx", "", "transaction_id whose qris_payload to pay (read from DATABASE_URL)")
	qr := flag.String("qr", "", "qrContent to pay (alternative to -tx)")
	flag.Parse()
	if (*txID == "") == (*qr == "") {
		fmt.Fprintln(os.Stderr, `usage: altosim -tx TRX-... | -qr "000201..."`)
		os.Exit(2)
	}

	if err := run(*txID, *qr); err != nil {
		fmt.Fprintln(os.Stderr, "altosim:", err)
		os.Exit(1)
	}
}

func run(txID, qrContent string) error {
	for _, file := range []string{".env", ".env.sandbox"} {
		if err := godotenv.Load(file); err != nil && !errors.Is(err, fs.ErrNotExist) {
			return fmt.Errorf("load %s: %w", file, err)
		}
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	if txID != "" {
		var err error
		if qrContent, err = loadQR(ctx, txID); err != nil {
			return err
		}
	}

	apiKey, validationKey := os.Getenv("ALTO_API_KEY"), os.Getenv("ALTO_VALIDATION_KEY")
	if apiKey == "" || validationKey == "" {
		return errors.New("ALTO_API_KEY and ALTO_VALIDATION_KEY must be set (see .env.sandbox.example)")
	}
	baseURL := os.Getenv("ALTO_BASE_URL")
	if baseURL == "" {
		baseURL = defaultBaseURL
	}

	ids, err := altosim.NewIdentifiers()
	if err != nil {
		return err
	}
	req, err := altosim.BuildPaymentRequest(qrContent, altosim.FormatTimestamp(time.Now()), ids)
	if err != nil {
		return err
	}

	fmt.Printf("paying %s (%s) Rp%d, reference %s via %s\n",
		req.Data.Merchant.Name, req.Data.NationalMID, req.Data.Amount, req.ReferenceLabel(), baseURL)

	client := &altosim.Client{BaseURL: baseURL, APIKey: apiKey, ValidationKey: validationKey}
	status, body, err := client.Pay(ctx, req)
	if err != nil {
		return err
	}
	fmt.Printf("HTTP %d\n%s\n", status, body)
	if status >= 300 {
		return fmt.Errorf("alto answered HTTP %d", status)
	}
	return nil
}

func loadQR(ctx context.Context, txID string) (string, error) {
	dbURL := os.Getenv("DATABASE_URL")
	if dbURL == "" {
		return "", errors.New("DATABASE_URL must be set to use -tx")
	}
	pool, err := database.NewPool(ctx, dbURL)
	if err != nil {
		return "", err
	}
	defer pool.Close()

	tx, err := sqlc.New(pool).GetTransactionByID(ctx, txID)
	if err != nil {
		return "", fmt.Errorf("transaction %s: %w", txID, err)
	}
	if !tx.QrisPayload.Valid {
		return "", fmt.Errorf("transaction %s has no qris_payload (status %s)", txID, tx.Status)
	}
	return tx.QrisPayload.String, nil
}
