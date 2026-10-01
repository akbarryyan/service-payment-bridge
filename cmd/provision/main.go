// Command provision registers and revokes Q161 Pro devices (see docs/running-locally.md):
//
//	go run ./cmd/provision add -merchant MT58530503 -sn 00078020709 [-out docs/mqttcfg.dat]
//	go run ./cmd/provision revoke -sn 00078020709 [-delete]
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io/fs"
	"os"
	"time"

	"github.com/joho/godotenv"

	"service-payment-bridge/internal/database"
	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/dynsec"
	"service-payment-bridge/internal/mqttclient"
	"service-payment-bridge/internal/provisioning"
)

const usage = `usage:
  provision add    -merchant <manjoMerchantId> -sn <SN> [-out <file>] [-tenant <id>] [-store-id <id>] [-terminal-id <id>]
  provision revoke -sn <SN> [-delete]`

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, usage)
		os.Exit(2)
	}
	for _, file := range []string{".env", ".env.sandbox"} {
		if err := godotenv.Load(file); err != nil && !errors.Is(err, fs.ErrNotExist) {
			fmt.Fprintf(os.Stderr, "provision: load %s: %v\n", file, err)
			os.Exit(1)
		}
	}

	var err error
	switch os.Args[1] {
	case "add":
		err = runAdd(os.Args[2:])
	case "revoke":
		err = runRevoke(os.Args[2:])
	default:
		fmt.Fprintln(os.Stderr, usage)
		os.Exit(2)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "provision:", err)
		os.Exit(1)
	}
}

func runAdd(args []string) error {
	fs := flag.NewFlagSet("add", flag.ExitOnError)
	merchant := fs.String("merchant", "", "Manjo merchant ID (merchants.manjo_merchant_id)")
	sn := fs.String("sn", "", "device hardware serial number")
	// docs/ is where the user keeps device files for Downtool; mqttcfg.dat is git-ignored.
	out := fs.String("out", "docs/mqttcfg.dat", "where to write mqttcfg.dat (contains the device password)")
	tenant := fs.String("tenant", "", "tenant ID (optional)")
	storeID := fs.String("store-id", "", "Manjo store ID (optional)")
	terminalID := fs.String("terminal-id", "", "Manjo terminal ID (optional)")
	fs.Parse(args)
	if *merchant == "" || *sn == "" {
		return errors.New("add needs -merchant and -sn\n" + usage)
	}
	server, port := os.Getenv("MQTT_DEVICE_SERVER"), os.Getenv("MQTT_DEVICE_PORT")
	if server == "" || port == "" {
		return errors.New("MQTT_DEVICE_SERVER and MQTT_DEVICE_PORT must be set (see .env.example)")
	}

	return withProvisioner(provisioning.Config{DeviceServer: server, DevicePort: port}, func(ctx context.Context, p *provisioning.Provisioner) error {
		res, err := p.Add(ctx, provisioning.AddRequest{
			ManjoMerchantID: *merchant, SN: *sn, TenantID: *tenant, StoreID: *storeID, TerminalID: *terminalID,
		})
		if err != nil {
			return err
		}
		if err := os.WriteFile(*out, res.ConfigFile, 0o600); err != nil {
			return fmt.Errorf("write %s: %w", *out, err)
		}
		state := "new device"
		if !res.Created {
			state = "existing device reactivated, password rotated"
		}
		fmt.Printf("provisioned %s for merchant %s (%s)\n", *sn, *merchant, state)
		fmt.Printf("  mqtt user : %s\n", res.Username)
		fmt.Printf("  receives  : %s\n", res.Topic)
		fmt.Printf("  sends     : %s\n", res.RequestTopic)
		fmt.Printf("  config    : %s — contains the device password: load it with Downtool, then delete it\n", *out)
		return nil
	})
}

func runRevoke(args []string) error {
	fs := flag.NewFlagSet("revoke", flag.ExitOnError)
	sn := fs.String("sn", "", "device hardware serial number")
	remove := fs.Bool("delete", false, "also delete the broker client and role")
	fs.Parse(args)
	if *sn == "" {
		return errors.New("revoke needs -sn\n" + usage)
	}

	return withProvisioner(provisioning.Config{}, func(ctx context.Context, p *provisioning.Provisioner) error {
		missing, err := p.Revoke(ctx, *sn, *remove)
		if err != nil {
			return err
		}
		fmt.Printf("revoked %s: device INACTIVE", *sn)
		if *remove {
			fmt.Print(", broker client and role deleted")
		}
		fmt.Println()
		if missing {
			fmt.Println("  note: the device had no broker login (registered before per-device accounts)")
		}
		return nil
	})
}

// withProvisioner connects to the DB and, as the provisioner account, to the broker's
// internal listener. Admin commands must never cross a network unencrypted.
func withProvisioner(cfg provisioning.Config, run func(context.Context, *provisioning.Provisioner) error) error {
	dbURL, brokerURL := os.Getenv("DATABASE_URL"), os.Getenv("MQTT_BROKER_URL")
	user := os.Getenv("MQTT_PROVISIONER_USERNAME")
	if user == "" {
		user = "provisioner"
	}
	pass := os.Getenv("MQTT_PROVISIONER_PASSWORD")
	if dbURL == "" || brokerURL == "" || pass == "" {
		return errors.New("DATABASE_URL, MQTT_BROKER_URL and MQTT_PROVISIONER_PASSWORD must be set")
	}

	ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
	defer cancel()

	pool, err := database.NewPool(ctx, dbURL)
	if err != nil {
		return err
	}
	defer pool.Close()

	conn, err := mqttclient.Connect(brokerURL, user, pass)
	if err != nil {
		return fmt.Errorf("connect broker as %s: %w", user, err)
	}
	defer conn.Disconnect()
	broker, err := dynsec.New(conn)
	if err != nil {
		return err
	}

	return run(ctx, provisioning.New(sqlc.New(pool), broker, cfg))
}
