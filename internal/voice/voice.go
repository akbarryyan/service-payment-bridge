package voice

import (
	"fmt"
	"strings"
)

// MaxRupiah is the largest speakable amount: above it a "triliun" level would be needed.
const MaxRupiah int64 = 999_999_999_999

const (
	// dir is where the audio files live on the device (Downtool StartAddr=/ext/).
	dir = "/ext/"
	// ext must stay ".mp3": the firmware tells an audio list from TTS text by it.
	ext       = ".mp3"
	separator = "+"
)

// scale pairs one number level with its multiplier clip. solo is the special form used
// when the level's value is exactly one ("seribu"); empty means the regular form
// ("satu juta").
type scale struct {
	value int64
	mult  Clip
	solo  Clip
}

// scales is ordered from the largest level down.
var scales = []scale{
	{value: 1_000_000_000, mult: ClipMiliar},
	{value: 1_000_000, mult: ClipJuta},
	{value: 1_000, mult: ClipRibu, solo: ClipSeribu},
}

// Compose returns the clips announcing a successful payment of rupiah.
func Compose(rupiah int64) ([]Clip, error) {
	if rupiah <= 0 {
		return nil, fmt.Errorf("voice: amount must be > 0, got %d", rupiah)
	}
	if rupiah > MaxRupiah {
		return nil, fmt.Errorf("voice: amount %d exceeds %d", rupiah, MaxRupiah)
	}

	clips := []Clip{ClipAwalQris}
	clips = append(clips, numberClips(rupiah)...)
	return append(clips, ClipAkhirBerhasil), nil
}

// Payload returns the MQTT payload announcing rupiah, e.g. for Rp50.000:
// "/ext/awal-qris.mp3+/ext/lima.mp3+/ext/puluh.mp3+/ext/ribu.mp3+/ext/akhir-berhasil.mp3".
func Payload(rupiah int64) (string, error) {
	clips, err := Compose(rupiah)
	if err != nil {
		return "", err
	}
	paths := make([]string, len(clips))
	for i, c := range clips {
		paths[i] = dir + string(c) + ext
	}
	return strings.Join(paths, separator), nil
}

// numberClips spells a positive integer, level by level from the highest.
func numberClips(n int64) []Clip {
	if n < 100 {
		return belowHundred(n)
	}
	for _, s := range scales {
		if n >= s.value {
			return withScale(n, s)
		}
	}
	return hundreds(n)
}

// belowHundred spells 1-99, including "sepuluh", "sebelas" and "<digit> belas".
func belowHundred(n int64) []Clip {
	switch {
	case n < 10:
		return []Clip{digitClips[n]}
	case n == 10:
		return []Clip{ClipSepuluh}
	case n == 11:
		return []Clip{ClipSebelas}
	case n < 20:
		return []Clip{digitClips[n-10], ClipBelas}
	}

	clips := []Clip{digitClips[n/10], ClipPuluh}
	if rest := n % 10; rest > 0 {
		clips = append(clips, digitClips[rest])
	}
	return clips
}

// hundreds spells 100-999: "seratus" for the first hundred, "<digit> ratus" otherwise.
func hundreds(n int64) []Clip {
	var clips []Clip
	if h := n / 100; h == 1 {
		clips = []Clip{ClipSeratus}
	} else {
		clips = []Clip{digitClips[h], ClipRatus}
	}
	if rest := n % 100; rest > 0 {
		clips = append(clips, belowHundred(rest)...)
	}
	return clips
}

// withScale spells one level (ribu, juta, miliar) and then the remainder.
func withScale(n int64, s scale) []Clip {
	var clips []Clip
	if part := n / s.value; part == 1 && s.solo != "" {
		clips = []Clip{s.solo}
	} else {
		clips = append(numberClips(part), s.mult)
	}
	if rest := n % s.value; rest > 0 {
		clips = append(clips, numberClips(rest)...)
	}
	return clips
}
