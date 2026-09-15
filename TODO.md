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
- [ ] Set up host VS Code entrypoints (`.vscode/tasks.json` + `launch.json`) via the
      entrypoints playbook once a main entrypoint exists

## Device firmware (host-owned, separate from the pipeline)

- [ ] Reconcile the live board against `setup.sh` after the 960x960 migration
- [ ] Decide deferred security hardening (VNC `-localhost`, tunnel-first access)