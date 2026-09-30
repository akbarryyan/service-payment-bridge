package altosim

import "testing"

// realQR is a real UAT qrContent for merchant Pupuk Kalteng, Rp50.000.
const realQR = "00020101021226620015ID.CO.MANJO.WWW01189360085802876978760210MT585305030303UKE51430014ID.CO.QRIS.WWW0205Manjo04121.0.28.09.26520452515303360540850000.005802ID5913Pupuk Kalteng6013JAKARTA BARAT61051155062470520A503321013864160FF4107036590812Q161 Payment6304A037"

func TestParseEMV_RealUATQR(t *testing.T) {
	top, err := ParseEMV(realQR)
	if err != nil {
		t.Fatalf("ParseEMV() error = %v", err)
	}
	wantTop := map[string]string{
		"52": "5251", "54": "50000.00", "58": "ID", "59": "Pupuk Kalteng",
		"60": "JAKARTA BARAT", "61": "11550", "63": "A037",
		"62": "0520A503321013864160FF4107036590812Q161 Payment",
	}
	for tag, want := range wantTop {
		if top[tag] != want {
			t.Errorf("tag %s = %q, want %q", tag, top[tag], want)
		}
	}

	merchant, err := ParseEMV(top["26"])
	if err != nil {
		t.Fatalf("ParseEMV(tag 26) error = %v", err)
	}
	for tag, want := range map[string]string{"00": "ID.CO.MANJO.WWW", "01": "936008580287697876", "02": "MT58530503", "03": "UKE"} {
		if merchant[tag] != want {
			t.Errorf("tag 26.%s = %q, want %q", tag, merchant[tag], want)
		}
	}

	additional, err := ParseEMV(top["62"])
	if err != nil {
		t.Fatalf("ParseEMV(tag 62) error = %v", err)
	}
	for tag, want := range map[string]string{"05": "A503321013864160FF41", "07": "659", "08": "Q161 Payment"} {
		if additional[tag] != want {
			t.Errorf("tag 62.%s = %q, want %q", tag, additional[tag], want)
		}
	}
}

func TestParseEMV_RejectsMalformedInput(t *testing.T) {
	for _, s := range []string{"000", "0005ab", "00X2ab", "01-1Xhello"} {
		if _, err := ParseEMV(s); err == nil {
			t.Errorf("ParseEMV(%q) error = nil, want error", s)
		}
	}
}
