# 2026-09-19 — NPU + Ollama: the driver is already here; the plan is hybrid

## Question

Can the NPU be put to work with Ollama (the deck's local-inference path)?
Explored what that process looks like, pulled in the prior work, and updated
[docs/hardware/npu.md](../docs/hardware/npu.md) — status moved
**Not started → In progress**.

## What today's probes found (evidence in `npu.md`)

- **The kernel side is already done in the vendor image.** `CONFIG_AW_NNA_VIP=m`
  in `/boot/config-6.6.98-vendor-sun60iw2`; module loaded as **`vipcore`**
  (lsmod, refcount 0 — nothing has opened the device); `/dev/vipcore` (199,0)
  exists. The old record's "no driver is bound" is outdated.
- **The active DTB enables it.** `npu@3600000` in
  `allwinner/sun60i-a733-orangepi-zero3w.dtb` (confirmed via
  `armbianEnv.txt fdtfile=`): `compatible = "allwinner,npu"`,
  `status = "okay"`, OPs 492/852/1008 MHz. No `firmware-name`, and no NPU
  `reserved-memory` region (only `bl31`) — open question about how the
  driver provisions memory; check the kernel boot log (operator, root).
- **The userspace side is empty.** No `libVIP*.so`, no `vpm_run`, no SDK.
- **Ollama is already installed and running** (0.34.1, root):
  qwen3.5:9b + qwen3.5:4b (Q4_K_M, GGUF) via `/usr/lib/ollama/llama-server`.
  Identity anomaly noted: `ps` shows `/bin/ollama serve`, yet
  `ls /bin/ollama` / `which ollama` (non-root) don't resolve it — flagged as
  an operator re-probe, not taken as provenance.

## Third-party prior pulled in

`third_party/a733_npu_driver` (submodule, github.com/petayyyy/a733_npu_driver,
pinned at `aae9287`) — a complete LLM/VLM bring-up of the *same VIP9000 on
the same Orange Pi Zero 3W* (kernel 6.6.98-sun60iw2, VIPLite
2.0.3.2-AW-2024-08-30). Verified on that board: SmolLM2-135M/360M on NPU
(20.7/8.4 tok/s, coherent, W=32), MobileCLIP-S0 vision (22.6 ms), hybrid
SmolVLM (SigLIP on NPU + CPU LLM, ~46.5 tok/s, accurate). **Verified
failures:** Qwen-class LLMs (int16/FP16 incoherent; BF16 host-correct at
cosine 0.991 but ACUITY won't compile it — `vnn_VerifyGraph -3`), SmolLM2-1.7B
(export segfault), no KV-cache (fixed window, coherence cliff at W≥128), no
working INT8/INT4 LLM.

## Answers

- **Ollama itself has no NPU path** — its backends are CPU (llama.cpp) plus
  GPU offloads; nothing in its toolchain touches `/dev/vipcore`. An Ollama
  model on this deck runs on CPU regardless.
- **The productive architecture is hybrid,** and it's compatible with keeping
  Ollama as the language server: Ollama (CPU) serves Qwen-class models
  (≤0.5–1.5B is the realistic size alongside the deck's other workloads —
  the 9.7B deployment alone sits at ~4.4 GB RSS of 6 GB); the NPU takes
  vision encoding + tiny-language work via the VIPLite runner, freeing the
  A76 cores. CPU and NPU workloads coexist; only one NPU consumer at a time.

## Decisions / next steps

- **Documentation landed in the same change** (mandate compliance):
  `docs/hardware/npu.md` rewritten (status In progress), README Hardware
  Status row flipped, `third_party/` registered in `AGENTS.md` host layout.
- **Next concrete step: procure/install the vendor VIPLite runtime**
  (2.0.3.2-AW-2024-08-30, or current) for our bookworm userspace, then run
  the submodule's `scripts/board/a733-g0-g1-smoke.sh` — G1 success evidence
  is a demo NBG through `vpm_run` with `cid=0x1000003b` in the log. Per-board
  rebuild details: submodule `docs/07-porting-radxa-to-orangepi.md`.
- **Open (operator, root):** `dmesg | grep -i vip` (clean probe? how is NPU
  memory provisioned given the DTB gaps), `modinfo vipcore`, and the ollama
  binary identity (`/bin/ollama` anomaly).
- **Vendor tickets** (canonical gap list, if we ever need to revisit the
  Qwen-on-NPU wall): `third_party/a733_npu_driver/docs/vendor-tickets.md`.
- No `setup.sh` verify row yet — none should be added until a runtime exists
  to verify against; the G0/G1 smoke script is the natural future body.

## Bring-up executed (2026-09-19, ~22:55 local / 2026-09-20T05:55Z) — G1 green

Approved step: install the vendor VIPLite runtime and run the G0/G1 smoke.
All of it ran as user, no sudo (`/dev/vipcore` is `crw-rw-rw-`).

- **Procurement:** Allwinner's public SDK clone (github.com/ZIFENG278/ai-sdk,
  depth 1, 1.1 GB) → `~/ai-sdk`. Two runtime variants ship:
  `aarch64-none-linux-gnu` (cross-compiled) and `glibc-gcc13_2_0` (system).
  `ldd` on the glibc variant links cleanly against bookworm → that's the one
  used. Headers in both variants are the full property set, so **no**
  `-DA733_VIP_LEGACY_DEVICE_ID` / `-DA733_VIP_NO_CORE_INDEX` defines were
  needed (the older-header quirk in the submodule's Orange Pi bring-up note
  did not apply to this SDK).
- **Staging:** `~/vip/` holds `libVIPhal.so` (md5 053948dddd) +
  `libNBGlinker.so` (md5 f93ccd52) from
  `viplite-tina/lib/glibc-gcc13_2_0/v2.0`, plus the two headers.
  `vpm_run` built from the SDK example (md5 7b4a533d, direct `cc` against
  `$LIB/inc`, `-lNBGlinker -lVIPhal`); the sample's Makefile requires an
  `AI_SDK_PLATFORM` machinfo install — a one-line `cc` bypassed it. Rpath
  alone was insufficient at runtime because the binary does not NEEDED
  `libVIPhal.so` directly, so runs use `LD_LIBRARY_PATH=~/vip`.
- **First inference (G1):** `~/ai-sdk/examples/vpm_run/operator/v3/` →
  224×224×3 int8 demo network binary. Output: banner `VIPLite driver
  software version 2.0.3.1-AW-2024-08-16`, `cid=0x1000003b, device_count=1`,
  prepare 725 µs, **run 4 850 µs, `vpm run ret=0`**, exit 0.
- **Formal smoke:** `scripts/board/a733-g0-g1-smoke.sh` (submodule),
  `A733_VPM_RUN=~/ai-sdk/examples/vpm_run/vpm_run`,
  `A733_VPM_RUN_ARGS="-s sample.txt -l 1 -d 0"` → **pass=7 warn=0 fail=0**
  (g0_cpu_cores, g0_thermals, g1_vipcore, g1_viplite_libs, g1_vpm_run,
  g1_vpm_inference, g1_vip_banner). Record kept at
  `artifacts/.g0g1-logs/micro-cyberdeck-20260920T055445Z/` (git-ignored).
  The `dmesg` step was silent as non-root — expected warn, not fail.
- **The NPID trap:** every SDK sample NBG under `examples/*/model/v2/` and
  `operator/v2/` targets `0x10000016` and is rejected by the device
  (`nbglk_valid_nbg_check ... actually target=0x1000003B`). The SDK's
  `models/pegasus_setup.sh` maps it: v1=PICO_PID0XEE, v2=NANOSI_PLUS_PID0X10000016,
  **v3=NANODI_PLUS_PID0X1000003B = our A733**. Only `operator/v3/` is
  green. All real workloads must be compiled for v3.
- **ACUITY is not in this clone** (only the pegasus/ACUITY *scripts* under
  `scripts/` + `models/`, which source an `$ACUITY_PATH` toolkit that ships
  separately). The ACUITY compiler itself (vendor Docker `ubuntu-npu` per
  the submodule) is the next procurement before SmolLM2/MobileCLIP-S0 NBGs
  can be built. Host-side: ACUITY runs on a modern Ubuntu + Docker box, not
  on the deck.
- **Docs updated in the same change:** `docs/hardware/npu.md` (userspace
  bullet, re-probe block, portends section), README + hardware README NPU
  rows.

## Next steps (post-G1)

1. **Procure the ACUITY toolchain** on a development box (Docker image
   `ubuntu-npu` / Vivante SDK per the submodule docs), export a v3 NBG for a
   first real model — SmolLM2-135M int16 W=32 is the cheap first proof
   (~281 MB NBG, ~400 MB free RAM needed; the deck has 11 Gi).
2. Rebuild `npu_lm_runner` against `~/vip` headers if/when the persistent
   runner is wanted (submodule `scripts/board/build-npu-lm-runner.sh`; this
   SDK's headers should not need the legacy defines).
3. Operator (root, still open): `dmesg | grep -i vip`, `modinfo vipcore`,
   ollama binary identity (`/bin/ollama` anomaly).

## Housekeeping notes

- `~/ai-sdk` (1.1 GB) and `~/vip` (320 KB) live in `$HOME`, outside the
  repo. If the bring-up sticks, either leave them as-is (deck-local,
  documented in npu.md) or distill to a fetched artifact; a `setup.sh`
  provision step for the runtime lands only when ACUITY-sourced NBGs are
  part of the story.
- My change set (submodule + 5 doc/journal files) commits separately from
  the in-flight fan/setup change set.

## Addendum (2026-09-19, evening) — the `/bin/ollama` anomaly resolved: it's Docker

Open operator question (3) is answered. Evidence (root + docker group):

- `cat /proc/<pid>/cgroup` of the `ollama serve` process (pid 1450, root) →
  `0::/system.slice/docker-fb9178…scope` — the process runs **inside Docker**,
  not on the host.
- `docker ps` → container **`ollama`**, image `ollama/ollama`, created
  2026-09-17 (same day the 9b/4b models were pulled), `0.0.0.0:11434->11434/tcp`.
  `docker inspect` mount: `/var/lib/docker/volumes/ollama/_data →
  /root/.ollama` (the model store lives in a named docker volume).
- `readlink /proc/<pid>/exe` → `/usr/bin/ollama` is the *container's* binary;
  the host has no ollama binary at any standard path — `which ollama` / `ls`
  were always empty because there was never anything to find.
- **Consequence hit live:** the new setup.sh §3g (verify-or-pull of
  `OLLAMA_MODELS` = `qwen3.5:2b`, `qwen3.5:2b:q8_0`) first failed to load the
  models because its pull path looked only for a host CLI (API checks all
  passed; registry egress was fine — `manifests/2b` → 200). Fixed: pulls now
  try the host CLI first, then `docker exec ollama ollama pull <name>`.
  A second, simpler bug: the desired set was written
  `qwen3.5:2b:q8_0` (two colons) — the registry answers
  `400 Bad Request: invalid model name` once name:tag is split by more
  than one colon. Probed `manifests/2b-q8_0` → 200: the tag is
  `2b-q8_0` (name `qwen3.5`), so `OLLAMA_MODELS` is
  `( qwen3.5:2b qwen3.5:2b-q8_0 )`.

**Blob identity (API probe, 06:14Z+1):** `qwen3.5:2b` IS the Q8_0 build —
`parameter_size 2.3B`, `quantization_level Q8_0`, 2.7 GB, digest
`324d162be6ca…`. `qwen3.5:2b-q8_0` tags the **same blob** (identical
digest), so its pull is a zero-download manifest write; the `:9b`/`:4b`
tags are the Q4_K_M builds. ("2b" and "2b-q8_0" are not two quantizations
here — same model under two tags; keeping both names preserves model-ref
stability.)

**Live proof (apply → verify, same board, root):** after `ollama rm
qwen3.5:2b-q8_0` → plan mode: `[DRIFT] ollama-models — absent from the local
library (apply pulls): qwen3.5:2b-q8_0` + `[FAIL]` in verify. Apply mode:
`[setup] pulling qwen3.5:2b-q8_0 via docker (this can take a while)` →
`[setup] pulled   qwen3.5:2b-q8_0` → `[PASS] ollama-models — qwen3.5:2b
qwen3.5:2b-q8_0 present via http://localhost:11434` → `RESULT: CONVERGED`.
Second apply run: no pulls, `CONVERGED` (idempotent). No reboot required
(model library is live state, not boot-time state).

Docs updated in the same change (`npu.md` identity bullet + re-probe
block, `docs/setup.md` scope row + verify row 13).