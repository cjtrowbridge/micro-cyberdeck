# 2026-09-23 — voice-agent G0: mic probes (decision: USB mic via OTG; hardware proof pending)

Plan: [plans/current/2026-09-23-22-57-17_voice-agent-gates.md](../plans/current/2026-09-23-22-57-17_voice-agent-gates.md)
(design: [projects/voice-agent/docs/design.md](../projects/voice-agent/docs/design.md) §4/§8)
User request: "ok go ahead with the plan" (execute the voice-agent gates).

## Probes (2026-09-23 22:56 local, all no-sudo)

| Probe | Result |
|---|---|
| `cat /proc/asound/pcm` | `00-00: sunxi-snd-plat-i2s-sunxi-snd-codec-hdmi … : playback 1 : capture 1` — card 0 **has** a capture subdevice |
| `arecord -l` | lists card 0, subdevices 1/1; `arecord` installed at `/usr/bin/arecord` |
| `arecord -D plughw:0,0 -f S16_LE -r 48000 -d 5 /tmp/captest.wav` + wave analyze | 240 000 frames, **peak = 0, rms = 0, zero_samples = 240000/240000 (100.0 %)** — bit-exact silence |
| `amixer -c 0 contents` | all controls are HDMI bitstream-format (AC3/MP3/AAC/DTS/DOLBY…/`loopback debug`) — the "capture" is the HDMI-Rx direction of the SoC codec, not an analog input stage |
| `lsusb` | root hubs only — **no USB audio device attached** |
| Reference-clone wiki (Spotpear GM154 1.54″) | base HAT has **no audio at all**; audio tutorials require an external GPIO audio module purchase; no mic documented for the board this HAT clones |
| Context | HAT amp is output-only (NS8002 on GPIO/PWM bitstream; no ADC in chain — speaker-hat-amp.md); ES8388 I²S overlay previously probed, bound nothing; 8-pin mystery IC in the HAT audio section still unidentified (power-path WS1) |
| Board state | `free -h`: 11 Gi total, 8.1 Gi free / 9.4 Gi available, swap 5.8 Gi unused; `df /`: 29 G, 2.5 G free (92 %) |

## Findings

1. **The one capture subdevice is a dead end, structurally** — it is the
   HDMI-Rx codec direction (bitstream formats as mixer controls, bit-exact
   silence, no source on this SBC). `arecord` *can* open it; nothing *feeds*
   it.
2. **No onboard input stage is demonstrated.** The HAT audio chain is
   output-only; the I²S codec path was already ruled out; the one
   unidentified IC that *could* be an input stage sits in the audio section
   and is shared with the power-path plan's chip-ID work.
3. **No USB mic is present** — the OTG route (design §4 option A) is the
   only path that needs no HAT surgery, and it needs a part.

## Decision (G0)

- **Path forward = option A, USB mic via OTG.** The capture contract is
  hardware-agnostic (16 kHz mono s16 ring buffer; the sidecar takes an
  `ALSA_INPUT` string), so a USB mic slots in with zero other design changes.
  Expected probe on arrival: a new card appearing in `arecord -l`
  (plausibly `hw:1,0`), non-zero peak/rms from a 5 s capture — agent re-probes
  on the operator's word; the winning device string gets journaled and carried
  into G2's chain defaults.
- **G0-c (onboard mic) stays parked** as an operator-physical step (case open,
  jack-conductor trace, the eight-pin IC ID — shareable with power-path WS1).
  Only worth running if the operator prefers onboard over USB once a mic is in
  hand to compare against.
- **G0 cannot close** on `[x]` without the hardware proof: `ALSA_INPUT` +
  non-silence numbers in a journal (the plan's G0 exit criterion).

## Next

Operator checkpoint (raised in-session):
1. **Mic:** obtain + attach a USB mic (or request the HAT physical
   investigation instead) — expected ALSA card shape to watch for
2. **Disk (G1 gate):** root is 2.5 G free (92 %); G1 wants ~2–2.5 G for
   weights + build + apt batch. Prune qwen3.5:9b (6.59 G, Ollama container
   volume) — operator call, it is not agent-executable

With those two answered, G1 executes: provisioning batch → whisper.cpp vendor
+ medium weights + user unit → Ollama `keep_alive: -1` pin → `free -h`
residency numbers → G2 chain → G3 app.

**Same-change updates (standing rule):** new record
`docs/hardware/audio-input-mic.md` (the input part had no record — created per
"the part with no record gets one… in the same change"), README Hardware
Status row added, `docs/hardware/README.md` index row added.