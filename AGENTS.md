# micro-cyberdeck — Host Agent Instructions

This host owns the Orange Pi Zero 3W GamePi device: the fresh-flash bring-up
script (`setup.sh`), the 960x960 single-screen migration (`apply-960.sh` /
`revert-480.sh`), the bring-up log (`Diagnose XFCE Freeze.md`), and the host
pipeline scaffold described below.

## Agentic Pipelines (framework submodule)

The prompt-first Agentic Pipelines framework is mounted at `./agentic-pipelines`.
That file is canonical for the framework:

- Universal model, safety invariants, and task routing: `./agentic-pipelines/AGENTS.md`
- Task playbooks: `./agentic-pipelines/playbooks/`
- Executable runtime prompts: `./agentic-pipelines/prompts/`

Route all pipeline design, prompt-building, validation, operation, review, and
framework-change work through `./agentic-pipelines/AGENTS.md`. Host-owned
artifacts (this file, `TODO.md`, `pipeline.yaml` when present, `prompts/`,
`plans/`, `journal/`) take precedence over framework defaults, and the framework
never overwrites host-owned content during bootstrap or updates.

## Host layout

- `setup.sh` — the canonical, re-entrant provisioning entrypoint. Its contract
  (managed scope, flags, phases, exit codes, reboot policy, result line) is
  documented in `docs/setup.md`.
- `docs/setup.md` — the setup/provisioning contract, linked from `setup.sh`.
  **MANDATE:** any change to `setup.sh` or the provisioning behavior must be
  documented in `docs/setup.md` in the same change (the script header carries
  the same mandate and links here), and must pass `apply -> verify` on a live
  board before the change is committed.
- `api.sample.yaml` — tracked local-inference template. Copy it to the ignored
  `api.yaml` and supply local values; never commit `api.yaml`.
- `pipeline.yaml` / `prompts/` — host pipeline definition and customized runtime
  prompts, created by the design and prompt-building playbooks once a host goal
  is set (see `TODO.md`).
- `plans/` and `journal/` — host change plans and design checkpoints.
- `state/ artifacts/ threads/ reports/ failures/ runs/ .agentic-pipelines/` —
  ignored runtime evidence, reports, and locally installed dependencies.