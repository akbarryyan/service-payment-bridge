package secrets

import (
	"context"
	"testing"
)

func TestEnvProvider_Resolve_Found(t *testing.T) {
	t.Setenv("TEST_SECRET_REF", "hello-secret")

	p := EnvProvider{}
	v, err := p.Resolve(context.Background(), "TEST_SECRET_REF")
	if err != nil {
		t.Fatalf("Resolve() error = %v", err)
	}
	if v != "hello-secret" {
		t.Errorf("Resolve() = %q, want %q", v, "hello-secret")
	}
}

func TestEnvProvider_Resolve_NotFound(t *testing.T) {
	p := EnvProvider{}
	_, err := p.Resolve(context.Background(), "TEST_SECRET_REF_DOES_NOT_EXIST")
	if err == nil {
		t.Fatal("Resolve() expected error for missing env var, got nil")
	}
}
