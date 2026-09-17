# NPU (3 TOPS INT8)

> Status: **Not started** (mirrors the README table)

## What it is

The Allwinner A733's integrated neural-processing unit — nominally
**3 TOPS at INT8** — the piece of silicon the deck's "run local models"
promise (see README, "Recursive Self-Improvement") is ultimately meant to
lean on.

## What we know

- **The silicon is real, present, and powered** — proven, not claimed:
  - A dedicated **`npu` thermal zone** exists and reports temperature
    (45.57 °C at the September 2026 idle probe, see
    [soc-thermal.md](soc-thermal.md)) — a thermal zone for a unit only exists
    if the SoC actually instantiates and powers it.
  - A **regulator rail consumer is registered for it**
    (`reg-virt-consumer …/npu` under the PMIC's regulator class, see
    [pmic-axp8191.md](pmic-axp8191.md)) — the board's power tree already
    budgets a rail for the NPU.
- **Nothing runs on it yet:** no NPU driver is bound, no SDK/runtime
  (the vendor's NPU toolchain + framework binding) is installed, and no
  local-inference workload in this repo targets it — `api.sample.yaml`
  describes the host's *local inference* setup generically, and today any
  "local inference" on the deck means **CPU** (the 8-core little/big complex),
  not the NPU.
- This is a driver/SDK gap, not a hardware gap — same class of "present but
  unbound" row as [rtc-hym8563.md](rtc-hym8563.md), [touch-gt9271.md](touch-gt9271.md).

## How we know

- Live probe (September 2026 session, no sudo): the `npu` thermal zone
  reading (see [soc-thermal.md](soc-thermal.md) for the exact loop) and the
  `/sys/class/regulator/` consumer list entries under
  [pmic-axp8191.md](pmic-axp8191.md)'s rail inventory.

## What it portends

- **This is the row behind the deck's core promise** — "powerful NPU built
  in, take over the work on itself essentially as soon as you flash
  Armbian" — and it is the single largest **long-lead** item in the whole
  hardware map: NPU bring-up on a brand-new SoC (vendor SDK, board-specific
  config, no Armbian-tested image for this board yet, per the
  [`Diagnose XFCE Freeze.md`](../../Diagnose%20XFCE%20Freeze.md) finding that
  Armbian only ships CLI images for the 3W) is a much larger effort than any
  other row in this directory.
- Until this lands, every "local model" plan in this deck should be scoped
  to **what the CPU can run**, and the NPU should be treated as
  "future, uncharacterized" — including in any [soc-thermal.md](soc-thermal.md)
  thermal trip planning: the `npu` zone is the early-warning sensor for when
  this row becomes active.
- No `setup.sh` verify row covers it, and none should be added until a
  driver/SDK actually exists to verify *existence of* (today there's
  nothing to probe for other than the thermal zone and rail consumer, which
  are the "it's there and powered" proof above).