# Audio input (microphone)

> Status: **Partial** (mirrors the README table) — a USB mic (card 1) is
> connected and records non-silence; the voice agent round trip that would
> consume it is not yet built.

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
  ALSA card and no `i2c-0` adapter).
- **The HAT's last unexamined candidate IC is a power part, not an audio
  input stage.** The eight-pin IC beside the `4R7` inductor — previously the
  "mystery IC" the onboard-mic option kept open — read **`9813` / `2512`**
  (date code week 12 of 2025) under the 2026-09-24 macro set; its
  switching-charge/boost topology (buck-boost controller, `SY89813`-class,
  *marking-confirmed, datasheet-unconfirmed*) and its non-I²C visibility
  make it the HAT's power IC, with the `NS8002` amp confirmed as a
  *separate, adjacent* part — see
  [power-path.md](power-path.md) WS1.5. That was the last unidentified IC in
  the chain, so **no onboard input stage is demonstrated anywhere**.
- **The 3.5 mm jack** ("headphone jack" / the README's "aux cord plug") shares
  the amp's output path ([headphone-jack.md](headphone-jack.md)) and has never
  been tested as an input. Even if it carries a mic conductor, there is
  **no demonstrated internal path** from it into Linux today.
- **The reference clone has no microphone.** The Spotpear GM154 1.54″ Game
  LCD (the board this HAT clones) ships **no audio at all** — its own
  audio-configuration tutorial requires purchasing a separate external GPIO
  audio module. This HAT clone adds the speaker/amp (output) and the jack;
  nothing documents a mic.
- **USB audio via OTG now exists and works (Option A landed, 2026-10-16).**
  The operator connected a USB-C PnP sound device — `lsusb` shows
  **`08bb:2902 Texas Instruments PCM2902 Audio Codec`** on bus 001; ALSA
  enumerates it as **card 1** (`USB PnP Sound Device`, subdevice 0/0, capture
  present). A 2 s probe at 16 kHz / S16 / mono (`arecord -D plughw:1,0`)
  returned 32 000 frames at **rms ≈ 1 527, peak ≈ 5 735** — real signal, not
  the bit-exact zero the card-0 HDMI capture produced. This is the
  deck's first working input path; the voice agent's push-to-talk has a
  recordable source. (The `setup.sh` `audio-cards` verify row asserts exactly
  one card and predates this — re-check it when the next `setup.sh` pass
  runs.)

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

- Re-probe (bare, no sudo), 2026-10-16 15:35 local — Option A landed:

  ```bash
  lsusb                       # 08bb:2902 Texas Instruments PCM2902 Audio Codec
  cat /proc/asound/cards      # card 1: USB PnP Sound Device
  arecord -l                  # card 1, device 0, subdevices 1/1
  arecord -D plughw:1,0 -f S16_LE -r 16000 -c 1 -t wav -d 2 /tmp/probe16k.wav
  python3 -c '…wave peak/rms…'  # → frames=32000 rms=1527 peak=5735 (signal)
  ```

- Journals: [`2026-09-23-voice-agent-g0-mic-probes.md`](2026-09-23-voice-agent-g0-mic-probes.md)
  (the full probe transcript and the option decision).
- Cross-references: [speaker-hat-amp.md](speaker-hat-amp.md) (output-only
  chain, amp identity), [hdmi-audio.md](hdmi-audio.md) (card identity),
  [headphone-jack.md](headphone-jack.md) (jack shares the amp path),
  `setup.sh` verify rows `audio-cards` (exactly one card) and
  `stale-overlays` (no `es8388`/`i2c0` dtbos, no `i2c-0` adapter).

## What it portends

- **Option A (USB mic via OTG) is now proven** — card 1 records real signal
  (2026-10-16 probe above), so the voice agent's push-to-talk has a
  recordable source. The G0 blocker in
  [`plans/current/2026-09-23-22-57-17_voice-agent-gates.md`](../../plans/current/2026-09-23-22-57-17_voice-agent-gates.md)
  clears.
- **Still open:** the ASR engine choice for the required real-time interim
  transcription (the 2026-10-16 design conversation — Whisper has no
  partials endpoint; candidates are streaming engines such as Vosk /
  Paraformer) and the round trip itself (record → ASR → inject into the
  focused field), which is the G2 gate.
- **Options B/C (onboard input / 3.5 mm jack)** remain dead unless later
  traced; Option A supersedes them for now.
- **Housekeeping (verified 2026-10-16):** `setup.sh`'s `audio-cards` verify row
  (line 1588) hard-requires `ncards == 1` and card `allwinnerhdmi` — the
  attached USB mic (card 1) makes that row FAIL. It must be revised to
  tolerate the known-good USB capture card before the next `setup.sh` pass
  ([docs/setup.md](../setup.md) mandate).
- Status flips to **Done** when a round trip uses the mic (voice agent G2).