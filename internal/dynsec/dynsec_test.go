package dynsec

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"testing"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"

	"service-payment-bridge/internal/mqttclient"
	"service-payment-bridge/internal/testbroker"
)

// newClient connects as the dev `test` account, which bootstrap --dev gives $CONTROL rights.
func newClient(t *testing.T) *Client {
	t.Helper()
	conn, err := mqttclient.Connect(testbroker.URL(), testbroker.Username(), testbroker.Password())
	if err != nil {
		t.Fatalf("connect broker (docker compose up -d; scripts/broker-bootstrap.sh --dev): %v", err)
	}
	t.Cleanup(conn.Disconnect)
	c, err := New(conn)
	if err != nil {
		t.Fatalf("New() error = %v", err)
	}
	return c
}

func canLogin(username, password string) bool {
	conn, err := mqttclient.Connect(testbroker.URL(), username, password)
	if err != nil {
		return false
	}
	conn.Disconnect()
	return true
}

func TestRoleAndClientLifecycle(t *testing.T) {
	ctx := context.Background()
	c := newClient(t)
	suffix := fmt.Sprintf("%d", time.Now().UnixNano())
	role, user := "dynsec-test-role-"+suffix, "dynsec-test-user-"+suffix
	t.Cleanup(func() {
		c.DeleteClient(context.Background(), user)
		c.DeleteRole(context.Background(), role)
	})

	if err := c.CreateRole(ctx, role); err != nil {
		t.Fatalf("CreateRole() error = %v", err)
	}
	if err := c.CreateRole(ctx, role); !errors.Is(err, ErrAlreadyExists) {
		t.Errorf("second CreateRole() error = %v, want ErrAlreadyExists", err)
	}
	if err := c.AddRoleACL(ctx, role, ACLSubscribeLiteral, "topic/X/"+suffix); err != nil {
		t.Fatalf("AddRoleACL() error = %v", err)
	}
	if err := c.AddRoleACL(ctx, role, ACLSubscribeLiteral, "topic/X/"+suffix); !errors.Is(err, ErrAlreadyExists) {
		t.Errorf("duplicate AddRoleACL() error = %v, want ErrAlreadyExists", err)
	}

	if err := c.CreateClient(ctx, user, "first-password", role); err != nil {
		t.Fatalf("CreateClient() error = %v", err)
	}
	if err := c.CreateClient(ctx, user, "first-password", role); !errors.Is(err, ErrAlreadyExists) {
		t.Errorf("second CreateClient() error = %v, want ErrAlreadyExists", err)
	}
	roles, err := c.ClientRoles(ctx, user)
	if err != nil || len(roles) != 1 || roles[0] != role {
		t.Errorf("ClientRoles() = %v, %v; want [%s]", roles, err, role)
	}
	if !canLogin(user, "first-password") {
		t.Fatal("new client cannot log in")
	}

	role2 := "dynsec-test-role2-" + suffix
	t.Cleanup(func() {
		c.DeleteRole(context.Background(), role2)
	})
	if err := c.CreateRole(ctx, role2); err != nil {
		t.Fatalf("CreateRole(role2) error = %v", err)
	}
	if err := c.AddClientRole(ctx, user, role2); err != nil {
		t.Fatalf("AddClientRole() error = %v", err)
	}
	roles, err = c.ClientRoles(ctx, user)
	if err != nil || !sameStringSet(roles, []string{role, role2}) {
		t.Errorf("ClientRoles() after AddClientRole = %v, %v; want {%s, %s}", roles, err, role, role2)
	}

	if err := c.SetClientPassword(ctx, user, "second-password"); err != nil {
		t.Fatalf("SetClientPassword() error = %v", err)
	}
	if canLogin(user, "first-password") || !canLogin(user, "second-password") {
		t.Error("password rotation did not take effect")
	}

	if err := c.DisableClient(ctx, user); err != nil {
		t.Fatalf("DisableClient() error = %v", err)
	}
	if canLogin(user, "second-password") {
		t.Error("disabled client can still log in")
	}
	if err := c.EnableClient(ctx, user); err != nil {
		t.Fatalf("EnableClient() error = %v", err)
	}
	if !canLogin(user, "second-password") {
		t.Error("re-enabled client cannot log in")
	}

	if err := c.DeleteClient(ctx, user); err != nil {
		t.Fatalf("DeleteClient() error = %v", err)
	}
	if err := c.DeleteClient(ctx, user); !errors.Is(err, ErrNotFound) {
		t.Errorf("second DeleteClient() error = %v, want ErrNotFound", err)
	}
	if _, err := c.ClientRoles(ctx, user); !errors.Is(err, ErrNotFound) {
		t.Errorf("ClientRoles() of deleted client error = %v, want ErrNotFound", err)
	}

	if err := c.DeleteRole(ctx, role2); err != nil {
		t.Errorf("DeleteRole(role2) error = %v", err)
	}
}

// sameStringSet reports whether a and b contain the same strings, ignoring order.
func sameStringSet(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	counts := make(map[string]int, len(a))
	for _, s := range a {
		counts[s]++
	}
	for _, s := range b {
		counts[s]--
	}
	for _, n := range counts {
		if n != 0 {
			return false
		}
	}
	return true
}

// fakeMessage is a minimal mqtt.Message: only Payload() matters for do(), the rest are no-ops.
type fakeMessage struct {
	payload []byte
}

func (m *fakeMessage) Duplicate() bool   { return false }
func (m *fakeMessage) Qos() byte         { return 0 }
func (m *fakeMessage) Retained() bool    { return false }
func (m *fakeMessage) Topic() string     { return responseTopic }
func (m *fakeMessage) MessageID() uint16 { return 0 }
func (m *fakeMessage) Payload() []byte   { return m.payload }
func (m *fakeMessage) Ack()              {}

// fakeConn captures the Subscribe handler and, on Publish, replies asynchronously: first a
// well-formed answer for the same command but with a DIFFERENT correlationData (simulating
// another client's broadcast answer, or a stale one), then the real matching answer.
type fakeConn struct {
	handler mqtt.MessageHandler
}

func (f *fakeConn) Subscribe(_ string, handler mqtt.MessageHandler) error {
	f.handler = handler
	return nil
}

func (f *fakeConn) Publish(_ string, payload []byte) error {
	var env struct {
		Commands []struct {
			Command         string `json:"command"`
			CorrelationData string `json:"correlationData"`
		} `json:"commands"`
	}
	if err := json.Unmarshal(payload, &env); err != nil {
		return err
	}
	if len(env.Commands) != 1 {
		return fmt.Errorf("fakeConn: expected 1 command, got %d", len(env.Commands))
	}
	cmdName, corrData := env.Commands[0].Command, env.Commands[0].CorrelationData

	go func() {
		foreign, _ := json.Marshal(map[string]any{
			"responses": []map[string]any{{"command": cmdName, "correlationData": "someone-else-999"}},
		})
		f.handler(nil, &fakeMessage{payload: foreign})

		real, _ := json.Marshal(map[string]any{
			"responses": []map[string]any{{"command": cmdName, "correlationData": corrData, "error": "Client already exists"}},
		})
		f.handler(nil, &fakeMessage{payload: real})
	}()
	return nil
}

// TestDoSkipsForeignAndStaleAnswers proves that do() only accepts the answer whose
// correlationData matches the command it sent: a well-formed, same-command answer with a
// different correlationData arrives first and must be ignored, so the call sees the second,
// matching answer's error instead of silently succeeding.
func TestDoSkipsForeignAndStaleAnswers(t *testing.T) {
	fc := &fakeConn{}
	c, err := New(fc)
	if err != nil {
		t.Fatalf("New() error = %v", err)
	}
	c.timeout = 2 * time.Second

	err = c.CreateClient(context.Background(), "someone", "pw", "role")
	if !errors.Is(err, ErrAlreadyExists) {
		t.Fatalf("CreateClient() error = %v, want ErrAlreadyExists (the foreign answer must be skipped)", err)
	}
}

func TestErrorFor(t *testing.T) {
	tests := []struct {
		msg  string
		want error
	}{
		{"Role already exists", ErrAlreadyExists},
		{"ACL with this topic already exists", ErrAlreadyExists},
		{"Client already exists", ErrAlreadyExists},
		{"Client not found", ErrNotFound},
		{"Role not found", ErrNotFound},
	}
	for _, tt := range tests {
		if err := errorFor(response{Command: "x", Error: tt.msg}); !errors.Is(err, tt.want) {
			t.Errorf("errorFor(%q) = %v, want %v", tt.msg, err, tt.want)
		}
	}
	if err := errorFor(response{Command: "addClientRole", Error: "Internal error"}); err == nil || errors.Is(err, ErrAlreadyExists) || errors.Is(err, ErrNotFound) {
		t.Errorf("errorFor(Internal error) = %v, want a plain error", err)
	}
	if err := errorFor(response{Command: "x"}); err != nil {
		t.Errorf("errorFor(no error) = %v, want nil", err)
	}
}
