package qrtopic

import "testing"

func TestParse_WithTenant(t *testing.T) {
	got, err := Parse("topic/MT82419344/TNT-tokoa/DEV-001")
	if err != nil {
		t.Fatalf("Parse() error = %v", err)
	}
	want := Topic{MerchantID: "MT82419344", TenantSlot: "TNT-tokoa", DeviceID: "DEV-001"}
	if got != want {
		t.Errorf("Parse() = %+v, want %+v", got, want)
	}
}

func TestParse_WithoutTenant_Sentinel(t *testing.T) {
	got, err := Parse("topic/MT82419344/_/DEV-001")
	if err != nil {
		t.Fatalf("Parse() error = %v", err)
	}
	if got.TenantSlot != TenantSlotNone {
		t.Errorf("TenantSlot = %q, want %q", got.TenantSlot, TenantSlotNone)
	}
}

func TestParse_InvalidFormat(t *testing.T) {
	cases := []string{
		"topic/MT82419344",
		"topic/MT82419344/DEV-001",
		"topic/MT82419344/_/DEV-001/extra",
		"wrong/MT82419344/_/DEV-001",
		"topic//_/DEV-001",
	}
	for _, tc := range cases {
		if _, err := Parse(tc); err == nil {
			t.Errorf("Parse(%q) expected error, got nil", tc)
		}
	}
}

func TestBuild_WithTenant(t *testing.T) {
	got := Build("MT82419344", "TNT-tokoa", "DEV-001")
	want := "topic/MT82419344/TNT-tokoa/DEV-001"
	if got != want {
		t.Errorf("Build() = %q, want %q", got, want)
	}
}

func TestBuild_EmptyTenantDefaultsToSentinel(t *testing.T) {
	got := Build("MT82419344", "", "DEV-001")
	want := "topic/MT82419344/_/DEV-001"
	if got != want {
		t.Errorf("Build() = %q, want %q", got, want)
	}
}
