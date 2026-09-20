---
plan_id: 2026-09-20-10-47-32_browser-ssh-vnc-bridges
title: Browser SSH + VNC — noVNC and ShellInABox bridges on the dashboard
summary: Make the deck's SSH and VNC reachable from a stock browser with no client software: ShellInABox (native SSH tunnel + PAM login, 0.0.0.0:4200) and noVNC (websockify WS bridge over the existing x11vnc :5900, 0.0.0.0:6080), both reachable same-origin through the existing Apache origin on :80 at /shell/ and /vnc.html — with a dashboard Access card linking both. The two new listeners bind to 0.0.0.0 as directed (trusted-LAN posture). The distro's `novnc` deb is NOT installed (its nodejs+net-tools deps pull ~106 MB we never run); the noVNC client is vendored into api/www/vnc/ and the only new apt packages are `websockify` and `shellinabox`. Provisioned through setup.sh per the mandate, documented in docs/setup.md in the same change, and proven by live apply -> verify before commit. User-directed follow-up of 2026-09-20 (decision 8): the ShellInABox client is dark-themed in the dashboard palette via a managed `--css` file (verify row 24).
status: past
created_at: 2026-09-20-10-47-32
---

Key: `[ ]` pending task, `[x]` completed task, `[?]` needs validation, `[-]` closed task

# Browser SSH + VNC — noVNC and ShellInABox bridges

Repo goal: a user on the LAN can open the dashboard at
`http://micro-cyberdeck.local/` and, from a link on the page, get **SSH**
(in-browser terminal) and **VNC** (in-browser desktop) with no client
software. As directed, **everything binds `0.0.0.0`** — the new listeners
included (trusted-LAN posture, same as `sshd :22` and `x11vnc :5900`
today). The dashboard's links stay same-origin through the single Apache
origin on `:80`.

Grounded research (all facts verified live on this board, 2026-09-20):

- On the wire today: `sshd` 0.0.0.0:22, `x11vnc` 0.0.0.0:5900 (pw file
  `/home/cj/.vnc/passwd`, unit `gamepi-vnc.service`). Ports **6080 and 4200
  are free**.
- apt `shellinabox 2.21+b3` — 1 package, ~0.5 MB, no systemd unit (only a
  sysvinit script + conffile `/etc/default/shellinabox`). The binary
  `/usr/bin/shellinaboxd` serves the login page at its port
  (`<title>Shell In A Box</title>`), authenticates via
  **`pam_start("shellinabox", …)`** (hardcoded in the binary; the deb
  ships **no** `/etc/pam.d/shellinabox`, so we must manage one), and
  implements its `SSH` service as a **native SSH tunnel** to the target —
  with the default `LOGIN` service on the SSH app, a browser session *is*
  an SSH session. Flags of note: `-p PORT` (def 4200), `-t
  --disable-ssl`, `--localhost-only` (default is NOT set — the stock init
  script already binds the LAN, matching this plan's 0.0.0.0 posture, so
  we pass nothing here), `-b --background=PIDFILE` (there is no
  foreground flag), `--service` (default = `/:LOGIN`).
- apt `websockify 0.12.0+dfsg1-4+b1` = meta + `/usr/bin/websockify`
  (python3). apt `novnc 1:1.6.0-2` ships **only static client files**
  (`/usr/share/novnc/**`, ~1.4 MB) but **Depends: nodejs, net-tools,
  websockify** — ~106 MB of js toolchain we would never run.
- The vendored noVNC client's default connect settings (host empty, port
  0, path `websockify`) build the WebSocket URL **relative to
  `location.href`** — i.e. same-origin `ws://<origins-host>/websockify` —
  which Apache can proxy to loopback with `mod_proxy_wstunnel`
  (`mod_proxy_wstunnel.so` is on disk, not yet enabled). Its page and
  assets use relative refs (`app/…`), so it serves from `/vnc.html`
  verbatim (`<title>noVNC</title>`).
- Disk: `/` is 29 G, 91 % used, ~2.8 G free. Vendoring the noVNC client
  into `api/www/vnc/` (~1.4 MB deployed) + installing `websockify`
  (~9.4 MB) + `shellinabox` (~0.5 MB) avoids the bulk of the nodejs
  chain.

## Design decisions

1. **ShellInABox on 0.0.0.0:4200, default LOGIN service.** Unit
   `gamepi-shellinabox.service` runs `shellinaboxd -q --background=
   /run/shellinabox/shellinaboxd.pid --disable-ssl --port 4200` as the
   deb's own `shellinabox` system user; `RuntimeDirectory=shellinabox`
   for the pidfile. The bind is `0.0.0.0` — the stock init script's
   default (no `--localhost-only`), as directed; :4200 is directly
   LAN-reachable in addition to the same-origin :80 dashboard path. The
   browser's session shell is a **native SSH login** (PAM: same
   credential as real SSH; logged to utmp/wtmp by default).
   **1b. Vendor sysvinit takeover (2026-09-20, from the first live apply).**
   The `shellinabox` deb ships an **enabled** sysvinit script
   (`S01shellinabox` rc links + conffile `SHELLINABOX_DAEMON_START=1`)
   that starts a second daemon on 4200 at boot; with both present
   whichever loses the race bounces
   `Failed to find any available port!` (observed live: our unit hit its
   start-limit while the vendor's stray — pidfile
   `/var/run/shellinaboxd.pid` — held the port, and
   `systemctl cat shellinabox` showed the auto-generated
   `shellinabox.service` from `systemd-sysv-generator`, `enabled`). setup.sh
   now owns a **§(d3) takeover**: `write_if_changed
   /etc/default/shellinabox` with `SHELLINABOX_DAEMON_START=0` (keeps the
   conffile format; the vendor script's `d_start` early-returns on 0) and,
   when S-start links or a stray `shellinaboxd` exist, `update-rc.d
   shellinabox disable` + `/etc/init.d/shellinabox stop` +
   `systemctl reset-failed gamepi-shellinabox` (the vendor script's own
   stop, `start-stop-daemon --oknodo`, is safe to call always). The K01
   stop links stay — no-ops once nothing starts. Plan-mode twin: one
   `DRIFT shellinabox-sysv` item.
2. **Managed PAM file.** `write_if_changed /etc/pam.d/shellinabox`:
   `pam_env` + `auth include common-auth` + `account/password/session
   include common-*`. Without it `pam_start("shellinabox")` has nothing to
   read and every login fails. (First login on the live board is the
   validation for this row — see 5.3.)
3. **noVNC via a websockify bridge on 0.0.0.0:6080** — NOT the distro
   `novnc` deb (nodejs/net-tools deps, ~106 MB of unused toolchain). Unit
   `gamepi-websockify.service` runs `/usr/bin/websockify 0.0.0.0:6080
   127.0.0.1:5900` — the listen side is all interfaces, as directed
   (websockify's default bind; stated explicitly so row 16 can assert
   it). The target side stays a loopback dial to the existing `x11vnc
   :5900`.
   The client page/assets are **vendored into `api/www/vnc/`** (the whole
   `/usr/share/novnc` tree, version-pinned in a `SOURCE` note with the
   deb's sha256) and deploy to `/var/www/html/vnc/` with the existing
   www-deploy loop; the page is reachable at **`/vnc.html`** (single-file
   client, no path collision with `/` root, no `Alias` needed).
4. **One browser origin: Apache on :80.** `cyberdeck.conf` gains a
   `proxy_wstunnel` block (`ProxyPass /vnc/websockify ws://127.0.0.1:6080/websockify` — the vendored client page is served from the docroot path `/vnc/`, and its `defaults.json` opens the WebSocket at page-relative `websockify`, so the proxy path must be `/vnc/websockify`)
   and an `http` block — one plain `ProxyPass /shell/` →
   `http://127.0.0.1:4200/` (trailing-slash pair). No `ProxyPassMatch`: the
   2.21 terminal page is XHR-POST, not WebSocket, and every in-page relative
   path (the assets, the `command` POST) maps cleanly under `/shell/`, so the
   plain proxy covers everything. The dashboard path stays
   same-origin via :80; the new ports are additionally directly
   LAN-reachable (their 0.0.0.0 binds, decisions 1 and 3).
   `a2enmod proxy_wstunnel` is added to the managed module set and
   `proxy_wstunnel` to `apache_desired_sig`'s module list (written as
   `wstunnel` at design time — the 2026-09-20 first live apply died with
   `ERROR: Module wstunnel does not exist!`; Debian's .load file is
   `proxy_wstunnel.load`, the .so is `mod_proxy_wstunnel`, and the a2enmod
   name is `proxy_wstunnel`; renamed in the script + docs in the same fix
   that found it). The conf change alters the sig → the existing reload
   gate loads it; no new reload machinery.
5. **Dashboard Access card.** A new card (same visual language as the
   System tab) with two links — *Desktop (VNC)* → `{origin}/vnc.html`,
   *Terminal (SSH)* → `{origin}/shell/` — built from
   `window.location.origin` in `main.js` (works over IP **and**
   `micro-cyberdeck.local`) — implementation: plain static links in
   `index.html` (`href="/vnc/vnc.html"`, `href="/shell/"` — absolute paths
   work identically over IP and `.local`, so `main.js` is untouched), styled
   in `style.css`. No new API
   surface: the links are static, the data endpoints are untouched.
6. **Trusted-LAN posture, stated.** Both bridges add browser-reachable
   authentication surfaces with the deck's existing credentials (PAM/SSH
   user for the terminal, the 8-char VNC password for the desktop) on
   unauthenticated HTTP :80 — and, with the 0.0.0.0 binds, the same
   surfaces directly on :4200/:6080. This is the same trust baseline as
   the existing SSH (:22) / VNC (:5900) exposure, and it inherits the
   auth/TLS + action-endpoint deferral carried over from plan
   2026-09-19-23-23-36 (its §3.1) — the deferral is restated below rather
   than re-discussed.
7. **Provisioning is `setup.sh` (the repo mandate)**: `websockify` +
   `shellinabox` join the `PKGS` array (`novnc` deliberately excluded,
   with an in-line comment); the two units generate/write in the existing
   `gamepi-*` unit idiom (compare-install + the §(g) repair loop); the PAM
   file is a managed root:root 644 artifact. `docs/setup.md` is updated
   **in the same change** (mandate) and the change passes `--plan` (clean)
   → `--yes` (CONVERGED) → re-run (no churn) → live browser proof before
   commit.
8. **Dark theme for the ShellInABox client (follow-up, 2026-09-20, user
   directive: “set shellinabox to dark theme”).** Grounding found the deb
   ships **no client files on disk** — the light stylesheet is baked into
   the binary — so theming is server-side: `shellinaboxd --css=FILE`
   *appends* the file's content after the built-in css, so at equal
   specificity the theme wins, and the content is embedded in the css the
   daemon serves (`/shell/styles.css` through Apache) — making the theme
   curl-verifiable (row 24). `--user-css=STYLES` was rejected: it is a
   per-session option menu, not a fixed deploy-time posture. The theme is
   `api/shellinabox/shellinabox-dark.css` (the deck palette; the shipped
   selectors were grounded from a live `curl` of the built-in css + login
   page; tuned 16-color ANSI ramp; the built-in 256-color ramp left as
   shipped), compare-then-written to `/usr/local/lib/shellinabox-dark.css`
   (0644 root; `/usr/local`because apt never touches it, and it sits
   outside the unit's `-c` datadir), with `--css $SHELLINABOX_CSS_DST` on
   the unit ExecStart. The existing unit-restart gate (CHANGED_FILES)
   re-applies the theme whenever the unit file changes this run, and
   §(d2b) writes the file before §(f)/§(g) run.

## Tasks

- [x] 1. Provisioning: packages, units, PAM, Apache wiring.
  - [x] 1.1 `setup.sh` PKGS: add `websockify`, `shellinabox` (comment:
        `novnc` excluded — nodejs/net-tools deps, ~106 MB unused; client is
        vendored in `api/www/vnc/`).
  - [x] 1.2 Unit `gamepi-websockify.service` (ExecStart decision 3;
        `Restart=always`); written through the existing unit idiom +
        §(g) repair loop.
  - [x] 1.3 Unit `gamepi-shellinabox.service` (decision 1; after gamepi-vnc
        not required — independent of X, but stays in the same unit loop).
  - [x] 1.4 `write_if_changed /etc/pam.d/shellinabox` (decision 2).
  - [x] 1.5 `a2enmod proxy_wstunnel` + `proxy_wstunnel` in
        `apache_desired_sig`'s module list (written as `wstunnel` at design
        time; first live apply died with `ERROR: Module wstunnel does not
        exist!` — Debian's .load file is `proxy_wstunnel.load`; renamed on
        2026-09-20).
  - [x] 1.6 `api/apache/cyberdeck.conf`: `/vnc/websockify` (WS) + `
        /shell/` (plain proxy — no `ProxyPassMatch`, see decision 4), each
        with a comment stating the loopback target and why it is same-origin.
- [x] 2. Vendor the noVNC client.
  - [x] 2.1 Copy the deb's `/usr/share/novnc/**` into `api/www/vnc/`
        (version 1.6.0-2; whole tree — the client's relative asset layout
        is load-bearing).
  - [x] 2.2 `api/www/vnc/SOURCE`: provenance — Debian package
        `novnc 1:1.6.0-2`, its sha256, what was copied and why (pins the
        vendor against silent drift).
- [x] 3. Dashboard Access card.
  - [x] 3.1 `index.html`: Access card markup (Desktop VNC + Terminal SSH
        links, `id` anchors).
  - [-] 3.2 `main.js`: populate link hrefs/titles from
        `window.location.origin` — superseded: implemented as plain static
        `href="/vnc/vnc.html"` / `href="/shell/"` links in `index.html`
        (absolute paths work identically over IP and `.local`), so
        `main.js` is untouched (decision 5 note).
  - [x] 3.3 `style.css`: card + link styling consistent with the System
        tab.
- [x] 4. Documentation (same change as 1–3, mandate).
  - [x] 4.1 `docs/setup.md`: scope list (two new units + PAM file +
        `proxy_wstunnel` mod + conf rows), a Browser access section (what
        each bridge is,
        which port is loopback-only, credential model), and the new
        verification-matrix rows (18–23 below).
  - [x] 4.2 `README.md` pointer (one line: browser SSH + VNC via the
        dashboard Access card) if the README carries the feature list.
- [ ] 5. Live apply -> verify (user runs `sudo bash setup.sh`).
  - [x] 5.1 `--plan` first (expect the new drift items; exit 1, nothing
        written — the plan-mode idiom). Ran with the bridge templates:
        the expected new drift items listed, exit 1, nothing written.
  - [x] 5.2 `--yes`: packages install; both units + PAM + conf applied;
        verify rows 1–17 stay PASS; rows 18–23 PASS. Result: applied with
        2026-09-20 evidence — the first live bridge apply exposed both
        defects (wrong a2enmod name; vendor sysv squat — decision 1b +
        4-note) — and then `--yes` ran again clean: `RESULT: DRIFT:130`
        (apply-mode = 130 files changed this run, **exit 0**; the 130 = 128
        www files + conffile + the websockify unit re-applied because the
        a2enmod-name fix touched its template comment), all 23 verify rows
        PASS, `[setup] systemd: reloading apache2` fired. (Note: an earlier
        `--yes` run of 2026-09-20 exited 0 against the pre-bridge on-disk
        script; the board still carried no bridge artifacts — the first
        clean `--plan` after this script lands was a legitimate DRIFT run.)
  - [x] 5.2a no-churn `--plan` — first pass: 1 red item, exposed a
        **detector blind spot, not a board defect**: the detector excluded
        only `MainPID`, but shellinaboxd's `-q --background=` keeps a
        parent+child pair (ppid 489033 → 489032 = `MainPID`), so the daemon's
        own child looked like a vendor stray → permanent `DRIFT:1` with the
        board otherwise fully clean (only K01 stop links remain, vendor unit
        inactive, :4200 ours, conffile `DAEMON_START=0`). Fix: exempt the
        whole unit tree (walk pid → ppid → … → `MainPID`). Post-fix re-run
        (operator, 2026-09-20): `RESULT: CONVERGED`, exit 0 — every managed
        artifact `unchanged`, zero `shellinabox*` drift items, 23/23 verify
        rows PASS (incl. row 19 WS `101`, row 20 PAM login page), vendor
        sysv state untouched. Mandate loop closed.
  - [x] 5.3 Terminal proof (browser, 2026-09-20, operator session): the
        dashboard's `/shell/` link served the PAM login page; typing the
        deck user (`cj`) + Enter produced `cj@micro-cyberdeck's password:`
        — the Apache → shellinaboxd → PAM chain accepted a real session
        (validation for 1.4: the managed PAM stack is being read). The
        password was deliberately not entered by the agent — final auth
        into the live shell is the user's act.
  - [x] 5.4 Desktop proof (browser, 2026-09-20, operator session): the
        dashboard's `/vnc/vnc.html` page loaded with assets; the noVNC
        Connect bar → **VNC password prompt** ("Server asked for
        credentials") — the full Apache `proxy_wstunnel` WS chain (row 22)
        reached `x11vnc` and returned an RFB auth request over the tunnel
        (the noVNC client can't be screenshot-verified pre-auth; the
        prompt IS the chain proof). The VNC password was deliberately not
        entered by the agent — final auth into the live desktop is the
        user's act.
  - [x] 5.5 No-churn proof — the post-5.2a re-run IS the no-churn proof:
        every managed artifact reported `unchanged`, 23/23 verify PASS,
        `RESULT: CONVERGED`, exit 0 — an apply run would write nothing and
        restart nothing. (The mandate loop is `--plan` → `--yes` →
        no-churn `--plan`; the final no-churn `--plan` is 5.2a post-fix.)
- [x] 6. Governance close-out.
  - [x] 6.1 Journal `journal/2026-09-20-browser-ssh-vnc-bridges.md` (runs,
        live proofs, any deltas from this plan; the dark-theme follow-up).
  - [x] 6.2 Checkboxes to final state; `regenerate_plan_indexes.py`.
  - [x] 6.3 Commit (setup.sh + docs/setup.md + api/apache + api/www +
        api/shellinabox + plan/journal in the mandated scope) + push; plan
        to `past/` after the commit with its index regenerated.
- [x] 7. Dark-theme follow-up (user-directed, 2026-09-20 — "set shellinabox
        to dark theme"; small additive change on the approved surface —
        decision 8; its live proof gates 6.1–6.3).
  - [x] 7.1 Theme file `api/shellinabox/shellinabox-dark.css` (deck palette,
        grounded selectors from the live served css + login page; tuned
        16-color ANSI; shipped 256-color ramp left as-is).
  - [x] 7.2 setup.sh — constants `SHELLINABOX_CSS_{SRC,DST}`, compare-then-
        write to `/usr/local/lib/shellinabox-dark.css` (§(d2b), plan-mode
        twin `DRIFT shellinabox-css-src`), and `--css $SHELLINABOX_CSS_DST`
        on the shellinabox unit (the §(g) gate re-applies it on unit
        change; §(d2b) writes the file before §(f)/§(g) run). `--user-css`
        rejected (per-session option, not a fixed posture).
  - [x] 7.3 docs/setup.md — scope-table row, byte-canonical paragraph,
        unit-row `--css` note, verify matrix row 24 (mandate: same change).
  - [x] 7.4 Live prove — done (operator, 2026-09-20): `--plan` = DRIFT
        naming exactly the css file + the shellinabox unit (row 24 FAIL
        while stale — correct negative control); `--yes` = both applied,
        `restarting gamepi-shellinabox.service (unit file written this run)`,
        24/24 PASS incl. row 24, `RESULT: DRIFT:2` exit 0 (drift = 2 changed
        files, apply semantics); re-`--plan` = all `unchanged`, 24/24 PASS,
        `RESULT: CONVERGED` exit 0. Browser: served `/shell/` computes
        `background-color: rgb(11,14,20)` = `#0b0e14` on `body`/`#vt100`/
        `#console`; the `micro-cyberdeck login:` prompt renders in deck ink
        on the dark page (screenshot in the journal).

## New verification-matrix rows (exact assertions, run as root)

| # | Check | Method (live board) |
|---|---|---|
| 18 | Dashboard access links | `curl -sf http://localhost/` contains the Access card's two routes (`/vnc/vnc.html` and `/shell/`) |
| 19 | websockify bridge on 0.0.0.0:6080 | `ss -lnt` shows an all-interface bind (`0.0.0.0`/`*`) on `:6080` (a 127.0.0.1 bind is drift) AND a hand-rolled WS upgrade GET to `127.0.0.1:6080` is answered `101 Switching Protocols` (a plain GET gets 405 — only a real WS client passes) |
| 20 | ShellInABox bridge on 0.0.0.0:4200 | `systemctl is-active gamepi-shellinabox.service == active` AND `ss -lnt` shows an all-interface bind on `:4200` AND `curl -sf http://localhost/shell/` (through Apache) serves the ShellInABox PAM login page |
| 21 | PAM file managed | `/etc/pam.d/shellinabox` exists, non-empty, mode 644 root:root |
| 22 | WS proxy wired | `/etc/apache2/mods-enabled/proxy_wstunnel.load` exists AND `grep -q 'ws://127.0.0.1:6080' /etc/apache2/conf-enabled/cyberdeck.conf` (the end-to-end upgrade is proven by the live browser check 5.4 — a curl handshake is not scriptable here) |
| 23 | noVNC client served | `curl -sf http://localhost/vnc/vnc.html` contains `<title>noVNC</title>`; its WebSocket target is the same-origin `/vnc/websockify` (row 22) |
| 24 | ShellInABox dark theme (decision 8) | `/usr/local/lib/shellinabox-dark.css` exists non-empty **and** the dashboard bg marker `0b0e14` (absent from the shipped light css) appears in the css `shellinaboxd` serves at `http://localhost/shell/styles.css` through Apache — `--css=FILE` is embedded into the served stylesheet, so the grep is direct proof the theme reaches the browser (a stale unit without the flag serves the light css and this row FAILs) |

## Deferred (intentionally out of scope)

- [-] A1 — Auth/TLS for the :80 surface and the directly exposed
      :4200/:6080 (both bridges inherit the deferral from
      2026-09-19-23-23-36 §3.1; the browser shells/VNC run on the deck's
      existing SSH/VNC credentials over plain HTTP).
- [-] A2 — `wss://` TLS termination for the noVNC WebSocket (arrives with A1).
- [-] A3 — VNC password pre-fill/autoconnect in the dashboard link (would
      put the VNC password in the page URL — not doing that).
- [-] A4 — Per-user session isolation on ShellInABox (one shared daemon,
      each client authenticates its own login — no extra isolation
      beyond PAM is modeled here).

## Approval

**Requested:** explicit approval of the plan (this file) before Task 1.
Approving = the changes in Tasks 1–4 land in one change with docs
(mandate), Tasks 5 are run live with the user driving `sudo`, and Task 6
closes it out. Any design-decision change re-opens approval.

**Post-approval addendum (2026-09-20):** the dark-theme follow-up
(decision 8, task 7) was **user-directed after approval** ("set shellinabox
to dark theme") — an additive change on the already-approved ShellInABox
surface (one managed css file, one unit flag, one verify row, doc sync),
recorded here under the addendum rather than silently inside the original
scope.