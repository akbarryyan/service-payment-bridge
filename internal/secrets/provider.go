package secrets

import (
	"context"
	"fmt"
	"os"
)

// Provider resolves a credential reference (e.g. merchants.manjo_private_key_ref)
// into its actual secret value. EnvProvider is the first implementation
// (ref = environment variable name); a real secret manager (Vault/cloud)
// can be swapped in later without changing callers.
type Provider interface {
	Resolve(ctx context.Context, ref string) (string, error)
}

type EnvProvider struct{}

func (EnvProvider) Resolve(ctx context.Context, ref string) (string, error) {
	v, ok := os.LookupEnv(ref)
	if !ok {
		return "", fmt.Errorf("secrets: environment variable %q not set", ref)
	}
	return v, nil
}
