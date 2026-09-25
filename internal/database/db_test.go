package database

import (
	"context"
	"testing"
	"time"
)

func TestNewPool_ConnectsAndPings(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	pool, err := NewPool(ctx, "postgres://payment_bridge:payment_bridge@localhost:15432/payment_bridge?sslmode=disable")
	if err != nil {
		t.Fatalf("NewPool() error = %v (pastikan `docker compose up -d postgres` sedang jalan)", err)
	}
	defer pool.Close()

	if err := pool.Ping(ctx); err != nil {
		t.Fatalf("pool.Ping() error = %v", err)
	}
}

func TestNewPool_InvalidURL(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	_, err := NewPool(ctx, "postgres://baduser:badpass@localhost:15432/nonexistent?sslmode=disable")
	if err == nil {
		t.Fatal("NewPool() expected error for invalid connection, got nil")
	}
}
