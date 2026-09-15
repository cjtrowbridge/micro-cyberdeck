# micro-cyberdeck — Host Checklist

Host-owned checklist for wiring the Agentic Pipelines framework into this repo.
The framework is mounted at `./agentic-pipelines` (see `./agentic-pipelines/AGENTS.md`).

## Bring-up status

- [x] Add `agentic-pipelines` as a git submodule at `./agentic-pipelines`
- [x] Create host scaffold: `AGENTS.md`, `.gitignore`, `api.sample.yaml`, `plans/`, `journal/`, `prompts/`, runtime dirs
- [x] Track `api.sample.yaml`; keep `api.yaml` ignored

## Operator actions (local, non-committed)

- [ ] Copy `api.sample.yaml` to `api.yaml` and fill in the local Ollama-compatible
      endpoint, model, and any gateway credential (do not commit `api.yaml`)
- [ ] `python agentic-pipelines/scripts/pipeline.py preflight --api-config api.yaml`
- [ ] Before any model-backed stage, bootstrap local deps into the ignored
      `.agentic-pipelines/` dir (see `how_to_configure_local_inference.md`)

## Pipeline definition (needs a host goal)

- [ ] State the pipeline goal, then run the design playbook to produce `pipeline.yaml`
- [ ] Build host runtime prompts in `prompts/` (worker/reviewer/repair) via the
      prompt-building playbook
- [ ] Validate the staged package: `python agentic-pipelines/scripts/validate_pipeline_package.py <staged-package>`
- [ ] Set up host VS Code pipeline entrypoints per the entrypoints playbook
      once `pipeline.yaml` exists (`.vscode/` already carries the provisioning
      task/play action "GamePi: set up the machine")

## Device provisioning (setup.sh — reentrant rewrite)

- [x] Rewrite `setup.sh` as the canonical re-entrant, self-verifying
      provisioning entrypoint (templates proven byte-canonical against the
      verified-live 960x960 configuration)
- [x] Document the contract in `docs/setup.md`; link it from `AGENTS.md` and
      `README.md`; mandate in the script header (changes to `setup.sh` must be
      documented there in the same change and pass apply->verify on a live
      board)
- [x] Single VS Code entrypoint (`.vscode/tasks.json` + `launch.json`):
      "GamePi: set up the machine" in the integrated terminal
- [x] Live validation on the board: `sudo bash setup.sh --plan` -> `RESULT:
      CONVERGED`, exit 0 (this also reconciles the board against the 960x960
      migration)
- [x] Second apply on a healthy board changes nothing and exits 0 (re-run
      contract proven live)
- [x] Commit the re-entrant rewrite + docs + governance (after live validation)
- [ ] Decide deferred security hardening (VNC `-localhost`, tunnel-first access)