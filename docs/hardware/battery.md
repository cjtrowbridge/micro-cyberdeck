# Battery (11.1 Wh LiPo)

> Status: **Not started** (mirrors the README table)

## What it is

The deck's 11.1 Wh LiPo, plugged into the GamePi HAT's two-pin battery
connector. The exact charge/discharge circuit and its relationship to the
SBC's [AXP8191](pmic-axp8191.md) remain untraced; see
[power-path.md](power-path.md) for the two USB inputs.

## What we know

- **Capacity:** 11.1 Wh (board/HAT specification).
- **Theoretical standby life:** ~13.9–14 h, derived from the deck's measured
  **~0.8 Wh** standby power draw — i.e. `11.1 Wh / 0.8 Wh/h ≈ 13.9 h`. This
  is a **standby-only** figure: it assumes the CPU idles and the GPU/NPU
  never wake; it will not survive contact with real game/agent workloads.
- **Linux does not see the battery at all today:**
  - `/sys/class/fuel/` is empty (no battery-backed class device).
  - `/sys/class/power_supply/` contains only
    `tcpm-source-psy-14-0022` — the fUSB302/TCPM USB-PD source node (see
    [usbc-power.md](usbc-power.md)) — and no `type=Battery` entry at all.
  - So there is no `capacity`, no `voltage_now`, no `status`
    (charging/discharging) available to the OS: **you cannot tell from a
    running system whether the battery is charging, discharging, or full.**
- **Possible readout path:** the AXP8191 has an **on-chip ADC with dedicated VBUS/ACIN and
  battery-voltage channels**, plus charge/adapter status bits, but the
  currently-bound `axp20x`-variant driver (bound for the PEK power button and
  its regulator rails) **exposes none of that** to Linux — no fuel-gauge
  class, no hwmon entry for the battery rails.
  The HAT photos do not establish that these SBC ADC inputs are connected to
  the HAT battery; this is a possible route, not a confirmed explanation for
  the missing battery reading.
- The companion `axp515` node on the same bus (probed but inert,
  `waiting_for_supplier=0`) is a possible alternate carrier for a
  battery-ADC the board simply never bound — but the chip identity on that
  node is itself unresolved (below). Its relevance to this battery is also
  unproven.

## How we know

- [HAT underside overview](images/PXL_20260917_070632106.jpg) and
  [battery connector close-up](images/PXL_20260917_070648424.jpg)
  (2026-09-17): the cell's red/black leads enter a two-pin HAT connector.
  An eight-pin IC and `4R7` inductor sit nearby. The IC marking is not
  reliable enough to identify its part number; these photos do not prove
  whether it charges, boosts, or measures the cell, nor whether the SBC PMIC
  can sense it.

- Live probe (September 2026 session, no sudo): `ls /sys/class/fuel/` (empty),
  `for d in /sys/class/power_supply/*; do echo "$d: $(cat $d/type 2>/dev/null)";
  done` (only `tcpm-source-psy-14-0022`, type `USB`, all property files
  empty), plus the PMIC-side regulator/chip probes documented in
  [pmic-axp8191.md](pmic-axp8191.md).
- The capacity and standby-life math come from the README's spec note under
  the parts list — verbatim: "(11.1 wh battery / 0.8 wh rated standby
  consumption = 13.9 hrs battery life)" — i.e. **11.1 Wh** capacity and a
  **0.8 Wh** *rated standby* consumption (vendor/HAT rating, not a
  deck-measured draw; no current-draw measurement of this board is on file).

## What it portends

- **First establish the electrical path:** identify the HAT's eight-pin
  power IC and trace or measure its battery and USB connections. An AXP chip
  ID read on the SBC cannot by itself show that the SBC measures this cell.

- **Battery visibility is the gate on real battery work.** Two possible
  paths to a voltage/charge readout, conditional on tracing the HAT cell:
  1. **Enable/patch the `axp20x` driver's ADC/fuel-gauge side** (the
     up/downstream kernel driver has battery-ADC support for some AXP
     variants; this only helps if the HAT cell reaches an AXP ADC input).
  2. **A userspace daemon reading an ADC over raw I2C**, if an accessible
     chip measures this cell — the `i2c-dev`
     nodes exist but are **root-only** on this board, and `i2c-tools` is not
     installed (an `apt` install needs the operator), so this path, like the
     chip-ID read below, is an operator step or a small privileged
     userspace helper, not something an unprivileged agent service can do
     directly.
- **Open question (needs the operator):** a read-only chip-ID register read
  (registers `0x00`–`0x06`) on **both** `i2c-13` addresses **0x34**
  (presented as `axp515`) and **0x36** (presented as `axp2101`-class, the
  one we know is working) — which physical chip actually carries the
  battery/charge ADC — helps establish the SBC chips' identities. It does
  not establish which one, if any, is wired to the HAT battery.
- **Deliberate decision (2026-09-16 session):** do **not** brute-force
  register writes on a live, powered PMIC "to see what happens" — the
  charge-path MOSFET enable bits are the kind of register that, written by
  accident, can disconnect the battery or the rail. Chip-ID reads are safe
  (read-only); anything else waits for the driver-level path.
- **Practical today:** the deck is **not** yet a true battery device as far
  as software is concerned — no "15 min of battery left" warning, no
  "stop charging to preserve the cell" logic, no way for the agent software
  to even *know* it's on battery vs. plugged in (that distinction lives in
  [power-path.md](power-path.md) and is equally unobservable today).
- The ~14 h standby figure is a planning number, not a guarantee — once
  [soc-thermal.md](soc-thermal.md)'s zones are what they look like under a
  real game workload, the practical runtime on a full battery is a much
  smaller and not-yet-measured number.
