# RTC (hym8563)

> Status: **Not started** (mirrors the README table)

## What it is

A hardware real-time clock (Hymitsu `hym8563`) on the board's I2C bus —
the standard "keeps time while the deck is off" chip for a small
battery-powered SBC.

## What we know

- **Present on the I2C bus, unbound:** the i2c device **`15-0051`
  (adapter `i2c-15`, address **0x51**) is enumerated with `name = hym8563`
  and **no `driver` symlink** — nothing bound it at boot (no `/dev/rtc*`
  devices beyond the kernel's virtual `rtc0`, and no `/sys/class/rtc/rtcN`
  beyond `rtc0` either; the kernel's default hardware-clock fallback, if
  any, is what's running today, not this chip).
- **NTP is standing in for it** — the deck gets its wall clock from the
  network today; there is no "deck is off, clock keeps walking" behavior.
- The `hym8563` driver exists upstream in the kernel, so this is not an
  "impossible-on-this-kernel" row — it's an unbound, not unsupported, row.

## How we know

- Live probes (September 2026 sessions, no sudo): `ls /sys/bus/i2c/devices/`
  → `15-0051`; `cat /sys/bus/i2c/devices/15-0051/name` → `hym8563`; no
  `driver` symlink on it; `ls /sys/class/rtc/` (only `rtc0`); no
  `/dev/rtc1`.

## What it portends

- **Becomes relevant the moment the deck starts being used as a
  battery-powered, network-detachable device** (i.e. once any of
  [battery.md](battery.md) / [power-path.md](power-path.md) lands and the
  deck is expected to keep a correct time while off or unplugged). For a
  purely plugged-in / always-SSH device, NTP is fine and this row is
  legitimately low-priority.
- Binding it is the same class of work as
  [membrane-buttons.md](membrane-buttons.md) /
  [touch-gt9271.md](touch-gt9271.md) — an unbound-but-supported driver
  waiting on an overlay/enablement step in `setup.sh` — and once bound it
  needs zero application-level work (the kernel's `clock`/`hwclock` story
  picks it up automatically as a genuine `rtc1`).