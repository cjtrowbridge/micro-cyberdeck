# Voice Agent — Runtime Design

Status: **design** (2026-09-23). Nothing on the board is touched by this
document. Conventions cited below are verified in the linked records; anything
marked *to-probe* is a deliberate unknown pending gate G0.

## 1. Goal

A push-to-talk voice agent on the deck:

1. **Hold** the left bumper (TL) → capture the voice.
2. **Release** → stop, transcribe, ask, speak.
3. Both models — **whisper-medium** (ASR) and **qwen3.5:4b** Q4_K_M (LLM, via
   Ollama) — stay **resident in RAM** so round-trip latency is measured in
   seconds of inference, not in model-load time.
4. The agent lives in an **Ebitengine app on the 960×960 `:1` desktop**
   (full-screen; the ST7789 bridge then downsamples it to the 240×240 panel
   like everything else on the deck).

## 2. Architecture

```
 TL membrane ────────────────┐
 gamepi-buttons (XTest :1)   │            ┌──────────────────────────┐
 Control_L key, 30ms debounce├───────────▶│  ebe app (Go/Ebitengine) │
                             │            │  DISPLAY=:1, 960×960     │
 mic (ALSA input, undecided) ├───────────▶│  capture ring buffer     │
                             │            │  push-to-talk state m.   │
                             │            │  HUD: state/listen/text  │
                             │            └────────────┬─────────────┘
                             │                         │ release event
                             │                         ▼
                             │        ┌────────────────────────────────┐
                             │        │ sidecar daemon (Go)            │
                             │        │  WAV → whisper-server :8080    │◀──── whisper-medium
                             │        │  text → Ollama /api/chat       │◀──── qwen3.5:4b (resident)
                             │        │  answer → espeak-ng → 48 kHz   │
                             │        │         → /run/gamepi-sound.sock│
                             │        └────────────────────────────────┘
```

**Why the sidecar exists (design decision, open for review).** The ebe app is a
*UI client* only: it watches `Control_L` (`ebiten.IsKeyJustPressed/Released`
with `KeyControlLeft` — keycode 37 on `:1`, already proven by
[docs/hardware/membrane-buttons.md](../../docs/hardware/membrane-buttons.md))
and renders whatever the agent feed says. The loop — capture → transcribe →
generate → speak — runs in a small non-UI daemon (or, minimum viable, a Go
goroutine in the same binary; the boundary is a deliberate seam, not a dogma).
Reasons: the loop is testable headlessly (no `:1`, no GL, no llvmpipe), the
Ebitengine process stays a thin consumer of llvmpipe time, and restarting the
app never disturbs a resident model conversation. If the single-binary shape
wins, the seam moves to an interface in the Go code; nothing external changes.

**Transport.** One loopback TCP socket (or an in-process interface in the
single-binary shape), minimal framed protocol: `START` / `STOP` on hold edge,
streamed `TEXT` deltas, `SPEAK` events. Nothing crosses the LAN — loopback
only, consistent with the deck's loopback-only posture for the Go metrics API.

## 3. Residency policy (the "resident" requirement)

| Model | Residency mechanism | RAM (expected) |
|---|---|---|
| qwen3.5:4b Q4_K_M | Ollama unloads idle models after ~5 min by default. Send **`keep_alive: -1`** with every `/api/chat` request (or set `OLLAMA_KEEP_ALIVE` on the container) → the 3.4 GB weights stay mapped for the process life of the container. | ~3.4–4.0 GB |
| whisper-medium | No unloader exists to fight: run one `whisper-server` process (`-m ggml-model-whisper-medium-*.bin -t 2`) as a **user systemd unit**; the model is loaded at process start and lives until the process dies. `Restart=always`. | ~1.5 GB |

RAM feasibility (board reports **11 Gi** total; [docs/hardware/npu.md](../../docs/hardware/npu.md) "RAM observation"): 3.4 + 1.5 + ~0.3 (ebe/llvmpipe) ≈ **5.2 GB** vs ~8.9 Gi currently available → both resident comfortably, ~5–6 GB headroom left for the desktop, the whisper server's audio buffers, and transient WAV files. *Validation at G3, not assumed: baseline `free -h` with both idle-resident, then under load.*

Scheduling (efficiency lever, optional until measured): `taskset` whisper.cpp to the four A76 cores, leave the A55s for the HUD/bridge/desktop. Measure first; the deck's fan budget ([docs/hardware/fan.md](../../docs/hardware/fan.md)) is the reason not to just pin the big cores blindly.

## 4. Audio capture — the one real unknown

**Facts (verified design inputs):**

- The board exposes exactly one ALSA card: `allwinnerhdmi`
  ([docs/hardware/speaker-hat-amp.md](../../docs/hardware/speaker-hat-amp.md),
  [docs/hardware/hdmi-audio.md](../../docs/hardware/hdmi-audio.md)). Its
  "playback" is the HDMI *bitstream* DAC. Whether the `hw:0` **input** subdevice
  captures anything is *to-probe* — if there is no source wired to the codec's
  input, it will open at 0 dB or refuse.
- `setup.sh`'s **stale-overlay** verify row keeps **no** `es8388-audio`/`i2c0`
  overlays on this board — the proven fact is that the HAT codec *is not an I²S
  codec* (the amp is GPIO/PWM-driven, `NS8002`). The journal's removal
  rationale concerns **playback**; whether the HAT routes *any* microphone to
  the codec's MIC pins is **unverified** — the codec (ES8388-class) is
  bi-directional (mic PGA/ADC) on paper, but the board may not wire the input
  side.
- The HAT has a **headphone (audio) jack** — jack wiring unknown, untested for
  input ([docs/hardware/headphone-jack.md](../../docs/hardware/headphone-jack.md)).
- The SBC is a Pi Zero form factor: one **OTG port**.

**Decision options (owner: operator) — ranked by risk:**

| Option | Work | Risk of "no audio at all" | Notes |
|---|---|---|---|
| **A. USB mic via OTG** (analog or USB-PnP digital) | zero kernel work | *low* — it's a normal ALSA card, `arecord -l` lists it | the pragmatic default; one $10 part; a visible dongle on the deck |
| **B. HAT codec MIC path (if the pads/wiring exist)** | overlay *or* userspace codec control (snd-ens1371-style via `i2cset` — the kernel has no `es8388` driver on this box: `CONFIG_SND_SOC_ES8388` absent *to-probe*); board-wiring check via the underside photos + a meter probe | *high* — could be an unbounded rabbit hole on this hat clone | would be the "clean" permanent answer: onboard mic, zero dongles |
| **C. Headphone-jack as mic** | depends on whether the jack on this HAT is a TRRS mic in — unknown | *medium* | check the reference Spotpear clone's pinout first (free) |

**Gate G0 settles this with evidence, not argument** (see §8): probe
`arecord -l` on the live `hw:0`; probe the jack (play a tone into it); consult
the reference clone's spec for the codec's MIC wiring. The design below works
unchanged once *any* input device answers — the sidecar just takes an
`ALSA_INPUT` device string (default `hw:1` for a USB card, `plughw:0,0` if the
codec input proves real).

**Capture contract (whatever option wins):** 16 kHz, mono, s16, 30–60 ms
frames, recorded into a ring buffer during the hold; on release the buffer is
written as a WAV (the same format `tools/wav2s16.py` already normalizes, for
consistency) and handed to the sidecar. This is standard `arecord` or
golang.design/x/audio territory — the mic path is the only genuinely new
surface.

## 5. ASR

- **whisper.cpp (not whisperX/faster-whisper):** single static binary + model
  file, no Python runtime on the board, NEON-optimized, trivially resident
  (`server` keeps the model mapped).
- **Server mode** (`server -p 8080 -m … -t 2 -m`): the app POSTs WAV to
  `127.0.0.1:8080/inference`; resident for the life of the unit.
- **Model:** whisper-medium ≈ **1.45 GB** on disk, ~1.5 GB resident.
- **Latency expectation (design assumption, to be confirmed at G2):** medium
  on CPU (4×A76, A733, no NEON-LLM-boost for whisper) is roughly **1× real-time
  class** — a 5 s utterance ≈ a few seconds of decode. For a push-to-talk agent
  that is *usable* (release, read the prompt, answer streams while you wait),
  not *snappy*. If it proves too slow: the swap to whisper-small/base is a
  model-file change, nothing architectural (the residency policy is model-agnostic).
  *Conversely, "medium" was the requirement — the swap is a documented
  fallback, not the plan.*
- **Streaming partials (optional, gate G2+):** the hold can stream ring-buffer
  chunks for interim transcripts on the HUD. whisper.cpp's server has no
  first-class partial endpoint; this is v1.1, behind the first round trip.

## 6. LLM

- **qwen3.5:4b Q4_K_M via the existing Ollama 0.34.1 Docker deployment**
  ([docs/hardware/npu.md](../../docs/hardware/npu.md)): `http://127.0.0.1:11434`,
  container `ollama`, **CPU-only** (no Allwinner NPU backend exists for Ollama —
  that record's "And the Ollama question" is the standing fact).
- `POST /api/chat` with `{"model":"qwen3.5:4b","stream":true}"` and the
  `keep_alive: -1` residency pin (§3). The system prompt (the agent's persona)
  is a config file in `projects/voice-agent/` (the real file, small, committed;
  only *secrets* — none needed for local model — stay out).
- **Streaming is non-negotiable for UX**: tokens land on the HUD while the
  answer is still generating; the TTS pipeline reads the *complete* answer
  (see §7 — we do not half-speak a sentence).
- **Disk note:** qwen3.5:4b (3.4 GB) is already on the board. The 6.59 GB
  **qwen3.5:9b is the obvious prune candidate** to make room for
  whisper-medium + toolchain (~2.5 GiB free today). *Operator decision — recorded
  here, not executed by this design.*

## 7. TTS + speaker

- **espeak-ng → WAV → `tools/wav2s16.py` → raw s16le 48 kHz mono →
  `/run/gamepi-sound.sock`** — the exact chain `setup.sh`'s tts-selftest already
  proves works end-to-end. No resampler bug surface: 48 kHz is the engine's
  wire rate, and `wav2s16.py` is the stdlib-only converter of record
  (no ffmpeg/sox on the board).
- **One client at a time** on the socket (engine contract): the sidecar is the
  *only* client it talks to; the daemon serializes. The paused ALSA-plugin arc
  ([docs/hardware/audio-normal-device.md](../../docs/hardware/audio-normal-device.md),
  gate c is the voice-agent round trip) doesn't block us — we *use its
  documented workaround* (the socket) by design and revisit only if that arc
  flips green.
- **espeak-ng is a robot voice.** That's the floor, not the ceiling: the
  speaker path is model-format-agnostic (it's raw PCM), so a neural TTS (a
  local GGML/ONNX TTS, or `piper`-class with a smaller model) is a drop-in
  replacement later. *Not in scope for the first round trip.*

## 8. Gates (the way this becomes true)

| Gate | What it proves | Exit |
|---|---|---|
| **G0 — the mic** | which capture option (§4) yields a working input: `arecord -l` on `hw:0`, tone-into-jack probe, reference-clone pinout lookup (USB mic ordering is the fallback that can always unblock) | an `ALSA_INPUT` string that records audible audio, recorded in a journal |
| **G1 — models resident** | whisper-server up with medium loaded + Ollama loaded with 4b + `keep_alive:-1`; **both** idle, `free -h` shows ~5.2 GB total with ~5 GB free; neither evicted after 10 min | the residency numbers above, journal-logged |
| **G2 — the wire** | `arecord`→WAV→whisper→text→Ollama→text→espeak-ng→**speaker**, shell-scripted, headless (no app, no HUD) — the loop with zero UI | a working shell chain + a journal with measured per-stage wall times |
| **G3 — the app** | the ebe app on `:1`: full-screen 960×960, HUD state machine (idle → LISTEN on hold edge → ANSWER on release), the loop from G2 driven by `Control_L`, answer spoken + streamed | the push-to-talk round trip works on the deck |

G0 and G1 are independent and can proceed in parallel (the mic question and the
model question don't touch each other). G2 needs both. G3 needs G2.

## 9. Provisioning batch (when we stop designing)

One `sudo apt install` batch on the board (operator), after G0 decides the mic
and before G1: `build-essential` (g++ for whisper.cpp), `make`,
`pkg-config` + `libgl1-mesa-dev`/X11 dev headers (Ebitengine native builds on
llvmpipe), `espeak-ng` (verify present — the tts-selftest implies it is),
`ffmpeg` **not** needed (`wav2s16.py` covers conversion). Plus: clone
whisper.cpp (vendor it under `third_party/` as a submodule — the repo's pinned-work
convention — at the first real build; the model file **never** enters git),
and the ebe-boilerplate clone (external, per the top-level README — not a
submodule; it's *your* boilerplate, pinned by whatever the clone carries).
Go **1.24.4** is present; if ebe-boilerplate's `go.mod` demands newer Go,
`go install`ing the toolchain locally is the move (not a system Go upgrade).

## 10. Open decisions (owner · status)

1. **Which mic** (A/B/C, §4) — *operator · open, G0 decides with evidence*
2. **Prune qwen3.5:9b to make disk room?** — *operator · open*
3. **Sidecar process vs in-app goroutine** (§2) — *design · leaning sidecar, not locked*
4. **whisper-medium vs smaller if G2 shows >~2×RTS** (§5) — *design · deferred until measured*
5. **HUD layout** (960×960, but the panel shows the downscaled 240×240 — legibility budget) — *design · deferred to G3; the ST7789 bridge record is the constraint*
6. **Neural TTS vs espeak-ng** — *operator · deferred past first round trip*