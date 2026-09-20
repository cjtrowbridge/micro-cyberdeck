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
- `docs/hardware/` — the device hardware records: one evidence-first file per
  part of the deck (display, touch, speaker, ALSA audio, buttons, battery,
  PMIC, power path, thermal, fan, NPU, …), indexed by
  `docs/hardware/README.md` and linked row-by-row from the README's
  **Hardware Status** table. Each record states what is known, **how we know
  it** (re-probe sysfs paths, `setup.sh` verify rows, journal/plan citations,
  binary md5s), and what it portends. **MANDATE:** any new research on any
  part of the device — a probe, a measurement, a journal finding, a fix that
  landed — must update the relevant `docs/hardware/` record in the same
  change, and the README **Hardware Status** row if the part's status moved.
  A part with no record yet gets one (plus a README row, if it has none) in
  the same change. The kernel boot log (`dmesg`/`/var/log/kern.log`) and
  `i2c-dev` nodes are root-only on this board — cite them as operator-level
  evidence, and never brute-force register writes against the live PMIC.
- `third_party/` — pinned external work as submodules:
  `third_party/a733_npu_driver` (github.com/petayyyy/a733_npu_driver) — the
  A733 NPU (Vivante VIP9000) LLM/VLM prior: verified configs, blocker list,
  toolchain, and board bring-up for this exact SoC/board. The evidence base
  for [docs/hardware/npu.md](docs/hardware/npu.md); read its
  `docs/import_chat.md` first.
- `api.sample.yaml` — tracked local-inference template. Copy it to the ignored
  `api.yaml` and supply local values; never commit `api.yaml`.
- `pipeline.yaml` / `prompts/` — host pipeline definition and customized runtime
  prompts, created by the design and prompt-building playbooks once a host goal
  is set (see `TODO.md`).
- `plans/` and `journal/` — host change plans and design checkpoints.
- `state/ artifacts/ threads/ reports/ failures/ runs/ .agentic-pipelines/` —
  ignored runtime evidence, reports, and locally installed dependencies.