# 2026-09-16 — WS3 ALSA plugin: build saga, the 9-minute noise incident, reference-pinned redesign

Governing plan: `plans/current/2026-09-15-13-17-28_hat-alsa-device-and-tts.md`
(WS3, task 3.1 flagged `[?]`; WS3-revision block pending operator approval).

## Why this entry exists

The operator asked (09-16 18:29) whether plan and journal reflected
*everything* done this session — every attempt, what worked, what didn't,
and what drove each next step. Prior entries lagged the disk (plan task 3.1
was still bare `[ ]` with the plugin already written, built, installed on
the board, and had produced a 9-minute noise incident). This entry replaces
the earlier 09-16 stub and is the permanent record of the whole arc.

## Operator-side context (what was actually asked)

- 09-15 20:14: eventual goal = "a fast, low-resource text to speech option,
  preferably gnu" to test the speaker with real audio.
- 09-15 20:15–20:17: "why cant we just play the sound like normal" /
  "is there a straightforward way to jury rig this so it will work like a
  normal sound device?" → **the plan's title task**: `aplay x.wav` / bare
  `espeak-ng "x"` with no flags, no stop/start dance.
- 09-16 08:11: "now there is a crazy noise that doesn't end" (the incident;
  resolved 08:17 by killing the stuck aplay → socket close → engine idle,
  tick loop back to 0 %).
- 09-16 08:18: "think very hard about what you've done and why it's gone
  wrong… without making any more insane noises tonight, deeply understand
  the problem and articulate a solution **without implementing it**" →
  post-mortem (below); explicit no-live-noise constraint.
- 09-16 17:59 + 18:21: "ok go ahead" → approved the *safe* order only:
  silent source reads → offline self-test (green first) → then a tight,
  timeout-gated live batch. Not a free pass to ship to the pin.
- 09-16 18:33: the request this entry answers.

## State of record, 2026-09-16 (verified on-disk & on-board, read-only)

- Repo HEAD `762d163` (hat-sound v3.6 daemon, WS1+WS2 committed). Untracked:
  `journal/2026-09-16-*.md` (this), `libasound_module_pcm_hat.so` (repo-root
  build artifact), `tools/hat_alsa_plugin.c` (593 lines). Modified: plan +
  index (this entry's revision).
- **Board:** engine running (`23106 /usr/local/bin/hat-sound -d
  /run/gamepi-sound.sock`), device registered (`aplay -L` shows `hat`).
- **Installed plugin `/usr/lib/aarch64-linux-gnu/alsa-lib/
  libasound_module_pcm_hat.so` = md5 `728c24565f58ca4b8371b6aac978faad`**
  (71,400 B, 01:05) = **build 2, the "dump-fast" build** (loads, wrong
  pace). `/etc/asound.conf` = the 5-line `pcm.hat { type hat }` block.
- **Staged `/tmp/hatplug/libasound_module_pcm_hat.so` = repo-root artifact
  = md5 `33037e8eec854c58b06392fddc251605`** (71,456 B, 01:08) = **build
  3, the pacing-bug build that made the 9-minute noise**.
  **NEITHER is the post-redesign v2 — build 3 must NOT be installed** (it
  reproduces the buzzer). Board is currently safe only because the stale
  build 2 is what's loaded; a reinstall is required before any live v2.

## The incident, chronology with evidence

**Build 1 (first cut) — 01:00. Loads: NO.** Exported
`_snd_pcm_hat_open` + version marker but dlopen (RTLD_NOW) rejected it:
undefined `snd_dlsym_start`. Root cause (verified against the libasound
sources): `SND_DLSYM_BUILD_VERSION` branches on `PIC` — without `-DPIC`
it emits a `constructor` referring to the `snd_dlsym_start` global, which
the board's PIC `libasound.so` does not export. Fix: build with `-DPIC`
(the upstream libtool flag; `configure:9922/9953`). Re-verified symbol
table: `_snd_pcm_hat_open` (T), `__snd_pcm_hat_open_dlsym_pcm_001` (B),
zero `snd_dlsym_start` refs. Lesson: out-of-tree ALSA plugin builds need
`-DPIC`; the loader dlopens RTLD_NOW so any bad symbol kills the load.

**Build 2 (with -DPIC) — 01:02. Loads: YES. Pacing: WRONG (dump-fast).**
Live batch: TTS `aplay -D hat` took **0.024 s** (a 2.445 s file). Engine
journal (one job, TTS): `underruns 10953`, effective **29,566 Hz / 0.62×
real-time** (38 % of tick slots starved, engine played truncated fast
audio). The four 3-second sines (8k/22.05k-stereo/96k/192k) each delivered
**~21,300 / 144,000 samples** (~15 %) yet each reported 1.00×. Diagnosis:
`hat_pointer()` returned `st->sent` (client frames accepted ≈ appl_ptr)
→ `hw ≡ appl` → `avail ≡ buffer_size` **always** → `write_areas` never
waits → aplay dumped the whole file into the socket in one burst; the
~63 KB kernel queue + engine ring absorbed it, the engine fed the tick as
fast as it drained the backlog (≈0.6 s) and starved. The engine was
innocent (constant wall-locked tick); the plugin was the sole pacer and
it paced nothing.

**Build 3 (wall-clock pacing attempt) — 01:08. WRONG, and the noise
incident.** Pacing fix = track `mass` (48 kHz samples sent) + `drain`
(wall instant the backlog is consumed): `consumed(now) = mass − max(0,
(drain − now)·48k/1e9)`. The session record shows two aborted rewrites
(the first `s48/scl` model was discarded, overcorrected, then restored —
the file was briefly mid-refactor and inconsistent; a single clean pass
wrote the final model) + a u64 reassociation so the 192 kHz long-session
`w·rate·DIV` product can't overflow. Rebuilt → md5 `33037e8e…` (the one
now staged + in the repo root). Preloaded via `ALSA_PLUGIN_DIR=/tmp/hatplug`
(engine untouched). TTS took ~2.4 s — **but then hung** (30+ s and
counting; expected 2.445 s). Live snapshot: engine's last *logged* job
ended 01:06:43; this run's job had **EOF-d after only 35,196 B ≈ 15 % of
file**; aplay alive in state **R**, ~190 % CPU (user 3m11 + sys 5m39),
`wchan` empty → spinning in userspace, not blocked. Operator: **crazy
noise that doesn't end** (08:11); killed aplay (08:17) → socket closed →
engine returned to pin-hold-idle, 0 % tick.

**What the noise was — engine telemetry is the record:**

```
job (v3.6 daemon) client EOF, ring drained — source 7040 B, consumed 7040 B
(3520 samples, 6.6 Hz, 0.00x real-time), underruns 25522298
socket t=531.8s ticks=102101467 ... late1us=290928 und=25522298 duty=57%
```

Over 531.8 s the engine consumed 7,040 B (≈73 ms of "hello…") then sat on
an empty ring: **25,522,298 underruns = 99.98 % of all ticks**. In
`tools/hat-sound.c`, `sample_next()` (`:303-315`) on an empty ring
`g_underrun++; return 0; /* hold last sample */` — the tick loop keeps
integrating that held 16-bit value, so the sigma-delta collapses to a
**constant DC wave at that sample's duty cycle** → a loud, unbroken tone
(`duty=57 %`). It could only stop on connection close = engine EOF; there
is no timeout on the play-to-EOF. This is **frozen, intended engine
behavior** (pin-held, plays to EOF); it never changes in this plan — the
plugin must be able to force EOF or fail cleanly so this can never be
triggered by a confused client.

## Root cause (three compounding faults — all mine, except the amplifier)

1. **Arithmetic defect** in the wall-clock drain model (fixed-point
   `mass`/`drain`/`HAT_LIN_DIV` — exact culprit not pinned by inspection;
   treated as the trigger, not the design lesson). Empirically: under v2
   (build 3) **every job stopped advancing at 13–15 % of file** (six jobs:
   21,119 / 21,998 / 21,324 / 21,624 / 17,598 samples ≈15 %; one early
   boundary ≈3 %). Framework saw `avail < 1` → `writei` returned
   **-EPIPE immediately and forever**.
2. **Design defect (the real one):** `pointer()` was the *sole*
   backpressure mechanism with **no bounded wait and no hard failure**.
   The plugin is fully cooperative — it paces only as the engine
   consumes — so *a confused client hangs instead of dying*: aplay's
   blocking-mode underrun path is EPIPE → `snd_pcm_recover` → retry →
   EPIPE … **with no timeout anywhere**, at 190 % CPU, holding the
   connection open for 9.5 minutes.
3. **Procedural defect:** validated the new pointer math only by "it no
   longer finishes in 0.024 s", then ran an **unbounded `time aplay`
   against the live pin and nobody watched the journal** while it spun.

Amplifier (engine, frozen): empty-ring hold → one stuck client =
unbounded loud tone. Consequence noted for future work (WS4 optional): a
user-run "socket guardian" that closes client connections quiet > N s
would make *any* future client failure non-audible. Deliberately NOT part
of this revision (no engine/wire change; keep tick path frozen).

## Why the wall-clock CONSUMPTION model was the wrong design (not just a bug)

Even correct drift-free, it's fundamentally fragile: it must **estimate
engine pull without a feedback channel**, and the send queue is what
hides the estimate:

- **Steady-state latency = the whole queue.** ~64 KiB `SO_SNDBUF` at
  48 kHz s16mono ≈ **0.667 s**; plus the 85 ms engine ring → the entire
  file plays 0.5–0.77 s behind. `send()` never blocks (POLLOUT always
  true on a 64 KiB queue), so aplay never sees backpressure → it
  front-loads as fast as the socket eats.
- **Drift is self-invisible.** Any engine jitter / feeder wakeup / RT
  hiccup makes the model lie, and since the model is the *only* pacing
  signal, the error shows as stalls / fast / slow — **never as a
  recoverable XRUN**.
- **Starvation is invisible.** Engine accepts but doesn't consume → send
  still succeeds (queue has room) → `pointer` still advances → silent
  stall. There is no path that turns "engine not consuming" into an
  error.

The consumption model was fundamentally fighting the socket: using it to
*measure* consumption that the socket never reports.

## The reference pinning (what decided the redesign)

- Pinned every detail of the spin path against alsa-lib **1.2.14** local
  checkout `/home/cj/hat-alsa-src/alsa-lib-1.2.14/` (line refs in the
  plan's WS3-revision block; the earlier stub had the same refs, kept).
- Confirmed **aplay 1.2.14** real loop (`aplay/aplay.c`, tag `v1.2.14`,
  tree `8cbe7b8…`): on partial/short write → `snd_pcm_wait(handle, 100)`
  → loop back to `writei` with the remainder. The **hot-spin path is
  inside `snd_pcm_write_areas`** (`pcm.c:7686`) when `size > avail` and
  `may_wait_for_avail_min` is false — the no-sleep path.
- Diffed build 2 vs build 3 object code: 8-line disassembly difference is
  **compiler scheduling, semantically equivalent** → the stall is NOT a
  rebuild regression; it's the model. Ruled out by evidence.
- Journal time-math: the "last job ended 01:06:43" line was from the
  *dump-fast* era; the hung run started 01:08:47.2 (aplay's start
  time). Reconciled.

## The redesign (the PROPOSAL the plan now carries)

Core insight from the audit: **stop trying to measure engine consumption
indirectly (via the socket). Instead, gate the send ON consumption and
let the pointer be the trivial "accepted minus a fixed lag" with no
estimate in the hot path.** The pinning proved the plugin controls the
client entirely through `pointer()` + `poll_fd`; so the client needs
**nothing but a bounded, honest pacer**:

- **pointer** = gated send-side accounting: 48k frames **ADVANCED**
  (accepted) are "consumed" at `gate_time = send_time + backlog_bytes /
  48 kHz`; monotone; never exceeds accepted; `-EIO` on transport death.
  No DIV-carry in the hot path (that was build 3's bug).
- **send gate**: before each `send`, if `backlog > 0` and
  `(now − send_time)·48k/2 < backlog` → `poll(POLLOUT, period_ms + 200)`.
  Wakes at the same instant the framework's own wait would → the client
  only ever sees short writes exactly as aplay expects (short/EAGAIN →
  `snd_pcm_wait(100)` loop), and `pointer` advances in step with actual
  consumption (gated by the engine's real pull on the socket, not an
  estimate).
- **`SO_SNDBUF ≈ 16–32 KiB`** bounds the worst-case backlog to
  ~250–410 ms (vs 667) instead of 0.667 s.
- **real drain**: `drain` callback = blocking loop until the account says
  fully consumed (or 2× the remaining drain time) so `snd_pcm_drain`
  (aplay's EOF path) waits for the actual tail; framework drop → `stop`
  → socket close stays the engine's EOF signal.
- **stop-closes**: `stop` closes the socket (fresh job on every `start`),
  removing any last-hw / state residue between streams.
- **starve watchdog**: engine accepts but consumes nothing for > X ms
  (X ≈ 5 × period, tunable) → `dead = 1` → `pointer` = `-EIO` → clean
  XRUN → aplay `snd_pcm_prepare` → lazy reconnect (fresh job). Kills the
  silent-stall / hot-spin-by-holding-a-live-pin failure class. A bounded
  client can never again hold a 1-bit output open.
- **Reuse from build 3 / v1** (validated fine by the audit on every
  framework interface, and by the live runs): resampler (identity/up/down
  + cross-chunk carry), downmix, full param lists, `prepare` reset,
  `close` frees, `dump`, lazy-connect idempotency, `reinit_status` at
  connect + close.
- **Offline proof BEFORE any pin time** — `tools/hat_pace_selftest.c`
  (tracked, no GPIO, no engine): a fake "engine" thread on a socket pair
  with a known ~85 ms in-kernel delay pulls at a fixed 48 kHz pace; the
  driver opens the plugin via `snd_pcm_open("hat", …)` pointed at the
  pair and drives the *same* `writei`/wait/EPIPE-prepare loop aplay
  uses. Asserts: (i) total wall time within ±10 % of pattern duration;
  (ii) `pointer` never backwards and never ≤ appl; (iii) XRUN never
  fires in healthy mode; (iv) the starve-watchdog case (fake engine stops
  pulling) DOES fire `-EIO` → XRUN within X ms with a bounded deadline.
  This is the P1 unit harness from the post-mortem (stubbed `send`,
  synthetic clock, `avail` stays `[1, buffer]`, starvation bounded) in
  end-to-end form. **Green on this machine (workspace == board) before
  3.2.**
- **Safe live batch (3.1r.4)**, ONE ask, ~3 min cap each, auto-Ctrl-C
  semantics documented: (a) ~2 s tone → elapsed ≈ 2 s (the pacing gate,
  v1 failed here); (b) espeak sentence → gap ≈ voice-end-to-start (no
  ~3 s dead air); (c) kill the daemon mid-play → aplay recovers to
  XRUN/reconnect **or** exits cleanly, no hang; systemd restarts the
  engine. **Gated: if (a) is off by more than ±15 % we stop and re-tune
  the gate before (b)/(c).** ONE operator question on pass: clean,
  on-time speech? Every `aplay` wrapped in `timeout 10`; engine
  underrun count + `pgrep aplay` + socket checked within seconds of each
  test; any stall → immediate kill, journal, no retry until the offline
  self-test explains it. **No unbounded runs against the pin, ever** —
  the discipline that replaces the procedural defect.

## What worked, what didn't, and what drove each next step

| Attempt | Result | What it told us | Next step chosen |
| --- | --- | --- | --- |
| Build 1 (plain dlopen) | **load FAIL** (`snd_dlsym_start`) | Out-of-tree ALSA plugin needs `-DPIC`; loader RTLD_NOW | Rebuild `-DPIC`, verify symbol table → Build 2 |
| Build 2 (`-DPIC`) | loads; **dump-fast 0.024 s / 0.62×** | `pointer` returned `sent` → `avail` always full → no backpressure | Add a real consumption model → Build 3 (wall-clock) |
| Build 3 (wall-clock) | **9-min buzzer**, aplay 190 % spin, jobs stop at ~15 % | pointer = sole backpressure with no hard failure turns any estimate error into unbounded held-sample tone; the send queue hides the estimate | Stop estimating; **gate the send** on consumption; bounded failure; offline self-test before any pin |
| v1 reference audit | **all framework interfaces correct**; only pacing model is the defect | the plugin's `pointer` + `poll_fd` *is* the whole pacing surface | the v2 gated-send-pacer design above |
| Build 2↔3 objcode diff | 8-line delta = scheduling only, semantically equivalent | the stall is the model, not a rebuild regression | rule out rebuild artifact |
| Journal time-math | "last job 01:06:43" was the dump-fast era; hung run = 01:08:47.2 | reconcile which job was which before blaming the pin | — |

**Worked:** the v3.6 engine socket contract (clean 0.5 s 440 Hz in
WS2); the `-DPIC`/symbol-loader chain; the v1 plugin's resampler /
downmix / param lists / prepare / close / connect-idempotence /
`reinit_status` (every interface the audit checked out is reusable);
the reference pinning (it's what located the defect exactly); the
discipline that landed this entry (read-only state dump before writing).

**Didn't work:** the original 3.1 premise ("thin pipe + wall-clock
pointer is enough"); build 1; build 2 (dump-fast); build 3 (the
incident — arithmetic + sole-backpressure-no-hard-failure); and the
procedural shortcut of validating pacing live instead of offline.

## Pending / next

- WS3-revision (3.1r.1–3.1r.4) **awaiting operator approval** — the
  journal states no implementation before approval (plan-playbook
  step 6/9).
- On approval, in order: 3.1r.1 (v2 source) → 3.1r.2 (green
  self-test, **before** any pin) → 3.1r.3 (rebuild, symbol + load check,
  re-stage `/tmp/hatplug`, discard the build-3 md5 note `33037e8e…`) →
  3.1r.4 (timeout-gated live batch) → 3.2 (board probe against the v2 .so;
  it also validates symbol naming) → 3.3/3.4/3.5 (setup.sh build+install,
  managed `/etc/asound.conf`, apt gains espeak-ng/alsa-utils/
  libasound2-dev, verify rows 12–14, `docs/setup.md` in the same change;
  preflight checks `tools/hat_alsa_plugin.c`) → 3.6 (bare
  `espeak-ng "Hello"` operator gate) → WS4 → plan `current → past`.
- If the WS3-revision is **rejected**: 3.1 stays `[?]`, this journal
  records the deferral, no engine / setup.sh / board change in the
  interim (rollback = delete the untracked files; the board keeps the
  stale build-2 `.so` until 3.3's managed-file path replaces it).
- No `sudo` used this window; all board reads were non-sudo. The stale
  build-2 `.so` stays installed until 3.3 lands the managed install
  path; that is not yet safe to replace with build 3.