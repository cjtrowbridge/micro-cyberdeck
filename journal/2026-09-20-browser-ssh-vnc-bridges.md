# 2026-09-20 — browser-ssh-vnc-bridges

Plan: [plans/past/2026-09-20-10-47-32_browser-ssh-vnc-bridges.md](../plans/past/2026-09-20-10-47-32_browser-ssh-vnc-bridges.md) (status `past`, all tasks closed). User request: browser-access bridges — noVNC + ShellInABox provisioned through `setup.sh`, reachable from the dashboard, all listeners on `0.0.0.0` ("trusted-LAN posture", decision D6). One approved plan, one **user-directed post-approval follow-up** (dark-theme the ShellInABox client — decision 8).

## Research (evidenced — package archaeology, board date 2026-09-20)

- **`shellinabox` 2.21+b3** is a single ~0.5 MB deb. It ships **no unit file of any kind** — only a sysv init script plus an `/etc/default/shellinabox` conffile (`DAEMON_START=1`). The PAM service name `shellinabox` is hardcoded in the binary (`/etc/pam.d/shellinabox` must exist or the login chain silently degrades). There is no foreground flag: `--background=PIDFILE` is the daemonization mechanism. The build is transparent-TLS, so `--disable-ssl` pins the port to plain HTTP. Data dir via `-c /var/lib/shellinabox`. Its SSH bridge is a **native tunnel that spawns a real SSH session authenticated by PAM** — i.e. it runs the deck's existing SSH credentials, no separate auth model.
- **`novnc` 1:1.6.0-2** is a static-only client (~1.4 MB of web files) whose package Depends pull **nodejs + net-tools (~106 MB we would never run)**. Decided: vendor the client into `api/www/vnc/` (128 files, tracked) instead of apt; the only new packages are `websockify` and `shellinabox`. The Apache module `mod_proxy_wstunnel.so` was present-but-not-enabled on the board.
- **ShellInABox client theming (decision-8 research):** the deb ships **no client files on disk** — the light stylesheet is baked into the `shellinaboxd` binary. `--css=FILE` *appends* the file's content after the built-in css; at equal specificity the appended rules win. `--user-css=STYLES` was rejected: it opens a per-session option menu rather than a fixed deploy posture. Therefore a managed file `api/shellinabox/shellinabox-dark.css` → `/usr/local/lib/shellinabox-dark.css` (under `/usr/local` because package upgrades never touch it) + the unit's `--css` flag is the fixed-posture mechanism.

## First live apply: two defects

1. **Wrong module name.** First `--yes` died with `ERROR: Module wstunnel does not exist!`. The `a2enmod` name is **`proxy_wstunnel`** (`.load` file `proxy_wstunnel.load`, module object `mod_proxy_wstunnel.so`) — a renamed module that does not match the `wstunnel` guess. Fixed in the script and docs in the same change (setup converge §(h) + docs/setup.md).
2. **Vendor sysv init squatting the port.** The deb's sysv script started a second `shellinaboxd` on `:4200` at boot, so the managed daemon hit `Failed to find any available port!`, and systemd's **sysv-generator** invented a stale `shellinabox.service` on top. Converge §(d3) now takes it over: conffile driven to `DAEMON_START=0`, the vendor S-link removed, any stray vendor daemon stopped, `reset-failed` on the generated unit; the K01 script path becomes a no-op.

## The detector blind spot: `--background` parent + child

`shellinaboxd --background=PIDFILE` forks: the parent writes the pidfile and stays alive as a monitor, the child serves. The unit file's `PIDFile` exposes only the child (MainPID), so the drift detector's MainPID-only exclusion missed the parent and a **permanent `DRIFT:1`** appeared on clean boards. Fix: the unit is `Type=forking`, and the drift exclusion now walks the whole process tree (`pid → ppid → … → MainPID`) rather than comparing a single pid.

## Convergence (the mandate loop, both iterations)

Setup-result semantics (re-confirmed live): in **plan** mode `RESULT: DRIFT:<n>` = *n drift items* and exit 1; `RESULT: CONVERGED` exit 0 = nothing to do. In **apply** mode `RESULT: DRIFT:<n>` = *n files changed this run* and **exit 0 (success)** — the number means "converged by writing", not "left drifting".

- **Bridge loop:** `--plan` listed the expected new drift → `--yes` = `RESULT: DRIFT:130` exit 0 (the 128 vendored www files, the PAM file, the conffile, the units, etc.) → post-defect-fix re-run `--plan` = `RESULT: CONVERGED` exit 0 (plan 5.2a), every managed artifact `unchanged`, 23/23 verify rows PASS, vendor sysv state untouched. (An early same-day `--yes` had run against the **pre-bridge** on-disk script and exited 0 writing nothing bridge-related — the drift items that followed were the script's own, not drift on the board.)
- **Theme loop (decision 8):** `--plan` = `RESULT: DRIFT:3` — `[DRIFT]` the css file, `[DRIFT]` `gamepi-shellinabox.service`, `[FAIL]` row 24 (the daemon still serving the light css is the correct negative control) → `--yes` = `RESULT: DRIFT:2` exit 0, both files applied, `systemd: restarting gamepi-shellinabox.service (unit file written this run)` (the §(g) restart gate self-applied the new `--css` flag), 24/24 PASS **incl. row 24** → no-churn `--plan` = `RESULT: CONVERGED` exit 0, all `unchanged`.

Row 24 probes the **served stylesheet**, not the page: `curl http://localhost/shell/styles.css` and grep for the dashboard bg marker `0b0e14` (absent from the shipped light css). Because `--css=FILE` content is embedded in the css the daemon serves through Apache, that grep is direct proof the theme reaches the browser — a stale unit without the flag keeps serving the light css and the row FAILs.

**Why plan mode cannot see most of this:** apt install, `a2enmod`, unit files, and vendor-init state are all operator-level side effects — plan mode can only compare the managed file it would write. The live `--plan → --yes → --plan` loop is what actually verifies them; that is the point of the mandate, and this plan's two defects were caught by exactly it.

## Browser proofs (operator-driven; the agent never entered a password)

- Dashboard `http://192.168.1.250/` — the Access card links `/vnc/vnc.html` and `/shell/` (verify row 18).
- **noVNC** `/vnc/vnc.html` → Connect → "Server asked for credentials" — the full `proxy_wstunnel` WS chain carried an RFB auth request from x11vnc back through Apache (rows 19/22/23). The console shows two TLS warnings — the expected plain-HTTP consequence, deferred with A1/A2.
- **ShellInABox** `/shell/` → PAM login page (row 20) → typed `cj` + Enter → `cj@micro-cyberdeck's password:` — the Apache → shellinaboxd → PAM chain live.
- **Theme (decision 8):** the served `/shell/` computes `background-color: rgb(11, 14, 20)` on `body`/`#vt100`/`#console` — exactly `#0b0e14`, the dashboard `--bg`; the `micro-cyberdeck login:` prompt renders in deck ink on the dark page (screenshot taken 2026-09-20). Final authentication into the live shell is deliberately the user's act, not the agent's.

## Decision 8 — the dark theme (as landed)

- `api/shellinabox/shellinabox-dark.css` (3,894 B, brace-balanced) — the palette is `:root` from `api/www/style.css` (the dashboard is the color source of truth); `#0b0e14` bg / `#dbe2ee` ink; tuned 16-color ANSI in deck hues; the shipped 256-color ramp left as-is.
- Selectors were grounded against the **live served** css (`/tmp/siab_styles.css`, 31,139 B): keyboard keycaps are `b`/`i`/`s`/`u` elements under `#keyboard`, `.box` ships without a border, `.selected` is `#eeeeee`/`#888` — the first theme draft missed these and was corrected from the evidence.
- `setup.sh`: constants `SHELLINABOX_CSS_{SRC,DST}`, compare-then-write in converge §(d2b) (plan-mode twin `DRIFT shellinabox-css-src`; missing src in apply mode → `INCOMPLETE:web-src` exit 1), and `--css $SHELLINABOX_CSS_DST` on the unit; §(d2b) writes the file before §(f)/§(g) run, and the §(g) restart gate re-applies it whenever the unit changes.
- `docs/setup.md` synced in the same change (mandate): scope-table row, byte-canonical paragraph, unit-row `--css` note, matrix row 24.

## Lessons carried

- `a2enmod` names can disagree with module object names (`proxy_wstunnel` vs `mod_proxy_wstunnel.so`) — check `mods-available/*.load` before writing the name.
- A vendored sysv script + systemd-sysv-generator combo can squat a managed port at boot; takeover = conffile off + S-link removal + stray stop + `reset-failed`.
- Forking daemons with `--background`-style pidfiles need **tree-walk** (pid→ppid→…→MainPID) ownership checks, not MainPID-only.
- A mixed batch of file edits can silently drop an item — grep-verify **every** expected artifact on disk after any multi-edit batch (this caught a missing `--css` flag on the unit's ExecStart before it reached the board).
- `/usr/local` is the right home for managed files a package owns-but-never-touches: upgrades cannot clobber them, and they are clearly operator-managed.

## Deferred (intentionally out of scope, as decided)

- **A1** — Auth/TLS for the `:80` surface and the directly exposed `:4200`/`:6080` (both bridges run on the deck's existing SSH/VNC credentials over plain HTTP; inherits the deferral from 2026-09-19-23-23-36 §3.1).
- **A2** — `wss://` TLS termination for the noVNC WebSocket (arrives with A1).
- **A3** — VNC password pre-fill/autoconnect in the dashboard link (would put the password in the page URL — not doing that).
- **A4** — Per-user session isolation on ShellInABox (one shared daemon; each client authenticates its own PAM login — no extra isolation is modeled).