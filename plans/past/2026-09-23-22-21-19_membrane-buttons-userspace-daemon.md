---
plan_id: 2026-09-23-22-21-19_membrane-buttons-userspace-daemon
title: Membrane buttons — land the 9 mapped keys as a userspace gpiod→XTest daemon
summary: Fold the live-mapped 9-membrane-key table (button-map.py session, log-verified) into setup.sh as a root systemd service gamepi-buttons that polls the gpiod lines and injects standard KEY_* events into :1 via XTest; park the 4 unmapped keys (Y/RIGHT/Select idle-low + the 13th power-key hypothesis: PEK-fused) for a later re-attempt; same-change docs (setup.md, membrane-buttons.md rewrite, power-button.md note, both README rows) and live user-run apply->verify before commit.
status: past
created_at: 2026-09-23-22-21-19
---

Key: `[ ]` pending task, `[x]` completed task, `[?]` needs validation, `[-]` closed task

# Membrane buttons — userspace gpiod→XTest daemon

## Context

The 13 membrane keys on the GamePi HAT have no input controller of their own
(plain GPIO lines, ground-on-press) and the running vendor kernel
(`6.6.98-vendor-sun60iw2`), **verified on disk 2026-09-23**, cannot take a
gpio-keys device driver for them:

- No `CONFIG_INPUT_GPIO_KEYS` in the running kernel; no `gpio-keys.ko` /
  `uinput.ko` under `/lib/modules/6.6.98-vendor-sun60iw2` (the only input
  modules are `joydev` and the touch `goodix_ts`).
- No `CONFIG_INPUT_UINPUT` — no `/dev/uinput`, so there is no even-simpler
  "fake a key event in userspace and let the kernel deliver it" path.
- No `CONFIG_OF_OVERLAY` — no live devicetree patching at all.
- An out-of-tree module or a full kernel rebuild is not buildable here:
  `/usr/src/linux-headers-6.6.98-vendor-sun60iw2` is a **pruned 154 MB
  headers package** (the `drivers/input/keyboard/` directory holds only
  Kconfig+Makefile, no C sources; `find` for `gpio_keys*` reaches only
  `include/linux/gpio_keys.h`; the tree's Kconfig has no `INPUT_GPIO_KEYS`
  section), and only ~2.6 GB is free on `/` — not enough to drag in the
  vendor source tree.

Therefore the **only viable implementation on this box is a userspace
daemon**: poll the GPIO lines with `python3-gpiod` and synthesize key
events directly on the X server with the XTest extension
(`libxtst6` + `python3-xlib`). Every dependency is already present on the
board (verified 2026-09-23): `python3-libgpiod` 2.2.0 (v3-style API),
`python3-xlib` 0.33, `libxtst6`, and an X display `:1` that is open (no
authority file needed — `xdpyinfo -display :1` succeeds unauthenticated,
and the daemon's Xlib probe confirms keycodes + `xtest.fake_input`).

`/dev/gpiochip*` nodes are `root:root 600` (the deck user is in the
`input` group but that does not help; also note the *second* chip is
`/dev/gpiochip1`, not `/dev/gpiochip352` — the 352 is its line *offset*,
and gpiod resolves the offset automatically when you hand it `/dev/gpiochip1`).
So the daemon unit runs as **root** (no `User=`/`Group=`). No new
`/dev/input/eventN` node is created: this is X-level injection into
`gamepi-openbox`'s desktop, which is all the deck's workloads consume.

## The mapped-table (from the live button-map.py session, log-verified)

The mapping session ran as a root probe at `/home/cj/button-map.py`
(13 header pins → `(chip, line, name)`); the authoritative log
`/home/cj/btnmap-run.log` (T=0…T=3140, 160 lines) records the idle level of
each pin at start and every transition. The 9 keys below each show a clean
paired press/release in that log; the 4 parked pins show **zero**
transitions in the entire log.

| Key | Header pin | Chip | GPIO line | GPIO name | Idle | Active |
|---|---|---|---|---|---|---|
| D-Pad Up | 29 | `/dev/gpiochip0` | 140 | `PE12` | 1 | 0 |
| D-Pad Down | 31 | `/dev/gpiochip0` | 141 | `PE13` | 1 | 0 |
| D-Pad Left | 36 | `/dev/gpiochip0` | 98 | `PD2` | **0** | **1** |
| Face A | 40 | `/dev/gpiochip0` | 39 | `PB7` | 1 | 0 |
| Face B | 38 | `/dev/gpiochip0` | 40 | `PB8` | 1 | 0 |
| Face X | 10 | `/dev/gpiochip0` | 42 | `PB10` | 1 | 0 |
| Left shoulder (TL) | 16 | `/dev/gpiochip1` | 2 | `PL2` | 1 | 0 |
| Right shoulder (TR) | 8 | `/dev/gpiochip0` | 41 | `PB9` | 1 | 0 |
| Start | 35 | `/dev/gpiochip0` | 38 | `PB6` | 1 | 0 |

XTest keycode targets (verified via `Xlib.display.Display(':1')` +
`keysym_to_keycode`, 2026-09-23): Up=111, Down=116, Left=113,
A('a')=38, B('b')=56, X('x')=53, L-Ctrl=37, R-Ctrl=105, Return=36.
(If RIGHT/Select are recovered later: Right=114, Menu=135, Y('y')=29.)

Parked for a later re-attempt (this session: "work with what's currently
working and plan to try again later"):

| Key (expected) | Header pin | Chip | Line | Idle | Why it read dead |
|---|---|---|---|---|---|
| Face Y | ~32 | gpiochip0 | 97 | **0** | idle-low: press must *drive high*; a grounded probe can't (pull-up-to-ground = press works on idle-high pins; the reverse polarity needs a real contact close) |
| D-Pad Right | ~33 | gpiochip0 | 99 | **0** | same as above |
| Select | ~37 | gpiochip0 | 100 | **0** | same as above |
| Power (13th key) | ~5 | gpiochip0 | 34 | 1 | zero transitions ever; user-confirmed "the only other button is the power button" → leading hypothesis: wired to the AXP PMIC PEK input rather than a GPIO, i.e. it is *already* keyboard-functional as the existing `KEY_POWER` on `/dev/input/event0` (see [power-button.md](../../docs/hardware/power-button.md)); the long-press that rebooted the board mid-session is consistent with a hardware power-reset path |

LEFT (pin 36) is the one *idle-low* pin that nonetheless fired (0→1) —
the only active-HIGH line in the confirmed table; the daemon carries
per-key polarity, so the 32/33/37 keys (if the membrane simply needs their
real contact path, which a screwdriver probe can't provide) will work as
soon as they are confirmed — no daemon change, only the key-table gains
rows.

## Scope

- `tools/hat-buttons.py` — the daemon (poll loop, debounce, XTest
  injection, per-key idle/active polarity, reconnection to a dropped X
  display). The key-table above is the single source of truth; the 3 parked
  rows live in the file, behind an explicit `# parked:` comment, so a later
  mapping round only adds table rows.
- `gamepi-buttons.service` — root unit, `After=gamepi-xvfb.service
  gamepi-openbox.service`, `Restart=always`, `WantedBy=multi-user.target`.
- `setup.sh` — new §1 constants; a new managed-artifact step in
  `converge()` (compare-then-write both the `.py` and the unit, same
  pattern as `hat-sound`); add `buttons` to the §(g) managed-unit repair
  loop; a new verify row; correct the two stale comments in
  `apply_overlay()` that still anticipate a "gpio-keys overlay" joining
  `overlay_desired_set()` (that join point is now unused — kept in the code,
  superseded, and the comments say so). **No overlay is added**: the
  daemon reads the GPIO lines directly; `user_overlays` stays
  `{spi3-cs0-48mhz}`.
- `docs/setup.md` (same change, per the MANDATE): managed-scope rows for the
  daemon+unit, the converge §(b2) step, the verify row, a short "Membrane
  buttons" section, and the package-list note (`python3-libgpiod` +
  `python3-xlib` were already in the PKGS list from the touch work — confirm
  they stay).
- `docs/hardware/membrane-buttons.md` — full rewrite (Status flips from
  *Not started* to *In progress — 9 of 13 keys surfaced*; the old
  gpio-keys-overlay design and *untested* claims are removed; the correct
  second-chip node name `pinctrl@7025000` replaces the stale
  "70025000.r-pinctrl" from the earlier design note).
- `docs/hardware/power-button.md` — small same-change note tying the
  accidental-reboot observation to the 13th membrane key (PEK-fusion
  hypothesis).
- Root `README.md` (L79) + `docs/hardware/README.md` (L56) rows flip to the
  new status + daemon wording.
- Host `journal/` entry recording the decision + mapping table + re-try
  pointer for the 4 parked keys.

## Acceptance (live, user-run — all sudo is the user's)

1. `sudo bash setup.sh --plan` → drift reported (new files) but nothing applied.
2. `sudo bash setup.sh` → applies, verify row passes (service active).
3. `sudo bash setup.sh` → **CONVERGED (operator)**, exit 0, no writes.
4. Operator keyproof on the `:1` desktop (VNC/noVNC): each of the 9 mapped
   keys produces its key in a focused X app (the keyproof is an operator
   step, same convention as the power-button "evtest is the operator's job"
   note — the verify row proves the *service* and its X connection, the
   human proves the *keys*).
5. Regenerate plan indexes (`python3
   agentic-pipelines/scripts/regenerate_plan_indexes.py --repo-root .`) and
   commit (repo-local identity, **never push**).

## Tasks

- [x] Architecture decision: kernel path infeasible on this box (on-disk
      evidence, this session) → userspace gpiod→XTest daemon
- [x] Live mapping session: 9-key table log-verified from
      `/home/cj/btnmap-run.log`; 4 keys parked with a concrete re-try theory
      (idle-low polarity) and the 13th key re-attributed to the PMIC PEK
- [x] Dependency probe: `python3-libgpiod` 2.2.0 + `python3-xlib` 0.33 +
      `libxtst6` present; `:1` is open (no xauth); keycodes +
      `xtest.fake_input` confirmed; gpiochip node paths confirmed
      (`/dev/gpiochip0`, `/dev/gpiochip1`); root required for the gpiod
      chip open (permission-denied as the deck user, 2026-09-23)
- [x] `tools/hat-buttons.py` + `gamepi-buttons.service` template
- [x] `setup.sh` converge/verify/unit-loop fold-in + stale-overlay-comment
      corrections
- [x] `docs/setup.md` same-change rows + section
- [x] `docs/hardware/membrane-buttons.md` rewrite; `power-button.md` note;
      both README rows
- [x] journal entry
- [x] live `--plan` → apply → CONVERGED → desktop keyproof (operator)
- [x] regenerate plan indexes + commit (no push)

## Result

All five acceptance steps passed live 2026-09-23: first `--plan` reported the
expected 2-file drift (plus 2 pre-existing unrelated FAILs unchanged by this
work); `--yes` applied both files, restarted `gamepi-buttons`, and
self-healed `gamepi-websockify`'s missing enable symlink as a side benefit of
the §(g) loop; the follow-up `--plan` returned **CONVERGED**; root
`--selftest --inject-test` was **SELFTEST OK** — 9/9 idle levels matched the
table, all 9 keycodes resolved, one real `Control_L` injected; and the
operator confirmed all nine keys land on the `:1` desktop (**keyproof
passed**). The accepted selftest's `(?)/(?)` chip labels were a daemon
log-lookup defect (sysfs path derived from the registration-order node name;
chip 1's sysfs dir is `gpiochip352`, its line-offset base): fixed before
commit to read labels via gpiod `Chip.get_info().label` — the final board
apply of the fixed daemon is the single deferred operator step
(`sudo bash setup.sh --yes` → `--plan` until CONVERGED → root re-selftest is
the expected proof; the label path is logging-only). The parked-key
re-attempt (pins 32/33/37) and the pin-5 PEK confirmation remain the
documented later task in `docs/hardware/membrane-buttons.md`.

## Re-attempt pointer (the 4 parked keys)

Next hardware sitting: re-run the mapping with a **real membrane press**
(not a grounded probe) on pins 32/33/37 — these idle LOW, so a press must
drive the line high through the membrane's own contact; the probe only
*grounds* a line, which is why the 3 idle-low keys read dead while the 4th
idle-low (LEFT, pin 36) responded (its contact path floats high on press).
If pins 32/33/37 still never transition, they may be wired nowhere (dead
HAT silk-screen labels) — in that case the table stays at 9+1(power) and
this row closes as **Partial** with the 3 as the documented gap. The 13th
key needs one controlled long-press with the PEK sampler
(`/home/cj/pek-sample.py`) open: if `KEY_POWER` fires on `event0` while pin
5 stays flat, the PEK-fusion hypothesis is confirmed and the key is already
usable (no daemon work).