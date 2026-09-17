# Screen touch (gt9271)

> Status: **Not started** (mirrors the README table)

## What it is

The capacitive touch digitizer on the GamePi13 HAT, sitting in front of the
ST7789 panel. The controller is a Goodix-class `gt9271` on the board's I2C
bus; it would surface as a regular `/dev/input` event node once its driver is
bound.

## What we know

- The **device-tree node exists**: adapter `i2c-12`, address **0x14**
  (node `12-0014`), `OF_NAME` `touchscreen`, compatible `gt9271` — confirmed in
  `/sys/firmware/devicetree/base`.
- At the time of probing the node showed **`waiting_for_supplier`**, **no
  driver bound** (its `driver` symlink was absent), and **no `/dev/input`
  event device** exists for the touch — the deck is display-only today.
- Goodix controllers typically need two extra GPIOs (interrupt + reset) wired
  from the host. Which header pins the HAT uses for those **is not yet mapped**
  — no schematic has been captured.

## How we know

- Live probes (September 2026 session, all no sudo — these sysfs files are
  world-readable):
  `cat /sys/bus/i2c/devices/12-0014/name` → `gt9271`,
  `ls -l /sys/bus/i2c/devices/12-0014/` (no `driver` link),
  `cat /sys/bus/i2c/devices/12-0014/OF_NAME`,
  `cat /sys/bus/i2c/devices/12-0014/waiting_for_supplier`,
  `ls /dev/input/` (touch event node absent).
- The HAT vendor's GamePi13 wiring reference describes the I2C +
  INT/RST-GPIO topology generally, but the pin assignment for *this* board
  needs the schematic (or the `button-map.py` run — the same unknown is the
  gating item for [membrane-buttons.md](membrane-buttons.md)).

## What it portends

- Once bound, touch arrives for free as a standard evdev device — the stack
  beyond that (UI hit-testing at 960×960) is pure software.
- **Gating chain:** (1) the HAT pinout schematic (INT/RST GPIOs), (2) a
  pinctrl/DT binding or overlay for those pins, (3) the `i2c-gt911` driver
  (mainline — it covers the GT927 family). The same mechanism class as the
  buttons overlay, so the `setup.sh` plumbing is already understood.
- This unblocks **on-screen interaction** — currently the only inputs are the
  13 membrane keys (also unbound) and SSH.