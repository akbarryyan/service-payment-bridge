package manjoclient

import "sync"

// Registry lazily creates and caches one Client per merchant, so each
// merchant's TokenManager (and its cached access token) is reused across
// requests instead of being rebuilt on every call — rebuilding per request
// would force a fresh access-token fetch every time, defeating the cache.
type Registry struct {
	mu      sync.Mutex
	clients map[string]*Client
}

func NewRegistry() *Registry {
	return &Registry{clients: make(map[string]*Client)}
}

// GetOrCreate returns the cached Client for merchantID, or builds one via
// buildConfig (invoked only on cache miss, while holding the lock so
// concurrent callers for the same merchant never build twice) and caches it.
func (r *Registry) GetOrCreate(merchantID string, buildConfig func() (Config, error)) (*Client, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	if c, ok := r.clients[merchantID]; ok {
		return c, nil
	}

	cfg, err := buildConfig()
	if err != nil {
		return nil, err
	}

	c := New(cfg)
	r.clients[merchantID] = c
	return c, nil
}
