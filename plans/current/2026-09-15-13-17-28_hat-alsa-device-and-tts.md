---
plan_id: 2026-09-15-13-17-28_hat-alsa-device-and-tts
title: Make the HAT speaker play "like a normal sound device" (ALSA jury-rig) + fast TTS (espeak-ng)
summary: Prove the speaker with real speech (espeak-ng, one-time gate), then integrate a root engine daemon (existing gamepi-sound unit, FROZEN tick path untouched) behind a thin user-space ALSA PCM plugin over a unix socket, so aplay / espeak-ng "text" work with no flags and no service stop/start; every setup.sh change documented in docs/setup.md in the same change and live apply->verify before each commit.
status: current
created_at: 2026-09-15-13-17-28
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
- No disassembly, no new I2S/DT-overlay work, no /tmp staging, USER runs all
  sudo, commits as CJ Trowbridge, NEVER push. Mandate stands: every
  `setup.sh`/provisioning change is documented in `docs/setup.md` in the same
  change and passes live `apply -> verify` before commit.

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

- [ ] 3.1 `tools/hat_alsa_plugin.c`: ALSA PCM plugin `.so` (canonical
      `libasound_module_pcm_hat` naming + `_snd_pcm_create_hat`… per the
      alsa-plugin howto; symbol prefix is the FIRST board-verified
      unknown — 3.2's tiny client test is the discriminator):
      ops = open (AF_UNIX connect to `/run/gamepi-sound.sock`, small
      retry), info (S16_LE, 1 channel, 48000, period 1024, buffer 4096),
      hw_params (fixed caps), writei/write (convert: linear resample →
      48k, downmix → mono, blocking send with EPIPE/EOF → -EIO), close
      (shutdown SHUT_WR → engine sees EOF), prepare/rewind/drop (no-op:
      single-consumer stream), hw_free, poll (socket readable),
      links (self). No pin, no RT, no engine code.
- [ ] 3.2 Board probe (operator, one batch): build the .so straight from
      the repo source into a scratch dir, drop a minimal
      `pcm.hat { type hat; }` config, and run the smallest possible
      client (`aplay -D hat -` fed 0.2 s 440 Hz) — verifies symbol naming,
      plugin dir lookup, connect path before any setup.sh wiring. Debug
      lever if load fails: run the client with `LD_DEBUG=libs` /
      `ALSA_CONFIG_DIR` pointed at the scratch alsa.conf.
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

- Plugin symbol naming / plugin dir lookup → 3.2's scratch build +
  `LD_DEBUG=libs` probe BEFORE any setup wiring; fallback = ALSA `shm`
  protocol (decision 7); engine/socket (WS2) remains fully useful alone.
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
= exactly today's converged state.