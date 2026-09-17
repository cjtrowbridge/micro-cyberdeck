# 2026-09-16 — Live batch PAUSED at 3.1r.4 gate (b) RED; mid-session reboot;
# diagnostic toolchain recovered from the session transcript

Plan: `plans/future/2026-09-15-13-17-28_hat-alsa-device-and-tts.md`
(status `current` → `future`; moved to `plans/future/` this session — the
framework lifecycle has no "paused" state; the pause is recorded in the
plan's 3.1r.4 note and in this journal).
Companion journal: `2026-09-16-gate-b-red-client-wait-hang.md` (addendum
appended this session with the run3/run4 detail below).

## State at operator pause

3.1r.4 (the operator live batch) was mid-flight, PAUSED by explicit operator
request — new work takes over.

- Gate (a) GREEN (48 kHz 2 s tone → 2.211 s, +10.6%, within ±15 %).
- Gate (b) RED (espeak 22 050 Hz sentence → full 10 s cap, ~73 ms of audio
  delivered; engine `7040 B, 3520 samples, 352.3 Hz, 0.01x`, clean EOF,
  single accept — no second job, no `-EPIPE`, no watchdog fired).
- Gate (c) (kill-daemon-mid-play) NOT STARTED.
- **The standing contradiction** (run3 + run4, both deterministic at engine
  level — 1512 B): the v4 harness's write loop returned
  `xfer = 32` frames per `snd_pcm_writei` (74 writes × 32 = 2368 client
  frames claimed, each write `dt < 0.03 ms`), yet the engine read
  **exactly 1512 wire B** (756 wire samples = 347.25 client frames ≈
  15.75 ms of audio; engine line `1512 B … 33.9 Hz, 0.00× real-time,
  und=479109`), the harness froze at `avail=27 < want=32`, `remain=20307`
  (only ~2337 frames claimed of 22644 total) by ~4.6 s, and `ss -xn`
  showed the client→engine socket TX queue at **zero for the entire stall
  (4+ s)** — the bytes the client claimed were handed to the kernel are not
  in the kernel, and nothing after the one ~4 ms burst ever appeared.
  No transport failure anywhere (no EPIPE/ECONNRESET, no second accept).
- The intended discriminator for the resume: feeder-thread **utime** across
  the stall (2 ms EAGAIN tight-spin ⇒ climbing utime; 10 ms sleep ⇒ flat),
  via the engine-thread sampler (monitor4 `E` lines) which was being
  rebuilt with 20 ms iteration and an `hp`-optional gate when the session
  was cut.

## The reboot (operator action mid-session)

The device was rebooted; **all of /tmp was wiped**: the /tmp/hatdiag toolchain
(harness, scripts, run3/run4 logs), the /tmp/hatplug staging dir + the
build-2 `.so` rollback copy, test wavs, and any scratch builds.

Post-reboot audit — everything critical survived:

- git HEAD `a3d8f32` (ahead 1, unpushed); dirty: `tools/hat_alsa_plugin.c`
  (M) + 3 untracked journals (+ the untracked `tools/hat_pace_selftest`
  binary).
- Installed engine FROZEN v3.6, installed plugin v2 `.so`
  md5 `4b5898126e7219b589ce7d1069df01f3`, `/etc/asound.conf` intact,
  `gamepi-sound` unit live after boot.
- `/home/cj/hat-alsa-src/alsa-lib-1.2.14/` reference tree intact.
- The session transcript (JSONL, 12.3k lines / ~29 MB) intact — it carries
  every create/edit call made to the /tmp toolchain, so full replay is
  possible even after a wipe. **Rule going forward: reconstructable
  artifacts are fine; evidence (the run logs) is NOT reconstructable —
  keep a scratch copy under the repo when a run matters.**

## Fresh-boot smoke test (first pin traffic after reboot)

`timeout 6 aplay -D hat /tmp/a1.wav` (regenerated the 1.028 s 22 050 Hz
sentence): rc=124 (killed at 6 s cap), engine one job
`source=consumed=14080 B, 7040 samples, 1173.3 Hz, 0.02× real-time,
underruns=279844` (~99 % hold-last). 14080 B = 5 × the gate (b) amount —
the burst was ~2× longer, same signature, same engine-side conclusion:
the engine is healthy, the client-side wait path stalls at 22 050 Hz,
deterministically, across reboots. (Burst size varies run to run; the
engine-level values are the stable part.)

## Toolchain recovery (transcript replay + rebuild)

Replayed the session transcript's create/edit calls for the three
/tmp/hatdiag files and rebuilt:

- `/tmp/hatdiag/hatdiag_client_v4.c` (358 lines) — the v4 aplay-faithful
  instrumented harness (args `<wav> <log> [want_per=32] [want_buf=2048]`;
  per-write dt; wait-episode logging; self-dump on 3 s no-progress:
  wchan/state/syscall + socket fds; `timeout 10` discipline). Compiled
  zero-warning: `cc -O2 -Wall -Wextra -o hatdiag_client_v4
  hatdiag_client_v4.c -lasound` → md5 `23d1d38b3c4785871947380497a7d11a`.
- `/tmp/hatdiag/procdump.sh` (63 lines) — ~10 Hz /proc sampler: harness
  state/wchan/syscall + every socket fd → /proc/net/unix RX queue, plus
  engine-thread state/wchan/syscall. `sh -n` clean; md5
  `e4609237f5553838d87d6bc198cf8b1e`.
- `/tmp/hatdiag/monitor4.sh` (41 lines) — the engine-feed discriminator:
  per 20 ms iteration (wall-clocked, not sample-count-clocked), harness
  line (state/syscall/wchan/**utime**) + `ss -xn` socket lines + one line
  per engine thread (state/syscall/**utime**) → run5's feeder-spin vs
  sleep classification. `hp` is optional (only `ep` + output are
  required). `sh -n` clean; md5 `12307811edeea768b8dafae074df3135`.

Known gap (honest, logged): one harness edit that only existed as an
argument blob (its `oldString` targeted a `main()` variant superseded by
later edits — it was a no-op replay-wise) carried a `dump_sockets()`
helper (own-fd → peer-inode → /proc/net/unix TX/RX queue dump) that was
NOT re-applied. It is not needed for run5 (monitor4's `ss -xn` sample
covers the same evidence) but if run5 is inconclusive the function can be
re-salvaged from the transcript (`c_edit3` extraction, /tmp/tx/ if still
present, otherwise re-salvage from the JSONL).

## On-disk state on pause (resumption checklist)

Next session:

1. Re-verify (cheap, no pin traffic): `git status` matches this journal;
   `md5sum` of the installed `.so` = `4b589812…`; `ls /tmp/hatdiag` shows
   the three sources + the binary at the md5s above; `pgrep -x hat-sound`.
   **If anything drifted (especially a /tmp wipe), re-run the smoke test
   and, if needed, re-replay the transcript — the procedure above is the
   recipe.**
2. run5 (bounded 10 s, 22 050 Hz, the approved next step — an
   instrumented repetition of gate (b), not a blind retry):
   `espeak-ng -w /tmp/a1.wav "The quick brown fox jumps over the lazy dog."`
   (22 050 Hz); engine pid E=$(pgrep -x hat-sound); start
   `sh monitor4.sh E mon5.log 15 &`; then
   `timeout 10 /tmp/hatdiag/hatdiag_client_v4 /tmp/a1.wav run5.log 32 2048`;
   collect run5.log + mon5.log + journalctl; classify the stall by the
   feeder-thread utime slope across the freeze window (2 ms EAGAIN spin ⇒
   climbing; 10 ms sleep ⇒ flat) and by the TX-queue timeline.
3. Classify & fix per the gate-b journal's next-steps list (framework
   wait path the prime suspect; plugin fix only if the plugin is at fault;
   any model change → self-test gap-fill first → live batch re-run
   a → b → c).
4. Parked, unchanged: 3.1r.4 `(c)`; WS4 4.1–4.4; commit of
   `tools/hat_alsa_plugin.c` (uncommitted M) + 3 untracked journals + the
   `tools/hat_pace_selftest` binary (disposition: ignore vs keep was still
   an open operator ask); push of `a3d8f32` still requires explicit
   approval.