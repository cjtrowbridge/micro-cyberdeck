# Power path (USB-C / HAT microUSB / battery)

> Status: **Not started** (mirrors the README table)

## What it is

The routing of electrical power into the board: two USB inputs (the SBC's own
USB-C port, with its fUSB302 PD chip — see [usbc-power.md](usbc-power.md) —
and the HAT's own microUSB port), and the battery (see
[battery.md](battery.md)). The relationship between the HAT's local power
circuit and the SBC's [AXP8191 PMIC](pmic-axp8191.md) has not been traced.

## What we know

- **Today's actual configuration:** the deck is powered from the **HAT's
  microUSB** port. The HAT carries a separate eight-pin IC beside the cell
  connector and `4R7` inductor. Which IC controls charging and which node
  feeds the SBC remain to be established (see [battery.md](battery.md)).
- **The SBC's own USB-C port is a separate input**, negotiated by the fUSB302
  over its I2C/TCPM path — but the kernel's power-supply stub for it reports
  `online=0`, `voltage_now=0`, `current_now=0` — every readable property zero
  (see [usbc-power.md](usbc-power.md)), i.e. **the OS is not currently seeing
  any negotiated power on the SBC's port** in the deck's normal (HAT-powered)
  configuration. The 2026-09-23 re-probe (WS0 of the power-path plan) also
  found that the fuel-gauge class directory `/sys/class/fuel/` is **absent
  entirely** (not merely empty) and that the tcpm node now carries a
  `hwmon0` child — see journal
  [2026-09-23-power-path-ws0-baseline.md](../../journal/2026-09-23-power-path-ws0-baseline.md).
- **The interaction between the two inputs is untested** — specifically:
  - What happens when *both* the HAT's microUSB and the SBC's USB-C are
    plugged in at the same time (which wins? do they fight? does the PMIC
    just ignore the second source?).
  - Whether the SBC-side USB-C alone can power the whole board + HAT at a
    level the board actually accepts (no measurement has been taken with the
    HAT microUSB unplugged and only the SBC USB-C connected).
- The HAT's charging and source-selection circuitry has not yet been
  identified. The current Linux power-supply nodes provide no battery
  telemetry (see [battery.md](battery.md)); a PMIC driver change will only
  help if the relevant HAT signals reach that PMIC.
- **WS1 census (2026-09-23, detect-only I2C sweep):** the HAT's eight-pin
  power IC is **not I2C-visible to the SBC at all.** All seven instantiated
  adapters (`i2c-9 11 12 13 14 15 20` — DT enumerates 16 `twi@` nodes but
  only these instantiate) were swept with `i2cdetect -y`: the only
  unclaimed raw responder found anywhere is `0x30` on **`i2c-20`**, which is
  the **HDMI controller's CEC/DDC bus** (`5520000.hdmi0`) — not a
  power-path bus, recorded and left alone per the detect-only rule. Every
  other responding address is a known, kernel-claimed node (`axp8191`/
  `axp20x-i2c` @ `13-0036`, unclaimed-but-wire-dead `13-0034`, `fusb302` @
  `14-0022`, unbound `gt9271` @ `12-0014`, unbound `hym8563` @ `15-0051`).
  Consequence: **the HAT's charge/source-selection logic is not exposed on
  the SBC's I2C fabric** — its IC either has no I2C side or its I2C lines
  are not on any of these buses — so the only I2C-visible power device on
  the SBC is the AXP8191 itself.

## How we know

- [HAT underside overview](images/PXL_20260917_070632106.jpg) and
  [battery connector close-up](images/PXL_20260917_070648424.jpg)
  (2026-09-17) show the HAT's microUSB input, battery connector, eight-pin
  IC and inductor. They do not show the internal nets or source-selection
  behavior.

- The "powered from the HAT microUSB" fact is observed directly (the cable
  that keeps the deck running is the HAT's, not the SBC's) — this is
  configuration-as-observed, not something a `setup.sh` verify row checks.
- The SBC-side USB-C all-zero reading: re-probe
  `cat /sys/class/power_supply/tcpm-source-psy-14-0022/{type,online,voltage_now,current_now}`
  (expect `USB`, `0`, `0`, `0` in the HAT-powered config); the older
  hwmon-style `in0_input`/`curr1_input` names are no longer at the top
  level of the node but live in its `hwmon0` child (still all zero —
  naming/path changed, the all-zero state did not; verified 2026-09-23,
  WS0 re-probe, journal `2026-09-23-power-path-ws0-baseline.md`); see
  [usbc-power.md](usbc-power.md) for the exact node.
- No journal entry documents a "both plugged in" or "SBC-USB-C-only" test
  yet — that's the point of this row existing as **Not started** rather than
  **Done** with caveats.
- WS1 detect sweep (2026-09-23, operator under `sudo`; deck on HAT-microUSB
  feed; `i2c-tools` per plan decision D3; script `tools/i2c_power_probe.sh`):
  - per-bus sysfs claimer list (`/sys/bus/i2c/devices/*`) + `i2cdetect -y`
    raw grids on all seven instantiated adapters (completeness proven
    against the DTS — 16 `twi@` nodes, 7 instantiated);
  - the only unclaimed raw responder on any bus: `0x30` on `i2c-20` = the
    HDMI CEC/DDC bus (out of this record's scope, left alone);
  - caveat recorded in the journal: the raw `i2cdetect` grids mangled in
    paste and under-marked bound devices, so the sysfs claimer list and the
    targeted chip-ID reads (EBUSY `0x36` / NAK `0x34`, see
    [pmic-axp8191.md](pmic-axp8191.md)) are the trustworthy evidence; the
    grids are used only to catch *new* responders;
  - verbatim log: `/tmp/ws1_i2c_probe2.log` on the board (2026-09-23 12:00);
    journal [2026-09-23-power-path-ws1-ic-identification.md](../../journal/2026-09-23-power-path-ws1-ic-identification.md).

## What it portends

- **Proposed test matrix (requires battery and input measurements):** run the deck
  in three configurations — (a) HAT microUSB only (the status quo), (b) SBC
  USB-C only, (c) both — and record, for each: input voltage/current on each
  port, battery current (charging vs. not), and whether the board stays up
  under a nominal game workload. This is the concrete "does the SBC's own
  port actually work as a power source" answer. **WS1 (2026-09-23) settled
  the tooling question:** the HAT cell has **no** software-readable monitor
  and no I2C-visible charge IC (see "What we know" census above), so the
  **bench meter is the primary instrument** for charge behavior — there is
  no telemetry path the matrix can ride on. The only unproven on-SBC
  candidate left is the AXP8191's own ADC (if the HAT circuit wires the
  cell into it — see [battery.md](battery.md)); the meter results will show
  whether that signal exists before anyone invests in driving it.
- Until that matrix is run, the safe assumption is **the HAT's microUSB is
  the only verified power source** and any deployment instructions (e.g. "just
  plug in the SBC's USB-C") should not be written on the strength of this
  row.
- This is **not** a blocker for any currently-planned feature row — it's a
  correctness/completeness item that becomes urgent the first time the deck
  is used in a configuration other than "powered from the HAT's cable."
