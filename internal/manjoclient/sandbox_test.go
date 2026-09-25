package manjoclient

import (
	"context"
	"os"
	"testing"
	"time"

	"service-payment-bridge/internal/secrets"
)

func TestSandbox_AccessTokenAndGenerateQR(t *testing.T) {
	if os.Getenv("RUN_SANDBOX_TESTS") != "1" {
		t.Skip("RUN_SANDBOX_TESTS not set to 1 — skipping real sandbox call (see .env.sandbox.example)")
	}

	ctx := context.Background()
	provider := secrets.EnvProvider{}

	clientKey := mustResolve(t, ctx, provider, "MANJO_SANDBOX_CLIENT_KEY")
	privateKeyRaw := mustResolve(t, ctx, provider, "MANJO_SANDBOX_PRIVATE_KEY")
	clientSecret := mustResolve(t, ctx, provider, "MANJO_SANDBOX_CLIENT_SECRET")
	merchantID := mustResolve(t, ctx, provider, "MANJO_SANDBOX_MERCHANT_ID")

	baseURL, err := provider.Resolve(ctx, "MANJO_SANDBOX_BASE_URL")
	if err != nil {
		baseURL = defaultBaseURL
	}

	cfg := Config{
		BaseURL:       baseURL,
		ClientKey:     clientKey,
		PrivateKeyPEM: mustWrapPKCS8PEM(t, privateKeyRaw),
		ClientSecret:  clientSecret,
		PartnerID:     merchantID, // dikoreksi setelah sandbox nyata menolak clientKey ("Invalid X-PARTNER-ID") — merchantID yang benar, bukan mcCodePayId
		ChannelID:     "05",
	}

	c := New(cfg)

	reqCtx, cancel := context.WithTimeout(ctx, 15*time.Second)
	defer cancel()

	token, expiresIn, err := c.AccessToken(reqCtx)
	if err != nil {
		t.Fatalf("AccessToken() against sandbox failed: %v", err)
	}
	if token == "" {
		t.Fatal("AccessToken() returned empty token")
	}
	if expiresIn.Seconds() != 900 {
		// Dokumentasi bilang 900s, tapi sandbox UAT nyata mengembalikan durasi
		// berbeda (ditemukan 13h saat verifikasi Fase 2) — dicatat, bukan
		// dianggap gagal, karena bukan bug di sisi Client.
		t.Logf("expiresIn = %v, BEDA dari dokumentasi (900s) — dicatat sebagai temuan, bukan kegagalan", expiresIn)
	}
	t.Logf("access token obtained, expiresIn=%v", expiresIn)

	resp, externalID, err := c.GenerateQR(reqCtx, GenerateQRRequest{
		PartnerReferenceNo: "SANDBOX-TEST-" + time.Now().Format("20060102150405"),
		Amount:             Amount{Value: "1000.00", Currency: "IDR"},
		MerchantID:         merchantID,
		ValidityPeriod:     "3600",
		AdditionalInfo: GenerateQRAdditionalInfo{
			PaymentID:     "99",
			DynamicAmount: "N",
			ProdDesc:      "Sandbox test Fase 2",
		},
	})
	if err != nil {
		t.Fatalf("GenerateQR() against sandbox failed: %v (kalau 401/404, coba PartnerID=merchantID sesuai contoh manjo-api-docs.md — lihat temuan #5 di spec)", err)
	}
	if resp.QRContent == "" {
		t.Error("GenerateQR() succeeded but QRContent is empty")
	}
	t.Logf("qrContent received (%d chars), referenceNo=%s", len(resp.QRContent), resp.ReferenceNo)
	t.Logf("externalID used=%s", externalID)
}

func mustResolve(t *testing.T, ctx context.Context, p secrets.Provider, ref string) string {
	t.Helper()
	v, err := p.Resolve(ctx, ref)
	if err != nil {
		t.Fatalf("failed to resolve %s: %v (see .env.sandbox.example)", ref, err)
	}
	return v
}

func mustWrapPKCS8PEM(t *testing.T, raw string) []byte {
	t.Helper()
	pemBytes, err := WrapPKCS8PEM(raw)
	if err != nil {
		t.Fatalf("WrapPKCS8PEM: %v", err)
	}
	return pemBytes
}
