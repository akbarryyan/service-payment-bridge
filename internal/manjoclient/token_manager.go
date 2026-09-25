package manjoclient

import (
	"context"
	"sync"
	"time"
)

const tokenRefreshThreshold = 60 * time.Second

type fetchTokenFunc func(ctx context.Context) (token string, expiresIn time.Duration, err error)

// TokenManager caches a Manjo access token in memory and refreshes it
// proactively before expiry. Concurrent Get() calls are serialized through
// a single mutex, so only one goroutine ever performs the actual fetch;
// the rest reuse the token it obtained (single-flight via lock, not
// singleflight.Group — simpler and sufficient for this use case).
type TokenManager struct {
	mu        sync.Mutex
	fetch     fetchTokenFunc
	token     string
	expiresAt time.Time
	now       func() time.Time
}

func NewTokenManager(fetch fetchTokenFunc) *TokenManager {
	return &TokenManager{
		fetch: fetch,
		now:   time.Now,
	}
}

func (tm *TokenManager) Get(ctx context.Context) (string, error) {
	tm.mu.Lock()
	defer tm.mu.Unlock()

	if tm.token != "" && tm.now().Before(tm.expiresAt.Add(-tokenRefreshThreshold)) {
		return tm.token, nil
	}

	token, expiresIn, err := tm.fetch(ctx)
	if err != nil {
		return "", err
	}

	tm.token = token
	tm.expiresAt = tm.now().Add(expiresIn)

	return tm.token, nil
}

// Invalidate clears the cached token, forcing the next Get() to fetch a
// fresh one — needed when Manjo rejects a cached-but-not-yet-expired token
// with 401 (architecture.md Section 13.1).
func (tm *TokenManager) Invalidate() {
	tm.mu.Lock()
	defer tm.mu.Unlock()
	tm.token = ""
	tm.expiresAt = time.Time{}
}
