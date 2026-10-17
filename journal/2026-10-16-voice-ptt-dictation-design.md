# 2026-10-16 — voice PTT dictation: TL verification + streaming-engine design conversation

Design checkpoint for the **push-to-talk dictation** feature: hold the left
trigger, speak, and the transcript lands in whatever text field is focused —
like a keyboard. Triggered by the operator's goal of a real-time dictating
button. Two threads were pursued: (1) verifying the hardware input, (2) the
ASR engine question.

## 1. Left trigger (TL) hardware capture — verified ✅

The left shoulder/trigger is `Control_L` in the daemon's table
(`/usr/local/bin/hat-buttons.py` on the board, `KEYS`:
`("/dev/gpiochip1", 2, "PL2", idle=1, active=0)` — i.e. **gpiochip1
line 2, PL2, pin 16, active-low** (idle HIGH).

Live probe on the board, 2026-10-16 ~14:04: a 5 ms-poll gpiod monitor
(`/tmp/tl_monitor.py`, `request_lines` INPUT on line 2, run under `sudo` —
the gpiochip nodes are `root:0600`) read:

```
T=  0.0s  PL2 (line 2)  value=1  (baseline)
T=  3.9s  PL2 (line 2)  1 -> 0  <== EDGE
T=  4.2s  PL2 (line 2)  0 -> 1  <== EDGE
```

- One clean press/release pair, ~230 ms press, **no bounce, no double edges** —
  the daemon's 30 ms debounce is comfortably above this.
- Polarity and line mapping in the daemon table are correct as written.
- Note: `gamepi-buttons.service` was **inactive/disabled** during the probe
  (operator-disabled since the 09-29 HAT-swap era — see
  [2026-09-29-voice-agent-g1-status-checkpoint.md](2026-09-29-voice-agent-g1-status-checkpoint.md));
  so only the hardware→GPIO half was proven, not the X11 key injection half.
- gpiod Python here is **2.2.0** — `Chip.get_line()` does not exist in this
  API; use `request_lines(config={line: LineSettings(direction=INPUT)})`
  (the same pattern the daemon's selftest uses). The first monitor draft
  failed on exactly this.

## 2. The operator's goal and the OpenWhispr proposal

Goal (operator, verbatim intent): left trigger as a **push-to-talk keyboard** —
types into the focused field like a keyboard. **Hard requirement: real-time
streaming — "doesn't wait until you're done to start transcribing."**
Operator chose, from the options offered, **interim text while still speaking**
(words appear live during the hold, finalized on release) — the demanding case,
not merely "fast decode after release."

Operator's proposed tool: **[OpenWhispr](https://github.com/OpenWhispr/openwhispr)**
(open-source WisprFlow/Granola alternative; README read 2026-10-16).

**Assessment: OpenWhispr is not a fit.** It is a **desktop GUI app** (Tauri;
macOS/Win/Linux AppImage/.deb) whose dictation path is the consumer
"press → speak → release → paste at cursor" shape. It is not an embeddable
streaming engine, its interim-partial behavior for dictation is not
demonstrated, and this deck is a **headless Xvfb :1** (960×960 scaled to the
240×240 ST7789 via `xvfb-to-st7789.py`) — there is no normal desktop for a
Tauri/Electron-class app to live in. We already own the X key-injection
(`Xlib`/XTest in `hat-buttons.py`), the mic path (now), and a resident-ASR
design. OpenWhispr would be more complexity for less control of the one
property that matters (latency/partial behavior).

## 3. Why the operator's hard requirement rules out plain Whisper

The pinned stack ([`projects/voice-agent/`](../projects/voice-agent/)) is
**whisper.cpp v1.9.4** + **whisper-medium** (`~/voice-agent/ggml-medium.bin`,
1.48 GB; `whisper-server` binary built, **not yet installed as a service** —
the deferred operator step from G1-c). The design
([`projects/voice-agent/docs/design.md`](../projects/voice-agent/docs/design.md))
already flags this: *"whisper.cpp's server has no first-class partial
endpoint; this is v1.1."* Whisper-class models decode **completed** segments;
they do not emit word-by-word partials while audio is still arriving. True
interim streaming therefore requires a **streaming ASR engine** (or a
stream-small + Whisper-refine hybrid), e.g.:

- **Vosk** — first-class real-time partials, small models (~40 MB class),
  proven on ARM; Python bindings.
- **Paraformer / SenseVoice (FunASR)** — near-real-time partials, stronger
  multilingual, heavier.
- **Whisper + VAD chunking** — only "chunked" interim (1–2 s granularity),
  not word-level; closest to the existing pin but weakest for the stated
  requirement.

Board headroom for this: 8×A76, 11 Gi RAM with ~9 Gi available at probe time —
enough for a streaming recognizer plus, if wanted, a Whisper medium refinement
pass, but latency must be measured (G2-class) before committing.

## 4. Mic — Option A landed ✅ (hardware records updated in this change)

- `lsusb`: **`08bb:2902 Texas Instruments PCM2902 Audio Codec`** (USB-C PnP
  sound device, bus 001).
- ALSA **card 1** `USB PnP Sound Device`, capture subdevice 0/0.
- Probe, 2026-10-16 15:35: `arecord -D plughw:1,0 -f S16_LE -r 16000 -c 1 -d 2`
  → **32 000 frames, rms ≈ 1 527, peak ≈ 5 735** — real signal, not the
  bit-exact zero of card 0's HDMI capture path (the 09-23 G0 finding).
- Records updated: [docs/hardware/audio-input-mic.md](../docs/hardware/audio-input-mic.md)
  (status **Not started → Partial**, probe evidence appended), the
  hardware README table row, and the main README Hardware Status table
  (new Microphone row).
- **Conflict discovered (verified, setup.sh:1588):** the `audio-cards` verify
  row hard-requires **exactly one ALSA card, `allwinnerhdmi`** — the attached
  mic (card 1) makes that row **FAIL**. It must be revised to tolerate the
  known-good USB capture card (plus card 0) before the next `setup.sh` pass;
  the `docs/setup.md` mandate applies to that change.
- The G0 blocker in the voice-agent gate plan is therefore **cleared**; the
  voice-agent status-checkpoint journal's "PTT input path is currently down"
  note is now about the daemon being disabled, not about a missing mic.

## 5. Open decisions / next steps

1. **Engine choice for interim partials** — operator is soliciting **second
   and third opinions from search-enabled harnesses** on whether streaming
   partials are achievable on this board and with what (Vosk vs Paraformer vs
   hybrid) before committing. *Nothing has been installed; the whisper.cpp
   pin is untouched.*
2. **Daemon restore + re-mapping** — `gamepi-buttons.service` is
   disabled/inactive; after it is re-enabled the TL→`Control_L` press must be
   re-confirmed end-to-end (X side), since the 09-29 note warned the 9-key
   table was from the pre-swap HAT session. (Today's live probe shows PL2 is
   healthy on the HAT as currently populated.)
3. **`setup.sh` audio-cards revision** (see §4) — blocks the next
   `apply → verify` pass while the mic is attached.
4. If an engine is chosen: G2-class round trip (hold TL → stream partials →
   finalize on release → inject into focused field on `:1`) with measured
   wall times, per the gate-plan discipline.

## Artifacts on the board

- `/tmp/tl_monitor.py` — the gpiod-2.x TL poll monitor (disposable).
- `/tmp/probe16k.wav` — the 2 s mic probe (disposable).
