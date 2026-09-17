# USB-C power negotiation (fusb302/TCPM)

> Status: **Automatic** (mirrors the README table)

## What it is

The SBC's own USB-C power-delivery path: an **fUSB302** PD (power delivery)
controller on `i2c-14` at address `0x22`, handled by the kernel's TCPM
(type-C port management) code, exposing a `power_supply` node for the
"source" (charger) side of the negotiation.

## What we know

- **The node exists and is the kernel's stub.**
  `/sys/class/power_supply/tcpm-source-psy-14-0022` — `type=USB`, and every
  property that would indicate an actual negotiation result
  (`in0_input`, `curr1_input`, and the like) reads **0 / empty**.
- In the deck's normal configuration (powered from the **HAT's microUSB**,
  see [power-path.md](power-path.md)), **this is expected and correct, not a
  fault** — nothing is plugged into the SBC's own port, so there's nothing
  for the PD negotiation to have negotiated. "Automatic" reflects that: the
  kernel is doing its job (the node binds, no user action involved), it just
  has nothing to report.
- No user-visible feature, no `setup.sh` verify row, no journal finding
  beyond "it exists and reads zero in the current power configuration."

## How we know

- Live probes (September 2026 session, no sudo):
  `cat /sys/class/power_supply/tcpm-source-psy-14-0022/type` (`USB`),
  `cat /sys/class/power_supply/tcpm-source-psy-14-0022/in0_input`
  (0), `cat /sys/class/power_supply/tcpm-source-psy-14-0022/curr1_input` (0).
- The node-to-chip mapping (`14-0022` = fUSB302 on `i2c-14`) is confirmed
  **without root**: `cat /sys/bus/i2c/devices/14-0022/name` → `fusb302`,
  and `ls -l …/14-0022/driver` → binds `typec_fusb302`.

## What it portends

- **The moment anything is plugged into the SBC's own USB-C port**, this
  node is where the actual negotiated voltage/current would appear — this is
  the observable half of the
  "SBC-USB-C-only" configuration in
  [power-path.md](power-path.md)'s proposed test matrix.
- Until that test is run, this row and
  [power-path.md](power-path.md) describe the **same underlying unknown**
  from two angles: this one says "the negotiation machinery is present and
  idle," the other says "we don't know what it would negotiate, or how it
  interacts with the HAT's own feed."
- No work is planned for this row in isolation — it's a **supporting sensor**
  for the power-path work, and should be re-read (not provisioned, not
  patched) whenever that arc gets a test run.