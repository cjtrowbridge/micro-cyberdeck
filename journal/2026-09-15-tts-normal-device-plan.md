# 2026-09-15 — TTS + "normal sound device" plan (plan: 2026-09-15-13-17-28_hat-alsa-device-and-tts)

## Plan approved

- User approved the staged plan by voice ("ok go ahead", 2026-09-15 ~13:2x).
- Plan lives at `plans/current/2026-09-15-13-17-28_hat-alsa-device-and-tts.md`
  (promoted `future → current` at the first implementation edit, task 1.3).
- Scope authorization recorded IN THE PLAN: the "no audio features beyond ONE
  feeder/underrun tweak" constraint applied to the pre-fold-in tuning period;
  this plan is new operator-authorized scope.

## Architecture (binding decisions)

- Two planes over a unix socket — the pin stays root-owned, the plugin stays
  thin (client user):
  1. **Engine**: `hat-sound v3.6 -d /run/gamepi-sound.sock` in the existing
     `gamepi-sound` unit. FROZEN tick/modulator loop reused byte-for-byte;
     pin acquired once, held until shutdown, LOW between jobs; only the
     feeder's input source changes (file fd → socket fd). Serialization =
     one active client (accept loop).
  2. **Device**: ALSA `pcm_hat` plugin (any user) — connect, resample → 48k
     if needed, stereo→mono downmix, blocking write to the socket.
     `/etc/asound.conf`: `pcm.hat` + `pcm.!default → hat` (hdmi card stays
     reachable explicitly). `aplay` / `espeak-ng "text"` work with no flags.
- Fallback if custom plugin loading proves fussy: ALSA `shm` protocol
  device; WS2 (engine + socket) is useful standalone regardless.
- The stop/start dance dies with v3.6.

## WS1 — TTS gate (in progress)

- Task 1.1 done: `espeak-ng 1.52.0` already installed on the board
  (`/usr/bin/espeak-ng`, data at `/usr/lib/aarch64-linux-gnu/espeak-ng-data`).
- Task 1.3 done: `tools/wav2s16.py` written (stdlib `wave`+`array`; mono or
  stereo 16-bit, any rate → s16le 48k mono; linear resample; (L+R)/2
  downmix; one-line summary; native LE = aarch64 = s16le). Validated:
  py_compile OK + three generated WAVs (22050 mono / 44100 stereo / 48000
  passthrough) all converted to exact expected sample counts.
- Task 1.2 DONE (operator, 2026-09-15): `espeak-ng -a 120` → tts-test.wav →
  `tools/wav2s16.py` → `148552 samples @ 48000 Hz mono (3.09s)`; hat-sound
  v3.5: `rt OK all three`, `consumed 297104 B (148552 samples, 47990.9 Hz,
  1.00x real-time), underruns 1`, `pin released LOW`.
- Task 1.4 DONE (agent): 1 underrun over ~588k ticks = ~50 µs — below the
  audible-click threshold; clock 1.00x. Self-report healthy under continuous
  full-band speech (the stress tones never applied).
- Task 1.5 PASSED 2026-09-15 (operator "ok continue" after the listen gate):
  clean voice at `-a 120`, no level change requested. WS1 CLOSED — committed
  (converter + plan + indexes + this journal). WS2 = v3.6 socket engine; the
  stop/start dance dies there.

## Tree state

- Tree dirty: plan (current/), `tools/wav2s16.py`, index updates, this
  journal. Nothing else touched. Commit gated on the WS1 listen gate per
  the plan (WS1 is "no repo change yet" except the converter).