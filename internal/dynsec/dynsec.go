// Package dynsec manages Mosquitto Dynamic Security accounts over MQTT, by publishing
// commands to $CONTROL/dynamic-security/v1 (see Soundbox/mqtt-poc/DYNAMIC-SECURITY.md).
//
// The response topic is broadcast to every subscriber, not just the client that issued a
// command: an answer meant for another connection (or a late answer to a timed-out call) can
// arrive on the same channel. Each command therefore carries a unique correlationData value,
// which the plugin echoes back, so do() can pick its own answer out of the broadcast traffic.
package dynsec

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"sync"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
)

const (
	controlTopic   = "$CONTROL/dynamic-security/v1"
	responseTopic  = controlTopic + "/response"
	defaultTimeout = 10 * time.Second
)

// ACL types used by this service.
const (
	ACLSubscribeLiteral     = "subscribeLiteral"
	ACLSubscribePattern     = "subscribePattern"
	ACLPublishClientSend    = "publishClientSend"
	ACLPublishClientReceive = "publishClientReceive"
)

var (
	// ErrAlreadyExists wraps "… already exists" answers (role, client, ACL).
	ErrAlreadyExists = errors.New("dynsec: already exists")
	// ErrNotFound wraps "… not found" answers (client, role).
	ErrNotFound = errors.New("dynsec: not found")
)

// Conn is the MQTT capability the client needs; *mqttclient.Client satisfies it.
type Conn interface {
	Publish(topic string, payload []byte) error
	Subscribe(topic string, handler mqtt.MessageHandler) error
}

// Client sends Dynamic Security commands. Safe for concurrent use, one command at a time.
type Client struct {
	conn      Conn
	mu        sync.Mutex
	responses chan []byte
	timeout   time.Duration
	prefix    string // random per-Client correlationData prefix
	counter   uint64 // next correlationData suffix; guarded by mu
}

// New subscribes to the response topic; conn must belong to an account allowed to use
// $CONTROL/dynamic-security/v1.
func New(conn Conn) (*Client, error) {
	prefix, err := randomHex(8)
	if err != nil {
		return nil, fmt.Errorf("dynsec: generate correlation prefix: %w", err)
	}
	// Buffered generously: the response topic is broadcast, so other clients' answers pass
	// through here too, not just this Client's own.
	c := &Client{conn: conn, responses: make(chan []byte, 32), timeout: defaultTimeout, prefix: prefix}
	err = conn.Subscribe(responseTopic, func(_ mqtt.Client, msg mqtt.Message) {
		select {
		case c.responses <- msg.Payload():
		default: // nobody is waiting; do() drains stale answers anyway
		}
	})
	if err != nil {
		return nil, fmt.Errorf("dynsec: subscribe %s: %w", responseTopic, err)
	}
	return c, nil
}

func randomHex(n int) (string, error) {
	b := make([]byte, n)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	return hex.EncodeToString(b), nil
}

type command map[string]any

type response struct {
	Command         string          `json:"command"`
	Error           string          `json:"error"`
	Data            json.RawMessage `json:"data"`
	CorrelationData string          `json:"correlationData"`
}

// do sends one command and waits for its answer. The response topic is broadcast to every
// subscriber, so do() tags each command with a unique correlationData value (which the plugin
// echoes back) and ignores any answer that doesn't carry it — whether it belongs to another
// client's command or is a late answer to a previous call of this Client's that already timed
// out or was cancelled. Only one command may be in flight per Client at a time.
func (c *Client) do(ctx context.Context, cmd command) (response, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	c.counter++
	corrData := fmt.Sprintf("%s-%d", c.prefix, c.counter)
	cmd["correlationData"] = corrData

drain:
	for {
		select {
		case <-c.responses:
		default:
			break drain
		}
	}

	body, err := json.Marshal(map[string][]command{"commands": {cmd}})
	if err != nil {
		return response{}, fmt.Errorf("dynsec: marshal %v: %w", cmd["command"], err)
	}
	if err := c.conn.Publish(controlTopic, body); err != nil {
		return response{}, fmt.Errorf("dynsec: publish %v: %w", cmd["command"], err)
	}

	timer := time.NewTimer(c.timeout)
	defer timer.Stop()
	for {
		select {
		case <-ctx.Done():
			return response{}, ctx.Err()
		case <-timer.C:
			return response{}, fmt.Errorf("dynsec: no answer to %v within %s", cmd["command"], c.timeout)
		case raw := <-c.responses:
			var env struct {
				Responses []response `json:"responses"`
			}
			if err := json.Unmarshal(raw, &env); err != nil || len(env.Responses) != 1 {
				continue // malformed or unrelated broadcast traffic; keep waiting for our answer
			}
			r := env.Responses[0]
			if r.CorrelationData != corrData {
				continue // another command's answer (foreign or stale); keep waiting for ours
			}
			return r, errorFor(r)
		}
	}
}

// errorFor maps Dynamic Security's error text to sentinel errors. Note: addClientRole for a
// role the client already has answers "Internal error", which is NOT mapped — check
// ClientRoles first instead of relying on it.
func errorFor(r response) error {
	switch {
	case r.Error == "":
		return nil
	case strings.HasSuffix(r.Error, "already exists"):
		return fmt.Errorf("%w: %s: %s", ErrAlreadyExists, r.Command, r.Error)
	case strings.HasSuffix(r.Error, "not found"):
		return fmt.Errorf("%w: %s: %s", ErrNotFound, r.Command, r.Error)
	default:
		return fmt.Errorf("dynsec: %s: %s", r.Command, r.Error)
	}
}

func (c *Client) exec(ctx context.Context, cmd command) error {
	_, err := c.do(ctx, cmd)
	return err
}

// CreateRole creates an empty role.
func (c *Client) CreateRole(ctx context.Context, role string) error {
	return c.exec(ctx, command{"command": "createRole", "rolename": role})
}

// AddRoleACL grants role the ACL aclType on topic.
func (c *Client) AddRoleACL(ctx context.Context, role, aclType, topic string) error {
	return c.exec(ctx, command{"command": "addRoleACL", "rolename": role, "acltype": aclType, "topic": topic, "allow": true})
}

// DeleteRole deletes a role.
func (c *Client) DeleteRole(ctx context.Context, role string) error {
	return c.exec(ctx, command{"command": "deleteRole", "rolename": role})
}

// CreateClient creates a login with one role.
func (c *Client) CreateClient(ctx context.Context, username, password, role string) error {
	return c.exec(ctx, command{
		"command":  "createClient",
		"username": username,
		"password": password,
		"roles":    []map[string]string{{"rolename": role}},
	})
}

// SetClientPassword replaces a login's password.
func (c *Client) SetClientPassword(ctx context.Context, username, password string) error {
	return c.exec(ctx, command{"command": "setClientPassword", "username": username, "password": password})
}

// EnableClient lets a disabled login connect again.
func (c *Client) EnableClient(ctx context.Context, username string) error {
	return c.exec(ctx, command{"command": "enableClient", "username": username})
}

// DisableClient stops a login from connecting, keeping the account.
func (c *Client) DisableClient(ctx context.Context, username string) error {
	return c.exec(ctx, command{"command": "disableClient", "username": username})
}

// DeleteClient deletes a login.
func (c *Client) DeleteClient(ctx context.Context, username string) error {
	return c.exec(ctx, command{"command": "deleteClient", "username": username})
}

// AddClientRole gives a login another role. Do not call it for a role the login already has.
func (c *Client) AddClientRole(ctx context.Context, username, role string) error {
	return c.exec(ctx, command{"command": "addClientRole", "username": username, "rolename": role})
}

// ClientRoles lists a login's roles.
func (c *Client) ClientRoles(ctx context.Context, username string) ([]string, error) {
	r, err := c.do(ctx, command{"command": "getClient", "username": username})
	if err != nil {
		return nil, err
	}
	var data struct {
		Client struct {
			Roles []struct {
				Rolename string `json:"rolename"`
			} `json:"roles"`
		} `json:"client"`
	}
	if err := json.Unmarshal(r.Data, &data); err != nil {
		return nil, fmt.Errorf("dynsec: getClient %s: %w", username, err)
	}
	roles := make([]string, 0, len(data.Client.Roles))
	for _, role := range data.Client.Roles {
		roles = append(roles, role.Rolename)
	}
	return roles, nil
}
