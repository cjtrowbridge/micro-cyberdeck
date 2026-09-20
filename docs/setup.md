# setup.sh — provisioning contract

This file is the single source of truth for what `setup.sh` is, what it
gathers/verifies, and what it must not do. **Any change to `setup.sh` — or to
the provisioning behavior this document describes — must be documented here in
the same change.** The script's header comment restates this requirement.

## Goal

Bring an Orange Pi Zero 3W "GamePi deck" board — fresh flash, partial state, or
drifted — to the configured working state, and leave it working. On a working
machine, a second run changes nothing, verifies everything, and exits cleanly.
The machine state always reflects either this document or a user edit that the
next repair run converges away.

## Managed scope (what setup.sh owns)

| File | Owner | Re-run behavior |
|---|---|---|
| `/boot/armbianEnv.txt` (two GamePi lines) | setup.sh | **Additive, byte-stable.** `user_overlays=` converges to the desired **set** of user overlays (order-insensitive; the current set is `{spi3-cs0-<MHZ>mhz}` — the buttons' `gpio-keys` overlay joins the set by extending `overlay_desired_set()` in §5, never by another writer); the line is rewritten only when the set differs. `overlays=` gets the stock `spi3-cs0-cs1-spidev` line appended only when missing. No `sed` rewrites of unrelated lines, ever. Hand edits to the GamePi lines are drift, repaired next run. |
| `/boot/overlay-user/<NAME>.dtbo` (older: `/boot/armbian-overlays/`) | overlay engine | Materialized by `armbian-add-overlay` from the user DTS in §5 of the script; Armbian writes user overlays to `/boot/overlay-user/` (the older name is still recognized when present). If missing while the env line exists, the script re-derives the DTS and re-runs `armbian-add-overlay` (reboot-requiring if the .dtbo bytes differ). |
| `/root/spi3-cs0-<MHZ>mhz.dts` | setup.sh | The user-space overlay source. Kept when byte-stable; regenerated + re-`armbian-add-overlay` when the DTS on disk differs from §5 (removing it does NOT silently drop the active overlay — the stamp mechanism below decides). |
| `/root/armbianEnv.txt.before-gamepi` | setup.sh | Created once, on the first overlay edit, as a one-time safety backup. Never churned again. |
| `/usr/local/bin/xvfb-to-st7789.py` | setup.sh | Compare-then-write against the §6 template. |
| `/usr/local/bin/hat-sound` | setup.sh | Rebuilt from the tracked `tools/hat-sound.c` (canonical flags, `-lm -lpthread`) on every run; **compare-then-install** — installed only when the bytes differ, and the `gamepi-sound` unit is restarted onto the new binary in the unit-repair step (a binary change is a unit change in disguise). |
| `/etc/systemd/system/gamepi-{xvfb,openbox,vnc,lcd,sound}.service` | setup.sh | Compare-then-write against the §6 templates. |
| `/run/gamepi-sound.sock` | `gamepi-sound` engine | **Transient, not setup.sh content.** `/run` is tmpfs — cleared at boot; the v3.6 daemon binds the socket at unit start (0666 via `fchmod(fd)` **and** `chmod(path)` — field finding 2026-09-15: on this vendor kernel/tmpfs a bare `fchmod` does not move the path-mode view that `connect()`/`stat()` use, so the engine does both), and unlinks it on exit. Verify row 12 probes its presence. No `RuntimeDirectory` in the unit — the socket sits directly in `/run`, which is wipe-cleared independently. |
| `/etc/sysctl.d/99-sched-rt.conf` | setup.sh | Compare-then-write: `kernel.sched_rt_runtime_us = -1`. Applied by `systemd-sysctl` (static) at every boot. The board has no `sysctl` binary; the runtime value is already `-1` here, the file is what makes it survive reboots (see Sound). |
| `/boot/overlay-user/{es8388-audio,i2c0}.dtbo` (older: `/boot/armbian-overlays/`) | setup.sh | **Removed** (logged) when present: wrong-audio-path probe leftovers — the HAT amp is GPIO/PWM pin 12, not I2S, so these bind nothing (verified 2026-09-15: no `i2c-0`, no extra ALSA card). Never re-added. |
| `<DECK_USER>/.config/openbox/autostart` | setup.sh | Compare-then-write against the §6 template (byte-strict). The auto-launched xterm is sized to fit the 960x960 :1 canvas (see the in-template comment in setup.sh §6: the Xvfb is 100 DPI, so `-fs` point-size + columns must stay ≤ ~952 px, else the window overflows the screen). |
| `<DECK_USER>/.vnc/passwd` | **the user** | Created (with the provided password, or a generated one) only if absent. Never overwritten, never deleted, no reset flag. Rotation is a manual act: `sudo rm <file>`, re-run with `--vnc-pass` (password is max 8 chars — VNC uses the first 8). |
| System packages + `default.target=multi-user` | setup.sh (idempotent apt/systemctl) | Re-asserted on every run; apt only runs when something is missing. |
| `/var/lib/micro-cyberdeck/*` (stamp + audit log) | setup.sh | Host-local bookkeeping (git-ignored). The stamp gates the reboot decision. Never hand-edit it; if it becomes suspect, delete the directory — the next run re-derives. |
| Ollama model library (local API `http://localhost:11434`) | setup.sh (host `ollama` CLI, `docker exec ollama` fallback) | **Verify-or-pull.** `OLLAMA_MODELS` (`qwen3.5:2b`, `qwen3.5:2b-q8_0`) must be in the local library (Ollama tag convention: exactly one colon separates name from tag — the Q8_0 variant is tag `2b-q8_0`, not `2b:q8_0`, which the registry rejects as `400 Bad Request: invalid model name`) — they are the models pipelines point at through `api.yaml` (Ollama runs **CPU-only** on this SoC; see `docs/hardware/npu.md`). The service install is **not** setup.sh's: on the current board `ollama serve` is a root **Docker container** (name `ollama`, 11434 published; model volume `/var/lib/docker/volumes/ollama/_data` ↔ `/root/.ollama` inside the container) and the host has no `ollama` CLI. Pulls try the host CLI first, then `docker exec ollama ollama pull <name>`; an unreachable API or an unresolvable pull path is reported as verify FAIL / plan DRIFT, not repaired. Absent models are pulled before the verify row runs; a successful second run therefore pulls nothing. `OLLAMA_HOST` overrides are drift — the deck's service is expected on localhost:11434. |

Hand edits to any "setup.sh-owned" file are **drift by definition**; the next
run repairs them. If you need a permanent custom change, change the template in
§5 of `setup.sh` (and document it here) — the script converges to its templates,
never to the filesystem.

The five `gamepi-*.service` files, the sound binary, the bridge script, the
sysctl drop-in, and the autostart are **byte-canonical with the verified live
configuration**: setup.sh templates them
from its §6 section and compare-then-write — identical bytes never touch disk,
so the script can be re-run without ever disturbing a healthy machine. One
deliberate, documented deviation: the `gamepi-lcd` unit carries
`Environment=PYTHONUNBUFFERED=1`. Root cause: Python3 block-buffers stdout
when not attached to a TTY, so under systemd the bridge's startup lines
(`X11 root: 960x960` etc.) never reached the journal unless the buffer filled
or the process died, which made the verify's journal proof item unreliable on
healthy long-running machines (observed 2026-09-14). First apply run after
this change writes the unit once and (re)starts it; afterwards the file is
byte-identical and the file is again byte-stable.

## Configuration knobs (script header)

| Flag | Default | Meaning |
|---|---|---|
| `--user <name>` | auto-detect | Deck user whose desktop the services run as (`$HOME` derived). Default: the non-root user running the script (on the live board: `cj`); without one, preflight fails with a `--user` hint. |
| `--hostname <h>` | `micro-cyberdeck` | Target hostname; a differing live hostname is reported as drift (plan mode) or set via `hostnamectl` (apply mode). |
| `--spi-mhz <n>` | `48` | SPI max frequency in MHz for the custom overlay. |
| `--vnc-pass <pw>` | generated | VNC password (only the first 8 chars are used); seeded **only when** `<home>/.vnc/passwd` does not exist. `VNC_PASSWORD` env also works. |
| `--vnc-localhost` | off | Adds `-localhost` to the generated VNC unit (tunnel-only exposure). Default stays off — byte-identical to the verified live configuration; see VNC exposure below. |
| `VNC_PASSWORD` (env) | — | Same as `--vnc-pass`, for non-interactive use. |

## CLI flags (automation surface)

```
setup.sh [--user <name>] [--hostname <h>] [--spi-mhz <n>] [--vnc-pass <pw>]
         [--vnc-localhost] [-y | --yes] [--plan] [--reboot] [--no-reboot]
         [--json] [--help]
```

| Flag | Meaning |
|---|---|
| `--yes` / `-y` | Unattended. Never prompts. Reboots only with an explicit `--reboot` (default: do not reboot). |
| `--plan` | Read-only audit: preflight + desired-vs-current comparison + verify. Writes nothing, reboots never, changes no state. **Wins over all mode flags.** `--reboot`/`--no-reboot` are ignored (not an error). |
| `--reboot` | Reboot at the end **iff a reboot-requiring change was applied this run** and verify passed. |
| `--no-reboot` | Never reboot, even if a reboot-requiring change was made. `--reboot` + `--no-reboot` together is an invariant violation → preflight failure (exit 2). |
| `--json` | Machine-readable final object (in addition to, never instead of, the RESULT line and exit code). |
| `--help` | Usage. |

The same file serves interactive humans and agents: interactive gets prompts;
agents get flags, stable exit codes, and one parseable result line.

## Fixed phase order (every run, all modes)

1. **Preflight** — root; Armbian; `armbian-add-overlay` present; board identity:
   the DTS model string (`/proc/device-tree/model`, lowercased — "orange pi zero 3w")
   OR the `BOARD=` id in `armbian-release` (`orangepizero3w`) — either matches;
   deck user exists (auto-detected when `--user` is not given);
   `armbianEnv.txt` present; `tools/hat-sound.c` present relative to the
   working directory (run from the repo root); `gcc` present; CLI invariants
   (`--reboot` + `--no-reboot` is not allowed). Failures abort exit `2` with `RESULT: PREFLIGHT_FAILED:<reason>`
   (nothing applied).
2. **Converge** — apply desired state (scope table; artifacts are the §5–§6
   sections of the script). Every write is compare-then-write: identical
   bytes never touch disk.
3. **Verify** — see Verification matrix. Pure observation, no writes.
   Immediately before it, in (non-plan) runs, when a unit file was written
   this run, **a new `hat-sound` binary was installed**, or a managed unit is
   not active, the script runs `systemctl daemon-reload`, then: units whose
   file (or binary, for `gamepi-sound`) was written this run get
   `systemctl restart` (mandatory — `enable --now` is a no-op on an
   already-active unit and would leave the stale process running, a live
   bug found 2026-09-14), and units that are simply not active get
   `systemctl enable --now`. The managed units carry no user state (they
   run as the deck user), and a verify that passes *right after* proves
   the (re)start worked.
4. **Reboot decision** — see Reboot policy (stamp-based).
5. **Report** — per-artifact APPLIED/UNCHANGED/DRIFT lines, per-item
   PASS/FAIL, summary, final `RESULT:` line, and (with `--json`) the JSON
   object; exit code per the table below.

`--plan` replaces the write half of converge with a desired-vs-current
comparison (unit files, sound source, sysctl drop-in, bridge, autostart,
overlay lines + stale overlays, packages), then runs verify. It writes **not a single** byte — no files, no apt, no systemctl,
no stamp, no log line — never reboots, and exits `1` with
`RESULT: DRIFT:<n>` when any difference or verify failure exists (exit `0`
only when converged and verify passes). Reboot mode flags are ignored in plan
mode and never recorded.

## Verification matrix

| # | Check | Method (live board) |
|---|---|---|
| 1 | Xvfb serving 960x960x24 | `xdpyinfo -display :1`: dimensions `960x960`, depth 24 bits/pixel |
| 2 | Openbox running | `systemctl is-active gamepi-openbox.service == active` |
| 3 | VNC listening on 5900 | `ss -lnt` contains `:5900` |
| 4 | VNC password exists | `<home>/.vnc/passwd` exists, non-empty |
| 5 | SPI overlay active | `overlays=` contains `spi3-cs0-cs1-spidev` AND `user_overlays=spi3-cs0-<MHZ>mhz` (exact value); `/dev/spidev3.0` exists |
| 6 | Bridge bound + screen size OK | `systemctl is-active gamepi-lcd.service`; `journalctl -u gamepi-lcd` **since the unit's current `ExecMainStartTimestamp`** contains `X11 root: 960x960` (the line is printed once at process start; anchoring to the current start avoids both the stale-evidence trap and a dead unit passing on old lines) |
| 7 | Units all active | `systemctl is-active` for all five `gamepi-*` units (incl. `gamepi-sound` — the v3.6 socket engine daemon) |
| 8 | SPI clock | `cat /sys/class/spi-master/spi3/max_speed_hz` (informational; logged, not gating) |
| 9 | RT budget live + persisted | `cat /proc/sys/kernel/sched_rt_runtime_us` == `-1` (the drop-in persists it across reboots; the board has no `sysctl` binary, so the read is from `/proc`) |
| 10 | Audio topology as intended | `/proc/asound/cards` contains exactly one card, `allwinnerhdmi` (the HAT amp is GPIO/PWM pin 12 — any second card means something bound an I2S path that should not exist here) |
| 11 | No stale audio-path overlays | no `es8388-audio`/`i2c0` `.dtbo` in `/boot/overlay-user` (or the older directory) and no `i2c-0` under `/sys/bus/i2c/devices` |
| 12 | HAT sound daemon socket | `[[ -S /run/gamepi-sound.sock ]]` — the v3.6 daemon bound the PCM socket (0666, any user may connect; serialized on accept). Transient: created at unit start (bind), unlinked by the engine on exit, wiped by the tmpfs `/run` at boot. A `--plan` run **before** the first v3.6 apply legitimately FAILs this row (the old unit ran `-i`). |
| 13 | Ollama model library | GET `http://localhost:11434/api/version` succeeds, and GET `/api/tags` (`jq -r '.models[].name'`) names **every** `OLLAMA_MODELS` entry. Apply mode pulls any missing model before this row runs (§3g — via the host `ollama` CLI when resolvable, else `docker exec ollama ollama pull`, the current board's deployment; the first run on a fresh board may take a while, the second pulls nothing). Plan mode reports absent models as drift instead. An unreachable API or unresolvable pull path is FAIL/DRIFT: the service install (a Docker container in the current deployment) is outside setup.sh's scope. |

Implementation notes (live-verified pitfalls, fixed 2026-09-15 — do not revert):

- Stale-overlay detection (the `--plan` drift audit and row 11) uses pure
  `[[ -f ]]` tests. An `ls a b | grep -q .` pipeline reports **failure** under
  the script's `set -o pipefail` whenever *either* operand is missing (GNU
  `ls` exits 2), so a stale dtbo present in only one of the two directories went
  undetected by the audit while apply-mode's `[[ -f ]]` removal still fired —
  observed live on a real board, fixed by removing the pipeline.
- Row 10 reads the card **name** from `awk -F'[][]'` field `$2` (field `$3` is
  the driver string) and enforces the count with
  `grep -c '^[[:space:]]*[0-9]'`. The live format is
  `NN [cardname  ]: driver - driver` plus an unnumbered continuation line.

Every item reports PASS, FAIL, or SKIPPED. In `--plan` mode, a FAIL or any
desired-vs-current difference counts toward `RESULT: DRIFT:<n>` (exit 1,
nothing applied). In apply mode, post-verify failures exit `1` with
`RESULT: INCOMPLETE:verify` (failing items are named in the lines above).

## Reboot policy and the applied-state marker

Only overlay changes require a real reboot: any `armbian-add-overlay`
call whose output actually differs (new `.dtbo`), or a change to the
`overlays=`/`user_overlays=` lines in `armbianEnv.txt`. Everything else —
service files, the sound binary, the bridge, the sysctl drop-in, autostart,
packages, hostname, VNC password, stale-overlay removals — is repaired live
or takes effect on the next unit start / next boot, and never needs a reboot.
The sysctl drop-in is applied by `systemd-sysctl` at boot; verify proves the
runtime value from `/proc` either way (and a drop-in write alone never sets
the reboot flag — the stamp covers overlay state only).

### The stamp — why re-running after a reboot is a no-op

Armbian applies the two overlay lines **at boot**, not at write time. Without
extra bookkeeping, a script cannot distinguish "I just wrote the overlay
lines (reboot pending)" from "the machine is already booted with them." So
setup.sh keeps a stamp:

- **Location:** `/var/lib/micro-cyberdeck/overlay.sig` (host-local,
  git-ignored; delete it if ever suspect — the next run re-derives it).
- **Content:** `sha256(on-disk .dtbo bytes) + ':' + normalized(overlays=,
  user_overlays= values)`, written **after** a successful
  `armbian-add-overlay` run and env-line write — i.e., it describes what the
  machine should boot into on its next boot.
- **Decision at the end of a run:**
  - **Run applied an overlay change** → stamp written with the new signature,
    `REBOOT_NEEDED=1` (the kernel is not in this state yet; env lines alone
    do not prove it).
  - **Run applied no overlay change**, stamp exists,
    `sha256(current .dtbo) + env values == stamp` → the machine booted with
    exactly what the script last asked for → **no reboot needed**.
  - **Run applied no overlay change**, but the signature ≠ stamp (overlay
    state changed outside this script) → reboot **required** to make the
    boot state match.

### Decision table (end of run, after verify)

| Context | Reboot needed? | Action |
|---|---|---|
| TTY (human), no | no | Print "Converged. No reboot needed."; exit `0`. |
| TTY (human), yes | yes | Prompt `Reboot now? [Y/n]` (default Y, Enter reboots on "no input"); on N: "converged + reboot queued for you" + instructions → exit `3`. |
| Non-interactive (`--yes` or no TTY), yes + `--reboot` | yes | Reboot (3-second delay, after verify passes). |
| Non-interactive, yes + *no flag* | yes | No reboot; instructions; exit `3` `RESULT: CONVERGED_REBOOT_PENDING`. |
| Non-interactive, yes + `--no-reboot` | yes | No reboot; instructions; exit `3` `RESULT: CONVERGED_REBOOT_PENDING`. |
| Any mode, no | no | No prompt, no reboot. (This is the 2nd-run-on-a-healthy-board no-op: nothing written, verify re-proves, exit `0`.) |

Consequences:

- **Fresh flash:** run 1 applies everything incl. overlay diff → reboot
  question (`--yes` agents: `--reboot`); run after boot: signature matches
  stamp → clean exit 0, no prompt.
- **Repeated run on a healthy board:** zero writes, zero prompts, exit 0
  (idempotent converge + re-verify).
- **Drift with an overlay change** (e.g., hand-edited `user_overlays`): drift
  is repaired, reboot question per the table.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Converged and verify passed; machine is working (or, in `--plan`, no drift and verify passed). |
| 1 | Drift and/or verify failure. `--plan`: differences detected, nothing applied. Apply mode: drift repaired but post-verify found failures. The `RESULT:` line names the failing items. |
| 2 | Preflight environment failure (not root, wrong board, missing tool, unknown user, conflicting flags). `RESULT: PREFLIGHT_FAILED:<reason>`. |
| 3 | Converged and verify passed, but a reboot is required and was not performed. `RESULT: CONVERGED_REBOOT_PENDING`. |
| 130 | Ctrl+C during a write. Script prints a truthful state summary and exits 130 (framework invariant: no silent aborts). |

## Result output (last line, always present)

```
RESULT: <STATUS>
```

| STATUS | Meaning |
|---|---|
| `CONVERGED` | Apply: everything is at desired state (no changes this run) and verify passed. Plan: no drift and verify passed. Exit 0. |
| `DRIFT:<n>` | Plan mode: `<n>` item(s) differ or verify-failed; nothing applied (exit 1). Apply: `<n>` file(s) changed this run, verify passed (exit 0). |
| `PREFLIGHT_FAILED:<reason>` | Environment gate failed; nothing applied (exit 2). |
| `INCOMPLETE:verify` | Apply succeeded; post-verify found failures — names in the lines above (exit 1). |
| `INCOMPLETE:overlay` | `armbian-add-overlay` failed; overlay state unknown (exit 1). |
| `INCOMPLETE:sound-src` | `tools/hat-sound.c` missing at apply time (run from the repo root); the sound binary was not touched (exit 1). |
| `INCOMPLETE:sound-build` | `hat-sound` compilation failed (exit 1). |
| `INCOMPLETE:interrupted` | Ctrl+C during a run; truthful state line precedes it (exit 130). |
| `CONVERGED_REBOOT_PENDING` | Converged + verify passed; reboot needed but not performed (exit 3). |

With `--json`, the same information is also emitted as a JSON object:
`{ result, mode, preflight, changed, items: [{name, state, detail?}],
reboot: {needed, performed, pending} }`.

### VNC exposure

The VNC service is generated from the template in §5 of the script, which
mirrors the verified live configuration: `-localhost` is **not** set — VNC
binds the board's interfaces. The security consequences are out of scope for
now (tracked in `TODO.md`); until then treat VNC as board-LAN-only and
prefer SSH tunnels (`ssh -L 5901:localhost:5900 cj@pi`) over direct
connections.

## Sound (hat-sound, v3.6)

`setup.sh` provisions the HAT's amp path as a root **socket-daemon** service
(plan 2026-09-15-13-17-28, WS2):

- **Binary:** `/usr/local/bin/hat-sound`, built from the tracked
  `tools/hat-sound.c` on every run (compare-then-install; an install
  restarts `gamepi-sound` onto the new binary).
- **Unit:** `gamepi-sound.service` runs
  `hat-sound -d /run/gamepi-sound.sock` (v3.6 daemon mode): acquires the amp
  pin (gpiochip0 line 37 = PB5, header pin 12) **once at start** and keeps
  it owned for the process lifetime — LOW between jobs, released only on
  exit. It binds `/run/gamepi-sound.sock` (0666, so **any user** can connect)
  and serves PCM jobs **serialized on accept**: one client at a time, extras
  block in `accept()`. Each job streams raw s16le 48 kHz mono into the
  frozen v3.3 tick grid (192 kHz pin clock, OS 4, `SCHED_FIFO` 98 on core 1,
  the signed re-anchor and pure-spin wait are byte-for-byte the frozen path).
  The per-job self-report (source bytes, samples, effective Hz, real-time
  multiple, underruns) goes to the **journal** — `journalctl -u
  gamepi-sound` — since the daemon never opens the old `/run` stats file.
  The socket sits directly in `/run` (tmpfs, wiped at boot); 0666 is set
  engine-side (`fchmod(fd)` + `chmod(path)` — see the field finding in the
  scope row above). No `RuntimeDirectory` in the unit. The socket is runtime
  state, not managed content (verify row 12). Stop is clean: SIGTERM/SIGINT
  are installed via `sigaction` **without `SA_RESTART`** (glibc's plain
  `signal()` implies it, so an interrupted `accept()` would be silently
  resumed and the stop never seen — the daemon got SIGKILLed after the full
  `TimeoutStopSec` on its first stop) so the engine exits promptly from its
  blocking `accept()` (pin released LOW, "daemon stopped — pin released LOW"
  in the journal); `Restart=always` re-owns the pin and re-binds the socket
  if the process dies.
- **RT budget:** `kernel.sched_rt_runtime_us = -1` via
  `/etc/sysctl.d/99-sched-rt.conf` (applied by `systemd-sysctl` at every
  boot; the board has no `sysctl` binary). The default 950 ms/1 s RT
  bandwidth throttles the `SCHED_FIFO` tick loop and distorts playback;
  verify row 9 proves the runtime value.
- **Playing is now connecting to the socket** (v3.6): any user, e.g.
  `python3 -c "import socket; s=socket.socket(socket.AF_UNIX,
  socket.SOCK_STREAM); s.connect('/run/gamepi-sound.sock'); ..."` stream the
  s16le data, half-close, done — the journal's per-job self-report is the
  outcome. The stop/start dance of v3.5 is **retired as the normal play
  path**; the manual one-shot player remains the **fallback** if the daemon
  or pin is needed off the service (e.g. debugging):
  1. `sudo systemctl stop gamepi-sound` — the daemon exits, unlinks the
     socket, releases the pin LOW (silent)
  2. `sudo /usr/local/bin/hat-sound -F <mono48k.s16>` — standalone; exit
     line self-reports the real-time ratio
  3. `sudo systemctl start gamepi-sound` — the daemon re-binds
- **Flags:** v3.6 repurposes `-d` — it now takes a **socket path** (daemon),
  and takes no source/tone args (combining them exits 2 with a clear error).
  The old "`-d SEC` duration" moved to `--duration SEC` (`-t HZ
  --duration 5` sets a tone's length; `-F FILE` / `-p` / `-i` are
  unchanged). `-i` (idle pin hold) is kept as the rescue/service mode
  inside the unit's restart path only; hand running it steals the pin
  from the daemon.

Stale audio-path overlays (`es8388-audio`, `i2c0`) are removed on every
apply run: the HAT amplifier is GPIO/PWM, not I2S (verified 2026-09-15), so
these dtbos bind nothing — a zombie probe at every boot.

## Agent usage (recommended loop)

```bash
setup.sh --plan                          # audit
# exit 0 -> already working; exit 1 -> drift, fix and re-audit
setup.sh --yes --reboot --json --spi-mhz 48 --vnc-pass "$VNC_PW"
# exit 0 -> machine working
```

Non-zero exit codes are the loop's signals; the RESULT line and JSON are the
payload. Agents never reboot on their own except via `--reboot`.

## VS Code entrypoint

- **One task** — `GamePi: set up the machine` — runs `setup.sh` in the
  integrated terminal, so sudo and the reboot prompt are visible and
  answerable.
- **One play action** in `.vscode/launch.json` invoking that same
  task.
- No bootstrap wrapper unless a platform prerequisite blocks the command.

## Troubleshooting

The first step for a "broken board" is to re-run setup (or re-run it under
`--plan` to see the diagnosis). The verify matrix is the diagnostic: each FAIL
names its file / unit. If the board won't boot, the rescue scripts in this
repo — `apply-960.sh` / `revert-480.sh` (one-time migration helpers) — cover the
two known broken states; anything else is a fresh-flash + `setup.sh` case.

## How to extend this file (and setup.sh)

- Add or change a knob -> update the Configuration table and the matching
  section of `setup.sh` (DESIRED block or template).
- Add a verification item -> extend the matrix (numbered, stable order so
  agent output stays diffable).
- Add a flag or exit code -> update both tables; the flag must compose with
  `--json`.
- State a manual workaround that has become the norm -> move it into the
  "Managed scope" table as an owner entry.