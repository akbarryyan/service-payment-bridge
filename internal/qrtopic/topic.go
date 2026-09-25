package qrtopic

import (
	"fmt"
	"strings"
)

// TenantSlotNone is the sentinel used in place of a real tenant_id when a
// device attaches directly to a merchant without going through a tenant.
const TenantSlotNone = "_"

type Topic struct {
	MerchantID string
	TenantSlot string
	DeviceID   string
}

// Parse splits a topic string of the form topic/{merchant_id}/{tenant_slot}/{device_id}
// (always exactly 4 segments — see architecture.md Section 4/7).
func Parse(topic string) (Topic, error) {
	parts := strings.Split(topic, "/")
	if len(parts) != 4 || parts[0] != "topic" {
		return Topic{}, fmt.Errorf("qrtopic: invalid topic format %q, want topic/{merchant_id}/{tenant_slot}/{device_id}", topic)
	}
	if parts[1] == "" || parts[2] == "" || parts[3] == "" {
		return Topic{}, fmt.Errorf("qrtopic: topic %q has an empty segment", topic)
	}
	return Topic{MerchantID: parts[1], TenantSlot: parts[2], DeviceID: parts[3]}, nil
}

func Build(merchantID, tenantSlot, deviceID string) string {
	if tenantSlot == "" {
		tenantSlot = TenantSlotNone
	}
	return fmt.Sprintf("topic/%s/%s/%s", merchantID, tenantSlot, deviceID)
}
