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
  **Re-confirmed 2026-09-23 (WS1 re-probe):** still **no `driver` symlink on
  `12-0014`**, still no touch `/dev/input` event node — and the sole
  `/dev/input/event0` on the board is the PMIC's PEK power key
  (`axp2101-pek.0`), not the touch.
- **Mechanism narrowing (2026-09-23):** the driver *should* match — the
  `goodix_ts` module is loaded, its **of_match table includes
  `goodix,gt9271`**, the DT node (`touchscreen@14` on `twi@251c000`) carries
  `interrupt-parent`, `interrupts`, `irq-gpios`, and `reset-gpios` (no
  `status` property → defaults okay), and `CONFIG_TOUCHSCREEN_GOODIX=m`.
  The non-bind is therefore a **probe-time/config issue, not a
  missing-driver or compatible-mismatch issue** — the board's boot log
  (operator-level `dmesg`) is the next evidence for the exact probe error.
- Goodix controllers need two extra GPIOs (interrupt + reset) wired
  from the host. Which physical header pins the HAT uses for these **is
  not yet mapped** — no schematic has been captured (the DT's GPIO
  phandles point at SoC pins, but whether the HAT's wiring actually
  matches them is unverified).

## How we know

- Live probes (September 2026 session, all no sudo — these sysfs files are
  world-readable):
  `cat /sys/bus/i2c/devices/12-0014/name` → `gt9271`,
  `ls -l /sys/bus/i2c/devices/12-0014/` (no `driver` link),
  `cat /sys/bus/i2c/devices/12-0014/OF_NAME`,
  `cat /sys/bus/i2c/devices/12-0014/waiting_for_supplier`,
  `ls /dev/input/` (touch event node absent).
- **WS1 re-probe (2026-09-23, no sudo):**
  - `ls -l /sys/bus/i2c/devices/12-0014/` / `readlink …/12-0014/driver` →
    still **no driver symlink** (the UNBOUND flag the power-path detect
    sweep prints for this node is a real missing link, not a display
    artifact);
  - `ls /sys/class/input/` → only `event0` = the PMIC PEK power key
    (`axp2101-pek.0`) — **no touch event device**;
  - `/boot/Image` strings → `goodix,gt9271` present in the kernel's match
    data; `strings` of
    `/lib/modules/6.6.98-vendor-sun60iw2/kernel/drivers/input/touchscreen/goodix_ts.ko`
    → of_match includes **`goodix,gt9271`**;
    `CONFIG_TOUCHSCREEN_GOODIX=m`;
  - `/sys/firmware/devicetree/base/touchscreen@14/` → node carries
    `interrupt-parent`, `interrupts`, `irq-gpios`, `reset-gpios`, and **no**
    `status` (defaults okay) — so driver present + compatible matches +
    DT-intended → the unbind is a **probe/config issue**; the operator's
    `dmesg` (root-only) is the next evidence. Journal:
    [2026-09-23-power-path-ws1-ic-identification.md](../../journal/2026-09-23-power-path-ws1-ic-identification.md).
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