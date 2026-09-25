package transaction

import (
	"regexp"
	"testing"
	"time"
)

func TestGenerateTransactionID_Format(t *testing.T) {
	now := time.Date(2026, 9, 25, 10, 0, 0, 0, time.UTC)
	id, err := generateTransactionID(now)
	if err != nil {
		t.Fatalf("generateTransactionID() error = %v", err)
	}
	pattern := `^TRX-20260925-[A-Z0-9]{6}$`
	matched, _ := regexp.MatchString(pattern, id)
	if !matched {
		t.Errorf("generateTransactionID() = %q, does not match pattern %q", id, pattern)
	}
}

func TestGenerateTransactionID_Unique(t *testing.T) {
	now := time.Now()
	seen := make(map[string]bool)
	for i := 0; i < 1000; i++ {
		id, err := generateTransactionID(now)
		if err != nil {
			t.Fatalf("generateTransactionID() error = %v", err)
		}
		if seen[id] {
			t.Fatalf("generateTransactionID() produced duplicate: %s", id)
		}
		seen[id] = true
	}
}
