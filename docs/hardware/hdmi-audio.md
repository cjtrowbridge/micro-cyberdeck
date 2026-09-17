# HDMI audio card

> Status: **Unused (by design)** (mirrors the README table)

## What it is

The one ALSA card the board's kernel actually exposes: **`allwinnerhdmi`**
(Allwinner HDMI audio output) — meant for sound on an *external*
monitor plugged into the deck's HDMI output. Deck audio (the GamePi speaker
and headphone jack) is **not** this card — see
[speaker-hat-amp.md](speaker-hat-amp.md).

## What we know

- `cat /proc/asound/cards` shows **exactly one** card: `allwinnerhdmi`.
- `setup.sh` treat this as a **verified invariant, not a gap**: verify row
  **`audio-cards`** asserts there is exactly one ALSA card and that it is
  `allwinnerhdmi`, with the explicit note *"HAT audio is GPIO/PWM pin 12, not
  I2S"* — i.e. it is actively checking that the earlier, wrong
  `es8388`-I2S path (see [speaker-hat-amp.md](speaker-hat-amp.md)) has not
  crept back in as a second, bogus card.
- No service, daemon, or application on the deck targets this card for
  anything — it's there because it's the SoC's stock HDMI audio path,
  nothing more. If an external monitor with speakers is ever plugged in, this
  is the card that would feed it — no work needed to make that true.

## How we know

- Live probe (September 2026 session, no sudo):
  `cat /proc/asound/cards`, `aplay -l`.
- Setup contract: `setup.sh` verify rows `audio-cards` (+ its companion
  `stale-overlays` row, same topic) — see `docs/setup.md` for the full verify
  row spec.

## What it portends

- **No planned work** — this row exists to prevent a future reader (or agent)
  from mistaking the one real audio *capability* the kernel exposes for "the
  deck's audio," when in fact the deck's actual audio is the HAT
  GPIO/PWM+sigma-delta path, a completely separate mechanism.
- If any future feature wants to *also* drive an external monitor's speakers
  (e.g. mirroring TTS to both the HAT speaker and HDMI), this card is the
  zero-effort second destination — but nothing on the current roadmap
  asks for that.