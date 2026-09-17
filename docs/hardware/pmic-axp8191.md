# PMIC (AXP8191 rails)

> Status: **Partial** (mirrors the README table)

## What it is

The board's power-management IC. The deck's active PMIC is an **AXP8191**,
plus a companion/secondary chip the kernel presents as an **AXP515** on the
same bus. The PMIC provides the board's regulated power rails and, as a side
effect, the hardware backing for the [power-button.md](power-button.md)
device.

## What we know

- **Two chips, one bus (`i2c-13`):**
  - **`axp8191` @ `0x36`** — the active one. Binds the in-kernel `axp20x`
    variant driver (the driver family covering the AXP20x/AXP8xxx line),
    which in turn exposes the PEK power-button input (see
    [power-button.md](power-button.md)) and the board's regulator rails.
  - **`axp515` @ `0x34`** — probed at boot (`waiting_for_supplier=0`, i.e.
    the probe ran and completed) but **inert**: no useful sysfs output of its
    own beyond the probe itself. (Its exact role, and whether it's the chip
    that actually carries the battery-ADC the
    [battery.md](battery.md) row wants, is an open question — resolving it
    needs the operator chip-ID read described there.)
- **The rails:** the `axp8191` node exposes roughly **40** regulator rails —
  `dcdc1`–`dcdc9`, `dc1sw1`/`dc1sw2`, `aldo1`–`aldo6`, `bl_do1`–`bl_do5` (LED
  boost), `cldo1`–`cldo5`, `dl_do1`–`dl_do6`, `eldo1`–`eldo6`, `rtc_ldo`, and
  the like. The consumer side (what each rail actually powers on this board)
  maps onto the SoC's functional blocks: NPU, GPU, PCIe, SDMMC, UFS,
  `hdmi0`, `edp0`, the `pwm-fan` (see [fan.md](fan.md)), the `twi*` I2C
  controllers, `dsufreq`, and the `usbc0` controller.
- **The driver exposes names, not live values.** The regulator sysfs entries
  for these rails show their names/roles, but **no current output voltage or
  load is readable** — the `axp20x` driver variant bound here does not report
  live µV per rail.
- **Thermal control node exists but is unused:** the PMIC has an internal
  "temp-ctrl" capability (its own on-die thermal sensing, separate from — and
  not to be confused with — the SoC's own thermal sensors, see
  [soc-thermal.md](soc-thermal.md)), but there is **no `temp` or `hwmon`
  sysfs entry** for it today; nothing reads it.

## How we know

- Live probes (September 2026 session, no sudo for any of these — the
  sysfs attributes are world-readable):
  - `ls /sys/bus/i2c/devices/` → `13-0036` and `13-0034`; the world-readable
    `name` attribute of each gives the **chip identity without dmesg**:
    `cat …/13-0036/name` → **`axp8191`**, `cat …/13-0034/name` → **`axp515`**.
  - `ls -l …/13-0036/driver` → binds **`axp20x-i2c`** (the driver probe
    registered the PEK key input as `axp2101-pek.0/input/input0` — see
    [power-button.md](power-button.md), whose by-path link
    `platform-7083000.twi-platform-axp2101-pek.0-event` names it); no
    `driver` link under `13-0034`.
  - `ls /sys/class/regulator/` (the ~40 rail names).
  - `cat /sys/bus/i2c/devices/13-0034/waiting_for_supplier` (0).
- The "chip ID: axp2101-class @ 0x36 is the working one" / "axp515 @ 0x34"
  split comes from the **kernel boot log** — the operator's
  `dmesg`/`/var/log/kern.log`, which the agent cannot read directly (root-only
  on this board; `dmesg` returns `Operation not permitted` without it).
- No `setup.sh` verify row checks rail voltages or the PMIC's temperature —
  nothing has ever been exposed to read them (see "What it portends").

## What it portends

- **This is the umbrella row behind two of the deck's other open rows:**
  [battery.md](battery.md) (charge/ADC visibility) and
  [usbc-power.md](usbc-power.md) (why the SBC's own port reads all zeros).
  Both are ultimately "the PMIC driver isn't giving Linux what the hardware
  is capable of" — getting either one to work starts here.
- **No live-voltage telemetry means no "is a rail sagging under load"
  diagnostics** available for when a future performance/tuning pass (NPU
  inference load, GPU workload) starts behaving oddly — a gap, not a bug,
  worth knowing exists rather than discovering late.
- The unused `temp-ctrl` node is a **free capability** if the driver ever
  exposes it: a PMIC-die temperature would be a much earlier/cheaper trip
  signal for the fan policy in [fan.md](fan.md) than waiting for one of the
  SoC's own thermal zones to trip — currently there is no OS-visible consumer
  of it, and nothing in this repo plans for one yet.
- **Deliberate caution (carried over from the battery-ADC session, 2026-09-16
  window):** any work on this chip's registers is read-only until we know
  exactly which chip is which; register *writes* against a live, powered PMIC
  (e.g. poking at charge-path MOSFET enable bits) are explicitly out of scope
  for unattended/agent-driven work.