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
  missing-driver or compatible-mismatch issue**.
- **Mechanism resolved (2026-09-23, operator boot log `/var/log/kern.log`):**
  the probe **ran** at boot and **failed** — it is not an un-attempted or
  deferred bind. Verbatim (2026-09-20T16:04:51 — the flash/setup date;
  same failure at every boot since; the node is still unbound today):
  ```
  Goodix-TS 12-0014: supply AVDD28 not found, using dummy regulator
  Goodix-TS 12-0014: supply VDDIO not found,using dummy regulator
  Goodix-TS 12-0014: Error reading 1 bytes from 0x8140: -22   (×2)
  Goodix-TS 12-0014: I2C communication failure: -22
  Goodix-TS 12-0014: probe of 12-0014 failed with error -22
  ```
  Reading: the IRQ/RST GPIOs were **acquired successfully** (the driver's
  "Failed to get %s GPIO" error exists in its message set and is **not**
  in the log); AVDD28/VDDIO fall back to **dummy regulators** (non-fatal —
  the DT node declares no regulator supplies); but the first data
  transaction — the **chip version-register read at `0x8140`** (the
  standard "does the chip answer" probe read) — fails with **-22 (EINVAL)**
  and the probe aborts with **no retry**. Combined with the WS1 raw
  `i2cdetect` grid showing **no quick-probe ACK at `0x14` on `i2c-12`**
  (bus 12 all `--`), the evidence points to **the chip not responding on
  the wire** — absent, unpowered, held in reset, or the I2C lines not
  where the DT expects. This is a **hardware/wiring-level issue**, not a
  driver, compatible, GPIO, or DT-config issue: every OS-side precondition
  is demonstrably in place. (Note for the record: the earlier zero-hit
  `dmesg | grep -iE 'gt9271|hym8563'` missed this because the driver logs
  as **`Goodix-TS`**, a name containing neither grep term.)
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
  - `/sys/firmware/devicetree/base/soc@3000000/twi@251C000/touchscreen@14/`
    → node carries `interrupt-parent`, `interrupts`, `irq-gpios`,
    `reset-gpios`, and **no** `status` (defaults okay) — driver present +
    compatible matches + DT-intended → the unbind is a **probe/config issue**;
  - **operator boot log (2026-09-23):** `sudo grep -iE 'goodix|12-0014|
    hym8563|15-0051' /var/log/kern.log` → the probe **failed at boot**:
    `Goodix-TS 12-0014: Error reading 1 bytes from 0x8140: -22` (×2) →
    `I2C communication failure: -22` → `probe of 12-0014 failed with error
    -22` (AVDD28/VDDIO fell back to dummy regulators; the "Failed to get
    %s GPIO" error is absent → IRQ/RST GPIOs acquired). The driver logs as
    **`Goodix-TS`** — which is why the earlier `dmesg | grep -iE
    'gt9271|hym8563'` returned zero lines (neither term matches that name,
    and the ring buffer had wrapped anyway). Journal:
    [2026-09-23-power-path-ws1-ic-identification.md](../../journal/2026-09-23-power-path-ws1-ic-identification.md).
- The HAT vendor's GamePi13 wiring reference describes the I2C +
  INT/RST-GPIO topology generally, but the pin assignment for *this* board
  needs the schematic (the `button-map.py` run once named as a candidate source
  for the HAT pin assignments was the membrane-keypad probe — closed
  2026-09-23, see [membrane-buttons.md](membrane-buttons.md); the touch IC's
  pin identity stays open with the power-path WS1 session).

## What it portends

- Once bound, touch arrives for free as a standard evdev device — the stack
  beyond that (UI hit-testing at 960×960) is pure software.
- **Gating chain:** (1) the HAT pinout schematic (INT/RST GPIOs), (2) a
  pinctrl/DT binding or overlay for those pins, (3) the `i2c-gt911` driver
  (mainline — it covers the GT927 family). The same mechanism class as the
  buttons overlay, so the `setup.sh` plumbing is already understood.
- This unblocks **on-screen interaction** — currently the only inputs are the
  13 membrane keys (also unbound) and SSH.