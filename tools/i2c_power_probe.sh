#!/usr/bin/env bash
#
# i2c_power_probe.sh — read-only power-path i2c census for this board.
#
# Part of the power-path arc (plan
# 2026-09-23-01-02-41_power-path-trace-measure-document.md, WS1).
#
# Safety frame (from the plan, DO NOT relax):
#   * PMIC register access here is READ-ONLY. Chip-ID family registers
#     0x00–0x06 on the AXP pair are the only registers touched, and only
#     via i2cget (read). Nothing in this script ever writes a register.
#   * The i2cdetect sweeps are quick-probe only and stop at recording
#     any new responding address — the script performs no further
#     transaction against a responder, and the operator must not.
#   * Run with the deck on its main power feed (HAT microUSB).
#
# Usage (one sudo, one password):
#   sudo bash tools/i2c_power_probe.sh 2>&1 | tee /tmp/ws1_i2c_probe_$(date +%F).log
#
# Section 1 (sysfs snapshot) works unprivileged. The i2cget/i2cdetect
# sections require root because /dev/i2c-* is root:i2c; they are skipped
# with a notice when the script is not run as root.

set -u

echo "=== i2c power probe: $(date '+%F %T') ==="
echo "host: $(hostname)  user: $(id -un)  uid: $(id -u)"

echo
echo "## Section 1 — power_supply / tcpm / fuel / hwmon snapshot (sysfs, unprivileged)"
echo "--- /sys/class/power_supply/ ---"
ls -1 /sys/class/power_supply/ 2>&1
PSY=/sys/class/power_supply/tcpm-source-psy-14-0022
if [[ -d $PSY ]]; then
  echo "--- $PSY : readable properties (files only) ---"
  while IFS= read -r f; do
    p=${f##*/}
    printf '%-14s = ' "$p"
    cat "$f" 2>&1 | tr -d '\n'
    echo
  done < <(find -L "$PSY" -maxdepth 1 -type f | sort)
  echo "--- $PSY/hwmon0 -> $(readlink -f "$PSY/hwmon0" 2>&1)"
  if [[ -e $PSY/hwmon0/name ]]; then
    echo "--- $PSY/hwmon0 : legacy hwmon-format properties (files only) ---"
    while IFS= read -r f; do
      p=${f##*/}
      printf '%-14s = ' "$p"
      cat "$f" 2>&1 | tr -d '\n'
      echo
    done < <(find -L "$PSY/hwmon0" -maxdepth 1 -type f | sort)
  fi
fi
echo "--- /sys/class/fuel/ ---"
ls -1 /sys/class/fuel/ 2>&1
echo "--- /sys/class/hwmon/ ---"
ls -l /sys/class/hwmon/ 2>&1

if [[ $(id -u) -ne 0 ]]; then
  echo
  echo "!! Sections 2–3 skipped: /dev/i2c-* is root:i2c; re-run under sudo."
  exit 0
fi

echo
echo "## Section 2 — chip-ID reads on i2c-13 (i2cget, READ-ONLY, regs 0x00–0x06)"
for a in 0x34 0x36; do
  echo "--- bus 13, addr $a ---"
  for r in 0 1 2 3 4 5 6; do
    printf '13 %s reg %02x = ' "$a" "$r"
    i2cget -y 13 "$a" "$r" 2>&1
  done
done

echo
echo "## Section 3 — detect-only sweeps (i2cdetect quick-probe; record new responders, do not probe further)"
echo "--- 3a. known kernel claimers (sysfs; reliable even if the grids mangle) ---"
for b in 9 11 12 13 14 15 20; do
  echo "i2c-$b:"
  found=0
  for d in /sys/bus/i2c/devices/*; do
    busid=${d##*/}
    [[ $busid =~ ^i2c- ]] && continue
    bnum=${busid%%-*}
    addr=${busid#*-}
    [[ "$bnum" == "$b" ]] || continue
    drv=$(readlink "$d/driver" 2>/dev/null)
    drv=${drv##*/}
    printf '  0x%s  %-28s %s\n' "$addr" "${drv:-UNBOUND}" "$(cat "$d/name" 2>/dev/null)"
    found=1
  done
  [[ $found == 0 ]] && echo "  (no bound devices)"
done

echo
echo "--- 3b. full sweep, ONE LABELED LINE PER RESPONDER (paste-proof) ---"
echo
echo
echo "    legend: -- absent (not printed)  U*/?? = kernel-claimed/unknown (not in this awk,"
echo "    i2cdetect marks them; they are bound to a driver, see 3a)  0xNN = raw byte"
echo "    read back from an unbound responder — the finding to note, then leave alone."
for b in 9 11 12 13 14 15 20; do
  i2cdetect -y "$b" 2>&1 | awk -v bus="$b" '
    /^[0-7][0-9a-f]:/ {
      row = substr($1, 1, 2)
      for (i = 2; i <= 17 && i <= NF; i++) {
        v = $i
        if (v == "--" || v == "??") continue      # absent / unknown-class
        if (v ~ /^U/) continue                    # kernel-claimed (see 3a)
        printf "RESPONDER bus %s addr 0x%s%x = %s\n", bus, row, i - 2, v
      }
    }'
done
echo "(no RESPONDER lines between 3a and the legend-above means: only kernel-claimed devices answered)"

echo
echo "=== probe complete: $(date '+%F %T') ==="