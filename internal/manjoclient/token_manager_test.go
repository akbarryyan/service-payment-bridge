package manjoclient

import (
	"context"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func TestTokenManager_Invalidate_ForcesRefreshOnNextGet(t *testing.T) {
	var fetchCount int32
	tm := NewTokenManager(func(ctx context.Context) (string, time.Duration, error) {
		n := atomic.AddInt32(&fetchCount, 1)
		if n == 1 {
			return "token-1", 900 * time.Second, nil
		}
		return "token-2", 900 * time.Second, nil
	})

	tok1, err := tm.Get(context.Background())
	if err != nil {
		t.Fatalf("Get() error = %v", err)
	}
	if tok1 != "token-1" {
		t.Fatalf("tok1 = %q, want token-1", tok1)
	}

	tm.Invalidate()

	tok2, err := tm.Get(context.Background())
	if err != nil {
		t.Fatalf("Get() error = %v", err)
	}
	if tok2 != "token-2" {
		t.Errorf("tok2 = %q, want token-2 (Invalidate should force a fresh fetch)", tok2)
	}
	if atomic.LoadInt32(&fetchCount) != 2 {
		t.Errorf("fetch called %d times, want 2", fetchCount)
	}
}

func TestTokenManager_CachesTokenAcrossCalls(t *testing.T) {
	var fetchCount int32
	tm := NewTokenManager(func(ctx context.Context) (string, time.Duration, error) {
		atomic.AddInt32(&fetchCount, 1)
		return "token-1", 900 * time.Second, nil
	})

	tok1, err := tm.Get(context.Background())
	if err != nil {
		t.Fatalf("Get() error = %v", err)
	}
	tok2, err := tm.Get(context.Background())
	if err != nil {
		t.Fatalf("Get() error = %v", err)
	}

	if tok1 != "token-1" || tok2 != "token-1" {
		t.Errorf("tokens = %q, %q, want both %q", tok1, tok2, "token-1")
	}
	if atomic.LoadInt32(&fetchCount) != 1 {
		t.Errorf("fetch called %d times, want 1", fetchCount)
	}
}

func TestTokenManager_RefreshesWhenNearExpiry(t *testing.T) {
	var fetchCount int32
	tm := NewTokenManager(func(ctx context.Context) (string, time.Duration, error) {
		n := atomic.AddInt32(&fetchCount, 1)
		if n == 1 {
			return "token-1", 900 * time.Second, nil
		}
		return "token-2", 900 * time.Second, nil
	})

	fakeNow := time.Now()
	tm.now = func() time.Time { return fakeNow }

	tok1, err := tm.Get(context.Background())
	if err != nil {
		t.Fatalf("Get() error = %v", err)
	}
	if tok1 != "token-1" {
		t.Fatalf("tok1 = %q, want token-1", tok1)
	}

	// Advance clock to within the refresh threshold (< 60s left of the 900s TTL).
	fakeNow = fakeNow.Add(900*time.Second - 30*time.Second)
	tm.now = func() time.Time { return fakeNow }

	tok2, err := tm.Get(context.Background())
	if err != nil {
		t.Fatalf("Get() error = %v", err)
	}
	if tok2 != "token-2" {
		t.Errorf("tok2 = %q, want token-2 (should have refreshed)", tok2)
	}
	if atomic.LoadInt32(&fetchCount) != 2 {
		t.Errorf("fetch called %d times, want 2", fetchCount)
	}
}

func TestTokenManager_ConcurrentGet_FetchesOnce(t *testing.T) {
	var fetchCount int32
	tm := NewTokenManager(func(ctx context.Context) (string, time.Duration, error) {
		atomic.AddInt32(&fetchCount, 1)
		time.Sleep(10 * time.Millisecond) // simulate network latency
		return "token-1", 900 * time.Second, nil
	})

	var wg sync.WaitGroup
	for i := 0; i < 20; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if _, err := tm.Get(context.Background()); err != nil {
				t.Errorf("Get() error = %v", err)
			}
		}()
	}
	wg.Wait()

	if atomic.LoadInt32(&fetchCount) != 1 {
		t.Errorf("fetch called %d times under concurrent load, want 1", fetchCount)
	}
}
