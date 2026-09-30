package transaction

import (
	"testing"
	"time"

	"service-payment-bridge/internal/manjoclient"
)

func TestComputeExpireAt(t *testing.T) {
	wib := time.FixedZone("WIB", 7*60*60)
	receivedAt := time.Date(2026, 9, 29, 9, 34, 54, 0, wib)
	expireDateWIB := time.Date(2026, 9, 29, 9, 42, 24, 0, wib)

	tests := []struct {
		name string
		info manjoclient.GenerateQRResponseAdditionalInfo
		want time.Time
	}{
		{
			// Real UAT response: expireDate is 7h too late, expiryDuration is right.
			name: "expiryDuration wins over the +7h expireDate",
			info: manjoclient.GenerateQRResponseAdditionalInfo{ExpiryDuration: "450000", ExpireDate: "20260929164224"},
			want: receivedAt.Add(7*time.Minute + 30*time.Second),
		},
		{
			name: "missing expiryDuration falls back to expireDate in WIB",
			info: manjoclient.GenerateQRResponseAdditionalInfo{ExpireDate: "20260929094224"},
			want: expireDateWIB,
		},
		{
			name: "non-numeric expiryDuration falls back to expireDate",
			info: manjoclient.GenerateQRResponseAdditionalInfo{ExpiryDuration: "abc", ExpireDate: "20260929094224"},
			want: expireDateWIB,
		},
		{
			name: "zero expiryDuration falls back to expireDate",
			info: manjoclient.GenerateQRResponseAdditionalInfo{ExpiryDuration: "0", ExpireDate: "20260929094224"},
			want: expireDateWIB,
		},
		{
			name: "nothing usable falls back to 10 minutes",
			info: manjoclient.GenerateQRResponseAdditionalInfo{},
			want: receivedAt.Add(10 * time.Minute),
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := computeExpireAt(tt.info, receivedAt); !got.Equal(tt.want) {
				t.Errorf("computeExpireAt() = %v, want %v", got, tt.want)
			}
		})
	}
}
