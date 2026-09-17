# 2026-09-16 — Offline pacing self-test GREEN (3.1r.2 complete, commit `a3d8f32`)

Checkpoint for the WS3-revision arc: plan step 3.1r.2 (`tools/hat_pace_selftest.c`)
is DONE — built `-Wall -Wextra` clean, 28 checks, 6 consecutive full-suite runs
green, committed **source only** as `a3d8f32` (ahead 1, not pushed). No live-
board work happened in this window. The self-test's in-code comments now double
as the spec the 3.1r.3 product plugin must mirror.

## What the harness is (shipped richer than the plan's paper spec)

- Fake "engine" = the REAL v3.6 serial job machine, not a bare puller thread:
  one accept at a time; job ends on client EOF **and** full ring drain; feeder
  thread + 192 kHz spin tick loop; fixed 8192 B ring (full → sleep, never
  drops); empty tick = hold-last-sample + `underrun++` (the DC-buzz mechanism).
- Two self-contained client models with the snd_pcm_ioplug `chained`/`hw_ptr`
  shapes, driven by a faithful aplay harness (−EPIPE → prepare + retry same
  data; −EAGAIN → poll 100 ms + retry; **no client-side sleep — the socket is
  the pacing** in both v1 and v2).
- Every check asserts a number. The RED checks assert the v1 violation
  **persists**, so a green run also pins the defect in place.

## Defects pinned (RED, build-3 machinery verbatim)

- **T1 (v1, healthy):** the `mass`/`drain` chain is a pure send-side clock. At
  drain-call the pointer reads file end (88,200 client frames claimed) while
  the **live engine counter says ~70.5k** — tail 19,206–19,261 wire samples
  (≈400 ms) still owed (socket + ring). **No truncation**: the engine plays
  the whole thing after close (drain is the product no-op — alsa-lib 1.2.14
  `pcm_ioplug.c:533-578`, `drain` returning 0 → framework drop → stop →
  close; the engine's EOF-then-drain plays the remainder). The defect is the
  **early done-claim + early release**, witnessed at the close instant by a
  live tick-loop counter (the final `last_emitted` would read exactly the
  over-claimed amount — that witness bug was itself fixed this window).
- **T6a (v1, 1 s feeder pause @ t=1 s):** the identical 1.60 s push (the
  socket's ~2.1 s ceiling ≈ sndbuf 131,072 B + rcvbuf 65,536 B + ring 85 ms
  ≥ the 2.0 s file absorbs the whole stall — T6a push == T1 push by
  construction). Cost of the stall is engine-side: ring drains, tick loop
  spins **≈915 ms ≈ ~44k underrun ticks** (buzz = pause − ring chew =
  1000 − 85 ms; earlier draft said "1000 − 170 ≈ 830" — wrong), and a
  **deterministic over-claim at close: 43,776 wire samples = 912 ms, identical
  every run** (close lands mid-freeze; owed-at-close = 1 s busy window −
  85 ms chew). No error, no XRUN, no backwards pointer on the client side.

## v2 model proven (GREEN) — incl. the arc's superset the plugin must mirror

- Send-gated monotone pointer; −EIO on transport death; `poll(POLLOUT)` gate
  + **600 ms watchdog** on the transport; **pointer-side stall detection**
  (chew front frozen ≥600 ms while schedule pending → −EIO) is the operative
  detector (T3/T6b ~1.70 s), since the client pointer can gate ahead of the
  socket and blind the transport; **evidence cap** (chew front ≤ bytes
  delivered to the ring, read under the job's ring lock; NULL between jobs)
  keeps the pointer from running ahead of what the engine actually fed;
  ring-margin drain (`chewed == sent` + one RING_MS). Fixed point: LIN_SHIFT
  19, PITCH = 48000·524288/44100, wire→client 147/160; 13,230 client frames
  → **exactly** 14,399 wire (28,798 B) (T5).
- Detect-path taxonomy (both correct GREEN routes): **T4 transport path**
  (kill → die-close EPIPEs the in-flight send → detect ~1.00 s) vs
  **T3/T6b pointer-stall path** (front freezes ~+85 ms post-pause as the ring
  empties → +600 ms watchdog → +100 ms poll grain → ~1.70 s).

## Forensic conclusions carried into the code (comments)

- **Engine fidelity is FEED-ONLY.** The stall freezes only the feeder; the
  tick loop keeps running off `t0` (no pause, no re-anchor). The pre-arc test
  variant froze BOTH the feeder and the tick clock — "two clocks, one freeze
  past the wall" — which hid the underrun signal and broke the wall
  accounting. The old T6a wall band [1.8, 2.6] assumed close ≈ EOF ≈ 2.0 s;
  false (queue absorbs the stall). Deleted; replaced by the stronger
  at-close over-claim pin.
- **`xruns` ∈ [1, 2] in T3/T4/T6b:** serial-engine accept-window hand-off —
  the fresh connecting socket sits in the accept queue while the old job
  tears down; the fresh model can read the dying job's residual ladder/
  evidence (log: `v2 done? … v2f.i=2 end_in=-2102 ms`), so the 600 ms stall
  clock fires once more on the fresh job.
- **T4 last-job accounting:** die-close discards conn-1's in-kernel bytes;
  the reconnect re-sends only the unsent remainder. Kill at exactly t=1 s
  (17 periods in) leaves 3 chunks = 13,230 client frames = 14,399 wire — the
  0.3 s file's length **by chunk granularity, not by file length**; engine
  counters read there are the last job's (28,798 B / 14,399 emitted).

## Stability

6 consecutive full-suite PASS (EXIT 0 each, `rm -f /tmp/hps_dbg.log` +
confirmed scenario header before every run): T3 detect 1.70 ×6, T4 detect
1.00 ×6, T6b detect 1.70/1.71, xruns 2/2/2 every run; T1 tail ±28 samples;
T6a over-claim 43,776 every run; T2 underrun 8661/8674/8758 (drain-margin
shape); T4 total drifted 1.49→1.89 s across runs 5–6 (reconnect jitter,
within band). Kept: all state-change debug lines (471 recv>0, 471 ring full,
12 connect, 10 accept/fd-close, 7 job done, 4 underrun start, 3 feed
stalled, pointer-stall line present in the last log). Stripped: `dbgrate` +
6 rate probes + their throttle counters + `stdarg`.

## Decisions / operator asks

- **T3 detect band kept [1.5, 2.6]** (steady 1.70): widening is NOT justified
  by scheduler noise — the offline engine is deterministic; the band is the
  spec, and 1.70 ± 0.01 over 6 runs is well inside any sane band. No
  deviation taken; the model never changes to fit a budget here either.
- **Committed source only.** The compiled binary `tools/hat_pace_selftest`
  (no `.c`) is untracked and NOT gitignored — flagged in the commit message;
  operator to decide ignore / delete / leave.
- Next: 3.1r.3 — v2 product `.so` from `tools/hat_alsa_plugin.c` mirroring the
  self-test v2 model (both entry points, `-DPIC`, zero warnings) +
  `ALSA_CONFIG_DIR` scratch load test **before** install; build-2 (md5
  728c2456…) stays installed until a green live batch (3.1r.4, every aplay
  wrapped `timeout 10`).