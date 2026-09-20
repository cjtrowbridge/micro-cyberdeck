# Fan

> Status: **Partial** (mirrors the README table)

## What it is

The deck's cooling fan: a **40 mm Noctua** in the printed case, wired into
the SBC's **fan header**. The original tiny onboard fan was **removed** and
replaced by this one — so there is exactly **one** fan, on the SBC port,
driven by the kernel. (The earlier "two fans" reading in this doc and the
README row was wrong.)

## What we know

- **Kernel-driven, already wired.** The `pwm-fan` driver
  (compatible `pwm-fan`) is bound and owns the header:
  - hwmon: `/sys/class/hwmon/hwmon1` → name `pwmfan`, `pwm1` writable
    0–255. **No `fan1`** — the tachometer wire is not surfaced (no RPM
    feedback path today).
  - DTB node: `/sys/firmware/devicetree/base/soc@3000000/pwm-fan`.
  - `cooling-levels` = **`0, 5, 102, 170, 255`** (5 u32, big-endian,
    confirmed by `od` + byte-swap) → fan states 0..4.
    **State 0 is a physical hard-off** (the driver disables the PWM output
    — `pwm_fan_power_off`); **state 1 is ≈2% duty** (`5/255`) — below the
    start threshold of any real fan, so state 1 is effectively a stall.
  - It is registered as **`cooling_device9`**
    (`/sys/devices/virtual/thermal/cooling_device9`, `cur_state` 0..4,
    max 4) alongside the cpufreq/ddr/`idle`/npu-devfreq cooling devices —
    the kernel's **internal thermal governor** walks it up/down as the
    thermal zones cross their DTB trips. There is **no user-space governor
    path on this kernel** (no `/sys/class/thermal/.../governor`, no
    `/sys/kernel/thermal/governors`) — so trip/duty policy is changeable
    **only via a DTB overlay**, not from userspace (see
    [soc-thermal.md](soc-thermal.md)).
- **The thermal zones feeding it** (all `mode=enabled`; trips live in the
  DTB, not sysfs): `cpub`, `cpul`, `cpul_idle`, `cpub_idle`, `gpu` (each
  with `trip-point@0/1` + `*_crit@0`), and `npu`, `ddr`, `skin`
  (crit-only). Evidence for the trip node names comes from the operator's
  `dtc` dump log in `docs/xfce-freeze-diagnosis.md` (L124–139). Most trips show
  **no visible hysteresis** in that dump.
- **The fan as installed is the wrong voltage.** It is a **12 V** part,
  but the SBC fan header is a **5 V-class rail** (the PMIC has no 12 V
  output — see [pmic-axp8191.md](pmic-axp8191.md); the header VCC is a
  bucked-from-input 5 V rail, exact rail unconfirmed). Running a 12 V fan
  on ~5 V gives weak/very slow thrust and it **cannot start below the duty
  level where `5 V × duty` reaches its 12 V start threshold** — so at low
  states it stalls, and the governor flaps between a spinning fan (state 4)
  and an almost-stopped one. **This was the user's bench finding
  (2026-09-19): "the fan wants 12 V."**
- **Airflow is adequate when the fan actually runs.** With the fan held at
  full duty (`pwm1=255`) under an active load (2026-09-19), the SoC zones
  stayed cool: `cpub` 64.4 °C / 148 °F, `cpul` 63.5, `cpub_idle` 64.7,
  `cpul_idle` 64.0, `gpu` 61.8, `npu` 59.5, `ddr` 57.9, `skin` 36.9 °C —
  and none of the SoC's cpufreq `crit` backstops fired. So the case/header
  airflow is not the problem; **the power + duty-curve shape is.**

## The fix (decided 2026-09-19)

Order a **5 V 40 mm 4-wire (PWM) fan** to replace the 12 V one — the native
fit for this port:

- **VCC → 5 V** (the header's own rail), **GND**, and **PWM ← the board's
  PWM line** (the kernel drives it via `cooling_device9` → `pwm1`). No
  external 12 V feed, no HAT-header GPIO, no tach readback needed.
- The 4-wire PWM control input is 5 V-tolerant, and the board's PWM output
  is the driver's normal output — so the fit is electrically standard.
  (Tach: not surfaced by the driver, so RPM feedback stays a "would be
  nice" and is out of scope for this fix.)

**Open item even after the 5 V fan arrives:** the DTB ladder still has a
**hard-off state 0 (0%)** and a **stall state 1 (≈2%)**, and the trips have
**little/no hysteresis** — so the governor can still dip the fan to 0%/2%
when close to a trip and sawtooth. The clean fix is a **DTB overlay**
(`fan-trips.dts`, installed into `setup.sh`'s overlay set) that:
  - replaces `cooling-levels` with a ladder that has a **non-zero floor**
    (e.g. `26, 77, 128, 190, 255` — a ~10% minimum, no hard-off, no 2%
    stall), and
  - adds **hysteresis** to the `cpub`/`cpul`/`cpul_idle`/`cpub_idle`/`gpu`
    `trip-point@0/@1` nodes.
This is a **follow-up, not part of the power swap** — do it after the 5 V
fan is in, the fan is confirmed starting cleanly, and a manual duty-step
check (`echo 0/2/4 > …/cooling_device9/cur_state`, listening + `pwm1`
readback, expecting `0/102/255` under the current ladder) has confirmed
what we're actually fixing.

## Current on-board stopgap (temporary, retire once the 5 V fan is in)

Because the 12 V fan is currently the only cooling and it can't be trusted
at low duty, the board is running it **held at full duty** by an operator
unit — `/etc/systemd/system/fan-hold.service` (active), whose loop writes
`255` to `/sys/class/hwmon/hwmon1/pwm1` every 0.25 s (which also pins the
governor's `cooling_device9/cur_state` to 4). **This is not the end state.**
Once the 5 V fan is confirmed, retire the stopgap:
`sudo systemctl disable --now fan-hold.service && sudo rm
/etc/systemd/system/fan-hold.service`, then watch the kernel governor take
over (duty should walk 0…255 with load).

## How we know

- **Fan on the SBC port + single-fan layout:** user statement (2026-09-19)
  — the original tiny fan is removed and the 40 mm Noctua is wired into the
  board's own fan header; so there is one kernel-driven fan, not two.
- **Kernel fan machinery (live probes, 2026-09-19, no sudo):** `cat
  /sys/class/hwmon/hwmon1/name` → `pwmfan`, `cat …/pwm1` → 0–255
  (255 under the stopgap), no `fan1` file; `od -t u4` (big-endian,
  byte-swapped) on
  `/sys/firmware/devicetree/base/soc@3000000/pwm-fan/cooling-levels` →
  `0, 5, 102, 170, 255`; `/sys/devices/virtual/thermal/cooling_device9/`
  → `pwm-fan`, `cur_state` 0..4 / max 4. No `governor` file exists under
  any `thermal_zone*`, so the governor path is the kernel-internal one.
- **Driver behavior (state 0 = off, state 1 = 2%):** Linux v6.6
  `drivers/hwmon/pwm-fan.c` — `pwm_fan_set_cur_state` maps a state to
  `cooling_levels[state]` → `pwm1`, and state 0 calls `pwm_fan_power_off`
  (PWM output disabled).
- **Zone temperatures with the fan held at full duty under active load
  (2026-09-19):** as tabulated in "What we know" — hottest `cpub_idle`
  64.7 °C, `skin` 36.9 °C, no cpufreq crit throttle fired.
- **Trip node names / no visible hysteresis:** operator's `dtc` dump log in
  `docs/xfce-freeze-diagnosis.md` (L124–139) — an operator-level citation (root
  dump), not a new probe.
- **The 12 V requirement:** the user's bench A/B — the fan does not behave
  on the port's 5 V-class supply ("the fan wants 12 V," 2026-09-19). The
  PMIC has no 12 V rail [pmic-axp8191.md](pmic-axp8191.md).

## What it portends

- **The concrete half of the case/thermal known-unresolved item** in the
  README reduces to: (1) swap to the 5 V fan (done once it arrives), then
  (2) optionally ship the DTB overlay above to fix ladder shape +
  hysteresis. The physical case redesign itself is tracked in the
  case-design repo, not here.
- **Unblocks a thermal trip that protects the battery** from sustained SBC
  heat ([battery.md](battery.md)): once the fan runs reliably, the existing
  `cooling_device9` / thermal-zone trips become a working "too hot → ramp
  the fan" reflex, keeping the SBC (and the battery next to it) cooler
  under NPU/GPU load.
- **No new driver work** is needed — the `pwm-fan` machinery already runs
  on this header. The remaining risk is purely the fan's electrical fit
  (resolved by the 5 V part) and the DTB duty-curve shape (the optional
  overlay).