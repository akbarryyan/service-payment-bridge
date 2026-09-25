package mqttclient

import (
	"fmt"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
)

type Client struct {
	client mqtt.Client
}

func Connect(brokerURL, username, password string) (*Client, error) {
	opts := mqtt.NewClientOptions().
		AddBroker(brokerURL).
		SetConnectTimeout(10 * time.Second).
		SetAutoReconnect(true)

	if username != "" {
		opts.SetUsername(username)
	}
	if password != "" {
		opts.SetPassword(password)
	}

	client := mqtt.NewClient(opts)
	token := client.Connect()
	if !token.WaitTimeout(10 * time.Second) {
		return nil, fmt.Errorf("mqtt connect timeout after 10s")
	}
	if err := token.Error(); err != nil {
		return nil, fmt.Errorf("mqtt connect failed: %w", err)
	}

	return &Client{client: client}, nil
}

func (c *Client) Disconnect() {
	c.client.Disconnect(250)
}

func (c *Client) IsConnected() bool {
	return c.client.IsConnected()
}

// Publish sends payload to topic at QoS 1 (at-least-once — reasonable
// default per brainstorm-q161-updated.md Section 25.1).
func (c *Client) Publish(topic string, payload []byte) error {
	token := c.client.Publish(topic, 1, false, payload)
	if !token.WaitTimeout(5 * time.Second) {
		return fmt.Errorf("mqttclient: publish to %q timed out", topic)
	}
	return token.Error()
}

// Subscribe registers handler for topic at QoS 1.
func (c *Client) Subscribe(topic string, handler mqtt.MessageHandler) error {
	token := c.client.Subscribe(topic, 1, handler)
	if !token.WaitTimeout(10 * time.Second) {
		return fmt.Errorf("mqttclient: subscribe to %q timed out", topic)
	}
	return token.Error()
}
