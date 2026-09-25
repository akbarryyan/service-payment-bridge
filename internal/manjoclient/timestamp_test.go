package manjoclient

import (
	"regexp"
	"testing"
	"time"
)

func TestNowJakarta_Format(t *testing.T) {
	got := NowJakarta()
	pattern := `^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}\+07:00$`
	matched, err := regexp.MatchString(pattern, got)
	if err != nil {
		t.Fatalf("regexp error: %v", err)
	}
	if !matched {
		t.Errorf("NowJakarta() = %q, does not match pattern %q", got, pattern)
	}
}

func TestNowJakarta_RepresentsCorrectInstant(t *testing.T) {
	before := time.Now()
	got := NowJakarta()
	after := time.Now()

	parsed, err := time.Parse("2006-01-02T15:04:05.000-07:00", got)
	if err != nil {
		t.Fatalf("failed to parse NowJakarta() output %q: %v", got, err)
	}

	if parsed.Before(before.Add(-2*time.Second)) || parsed.After(after.Add(2*time.Second)) {
		t.Errorf("NowJakarta() = %q (parsed instant %v) is not close to actual now (between %v and %v) — timestamp may be mislabeled instead of shifted", got, parsed, before, after)
	}
}
