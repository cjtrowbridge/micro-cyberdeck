---
plan_id: 2026-09-15_hat-sound-setup-fold-in
title: Track hat-sound.c and fold real-audio into setup.sh
summary: Make tools/hat-sound.c a tracked managed artifact, build+install it from source into /usr/local/bin, add a gamepi-sound.service, persist kernel.sched_rt_runtime_us=-1 via a sysctl.d drop-in, converge user_overlays to the full desired set, clean stale es8388/i2c overlays, and document every row in docs/setup.md in the same change; live apply->verify on the board before commit.
status: current
created_at: 2026-09-15
---

Key: `[ ]` pending task, `[x]` completed task, `[?]` needs validation, `[-]` closed task

# Track hat-sound.c and fold real-audio into setup.sh

Repo goal: the HAT's speaker is driven by a **version-controlled,
provisioned, self-verifying** service, not a hand-built binary in `$HOME`.
`setup.sh` (the canonical re-entrant entrypoint) converges a fresh board to
"audio works, RT limit lifted, no stale I2S overlays." This plan turns the
working `tools/hat-sound.c` (v3.3 real audio) into managed artifacts.

Blocking precondition: **the v3.3 4×-pacing anomaly must be closed first**
(see `journal/2026-09-15-hat-audio-v3.3-status.md`). Do not fold a binary we
have not yet proven plays at `1.00x real-time`. The self-report line ships
in the 01:52 build; one operator run settles it. If `1.00x` → green light.
If `~4.00x` → resolve on-board (board-side build check, `*tis` phase
instrumentation) before folding.

## Design decisions

1. **Build from source at provision time.** `setup.sh` compiles
   `tools/hat-sound.c` (tracked) → `/usr/local/bin/hat-sound`. Kills the
   stale-binary drift that consumed a whole window. Idempotent: rebuild
   compare-to-installed; install only on byte change (else the service
   restart gate in §f would flap).
2. **`gamepi-sound.service` mirrors the plain-root LCD-bridge unit**: no
   `User=` line, `Environment=HOME=/root`, `Restart=always`, **no X
   dependency** (no `ExecStartPre` X-wait). Behavior is a small open
   decision for the operator (see task 3).
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
- [?] **Close the v3.3 4×-pacing anomaly.** Operator runs
      `sudo /home/cj/hat-sound -F /home/cj/test.s16` (01:52 self-report
      binary); read the final `... N.NNx real-time` line. Accept `1.00x`
      (and operator hears 4 tones at normal pitch, ~5.5 s). If `~4.00x`,
      board-side build + `*tis` instrumentation before proceeding.
- [ ] **Track `tools/hat-sound.c`** (`git add tools/`; it is untracked
      `??`). Keep `tools/hat-sound-v2.c`, `sd-bench.c` OUT of the commit
      unless they become reference (they live in `$HOME`, not the repo).
- [ ] **DESired block additions** in `setup.sh` near L71-81:
      `SOUND_BIN="/usr/local/bin/hat-sound"`; `SOUND_SRC="tools/hat-sound.c"`
      (repo-relative); a `SYSCTL_RT_DROPIN=/etc/sysctl.d/99-sched-rt.conf`;
      `DESIRED_OVERLAYS="spi3-cs0-48mhz"` (extendable set, supersedes the
      single-value `OVERLAY_NAME` sed path for user_overlays).
- [ ] **Converge: build+install the binary.** In the (f) artifact section,
      compile `SOUND_SRC` with `gcc -O2 -Wall -Wextra ... -lm -lpthread` to
      a temp; if it differs from the installed `SOUND_BIN`, install (root,
      755) and mark the binary changed so the unit restart gate fires; plan
      mode records DRIFT and writes nothing.
- [ ] **Converge: `gamepi-sound.service`** via a new `sound)` case in
      `generate_unit()`. Root-owned, no X dep, `Restart=always`. `ExecStart`
      = the behavior chosen in task 3.
- [ ] **Converge: `kernel.sched_rt_runtime_us=-1` drop-in.**
      `write_if_changed $SYSCTL_RT_DROPIN 644 root '<one-line> kernel.sched_rt_runtime_us = -1'`.
- [ ] **Converge: `user_overlays` to the DESIRED set** (replace the single-
      value sed in `apply_overlay()` with a set-converge; idempotent).
- [ ] **Converge: remove stale `es8388-audio.*` + `i2c0.*`** from
      `/boot/overlay-user` (and copy in `$HOME` if present), logged.
- [ ] **Extend every `for unit in ...` loop** (converge L760, unit-repair
      L778/786/795, verify L832) with `sound`; add a verify row
      `systemctl is-active gamepi-sound.service` PASS.
- [ ] **Verify matrix additions**: ALSA cards == {allwinnerhdmi} (no extra
      audio card), i2c adapter set has no stale `i2c-0` from es8388, sysctl
      value == -1 (read `/proc/sys/kernel/sched_rt_runtime_us`, NOT the
      binary), `gamepi-sound` active.
- [ ] **`docs/setup.md` rows (SAME change)**: sound artifact + behavior;
      build-from-source; sysctl drop-in + why; user_overlays set; es8388/i2c
      removals + why; the new verify rows.
- [ ] **Operator decision (one question): boot-time audio behavior** —
      (c) silent/low-power idle, pin LOW, no sound at boot [recommended],
      (a) built-in tone program, or (b) one-shot test then LOW.
- [ ] **Live board validation (MANDATORY before commit):** `bash -n setup.sh`;
      non-root `--plan` (drift-only, no writes); operator `sudo bash
      setup.sh` (interactive) → verify shows `unit:sound` PASS, sysctl PASS,
      no es8388/i2c, audio card set correct → `RESULT: CONVERGED`, exit 0.
- [ ] **Commit** as `CJ Trowbridge <chris.j.trowbridge@gmail.com>` (NEVER
      push). Scope: `setup.sh`, `tools/hat-sound.c`, `docs/setup.md`,
      `AGENTS.md`/README touch-ups if referenced.
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