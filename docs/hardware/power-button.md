# Power button (PMIC key)

> Status: **Done** (mirrors the README table)

## What it is

The physical power button on the deck, wired not to a bare GPIO but to the
**PMIC's PEK (Power Enrichment Key / power-key) input** on the AXP8191 (see
[pmic-axp8191.md](pmic-axp8191.md)). Linux already knows about it as a
standard input device — no overlay or driver work was needed for this row.

## What we know

- It surfaces as `/dev/input/event0`, the **only** `/dev/input` device that
  exists on a fresh provision.
- Capability inspection of that node
  (`/sys/class/input/event0/device/capabilities/`,
  `/proc/bus/input/devices`, and the `EVIOCG*` ioctls read through a
  compiled C probe) shows:
  - `EV` mask = `100003` — the kernel prints the mask **hex, unpadded,
    big-word-first**, i.e. `0x100003` = bits `{0, 1, 20}` = `EV_SYN` (0) +
    `EV_KEY` (1) + `EV_REP` (20 — the kernel's key-repeat capability, a
    *named* one). I.e. a **pure key/button** device: press/release plus
    long-press **repeat**, and *nothing else* — no axes, no relative
    motion, no switches.
  - Exactly **one** `KEY_*` bit is advertised: **bit 116 = `KEY_POWER`**
    (from `capabilities/key` word[1] = `0x10000000000000`; on-disk
    `linux/input-event-codes.h` defines `KEY_POWER 116`). No other button
    shares this node today.
  - `EVIOCGID` is all zeros (bus/vendor/product/version = 0); the
    identifying strings are `NAME = axp8191-pek`, `PHYS = m1kbd/input2` —
    the PMIC driver's name for the key input.
  - In sysfs the input node is a **child of the PMIC's platform device**:
    `readlink -f /sys/class/input/event0` →
    `/sys/devices/platform/soc@3000000/7083000.twi/i2c-13/13-0036/axp2101-pek.0/input/input0/event0`
    — i.e. it hangs off the same axp8191 @ i2c-13/0x36 that
    [pmic-axp8191.md](pmic-axp8191.md) binds via `axp20x-i2c`
    (`axp2101-pek.0` is the generic `axp20x` driver device name).
  - `/dev/input/by-path/` lists exactly one entry:
    `platform-7083000.twi-platform-axp2101-pek.0-event` → `../event0`.
    `/dev/input/by-id/` is **empty** (nothing to key on when EVIOCGID is
    all zeros), so by-path is the stable reference to use.
- Because it's a kernel input event, any standard input listener
  (`grep`-able via `evtest`, or pollable from a userspace service) sees
  press/release as ordinary `key` events — no custom parsing.

## How we know

- Live probe (September 2026 session, no sudo): the capability files are
  world-readable — `cat /sys/class/input/event0/device/capabilities/ev` →
  `100003`, `cat .../capabilities/key` → `10000000000000 0` (decode:
  word[1] bit 52 = global key 116); `cat /proc/bus/input/devices` shows the
  single `axp8191-pek` block, `Handlers=kbd event0`, all ID fields 0;
  `readlink -f /sys/class/input/event0` gives the PMIC-child sysfs path
  above; `ls -l /dev/input/by-path/` shows the one
  `platform-…-axp2101-pek.0-event` link; `ls /dev/input/by-id/` →
  no such directory. The `EVIOCG*` check was done with a small C program
  compiled against the on-disk uapi headers (hand-computed `_IOC` constants
  in shell one-liners returned `EINVAL` here — always build the number from
  the real headers), opened `O_RDONLY|O_NONBLOCK`: `EVIOCGID` all zeros,
  `EVIOCGBIT(EV)` bits `{0,1,20}`, `EVIOCGBIT(KEY)` sole bit 116, NAME/PHYS
  as above. (The interactive `evtest` check is the operator's task; the
  agent must not `cat /dev/input/event0` directly — it blocks waiting for
  keypresses.)
- Nothing in `setup.sh` provisions, configures, or needs to verify this —
  it comes up as a side effect of the PMIC driver binding, so no verify row
  exists for it (nor needs one, as long as `audio-cards`/the PMIC rail
  checks in [pmic-axp8191.md](pmic-axp8191.md) keep passing).
- **Membrane keypad cross-cut (suspected, unconfirmed — see
  [membrane-buttons.md](membrane-buttons.md), "Parked keys"):** the keypad
  *pin 5* line (`gpiochip0` / line 34 / `PB2`) shows **no** GPIO transitions
  during the 2026-09-23 full mapping session (`/home/cj/btnmap-run.log`, a
  line the daemon deliberately does not poll either). That is consistent
  with the line not being an independent GPIO input but wired into (or
  shared with) this PEK input — the hypothesis this record owns, since the
  membrane record's key count is the one thing it changes. An observed
  event0 `KEY_POWER` during a long-press on pin 5 would confirm the fusion
  (and would also mean a 13th-button "select"-class key is already the deck
  power-key, which is fine for power-control purposes — the membrane
  record treats pin 5 as out-of-scope either way).

## What it portends

- **Already usable today** — e.g. a tiny service could watch this node and
  implement long-press-to-shutdown / short-press-to-
  suspend, which is the standard "power button does something sane" gap on a
  headless-ish device that otherwise has no other local power control (SSH
  is the only other way to power it down).
- No dependency on the [membrane-buttons.md](membrane-buttons.md) or
  [touch-gt9271.md](touch-gt9271.md) work — this one is simply already done
  at the driver level; if a behavior (long-press policy) is ever wanted,
  that's pure software on top of this existing event node, tracked under that
  feature's own plan, not under this hardware row.