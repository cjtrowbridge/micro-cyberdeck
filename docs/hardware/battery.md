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
  - `/sys/class/fuel/` is absent entirely (no fuel-gauge driver registered,
    so the class directory never materialized — re-verified 2026-09-23,
    WS0 re-probe; journal
    [2026-09-23-power-path-ws0-baseline.md](../../journal/2026-09-23-power-path-ws0-baseline.md)).
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
  the missing battery reading. **WS1 (2026-09-23) narrows this to the one
  standing on-SBC candidate:** the full-bus detect sweep found **no
  I2C-addressable charger/gauge IC on the HAT's power path at all** — the
  HAT's eight-pin IC is not I2C-visible to the SBC (see
  [power-path.md](power-path.md)) — so the only way the SBC could sense this
  cell is via the AXP8191's own ADC *if* the HAT circuit ties the cell (or a
  divider from it) into one of the AXP's sense inputs. That is unproven and
  will be tested with meter measurements in WS2 (charge behavior under the
  three feed configurations), since **no software path to that ADC exists
  today either way**.
- **The companion `axp515` node is killed as a battery-ADC path (WS1,
  2026-09-23).** It enumerates on the same bus (`13-0034`,
  `waiting_for_supplier=0`) but **nothing answers at `0x34` on the wire** —
  all seven read-only chip-ID reads NAKed — and **no mainline driver/binding
  for `x-powers,axp515` exists at all** (zero text hits in `torvalds/linux`).
  The September-2026 open question ("is axp515 the unbound carrier of a
  battery ADC?") is answered **No for this board**: not a usable second PMIC
  (see [pmic-axp8191.md](pmic-axp8191.md)).

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
- WS1 wire evidence (2026-09-23, operator under `sudo` via
  `tools/i2c_power_probe.sh`; verbatim log `/tmp/ws1_i2c_probe2.log`;
  journal [2026-09-23-power-path-ws1-ic-identification.md](../../journal/2026-09-23-power-path-ws1-ic-identification.md)):
  - chip-ID reads on both `i2c-13` addresses: `0x36` → **EBUSY** (the bound
    `axp20x-i2c` holds it; raw reads by design refused) and `0x34` →
    **wire NAK** (nothing there) — the axp515 alternate-carrier path above
    is thus killed on wire evidence, not just driver absence;
  - detect-only `i2cdetect` sweep of **all seven instantiated adapters**
    (`9 11 12 13 14 15 20`): no new unclaimed I2C responder anywhere on a
    header-reachable bus — i.e. **no I2C-visible charger/gauge chip on the
    HAT power path**, which kills the userspace-raw-I2C readout path (below)
    as a battery-telemetry route on this deck.
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
  2. **A userspace daemon reading an ADC over raw I2C** — **closed by
     WS1 (2026-09-23):** the full-bus detect sweep found **no
     I2C-addressable chip measuring this cell** (the HAT's eight-pin IC is
     not I2C-visible to the SBC; the only unclaimed `i2c-13` address
     answering is nothing — `0x34` NAKs, `0x36` is the driver-held PMIC).
     There is no chip to point a raw-I2C reader at, so this path is dead
     for this deck. (`i2c-tools` was installed per plan decision D3;
     `i2c-dev` nodes remain root-only — moot now, since no target chip
     exists on the wire.)
- **Chip-ID question — answered (WS1, 2026-09-23):** the read-only
  chip-ID reads (registers `0x00`–`0x06`) on both `i2c-13` addresses were
  run, with decisive and complementary results: **`0x36` refused with
  EBUSY** (the bound `axp20x-i2c` grips the live PMIC — identity there
  stands as the DT-provided `x-powers,axp8191`; we deliberately did not
  unbind the live driver to force a numeric ID, that being a write-class
  disturbance of the live rail path) and **`0x34` NAKed on every read**
  (no silicon there, and no mainline driver could read it anyway). Neither
  read established a battery-ADC carrier — both on-SBC candidates are
  accounted for: the AXP8191's own ADC (candidate 1, unproven, WS2 meter
  test) and the axp515 (killed, see "What we know").
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
