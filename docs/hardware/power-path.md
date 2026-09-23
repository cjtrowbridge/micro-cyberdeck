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
  (expect `USB`, `0`, `0`, `0` in the HAT-powered config) — the older
  `in0_input`/`curr1_input` names no longer exist on this kernel (property
  naming changed; verified 2026-09-23, WS0 re-probe, journal
  `2026-09-23-power-path-ws0-baseline.md`); see
  [usbc-power.md](usbc-power.md) for the exact node.
- No journal entry documents a "both plugged in" or "SBC-USB-C-only" test
  yet — that's the point of this row existing as **Not started** rather than
  **Done** with caveats.

## What it portends

- **Proposed test matrix (requires battery and input measurements):** run the deck
  in three configurations — (a) HAT microUSB only (the status quo), (b) SBC
  USB-C only, (c) both — and record, for each: input voltage/current on each
  port, battery current (charging vs. not), and whether the board stays up
  under a nominal game workload. This is the concrete "does the SBC's own
  port actually work as a power source" answer. A bench meter may be needed
  if the HAT cell has no software-readable monitor.
- Until that matrix is run, the safe assumption is **the HAT's microUSB is
  the only verified power source** and any deployment instructions (e.g. "just
  plug in the SBC's USB-C") should not be written on the strength of this
  row.
- This is **not** a blocker for any currently-planned feature row — it's a
  correctness/completeness item that becomes urgent the first time the deck
  is used in a configuration other than "powered from the HAT's cable."
