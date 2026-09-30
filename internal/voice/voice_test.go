package voice

import (
	"strings"
	"testing"
)

// words renders clips as a sentence so failures read as Indonesian, not file names.
func words(clips []Clip) string {
	parts := make([]string, len(clips))
	for i, c := range clips {
		parts[i] = string(c)
	}
	return strings.Join(parts, " ")
}

func TestCompose(t *testing.T) {
	tests := []struct {
		rupiah int64
		want   string
	}{
		{7, "awal-qris tujuh akhir-berhasil"},
		// "se-" forms must not be built from "satu" + multiplier.
		{10, "awal-qris sepuluh akhir-berhasil"},
		{11, "awal-qris sebelas akhir-berhasil"},
		{100, "awal-qris seratus akhir-berhasil"},
		{1_000, "awal-qris seribu akhir-berhasil"},
		{12, "awal-qris dua belas akhir-berhasil"},
		{17, "awal-qris tujuh belas akhir-berhasil"},
		{40, "awal-qris empat puluh akhir-berhasil"},
		{45, "awal-qris empat puluh lima akhir-berhasil"},
		{125, "awal-qris seratus dua puluh lima akhir-berhasil"},
		{375, "awal-qris tiga ratus tujuh puluh lima akhir-berhasil"},
		{1_500, "awal-qris seribu lima ratus akhir-berhasil"},
		{50_000, "awal-qris lima puluh ribu akhir-berhasil"},
		{125_500, "awal-qris seratus dua puluh lima ribu lima ratus akhir-berhasil"},
		// "satu juta", not "sejuta": "se-" only applies up to thousands.
		{1_000_000, "awal-qris satu juta akhir-berhasil"},
		{1_250_000, "awal-qris satu juta dua ratus lima puluh ribu akhir-berhasil"},
		{2_000_000_000, "awal-qris dua miliar akhir-berhasil"},
	}

	for _, tt := range tests {
		clips, err := Compose(tt.rupiah)
		if err != nil {
			t.Fatalf("Compose(%d) error = %v", tt.rupiah, err)
		}
		if got := words(clips); got != tt.want {
			t.Errorf("Compose(%d)\n  got:  %s\n  want: %s", tt.rupiah, got, tt.want)
		}
	}
}

func TestComposeRejects(t *testing.T) {
	for _, rupiah := range []int64{0, -1, MaxRupiah + 1} {
		if _, err := Compose(rupiah); err == nil {
			t.Errorf("Compose(%d) error = nil, want error", rupiah)
		}
	}
}

func TestPayload_MatchesDeviceFormat(t *testing.T) {
	got, err := Payload(50_000)
	if err != nil {
		t.Fatalf("Payload() error = %v", err)
	}
	want := "/ext/awal-qris.mp3+/ext/lima.mp3+/ext/puluh.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3"
	if got != want {
		t.Errorf("Payload(50000)\n  got:  %s\n  want: %s", got, want)
	}
}

// mp3APIClips are the (non-_adr) files in Soundbox/docs/mp3/mp3-api, the set loaded on
// the Q161 Pro. A clip outside this set would be silently skipped by the device.
var mp3APIClips = map[Clip]bool{
	"akhir-berhasil": true, "awal-qris": true, "belas": true, "delapan": true, "dua": true,
	"empat": true, "enam": true, "juta": true, "lima": true, "miliar": true, "minus": true,
	"nol": true, "puluh": true, "ratus": true, "ribu": true, "satu": true, "sebelas": true,
	"sembilan": true, "sepuluh": true, "seratus": true, "seribu": true, "tiga": true,
	"triliun": true, "tujuh": true,
}

func TestEveryClipExistsOnDevice(t *testing.T) {
	amounts := []int64{12_345, 999_999, 1_000_000, 987_654_321, MaxRupiah}
	for rupiah := int64(1); rupiah <= 2_000; rupiah++ {
		amounts = append(amounts, rupiah)
	}
	for _, rupiah := range amounts {
		clips, err := Compose(rupiah)
		if err != nil {
			t.Fatalf("Compose(%d) error = %v", rupiah, err)
		}
		for _, c := range clips {
			if !mp3APIClips[c] {
				t.Fatalf("Compose(%d) uses clip %q, which is not on the device", rupiah, c)
			}
		}
	}
}
