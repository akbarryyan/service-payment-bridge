package manjoclient

import (
	"sync"
	"sync/atomic"
	"testing"
)

func TestRegistry_CachesClientPerMerchant(t *testing.T) {
	r := NewRegistry()
	var buildCount int32

	build := func() (Config, error) {
		atomic.AddInt32(&buildCount, 1)
		return Config{ClientKey: "k"}, nil
	}

	c1, err := r.GetOrCreate("MT001", build)
	if err != nil {
		t.Fatalf("GetOrCreate() error = %v", err)
	}
	c2, err := r.GetOrCreate("MT001", build)
	if err != nil {
		t.Fatalf("GetOrCreate() error = %v", err)
	}

	if c1 != c2 {
		t.Error("GetOrCreate() returned different *Client for same merchant")
	}
	if atomic.LoadInt32(&buildCount) != 1 {
		t.Errorf("build called %d times, want 1", buildCount)
	}
}

func TestRegistry_DifferentMerchantsGetDifferentClients(t *testing.T) {
	r := NewRegistry()

	c1, _ := r.GetOrCreate("MT001", func() (Config, error) { return Config{ClientKey: "k1"}, nil })
	c2, _ := r.GetOrCreate("MT002", func() (Config, error) { return Config{ClientKey: "k2"}, nil })

	if c1 == c2 {
		t.Error("GetOrCreate() returned same *Client for different merchants")
	}
}

func TestRegistry_ConcurrentGetOrCreate_BuildsOncePerMerchant(t *testing.T) {
	r := NewRegistry()
	var buildCount int32

	var wg sync.WaitGroup
	for i := 0; i < 20; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			_, err := r.GetOrCreate("MT001", func() (Config, error) {
				atomic.AddInt32(&buildCount, 1)
				return Config{ClientKey: "k"}, nil
			})
			if err != nil {
				t.Errorf("GetOrCreate() error = %v", err)
			}
		}()
	}
	wg.Wait()

	if atomic.LoadInt32(&buildCount) != 1 {
		t.Errorf("build called %d times under concurrent load, want 1", buildCount)
	}
}
