---
plan_id: 2026-09-23-22-57-17_voice-agent-gates
title: Voice agent — run the gates (G0 mic → G1 resident → G2 chain → G3 app)
summary: Gate execution for projects/voice-agent per the design's G0-G3 plan. G0 = identify + prove the audio input path (2026-09-23 probes: the only ALSA capture subdevice is a dead-end HDMI-Rx codec — bit-exact silence — so Option A, USB mic via OTG, is the declared path; hardware proof pending). G1 = provisioning batch + whisper.cpp vendoring + both models resident + the RAM numbers logged. G2 = the headless shell chain with per-stage wall times. G3 = the ebe app push-to-talk round trip on :1.
status: current
created_at: 2026-09-23-22-57-17
revised: 2026-09-23
---

Key: `[ ]` pending task, `[x]` completed task, `[?]` needs validation, `[-]` closed task

# Voice agent — run the gates

Design of record: [`projects/voice-agent/docs/design.md`](../../projects/voice-agent/docs/design.md)
(§8 is the gate contract; this plan executes it). Predecessor:
[2026-09-23_voice-agent-design.md](2026-09-23_voice-agent-design.md) (the design
plan, closed by the commit that created the project).

Standing rules in force for every task: hardware findings update the relevant
`docs/hardware/` record + README row in the same change (standing rule);
evidence goes to `journal/`; nothing destructive (model deletion, service
changes) runs without the operator checkpoint it names.

## G0 — the mic (design §4, §8)

The gate that gates everything: prove *some* `ALSA_INPUT` records audible
audio.

- [x] (G0-a) Board probes, 2026-09-23 22:56 local, no sudo:
  - `/proc/asound/pcm` → card 0 (`allwinnerhdmi`) reports
    **`playback 1 : capture 1`** — a capture subdevice *does* exist
    (`/dev/snd/pcmC0D0c`), and `arecord` is installed
  - `arecord -D plughw:0,0 -f S16_LE -r 48000 -d 5` → **240000 frames,
    peak=0, rms=0, 100.0% zero samples** (bit-exact silence)
  - `amixer -c 0 contents` → every control on the card is an
    **HDMI bitstream-format** control (AC3/DTS/DOLBY/…/"loopback debug") —
    the "capture" direction is the SoC HDMI audio codec's **Rx** (audio
    arriving over an HDMI *input* this SBC does not use), not an analog mic
    stage
  - `lsusb` → root hubs only; **no USB audio device attached** today
  - reference-clone wiki (Spotpear GM154 1.54" Game LCD): the base HAT has
    **no audio at all** (its audio tutorial requires buying an external GPIO
    audio module); this HAT clone adds speaker+amp (output-only GPIO/PWM
    bitstream, `speaker-hat-amp.md`) and a 3.5 mm jack; **no microphone is
    documented anywhere**
  - landings: new record `docs/hardware/audio-input-mic.md` + README row
    (standing rule), journal
    [`journal/2026-09-23-voice-agent-g0-mic-probes.md`](../../journal/2026-09-23-voice-agent-g0-mic-probes.md)
- [?] (G0-b) **Hardware proof pending — the operator plugs in a USB mic.**
  Expected shape: a new card (`hw:1`) with a capture subdevice appearing in
  `arecord -l`; a 5 s `arecord` shows non-zero peak/rms. Agent re-probes on
  word; the winning `ALSA_INPUT` string gets recorded in the journal and
  carried into G2's chain defaults
- [ ] (G0-c, parked — operator physical, optional) onboard-input
  investigation: open the case, trace the 3.5 mm jack's second conductor,
  identify the eight-pin mystery IC in the audio section (shareable with the
  power-path plan's WS1 chip-ID step), meter the codec's I²S/mic lines. Only
  worth running if the operator wants an onboard mic instead of a USB one;
  even a *winning* find needs an overlay (+ possibly a codec driver module —
  `CONFIG_SND_SOC_ES8388` absent *to-probe*) before it reaches Linux
- **G0 exit criterion:** an `ALSA_INPUT` string + a journal with non-zero
  capture numbers. *(Decision is made (A: USB mic via OTG); the [?] proof is
  the only open item and it needs a part the board doesn't currently have.)*

## G1 — models resident (design §3, §8; independent of G0's [?] item)

- [x] (G1-a) **Operator checkpoint: disk — satisfied 2026-09-23.** Pruned
  **qwen3.5:9b (measured 6.14 G, inside the Ollama container volume —
  `docker`-side, no host binary involved)** on the operator's instruction:
  root FS **2.5 G free (92 %) → 8.6 G free (71 %)**. `setup.sh` verified
  presence-only (no re-pull risk); `/api/ps` empty before and after.
  Evidence: [journal/2026-09-23-voice-agent-g1a-9b-prune.md](../../journal/2026-09-23-voice-agent-g1a-9b-prune.md).
- [x] (G1-b) **Complete 2026-09-23.** `third_party/whisper.cpp` pinned as a
  submodule at tag `v1.9.4` (`927cfce`); `ebe-boilerplate` cloned to the
  board's workspace (`e741e1c`, git-ignored — external, not a submodule, per
  design §9). The operator ran the toolchain batch
  (`sudo apt install -y build-essential cmake pkg-config`): cmake 3.31.6,
  g++ 14.2.0, pkg-config 1.8.1 verified on the board; ALSA dev (1.2.14) was
  already present so `libasound2-dev` was not needed.
- [x] (G1-c, partial) **Weights + build done 2026-09-23; the unit install
  remains the operator step.** `ggml-medium.bin` (1.48 G) at
  `~/voice-agent/ggml-medium.bin`; **CMake build of `whisper-server`
  complete** (Release, `-march=native` →
  `third_party/whisper.cpp/build/bin/whisper-server`, 1.2 M, in-submodule
  untracked). Two deviations from the design, both evidenced: **port 8086
  (not 8080** — `:8080` is the root `cyberdeck-api.service`, the
  setup.sh `api-health` verify target) and a **system unit** with
  `User=cj` (**not a user unit** — this board has no user session:
  `loginctl` reports none, no `/run/user/1000`, `DBUS_SESSION_BUS_ADDRESS=
  disabled:`; `gamepi-xvfb.service` is the precedent). Unit written to
  `projects/voice-agent/systemd/whisper-server.service`; **operator install
  owed** (`sudo cp → /etc/systemd/system/ && daemon-reload && enable --now`)
  + post-boot verify. Foreground functional test passed pre-reboot: UP after
  ~6 s, `/health` ok, `/inference` decoded a 1 s test WAV.
- [ ] (G1-d) Ollama residency pin: `keep_alive: -1` requests against the
  existing `ollama` Docker deployment (127.0.0.1:11434) for
  `qwen3.5:4b`; verify loaded after ≥10 min idle (no eviction). Status:
  mechanism proven (pin `expires_at` year 2319, 3.66 G size in `/api/ps`);
  **pins are in-process — every container restart/reboot loses them and a
  re-pin request is required**; the 2026-09-23 23:35 reboot lost the pin
  and it was re-established. Two more lessons locked in: requests to the
  thinking-family `qwen3.5` **must carry `"think": false`** (a no-think
  request spun a 12 m 50 s runaway thinking block that had to be killed via
  `docker restart ollama`), and the ≥10 min idle check is still owed.
- [ ] (G1-e) **Exit criterion:** `free -h` with both idle-resident shows
  ≈ 5 GB combined (measured: 3.16 G 4b + ~1.5 G medium + ~0.3 ebe/llvmpipe)
  with ≈ 4 G still available, logged to the journal; whisper `/inference`
  answers a test WAV; Ollama `/api/ps` shows `qwen3.5:4b` resident. Status:
  `/inference` proven (test-WAV decode); both-resident observation 8.2 Gi
  used / 300 Mi free / 3.4 Gi available (tight; **zram swap 5.8 G exists** —
  correcting the earlier "no swap" note); the t=2/4/8 thread sweep was
  **invalidated by the 23:35 unclean reboot** (heaviest-load correlation —
  the t=8 decode's completion coincides with the cut; numbers: t=2 56 s,
  t=4 46→55 s, t=8 51 s on a 1 s input = ~50× class, treated as
  "≈ 50× real-time, thread-count not separable" until a controlled re-run —
  this blows past the design §5 >2× small-medium fallback trigger)
  [2026-09-23-g1-reboot-incident.md](../../journal/2026-09-23-g1-reboot-incident.md)

## G2 — the wire (design §5–7, §8)

- [ ] (G2-a) The headless chain, shell-scripted (no app, no HUD):
  `arecord $(ALSA_INPUT) 16 kHz mono` during a timed hold → WAV →
  whisper `/inference` → transcript → Ollama `/api/chat`
  (`qwen3.5:4b`, streaming, `keep_alive: -1`) → answer → `espeak-ng` → WAV →
  `tools/wav2s16.py` → `/run/gamepi-sound.sock`. One file,
  `projects/voice-agent/tools/g2-chain.sh` (tracked; the design's §9 batch
  remains the only provisioning step)
- [ ] (G2-b) **Exit criterion:** a real spoken round trip through the
  speaker, with **per-stage wall times** in the journal (capture / ASR /
  LLM-first-token / TTS / speak). If ASR > ~2× real-time, the design §5
  fallback (whisper-small) is re-decided with numbers

## G3 — the app (design §1–2, §8)

- [ ] (G3-a) ebe app on `:1` from the ebe-boilerplate clone: full-screen
  960×960 window, HUD state machine **idle → LISTEN** (on
  `Control_L`-just-pressed) **→ ANSWER** (on release: transcript, then
  streamed tokens), `ebiten.SetTPS(15)` (the ST7789 bridge samples ~8 fps —
  `display-st7789.md`), the G2 loop driven over the loopback protocol from
  design §2 (sidecar seam per the design's decision-3, still reopenable to
  single-binary)
- [ ] (G3-b) **Exit criterion:** hold the *physical* left bumper → talk →
  release → the answer is **spoken** (socket) and **streamed** (HUD),
  on-deck, journal-logged. Green G3 flips the README
  [Known Unresolved Issues](../../README.md#known-unresolved-issues) bullet
  "Building voice-interactive local agent software" to a running-state note
  (the peripheral-integration sub-bullet stays open)

## Standing

- [ ] every hardware-relevant finding updates its `docs/hardware/` record +
  README row in the same change
- [ ] journals for each gate landing; this plan closes to `plans/past/` when
  G3 is green