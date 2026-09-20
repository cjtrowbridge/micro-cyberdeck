module deck.cyberdeck/api

// Matches apt `golang-go` on Armbian 13 (trixie) — the toolchain setup.sh
// provisions, so the build stays reproducible in setup.sh with no external
// modules (stdlib only by design).
go 1.24