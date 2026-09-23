# PMIC (AXP8191 rails)

> Status: **Partial** (mirrors the README table)

## What it is

The SBC's power-management IC. The SBC's active and only functional PMIC is
an **AXP8191**; the device tree also declares a second PMIC the kernel
presents as **AXP515** on the same bus, but WS1 (2026-09-23) showed that node
to be **wire-dead** — the chip it names is not answering on the bus and no
mainline driver for it exists (see below). The PMIC provides the board's
regulated power rails and, as a side effect, the hardware backing for the
[power-button.md](power-button.md) device.
The GamePi HAT also has a local power circuit near its battery connector;
its connection to these SBC PMICs is not established.

## What we know

- **Two chips, one bus (`i2c-13`):**
  - **`axp8191` @ `0x36`** — the active one. Binds the in-kernel
    `axp20x-i2c` driver dir, which — per the kernel config (mainline
    `MFD_AXP20X` **not** set) — is the **vendor Allwinner AXP2101-family MFD
    driver** (`CONFIG_AW_MFD_AXP2101_I2C=y`; it keeps the historic
    `axp20x-i2c` i2c-client name). It exposes the PEK power-button input as
    `axp2101-pek.0` (see [power-button.md](power-button.md)), the rails as
    `axp2101-regulator.0`, and the unused thermal node as
    `axp8191-temp-ctrl.0` — matching `CONFIG_AW_INPUT_AXP2101_PEK=y` /
    `CONFIG_AW_REGULATOR_AXP2101=y` / `CONFIG_AW_AXP8191_TEMP_CTRL=y`
    one-for-one.
  - **`axp515` @ `0x34`** — a **DT-enumerated but wire-dead** node. The
    device tree declares `pmu@34` with `compatible = x-powers,axp515` and it
    enumerates (`13-0034`, `waiting_for_supplier=0`), but **nothing answers
    at address `0x34` on the wire**: WS1's read-only chip-ID reads
    (2026-09-23, `i2cget -y 13 0x34 0x00`…`0x06`, all NAK — "Read failed")
    and the detect sweep (no responder at `0x34`) both say the silicon the
    DT expects there is absent, unpowered, or not in this deck's power
    path. It is **not** a functional companion PMIC, and **there is no
    mainline driver nor DT binding for `x-powers,axp515` at all** (text
    search of `torvalds/linux`: zero hits), so no driver could ever bind it
    even if it did answer. **Resolved 2026-09-23 (WS1):** the September-2026
    open question — "does axp515 carry the battery ADC?" — is answered **No
    for this board** (see [battery.md](battery.md)).
- **The rails:** the `axp8191` node exposes roughly **40** regulator rails —
  `dcdc1`–`dcdc9`, `dc1sw1`/`dc1sw2`, `aldo1`–`aldo6`, `bl_do1`–`bl_do5` (LED
  boost), `cldo1`–`cldo5`, `dl_do1`–`dl_do6`, `eldo1`–`eldo6`, `rtc_ldo`, and
  the like. The consumer side (what each rail actually powers on this board)
  maps onto the SoC's functional blocks: NPU, GPU, PCIe, SDMMC, UFS,
  `hdmi0`, `edp0`, the `pwm-fan` (see [fan.md](fan.md)), the `twi*` I2C
  controllers, `dsufreq`, and the `usbc0` controller.
- **The driver exposes names, not live values.** The regulator sysfs entries
  for these rails show their names/roles, but **no current output voltage or
  load is readable** — the bound driver (vendor AW AXP2101-family MFD, see
  above) does not report live µV per rail.
- **Thermal control node exists but is unused:** the PMIC has an internal
  "temp-ctrl" capability (its own on-die thermal sensing, separate from — and
  not to be confused with — the SoC's own thermal sensors, see
  [soc-thermal.md](soc-thermal.md)), but there is **no `temp` or `hwmon`
  sysfs entry** for it today; nothing reads it.

## How we know

- [HAT underside overview](images/PXL_20260917_070632106.jpg) and
  [battery connector close-up](images/PXL_20260917_070648424.jpg)
  (2026-09-17) show a separate eight-pin IC and `4R7` inductor on the HAT.
  No marking or visible trace in these images ties this circuit to either
  SBC I2C address.

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
- **WS1 wire reads (2026-09-23, operator under `sudo`; `i2c-tools` installed
  per plan decision D3; deck on its HAT-microUSB feed throughout; read-only
  chip-ID registers `0x00`–`0x06`; packaged in `tools/i2c_power_probe.sh`):**
  - `i2cget -y 13 0x36 0x00`…`0x06` → **all seven reads fail with EBUSY
    ("Could not set address to 0x36: Device or resource busy")** — the bound
    `axp20x-i2c` driver holds the chip exclusively, so a numeric ID could not
    be read without unbinding the live driver (refused — that would be a
    write-class disturbance of the in-service rail path). This *confirms*
    `0x36` is the live, driver-claimed PMIC.
  - `i2cget -y 13 0x34 0x00`…`0x06` → **all seven fail with "Read failed"**
    — a wire **NAK** at an unclaimed address (the busy-but-connecting `0x36`
    in the same run proves bus + tool are working), i.e. **nothing answers at
    `0x34`**: the `axp515` the DT expects is absent, unpowered, or not in this
    deck's power path.
  - Detect sweep (`i2cdetect -y`, detect-only) across **all seven
    instantiated adapters** (`i2c-9 11 12 13 14 15 20` — DT enumerates 16
    `twi@` nodes but only these instantiate on this DTS): **no new
    unclaimed responder on any bus**; the only raw responder anywhere is
    `0x30` on `i2c-20` = the HDMI controller's CEC/DDC bus — not a
    power-path bus, recorded and left alone. Verbatim output:
    `/tmp/ws1_i2c_probe2.log` on the board (2026-09-23 12:00); journal
    [`2026-09-23-power-path-ws1-ic-identification.md`](../../journal/2026-09-23-power-path-ws1-ic-identification.md).
  - Literature pass (same change): **no driver or DT binding for
    `x-powers,axp515` exists in mainline (`torvalds/linux`, zero text hits)**
    while `x-powers,axp8191` is present there — the `axp515` (and the running
    "vendor" kernel's support for both) comes from the Armbian/vendor tree,
    and the `axp515` node could never bind on a mainline kernel.
- No `setup.sh` verify row checks rail voltages or the PMIC's temperature —
  nothing has ever been exposed to read them (see "What it portends").

## What it portends

- **This is the umbrella row behind two of the deck's other open rows:**
  [battery.md](battery.md) (charge/ADC visibility) and
  [usbc-power.md](usbc-power.md) (why the SBC's own port reads all zeros).
  The PMIC driver does not expose its full telemetry, but its relevance to
  the HAT battery remains unproven. Trace the HAT power path before treating
  an SBC PMIC driver change as the battery fix.
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
