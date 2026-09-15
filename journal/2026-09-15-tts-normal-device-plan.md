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

## WS2 — v3.6 socket-daemon engine (CLOSED 2026-09-15)

- Task 2.1 (agent): `tools/hat-sound.c` v3.6. `-d SOCKET` daemon mode:
  bind (0666) → accept loop, serialized on accept; per job: ring counter
  reset, per-job `feed_thread` (re-drops FIFO→CFS at entry), the FROZEN
  tick path plays the client stream, per-job `stats_line` + self-report to
  stderr (= journal); pin acquired ONCE at start, released LOW only on
  exit. `-d` + any source/tone arg → rc 2; `--duration SEC` replaces the
  old `-d SEC`. Two field fixes during deploy (see below) → final md5
  `6b7b9ea18c5ac3560132eb5f3d0a4008`.
- Task 2.2 (agent): `setup.sh` unit rewrite — `ExecStart=… -d
  /run/gamepi-sound.sock`, `RuntimeDirectory=gamepi-sound` dropped (socket
  sits directly in tmpfs /run; engine unlinks it), new `sound-socket`
  verify row, `docs/setup.md` same-change. `bash -n` clean; plan-mode unit
  drift reporting is free via `write_if_changed`.
- Task 2.3 (agent, local): `--help` → usage rc 2; `-d … -F -` → rc 2
  conflict; `-t 440 --duration 5` → expected `open /dev/gpiochip0:
  Permission denied` (the board DOES have gpiochip0 — root-gated, not
  absent); non-root daemon run proved the pin-fail path removes the
  just-bound socket (no stale file).
- **DISCOVERY: workspace == live board.** The VS Code workspace folder is
  on the Orange Pi itself (`hostname` = `micro-cyberdeck`). Non-sudo agent
  terminal commands run ON the board (compiles, stat, objdump, C probes);
  `sudo` still requires the operator's password. The earlier "host has no
  gpiochip0" assumption was a permission error misread as absence.
- **FIELD FINDING 1 — fchmod(fd) ≠ path mode on this kernel/tmpfs.**
  First client got `EACCES` despite the unit's daemon running. Isolated C
  test (`/home/cj/fchmod_test`): after `bind` under umask 022 +
  `fchmod(fd, 0666)`, `fstat(fd)` → 0666 but `stat(path)` stays **0755
  forever**; `connect()` and the client's `stat()` consult the path view →
  EACCES for non-root. `chmod(path, 0666)` is the reliable lever. Fix:
  `socket_bind` does `fchmod(fd, 0666)` **and** `chmod(path, 0666)`
  (chmod is load-bearing; fchmod kept as harmless redundancy). Lesson
  recorded: on this board, verify socket perms with `stat`, never assume
  `fchmod` landed.
- **FIELD FINDING 2 — glibc `signal()` silently restarted the daemon's
  blocking `accept()`.** First `systemctl stop` hung 90 s (journal:
  `State 'stop-sigterm' timed out. Killing.` → SIGKILL, `Failed with
  result 'timeout'`) on every attempt. glibc's `signal()` implies
  `SA_RESTART`: the SIGTERM handler set `g_stop`, but `accept()` was
  silently resumed, so the flag was never consulted. Fix: `sigaction()`
  with `SA_NODEFER` and (crucially) **no `SA_RESTART`** →
  `accept()` returns `-1/EINTR`, the existing loop condition exits, pin
  released LOW, "daemon stopped — pin released LOW" in the journal. The
  one-shot tick loop was immune (pure spin reads `g_stop` every tick).
  Tick path untouched (FROZEN).
- Task 2.4 (operator, live): apply #1 = `applied /usr/local/bin/hat-sound`
  + `applied /etc/systemd/system/gamepi-sound.service` → restart → all
  verify PASS incl. `sound-socket` → `RESULT: DRIFT:2`; apply #2 =
  zero-write `RESULT: CONVERGED` (a first CONVERGED block had one
  transitional `[FAIL] xvfb-screen` — `xdpyinfo` dimension mismatch
  mid-settle; the next full apply PASSed it, and `bridge-live` had
  already confirmed the 960x960 X server, so no action). `stat` on the
  socket after the fchmod fix: `srw-rw-rw- root:root`. After the
  sigaction stop-fix was folded in: apply #1 again `DRIFT:2` (binary +
  unit) with all verify PASS — incl. `xvfb-screen` — `RESULT: DRIFT:2`;
  `time sudo systemctl stop gamepi-sound` returned in **0.119 s** with
  `Deactivated successfully` + `daemon stopped — pin released LOW` (was
  90 s + SIGKILL); apply #2 zero-writes `RESULT: CONVERGED` (the script
  re-starts the stopped unit before verifying, re-creating its
  `multi-user.target.wants` symlink).
- **Gate (2.5) PASSED 2026-09-15** — operator: clean tone. The 0.5 s
  440 Hz python client (as user `cj`, no sudo) connected to 0666 and the
  journal self-report: `job (v3.6 daemon) client EOF, ring drained —
  source 48000 B, consumed 48000 B (24000 samples, 47873.9 Hz, 1.00x
  real-time), underruns 51`; stats line `socket t=0.5s ticks=96195
  dev_avg=57.9 ns dev_max=69921 ns late1us=238 x1ms=0 x10ms=0 und=51
  duty=49%`. Underruns (51) are slightly higher than the WS1 speech run
  (1) but x1ms=0/x10ms=0 — sub-microsecond jitter, no audible impact
  (operator confirmed clean).
- WS2 CLOSED. Commit: hat-sound v3.6 + setup.sh unit flip + docs + plan +
  index + this journal. WS3 = the ALSA `pcm_hat` plugin + `/etc/asound.conf`
  (3.1 plugin source; 3.2 board scratch probe BEFORE any setup.sh wiring).

## Tree state

- WS1 committed as `3e92042` (converter + plan + indexes + journal).
- WS2 commit: `tools/hat-sound.c`, `setup.sh`, `docs/setup.md`, plan,
  `plans/current/index.md`, this journal.