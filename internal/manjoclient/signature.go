package manjoclient

import (
	"crypto"
	"crypto/hmac"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/sha512"
	"crypto/x509"
	"encoding/base64"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"strings"
)

// SignAccessToken computes the X-SIGNATURE header for Access Token B2B
// (RSA-SHA256 over "clientKey|timestamp", hex-encoded — confirmed against
// docs/manjo-collection/BI SNAP/access-token.yml, not base64).
func SignAccessToken(privateKeyPEM []byte, clientKey, timestamp string) (string, error) {
	block, _ := pem.Decode(privateKeyPEM)
	if block == nil {
		return "", fmt.Errorf("manjoclient: failed to decode PEM block from private key")
	}

	key, err := x509.ParsePKCS8PrivateKey(block.Bytes)
	if err != nil {
		return "", fmt.Errorf("manjoclient: failed to parse PKCS8 private key: %w", err)
	}

	rsaKey, ok := key.(*rsa.PrivateKey)
	if !ok {
		return "", fmt.Errorf("manjoclient: private key is not an RSA key")
	}

	message := clientKey + "|" + timestamp
	hashed := sha256.Sum256([]byte(message))

	signature, err := rsa.SignPKCS1v15(rand.Reader, rsaKey, crypto.SHA256, hashed[:])
	if err != nil {
		return "", fmt.Errorf("manjoclient: failed to sign access token request: %w", err)
	}

	return hex.EncodeToString(signature), nil
}

// SignHMAC computes the X-SIGNATURE header for Generate QR / Query / Refund /
// Notify (HMAC-SHA512 over method:path:accessToken:bodyHash:timestamp,
// hex-encoded). body must already be the exact bytes sent as the HTTP body
// (json.Marshal output is compact by default, satisfying "minify").
func SignHMAC(clientSecret, method, path, accessToken string, body []byte, timestamp string) (string, error) {
	bodyHash := sha256.Sum256(body)
	bodyHashHex := hex.EncodeToString(bodyHash[:])

	stringToSign := method + ":" + path + ":" + accessToken + ":" + bodyHashHex + ":" + timestamp

	mac := hmac.New(sha512.New, []byte(clientSecret))
	mac.Write([]byte(stringToSign))

	return hex.EncodeToString(mac.Sum(nil)), nil
}

// WrapPKCS8PEM wraps a raw base64 PKCS8 key (no PEM headers, possibly with
// embedded whitespace — the format Manjo credentials are typically shared
// in) into a proper PEM block ready for SignAccessToken.
func WrapPKCS8PEM(raw string) ([]byte, error) {
	clean := strings.Join(strings.Fields(raw), "")
	decoded, err := base64.StdEncoding.DecodeString(clean)
	if err != nil {
		return nil, fmt.Errorf("manjoclient: failed to decode base64 private key: %w", err)
	}
	return pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: decoded}), nil
}
