---
plan_id: 2026-09-15-13-17-28_hat-alsa-device-and-tts
title: Make the HAT speaker play "like a normal sound device" (ALSA jury-rig) + fast TTS (espeak-ng)
summary: Prove the speaker with real speech (espeak-ng, one-time gate), then integrate a root engine daemon (existing gamepi-sound unit, FROZEN tick path untouched) behind a thin user-space ALSA PCM plugin over a unix socket, so aplay / espeak-ng "text" work with no flags and no service stop/start; every setup.sh change documented in docs/setup.md in the same change and live apply->verify before each commit. Rev 2026-09-16: WS3 v1 plugin built + live-tested and FAILED three ways (build 1 dlopen load-fail "snd_dlsym_start"; build 2 dump-fast ~0.024 s / 0.62x; build 3 wall-clock pacing bug -> 9-min solid-buzzer incident, aplay hot-spin 190% CPU, engine held one sample). Root cause: pointer() as sole backpressure with no bounded wait and no hard failure. Redesign (gated send-pacer, real drain, stop-closes, starve watchdog, offline pacing self-test BEFORE any pin) proposed after a reference-grounding audit of the 1.2.14 spin path + aplay loop — see journal 2026-09-16-hat-plugin-builds-audit-and-redesign. Rev 2026-09-16 (later): 3.1r.2 DONE + committed `a3d8f32` — offline pacing self-test GREEN (6 consecutive full-suite runs; the v1 defects pinned RED, the v2 gated model incl. the arc's superset proved green); the self-test's in-code comments now double as the spec for what the product plugin must mirror. PAUSED 2026-09-16: 3.1r.4 live batch gate (a) GREEN, gate (b) RED (client-side wait stall at the 22 050 Hz resampled rate — the run3/run4 "client wrote / engine never read it" contradiction stands; feeder-utime discriminator run5 pending); a mid-investigation reboot wiped the /tmp toolchain, re-reconstructed from the session transcript — journals 2026-09-16-gate-b-red-client-wait-hang + 2026-09-16-live-batch-paused-reboot-and-recovery.
status: future
created_at: 2026-09-15-13-17-28
revised: 2026-09-16
---

Key: `[ ]` pending task, `[x]` completed task, `[?]` needs validation, `[-]` closed task

# Make the HAT speaker play "like a normal sound device" + TTS

Repo goal: the deck's speaker becomes a **normal userspace audio device** —
`aplay x.wav`, `espeak-ng "hello"`, `espeak-ng --stdout "x" | aplay -` — with
no stop/start dance, no EBUSY, no root app requirement. TTS lands as
**espeak-ng** (fast, ~MB-scale, no daemon, ALSA-native output; the classic
eSpeak's modern successor — Festival considered and rejected: far heavier,
slow per-process start, wrong trade for a 1 GB / 800 MHz-class A733 board).

## Scope authorization (supersedes period constraints)

- The journal "Do NOT: no audio features beyond ONE feeder/underrun tweak"
  applied to the pre-fold-in tuning period. This plan is **new
  operator-authorized scope** (TTS + normal-device integration, requested
  2026-09-15). Approval of this plan = approval of this scope.
- The tick path stays **FROZEN and reused byte-for-byte** (grid 192 kHz,
  OS=4, SD_FS 8192, signed re-anchor, wait_deadline, RT setup). The engine's
  ONLY change is the feeder's input source (file fd → socket fd) plus a
  between-jobs loop that keeps the pin held for process lifetime.
- No disassembly, no new I2S/DT-overlay work, USER runs all sudo, commits
  as CJ Trowbridge. Push: originally "NEVER push" (local-only during the
  bring-up period); **pushing to origin is operator-approved 2026-09-16**
  (record: the "commit everything and push" instruction; main was 11 commits
  / 0 behind → fast-forward push). Mandate stands: every `setup.sh`/
  provisioning change is documented in `docs/setup.md` in the same change
  and passes live `apply -> verify` before commit. (Note: the /tmp
  build-staging constraint was relaxed for this arc — the pre-live .so
  staging dir `/tmp/hatplug` is the documented scratch path for 3.1r.3.)

## Design decisions

1. **Two-plane split (root constraint).** `/dev/gpiochip0` is
   `crw------- root:root`; an ALSA PCM plugin loads into *every client
   process* (running as `cj`), so the plugin can never hold the pin.
   Therefore: (a) the **engine** = root process in the existing
   `gamepi-sound` unit: acquires the pin at start (held until shutdown,
   LOW between jobs), runs the FROZEN tick/modulator, feeds it from a
   **unix socket**; (b) the **device** = a thin ALSA `.so` plugin
   (`type hat`, any user): `connect()` to the socket, convert
   (resample → 48 kHz, stereo → mono), `snd_pcm_writei` → blocking
   `send`. EBUSY goes away (single pin owner, always the service); the
   stop/start dance goes away (clients never touch the pin).
2. **Engine input contract unchanged: raw s16le 48 kHz mono.** The v3.5
   inline file-feeder logic generalizes to a socket fd with no ring/modulator
   change. Concurrency = **serialize-on-accept** (one active client;
   additional clients block in `accept` — pulse-style queueing, documented).
   Back-pressure = kernel socket buffer + the existing ring pacing.
3. **Engine is the v3.6 mode of the SAME tracked binary**
   (`tools/hat-sound.c`): new `-d SOCKET` daemon mode; `-i` (pin-only idle
   hold) stays as the rescue/manual-play path. Unit `ExecStart` flips to
   `-d /run/gamepi-sound.sock` + `RuntimeDirectory=gamepi-sound`; socket
   bound 0666 (clients are any user; engine stays root).
4. **Client-side glue is where all "normalization" happens** (in the
   plugin): linear resample of any mono rate → 48 000; stereo → mono
   downmix (L+R)/2; other channel layouts rejected. ~150–250 lines, no
   RT, no GPIO — the plugin must never become the engine.
5. **Alsa config = one managed file**: `/etc/asound.conf` with
   `pcm.hat` + `pcm.!default → hat` (+ matching `ctl`). The board's only
   real card (allwinnerhdmi) stays reachable explicitly; the deck speaker
   IS the default output. Plugin build is board-side at setup time (same
   ritual as hat-sound: `gcc -shared -fPIC … -lasound` → alsa plugin dir,
   compare-then-install by bytes; a plugin .so change needs NO
   daemon-reload and NO engine restart — clients pick it up in new
   processes, documented).
6. **Rollback = one `git revert` + one setup apply per WS commit.** All
   artifacts are new files or additive config lines with one documented
   exception: `/etc/asound.conf` removal is manual (`rm`); after removal
   alsa falls back to the stock `default` (= hdmi card).
7. **Fallback if custom plugin loading proves fussy on the board** (symbol
   naming / plugin dir quirks): ALSA `shm` protocol device is the
   alternative shape; WS2 (the engine + socket) is valuable and
   independent of the plugin — it already gives a working device for any
   language (python `socket` clients during debug).
8. **Machine-verify vs operator-verify**: exit codes, `aplay -L` listing,
   and a 0.2 s 440 Hz self-test `aplay -D hat` are machine-verifiable
   (verify rows); hearing a human voice through the HAT amp is an
   operator gate per WS (one question each).

## WS1 — TTS gate: real speech through the speaker (no repo change yet)

- [x] 1.1 Operator: `sudo apt-get install -y espeak-ng`.
      DONE: already installed — `espeak-ng --version` = eSpeak NG 1.52.0, `/usr/bin/espeak-ng`,
      data at `/usr/lib/aarch64-linux-gnu/espeak-ng-data`.
- [x] 1.2 Operator: synthesize + convert + play using the stop/start dance
      (v3.5 unit still active). DONE 2026-09-15 with `espeak-ng -a 120`:
      `wav2s16: 68241 frames @ 22050 Hz mono -> 148552 samples @ 48000 Hz
      mono (3.09s)`; hat-sound self-report:
      `EOF after 297104 source bytes ... consumed 297104 B (148552 samples,
      47990.9 Hz, 1.00x real-time), underruns 1 ... pin released LOW`.
- [ ] 1.3 Agent writes `tools/wav2s16.py` (tracked, stdlib-only
      `wave`+`array`): mono/stereo 16-bit WAV any rate → raw s16le 48 kHz
      mono (linear resample, (L+R)/2 downmix), prints a one-line summary
      (in samples @ rate → out samples @ 48000, seconds). `python3 -m
      py_compile` + one local run on a generated WAV.
      WRITTEN 2026-09-15: `tools/wav2s16.py` (stdlib wave+array; mono or
      stereo 16-bit any rate; native LE is aarch64=s16le; one-line summary).
- [x] 1.4 Agent reads the on-screen self-report from 1.2: 148552 samples
      @ 47990.9 Hz = 1.00x real-time; underruns = 1 of ~588k ticks (~50 µs,
      below audible-click threshold, no x10ms report). Clock/RT healthy
      under continuous full-band speech.
- [x] 1.5 Operator gate PASSED 2026-09-15: voice clean at `-a 120`
      (operator "ok continue"; no level change requested). WS1 CLOSED → WS2.

## WS2 — v3.6 engine: socket daemon mode (first setup.sh change of the plan)

- [x] 2.1 `tools/hat-sound.c` v3.6: new `-d SOCKET` daemon mode —
      bind/connect the socket (0666, AF_UNIX), `accept` loop; per job:
      feeder reads s16le 48 kHz mono from the client fd (same inline-
      read pacing as file mode), EOF → drain → stats line (stderr →
      journal) → back to accept; pin acquired ONCE at start, NOT released
      between jobs, released LOW only on SIGTERM/exit. `-i` unchanged.
      `-d` + any source/tone arg → usage error rc 2. Version strings v3.6,
      header note. Build zero warnings; local md5 recorded; NEVER hand an
      unrebuilt binary.
      DONE 2026-09-15: compiled `gcc -O2 -Wall -Wextra … -lm -lpthread`
      zero warnings → scratch `/home/cj/hat-sound-v36-check` (first md5
      `db3dfb0da675eaa5b68fad2e4d149717`, 982 lines src). Daemon block,
      per-job feeder (CFS re-drop at entry), pin acquired once, `-i`
      rescue path intact; `-d` + source/tone → rc 2; `--duration SEC`
      replaces old `-d SEC`. Addendum 13:48: `socket_bind` gained
      `chmod(path, 0666)` (live EACCES — bare `fchmod(fd)` never moves
      the path-entry mode on this vendor kernel; isolated C test) →
      md5 now `33f73667ab12ee866d9745eb04afcffe`, re-verified zero
      warnings.
- [x] 2.2 `setup.sh`: unit `ExecStart=/usr/local/bin/hat-sound -d
      /run/gamepi-sound.sock`; the 0666 perms are set ENGINE-side
      (`fchmod(fd)` + `chmod(path)` — the kernel-quirk addendum in 2.1),
      so NO `RuntimeDirectory` (the socket sits directly in tmpfs /run;
      the engine unlinks it on clean exit and re-unlinks a stale one at
      bind). New verify row `sound-socket` (tmpfs → absent when the
      daemon is stopped, so the row only passes with the daemon live).
      `docs/setup.md` SAME change: unit row, `/run/gamepi-sound.sock`
      lifecycle row, "## Sound (v3.6)" re-write (stop/start dance
      retired, kept as fallback only), verify-matrix row 12.
      DONE 2026-09-15: unit flip + row + docs in one change; `bash -n`
      OK; plan-mode unit-drift reporting is automatic (`write_if_changed`).
- [x] 2.3 Local socket smoke test (no board): run `hat-sound -d
      /tmp/…sock` is NOT allowed (pin needs the board) → local check =
      compile + `--help` + dry parse only; board behavior is the gate.
      DONE 2026-09-15 (re-run on the chmod-fixed md5): `--help` →
      usage rc 2; `-d … -F -` → "daemon mode; takes no source or tone
      args" rc 2; `-t 440 --duration 5` parses through to the expected
      `open /dev/gpiochip0: Permission denied` (the board DOES have
      gpiochip0 — it is root-gated); non-root daemon run also proved the
      pin-fail path removes the just-bound socket (no stale file left).
- [x] 2.4 Live gate (operator): first apply rewrite binary+unit (drift:2),
      second apply zero writes, `sound-socket` verify row present and PASS.
      Operator runs the 0.5 s 440 Hz python client; journal self-report
      shows `job (v3.6 daemon) … 24000 samples, 47873.9 Hz, 1.00x
      real-time, underruns 51`; `srw-rw-rw- root:root` on the socket.
      Operator confirms clean tone ("clean 440 Hz tone").
      DONE 2026-09-15: both applies, self-report, operator gate all
      passed. Addendum: first stop of the daemon hit the 90 s
      TimeoutStopSec and was SIGKILLed (glibc's plain signal() implies
      SA_RESTART — the interrupted accept() was silently resumed) — fixed
      with sigaction(SA_NODEFER, WITHOUT SA_RESTART) before commit;
      re-applied binary+unit, second apply zero-writes CONVERGED.
- [x] 2.5 Operator gate (ONE question): clean tone from the socket
      client → approve WS3. (Same audibility check as 1.5 but through the
      new engine path — the FROZEN tick path is exercised unchanged.)
      DONE 2026-09-15: operator confirmed clean 440 Hz tone from the
      python client; WS2 closed. Addendum: first daemon stop hit the
      90 s timeout (glibc signal() implies SA_RESTART); fixed with
      sigaction(SA_NODEFER, without SA_RESTART); re-deployed, second
      apply CONVERGED.

## WS3 — the normal device: ALSA `hat` plugin + espeak-ng native output

- [?] 3.1 `tools/hat_alsa_plugin.c`: ALSA PCM plugin `.so`. WRITTEN,
      BUILT, and LIVE-TESTED 2026-09-16 (593 lines, untracked). The v1
      design premise FAILED three ways in sequence (journal
      `2026-09-16-hat-plugin-builds-audit-and-redesign`):
      - BUILD 1 (no `-DPIC`): dlopen (RTLD_NOW) REJECTED — undefined
        `snd_dlsym_start`. `SND_DLSYM_BUILD_VERSION` without `-DPIC`
        emits a constructor referencing that global, which the board's
        PIC libasound does not export. Fixed with `-DPIC`.
      - BUILD 2 (`-DPIC`; installed md5 `728c24565f58ca4b8371b6aac978faad`,
        01:05): LOADED and drove the pin, but DUMPED FAST — a 2.445 s
        file in 0.024 s; engine self-report `underruns 10953`, `0.62x`
        effective / `29566 Hz` (38 % of ticks starved). Cause:
        `hat_pointer()` returned `st->sent` (= accepted ~ appl) →
        `avail ≡ buffer_size` always → the client never waits → the
        whole file front-loads into the socket and the engine races to
        drain.
      - BUILD 3 (wall-clock consumption model; staged + repo-root
        md5 `33037e8eec854c58b06392fddc251605`, 01:08): the pacing
        BUG. Fixed-point `mass`/`drain` under-reports consumption
        (carry in the CONSUMED compute — exact line pinned numerically
        by the 3.1r.2 self-test, not by inspection), so every live job
        stopped advancing at ~13–15 % of file → aplay's blocking
        underrun path = -EPIPE instantly and forever → aplay hot-spun
        at 190 % CPU in the EPIPE→recover→retry loop with NO timeout
        for ~9 min, holding the socket open, while the FROZEN engine
        fed the tick from an empty ring where `sample_next()` HOLDS THE
        LAST SAMPLE (`tools/hat-sound.c:303-315`) → the sigma-delta
        collapsed to a solid tone at that sample's duty
        (`duty=57 %`, 25.5M underruns / 531.8 s) — the ~9-min buzzer,
        ended only by killing aplay (socket close → engine EOF).
      Root causes (mine, except the engine amplifier): (1) the
      arithmetic under-report; (2) the DESIGN — `pointer()` was the
      SOLE backpressure with no bounded wait and no hard failure, so
      any estimate error became an unbounded held-sample tone; (3) the
      procedure — pacing was validated live, not offline. The wall-clock
      CONSUMPTION model is the wrong design even when correct: it
      estimates engine pull from the socket (which never reports it),
      puts steady-state latency at the full ~64 KiB send queue
      (~0.5–0.77 s), and makes starve/stop invisible. FLAGGED:
      superseded by the WS3-revision below (gated send-pacer, real
      drain, stop-closes, starve watchdog, offline self-test); do NOT
      wire v1 — and DO NOT install build 3 (md5 33037e8e…); the board
      currently STILL LOADS the stale BUILD 2 (md5 728c2456…). Original
      spec kept for reference: canonical `libasound_module_pcm_hat`
      naming + the RESOLVED load contract — dlopen target
      `_snd_pcm_hat_open` (T) + version marker
      `__snd_pcm_hat_open_dlsym_pcm_001` (B), build flag `-DPIC` (see
      BUILD 1); the "first board-verified unknown" is RESOLVED.
      open (AF_UNIX connect to `/run/gamepi-sound.sock`, small retry),
      info (S16_LE, 1ch, 48000, period 1024, buffer 4096), hw_params
      (fixed caps), writei/write (resample → 48k, downmix → mono,
      blocking send, EPIPE/EOF → -EIO), close (shutdown SHUT_WR →
      engine sees EOF), prepare/rewind/drop no-op, hw_free, poll
      (socket), links (self). No pin, no RT, no engine code.
- [?] 3.2 Board v2-load check (REDUCED): the unknowns this step was
      written to discriminate are ALREADY RESOLVED on-board (journal):
      build 2 (md5 728c2456…) loaded from the stock plugin dir and
      drove the pin, `aplay -L` listed `hat` from the 5-line
      `/etc/asound.conf`, and the AF_UNIX connect path ran on every
      live test. 3.2 is now just: after 3.1r.3, confirm the v2 .so
      loads, `aplay -L` lists `hat`, and a 0.2 s 440 Hz plays — i.e. the
      pre-check for 3.1r.4(a). Subsumed into that batch; no separate
      operator ask expected. Load debug lever if needed:
      `LD_DEBUG=libs` / `ALSA_CONFIG_DIR` pointed at a scratch
      alsa.conf.
- [ ] 3.3 `setup.sh`: second build step (compare-bytes → install into the
      alsa plugin dir, path derived from `pkg-config --variable=dir` with
      fallbacks `/usr/lib/alsa-lib`, `/usr/lib/x86_64-linux-gnu/alsa-
      lib/`); managed file `/etc/asound.conf` (`pcm.hat` +
      `pcm.!default → hat` + `ctl.!default` passthrough to card 0,
      keep-hdmi-reachable comment); apt list gains `espeak-ng`,
      `alsa-utils` (aplay), `libasound2-dev` (board build dep);
      preflight gains `tools/hat_alsa_plugin.c` presence.
- [ ] 3.4 New verify rows (machine-verifiable; rows 12–14):
      `tts-engine` (`command -v espeak-ng`), `hat-device` (`aplay -L`
      lists `hat` and `default`), `tts-selftest` (generate 0.2 s 440 Hz
      WAV via python, `wav2s16.py`, `aplay -D hat -q` exit 0; then
      `espeak-ng --stdout "test" | aplay -D hat -` exit 0 — the self-test
      makes a short audible blip: documented expectation in the docs row).
- [ ] 3.5 `docs/setup.md` SAME change: managed-scope rows (plugin .so,
      `/etc/asound.conf`, apt list), verify rows 12–14, exit/none new,
      rollback note (`/etc/asound.conf` removal manual; plugin change
      visible to new client processes only), `## Sound (v3.6 + TTS)`
      section final form: the three playback verbs + espeak-ng native
      output check (if espeak-ng's default ALSA output lands on
      `default` → bare `espeak-ng "hello"` speaks the deck; if its build
      lacks it, the documented verb is `espeak-ng --stdout | aplay -`).
- [ ] 3.6 Live gate (operator): `--plan` → apply → apply (expect: units
      untouched, plugin installed, asound.conf written, all verify rows
      PASS incl. 12–14, zero-write second run). THEN the moment:
      **operator runs bare `espeak-ng "Hello"` (or the stdout-pipe verb)
      and hears it** — ONE question: clean speech, no cracks? Commit
      (mandate satisfied by this apply→verify).

## WS3-revision — plugin pacing redesign (PROPOSED 2026-09-16, pending operator approval)

Trigger: plan-playbook step 6 — the 3.1 premise (thin pipe +
wall-clock pointer is enough) failed THREE live builds, culminating in
the 2026-09-16 incident: build 3's under-reporting `mass`/`drain`
model made aplay hot-spin (190 % CPU) on perpetual -EPIPE for ~9 min
while the engine held one sample at `duty=57 %` — and the reference
audit of the pinned 1.2.14 spin path + aplay loop then showed the
defect is the DESIGN (pointer as sole backpressure, no hard failure),
not just the arithmetic (journal
`2026-09-16-hat-plugin-builds-audit-and-redesign`, permanent record of
the full arc: all three builds, the incident telemetry, root causes).
On approval, 3.1r SUPERSEDES 3.1 (v1 source untracked; build 3's
staged md5 33037e8e… must be retired, not installed) and 3.2..3.6
continue except 3.2 is reduced (its unknowns are resolved) and 3.4's
rows test the final .so. Design is grounded on the pinned 1.2.14 spin
path — the plugin's `pointer()` IS the hardware pointer (hwsync →
hw_ptr_update), the wait gate is pure `avail < avail_min`, and the
wait is a pass-through poll of the plugin's `poll_fd`.

- [ ] 3.1r.1 `tools/hat_alsa_plugin.c` REDESIGN (v2):
  - **pointer** = gated send-side accounting: 48k frames ADVANCED
    (accepted) are "consumed" at gate_time = send_time +
    backlog_bytes/48 kHz (monotone; never exceeds accepted; -EIO on
    transport death). No fixed-point carry in the hot path (that was
    build 3's bug).
  - **send gate**: before each send, if backlog > 0 and
    (now-send_time)*48k/2 < backlog → `poll(POLLOUT, period_ms+200)` —
    wakes at the same instant the framework's own wait would, so the
    client only ever sees short writes exactly as aplay expects
    (short/EAGAIN → `snd_pcm_wait(100)`), and `pointer` advances in
    step with actual consumption.
  - **SO_SNDBUF ≈ 16–32 KiB** (cap the worst-case backlog at ~250–410
    ms instead of 667) — constant documented, tuned once live if needed.
  - **real drain**: `drain` callback = blocking loop until
    account says fully consumed (or 2× the remaining drain time), so
    `snd_pcm_drain` (aplay's EOF path) waits for the actual tail instead
    of returning when the model first says "empty"; framework drop →
    `stop` → socket close stays the engine's EOF signal.
  - **stop-closes** (hardening): `stop` closes the socket (fresh job on
    every `start`); removes any last-hw/state residue between streams.
  - **starve watchdog**: engine accepts but does not consume for > X ms
    (X ≈ 5 × period, tunable) → `dead` = 1 → `pointer` = -EIO → clean
    XRUN → aplay `snd_pcm_prepare` → lazy reconnect (fresh job). No
    more silent stalls.
  - Reuse from v1: resampler (identity/up/down, cross-chunk carry),
    downmix, param lists, `prepare` reset, `close` frees, `dump`,
    connect/lazy-connect idempotency, `reinit_status` at connect/close.
  - Build zero warnings; md5 recorded; stage to /tmp/hatplug (scratch).
- [x] 3.1r.2 `tools/hat_pace_selftest.c` (tracked; commit `a3d8f32`,
  2026-09-16, ahead 1, NOT pushed): deterministic pacing check with NO
  GPIO and NO engine — a fake "engine" daemon (NOT a bare thread: the
  REAL serial job machine — one accept at a time, job = client
  EOF + full ring drain, feeder thread + 192 kHz spin tick loop, fixed
  8192 B ring that sleeps when full, hold-last-sample + underrun++ on
  empty tick) plus two self-contained v1/v2 snd_pcm-ioplug-shaped
  client models driven by a faithful aplay harness (blocking send on
  -EPIPE prepare+retry; 100 ms poll retry on -EAGAIN; no client-side
  sleep — the socket IS the pacing, in both models). Shipped richer
  than the paper spec; every check asserts a number, not a name, and
  the RED checks assert the v1 violation PERSISTS (a green run keeps
  pinning the defect): T1 v1-healthy → early done-release pinned
  (claim 88,200 client frames at close vs live engine counter
  ~70.5k; tail 19,206–19,261 wire samples ≈ 400 ms still owed; no
  truncation — engine plays on, drain is the product no-op per
  pcm_ioplug.c:533-578); T2/T5 v2-healthy clean (T5: 0.3 s file =
  exactly 14,399 wire samples in/out — resampler pinned; 28,798 B
  exact); T3/T6b v2 stall (paused feeder, tick keeps running) → XRUN +
  recover, detect ~1.70 s via the POINTER-STALL path (front freezes
  ~+85 ms post-pause + 600 ms watchdog + 100 ms poll grain); T4 v2 kill →
  XRUN + lazy reconnect, detect ~1.00 s via the TRANSPORT path (die-
  close EPIPEs the in-flight send); T6a v1 stall → silent 915 ms
  (~44k-tick) DC-buzz ride-through + deterministic full-second over-claim
  at close (43,776 wire = 912 ms, identical every run: close lands
  mid-freeze, owed = 1000 ms − 85 ms ring chew) — the socket's ~2.1 s
  ceiling (sndbuf 131,072 B + rcvbuf 65,536 B + ring) absorbs the whole
  1 s stall, so T6a push == T1 push == 1.60 s: the stall's cost is
  engine-side buzz + over-claim, NOT client wall time; xruns ∈ [1,2] in
  the kill/stall scenarios (serial engine accept-window hand-off: a
  fresh connection reads the dying job's residual ladder/evidence —
  logged `end_in=-2102 ms` — and the stall clock fires once more).
  FIRST JOB met: build-3's `mass`/`drain` under-report is the T1/T6a
  RED machinery verbatim — the harness catches it. 6 consecutive
  full-suite runs green (28 checks), `-Wall -Wextra` clean, zero
  warnings; kept all state-change debug lines (`/tmp/hps_dbg.log`),
  stripped all rate probes. Engine fidelity note is baked in (FEED-
  ONLY stall; no tick-clock freeze, no re-anchor — the old test
  variant froze both clocks and hid the signal). The self-test's
  in-code comments are now the spec the 3.1r.3 product plugin must
  mirror (gated send + 600 ms gate watchdog, pointer-side stall →
  -EIO, evidence cap, ring-margin drain, monotone pointer, no op
  drain, stop-closes, 19-bit fixed point PITCH = 48000*524288/44100,
  wire→client 147/160). Operator ask raised in the commit message:
  the compiled binary `tools/hat_pace_selftest` (the binary WITHOUT
  `.c`) is untracked and not gitignored — decide ignore/delete/keep
  before the next commit.
  - [x] 3.1r.3 DONE 2026-09-16 (code-complete + verified offline; journal
  2026-09-16-hat-plugin-v2-complete): `tools/hat_alsa_plugin.c` rewritten
  to the v2 pacing model (dry-only re-anchor, D-transfer-err -EPIPE
  contract, D-ptrreset prepare stall-clear, D-drain-dead). Zero-warning
  build; re-staged /tmp/hatplug/libasound_module_pcm_hat.so md5
  `4b5898126e7219b589ce7d1069df01f3`; `nm -D` shows exactly `_snd_pcm_hat_open`
  (T) + `__snd_pcm_hat_open_dlsym_pcm_001` (B), NO `snd_dlsym_start`
  (also probe-verified via dlopen+dlsym); scratch load under
  ALSA_CONFIG_DIR=/tmp/hatscratch + ALSA_PLUGIN_DIR=/tmp/hatplug:
  `aplay -L` lists `hat`, `timeout 10 aplay -D hat -l` exit 0. Build-3
  artifact (md5 33037e8e…) RETIRED (repo-root copy deleted; it was never
  installed — board still runs build-2 `728c2456…` until the live batch
  is green).
- [ ] 3.1r.4 Timeout-gated live batch for the operator (ONE ask,
  ~3 min cap each, auto-Ctrl-C semantics documented in the batch):
  (a) `aplay -D hat` ~2 s generated tone → elapsed ≈ 2 s (the pacing
  gate — build 2 failed it by dumping in 0.024 s; build 3 failed it by
  stalling at ~15 % → the 9-min buzzer); (b) espeak-ng sentence → gap
  between sentences ≈ voice-end-to-start (no ~3 s dead air); (c) kill
  the
  daemon mid-play (sudo in the batch) → aplay recovers to XRUN/reconnect
  OR exits cleanly with no hang; daemon restarted by systemd. Gated: if
  (a) is off by >±15% we stop and re-tune the gate before (b)/(c).
  ONE question on pass: clean, on-time speech?
  LIVE RESULTS 2026-09-16 (see journal 2026-09-16-gate-b-red-client-wait-hang +
  2026-09-16-live-batch-paused-reboot-and-recovery): (a) GREEN — 48 kHz 2 s
  tone → 2.211 s wall (+10.6%, within ±15%). (b) RED — espeak 22 050 Hz
  sentence hit the full 10 s cap; engine consumed ~73 ms of audio (single
  accept, no EPIPE, no 2nd job, no watchdog fire) → client parked in its wait
  loop ~9.9 s while the socket was writable and the pointer fully advanced.
  Deterministically reproduced in run3/run4 (engine read exactly 1512 B
  while the harness's own write loop returned 74×32 frames, dt<0.03 ms each,
  and ss showed the TX queue at zero for 4+ s — an unsatisfied
  contradiction; prime suspect the framework wait path at the resampled
  rate). Operator rebooted mid-investigation → all /tmp toolchain + run
  logs wiped; harness + monitor4 re-reconstructed from the session
  transcript (zero-warning; md5s in the pause journal). (c) NOT STARTED.
  PLAN PAUSED 2026-09-16 at the operator's request — status back to `future`
  (the framework lifecycle has no "paused"; a paused plan is a queued one),
  file moved to plans/future/. Resumption = the run5 feeder-utime discriminator
  (script + command in the pause journal's resumption checklist), then
  classify-and-fix per the gate-b journal's next-steps list.
- On approval: 3.2 is now a trivial v2-load check (symbol naming /
  plugin dir / connect path are already resolved on-board by builds
  2/3 — see journal) and subsumes into the 3.1r.4 batch as its
  pre-check; 3.3/3.4/3.5 proceed as written (setup.sh installs the v2
  source — 3.3 preflight checks `tools/hat_alsa_plugin.c`; nothing else
  changes); 3.6 gate unchanged.

## WS4 — polish + closeout

- [ ] 4.1 `docs/setup.md` + journal: final state block (device verbs,
      espeak voice list hint `espeak-ng -v …`, engine stats in journal,
      multi-client serialization behavior, fallback path 3.2 if the
      plugin was the `shm` alternative).
- [ ] 4.2 Optional (only if operator wants one more verb): tracked
      `tools/hat-say` wrapper (`espeak-ng --stdout "$@" | aplay -D hat -`
      or the native-output equivalent) installed by the same
      compare-then-write ritual; otherwise `[-]` closed.
- [ ] 4.3 Session memory: device architecture line (socket contract
      `s16le 48k mono → /run/gamepi-sound.sock`; plugin = thin pipe;
      engine = v3.6 `-d`; rollback notes).
- [ ] 4.4 Index `--check`, move this plan `future → current` at first WS2
      edit (WS1 makes no repo edit except 1.3 → promote at 1.3),
      `current → past` when all items `[x]`/`[-]`.

## Failure modes (known, with the in-plan response)

- Plugin symbol naming / plugin dir lookup → RESOLVED on-board by this
  arc (journal): the load target is `_snd_pcm_hat_open` (+ version
  marker); the build MUST use `-DPIC` (without it, the
  `SND_DLSYM_BUILD_VERSION` macro emits a constructor referencing
  `snd_dlsym_start`, unexported by the board's PIC libasound →
  RTLD_NOW load fails — that was BUILD 1). Plugin dir is the stock
  `/usr/lib/aarch64-linux-gnu/alsa-lib` (build 2 loaded + listed
  there). Fallback = ALSA `shm` protocol (decision 7) still stands.
- Stuck / confused client holding the pin open (THE incident) → a
  client that stops advancing while the connection stays open makes the
  engine feed the tick loop from an empty ring, where `sample_next()`
  HOLDS THE LAST SAMPLE (`tools/hat-sound.c:303-315`) and the
  sigma-delta collapses to a solid tone at that sample's duty (the
  ~9-min `duty=57 %` buzzer; only a socket close = engine EOF stops it).
  Frozen engine behavior; in-plan response = the v2 **starve watchdog**
  (engine accepts but consumes nothing > 5×period → `dead` → `pointer`
  = -EIO → clean XRUN → aplay prepare/reconnect) + **stop-closes** +
  the **real drain**. Future optional (WS4, not this revision): a
  user-run "socket guardian" that closes client connections idle
  > N s — no engine/wire/tick change.
- No-unbounded-live-run discipline (the procedural fix for the
  incident): every live `aplay` wrapped in `timeout`, engine
  `underruns` + `pgrep aplay` + the socket checked within seconds of
  each test, any stall → immediate kill + journal before retry, and the
  offline self-test (3.1r.2) explains any P0 before a live rerun.
  Never again an unbounded `time aplay` against the pin.
- Client dies mid-stream → engine: `send`/`recv` EPIPE/ECONNRESET and
  read-EOF treated as job end (pin parked LOW, next accept); `aplay`
  itself exits on engine death; `Restart=always` covers engine crashes.
- espeak rate 22.05 kHz stereo-ish quirks / `--stdout` format drift →
  converter + plugin handle any mono rate and stereo; 3.4's
  `espeak --stdout | aplay` row is the live format check.
- RT: the engine daemon is SCHED_FIFO 98 for process lifetime —
  acceptable because the RT budget is already lifted (drop-in, -1) and
  the machine is dedicated; documented in `## Sound`.
- `/etc/asound.conf` + `!default → hat` hides the hdmi card from
  `default` (by design: the deck speaker is the output); hdmi stays
  reachable as an explicit device (`aplay -D default|hdmi …` via `pcm.hat`
  sibling entry if needed — add only if an app breaks).

## Rollback per commit

WS1: nothing tracked (only `wav2s16.py`, keep or revert).
WS2: revert commit + one apply restores `-i` ExecStart (unit converges).
WS3: revert + apply removes plugin/asound.conf (the revert's setup.sh no
longer manages them — one manual `rm /etc/asound.conf` + `rm` of the .so
per the docs rollback note) and verify rows. Board state after rollback
= exactly today's converged state. WS3-revision: nothing installed on
the board yet (v1/v2 .so are untracked scratch in /tmp/hatplug + the
repo root until 3.3 lands) → rollback = delete the untracked files;
if the revision is rejected, 3.1 stays `[?]` and the journal records
the deferral (no engine, no setup.sh, no board change in the interim).