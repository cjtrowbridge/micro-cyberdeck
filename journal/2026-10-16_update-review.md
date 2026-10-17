# Repository update and review

- Authority: user requested pull, review, and Agentic Pipelines pointer update. Execution plan: `plans/current/2026-10-16_framework-update-review.md`.
- Host: clean starting worktree; fast-forwarded `ea6d64c` to `582f545` (32 commits). No host source fixes or device deployment performed.
- Framework: clean checkout advanced from `139f99308697c9de864f1d7f0d4e847b5347ff5a` to upstream main `51b2cb7db581dc61450bdbd31625fffc73e08ec0` (13 commits). Pointer change remains uncommitted.

## Review findings

1. **P1 — Unbounded history allocation can kill the metrics service.** `api/go/handlers.go:127` accepts any positive integer for `minutes`, then allocates a map and two maps per minute at lines 142–147, before allocating the output. Apache exposes this route to LAN clients. A large request can exhaust the board's memory; max-int also overflows `minutes+1`. Reject or clamp to the retained one-hour window before any allocation or arithmetic.
2. **P2 — Button edges each inject an entire tap.** `tools/hat-buttons.py:248–251` ignores the `kind` returned by debounce and calls `XSide.press` for both edges. That method (155–165) emits press AND release each time. One physical press/release therefore produces two taps; holding arrows does not hold a key, and shoulder modifiers cannot chord normally. Inject the matching edge and release held keys on disconnect/shutdown, or explicitly choose a single-tap policy on physical press only.
3. **P2 — History chart keeps stale labels and can remain empty.** `api/www/main.js:200–205` updates only existing dataset values when the point count matches. The API always returns 61 points for the dashboard request, so labels never advance as the window rolls. If the first response has no samples, zero datasets are created; later responses cannot add them. Refresh labels and reconcile dataset identities on each update.

Findings are source review of newly pulled host code, excluding exhaustive review of vendored noVNC. No reviewed implementation was edited.

## Framework synthesis

- Safe adoption: new PDF renderer, Ollama/VS Code/Cerebras playbooks, supporting scripts/tests, and updated task routes through the canonical submodule instructions.
- Host preservation: no replacement of `AGENTS.md`, `TODO.md`, provisioning, VS Code entrypoints, API sample, prompts, plans, or runtime configuration.
- Migration proposal: upstream now requires host-owned portable `scripts/bootstrap.py` / `scripts/bootstrap-vN.py` and context presets for supported Ollama stage budgets. This host has neither portable bootstrap nor a declared pipeline goal/package. Preserve `setup.sh` as the host provisioning authority; design any portable bootstrap integration and budget presets separately, with live apply -> verify if provisioning changes. No automatic aliases, dependencies, or bootstrap examples installed.
- No upstream runtime prompt/schema changes in this delta; no host pipeline package to validate. Model/API preflight is outside this pointer-only update and was not run. No conformance audit or device research performed.
- New host-terminal authentication and concise context-record rules adopted. Fetch ran outside the sandbox with existing credentials. Git safe-directory exceptions were command-scoped; no global Git configuration changed.

## Validation

- Git Bash syntax checks passed for `setup.sh`, `api/go/install.sh`, and `tools/i2c_power_probe.sh`; Python AST parse passed for `tools/hat-buttons.py`.
- Framework targeted tests: bootstrap 5 passed; Ollama aliases 10 passed; VS Code examples 4 passed. PDF tests skipped for missing optional renderer dependencies; no dependencies installed.
- Go Linux/ARM64 test-binary cross-compilation and `go vet ./...` passed. Native Linux tests and live-board apply -> verify were not run on this Windows host.
- Rollback framework only: `git -C agentic-pipelines checkout --detach 139f99308697c9de864f1d7f0d4e847b5347ff5a`. Host old revision remains reachable at `ea6d64c`; inspect in a separate worktree rather than resetting over subsequent work.
