# 2026-09-16 — Live batch: gate (a) GREEN, gate (b) RED — client-side
# wait hang at the resampled rate

The operator-approved live batch ran today against the installed v2 plugin
(md5 `4b5898126e7219b589ce7d1069df01f3`). Results:

- **GATE (a) GREEN.** `timeout 10 aplay -D hat /tmp/hat_tone.wav` (2.0 s,
  48 000 Hz mono, native wire rate): `rc=0`, elapsed **2.211 s** (+10.6 %,
  within ±15 %). Engine job log: 192 000 B consumed @ 43 983 Hz (0.92×
  real-time), underrun ticks 8 704/105 843 (~8 %), clean EOF. The v1
  failure modes (dump-fast 0.024 s; 9-min solid buzzer) are both off the
  table.
- **GATE (b) RED.** Two espeak sentences (`espeak-ng -w` → /tmp/a1.wav
  1.028 s, /tmp/a2.wav 2.789 s — both **22 050 Hz** mono). First
  `timeout 10 aplay -D hat /tmp/a1.wav`: **hit the full 10 s cap and was
  killed (rc=124)**; the second never started. Expected wall ≈ 1.4 s.

## Engine-side evidence (authoritative, from journalctl -u gamepi-sound)

Gate (b) single job, exactly one accept for the whole 10 s:

    EOF after 7040 source bytes
    socket  t= 10.0s  ...  und=475975  duty=48%
    job (v3.6 daemon) client EOF, ring drained — source 7040 B,
      consumed 7040 B (3520 samples, 352.3 Hz, 0.01x real-time)

- 7 040 wire B = 3 520 wire samples ≈ **1 617 client frames at 22 050 Hz**
  (i.e. ~73 ms of client audio). That is **exactly one aplay period's
  worth**: aplay handed over one period and then wrote nothing for ~9.9 s.
- 3 520 slots filled, 475 975 underruns of a ~479 481-slot 10 s window:
  the engine tick loop ran fine the entire time (99.27 % hold-last) and
  drained the socket **immediately** (the feed thread is decoupled from the
  tick; it reads the socket as fast as the 8 192 B ring allows, ≤ ~96 KB/s
  per tick).
- **No second job.** A transport failure in our plugin would have
  disconnected, returned `-EPIPE` to aplay, and aplay's `snd_pcm_recover`
  → prepare → next transfer would have made a fresh `hat_connect` = a
  second engine job. There is none. Our send-side 600 ms watchdog can
  equally not explain it (it returns the same way). Our pointer-side
  watchdog also couldn't fire: after the one chunk, `consumed` reaches
  `sent` at ~73 ms → no pending schedule → healthy-immune, by design.

**Conclusion: the plugin sat silently and correctly; the client (aplay)
parked in its wait loop for ~9.9 s even though the socket became writable
and the pointer fully advanced.** This is a framework/client wait-path
anomaly at the resampled rate (22 050 ≠ 48 000 wire), NOT a transport,
pacing, or engine failure.

## Why the offline self-test did not catch this

The harness (`tools/hat_pace_selftest.c`) models the engine faithfully
(serial job machine, 8 192 B ring, RT tick, hold-last) but drives the
plugin's `pointer()` **directly** — it never exercises aplay's
wait/poll/avail loop (`snd_pcm_wait` + ioplug `avail_update` + `hwsync`
re-reading `pointer()` under the pcm lock). Its single client rate is
44 100 Hz. The broken path is precisely:
framework `hwsync` → our `pointer()` → `snd_pcm_mmap_hw_forward`
(last_hw delta) → `__snd_pcm_playback_avail` → aplay's `snd_pcm_wait`
decision — at a rate where wire:client ≠ 1:1.

Hypotheses to test (in order of likelihood, all framework-side for now):
1. `snd_pcm_wait` at `mmap_rw = 0` waits on a condition our pointer
   dynamics never satisfy (need: pinned `snd_pcm_wait` semantics +
   aplay's actual loop + negotiated buffer/period at 22 050 Hz).
2. ioplug avail/hw_ptr_update boundary math: our pointer is a pure
   function of time, not of the client's `appl_ptr`; the framework's
   `last_hw` delta tracking (pcm_ioplug.c:57–88) plus `__snd_pcm_playback
   _avail` normalization may yield a persistent "no space" to the client.
3. Pathological negotiated params at 22 050 Hz (e.g. period/buffer from
   our advertised byte ranges refined through the time↔size rules).

## State after the RED

- Installed plugin = v2 (`4b589812…`); build-2 backup preserved at
  `/tmp/hatplug/build2-backup-728c2456.so` (md5 `728c2456…`),
  reinstall-able with one `sudo install` if we want to fall back.
- Engine: healthy, single-listen state, pid 23106, untouched.
- No unbounded runs occurred; every aplay was `timeout 10`.

## Next steps (bounded, one at a time)

1. Read pinned alsa-lib 1.2.14: `snd_pcm_wait`, `__snd_pcm_playback_avail`,
   `snd_pcm_ioplug_hw_ptr_update` + tail of `avail_update` — no pin traffic.
2. Re-run ONE bounded gate (b) with `aplay -v` to capture the negotiated
   buffer/period at 22 050 Hz (timeout 10, same as before — an
   instrumented repetition of the approved gate, not a blind retry).
3. If still dark: a temporary diagnostic build of the logging variant in
   /tmp/hatdiag (transfer/pointer/send/poll traces to /tmp/hat_diag.log),
   loaded via ALSA_PLUGIN_DIR so the installed .so is never touched.
4. Fix lands in the plugin only if the plugin is at fault; if it's the
   framework wait path (likely), the fix is making our pointer dynamics
   compatible with it — model change, with in-code justification, then a
   self-test gap-fill for the missing wait-path case, then the live batch
   re-run (a → b → c).

## Addendum (same session, later) — run3/run4: the contradiction, and the
## toolchain's loss to a reboot

Run3 (v4 harness, /tmp/a1.wav, period 32 / buffer 2048) and its replay
run4 both froze the harness at `avail=27 < want=32`, `remain=20307` (~2337
client frames claimed of 22644), after ~74 successful
`snd_pcm_writei` calls (`xfer=32`, each `dt < 0.03 ms`). Engine level:
EXACTLY 1512 B in both runs — `1512 B (756 samples, 33.9 Hz, 0.00×
real-time)`, underruns 479109 — 756 wire samples = 347.25 client frames ≈
15.75 ms of audio, i.e. ~89 client B were ever fed after the initial
burst, while the client claimed 2368 frames. `ss -xn` showed the
client→engine socket TX queue at zero for the entire ~4+ s stall. Three
facts that cannot all be true without an intervening sink: (i) the
harness's write loop returned success for 74 × 32 frames, (ii) the engine
consumed exactly one burst's worth and then starved, (iii) the kernel
socket queue held nothing. Candidate sinks: a drop/absorption on the
plugin→socket path between `snd_pcm_writei` returning and `send()` (the
plugin's transfer accounting vs its actual sends), or a framework-level
retreat/rewind of the ioplug ring (consumed frames never reaching the
transfer callback) — the framework's `write_areas`/`drain` interaction at
the resampled rate is the prime suspect (wire:client ≠ 1:1).

Mid-investigation the operator rebooted the device: the entire /tmp/hatdiag
toolchain (harness, procdump.sh, monitor4.sh, run3.log, proc3.log, run4
logs) and the /tmp/hatplug staging dir (incl. the build-2 `.so` rollback
copy) were wiped; the run logs were **not** kept elsewhere, so the raw
per-line evidence does not survive — only the numbers recorded above and
in the session transcript. The v4 harness + monitor4 were reconstructed
from the transcript's edit calls and rebuilt zero-warning
(`/tmp/hatdiag/`, md5s in the pause journal
`2026-09-16-live-batch-paused-reboot-and-recovery.md`); a fresh-boot smoke
test confirmed the RED signature reproduces on a clean boot (10 s cap,
engine 14080 B consumed, 0.02× — burst size varies run to run). The
remaining discriminator (feeder-thread utime slope across the stall: 2 ms
EAGAIN-spin ⇒ climbing, 10 ms sleep ⇒ flat) is the approved next step of
this plan (3.1r.4 continuation, run5) if the work is resumed.
Process rule added: run logs for load-bearing runs get a scratch copy
under the repo (they are evidence, unlike reconstructable toolchain
sources).