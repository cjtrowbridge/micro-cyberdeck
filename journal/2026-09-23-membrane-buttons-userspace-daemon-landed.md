# 2026-09-23 — membrane-buttons-userspace-daemon (landed)

Plan: [plans/past/2026-09-23-22-21-19_membrane-buttons-userspace-daemon.md](../plans/past/2026-09-23-22-21-19_membrane-buttons-userspace-daemon.md) (closed `past` 2026-09-23 — full acceptance gate passed live, operator keyproof confirmed). User request: get the HAT's 13 membrane keys working and assigned to keyboard keys.

## What happened today

The live mapping session (earlier this day, operator-driven, logged to
`/home/cj/btnmap-run.log` — 160 lines, board-local, not tracked) yielded a
clean paired press/release on **9 of 13** keys; the three keys on
`PD1/PD3/PD4` (header pins 32/33/37) went idle-LOW with zero transitions, and
the 13th (pin 5) never moved while a long-press mid-session rebooted the deck
— consistent with the AXP8191 PEK line that already owns `KEY_POWER` on
`/dev/input/event0`.

### The kernel path is closed on this box (the decision driver)

`6.6.98-vendor-sun60iw2` has no `CONFIG_INPUT_GPIO_KEYS`, no
`CONFIG_INPUT_UINPUT` (no `/dev/uinput`), no `CONFIG_OF_OVERLAY`, and the on-disk
`/usr/src` headers are a **pruned 154 MB package** — `drivers/input/keyboard/`
holds only Kconfig+Makefile, no `gpio-keys` sources, so a module rebuild is out
too. The userspace gpiod→XTest daemon is the **only** input path, and it is
kernel-free: one tracked script + one unit, both converged by `setup.sh`.

### Xlib/XTest live verification chain (python3-xlib 0.33)

Three API assumptions from the prior design notes were each killed by a live
probe before the daemon shipped — the final shape, proven end-to-end with
**"INJECTION OK — keycode 37"** (a real `Control_L` press+release on the open
`Xvfb :1` desktop):

- `display.Display(':1')` **auto-binds all X extension methods** — including
  `xtest_fake_input` on the Display object itself. Calling `xtest.init(d, …)`
  (the documented boot path) **double-registers** and raises
  `AssertionError: attempting to replace display method: xtest_get_version`.
  The daemon never imports or calls `init`.
- `d.screen()` is a **method** returning the default `Screen` object (not a
  property with `.root`, not a list); `d.screen().root` is the root Window.
- Injection is three lines: `d.xtest_fake_input(X.KeyPress, code,
  X.CurrentTime, d.screen().root, 0, 0)` — then the same with
  `X.KeyRelease` — then `d.sync()`.

Verified `:1` keycodes (selftest prints them on every root run): Return=36,
Control_L=37, a=38, x=53, b=56, Control_R=105, Up=111, Left=113, Down=116.

### Daemon (`tools/hat-buttons.py`)

One `request_lines` per chip (the A733 gpiolib rejects re-requesting a line an
already-live holder owns — so one request covers that chip's whole mapped
subset). Poll loop at 5 ms; an edge **commits only after 30 ms of stability at
the new level** (any raw motion back cancels) — that kills bounce, phantom
taps, and the "released before debounced" hole without double-firing.
`After=gamepi-xvfb.service` is ordering only — the daemon re-opens a restarted
X every 5 s and the unit `Restart=always`/`RestartSec=1`, same
tolerance-and-self-heal posture as the bridge units. `--selftest` re-proves X,
keycode resolution, and idle levels; `--inject-test` (unit stopped first — it
holds the line requests) pushes one real `Control_L` through the path.

Non-root selftest semantics are deliberate: on this board the `/dev/gpiochip*`
nodes are `root:root 0600` (the deck user's `input` group does not cover them)
— `--selftest` as `cj` prints the X side green (9 keycodes resolved) and
**`GPIO DENIED (needs root)`** on both chips, then exits 1. That is the
contract: the full proof is the root-gated selftest / the unit itself.

### setup.sh fold-in (re-entrant, in the existing patterns)

- New `BUTTONS_PY_SRC`/`BUTTONS_PY_DST` vars beside the `SOUND_SRC` cluster;
  daemon is **compare-then-write** from `tools/hat-buttons.py` (pure-text
  interpreter script — the installed copy *is* the source bytes, no build
  step), with the same DRIFT guard as `SOUND_SRC`.
- New `buttons)` case in `generate_unit()`: root (no user directives),
  `Environment=DISPLAY=:1`, `Restart=always`, `RestartSec=1`,
  `WantedBy=multi-user.target`.
- `buttons` added to **all four** unit loops (converge write, §(g) inactive
  scan, §(g) restart, `run_verify`) — the loop is now
  `xvfb openbox vnc lcd sound buttons websockify shellinabox`.
- §(g) repair: a changed `hat-buttons.py` maps into
  `unit_written["gamepi-buttons.service"]` (a daemon change is a unit change in
  disguise — same rule as the sound binary).
- No package change: `PKGS` already carried `python3-xlib python3-libgpiod`
  (installed during the earlier audio/web UI work).
- The two stale "buttons are a `gpio-keys` overlay joining
  `overlay_desired_set()`" comments were rewritten to note the buttons are a
  userspace daemon, not an overlay — the set is expected to stay a single
  overlay.

## Same-change documentation (the mandates)

- `docs/setup.md` — the `user_overlays=` scope row no longer anticipates a
  buttons overlay; `gamepi-buttons.service` added to the managed-scope units
  row and the byte-canonical paragraph (now "eight" units);
  `/usr/local/bin/hat-buttons.py` is its own managed-scope row; verify row 7
  is eight units with `buttons` in the loop; **new "Membrane buttons
  (hat-buttons.py)" section** (kernel infeasibility rationale, the 9-key
  table, chip labels, parked keys, root-unit rationale, the selftest/
  inject-test keyproof procedure, no-reboot-needed statement).
- `docs/hardware/membrane-buttons.md` — **full rewrite**: Not started →
  **In progress** (9/13 live, 3 parked, 13th suspected PEK-fused); the old
  record's `gpio-keys`-overlay design, its "active-low" blanket statement
  (PD2/Left is the one **active-HIGH** line), and the **wrong second-chip
  label** (`70025000.r-pinctrl` → corrected to the live-sysfs
  `7025000.pinctrl`, base 352) are all superseded; parked-key re-attempt
  procedure (a confirmed round adds `KEYS` rows and nothing else).
- `docs/hardware/power-button.md` — cross-cut addendum: the pin-5 membrane
  key's inert-line + mid-session-reboot evidence **fits the PEK fusion**;
  the record documents the suspect and the confirming operator test.
- `README.md` + `docs/hardware/README.md` — the membrane row flips
  Not started → **In progress** in both, with the daemon wording.

## Verification (2026-09-23 — acceptance gate passed live)

Agent-side, live: daemon compiles; non-root selftest shows the X side green
(9 keycodes) with the expected `GPIO DENIED (needs root)`; `XTest` injection
proven end-to-end on `:1`; `setup.sh` passes `bash -n`; every doc site above
is on disk.

Operator gate, all five plan steps green:

1. `sudo bash setup.sh --plan` → the expected 2-file `hat-buttons.py` +
   `gamepi-buttons.service` drift, plus 2 pre-existing unrelated FAILs
   (unchanged by this work).
2. `sudo bash setup.sh --yes` → applied both files, `systemd: restarting
   gamepi-buttons.service`, `unit:buttons` PASS — and as a side benefit the
   §(g) repair loop recreated `gamepi-websockify`'s missing enable symlink
   and restarted it: the re-entrancy design doing its job on live collateral.
3. Follow-up `--plan` → **RESULT: CONVERGED**, exit 0.
4. **Keyproof** — root `--selftest --inject-test` was **SELFTEST OK**
   (9/9 idle levels matched the table, one real `Control_L` injected), and
   the operator pressed all nine physical keys on the `:1` desktop confirming
   each landed (d-pad up/down/left, A/B/X, both shoulders, Start). The
   membrane→keystroke path, not just the injection path, is now proven.
5. Indexes regenerated; plan flipped to `past` and committed — repo-local
   identity, no push (this commit).

**Final commit delta (same change, logging-only):** the accepted selftest
printed `GPIO OK: /dev/gpiochip0 (?)` / `(?)` — a daemon *log-lookup* defect
in `_label()`: it derived `/sys/class/gpio/gpiochipN` from the device-node
name, but the node name is pinctrl *registration* order while the sysfs dir
is named by line-offset **base** (chip 1's dir is `gpiochip352`, not
`gpiochip1` — `cat .../gpiochip1/label` is "No such file or directory"). Fixed
before commit: `_label(chip)` now reads `chip.get_info().label` (gpiod kernel
chip-info query — the `ChipInfo` dataclass carries `name`/`label`/`num_lines`),
`open_chips()` warns on a label mismatch for **both** chips (expected
`2000000.pinctrl` / `7025000.pinctrl`, still warn-not-fail), and the selftest
prints the true labels. No key-table, polling, or injection behavior moved.
`docs/hardware/membrane-buttons.md`'s re-probe command had the identical node-name
mistake and now globs `/sys/class/gpio/gpiochip*/label` instead, and its how-we-know
paragraph carries the registration-vs-base explanation.

**One deferred operator step (post-commit):** the installed
`/usr/local/bin/hat-buttons.py` is the accepted pre-fix copy —
`sudo bash setup.sh --yes` now (expect `DRIFT:1`,
`systemd: restarting gamepi-buttons`), `--plan` until CONVERGED, and one root
re-selftest with the expected labels `2000000.pinctrl` / `7025000.pinctrl`
(no `?`). The label path is logging-only, so the board is fully functional
meantime; the daemon unit itself is already active.

The parked-key re-attempt (32/33/37) is recorded in the hardware record as a
separate, later task — it needs a mechanical re-test (resheet or jig), not
code.