# Voice Agent (push-to-talk runtime)

> Status: **Gates in progress** (2026-09-29 checkpoint) — G0 decided (Option
> A, USB mic via OTG; hardware proof pending); G1: 9b pruned, whisper.cpp
> v1.9.4 pinned + **server built**, medium weights staged — but the **system
> unit is not installed, the Ollama pin was lost in the 09-23 unclean reboot
> and never re-established, and the clean sweep is invalidated** (open end:
> two operator installs, re-pin, controlled sweep). This project owns the
> "voice-interactive local agent software" item from the top-level
> [Known Unresolved Issues](../../README.md#known-unresolved-issues) list.
> 2026-09-29 evidence: [journal/2026-09-29-voice-agent-g1-status-checkpoint.md](../../journal/2026-09-29-voice-agent-g1-status-checkpoint.md).

A push-to-talk voice agent for the deck: **hold the left bumper → talk →
release → the resident local LLM answers out loud**, with a full-screen
Ebitengine HUD on the 960×960 `:1` desktop. Both models stay **resident in
RAM** so the round trip has no model-load latency:

- **whisper-medium** — ASR, whisper.cpp `server`, resident for the process life
- **qwen3.5:4b** (Q4_K_M) — the brain, served by the existing Ollama
  deployment, pinned with `keep_alive`

## Layout

| File | Purpose |
|---|---|
| [docs/design.md](docs/design.md) | the runtime design: topology, residency policy, evidence base it was built from, resource math, open decisions, and the gate plan (G0–G3) that turns this design into a working round trip |

## External work (never tracked here)

| Dependency | Role | Where it lives |
|---|---|---|
| [ebe-boilerplate](https://github.com/cjtrowbridge/ebe-boilerplate) | the Ebitengine skeleton the HUD runs in ("the ebe pipeline"; top-level README) | cloned at bring-up, not a submodule |
| [whisper.cpp](https://github.com/ggerganov/whisper.cpp) | provides `server` + the medium weights | pinned in-repo as a submodule at **v1.9.4** (`third_party/whisper.cpp`); build + weights live on the board at `~/voice-agent/ggml-medium.bin` (1.48 G) |
| Ollama 0.34.1 (Docker container, `:11434`) | serves qwen3.5:4b on CPU | existing `setup.sh`-verified deployment — see [docs/hardware/npu.md](../../docs/hardware/npu.md) |

## Related deck records

- [docs/hardware/membrane-buttons.md](../../docs/hardware/membrane-buttons.md) — the left bumper is already live as a `Control_L` XTest keystroke on `:1` (the `gamepi-buttons` daemon); this project consumes that signal, it does not re-plumb one
- [docs/hardware/speaker-hat-amp.md](../../docs/hardware/speaker-hat-amp.md) — the answer path: raw s16le 48 kHz mono to `/run/gamepi-sound.sock`, one client at a time
- [docs/hardware/audio-normal-device.md](../../docs/hardware/audio-normal-device.md) — the paused ALSA-plugin arc; this project deliberately rides the socket workaround until it flips green
- [docs/hardware/display-st7789.md](../../docs/hardware/display-st7789.md) — the bridge that ships whatever the app draws on `:1` to the 240×240 panel
- [docs/hardware/npu.md](../../docs/hardware/npu.md) — why the LLM runs CPU-only via Ollama (no NPU backend exists for it)

## Known inputs and blockers (design-time facts, 2026-09-23)

- **No audio input path exists on the board today** — only the `allwinnerhdmi`
  ALSA card; capture is a *design decision with two candidate paths*, not a
  bug (see docs/design.md §4: USB mic vs. HAT codec input)
- **Disk was tight; resolved 2026-09-23**: qwen3.5:9b (measured 6.14 G) pruned
  from the Ollama container — root FS 2.5 G free (92 %) → **8.6 G free
  (71 %)**; [journal/2026-09-23-voice-agent-g1a-9b-prune.md](../../journal/2026-09-23-voice-agent-g1a-9b-prune.md)
- **No GPU path** to the `:1` desktop (no Mali/panfrost kernel driver): the HUD
  renders under llvmpipe — fine for a text HUD, sized accordingly
- **Build toolchain landed 2026-09-23**: operator apt batch put cmake
  3.31.6 / g++ 14.2.0 / pkg-config 1.8.1 on the board (ALSA dev already
  present); the `whisper-server` build succeeded at
  `third_party/whisper.cpp/build/bin/` (Release, `-march=native`)

Gates G0–G3 in docs/design.md must be green (in order) before this project is
"running" rather than "designed"; the first green round trip also flips the
README Known-Unresolved item.