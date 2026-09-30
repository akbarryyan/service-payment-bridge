// Package voice turns a Rupiah amount into the "+"-separated list of audio files the
// Q161 Pro firmware plays (mqtt.c: a payload containing ".mp3" is split on "+" and each
// file is played in order).
//
// Adapted from Soundbox/mqtt-poc/publisher/internal/voice and re-targeted at the mp3-api
// clip set loaded on the device: "awal-qris" opens, "akhir-berhasil" (which already says
// "rupiah") closes.
package voice

// Clip is an audio file's base name on the device, without directory or extension.
type Clip string

// Opening and closing clips.
const (
	ClipAwalQris      Clip = "awal-qris"
	ClipAkhirBerhasil Clip = "akhir-berhasil"
)

// Digit clips.
const (
	ClipSatu     Clip = "satu"
	ClipDua      Clip = "dua"
	ClipTiga     Clip = "tiga"
	ClipEmpat    Clip = "empat"
	ClipLima     Clip = "lima"
	ClipEnam     Clip = "enam"
	ClipTujuh    Clip = "tujuh"
	ClipDelapan  Clip = "delapan"
	ClipSembilan Clip = "sembilan"
)

// Irregular forms. Indonesian uses the prefix "se-" for the first unit of these levels,
// so they cannot be built from "satu" plus a multiplier.
const (
	ClipSepuluh Clip = "sepuluh" // 10, not "satu puluh"
	ClipSebelas Clip = "sebelas" // 11, not "satu belas"
	ClipSeratus Clip = "seratus" // 100, not "satu ratus"
	ClipSeribu  Clip = "seribu"  // 1.000, not "satu ribu"
)

// Multiplier clips.
const (
	ClipBelas  Clip = "belas"
	ClipPuluh  Clip = "puluh"
	ClipRatus  Clip = "ratus"
	ClipRibu   Clip = "ribu"
	ClipJuta   Clip = "juta"
	ClipMiliar Clip = "miliar"
)

// digitClips maps 1-9 to their clip. Index 0 is intentionally empty: zero is never
// spoken inside a number.
var digitClips = [10]Clip{
	1: ClipSatu,
	2: ClipDua,
	3: ClipTiga,
	4: ClipEmpat,
	5: ClipLima,
	6: ClipEnam,
	7: ClipTujuh,
	8: ClipDelapan,
	9: ClipSembilan,
}
