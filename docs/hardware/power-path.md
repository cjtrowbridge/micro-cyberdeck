# Power path (USB-C / HAT microUSB / battery)

> Status: **Not started** (mirrors the README table)

## What it is

The routing of electrical power into the board: two USB inputs (the SBC's own
USB-C port, with its fUSB302 PD chip — see [usbc-power.md](usbc-power.md) —
and the HAT's own microUSB port), and the battery (see
[battery.md](battery.md)), all arbitrated by the PMIC (see
[pmic-axp8191.md](pmic-axp8191.md)).

## What we know

- **Today's actual configuration:** the deck is powered from the **HAT's
  microUSB** port; that feed goes into the PMIC's charge/adapter path (the
  same circuitry that would charge the battery, see
  [battery.md](battery.md)).
- **The SBC's own USB-C port is a separate input**, negotiated by the fUSB302
  over its I2C/TCPM path — but the kernel's power-supply stub for it reports
  `in0_input=0`, `curr1_input=0`, all-empty properties (see
  [usbc-power.md](usbc-power.md)), i.e. **the OS is not currently seeing any
  negotiated power on the SBC's port** in the deck's normal (HAT-powered)
  configuration.
- **The interaction between the two inputs is untested** — specifically:
  - What happens when *both* the HAT's microUSB and the SBC's USB-C are
    plugged in at the same time (which wins? do they fight? does the PMIC
    just ignore the second source?).
  - Whether the SBC-side USB-C alone can power the whole board + HAT at a
    level the board actually accepts (no measurement has been taken with the
    HAT microUSB unplugged and only the SBC USB-C connected).
- Because all of this is governed by the PMIC's charge-path registers, and
  that chip is not measured/exposed to Linux today (see
  [battery.md](battery.md)), **none of this is observable from the OS
  without the battery-ADC work landing first.**

## How we know

- The "powered from the HAT microUSB" fact is observed directly (the cable
  that keeps the deck running is the HAT's, not the SBC's) — this is
  configuration-as-observed, not something a `setup.sh` verify row checks.
- The SBC-side USB-C all-zero reading: re-probe `cat
  /sys/class/hwmon/hwmon*/in*` and `cat
  /sys/class/power_supply/tcpm-source-psy-14-0022/type` (see
  [usbc-power.md](usbc-power.md) for the exact node).
- No journal entry documents a "both plugged in" or "SBC-USB-C-only" test
  yet — that's the point of this row existing as **Not started** rather than
  **Done** with caveats.

## What it portends

- **Proposed test matrix (blocked on the battery-ADC work):** run the deck
  in three configurations — (a) HAT microUSB only (the status quo), (b) SBC
  USB-C only, (c) both — and record, for each: input voltage/current on each
  port, battery current (charging vs. not), and whether the board stays up
  under a nominal game workload. This is the concrete "does the SBC's own
  port actually work as a power source" answer, and it's the same
  measurement capability [battery.md](battery.md) is gated on.
- Until that matrix is run, the safe assumption is **the HAT's microUSB is
  the only verified power source** and any deployment instructions (e.g. "just
  plug in the SBC's USB-C") should not be written on the strength of this
  row.
- This is **not** a blocker for any currently-planned feature row — it's a
  correctness/completeness item that becomes urgent the first time the deck
  is used in a configuration other than "powered from the HAT's cable."