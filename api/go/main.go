// cyberdeck-api — the deck's at-a-glance metrics API.
//
// Stdlib only, one static binary (built by install.sh with CGO_ENABLED=0),
// bound loopback-only (CYBERDECK_ADDR, default 127.0.0.1:8080). Apache
// proxies /api/ to it (api/apache/cyberdeck.conf), so the browser talks to
// one origin and this API never binds a non-loopback interface.
//
// Endpoints (everything else is a 404 — no write endpoints; action
// endpoints + auth are deferred, plan 2026-09-19-23-23-36 §3):
//
//	GET /health        -> {"ok":true}
//	GET /api/metrics   -> current snapshot (temps/mem/disk/cpu/load/fan)
//	GET /api/history   -> per-minute series, last hour + live point
package main

import (
	"context"
	"errors"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"
)

func main() {
	addr := os.Getenv("CYBERDECK_ADDR")
	if addr == "" {
		addr = "127.0.0.1:8080"
	}

	s := newSampler()

	mux := http.NewServeMux()
	mux.HandleFunc("/health", handleHealth)
	mux.HandleFunc("/api/metrics", s.handleMetrics)
	mux.HandleFunc("/api/history", s.handleHistory)

	srv := &http.Server{
		Addr:              addr,
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second,
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer stop()

	// The sampler ticks every 5 s into the 720-sample ring (one hour).
	done := make(chan struct{})
	go s.run(ctx, done)

	errCh := make(chan error, 1)
	go func() {
		log.Printf("cyberdeck-api listening on %s (loopback only)", addr)
		errCh <- srv.ListenAndServe()
	}()

	select {
	case <-ctx.Done():
		log.Printf("shutdown signal received; closing listener")
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		if err := srv.Shutdown(shutdownCtx); err != nil {
			log.Printf("graceful shutdown: %v", err)
		}
		<-done // sampler exited before the process dies
		log.Printf("sampler stopped; bye")
	case err := <-errCh:
		if err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Printf("http server error: %v", err)
			os.Exit(1)
		}
	}
}
