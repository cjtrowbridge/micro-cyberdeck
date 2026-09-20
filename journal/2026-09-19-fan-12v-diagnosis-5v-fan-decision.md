# 2026-09-19 — Fan flapping/heat: 12 V root cause found, 5 V fan ordered

## Symptom

The 40 mm Noctua fan (in the printed case, wired into the SBC's own fan
header) was turning on/off too frequently and the board got very hot. The
user found the original tiny onboard fan had been **removed and replaced**
by this one — so there is a single, kernel-driven fan, not the "two fans"
the older `fan.md`/README described.

## Diagnosis (evidenced, see `docs/hardware/fan.md`)

- Kernel driver `pwm-fan` owns the header: hwmon1/`pwm1` (0–255, no
  tach), DTB `/soc@3000000/pwm-fan`, `cooling-levels` =
  **`0, 5, 102, 170, 255`** (state 0 = hard off, state 1 ≈2% stall),
  registered as `cooling_device9` (max 4), driven by the kernel's
  internal thermal governor. **No user-space governor path exists on
  this kernel** — policy changes require a DTB overlay.
- Trips live in the DTB only; per-zone trip node names from the operator's
  `dtc` log (`docs/xfce-freeze-diagnosis.md` L124–139); most trips show no
  visible hysteresis.
- **Primary root cause (user's bench finding): the fan is a 12 V part on a
  5 V-class header rail** — "the fan wants 12 V." The PMIC has no 12 V
  rail. Hence weak thrust, stall below the start threshold, and governor
  flapping.
- Airflow itself is adequate: with the fan held at `pwm1=255` under load,
  hottest zone `cpub_idle` 64.7 °C (148.5 °F), `skin` 36.9 °C, no cpufreq
  crit throttle fired.
  (Post-fix spot check with the bench 12 V supply also hot: `cpul` 54.1 °C,
  `cpub` 50.8 °C, `skin` 34.4 °C — though that reading is at full-duty
  stopgap, not governor state.)

## Decision (user, 2026-09-19)

**Order a 5 V 40 mm 4-wire (PWM) fan** to replace the 12 V one — the native
fit for the SBC port (5 V VCC + GND + board PWM line via `cooling_device9`).
No 12 V feed, no HAT GPIO detour, no tach readback (not surfaced by the
driver anyway).

## Stopgap in place (temporary)

`fan-hold.service` (active, enabled): 0.25 s loop writing `255` to
`hwmon1/pwm1` while the 12 V fan is benched on a supply. **Retire once the
5 V fan is confirmed**: `sudo systemctl disable --now fan-hold.service &&
sudo rm /etc/systemd/system/fan-hold.service`, then watch the governor take
over (duty should walk 0…255 with load).

## Follow-ups (ordered)

1. **5 V fan arrives** → wire VCC/GND/PWM to the SBC header, watch the
   governor for an hour or two under real use (audible ramp + temp walk, no
   flapping).
2. **Manual duty-step check** (optional, pre- or post-swap):
   `echo 0/2/4 > /sys/devices/virtual/thermal/cooling_device9/cur_state` →
   expect `pwm1` 0/102/255 with audible off→spool→full.
3. **DTB overlay `fan-trips.dts`** (only if the governor still dips to
   0%/2% or sawtooths after the swap): floor the ladder (draft:
   `26, 77, 128, 190, 255`) + add ~5 °C hysteresis to the
   `cpub`/`cpul`/`cpul_idle`/`cpub_idle`/`gpu` `trip-point@0/@1` nodes;
   install via `dtc` → `/boot/overlay-user/` + `user_overlays` in
   `/boot/armbianEnv.txt`, and fold into `setup.sh`'s overlay set with
   verify rows + `docs/setup.md` update per the repo mandate.
4. Docs updated this session: `docs/hardware/fan.md` (rewritten — single
   12 V fan on the SBC port, ladder/governor evidence, 5 V decision),
   `README.md` Hardware Status fan row, `docs/hardware/soc-thermal.md`
   cooling-device bullet (was "0% with no policy" — now the 5-state
   kernel-governor ladder). The `docs/hardware/README.md` row stays
   **Partial** until the 5 V fan is verified on the board** (status moves
   with the swap, per the hardware-row mandate).