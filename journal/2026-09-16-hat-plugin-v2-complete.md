# 2026-09-16 — v2 product plugin complete; ready for the ONE live batch

Checkpoint for the WS3-revision arc: plan step 3.1r.3 (rewrite
`tools/hat_alsa_plugin.c` to the v2 pacing model) is **code-complete and
verified offline**. The offline-only milestone is done; the only remaining
step before the speaker is usable is the operator-approved single live
batch (install + bounded `aplay` gates). The frozen v3.6 engine and the
committed self-test spec are untouched.

## What is now on disk

- `tools/hat_alsa_plugin.c` (852 lines) — rewritten to the v2 pacing model.
  Build: `cc -O2 -Wall -Wextra -fPIC -DPIC -shared -o
  /tmp/hatplug/libasound_module_pcm_hat.so tools/hat_alsa_plugin.c
  -lasound` → **zero warnings**, md5
  `4b5898126e7219b589ce7d1069df01f3`. (Two comment-only edits after the
  previous green build left the binary bit-identical — expected: no `-g`,
  and stripped comments don't change emitted code.)
- `nm -D` exports **exactly** `T _snd_pcm_hat_open` +
  `B __snd_pcm_hat_open_dlsym_pcm_001` — no `snd_dlsym_start` (which is why
  v1 build 1 load-failed). Confirmed again at runtime with a `dlopen` +
  `dlsym` probe of the final artifact.
- Scratch load test (no install, no engine traffic — connect is lazy):
  `ALSA_CONFIG_DIR=/tmp/hatscratch ALSA_PLUGIN_DIR=/tmp/hatplug aplay -L`
  lists `hat`; `... timeout 10 aplay -D hat -l` → exit 0.
- Installed on the board is still **build-2** (md5
  `728c24565f58ca4b8371b6aac978faad`) in
  `/usr/lib/aarch64-linux-gnu/alsa-lib/`. It stays installed until the live
  batch is green. Repo-root stale build-3 artifact
  (`libasound_module_pcm_hat.so`, md5 `33037e8e…`) deleted today.
- Engine: `hat-sound` pid 23106, socket `/run/gamepi-sound.sock` (0666) —
  running, untouched.

## Decisions made this window (framework-grounded)

**D-transfer-err — transfer returns `-EPIPE`, not `-EIO`, on transport
failure.** Pinned ALSA 1.2.14 `snd_pcm_recover` (pcm.c:8759) recovers only
`-EPIPE` / `-ESTRPIPE` / `-EINTR`; anything else (including `-EIO`) is
returned to the client, and `snd_pcm_check_error` (pcm_local.h:476) passes
non-`-EINTR` errors through verbatim. So a transport failure surfaced as
`-EIO` from `transfer` would have killed aplay. Final contract:
`hat_send_chunk` internally reports `-EIO` (dead/wedged transport);
`hat_transfer` maps it to disconnect + carry-restore + `dead = 1` +
**`return -EPIPE`**. aplay then takes the standard recoverable-underrun
path: `snd_pcm_recover` → `snd_pcm_prepare` → framework `ioplug_prepare`
(→ our `hat_prepare`) → retry → fresh lazy-connect = fresh engine job. The
pointer-side `-EIO` (dead / 600 ms stall) is the separate detection path:
negative pointer → `hw_ptr_update` sets XRUN → next avail returns `-EPIPE`
to the client.

**D-ptrreset — `hat_prepare` clears the stall clock.** The framework's
`snd_pcm_ioplug_prepare` (pcm_ioplug.c:167) runs `snd_pcm_ioplug_reset`
(**`last_hw = 0`**, appl 0) *before* calling our `prepare` — the pointer
restarts at 0 for a recovered job. Leaving `ptr_val/ptr_ns/ptr_have` stale
across that boundary means a stream resuming into real congestion would
false-fire the 600 ms watchdog almost immediately. `hat_prepare` now sets
`anchor_ns = hat_now_ns()`, zeroes `ptr_val/ptr_ns/ptr_have` alongside the
existing `sent/consumed/frames_sent/acc` reset — exactly the self-test
spec's `model_reset` clean-front mandate ("a prepare-then-reconnect (XRUN
recovery) MUST start with a clean front", spec line 44).

**D-anchor — re-anchor only when the queue is dry.** The initial draft
re-anchored `anchor_ns` unconditionally after every send. Re-reading
`v2_chewed_wire` in the self-test settled the model: `start = entry >
front_end ? entry : front_end` — a chunk queued behind the front keeps the
front's anchor; only a chunk that starts chewing immediately (queue dry,
`consumed == sent`) gets `now` as its anchor. Final pacing block in
`hat_transfer`: `{ uint64_t now = hat_now_ns(); if (st->consumed ==
st->sent) st->anchor_ns = now; st->sent += n_out; }`. Condition is checked
against the pre-increment state, matching the v2 model exactly.

**D-drain-dead — drain while dead disconnects and fails.** `hat_drain`
calls `hat_consume_advance` each 10 ms poll; `dead` → `hat_disconnect(st);
return -EIO;`. The framework turns a drain-callback error into a drop while
DRAINING, so the socket is freed via `hat_stop` and the client recovers
through the normal XRUN path. Success path: fully chewed (`consumed ==
sent`) → wait the 180 ms ring-drain margin → 0 → framework drop. This
makes aplay's wall clock ≈ true pin silence for live gate (a).

**D-ownership — verified, no change.** ioplug `create` (pcm_ioplug.c:1074)
`calloc`s the framework's `ioplug_priv_t` and stores `io->data = ioplug`
(*our* caller's struct) without claiming it; `close` (pcm_ioplug.c:901)
calls `callback->close(io->data)` then `free(io)` only. So `hat_close`
freeing our heap `snd_pcm_ioplug_t` is correct and not a double-free.

Hygiene in the same pass: B6 header constant-naming fix, B7 removed unused
`sent_ns_at_anchor` field, B8 split the `(el * RATE)` multiply to
`(el/N)*RATE + ((el%N)*RATE)/N` (u64 overflow at el ≈ 15.9 days), and a
missing `)` on the `poll()` timeout in `hat_send_chunk` (caught by the
compiler, fixed).

## Process incident (institutionalized rule)

The `replace_string_in_file` tool **twice reported success while leaving
duplicate/stale lines behind** (oldString contained a typo — `st->consume`,
and a `HATE_WIRE_RATE` slip — yet the tool matched fuzzily and applied
partially/wrongly). Both were caught only by re-reading the edited region.
**Rule from now on: after every `replace_string_in_file`, re-read the edited
region to verify the final on-disk text before proceeding.** The B8 fix's
duplication required a second corrective edit.

## Carried invariants (unchanged, all still holding)

- v2 model = serial send-side schedule: chewing bound by `sent`
  (kernel-accepted bytes; the product has **no delivered-bytes channel** —
  audited, engine socket is wire-only), `consumed ≤ sent` exactly.
- Budgets re-derived from the in-code model only, never fit to the numbers:
  `HAT_WATCHDOG_NS` 600 ms, `HAT_RING_NS` 180 ms, `HAT_SNDBUF` 16 KiB.
- No mid-transfer `-EAGAIN` soft-pause in-product (safe only for the
  self-test's synthetic-zero content); the resampler carry stays
  contiguous, restored on the failed-chunk error path.
- No unbounded runs against the pin; every live `aplay` wrapped in
  `timeout 10`; offline self-test explains the model before any live
  anomaly is touched.
- Frozen: `tools/hat-sound.c` (v3.6) and `tools/hat_pace_selftest.c`
  (`a3d8f32`).

## Parked / pending

- **THE ONE live batch (needs operator approval):** install
  `/tmp/hatplug/libasound_module_pcm_hat.so` →
  `/usr/lib/aarch64-linux-gnu/alsa-lib/`, then the bounded live gates
  (`timeout 10 aplay` only). Build-2 (`728c2456…`) stays installed until
  the batch is green; on red, reinstall build-2 and journal the anomaly
  before any retry the offline test can't explain.
- `a3d8f32` (self-test, ahead 1) **not pushed** — push needs approval.
- `tools/hat_alsa_plugin.c` uncommitted — commit **after** the green live
  batch, with identity prefix
  `-c user.name='CJ Trowbridge' -c user.email='chris.j.trowbridge@gmail.com'`.
- Untracked `tools/hat_pace_selftest` binary: gitignore / delete / leave —
  operator decision.
- Anomaly-stop rule stands: if the live batch shows anything the offline
  self-test does not explain, stop, journal, report — no blind retry.