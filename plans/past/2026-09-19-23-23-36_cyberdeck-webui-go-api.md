---
plan_id: 2026-09-19-23-23-36_cyberdeck-webui-go-api
title: Cyberdeck web UI (static Apache dashboard) + Go API — bootstrap System tab
summary: Add a Golang metrics API (thermal zones, RAM, / disk, fan, CPU, uptime) behind a systemd unit and a static Chart.js dashboard with a bootstrap "System" tab that polls every 5 s; layout is api/go (code + build/install pipeline) and api/www (static files setup.sh installs to /var/www/html once apache2 is confirmed); provisioned through setup.sh with a documented contract change and live apply->verify.
status: past
created_at: 2026-09-19-23-23-36
---

Key: `[ ]` pending task, `[x]` completed task, `[?]` needs validation, `[-]` closed task

# Cyberdeck web UI + Go API — bootstrap System tab

Repo goal: the deck is inspectable over the network at a glance — a static
dashboard (Apache, default root `/var/www/html`) fed by a small Go API —
starting with a single **System** tab (zone-temperature card + per-minute/
hour chart, RAM card, /-partition card) that refreshes every 5 seconds.

## Repo layout

- `api/go/` — the Go API codebase **and its build/install pipeline**:
  `go.mod`, source files, `systemd/cyberdeck-api.service`, and `install.sh`
  (`go build` -> `/opt/cyberdeck/cyberdeck-api`, install the unit,
  `systemctl daemon-reload enable --now cyberdeck-api`; idempotent — invoked
  by `setup.sh`).
- `api/www/` — the static dashboard: `index.html`, `main.js`, `style.css`,
  `vendor/chart.umd.js`. The **contents** of this directory are installed by
  `setup.sh` to `/var/www/html/` (so `/var/www/html/index.html` is the page
  root) — only after/apache2 installation is confirmed.
- `api/apache/cyberdeck.conf` — the one managed Apache conf (the `/api/`
  proxy), enabled via `a2enconf cyberdeck`. The only directory under `api/`
  beyond the two named above; it keeps the conf a verifiable template
  instead of a heredoc inline in `setup.sh`.

## Design decisions (session 2026-09-19)

1. **Go API = stdlib only** (`net/http`, no framework, no external modules),
   one static binary built from `api/go` (`go build`), runs as
   `cyberdeck-api.service`, binds **127.0.0.1:8080** only. Go comes from apt
   (`golang-go`) so the install is reproducible in `setup.sh`.
2. **Apache serves the static dashboard from its default root
   `/var/www/html`** (no DocumentRoot override) and **proxy-passes `/api/`
   to 127.0.0.1:8080** (a2enmod `proxy proxy_http` + the managed conf), so
   the browser talks to one origin (no CORS, API stays loopback-private).
   The distro's default `index.html` (the package placeholder page) is
   expected to be replaced by the deck dashboard on first deploy.
3. **Chart.js is vendored locally** (`api/www/vendor/chart.umd.js`,
   downloaded once at provisioning, version + checksum pinned) — no CDN; the
   deck may be offline.
4. **Sampling model:** the API ticks every 5 s into an in-memory ring of
   720 samples (one hour). `GET /api/history?minutes=60` aggregates 5 s
   samples into per-minute points (60 points × all series) **and appends a
   live in-progress "now" point**, so the chart's last point moves every
   5 s even though the buckets are per-minute.
5. **History is not persisted** — after a reboot the chart fills back in over
   the next hour. Honest, zero storage. (Persistence is a later plan.)
6. **Read-only sysfs sources**, all probed live on this board:
   - 8 thermal zones, types `cpub_thermal_zone`, `cpul_thermal_zone`,
     `cpul_idle_zone`, `cpub_idle_zone`, `ddr_thermal_zone`, `gpu_thermal_zone`,
     `npu_thermal_zone`, `skin_zone`; values are millidegrees under
     `/sys/class/thermal/thermal_zone*/temp` -> API reports `°C` at 0.1
     precision. **No hardcoded zone list**: the API discovers zones at
     startup, so an added/renamed zone shows up automatically.
   - RAM from `/proc/meminfo` (kB): `MemTotal`, `MemFree`, `MemAvailable`,
     `SwapTotal`, `SwapFree`. "Used" = Total − Available.
   - Disk via `syscall.Statfs("/")`: total / used / free bytes + percent
     (board's `/` is 29 G UFS, was **89 % full** at plan time — the UI
     color-codes the disk bar >80 %).
   - `load1/5/15` from `/proc/loadavg`; uptime from `/proc/uptime`.
   - CPU utilization % from a 2-sample delta of `/proc/stat` per tick.
   - Fan: `/sys/class/hwmon/hwmon1/pwm1` (0–255 duty) and
     `/sys/devices/virtual/thermal/cooling_device9/cur_state` (0–4).
     The API reads hwmon name (`pwmfan`) to lock the right hwmon id, not a
     bare index.
7. **Endpoints:**
   - `GET /health` -> `{ "ok": true }` (Apache verify probe target)
   - `GET /api/metrics` -> current snapshot: `ts`, `temps{}`, `mem{}`,
     `disk{}`, `cpu_util_pct`, `load{}`, `uptime_s`, `fan{pwm, state}`,
     `hostname`
   - `GET /api/history?minutes=60` -> `{ "step_s": 60, "series":
     [{ "t", "temps{}`, "mem_used_bytes", "disk_used_bytes" } ...] }`
     (last point is the live partial bucket)
   - everything else on :8080 -> 404; no write endpoints (action endpoints
     are a later plan)
8. **Dashboard layout (System tab, single tab for now):**
   - header line: deck hostname (browser URL host — no API field needed),
     uptime chip, **freshness dot** (green <10 s, amber <30 s, red stale)
   - **Temps card**: 8 zone chips (label + °C), color-ramped green->red
   - directly under it, **Chart.js line chart**: one line per zone,
     per-minute for the last hour (60 points + live point), 5 s refresh,
     legend click to toggle lines
   - **Memory card**: used / free / available / total with a horizontal bar;
     swap row under it
   - **Disk (/) card**: used / free / total, percent bar, red >80 %
   - **Fan card**: duty % (pwm/255) + cooling state N/4 — the thermal
     response half of the temps story
   - 5 s polling loop: `metrics` and `history` on every tick; request
     failures update the freshness dot only (never blank the last good data)
   - dark cyberdeck theme, no framework
9. **Provisioning is `setup.sh`** (the repo mandate). A new managed section,
   in this order: apt `golang-go` + `apache2` -> **confirm apache2
   installed** (dpkg + service check) -> `a2enmod proxy proxy_http` +
   `a2enconf cyberdeck` -> run `api/go/install.sh` (build + unit) -> deploy
   `api/www/**` contents -> `/var/www/html/`. All of it converges (re-run
   changes nothing when already converged). `docs/setup.md` is updated
   **in the same change** (mandate) and the change must pass `apply ->
   verify` on the live board before commit.
10. **Auth/TLS: deliberately out of scope** — trusted-LAN HTTP on :80.
    Deferred with the action endpoints (reboot/service toggles) to a follow-up
    plan.

## Tasks

- [x] 1. Build the System-tab dashboard + API (bootstrap, under `api/`).
  - [ ] 1.1 Go API (`api/go/`, module name to be set in `go.mod`, e.g.
        `deck.cyberdeck/api`), codebase **and** its build/install pipeline.
    - [x] 1.1.1.1 Scaffolding: `api/go/go.mod` (go version matching apt
          `golang-go`), `api/go/main.go` — env `CYBERDECK_ADDR`
          (default `127.0.0.1:8080`), `main` assembles handlers, graceful
          shutdown on SIGTERM.
    - [x] 1.1.1.2 Sampler: 5 s ticker goroutine -> ring buffer of 720
          `{ts, temps, mem, disk, cpu_util, load, fan}`; every read source
          wrapped so a missing sysfs path degrades to `null` for that field
          (API never crashes on a missing sensor).
    - [x] 1.1.1.3 Temp discovery at startup (`/sys/class/thermal/thermal_zone*`
          -> type names); snapshot + history carry the discovered set.
    - [x] 1.1.1.4 `/health` handler (JSON `{"ok":true}`).
    - [x] 1.1.1.5 `/api/metrics` handler (current tick, JSON schema of §7).
    - [x] 1.1.1.6 `/api/history?minutes=60` handler — per-minute buckets
          (avg of the enclosing 5 s samples) over the last N minutes, plus
          the live partial bucket as the final point.
    - [x] 1.1.1.7 CPU utilization: keep prior `/proc/stat` aggregate jiffies,
          delta per tick; first tick reports `null`.
    - [x] 1.1.1.8 Unit file `api/go/systemd/cyberdeck-api.service` (static,
          `User=root`, `ExecStart=/opt/cyberdeck/cyberdeck-api`,
          `Restart=on-failure`, `WantedBy=multi-user.target`).
    - [x] 1.1.1.9 `api/go/install.sh` — the build/install pipeline:
          `go build -o /opt/cyberdeck/cyberdeck-api` (cache-skip via a
          source-hash marker so converged re-runs don't rebuild), install
          unit, `systemctl daemon-reload`, `systemctl enable --now
          cyberdeck-api`; exits non-zero on any failure; idempotent.
  - [ ] 1.2 Apache conf + vendored chart.
    - [x] 1.2.1.1 `api/apache/cyberdeck.conf`: **default DocumentRoot kept**
          (serves `/var/www/html`) + `ProxyPass /api/` /
          `ProxyPassReverse /api/` -> `http://127.0.0.1:8080/api/` (**the
          `/api/` prefix is preserved** — the Go mux registers its data
          routes under `/api/`, so a bare-root target would strip it and the
          API would answer 404; found + fixed on the first live converge,
          2026-09-20); no other virtual-host behavior changes.
    - [x] 1.2.1.2 Vendor Chart.js (pin version + checksum) into
          `api/www/vendor/chart.umd.js` (tracked in git) — download once when
          the file is created, not at every provision.
  - [ ] 1.3 Web UI (`api/www/`: `index.html`, `main.js`, `style.css`,
        `vendor/`) — the contents are what `setup.sh` installs into
        `/var/www/html/` (so `index.html` is the page root).
    - [x] 1.3.1.1 Tab shell + System tab; dark theme; card grid layout.
    - [x] 1.3.1.2 Header: URL-host label, uptime chip, freshness dot.
    - [x] 1.3.1.3 Temps card (zone chips) + per-minute/hour line chart
          (Chart.js, one series per zone, live-point update on 5 s tick,
          no full-chart re-render — `chart.update()` data mutation).
    - [x] 1.3.1.4 Memory card: used/free/available/total + bar, swap row.
    - [x] 1.3.1.5 Disk card: used/free/total + percent bar (red >80 %).
    - [x] 1.3.1.6 Fan card: duty % + cooling state.
    - [x] 1.3.1.7 5 s poller (setInterval, single-flight guard) for
          `/api/metrics` + `/api/history`; stale-state handling keeps last
          good data visible and turns the dot red.
  - [ ] 1.4 Provisioning (`setup.sh`).
    - [x] 1.4.1.1 `setup.sh` managed section, in order: apt `golang-go` +
          `apache2` -> **confirm apache2 installed** (dpkg status + service
          present, fail visibly otherwise) -> `a2enmod proxy proxy_http` +
          `a2enconf cyberdeck` -> `bash api/go/install.sh` -> deploy
          `api/www/**` contents to `/var/www/html/` (replace the distro's
          default `index.html` — expected on this image); all idempotent
          (re-run: no changes when converged). Apache's *loaded* state
          converges via a sig file (`apache.loaded-sig`), not via "wrote a
          conf/module THIS run": run 3 exposed that a conf-content-only
          change left Apache serving the previous proxy target until a
          manual reload (and run 1's interrupted build would never have
          triggered one).
    - [x] 1.4.1.2 `setup.sh` verify rows: `cyberdeck-api.service` active;
          `curl 127.0.0.1:8080/health` ok; `curl localhost/api/metrics`
          (through Apache) has `temps.cpub_thermal_zone` present;
          `curl localhost/` serves the dashboard (e.g. `<!doctype html>` +
          a marker string from the page) and **not** the distro placeholder.
    - [x] 1.4.1.3 `docs/setup.md`: document the new managed scope (`api/`
          layout, units, ports 80/8080, `/var/www/html` deploy, idempotency,
          troubleshooting) **in the same change** (mandate); `AGENTS.md`
          link audit (doc already linked).
    - [x] 1.4.1.4 `.vscode/tasks.json`: "GamePi: set up the machine" remains
          the single entrypoint (setup.sh) — no new task; note the new
          managed scope only if the existing task description names scopes.
- [x] 2. Verify.
  - [x] 2.1 Unit-level.
    - [x] 2.1.1.1 `gofmt` + `go vet` + `go build` clean in `api/go` (also
          `go test` — a regression test for the `readCPUJiffy` parse below).
    - [x] 2.1.1.2 Run the binary with a temp addr; `curl /health`,
          `/api/metrics`, `/api/history?minutes=60`; assert JSON fields,
          8 temp keys, history length ≈ 61 incl. live point, per-minute
          bucketing (all asserted 2026-09-20; this pass also caught the
          `SplitN(…, 1)` bug in `readCPUJiffy` — it returned the whole file
          as one line, so `cpu_util_pct` was permanently nil; fixed +
          guarded by the unit test).
    - [x] 2.2 Live board. (2026-09-20: runs #1–5 below, 2.2.1.1–3 all green)
    - [x] 2.2.1.1 `sudo bash setup.sh --plan` from the current state
          (read-only; record exactly what it would install/change for this
          feature — apt go + apache, the conf, the unit, the `/var/www/html`
          deploy), then `sudo bash setup.sh --yes` -> `RESULT: CONVERGED`.
          (Live, 2026-09-20: `--plan` first pass recorded `DRIFT:16` — the
          full web stack pending; four `--yes` runs carried it to
          CONVERGED: #1 apt + first apply, failed at the then-missing Go
          toolchain build; #2 applied everything (row 16 FAILED — the
          conf's `ProxyPass` target stripped the `/api/` prefix the Go mux
          keeps, found from Go's 404 body passing through Apache; fixed
          in same change); #3 rewrote the conf + rebuilt/installed the
          binary with the `readCPUJiffy` SplitN fix, but exited
          INCOMPLETE:verify because apache's LOADED copy stayed stale —
          the `web_changed` reload gate missed conf-content writes; that
          hole became the sig-stamped reload gate in 1.4.1.1's follow-up);
          #4 `systemd: reloading apache2 (loaded state differs from
          on-disk conf/modules)` (first stamp) -> rows 14–17 all PASS,
          `no file changes (byte-stable)`, `RESULT: CONVERGED`, exit 0.
          Post-check through Apache: `/api/metrics` full JSON, `/` the
          dashboard, `/api/history?minutes=5` = 6 pts step 60.)
    - [x] 2.2.1.2 Re-run: no file churn, still converged (re-run contract).
          (Live, 2026-09-20, run #5: every artifact `unchanged` incl. the
          conf + unit; `unchanged apache2 (loaded state matches on-disk
          conf/modules)` — no reload; rows 14–17 PASS; `no file changes
          (byte-stable)`; `RESULT: CONVERGED`, exit 0. apache2 worker
          etime 14:52 -> 30 s across run #4 proved the reload landed and
          the loaded copy is the prefix-preserving conf.)
    - [x] 2.2.1.3 Browser: open `http://<deck>/`; confirm all four cards,
          chart renders, values move on the 5 s tick, dot stays green;
          `curl localhost/api/metrics` through Apache works.
          (Live, 2026-09-20, via Apache on the board — the same origin a
          LAN client hits: all four cards rendered with live data —
          8 temp rows, mem 20.4 %, disk 89.4 % with the >80 % warning ▲,
          fan duty 100 % state 4/4 (governor); chart canvas 211x260
          backing store with 53/340 sampled pixels non-transparent
          (Chart.js lines carry sparse ink — non-zero proves a draw); two
          DOM samples 7 s apart — every temp row changed
          (e.g. ddr 46.2->45.5, gpu 46.3->47.3), mem 20.4->21.3 %, uptime
          chip 11h 34m -> 11h 35m (5 s tick live); freshness dot "• live"
          (green) in both samples. `curl localhost/api/metrics` through
          Apache: full JSON, row 16 PASS.)
  - [x] 2.3 Governance. (journal `journal/2026-09-20-webui-go-api-live.md`;
          indexes regenerated after the `current -> past` move; promotion
          `future -> current` was done before the first implementation edit
          on 2026-09-19; hardware-record reuse noted in the journal)
    - [x] 2.3.1.1 Journal entry (design + live results); plan indexes
          regenerated (`python agentic-pipelines/scripts/regenerate_plan_indexes.py --repo-root .`);
          promote plan `future -> current` before first implementation edit;
          hardware records: no new sensor research expected (all sources
          already documented in `docs/hardware/soc-thermal.md` + `fan.md`) —
          note the reuse in the journal instead.

## Deferred (explicit, for a follow-up plan)

- [-] 3.1 Auth/TLS (or tunnel-first access) for the HTTP surface.
- [-] 3.2 Action endpoints (reboot, service start/stop) + UI buttons — needs
      the safety story from 3.1 first.
- [-] 3.3 History persistence across reboots (append-only ring file).
- [-] 3.4 More tabs: Network (IP/Wi-Fi), NPU (VPM activity), Audio (engine
      status), Battery (blocked on the missing fuel-gauge work in
      `docs/hardware/battery.md`).
- [-] 3.5 History charts for RAM/disk (the ring already carries them).
- [-] 3.6 Fan control UI (write `pwm1`) — needs the DTB-overlay policy work
      in `docs/hardware/fan.md` first; do not bypass the kernel governor
      from the dashboard.

  (All six intentionally closed with this plan, 2026-09-20: the 3.x scope is
  the explicit backlog for the next webui plan — 3.1 before anything that
  widens the HTTP surface beyond read-only; 3.6 gated on the fan DTB work.)