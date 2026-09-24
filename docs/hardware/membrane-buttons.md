# Membrane key buttons (13)

> Status: **In progress** (mirrors the README table) — 9 of 13 keys live via a
> userspace daemon; 3 parked pending a physical re-test, 1 suspected fused to
> the PMIC power-key input.

## What it is

The 13 membrane keypad buttons on the GamePi HAT faceplate (D-pad,
start/select, face-key and shoulder cluster). Each is a plain GPIO line, not
an I2C keyboard controller. The intended end state of a `gpio-keys`
device-tree overlay exposing them as a single input device is **impossible on
this board** (the vendor kernel can't take a button driver — see "How we
know"), so the input path that landed is **userspace**: the
`gamepi-buttons.service` unit runs `/usr/local/bin/hat-buttons.py` (source
`tools/hat-buttons.py`, templated by `setup.sh` §6) as root. It polls the nine
mapped GPIO lines (the three parked keys and the 13th-key pin are excluded,
see "Parked keys") via `python3-gpiod` and synthesizes standard X11 key events
on the open `:1` Xvfb
desktop through the XTEST extension (pure-Python `python3-xlib` speaks XTest
on the wire; no native input library and no `/dev/uinput` involved). A press
is one X Press + Release pair on the focused window — anything on the desktop
that reacts to keyboard input receives it.

## What we know

**Two pinctrl controllers carry the mapped buttons (live-sysfs, 2026-09-23):**

| Node | sysfs label | Role |
|---|---|---|
| `/dev/gpiochip0` | `2000000.pinctrl` | main A733 GPIO controller — 8 of 9 mapped lines |
| `/dev/gpiochip1` | `7025000.pinctrl` (base 352) | small second controller — the left shoulder only |

Correction to an earlier note in this record's history: the second
controller was once named `70025000.r-pinctrl`. The live `label` files on the
board read `2000000.pinctrl` and `7025000.pinctrl`; this record is canonical.
The daemon reads each chip's label through gpiod's own kernel chip-info query
(`Chip.get_info().label`) at start and **warns — it does not fail —** if a
chip no longer carries the label it expects. Reading via the kernel rather
than the sysfs `label` file matters: the `/dev/gpiochipN` device-node number
is the pinctrl *registration* order, but the sysfs directory is named by the
chip's *line-offset base* — here `/dev/gpiochip1` is sysfs `gpiochip352`, and
there is no `gpiochip1` directory — so a `gpiochipN/label` path derived from
the node number points at no such file. Node numbering can also shift between
vendor kernel builds, and the A733 gpiolib rejects re-requesting a line
another already-live request holds.

**Live key table (the 9 working keys).** Every entry below is a clean,
paired press/release observed in the live mapping log
(`/home/cj/btnmap-run.log`, 160 lines, 2026-09-23 session). Nine lines idle
**HIGH** and go low on press (active-low); `PD2` (Left, pin 36) is the one
**active-HIGH** line:

| Key | Keysym | Chip | Line | A733 | Pin | Idle→Active |
|---|---|---|---|---|---|---|
| D-pad up | `Up` | gpiochip0 | 140 | PE12 | 29 | 1→0 |
| D-pad down | `Down` | gpiochip0 | 141 | PE13 | 31 | 1→0 |
| D-pad left | `Left` | gpiochip0 | 98 | PD2 | 36 | **0→1** |
| Face A | `a` | gpiochip0 | 39 | PB7 | 40 | 1→0 |
| Face B | `b` | gpiochip0 | 40 | PB8 | 38 | 1→0 |
| Face X | `x` | gpiochip0 | 42 | PB10 | 10 | 1→0 |
| Left shoulder (TL) | `Control_L` | gpiochip1 | 2 | PL2 | 16 | 1→0 |
| Right shoulder (TR) | `Control_R` | gpiochip0 | 41 | PB9 | 8 | 1→0 |
| Start | `Return` | gpiochip0 | 38 | PB6 | 35 | 1→0 |

**Parked keys — not in the daemon table, by evidence** (the re-attempt
procedure lives here too, per the plan):

| Pin | Chip | Line | A733 | Suspected key | Why parked |
|---|---|---|---|---|---|
| 32 | gpiochip0 | 97 | PD1 | Face Y | idled **LOW** the whole mapping log; zero transitions on any press |
| 33 | gpiochip0 | 99 | PD3 | D-pad Right | idled **LOW** the whole mapping log; zero transitions |
| 37 | gpiochip0 | 100 | PD4 | Select | idled **LOW** the whole mapping log; zero transitions |

- **Re-attempt (32/33/37):** a grounded external probe cannot manufacture the
  other half of a membrane contact — it can only pull the line the membrane
  ties to `GND` side, so an idle-LOW line under a grounded probe is
  indistinguishable from a dead contact on that side of the membrane. The
  re-attempt needs a **real membrane press, one key at a time** (the operator
  has done exactly this twice already, no edge). If a future round
  (e.g. after the HAT is reseated, or a mechanical jig holds the button fully
  depressed across the whole probe window) finally shows a paired
  press/release on any of these three lines, the fix is mechanical: add one
  row to `KEYS` in `tools/hat-buttons.py` with its `idle`/`active` polarity,
  re-run `setup.sh` (which rewrites the installed copy and restarts the unit),
  and flip the row to **Done** if the rest of the table holds. The daemon's
  edge-commit logic is polarity-agnostic, so no other code changes — this is
  a table edit, not a feature.
- **Pin 5 (the 13th key), `gpiochip0` line 34 / `PB2`.** The mapping log for
  this line was **inert** (idle HIGH, no transitions) during a session in
  which an **accidental long-press caused the deck to reboot** (observed: the
  session dropped, then resumed after restart) — that fits the AXP8191 **PEK**
  input that already delivers `KEY_POWER` on `/dev/input/event0`
  ([power-button.md](power-button.md)): the membrane's 13th key is suspected
  fused into (or shared with) the PMIC power-key line, and a kernel power-key
  long-press hold (a hardware power cut) is exactly what would explain a
  mid-session reboot. The daemon does **not** poll this line — double-handling
  a PMIC power key from userspace GPIO would race the kernel's own event (and
  could trigger the same power cut twice). Confirming the fusion (reading
  `/dev/input/event0` while pressing the 13th key) is an operator test; until
  it is, this row stays **In progress** rather than **Done**. This is exactly
  the sort of *power button* cross-cut that belongs in
  [power-button.md](power-button.md), and the two records now cross-reference.

**Runtime / daemon contract** (source of truth: `tools/hat-buttons.py`,
documented in the "Membrane buttons" section of [`../setup.md`](../setup.md)):
root (the `/dev/gpiochip*` nodes are `root:root 0600` — the deck user's
`input` group does not cover them), `DISPLAY=:1` (the open Xvfb desktop — no
`Xauthority` needed), `Restart=always`/`RestartSec=1`, `After=gamepi-xvfb`
(ordering only — the daemon re-opens a restarted X every 5 s, same
tolerance-and-self-heal posture as the bridge units). Edges commit only after
30 ms of stability at the new level (any raw level motion cancels the pending
edge) — well above membrane bounce, well below the ~50 ms human-press floor,
so a quick release never double-fires or phantom-taps. `--selftest`
re-proves X connectivity, keycode resolution, and idle levels against the
table at any time; `--selftest --inject-test` (unit stopped first — it holds
the live line requests) additionally pushes one `Control_L` press+release
through the real path.

## How we know

- **The acceptance gate passed live 2026-09-23** (journal entry
  [2026-09-23-membrane-buttons-userspace-daemon-landed.md](../../journal/2026-09-23-membrane-buttons-userspace-daemon-landed.md),
  plan
  [2026-09-23-22-21-19](../../plans/past/2026-09-23-22-21-19_membrane-buttons-userspace-daemon.md)
  now `past`): `setup.sh --yes` applied daemon + unit with
  `unit:buttons` PASS, follow-up `--plan` **CONVERGED**, root
  `--selftest --inject-test` **OK** (9/9 idle levels matched the table, one
  real `Control_L` injected), and the operator pressed all nine physical keys
  on the `:1` desktop confirming each landed (d-pad up/down/left, A/B/X, both
  shoulders, Start) — the membrane→keystroke path proven end-to-end.
- **The 9-key table and the 3 parked lines are operator evidence, each in
  the live mapping log** `/home/cj/btnmap-run.log` (160 lines, 2026-09-23):
  every mapped key a clean paired press/release; every parked line an
  all-idle-LOW (or idle-HIGH, for pin 5) line. The probe that produced it is
  the board-local `/home/cj/button-map.py`, whose gpiod request/re-read
  pattern (`Chip` + `request_lines` with `LineSettings(direction=INPUT)` +
  `get_value`) is the exact pattern the daemon uses. (That helper and the
  log are operator-tier files, **not** in the repo — this record summarizes
  them; the repo-side source of truth for the table is `tools/hat-buttons.py`
  `KEYS`.)
- **The kernel infeasibility is a board fact, not a design choice** — the
  `6.6.98-vendor-sun60iw2` kernel has no `CONFIG_INPUT_GPIO_KEYS` (no
  `gpio-keys` module, and the on-disk headers are a **pruned 154M package**
  with no `drivers/input/keyboard/gpio-keys.c` to build it from), no
  `CONFIG_INPUT_UINPUT` (no `/dev/uinput`), and no runtime DT-overlay
  machinery (no `CONFIG_OF_OVERLAY`): the `gpio-keys` overlay, a module
  rebuild, and a uinput-from-userspace path are all closed on this box. The
  daemon — a tracked source under `tools/`, installed by `setup.sh`, and its
  `gamepi-buttons` unit in the §6 `unit loop` — is the only viable input
  path, and it is the **kernel-free** one: no new `.dtbo`, no module, no
  udev rule; just a unit + a script, both convergent and byte-stable.
- **Re-probe any of it** (read-only, no sudo required for the first three):
  - nodes + labels: `stat -c '%n %U:%G %a' /dev/gpiochip*` — expect
    `root:root 600` on both. For the labels, **glob the sysfs dirs** — they
    are named by line-offset base, so there is a `gpiochip0` and a
    `gpiochip352` but **no `gpiochip1`** (that node's base is 352):
    `for d in /sys/class/gpio/gpiochip*/label; do echo "$d: $(cat $d)"; done`
    — expect the pair `2000000.pinctrl` and `7025000.pinctrl`.
  - Note: `images/PXL_20260924_*.jpg` (the six 2026-09-23 evening photos)
    belong to the parallel power-path hardware session, not this record.
  - idle levels (with the daemon stopped, or as root): `sudo python3
    /usr/local/bin/hat-buttons.py --selftest` — re-reads every mapped line
    and prints its idle value against the table.
  - the X path end-to-end (the "INJECTION OK" proof from the live session):
    `sudo systemctl stop gamepi-buttons && sudo python3 /usr/local/bin/
    hat-buttons.py --selftest --inject-test` (sends one real `Control_L`
    press+release on `:1` — verify by watching a focused window on the
    desktop), then `sudo systemctl start gamepi-buttons`.

## What it portends

- The deck's **native input path is live** (9 of 13 keys) with zero kernel
  surface: no overlay, no module, no udev rule — just a tracked script and a
  unit, both folded into `setup.sh`'s converge/verify cycle (verify row 7
  covers `gamepi-buttons` active; the interactive key-proof is on the `:1`
  desktop, see [`../setup.md`](../setup.md) "Membrane buttons").
- **The pin-5 / PMIC PEK fusion is a cross-cutting hardware fact** about two
  separate subsystems (membrane keypad + PMIC power key). It is recorded here
  because it changes the membrane's key count, and cross-referenced from
  [power-button.md](power-button.md) because it changes the power key's
  surface. Either record is the single place that owns it; this one does.
- The 3 parked lines (32/33/37) are the only open item on this row. A
  confirmed round **adds rows to `KEYS` and flips the row to Done** — the
  daemon, the unit, and `setup.sh` wiring are all already converged for the
  general case; the three dead-LOW lines are a membrane/trace question, not a
  daemon question.
- Downstream: anything that needs "deck buttons as keyboard events" (the
  README's unresolved "local agent software" items — push-to-talk, menu
  confirm, launch) now has a working input source on `:1` with **no touch
  dependency** — the membrane buttons work even before
  [touch-gt9271.md](touch-gt9271.md) lands.