# Audio as a normal device

> Status: **In progress — paused** (mirrors the README table)

## What it is

The work arc that makes the HAT speaker behave like an ordinary ALSA PCM
device, so `aplay file.wav` and `espeak-ng -w out.wav` (and anything else in
the ALSA world) work with **no flags and no socket clients**. Implementation:
a custom ALSA PCM plugin that forwards frames to the running `hat-sound`
daemon's socket (the engine itself is frozen — see
[speaker-hat-amp.md](speaker-hat-amp.md)).

**Plan / recipe of record:** [`plans/future/2026-09-15-13-17-28_hat-alsa-device-and-tts.md`](../../plans/future/2026-09-15-13-17-28_hat-alsa-device-and-tts.md)
— gates, run scripts, resume procedure.

## What we know

- **Artifact:** `tools/hat_alsa_plugin.c` (≈852 lines) builds
  `libasound_module_pcm_hat.so`, installed where ALSA finds plugins. Exported
  entry points: `_snd_pcm_hat_open` and
  `__snd_pcm_hat_open_dlsym_pcm_001`. Current build md5
  `4b5898126e7219b589ce7d1069df01f3`.
- **Reference pinned here:** alsa-lib 1.2.14 sources at
  `/home/cj/hat-alsa-src/alsa-lib-1.2.14/` — the header/behavior reference for
  the plugin's frame/avail/hwsync handling.
- **Gate (a) — 48 kHz tone through the plugin: GREEN.** Playback paced
  correctly; wall time was **+10.6 %** over the expected for the tone
  duration. (Positive bias, not the bug being hunted — recorded as
  acceptable in the journal.)
- **Gate (b) — espeak-ng sentence at 22 050 Hz through the plugin: RED,
  deterministic.** Across runs 3/4: the engine consumes **exactly one burst
  of 1512 bytes** (= 756 s16 samples = 347.25 client frames = **15.75 ms of
  audio**, ≈ a 4 ms tick burst in engine time), then stops; the client stalls
  inside its ALSA wait loop for **~9.9 s** before giving up; throughout, the
  socket's **TX queue is zero** (the client is not blocked on sending — it
  believes the pipe is full).
- **Fresh-boot smoke:** engine read **14080 B** = **0.02×** the expected for
  the utterance — same class of failure on a clean boot, so it is not a
  session-state artifact.
- **Prime suspect (journal's classification):** alsa-lib's `pcm_ioplug`
  (resampler → `snd_pcm` glue) **avail/hwsync accounting at the resampled
  rate** — 22 050 Hz is not the 48 kHz wire rate, so the plugin's
  `avail`/`mmap`-style sync path is the only untested surface at the failing
  rate. 48 kHz (gate a) exercises the same plugin with *no* ioplug
  resampling and passes.
- **Discriminator not yet run (run5):** measure the **feeder thread's utime
  slope** during a gate-b stall — if utime keeps climbing, the engine is
  spinning (scheduling/lock); if it is flat, the stall is purely in the
  ALSA-layer bookkeeping. Exact monitor form:
  `E=$(pgrep -x hat-sound); sh monitor4.sh "" "$E" mon5.log 15 &`
  (tools are the volatile `/tmp/hatdiag` kit: `hatdiag_client_v4.c`,
  `procdump.sh`, `monitor4.sh`).
- **Gate (c)** (full TTS session + repeated voice round-trips; the
  "voice-interactive local agent" end goal) — **not started**.

## How we know

- All gate numbers and the stall anatomy:
  [`journal/2026-09-16-gate-b-red-client-wait-hang.md`](../../journal/2026-09-16-gate-b-red-client-wait-hang.md)
  (and the companion batch/recovery journal
  [`journal/2026-09-16-live-batch-paused-reboot-and-recovery.md`](../../journal/2026-09-16-live-batch-paused-reboot-and-recovery.md)
  for the 0.02× smoke figure).
- Build/identity history (audit → v2):
  [`journal/2026-09-16-hat-plugin-builds-audit-and-redesign.md`](../../journal/2026-09-16-hat-plugin-builds-audit-and-redesign.md),
  [`journal/2026-09-16-hat-plugin-v2-complete.md`](../../journal/2026-09-16-hat-plugin-v2-complete.md).
- Re-verify build identity (no sudo): `md5sum` of the installed
  `libasound_module_pcm_hat.so` vs `4b5898126e7219b589ce7d1069df01f3`.
- The plan file holds the repro scripts for each gate and the exact resume
  procedure.

## What it portends

- **This is the gate on "voice-interactive local agent software"** (a README
  known-unresolved item) being pleasant: without a normal device, every TTS
  call site is a bespoke socket client pinned to 48 kHz.
- Blocked on: run5 (feeder-utime slope) → classification of the RED
  (engine-side vs ALSA-ioplug-accounting) → fix decision per the journal's
  next-steps → gates re-run **a → b → c** in order.
- **Workaround until it's green:** client-side resample to 48 kHz before
  `aplay` (e.g. `espeak-ng ... | socat - UNIX-CONNECT:/run/gamepi-sound.sock`
  paths already in use) — i.e. use the daemon socket directly, as
  `hat-sound`'s existing callers do.
- **Paused on purpose** (the operator parked the arc 2026-09-16 mid-batch).
  Resume exactly per the plan file's procedure; the `/tmp/hatdiag` kit is
  volatile and must be rebuilt from the plan's recipe if the board was
  rebooted.