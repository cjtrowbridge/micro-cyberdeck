# 2026-09-20 — Web UI + metrics API: live convergence on the board

Plan `plans/past/2026-09-19-23-23-36_cyberdeck-webui-go-api.md` (promoted
`future -> current` on 2026-09-19 before the first implementation edit;
promoted `current -> past` at close-out, 2026-09-20).

## What landed

- `api/go/` — stdlib-only Go metrics API (no external modules):
  `main.go` (routes + 5 s sampling loop), `sampler.go` (8 auto-discovered
  thermal zones, mem, disk, load, uptime, fan duty/state, CPU utilization),
  `handlers.go` (`/health`, `/api/metrics`, `/api/history`), `install.sh`
  (source+binary sha marker for skip-rebuild, unit compare-install,
  start/restart/enable branches, `/health` probe). Binds 127.0.0.1:8080
  only; runs as `cyberdeck-api.service`.
- `api/apache/cyberdeck.conf` — default DocumentRoot kept; `/api/`
  proxied to `127.0.0.1:8080/api/` (**prefix preserved** — see bug #1).
- `api/www/` — dark System-tab dashboard: temps (wide, chart), mem, disk,
  fan cards; 5 s polling; vendored Chart.js 4.4.1 (sha256 + size pinned).
- `setup.sh` — managed §(h) + verify rows 14–17; `docs/setup.md` same-change
  updates (mandate).

## Live runs (all `sudo bash setup.sh`, on this board)

- `--plan` (initial record): `RESULT: DRIFT:16` — the entire web stack
  pending (apt golang-go + apache2, conf, unit, service start, www deploy,
  verify rows).
- `--yes` #1: apt + apply started; failed at the Go build (`INCOMPLETE`
  before verify) — gofmt/source issues in the fresh tree; fixed in sources.
- `--yes` #2: full apply (apt, conf, a2enmod/a2enconf, build, unit, www).
  Row 16 `api-proxy` **FAILED** — the conf's `ProxyPass /api/ ->
  http://127.0.0.1:8080/` stripped the `/api/` prefix the Go mux registers
  under; the 404 body passed through Apache was Go's `404 page not found`
  (direct `:8080/api/metrics` was 200). Fixed the conf target in the same
  change + `docs/setup.md` + plan annotation.
- `--yes` #3: conf rewritten + binary rebuilt (now also carrying the
  `readCPUJiffy` fix, bug #2) + unit installed; service active+enabled+
  healthy; **but** apache's loaded copy stayed stale — the `web_changed`
  reload gate only tripped on www-deploy writes, and none occurred, so no
  reload fired (worker etime 14:52). `RESULT: INCOMPLETE:verify` (row 16).
- `--yes` #4: **`systemd: reloading apache2 (loaded state differs from
  on-disk conf/modules)`** — the first stamp write of the new sig-stamped
  reload gate (bug #3 fix) — rows 14–17 all PASS, `no file changes
  (byte-stable)`, `RESULT: CONVERGED`, exit 0. Worker etime 14:52 -> 30 s
  confirmed the reload landed.
- `--yes` #5 (no-churn re-run, 2.2.1.2): every artifact `unchanged`;
  `unchanged apache2 (loaded state matches on-disk conf/modules)` (no
  reload); rows 14–17 PASS; `RESULT: CONVERGED`, exit 0.

## Three live bugs, found only by converging on the board

1. **Proxy prefix strip** (conf): `ProxyPass /api/ -> :8080/` vs Go routes
   under `/api/`. Fix: target `:8080/api/` + in-conf comment explaining the
   404; `docs/setup.md` scope row updated.
2. **`readCPUJiffy` used `strings.SplitN(data, "\n", 1)`** — returns the
   whole file as one element, so the aggregate-cpu scan never matched and
   `cpu_util_pct` was permanently nil. Fix: split all lines, break on the
   aggregate line; regression test `sysfs_test.go:TestReadCPUJiffy`
   (total>0, idle<=total, non-nil). gofmt/wet/build/test clean.
3. **Reload gate hole** (`setup.sh` §(h)): `web_changed` was set by the
   www compare-deploy and `a2enconf` symlink — not by the conf **content**
   write — so a run that fixed only the conf (and exited before verify
   passed) left Apache serving the stale loaded copy forever. Fix:
   converged-reload by signature — `apache_desired_sig()` = hash of the
   on-disk `cyberdeck.conf` + the proxy/proxy_http `mods-enabled` markers +
   the conf-enabled symlink, stamped to
   `/var/lib/micro-cyberdeck/apache.loaded-sig` after every reload/start;
   the gate reloads when stamp != sig, stamps unconditionally, and in plan
   mode reports a pending `apache-loaded` DRIFT item. Deliberately **not**
   in `CHANGED_FILES`: reload + stamp are bookkeeping, so a converged
   re-run changes no bytes and reports `RESULT: CONVERGED`. Design note:
   this also repairs past runs that wrote the conf and exited before their
   own reload (exactly runs #1/#3). `docs/setup.md` updated in the same
   change (scope rows + phase-2 (h) paragraph + Web UI proxy bullet).

## Verification evidence (2.1 + 2.2.1.3)

- Unit-level: 8 temp keys present; `/api/history?minutes=60` = 61 points
  (60 minute buckets + live in-progress point), timestamp minute-aligned
  RFC3339; `cpu_util_pct` populating post-fix (live 41–48 % on the board).
- Browser (via Apache, the browser origin): all four cards render with
  live data (8 temp rows, mem ~20 %, disk 89.4 % with the >80 % warning ▲,
  fan duty 100 % state 4/4); chart canvas backing store has non-zero ink
  (drawn, not blank — Chart.js lines are sparse, ~53/340 sampled px);
  two DOM samples 7 s apart show every temp row and the mem % changed and
  the uptime chip advanced (the 5 s tick is live); freshness dot green
  ("live") in both samples.
- Verify rows 14–17 PASS on the final two consecutive runs.

## Hardware records

No new sensor research: every sysfs source the API reads is already
documented — `docs/hardware/soc-thermal.md` (the 8 thermal zones, zone
types, millidegree scales) and `docs/hardware/fan.md` (hwmon1/`pwm1`,
cooling_device9, kernel governor; no user-space pwm path on this kernel —
the fan card is display-only for that reason). No `docs/hardware/` changes;
no README Hardware Status row moves.

## Lifecycle

Plan `current -> past` (all items `[x]` or intentionally closed `[-]`;
3.1–3.6 carry forward as the explicit backlog of the next webui plan —
3.1 before anything that widens the HTTP surface; 3.6 gated on the fan
DTB-overlay work). Indexes regenerated via
`python3 agentic-pipelines/scripts/regenerate_plan_indexes.py --repo-root .`.