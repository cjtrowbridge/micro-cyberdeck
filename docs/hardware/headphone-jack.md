# Headphone jack

> Status: **Partial** (mirrors the README table)

## What it is

The 3.5 mm jack on the HAT. It shares the **same amplifier output path** as
the small speaker — the amp's output feeds both destinations (the hardware
wiring details on the HAT: see the vendor's GamePi13 HAT wiki page linked in
the README, and `/sys/bus/i2c/devices/` for what the host side exposes).

## What we know

- Because the amp output is shared, the deck's entire audio path described in
  [speaker-hat-amp.md](speaker-hat-amp.md) (the `hat-sound` socket daemon,
  1-bit sigma-delta at 48 kHz) is **identically** the headphone path. Whatever
  is true of the speaker is true of the jack: today that means "it's the same
  1-bit sigma-delta engine output, wired to both destinations."
- **Never tested on its own.** Every audio validation run to date drove the
  small speaker; there's no evidence on file for whether the jack's level,
  impedance match, or the speaker-mute cross-talk (when the jack is plugged,
  does the amp mute the speaker? via a mechanical relay? via a detection
  pin?) behave as the HAT intends.
- There is **no ALSA/HDMI-style "jack detection" input device** from the
  host's perspective — the detection (if any) happens entirely on the HAT
  side of the amp, not something the SBC can read.

## How we know

- The amp-is-PWM-not-I2S conclusion and the shared-output wiring follow from
  the same physical constraint that produced
  [`journal/2026-09-15-hat-audio-v3.3-status.md`](../../journal/2026-09-15-hat-audio-v3.3-status.md)
  (a single amp, one control pin).
  There is no separate probe log or service for the jack itself — that's the
  gap this row documents.
- Re-probe for anything new: `alsamixer`/`aplay` against the current engine
  while a headphone is plugged in versus unplugged, and a voltage/current
  check with a multimeter if the HAT's schematic is available (it isn't
  captured in-repo).

## What it portends

- **Low-hanging test, no new code needed:** a one-time "play a tone with
  nothing plugged in vs. with headphones plugged in" check, plus a note on
  whether the speaker goes silent or ducks, is all that's needed to close
  this row to **Done** for *provisioning-correct* purposes (i.e. the
  `setup.sh` contract doesn't change — the engine doesn't care).
- If the HAT mutes the speaker via its own jack-detect and we later want the
  SBC to *know* a jack is in (e.g. to raise the TTS volume in response, or
  to route a "headphones connected" notification to the deck's UI), that
  would require finding a detect GPIO on the header and treating it like the
  (unbound) buttons — i.e. it joins the same
  [membrane-buttons.md](membrane-buttons.md) /
  [touch-gt9271.md](touch-gt9271.md) class of "need-the-schematic" work.
- This row is **not gating** the [audio-normal-device.md](audio-normal-device.md)
  arc: the ALSA plugin forwards to the same socket daemon regardless of what
  is plugged into the jack.