package transaction

import (
	"crypto/rand"
	"fmt"
	"time"
)

// generateTransactionID produces TRX-{yyyyMMdd}-{6 char random alphanumeric}.
// Collision probability is negligible (36^6 per day); on the rare UNIQUE
// violation, callers regenerate and retry the insert (process-flow.md
// Flow 1 step 6).
func generateTransactionID(now time.Time) (string, error) {
	const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	randomBytes := make([]byte, 6)
	if _, err := rand.Read(randomBytes); err != nil {
		return "", fmt.Errorf("transaction: failed to generate id suffix: %w", err)
	}
	b := make([]byte, 6)
	for i, rb := range randomBytes {
		b[i] = alphabet[int(rb)%len(alphabet)]
	}
	return fmt.Sprintf("TRX-%s-%s", now.Format("20060102"), string(b)), nil
}
