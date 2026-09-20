#!/usr/bin/env bash
# =============================================================================
# cyberdeck-api — build + install pipeline (invoked by setup.sh §(h)).
#
#   api/go/*.go                          --go build-->  /opt/cyberdeck/cyberdeck-api (static)
#   api/go/systemd/cyberdeck-api.service  --->           /etc/systemd/system/cyberdeck-api.service
#   + enable/(re)start + /health probe
#
# Idempotent: a source+binary hash marker skips the rebuild on converged
# re-runs (and a hand-replaced binary is detected); binary and unit are
# compare-then-install (identical bytes never touch disk); the service is
# (re)started only when the binary or the unit file actually changed — a new
# binary is a unit file change in disguise (same rule as gamepi-sound).
# Exits non-zero on any failure.
# =============================================================================

set -euo pipefail

# setup.sh invokes this with CWD = repo root; be safe from any CWD.
if [[ -f go.mod ]]; then
  SRC="$(pwd)"
else
  SRC="$(cd "$(dirname "$0")" && pwd)"
fi
cd "$SRC"

log() { printf '[cyberdeck-api] %s\n' "$*"; }

BIN=/opt/cyberdeck/cyberdeck-api
UNIT_SRC=systemd/cyberdeck-api.service
UNIT_DST=/etc/systemd/system/cyberdeck-api.service
MARKER=/var/lib/micro-cyberdeck/cyberdeck-api.src-hash

command -v go >/dev/null 2>&1 || { log "go toolchain missing (apt: golang-go)"; exit 1; }

# [1] build — skip the rebuild when converged: marker = hash(source) hash(binary)
# (xargs -0r: an empty find must not make sha256sum block reading stdin)
hash_src() {
  local list
  list="$(find . -type f \( -name '*.go' -o -name 'go.mod' -o -name 'go.sum' \) -print0 \
          | sort -z | xargs -0r sha256sum 2>/dev/null || true)"
  printf '%s\n' "$list" | sha256sum | awk '{print $1}'
}
need_build=1
cur_combined="$(hash_src) $(sha256sum "$BIN" 2>/dev/null | awk '{print $1}' || true)"
if [[ -f "$MARKER" && -x "$BIN" && "$(cat "$MARKER" 2>/dev/null || true)" == "$cur_combined" ]]; then
  need_build=0
fi
bin_written=0
if (( need_build )); then
  log "building (go $(go env GOVERSION), CGO off for a static binary) ..."
  tmp="$(mktemp /tmp/cyberdeck-api.XXXXXX)"
  CGO_ENABLED=0 go build -trimpath -o "$tmp" .
  if [[ -f "$BIN" ]] && cmp -s "$BIN" "$tmp"; then
    log "unchanged  $BIN (byte-identical rebuild)"
    rm -f "$tmp"
  else
    install -d -m 755 /opt/cyberdeck
    install -m 755 "$tmp" "$BIN"
    rm -f "$tmp"
    bin_written=1
    log "installed  $BIN"
  fi
  mkdir -p "$(dirname "$MARKER")"
  printf '%s\n' "$(hash_src) $(sha256sum "$BIN" 2>/dev/null | awk '{print $1}' || true)" > "$MARKER"
fi
(( bin_written == 1 )) && log "binary changed -> service will be restarted" || true

# [2] unit — compare-then-install
unit_written=0
if [[ ! -f "$UNIT_DST" ]] || ! cmp -s "$UNIT_SRC" "$UNIT_DST" 2>/dev/null; then
  install -m 644 "$UNIT_SRC" "$UNIT_DST"
  unit_written=1
  log "installed  $UNIT_DST"
else
  log "unchanged  $UNIT_DST"
fi
if (( unit_written == 1 )); then
  systemctl daemon-reload
fi

# [3] start / restart (only when something actually changed this run)
svc_state() { systemctl is-active cyberdeck-api.service 2>/dev/null || true; }
ENABLED_LINK=/etc/systemd/system/multi-user.target.wants/cyberdeck-api.service
if (( bin_written == 1 )); then
  systemctl enable cyberdeck-api.service >/dev/null
  systemctl restart cyberdeck-api.service
  log "restarted cyberdeck-api (new binary)"
elif (( unit_written == 1 )); then
  if [[ "$(svc_state)" == "active" ]]; then
    systemctl restart cyberdeck-api.service
    log "restarted cyberdeck-api (unit written this run)"
  else
    systemctl enable --now cyberdeck-api.service >/dev/null
    log "enabled + started cyberdeck-api"
  fi
elif [[ "$(svc_state)" != "active" ]]; then
  systemctl enable --now cyberdeck-api.service >/dev/null
  log "started cyberdeck-api (was not active)"
elif [[ ! -f "$ENABLED_LINK" ]]; then
  systemctl enable cyberdeck-api.service >/dev/null
  log "enabled cyberdeck-api (was running but not enabled)"
else
  log "unchanged  cyberdeck-api (active + enabled, converged)"
fi

# [4] health probe
for _ in $(seq 1 12); do
  if curl -sf --max-time 2 http://127.0.0.1:8080/health 2>/dev/null | grep -q '"ok":true'; then
    log "ok — cyberdeck-api healthy (/health)"
    exit 0
  fi
  sleep 1
done
log "cyberdeck-api not healthy after 12 s — journal tail:"
journalctl -u cyberdeck-api.service --no-pager -n 15 2>/dev/null | sed 's/^/    /' || true
exit 1