# Audio input (microphone)

> Status: **Not started** (mirrors the README table)

## What it is

The deck's voice *input* — the source the push-to-talk voice agent
([`projects/voice-agent/`](../../projects/voice-agent/)) records from when the
left bumper is held. Distinct from the output chain, which is settled
([speaker-hat-amp.md](speaker-hat-amp.md)): this record is about where
**captured** audio would come from.

## What we know

- **A capture subdevice exists, and it is a structural dead-end.**
  `/proc/asound/pcm` reports card 0 (`allwinnerhdmi`) as
  **`playback 1 : capture 1`**; `arecord -l` lists it (`pcmC0D0c`,
  subdevice 1/1); `arecord` is installed. But a 5 s capture at 48 kHz mono
  (2026-09-23) returned **240 000 frames at peak = 0, rms = 0 — 100 % zero
  samples, bit-exact silence**. Every mixer control on the card is an
  **HDMI bitstream-format** control (AC3 / DTS / DOLBY / "loopback debug" /
  …) — the capture direction is the SoC HDMI audio codec's **Rx** path (audio
  arriving on an HDMI *input*, which this SBC does not use or route). There is
  no analog mic stage behind it and nothing to gain.
- **The HAT's audio chain is output-only.** The `NS8002` Class-AB amp is
  driven by a 1-bit sigma-delta bitstream on GPIO PB5 (line 37) from the
  real-time `hat-sound` engine — no ADC anywhere in the chain
  ([speaker-hat-amp.md](speaker-hat-amp.md)).
- **The ES8388 I²S path was probed and bound nothing** — the HAT codec is
  *not* an I²S codec (that was a playback-side diagnosis; kept ruled out by
  `setup.sh`'s `stale-overlays` verify row, which also asserts exactly one
  ALSA card and no `i2c-0` adapter). Whether **any** component on the HAT has
  an input stage is unverified; the eight-pin mystery IC in the HAT's audio
  section (beside the `4R7` inductor, marked in the underside photos) is still
  unidentified pending the power-path plan's WS1 chip-ID step
  ([power-path.md](power-path.md)).
- **The 3.5 mm jack** ("headphone jack" / the README's "aux cord plug") shares
  the amp's output path ([headphone-jack.md](headphone-jack.md)) and has never
  been tested as an input. Even if it carries a mic conductor, there is
  **no demonstrated internal path** from it into Linux today.
- **The reference clone has no microphone.** The Spotpear GM154 1.54″ Game
  LCD (the board this HAT clones) ships **no audio at all** — its own
  audio-configuration tutorial requires purchasing a separate external GPIO
  audio module. This HAT clone adds the speaker/amp (output) and the jack;
  nothing documents a mic.
- **No USB audio is attached** (probe-time `lsusb`: root hubs only). The SBC's
  OTG port is the one clean input route that needs no HAT surgery: any USB
  mic (or headset) becomes a normal second ALSA card.

## How we know

- Re-probe (all bare, no sudo), 2026-09-23 22:56 local:

  ```bash
  cat /proc/asound/pcm        # 00-00 …: playback 1 : capture 1
  arecord -l                  # card 0 capture, subdevices 1/1
  lsusb                       # root hubs only — no USB audio device
  arecord -D plughw:0,0 -f S16_LE -r 48000 -d 5 /tmp/captest.wav
  python3 -c '…wave peak/rms…'  # → peak=0 rms=0 zero_samples=240000/240000
  amixer -c 0 contents | head -30   # HDMI-format controls only
  ```

- Journals: [`2026-09-23-voice-agent-g0-mic-probes.md`](2026-09-23-voice-agent-g0-mic-probes.md)
  (the full probe transcript and the option decision).
- Cross-references: [speaker-hat-amp.md](speaker-hat-amp.md) (output-only
  chain, amp identity), [hdmi-audio.md](hdmi-audio.md) (card identity),
  [headphone-jack.md](headphone-jack.md) (jack shares the amp path),
  `setup.sh` verify rows `audio-cards` (exactly one card) and
  `stale-overlays` (no `es8388`/`i2c0` dtbos, no `i2c-0` adapter).

## What it portends

- **There is no working input path on the deck today.** The voice agent's
  push-to-talk has nothing to record until one of the options below lands —
  this is the G0 blocker in
  [`plans/current/2026-09-23-22-57-17_voice-agent-gates.md`](../../plans/current/2026-09-23-22-57-17_voice-agent-gates.md).
- **Options (design
  [`projects/voice-agent/docs/design.md`](../../projects/voice-agent/docs/design.md)
  §4):**
  - **A — USB mic via OTG (the declared G0 path):** zero kernel work; the new
    card appears as `hw:1` (expected) with a capture subdevice; the sidecar's
    `ALSA_INPUT` takes it. Cost: one part, and a visible dongle on the deck
    while it's plugged
  - **B — onboard input, if the HAT actually has one (unproven):** needs the
    physical investigation (G0-c): open the case, trace the jack's mic
    conductor, identify the eight-pin IC (shareable with power-path WS1).
    Even a winning find requires a DT overlay and possibly a codec driver
    module (`CONFIG_SND_SOC_ES8388` absent *to-probe*) before Linux sees it
  - **C — the 3.5 mm jack as TRRS mic input:** dead unless B shows the wiring;
    the amp is output-only, so there is no input stage behind it today
- **Disk interaction:** the mic itself costs nothing on disk (USB), but G1's
  weights + toolchain need the qwen3.5:9b prune decision (operator) — see the
  gate plan, G1-a.
- Status flips to **Partial** when a capture device records non-silence (A or
  B evidence), and **Done** when a round trip uses it (voice agent G2).