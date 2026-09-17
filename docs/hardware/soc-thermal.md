# SoC temperature (CPU/DDR/GPU/NPU)

> Status: **Done** (mirrors the README table)

## What it is

The Allwinner A733 SoC's **own** on-die thermal sensors, exposed through the
standard `thermal_zone*` / `cooling_device*` kernel interface. These are
**distinct from** the PMIC's separate internal `temp-ctrl` sensing (see
[pmic-axp8191.md](pmic-axp8191.md)), which is present but not exposed.

## What we know

- **8 thermal zones** exist, each reporting temperature in millidegrees
  Celsius under `/sys/class/thermal/thermal_zone*/temp`. Sample reading at
  the September 2026 probe (idle board):

  | Zone | Reading | °C |
  |---|---|---|
  | `cpub` (CPU big core) | 50096 | 50.1 |
  | `cpul` (CPU little core) | 52080 | 52.1 |
  | `cpul_idle` | 50964 | 51.0 |
  | `cpub_idle` | 50592 | 50.6 |
  | `ddr` | 46748 | 46.7 |
  | `gpu` | 47058 | 47.1 |
  | `npu` | 45570 | 45.6 |
  | `skin` (case-relevant surface) | 34118 | 34.1 |

  Note the two "idle" companion zones per cluster — the SoC tracks the
  active-core and core-idle sensor separately, which is more telemetry than a
  typical desktop SoC offers.
- **10 cooling devices** exist under `/sys/devices/virtual/thermal/`,
  including the SoC's own frequency-cap trip points and **one entry that is
  the `pwmfan` hwmon** (see [fan.md](fan.md)) — i.e. the kernel's thermal
  framework already has a fan-shaped cooling device in its model, it's just
  at 0% with no policy driving it (that's the whole [fan.md](fan.md) story).
- `skin` at ~34 °C while the CPU big core reads ~50 °C is the direct,
  OS-visible evidence of the **case/thermal problem** in the README's Known
  Unresolved section (heat is generated on-die faster than the case + tiny
  fan can move it).

## How we know

- Live probe (September 2026 session, no sudo, read-only sysfs):
  ```sh
  for z in /sys/class/thermal/thermal_zone*; do
    echo "$(cat $z/type): $(cat $z/temp)"
  done
  ls /sys/devices/virtual/thermal/
  ```
- No `setup.sh` verify row touches these — thermal state is observation, not
  provisioning.

## What it portends

- **This is the sensor the fan policy should key off of**
  ([fan.md](fan.md)) — and it is already there, no hardware work needed, just
  a policy (either a kernel thermal-zone trip point wired into the existing
  `pwmfan` cooling device, or a userspace daemon).
- The `npu` zone is the early-warning sensor for the
  [npu.md](npu.md) arc: if/when NPU inference lands, "is the NPU about to trip
  thermal" is answerable from here before any workload actually misbehaves.
- No action required for this row itself — it works — but it is the observable
  half of two open rows: fan policy (Partial) and the case/thermal case
  redesign (README Known Unresolved, tracked as a case-design effort, see
  [fan.md](fan.md)).