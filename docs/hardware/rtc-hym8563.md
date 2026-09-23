# RTC (hym8563)

> Status: **Not started** (mirrors the README table)

## What it is

A hardware real-time clock (Hymitsu `hym8563`) on the board's I2C bus —
the standard "keeps time while the deck is off" chip for a small
battery-powered SBC.

## What we know

- **Present on the I2C bus, unbound:** the i2c device **`15-0051`
  (adapter `i2c-15`, address **0x51**) is enumerated with `name = hym8563`
  and **no `driver` symlink** — nothing bound it at boot (re-probed
  2026-09-23, WS1: **no entry in `/sys/class/rtc/` at all and no
  `/dev/rtc*` node** — not even a kernel fallback RTC is materialized, so
  the deck's wall clock today is purely software, fed from NTP; an earlier
  session note of a "virtual `rtc0`" is superseded by this re-probe).
- **NTP is standing in for it** — the deck gets its wall clock from the
  network today; there is no "deck is off, clock keeps walking" behavior.
- The `hym8563` support exists **in this kernel**, so this is not an
  "impossible-on-this-kernel" row — it's an unbound, not unsupported, row.
  **Mechanism narrowing (2026-09-23):** `CONFIG_RTC_DRV_HYM8563=y`
  (built-in — no module, hence no `/sys/module/rtc_hym8563`), the
  `rtc-hym8563` driver **is registered** under `/sys/bus/i2c/drivers/`, the
  DT node (`rtc@51` on `twi@7085000`) is `status=okay` and carries
  `clock-output-names`, and its compatible is **`haoyu,hym8563`** — a
  vendor/Armbian compatibility string (the mainline binding uses
  `hymitsu,hy8563`; the Armbian tree documents `haoyu,hym8563`), which the
  vendor kernel's built-in driver evidently accepts. So: driver present +
  node okay → the non-bind is a **probe-time issue, not a
  missing-driver or disabled-node issue**; the operator's `dmesg`
  (root-only) is the next evidence.

## How we know

- Live probes (September 2026 sessions, no sudo): `ls /sys/bus/i2c/devices/`
  → `15-0051`; `cat /sys/bus/i2c/devices/15-0051/name` → `hym8563`; no
  `driver` symlink on it.
- **WS1 re-probe (2026-09-23, no sudo):**
  - `ls -l /sys/bus/i2c/devices/15-0051/` / `readlink …/15-0051/driver` →
    still **no driver symlink** (the UNBOUND flag in the power-path detect
    sweep's per-bus claimer list is a real missing link);
  - `ls /sys/class/rtc/` → **empty** (no `rtc0` at all); `ls /dev/rtc*` →
    no matching files — no RTC device of any kind is exposed today;
  - `/boot/config-$(uname -r)` → `CONFIG_RTC_DRV_HYM8563=y` (built-in; no
    module, so no `/sys/module/rtc_hym8563` directory — expected for a
    built-in driver);
  - `/sys/bus/i2c/drivers/` → **`rtc-hym8563`** is registered as a driver
    (i.e. the built-in's `i2c_driver` is live and probing);
  - `/sys/firmware/devicetree/base/rtc@51/` → `status=okay`,
    `clock-output-names` present, `compatible = haoyu,hym8563` (the
    vendor/Armbian string — mainline's binding file documents
    `hymitsu,hy8563`; `haoyu,hym8563` appears in the Armbian/repo tree,
    including a linux-rockchip binding doc);
  - net: driver present + node okay + compatible matched by this vendor
    kernel → the non-bind is a **probe-time issue**; next evidence is the
    operator-level boot log (`dmesg`, root-only on this board). Journal:
    [2026-09-23-power-path-ws1-ic-identification.md](../../journal/2026-09-23-power-path-ws1-ic-identification.md).

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