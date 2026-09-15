# 2026-09-14 — Agentic Pipelines bootstrap into micro-cyberdeck

## Action

Followed `agentic-pipelines/playbooks/how_to_bootstrap_framework_submodule_into_host_repo.md`
to mount the framework (already added as a submodule at `./agentic-pipelines`) and
create the missing host scaffolding.

## Decision record

- **Submodule**: present at `./agentic-pipelines` (committed earlier the same day).
- **Host scaffold created** (missing files only; no host-owned file touched since
  none existed):
  - `AGENTS.md` — host routing shim pointing to `./agentic-pipelines/AGENTS.md`,
    with host layout and the host > framework precedence rule.
  - `TODO.md` — sole host-owned checklist (operator actions + pipeline-design steps).
  - `api.sample.yaml` — tracked, copied byte-for-byte from the framework sample.
  - `prompts/README.md`, `journal/README.md` — host-owned directories, documented.
  - `plans/{current,future,past}/index.md` — regenerated with the framework's
    `regenerate_plan_indexes.py --repo-root .` (all empty, check passes).
  - `.gitignore` — excludes `api.yaml`, `state/`, `runs/`, `threads/`, `artifacts/`,
    `failures/`, `reports/`, `*.rejected.*`, `.agentic-pipelines/`, and Python noise.
- **`api.yaml` NOT created** — operator action per the local-inference playbook
  (copy sample, supply local endpoint/model/credential, then preflight).
- **No `pipeline.yaml` / `prompts/` runtime prompts created** — requires a host
  pipeline goal via the design playbook; refused to invent one.
- **VS Code entrypoints (`.vscode/`) NOT created** — the host declares no
  interactive pipeline entrypoint yet (no `pipeline.yaml`); the entrypoints playbook
  forbids creating files without reviewed host inputs. Recorded in `TODO.md` as the
  next step after a main entrypoint exists.

## Environment note

System Python 3.13 (Debian trixie) has no pip module. To respect "never alter
system Python", the bootstrap helper was run with a host-local, ignored
`.agentic-pipelines/venv/` (created `venv --without-pip`, pip via get-pip.py).
Declared deps (PyYAML 6.0.3, jsonschema 4.26.0) now live in
`.agentic-pipelines/dependencies` with `bootstrap.json` metadata.

## Verification (deterministic, no processing started)

| Check | Result |
| --- | --- |
| Plan index check (`--check`) | pass (rc=0) |
| `git check-ignore` for api.yaml/state/artifacts/threads/reports/.agentic-pipelines/rejected/runs/.venv | all ignored |
| `pipeline.py preflight --api-config api.yaml` | clean "missing api.yaml" failure (rc=2), correct operator instruction |
| `validate_pipeline_package.py agentic-pipelines/examples/markdown_repair` | rc=0, `governance_conformant: true` (6 prompts, 4 traceabilities, schema v5) |

## Stop conditions

None hit. Left for the operator: create `api.yaml` + run preflight, then decide the
host pipeline goal (design playbook) before any `.vscode/` entrypoint work.