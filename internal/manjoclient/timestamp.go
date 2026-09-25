package manjoclient

import "time"

// NowJakarta returns the current time formatted as WIB (UTC+7), matching
// the format Manjo's sandbox actually accepts (yyyy-MM-ddTHH:mm:ss.SSS+07:00).
// It shifts the clock explicitly rather than relabeling UTC as WIB — a bug
// found in an earlier version of the reference implementation.
func NowJakarta() string {
	loc := time.FixedZone("WIB", 7*60*60)
	return time.Now().In(loc).Format("2006-01-02T15:04:05.000-07:00")
}
