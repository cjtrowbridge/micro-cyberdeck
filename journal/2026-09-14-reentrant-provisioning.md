# 2026-09-14 — setup.sh re-entrant provisioning rewrite (pre-live-validation)

## Context

Governing plan: `plans/current/2026-09-14-23-03-51_setup-script-reentrant-provisioning.md`.
Protective commit for the prior 960x960 one-shot work: `6cd977f`. This
checkpoint records the rewrite's state **before** the single live
`sudo bash setup.sh --plan` validation run that the operator performs
(agent sudo prompts are auto-cancelled in this environment).

## Action

- Rewrote `setup.sh` as the canonical re-entrant, converging, self-verifying
  provisioning entrypoint:
  - fixed phase order: preflight -> converge -> verify -> reboot decision ->
    report (applied-state marker + audit log line appended in `main`)
  - flags as the automation surface: `--user --hostname --spi-mhz --vnc-pass
    --vnc-localhost -y/--yes --plan --reboot --no-reboot --json --help`
    (`--plan` wins over all; `--reboot`+`--no-reboot` is an invariant
    violation, exit 2)
  - compare-then-write everywhere: identical bytes never touch disk; plan
    mode records drift items and writes nothing (no files, no apt, no
    systemctl, no stamp, no log line)
  - stamp-based reboot policy: `/var/lib/micro-cyberdeck/overlay.sig`
    (sha256(.dtbo) + normalized overlay env values); reboot is required only
    when the signature a run wrote differs from what was present at run
    start — a second run on a healthy board is a zero-write no-op
  - exit codes 0/1/2/3/130 and a final `RESULT: <STATUS>` line always;
    `--json` adds the machine object; Ctrl+C trap prints a truthful state
    summary, exits 130
  - verify matrix (xdpyinfo 960x960x24, 4 units active, :5900,
    `.vnc/passwd`, overlay env lines + `/dev/spidev3.0`, whole-journal
    `journalctl -u gamepi-lcd` grep "X11 root: 960x960"); items that can
    only be proven post-reboot report SKIPPED (not FAIL) in the run that
    applied an overlay change, so the reboot prompt can still fire
- Documented the contract in `docs/setup.md` (managed scope, knobs, phase
  order, verification matrix, reboot decision table, exit codes, RESULT
  table, VNC exposure, agent loop, troubleshooting).
- Linkage/mandate: `AGENTS.md` host layout entry + MANDATE (setup.sh or
  provisioning-behavior changes are documented in `docs/setup.md` in the same
  change and pass apply->verify on a live board before commit); the script
  header carries the same mandate.
- `README.md` gained a "Setup (provisioning)" section: the one command,
  `--plan`/`--yes --reboot --json`, re-run contract, `.vscode/` entrypoint,
  rescue-script scope.
- `.vscode/`: single entrypoint "GamePi: set up the machine" (tasks.json
  process task + launch.json node-terminal play action) running
  `bash ${workspaceFolder}/setup.sh` in the integrated terminal so sudo and
  the reboot prompt are visible and answerable; no bootstrap wrapper (no
  platform prerequisite on Linux).
- `TODO.md`: new "Device provisioning" section mirrors the plan; the
  "reconcile the live board" item is now part of it (live `--plan`).

## Correctness evidence (local, deterministic)

| Check | Result |
| --- | --- |
| `bash -n setup.sh` | pass |
| `--help` | correct usage, rc 0 |
| `--reboot --no-reboot` | rc 2 (invariant) |
| `--spi-mhz abc` / `--vnc-pass` >8 chars / unknown flag | rc 2 each |
| non-root `--plan` | `PREFLIGHT_FAILED:must_run_as_root,missing_armbian-add-overlay`, no writes |
| byte-canonical templates vs verified-live config (960/24/cj/48MHz) | DTS, bridge (206 lines), 4 units, autostart all byte-identical |
| board-model preflight | initially matched DTS model `orange pi zero 3w` against a bogus underscore constant, which would have false-failed every run; now accepts the DTS model string **or** the `BOARD=orangepizero3w` id from `armbian-release`; non-root `--plan` on this board now passes the model check |

## Known defects fixed this window

- Bridge template: restored `# Keep known-good speed for first live test.`
  line and removed one blank before the `Main` header (byte-fidelity to the
  live `/usr/local/bin/xvfb-to-st7789.py`).
- Autostart template: unquoted heredoc collapsed `\` line continuations into
  nothing (bash line-continuation inside an unexpanded heredoc); switched to
  a quoted `<<'EOF'` delimiter. Autostart is now byte-identical to the live
  `/home/cj/.config/openbox/autostart`.
- `docs/setup.md`: RESULT table corrected to the exact emitted strings
  (`DRIFT:<n>` in plan mode, added `INCOMPLETE:overlay` and
  `INCOMPLETE:interrupted`); phase-3 wording matches code
  (`daemon-reload` + `enable --now` for the four managed units when a unit
  file was written or any managed unit is inactive); preflight line records
  the dual board-identity check; `--hostname` documented as drift-report
  (plan) / `hostnamectl` (apply).

## Live validation result (2026-09-14, post-checkpoint)

Three live runs happened between this pre-checkpoint and this addendum; each
exposed a real defect that is now fixed in `setup.sh` (and, where warranted,
documented in `docs/setup.md` per the standing mandate):

1. `sudo bash setup.sh --plan` (first) surfaced **two** defects:
   - `overlay-dtbo` **DRIFT**: the script looked for the compiled user overlay
     under `/boot/armbian-overlays/`, but Armbian's `armbian-add-overlay` writes
     user overlays to `/boot/overlay-user/`. Fixed with `find_dtbo()` (tries
     `/boot/overlay-user/` then `/boot/armbian-overlays/`), used by both the
     overlay signature and the plan/apply logic.
   - `bridge-live` **FAIL**: Python3 **block-buffers stdout when not on a TTY**,
     so the bridge's one-shot startup self-check line (`X11 root: 960x960`)
     never reached the systemd journal. The `gamepi-lcd` unit template (which
     runs as root, no `User=`) gained `Environment=PYTHONUNBUFFERED=1` — one
     deliberate, documented deviation from the original live unit.
2. `sudo bash setup.sh` (apply) converged, wrote exactly the two intended files
   (the `.before-gamepi` backup + the new `gamepi-lcd.service`), no backup
   churn, no spurious reboot — but `bridge-live` still **FAIL**ed: `systemctl
   enable --now` is a **no-op on an already-active unit**, so the changed unit
   file never reached the running process. Fixed the (g) unit-repair rule so a
   unit whose file was written this run gets `systemctl restart` (only
   truly-inactive units fall back to `enable --now`). Separately, the
   `bridge-live` check is now anchored to the unit's current
   `ExecMainStartTimestamp` (`journalctl --since "@<epoch>"`) so the once-at-start
   line is neither hidden by rotation (stale-evidence trap) nor let through from
   a dead unit's old lines.
3. `sudo systemctl restart gamepi-lcd.service` (one-time, manual, out-of-band)
   then `sudo bash setup.sh --plan` — **all 7 managed artifacts `unchanged`,
   9/9 verify items PASS** (incl. since-start `bridge-live`), `RESULT:
   CONVERGED`, exit 0. This is the live proof of the converge + byte-canonical +
   verify contract.

The one-time manual restart was necessary because the script only restarts a
unit it wrote itself; the running bridge still carried the pre-fix (buffered)
environment and had been started before the unit file changed. (Note for the
record: the journal also showed a brief ~:22:41 run of a separate "v2" bridge
with `FPS:`/`Live bridge running:` lines that are not in the on-disk template —
consistent with manual experimentation; not acted on.)

## Stop condition / next step — updated

Live `--plan` validation is **done** (CONVERGED, exit 0, above). The second
`sudo bash setup.sh` (apply) run is **done** too: every artifact `unchanged`,
`---- no file changes (byte-stable) ----`, no `systemd:` lines, 9/9 verify
PASS, no reboot prompt, `RESULT: CONVERGED`, exit 0 — the write-path no-op is
now live-proven. Flipped the plan checkboxes for the live-validated items,
updated this note, regenerated `plans/*/index.md` via
`agentic-pipelines/scripts/regenerate_plan_indexes.py --repo-root .` (then
`--check`), and cut commit 2 (this journal, `setup.sh`, `docs/setup.md`,
`README.md`, `AGENTS.md`, `TODO.md`, `.vscode/`, plan + index). No push.