// Package paymentpoller detects QRIS payments by regularly asking Manjo about every
// QR_GENERATED transaction whose next check is due, and announces the ones that got paid.
package paymentpoller

import (
	"context"
	"log/slog"
	"sync"
	"time"

	"github.com/jackc/pgx/v5/pgtype"

	"service-payment-bridge/internal/announcer"
	"service-payment-bridge/internal/database/sqlc"
	"service-payment-bridge/internal/transaction"
)

const (
	tick      = time.Second
	batchSize = 20
	workers   = 8

	// staleAnnouncementAge: a PAID transaction older than this when it's first claimed is
	// not announced — the amount would already be stale to whoever's standing at the
	// soundbox (e.g. right after a migration backfill, or a restart after downtime).
	staleAnnouncementAge = 10 * time.Minute
)

// PaymentChecker is satisfied by *transaction.Service.
type PaymentChecker interface {
	CheckPayment(ctx context.Context, tx sqlc.Transaction) (*transaction.PaymentCheckResult, error)
}

// PaymentAnnouncer is satisfied by *announcer.Announcer.
type PaymentAnnouncer interface {
	Announce(ctx context.Context, req announcer.Request) error
}

// Poller claims due transactions every second and checks them concurrently.
type Poller struct {
	q          *sqlc.Queries
	checker    PaymentChecker
	announcer  PaymentAnnouncer
	interval   time.Duration
	logger     *slog.Logger
	merchantID string // "" = all merchants
}

// New creates a Poller that re-checks a transaction every interval.
func New(q *sqlc.Queries, checker PaymentChecker, a PaymentAnnouncer, interval time.Duration, logger *slog.Logger) *Poller {
	return &Poller{q: q, checker: checker, announcer: a, interval: interval, logger: logger}
}

// OnlyMerchant restricts claims to one merchant. Integration tests use it so a poller run
// never touches other merchants' transactions in the shared dev database.
func (p *Poller) OnlyMerchant(merchantID string) *Poller {
	p.merchantID = merchantID
	return p
}

// Run polls every second until ctx is cancelled. The batch in flight when that happens
// is finished — announcements included — before Run returns, so a normal shutdown never
// leaves a transaction PAID but unannounced.
func (p *Poller) Run(ctx context.Context) {
	ticker := time.NewTicker(tick)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			p.RunOnce(context.WithoutCancel(ctx))
		}
	}
}

// RunOnce claims one batch of due transactions and processes it to completion.
func (p *Poller) RunOnce(ctx context.Context) {
	txs, err := p.q.ClaimDueTransactions(ctx, sqlc.ClaimDueTransactionsParams{
		PollInterval: pgtype.Interval{Microseconds: p.interval.Microseconds(), Valid: true},
		MerchantID:   pgtype.Text{String: p.merchantID, Valid: p.merchantID != ""},
		BatchSize:    batchSize,
	})
	if err != nil {
		p.logger.Error("payment poll claim failed", "error", err)
		return
	}

	sem := make(chan struct{}, workers)
	var wg sync.WaitGroup
	for _, tx := range txs {
		wg.Add(1)
		sem <- struct{}{}
		go func() {
			defer wg.Done()
			defer func() { <-sem }()
			// A panic anywhere in process (bug in the checker, announcer, etc.) must
			// stay confined to this one transaction — the poller, and the service it
			// runs in, must never go down because of it (spec §11).
			defer func() {
				if r := recover(); r != nil {
					p.logger.Error("payment check panicked", "transaction_id", tx.TransactionID, "panic", r)
				}
			}()
			p.process(ctx, tx)
		}()
	}
	wg.Wait()
}

func (p *Poller) process(ctx context.Context, tx sqlc.Transaction) {
	res, err := p.checker.CheckPayment(ctx, tx)
	if err != nil {
		p.logger.Error("payment check failed", "transaction_id", tx.TransactionID, "error", err)
		return
	}
	if res.QueryErr != nil {
		p.logger.Warn("payment query failed", "transaction_id", tx.TransactionID, "error", res.QueryErr)
	}
	if !res.Transitioned {
		return
	}

	done := res.Transaction
	switch done.Status {
	case sqlc.TransactionStatusPAID:
		if age := time.Since(paidAt(done)); age > staleAnnouncementAge {
			p.logger.Warn("stale payment not announced", "transaction_id", done.TransactionID, "device_id", done.DeviceID, "paid_at", paidAt(done))
			return
		}
		p.announcePaid(ctx, done, res.ManjoAmount, res.ManjoMerchantID)
	case sqlc.TransactionStatusEXPIRED:
		via := "manjo"
		if res.ExpiredByDeadline {
			via = "deadline"
		}
		p.logger.Info("transaction expired", "transaction_id", done.TransactionID, "via", via)
	case sqlc.TransactionStatusREFUNDED:
		p.logger.Warn("unpaid QR reported as refunded", "transaction_id", done.TransactionID)
	default:
		p.logger.Info("transaction closed", "transaction_id", done.TransactionID, "status", done.Status)
	}
}

// paidAt returns when tx was actually paid, falling back to when it was created if
// paid_at somehow wasn't set.
func paidAt(tx sqlc.Transaction) time.Time {
	if tx.PaidAt.Valid {
		return tx.PaidAt.Time
	}
	return tx.CreatedAt.Time
}

func (p *Poller) announcePaid(ctx context.Context, tx sqlc.Transaction, manjoAmount int64, merchantID string) {
	p.logger.Info("payment detected", "transaction_id", tx.TransactionID, "device_id", tx.DeviceID, "amount", tx.Amount)
	if manjoAmount != 0 && manjoAmount != tx.Amount {
		p.logger.Warn("amount mismatch", "transaction_id", tx.TransactionID, "amount", tx.Amount, "manjo_amount", manjoAmount)
	}

	err := p.announcer.Announce(ctx, announcer.Request{TransactionID: tx.TransactionID, MerchantID: merchantID, DeviceID: tx.DeviceID, Rupiah: tx.Amount})
	if err != nil {
		p.logger.Error("announcement failed", "transaction_id", tx.TransactionID, "device_id", tx.DeviceID, "error", err)
	}
}
