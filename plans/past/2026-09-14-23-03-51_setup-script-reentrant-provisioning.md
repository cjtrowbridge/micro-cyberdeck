---
plan_id: 2026-09-14-23-03-51_setup-script-reentrant-provisioning
title: Reentrant setup.sh as canonical provisioning entrypoint
summary: Convert setup.sh from a one-shot installer into a converging, self-verifying, flag-driven provisioning tool; document the contract in docs/setup.md; add the single VS Code entrypoint; mandate setup.sh change documentation.
status: past
created_at: 2026-09-14-23-03-51
---

Key: `[ ]` pending task, `[x]` completed task, `[?]` needs validation, `[-]` closed task

# Reentrant setup.sh as canonical provisioning entrypoint

Repo goal: the device works, and `setup.sh` is the canonical, version-controlled
means of bringing any fresh (or drifted) Orange Pi Zero 3W GamePi board to the
configured state. This plan turns `setup.sh` into a **converging desired-state**
tool and wires the repo governance around that contract.

## Design decisions (agreed in session 2026-09-14)

1. **Single interactive entrypoint.** One command, one VS Code task, one launch
   play action. No plan/apply/verify subcommand trifecta on the surface.
2. **Fixed phases per run:** preflight -> converge -> verify -> reboot decision.
   Verify is the final phase of the one command, not a separate entrypoint.
3. **Flags are the automation surface** (same file; the framework keeps direct
   CLI available for CI/schedulers/agents):
   - `--yes`/`-y`   unattended; never prompts; implies no-reboot by default
   - `--plan`       read-only audit (preflight + compare + verify); wins over all
   - `--reboot`     reboot at end iff a reboot-requiring change was made
   - `--no-reboot`  never reboot; mutually exclusive with `--reboot`
   - `--json`       machine-readable result object; composes with any mode
4. **Reboot policy:** interactive TTY + reboot required -> prompt, default Y;
   non-interactive -> no reboot unless `--reboot`; reboot only when a
   reboot-requiring change actually occurred (tracked via an applied-state
   marker).
5. **Exit codes:** 0 converged/working; 1 drift or verify failed; 2 preflight
   environment failure; 3 converged but reboot pending (not performed);
   130 controlled Ctrl+C.
6. **Credential preservation:** `~/.vnc/passwd` is user state — preserve if
   present, create if absent. No flag-driven reset is part of this plan scope.
7. **DESIRED block:** one readable block near the top of `setup.sh` is the
   single source of the target configuration; feature work changes that block.
8. **Manual edits to managed files are drift** by definition; the next run
   repairs them. `apply-960.sh`/`revert-480.sh` are demoted to documented
   one-time rescue scripts.

## Tasks

- [x] Record design decisions and plan (this file)
- [x] Create `docs/setup.md` — the setup contract (goal, re-run contract, phases,
      flags, exit codes, managed vs user-owned files, reboot policy, result
      line, troubleshooting), and state that setup.sh changes must be documented
      there
- [x] Rewrite `setup.sh`: (live-validated — on-board `--plan`: all artifacts
      unchanged, verify all PASS, `RESULT: CONVERGED`, exit 0; second apply:
      no file changes, `RESULT: CONVERGED`, exit 0)
  - header comment mandating documentation of changes in `docs/setup.md`
  - top-of-file DESIRED block
  - flag parsing (`--yes --plan --reboot --no-reboot --json`), preconditions
    (plan wins over all; reboot/no-reboot mutex)
  - phases: preflight -> converge (existing sections, drift-aware, creds
    preserved) -> verify (xdpyinfo 960x960, bridge journal line, 4 units
    active, port 5900, /dev/spidev3.0, overlay lines) -> reboot decision
  - applied-state marker; single parseable RESULT line; `--json` object;
    stable exit codes; Ctrl+C -> 130
- [x] Validate: `bash -n`; `--help`; non-root `--plan` (expect exit 2, clean
      message); confirm zero 480-related literals remain
- [x] README: provisioning section — what the entrypoint is, how to run it
      (task/play action vs CLI), re-run contract, troubleshooting = re-run +
      `--plan`; note rescue scripts' scope
- [x] AGENTS.md: link docs/setup.md; mandate that any change to setup.sh (or the
      provisioning contract) must be documented in the linked doc and that the
      change passes apply->verify on a live board before commit
- [x] .vscode/: one task (`GamePi: set up the machine`) + one launch play
      action, same command, integrated terminal so sudo/reboot prompts are
      visible; no bootstrap wrapper unless a platform prerequisite is needed
- [x] TODO.md mirrors this plan as the human checklist; journal checkpoint;
      regenerate plan indexes
- [x] Commits: (1) 960x960 work (setup.sh edit + apply-960.sh + revert-480.sh)
      (6cd977f); (2) the reentrant rewrite + docs + governance updates (this
      commit; cut after the final live validation, per the journal stop
      condition)

## Acceptance

- On an already-converged board, a second `setup.sh` run reports converged,
  changes nothing (no backup churn, password untouched), performs no reboot,
  exits 0.
- On a fresh flash, the single entrypoint (interactive) ends with a working
  machine after answering the one reboot question; the agent path
  (`--plan` then `--yes --reboot --json`) reaches `RESULT: CONVERGED`.
- A deliberate drift (e.g., hand-edit a managed unit) is detected by verify and
  repaired on the next run.
- `docs/setup.md`, AGENTS.md, README, and the script's header all state the
  same re-run contract with no contradictory prose.