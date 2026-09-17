# Membrane key buttons (13)

> Status: **Not started** (mirrors the README table)

## What it is

The 13 membrane keypad buttons on the GamePi HAT faceplate (the D-pad, start/
select, and face/shoulder-style cluster typical of a game form factor). Each
is a plain GPIO line, not an I2C keyboard controller — the intended end state
is a `gpio-keys` device-tree overlay exposing them as a single input
device with standard `KEY_*` event codes.

## What we know

- The design is **fully specified in the repo** — no hardware discovery is
  left unknown except the physical pin assignment:
  - The pinctrl phandles are already mapped: banks `PB`=1, `PD`=3, `PE`=4,
    node `2000000.pinctrl`; plus `PL2` under `70025000.r-pinctrl` (a second,
    smaller GPIO controller covering the button bank's pins).
  - Buttons are **active-low with pull-ups** in the DTS binding.
  - The overlay declares `linux,code = <KEY_*>` per key, so once bound they
    arrive as a normal `/dev/input/eventN` alongside
    [power-button.md](power-button.md).
  - `setup.sh` already knows how to join a new overlay to the desired set —
    `overlay_desired_set()` — and the phase-2 comment in `setup.sh` (around
    the "Today the set is `{spi3-cs0-<MHZ>mhz}`" line) explicitly anticipates
    the gpio-keys overlay joining the set; the wiring for that join is
    documented there and in
    [`journal/2026-09-15-hat-audio-v3.3-status.md`](../../journal/2026-09-15-hat-audio-v3.3-status.md)
    (design note section).
- **The one open item is the pinout itself.** A helper lives on the board at
  `/home/cj/button-map.py`: running it (operator, `sudo python3
  /home/cj/button-map.py`) reads back which GPIO line toggles as each
  physical button is pressed, producing the pin→key mapping needed to fill in
  the overlay's `gpios` cell. That run **has not happened yet** — it is
  deliberately an operator step (pressing physical keys one at a time).
- No `/dev/input/eventN` node exists for the buttons today — the deck's only
  input devices are the PMIC power button (`event0`) and whatever an external
  keyboard/mouse might be (see [power-button.md](power-button.md)).

## How we know

- The pin-bank mapping, active-low/pull-up convention, and the
  `overlay_desired_set()` join point are all committed in `setup.sh` and the
  design-note journal cited above.
- Re-probe to confirm the buttons still surface no input device:
  `ls -l /dev/input/` (only the PMIC `event0` expected) — no sudo required.
- To run the mapping (operator): `sudo python3 /home/cj/button-map.py`,
  pressing each labeled key when prompted.

## What it portends

- Unblocks **native game-deck ergonomics** — the whole point of the form
  factor. Without it the deck can only be driven by SSH or (when
  [touch-gt9271.md](touch-gt9271.md) lands) by touch.
- The `button-map.py` run is the **critical-path unknown** for this row: it's
  a one-time, manual, ~5-minute step (the only "I need to be in front of the
  hardware" step in the whole deck's remaining input work, alongside the
  touch INT/RST pins — the two can be captured in the same sitting).
- Once mapped, the `gpio-keys` overlay is a **known-good, already-plumbed**
  pattern for `setup.sh` (identical mechanism to the SPI overlay it joins) —
  the row should flip to **Done** with a normal converge/verify cycle and no
  new framework work.
- Downstream: any "voice-interactive local agent software" (a README
  known-unresolved item) benefits from real button events (push-to-talk,
  menu confirm) rather than having to wait on touch.