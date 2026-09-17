# Fan

> Status: **Partial** (mirrors the README table)

## What it is

The deck's cooling: a tiny onboard fan driven by the kernel's `pwmfan`
driver, plus — for the printed case — a full-size **40 mm case fan** that is
currently just wired for constant-speed spin.

## What we know

- **The kernel part is already there and at 0%:** the `pwmfan` hwmon node
  (compatible `pwm-fan`) is probed and exposes `pwm1 = 0` — the fan driver is
  bound, the thermal framework even lists it as one of the board's cooling
  devices (see [soc-thermal.md](soc-thermal.md)), but **no thermal policy is
  driving it**, so it is effectively off. There is no `min`/`max`
  pulse-width mapping exposed that anyone has tuned.
- **The tiny onboard fan is underpowered for this board** — the README's
  standing known-unresolved note (better case/thermal) exists because the
  little fan "struggles to keep up by itself," and separately because we want
  the battery (see [battery.md](battery.md)) kept insulated from SBC heat.
- **The printed case runs a 40 mm fan at constant speed** — no PWM, no RPM
  feedback, no thermal trip — just "on."
- **Goal: real fan control, ideally PWM**, with this shape:
  - Drive the 40 mm fan's **PWM input** from a **free SBC GPIO** via the HAT
    header.
  - **Verify before wiring:** the GPIO is 3.3 V logic (40 mm fan PWM inputs
    are typically 5 V-tolerant but should be confirmed against the specific
    fan/part's datasheet), and the fan's **5 V power feed** is actually
    available on the header (the board's 5 V rail is one of the PMIC rails,
    see [pmic-axp8191.md](pmic-axp8191.md) — confirm which header pin carries
    the 5 V the fan wants, vs. 3.3 V).
  - Use the fan's **tach wire** for RPM feedback (another GPIO, count
    pulses).
  - Add a **thermal trip policy** — this is where [soc-thermal.md](soc-thermal.md)'s
    `cpub`/`skin` zones and the existing `pwmfan` cooling device become the
    trip source.
  - Two implementation shapes are on the table: **proper** — a
    hardware-PWM overlay reusing the existing `pwm-fan` thermal machinery (the
    kernel already models this fan, we'd be pointing it at the right
    hardware) — or **fast** — a userspace `gpiod` daemon generating the PWM
    and reading tach, with the trip thresholds in userspace.

## How we know

- `pwmfan` at 0%: live probe (September 2026 session, no sudo):
  `grep -l pwmfan /sys/class/hwmon/*/name` then `cat
  <that>/pwm1` (0). The "it's one of the cooling devices" fact comes from the
  same probe pass as [soc-thermal.md](soc-thermal.md).
- **40 mm fan wiring (PWM input voltage tolerance, 5 V feed, tach) is not yet
  verified against the specific fan part in the case** — this is explicitly
  the "verify" half of the goal and is not done; no datasheet check or
  multimeter confirmation is on file for those two voltage-level questions.

## What it portends

- **This is the concrete, software-and-light-hardware-fix half of the
  case/thermal known-unresolved item** in the README — the other half is the
  physical case redesign itself (tracked in the case-design repo, not here).
- **Gating on verification, not on driver work:** the kernel-side `pwm-fan`
  machinery already exists; the risk in this row is the **electrical**
  unknowns (3.3 V vs 5 V on the PWM input, where the 5 V feed actually lives
  on the header), which are a bench/multimeter step, not a code step.
- Unblocks a **thermal trip that protects the battery** from sustained SBC
  heat (see [battery.md](battery.md), README Known Unresolved) — right now
  there is no OS-visible "the deck is too hot, back off" reflex at all.
- Cheap intermediate win available without any of the above: since the
  `pwmfan` cooling device and the `cpub`/`skin` thermal zones both already
  exist, a **kernel-side trip point** (expose/configure the existing
  `pwm-fan` cooling curve, no 40 mm fan involvement) would get the *tiny*
  onboard fan actually spinning under load before any 40 mm fan wiring is
  done — a quick "does the existing half of this row even work under real
  load" test.