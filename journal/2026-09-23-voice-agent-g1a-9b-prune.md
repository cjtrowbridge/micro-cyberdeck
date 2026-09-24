# 2026-09-23 — voice-agent G1-a: prune qwen3.5:9b (operator-approved disk checkpoint)

Plan: [`plans/current/2026-09-23-22-57-17_voice-agent-gates.md`](../plans/current/2026-09-23-22-57-17_voice-agent-gates.md) (G1-a)
Design: [`projects/voice-agent/docs/design.md`](../projects/voice-agent/docs/design.md) §6 disk note / §10 decision 2

## Trigger

G1-a was the gate plan's explicit **operator checkpoint**: root FS at **2.5 G
free (92 %)** vs G1's need for ≈2–2.5 G (whisper-medium ~1.45 G + whisper.cpp
build + Go module cache + apt batch). The 9.7B `qwen3.5:9b` (6.14 G on disk)
was the obvious prune candidate. **Operator decision: delete it.**

## Safety check before the cut

- `setup.sh` (the re-entrant provisioning entrypoint, run as
  `sudo bash setup.sh --yes`) **manages the Ollama model set as *presence*-only**
  — `ollama_missing_models()` pulls any of `OLLAMA_MODELS=( qwen3.5:2b
  qwen3.5:2b-q8_0 )` that are absent, and there is **no verify/apply row that
  deletes unmanaged models**. So removing `qwen3.5:9b` will *not* be re-pulled
  by the next `setup.sh` run. The Ollama section also isn't a
  `docs/setup.md`-mandated surface for this change, so no doc+apply/verify pair
  is owed here.
- `/api/ps` was empty beforehand (`{"models": []}`), so the live process state
  can't lose a loaded resident.

## Evidence (verbatim, 2026-09-23, no sudo needed for the delete — it goes through
`docker exec`, which the `cj` user can reach)

```
== before: df ==
/dev/mmcblk1p1   29G   27G  2.5G  92% /
== only tag on the 9.7B blob? ==
qwen3.5:9b      9.7B
== rm ==
deleted 'qwen3.5:9b'
== after: tags ==
qwen3.5:2b-q8_0
qwen3.5:2b
qwen3.5:4b
== after: df ==
/dev/mmcblk1p1   29G   21G  8.6G  71% /
== after: /api/ps (confirm nothing was using it) ==
{ "models": [] }
```

| | before | after |
|---|---|---|
| free | 2.5 G (92 %) | **8.6 G (71 %)** |
| Ollama library | 2b, 2b-q8_0, 4b, 9b | 2b, 2b-q8_0, 4b |
| loaded (`/api/ps`) | none | none |

**Result:** 6.1 G freed. Root FS now has **~4–6 G of headroom** beyond G1's
~2–2.5 G need — G1's disk gate is **satisfied**; the only remaining open G0
item is the USB-mic hardware proof (G0-b, needs a physical part).

## Corrected size note

`/api/tags` reports `qwen3.5:4b` at **3.16 GB** (4.7 B params, Q4_K_M) — the
design doc's earlier "3.4 GB" estimate is corrected to the measured value.
Residency target at G1: 3.16 G (4b) + ~1.5 G (whisper-medium) ≈ **4.7 GB**
against **9.0 Gi** host-available — comfortably inside the design's 11 Gi
budget.

## G1-b landing — vendored + weights (2026-09-23, no-sudo; apt batch still pending operator)

- **Pinned:** `third_party/whisper.cpp` @ tag **`v1.9.4`** (= `927cfce34f31…`,
  the tree also carries `b5130`); committed in-repo as a submodule pin
  (`.gitmodules` row + gitlink). The design's vague "pinned on board" is now a
  real commit — reproducible from the repo alone.
- **External clone:** `./ebe-boilerplate` @ `e741e1c…` (`origin/main`, depth-1),
  git-ignored (board-clone dependency, never tracked — `.gitignore` entry).
- **Weights (decision recorded):** `~/voice-agent/ggml-medium.bin` —
  **1.48 G**. Path chosen: not `/var/lib/` (no sudo touch) and not the repo
  (git, never on the order of a GB).
- **Corrected the weights filename (my earlier error):** the canonical asset is
  **`ggml-medium.bin`** — `https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin`.
  The old `ggml-model-whisper-medium-*.bin` name came from the legacy
  `ggml.ggerganov.com` host; the HF repo re-named it, and whisper.cpp's own
  `models/download-ggml-model.sh` maps `medium` → `ggml-medium.bin`. Corrected
  in design §5, §8, and the gate plan G1-c.
- **Apt batch (G1-b remainder) — the ONLY operator step left before G1-c's
  build:** run this (it needs a password I cannot provide):
  ```bash
  sudo apt install -y build-essential cmake pkg-config   # libasound2-dev ONLY if build errors demand alsa.h
  ```
  `make` was already present; `espeak-ng` present (`/usr/bin/espeak-ng`).
  I will not run `sudo` (I cannot enter a password); once you run it, say the
  word and I pick up the G1-c **CMake** build of the server (v1.9.4's root
  Makefile wraps `cmake -B build && cmake --build build`) + the user unit on
  127.0.0.1:8080.

**Same-change updates:** gate plan G1-a → `[x]`, G1-b/G1-c partial `[x]`, G1-e
size corrected; gate index row updated; design §3 math + §5/§8 filenames + §6
disk note + §10 decision 1/2; project README (status, external deps, disk
bullet); top README status sub-bullet; `.gitignore` (ebe) + `.gitmodules` + the
whisper.cpp submodule pin (committed with this journal).