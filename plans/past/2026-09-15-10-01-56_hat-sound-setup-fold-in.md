---
plan_id: 2026-09-15-10-01-56_hat-sound-setup-fold-in
title: Track hat-sound.c and fold real-audio into setup.sh
summary: Make tools/hat-sound.c a tracked managed artifact, build+install it from source into /usr/local/bin, add a gamepi-sound.service, persist kernel.sched_rt_runtime_us=-1 via a sysctl.d drop-in, converge user_overlays to the full desired set, clean stale es8388/i2c overlays, and document every row in docs/setup.md in the same change; live apply->verify on the board before commit.
status: past
created_at: 2026-09-15-10-01-56
---

Key: `[ ]` pending task, `[x]` completed task, `[?]` needs validation, `[-]` closed task

# Track hat-sound.c and fold real-audio into setup.sh

Repo goal: the HAT's speaker is driven by a **version-controlled,
provisioned, self-verifying** service, not a hand-built binary in `$HOME`.
`setup.sh` (the canonical re-entrant entrypoint) converges a fresh board to
"audio works, RT limit lifted, no stale I2S overlays." This plan turns the
working `tools/hat-sound.c` (v3.3 real audio) into managed artifacts.

Blocking precondition (UPDATED 2026-09-15 ~10:15): the v3.3 4×-pacing
bug is **root-caused and fixed in v3.4** — the loop's 4-tick overrun
re-anchor compared `now - next` as `uint64_t`; on a healthy clock that
underflows to ~2^64, so the re-anchor fired every tick and `tis` was
stuck at 0 → 1 sample per tick (the self-report measured 3.98x). Fix:
signed compare. The engine itself was always correct (the bench links it
and it never exercises `main()`'s loop — that is why the bench was green
while the field was 4x). **GATE CLOSED 2026-09-15:** operator run
self-reported `t=5.5s ticks=1055990 … 1.00x real-time` (1 055 990 =
5.5 s × 192 kHz) with the 4 tones at normal pitch, ear-clean; fix
committed `a2b7dc8`.

STATUS (2026-09-15 ~13:00): **fold-in WRITTEN + LIVE-VALIDATED.**
Operator ran `--plan` → apply → second apply: first apply removed the two
stale dtbos, installed the v3.5 build from tracked source, wrote the
drop-in + unit (`enable --now`), no reboot (stamp unchanged); the next run
was zero-write, all 13 verify rows PASS, `RESULT: CONVERGED`, exit 0.
Two fold-in verify bugs found + fixed in the same change (see task notes);
`docs/setup.md` implementation notes same change. COMPLETED: commit +
index regen + move to `plans/past/`.

## Design decisions

1. **Build from source at provision time.** `setup.sh` compiles
   `tools/hat-sound.c` (tracked) → `/usr/local/bin/hat-sound`. Kills the
   stale-binary drift that consumed a whole window. Idempotent: rebuild
   compare-to-installed; install only on byte change (else the service
   restart gate in §f would flap).
2. **`gamepi-sound.service` mirrors the plain-root LCD-bridge unit**: no
   `User=` line, `Environment=HOME=/root`, `Restart=always`, **no X
   dependency** (no `ExecStartPre` X-wait). Behavior RESOLVED: operator
   chose option (c) → `ExecStart=/usr/local/bin/hat-sound -i` (v3.5
   idle: pin LOW, no sound at boot; stop the service to play).
3. **RT bandwidth lift = `/etc/sysctl.d/` drop-in**, not a runtime `echo`.
   The board has no `sysctl` binary; current `kernel.sched_rt_runtime_us=-1`
   is runtime-only and regresses on reboot. Documented in `docs/setup.md`
   with the why (FIFO task throttled to 50 ms/1 s).
4. **`user_overlays` converges to a SET, not a single value.** Today
   `apply_overlay()` seds it to `OVERLAY_NAME` alone. Introduce a DESIRED
   overlay list; the fold-in adds `gpio-keys` later. Must not clobber future
   overlays.
5. **Stale audio overlays removed with a documented why**: `es8388-audio.*`
   and `i2c0.*` are the wrong audio path (the HAT amp is GPIO/PWM, not I2S).
6. **AGENTS.md mandate honored**: every new/changed behavior gets a
   `docs/setup.md` row in THIS change, and the change passes live
   `apply -> verify` on the board before commit.

## Tasks

- [x] Author this plan + the v3.3 status journal (2026-09-15).
- [x] **Track `tools/hat-sound.c`** (commit `a63a759`; first git
      baseline). `hat-sound-v2.c` / `sd-bench.c` stay in `$HOME`, OUT of
      the repo unless they become reference.
- [x] **Root-cause the 4×-pacing bug** (2026-09-15 ~10:15). Self-report
      run: `3.98x real-time` → field really was 4x. Cause: `main()`'s
      4-tick overrun re-anchor `if (now - next > 4 * TICK_NS)` on
      `uint64_t` operands underflows on a healthy clock (now < next) and
      fires every tick, resetting `tis` to 0 → 1 sample/tick. The engine
      bench was green because it never ran `main()`'s loop. Fix = signed
      compare; v3.4 built 10:14, md5
      `38281b372cb30c292a02efb3e0e59a5f`.
- [x] **Confirm the v3.4 fix live (GATE before folding).** Operator run
      (2026-09-15): self-report `t=5.5s consumed=528000 B (264000
      samples, 48000.0 Hz, 1.00x real-time) , ticks=1055990, dev_avg=54.7
      ns, late1us=2396 x1ms=0 x10ms=0, und=1, duty=50%` + operator
      ear-clean (4 tones, normal pitch). Fix committed `a2b7dc8`.
- [x] **DESired block additions** in `setup.sh` §1 (~L79-82):
      `SOUND_BIN="/usr/local/bin/hat-sound"`, `SOUND_SRC="tools/hat-\n      sound.c"` (caller-CWD-relative, preflight-checked), `SYSCTL_RT_\n      DROPIN="/etc/sysctl.d/99-sched-rt.conf"`. (The set lives in
      `overlay_desired_set()` below rather than a new variable — one
      function owns it so gpio-keys joins in one place.)
- [x] **Converge: build+install the binary.** New `build_sound()`: plan
      → `sound-src` item; apply → missing src: `INCOMPLETE:sound-src`
      exit 1; `gcc -O2 -Wall -Wextra` to mktemp; fail:
      `INCOMPLETE:sound-build` exit 1; `cmp -s` vs installed → identical
      = zero writes, else `install -m 755` + `CHANGED_FILES`; (g) marks
      the sound unit dirty on a binary change (restart onto new binary).
- [x] **Converge: `gamepi-sound.service`** via a new `sound)` case in
      `generate_unit()`: root-owned (no `User=`), `Environment=HOME=/root`,
      no X dep (no `ExecStartPre`), `Restart=always`, `RestartSec=1`,
      `ExecStart=/usr/local/bin/hat-sound -i` (v3.5 idle — the settled
      option (c)).
- [x] **Converge: `kernel.sched_rt_runtime_us=-1` drop-in.**
      `write_if_changed "$SYSCTL_RT_DROPIN" 644 root
      'kernel.sched_rt_runtime_us = -1'` in converge (f). Board has no
      `sysctl` binary; `systemd-sysctl.service` (static) applies it at
      every boot.
- [x] **Converge: `user_overlays` to the DESIRED set** — helpers
      `overlay_desired_set()` / `overlay_uvs()` (LC_ALL=C-sorted space-
      joined); plan + apply drift is a set comparison; whole-line sed only
      when the set differs; no-op on the healthy board (board set already
      equals `spi3-cs0-48mhz`).
- [x] **Converge: remove stale `es8388-audio.*` + `i2c0.*`** from
      `/boot/overlay-user/` **and** `/boot/armbian-overlays/`, logged with
      the why ("es8388/audio = wrong I2S audio path; HAT amp is
      GPIO/PWM"). `$HOME` copies deliberately untouched (operator rm —
      housekeeping in the journal). Not in `overlay_sig` → removal is not
      a reboot trigger.
- [x] **Extend every `for unit in ...` loop** (converge, unit-repair ×2,
      verify) with `sound`; `unit:sound` verify row in place.
- [x] **Verify matrix additions**: `rt-sysctl` (PASS iff
      `/proc/sys/kernel/sched_rt_runtime_us` == `-1` — read /proc, sysctl
      binary absent), `audio-cards` (PASS iff `/proc/asound/cards` has
      EXACTLY one card, `allwinnerhdmi`), `stale-overlays` (PASS iff no
      es8388-audio/i2c0 dtbo in either overlay dir AND no
      `/sys/bus/i2c/devices/i2c-0`), `gamepi-sound` active via
      `unit:sound`.
- [x] **`docs/setup.md` rows (SAME change)**: five-unit row incl.
      `gamepi-sound`; `hat-sound` build/install row; sysctl drop-in + why;
      `user_overlays` set row; es8388/i2c removals + why (rationale
      verified 2026-09-15); verify rows 9/10/11; preflight `gcc` +
      `tools/hat-sound.c`; exit codes `INCOMPLETE:sound-src|sound-build`;
      reboot-policy extension; new **"## Sound (hat-sound, v3.5)"**
      section.
- [x] **Operator decision: boot-time audio behavior.** SETTLED: option
      **(c)** silent/low-power idle, pin LOW, no sound at boot; tone
      program stays reachable manually. Implemented as v3.5 `-i` (idle
      branch: acquire pin, park LOW, pause() until signal, release LOW).
- [x] **Live board validation (MANDATORY before commit):** `bash -n setup.sh`;
      non-root `--plan` (drift-only, no writes); operator `sudo bash
      setup.sh` (interactive) → verify shows `unit:sound` PASS, sysctl PASS,
      no es8388/i2c, audio card set correct → `RESULT: CONVERGED`, exit 0.
      (2026-09-15 ~13:00: `--plan` → apply → apply; first apply removed
      `es8388-audio.dtbo` + `i2c0.dtbo`, installed v3.5 from tracked source,
      drop-in + unit (`enable --now`), no reboot — stamp unchanged; next
      run zero-write, ALL 13 verify rows PASS, `RESULT: CONVERGED`, exit 0.
      v3.5 sanity = `unit:sound` active in the same run.)
- [x] **Fix the two fold-in verify bugs in the SAME change** (found by
      the first live run): (1) `audio-cards` awk printed field $3 (the
      driver string `: allwinner-hdmi - allwinner-hdmi`) instead of the
      card name in $2 — a healthy single-card board reported
      `INCOMPLETE:verify`. Fix: name from `-F'[][]'` field $2 + explicit
      `grep -c '^[[:space:]]*[0-9]'` count enforcing "exactly one card"
      (docs row 10 unchanged — the fix matches the documented contract).
      (2) stale-dtbo detection (plan audit + verify row 11) used
      `ls a b 2>/dev/null | grep -q .`; GNU `ls` exits 2 when either
      operand is missing, and `pipefail` made the whole pipeline fail —
      the audit showed PASS + no `stale-overlay:*` items while the dtbos
      were still on disk, while apply's `[[ -f ]]` removal loop fired.
      Fix: pure `[[ -f ]]` tests at both sites; both failure modes
      reproduced + fixed locally before the green re-run. `docs/setup.md`
      gained the two implementation notes (same change, mandate).
- [x] **Commit** as `CJ Trowbridge <chris.j.trowbridge@gmail.com>` (NEVER
      push). Scope: `setup.sh`, `tools/hat-sound.c`, `docs/setup.md`,
      journal + plan updates (`AGENTS.md`/README touch-ups if referenced).
      Then: `python agentic-pipelines/scripts/regenerate_plan_indexes.py
      --repo-root .` (+ `--check`), move this plan `current → past`,
      session-memory final line.
- [ ] **Regenerate plan indexes**
      (`python3 agentic-pipelines/scripts/regenerate_plan_indexes.py
      --repo-root . --check`); move this plan `current → past`.

## Acceptance

- A fresh (or drifted) board, after one `setup.sh` run from the operator,
  has `/usr/local/bin/hat-sound` built from the tracked source,
  `gamepi-sound.service` active, `kernel.sched_rt_runtime_us == -1`
  persisted, `user_overlays` at the desired set, no `es8388-audio`/`i2c0`
  overlays, and `verify` reports all new rows PASS; a second run on a
  healthy board is a zero-write no-op (exit 0, no service flap).
- `docs/setup.md` has a row for every new managed artifact and behavior, in
  the same commit, per the AGENTS.md mandate.
- The 4×-pacing anomaly is closed (self-report `1.00x`) before any of the
  above is committed.

## Out of scope (separate tracks)

- Buttons → `gpio-keys` overlay (operator runs `button-map.py` first).
- VNC hardening; battery README note (housekeeping).