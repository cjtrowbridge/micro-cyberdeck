# NPU (3 TOPS INT8)

> Status: **In progress** (mirrors the README table)

## What it is

The Allwinner A733 (sun60iw2) integrated neural-processing unit — the
**Vivante VIP9000**, single core, ~1.0 GHz, ~3 TOPS at INT8 (cid
`0x1000003b`, per the submodule knowledge base below). The piece of silicon
the deck's "run local models" promise (README, "Recursive Self-Improvement")
is ultimately meant to lean on.

In the SoC layout it is: power domain `pd_npu@4`, power rail consumer
`npu` (see [pmic-axp8191.md](pmic-axp8191.md)), and its own thermal zone
(see [soc-thermal.md](soc-thermal.md)).

## What we know

- **The kernel driver is present and bound.** The vendor kernel
  (6.6.98-vendor-sun60iw2) defines it as a module (`CONFIG_AW_NNA_VIP=m`,
  `CONFIG_NNA_VIP2=y`) and the module is loaded as **`vipcore`**, with
  refcount 0 — nothing has opened the device yet.
- **The device node exists: `/dev/vipcore` (199,0).** Character device,
  root:root, `crw-rw-rw-`. This is the single access point to the NPU —
  the vendor stack restricts it to **one consumer at a time**.
- **The active DTB enables it.** `npu@3600000` in
  `allwinner/sun60i-a733-orangepi-zero3w.dtb`: `compatible =
  "allwinner,npu"`, `status = "okay"`, OP table at 492/852/1008 MHz,
  `npu-supply = <0x66>`, power domain 0x13/0x04. **Notably it carries no
  `firmware-name` property**, and `reserved-memory` holds only the `bl31`
  region — no dedicated NPU carve-out is declared.
- **RAM observation:** `free -h` reports **11 Gi total** on this board,
  versus the 6 GB figure in the deck specs — unexplained, but it means the
  "enough RAM for an NPU test" question resolves in favor (see below).
- **The vendor userspace stack is installed (2026-09-19) and G1 is green.**
  From Allwinner's public SDK (github.com/ZIFENG278/ai-sdk, 1.1 GB clone at
  `~/ai-sdk`): the **glibc-matched** prebuilt libs
  (`viplite-tina/lib/glibc-gcc13_2_0/v2.0`, VIPLite driver software
  2.0.3.1-AW-2024-08-16) are staged in `~/vip` (`libVIPhal.so` md5
  053948dddd, `libNBGlinker.so` md5 f93ccd52) and `vpm_run` is built from
  the SDK example (md5 7b4a533d). First live inference through
  `/dev/vipcore` succeeded on 2026-09-19: the SDK's v3-target demo NBG
  (`~/ai-sdk/examples/vpm_run/operator/v3/network_binary.nb`, 224×224 int8
  input) ran in 4.85 ms with `vpm run ret=0` and banner `cid=0x1000003b`;
  the submodule's G0/G1 smoke test passed 7/7 (record in
  `artifacts/.g0g1-logs/micro-cyberdeck-20260920T055445Z/`). Two things to
  keep in mind: (a) the runtime is one vendor patch older than the
  submodule's bring-up record (2.0.3.1-AW-2024-08-16 vs
  2.0.3.2-AW-2024-08-30) — functionally equivalent for G1, no reason to
  downgrade-resolve further; (b) the ACUITY ONNX→NBG compiler is **not**
  in this SDK clone — the submodule documents it as a vendor Docker
  `ubuntu-npu` image, still to be procured before we can build our own
  NBGs. Ollama 0.34.1 *is* installed — as a root **Docker container**
  (image `ollama/ollama`, name `ollama`, 11434 published; model volume
  `/var/lib/docker/volumes/ollama/_data` ↔ `/root/.ollama`; no host CLI);
  qwen3.5:9b + qwen3.5:4b (Q4_K_M, CPU-only) plus the setup.sh-managed
  `OLLAMA_MODELS` (qwen3.5:2b and the `qwen3.5:2b-q8_0` alias tag of the
  same blob — 2.3 B Q8_0, 2.7 GB, digest `324d162be6ca…`) — see the "and
  the Ollama question" subsection below.
- **SDK sample models mostly do not target this part.** Every shipped NBG
  in `examples/*/model/v2/` (lenet, resnet50, yolact, yolov5) and
  `operator/v2/` carries `binary target=0x10000016` and fails with
  `nbglk_valid_nbg_check … actually target=0x1000003B`. Per the SDK's own
  `models/pegasus_setup.sh`, the simulator/config naming is
  v1 = VIP9000PICO_PID0XEE, v2 = VIP9000NANOSI_PLUS_PID0X10000016,
  v3 = VIP9000NANODI_PLUS_PID0X1000003B — our A733 is the **v3** part, so
  `operator/v3/` is the only green sample. Anything we run for real
  (SmolLM2, MobileCLIP-S0) must be compiled for v3 via ACUITY.
- **A complete third-party prior exists for this exact board.**
  `third_party/a733_npu_driver` (submodule, commit `aae9287`, pinned) is a
  fully documented LLM/VLM bring-up of the same VIP9000 on the same
  Orange Pi Zero 3W (kernel `6.6.98-sun60iw2`, VIPLite
  2.0.3.2-AW-2024-08-30): working configs, exhaustive blocker list, and a
  hand-off knowledge base at
  `third_party/a733_npu_driver/docs/import_chat.md`. Their entry points:
  `docs/02-board-bringup.md` (bring-up) and
  `scripts/board/a733-g0-g1-smoke.sh` (G0/G1 smoke test).

### Verified working configurations (third-party evidence, same board)

From `third_party/a733_npu_driver/docs/import_chat.md` + `docs/RESULTS.md`
(measured on an Orange Pi Zero 3W, so directly transferable; the NBG
network binaries are binary-compatible across A733 boards):

| Path | What | Numbers |
|---|---|---|
| NPU LLM | SmolLM2-135M int16, W=32 fixed window | 20.7 tok/s, coherent; NBG 281 MB |
| NPU LLM | SmolLM2-360M int16, W=32 fixed window | 8.4 tok/s, coherent; NBG 673 MB |
| NPU vision | MobileCLIP-S0 encoder | 22.6 ms/frame, cosine 0.99996 |
| Hybrid VLM | SmolVLM: SigLIP on NPU + LLM on CPU (2×A76) | vision 5.94 s at 0 CPU, LLM ~46.5 tok/s, accurate |
| CPU LLM | Qwen2.5-0.5B Q8_0, 2×A76 (`taskset`) | 18.0 tok/s, ~1.1 GB RSS |
| CPU LLM | Qwen2.5-1.5B Q4_K_M | 8.5 tok/s — their recommended default |
| CPU VLM | SmolVLM-256M Q8_0 | 52.6 tok/s, 634 MB RSS |

**Hard limits (their verified failures — do not re-attempt without a new
toolchain/vendor fix):**

- **Qwen-class LLMs do not work on this NPU.** Every quantization tried:
  int16/FP16 incoherent (Qwen's activation outliers exceed the range),
  BF16 passes host checks (cosine 0.991) but ACUITY cannot compile it
  (`vnn_VerifyGraph -3`); SmolLM2-1.7B export segfaults to a 0-byte NBG.
- **No KV-cache** — static-shape NBGs, so chat means a fixed window and
  full-window recompute per token (O(W²)); coherence holds at W≤64 and
  collapses at W≥128.
- **No working INT8/INT4 LLM path** (PCQ exports but incoherent; per-channel
  int16 rejected by the toolchain).
- The toolchain is vendor/VeriSilicon: ONNX → ACUITY (Docker
  `ubuntu-npu`) → NBG → VIPLite (`/dev/vipcore`) runner.

### And the Ollama question

**Ollama has no path to this NPU.** Its execution backends are CPU
(llama.cpp) plus its GPU offloads (CUDA/ROCm/…); there is no Allwinner
VIP9000/VIPLite plugin, and nothing in Ollama's toolchain (GGUF →
llama.cpp) addresses `/dev/vipcore`. A GGUF/Ollama model on this deck runs
**on the CPU only** — which is fine, it is just not offloading. (The
containerized `ollama serve` on this deck is granted no NPU device at all,
so the conclusion holds unchanged.)

The productive architecture (the third-party's conclusion, and the only
one compatible with Ollama staying in the stack) is **hybrid**:

- **Ollama (CPU) keeps serving the Qwen-class models** it already runs —
  the current 9.7B Q4_K_M deployment sits at ~4.4 GB RSS; with 11 Gi
  observed total (see the RAM note above) it coexists fine with a vision
  NBG, so a realistic *serving* size (≤0.5–1.5B) is a latency choice, not
  a memory one.
- **The NPU earns its keep on vision encoding and tiny-language work**
  (MobileCLIP-S0 encoder, SmolLM2-135M), driven by the VIPLite runner —
  *not* by Ollama — keeping both A76 cores free for the rest of the deck.

## How we know

Re-probe (all bare, no sudo except where noted):

```bash
ls -l /dev/vipcore                      # crw-rw-rw- root root 199,0
lsmod | grep -iE 'vip|npu'              # vipcore  266240  0
grep -E 'AW_NNA_VIP|NNA_VIP2' \
  /boot/config-$(uname -r)              # =m / =y (6.6.98-vendor)
fdtfile=$(grep fdtfile /boot/armbianEnv.txt | cut -d= -f2)
dtc -I dtb -O dts "/boot/dtb-6.6.98-vendor-sun60iw2/$fdtfile" \
  | grep -A14 'npu@3600000'             # status = "okay"; no firmware-name; no npu reserved-memory
ls -l ~/vip ~/ai-sdk/examples/vpm_run/vpm_run   # staged runtime + harness (md5s above)
cd ~/ai-sdk/examples/vpm_run/operator/v3    # only SDK sample that targets this SoC
LD_LIBRARY_PATH=~/vip ../vpm_run -s sample.txt -l 1 -d 0
# → "VIPLite driver software version 2.0.3.1-AW-2024-08-16"
#   "cid=0x1000003b, device_count=1" … "vpm run ret=0"   (2026-09-19, exit 0)
curl -s localhost:11434/api/version     # Ollama 0.34.1 (CPU-only backends)
curl -s localhost:11434/api/tags        # qwen3.5:9b, :4b, :2b(-q8_0) (GGUF, CPU)
docker ps --format '{{.Names}} {{.Image}}'   # ollama  ollama/ollama (root; no host binary)
cat /proc/$(pgrep -f 'ollama serve' | head -1)/cgroup   # docker-*.scope (root)
```

- Ollama deployment identity — **resolved 2026-09-19** (root + docker
  group): the `ollama serve` process lives in a **Docker container**
  (`/proc/<pid>/cgroup` → `0::/system.slice/docker-<id>.scope`; `docker ps`
  → name `ollama`, image `ollama/ollama`, 11434 published; `docker inspect`
  → `/var/lib/docker/volumes/ollama/_data → /root/.ollama`).
  `readlink /proc/<pid>/exe` → `/usr/bin/ollama` is the *container's* path;
  the host has no ollama binary at all — why `which ollama` never resolved
  here (the open re-probe above is closed). Pull models with
  `docker exec ollama ollama pull <name>` — setup.sh §3g does exactly that
  when no host CLI resolves.

- Kernel boot log (operator, root): `dmesg | grep -i vip` — expected to show
  the `vipcore` probe; check what it says about memory/clock bring-up
  (open question below).
- Prior knowledge (third-party, measured on the identical board):
  `third_party/a733_npu_driver` at commit `aae9287` — its
  `docs/import_chat.md`, `docs/RESULTS.md`, `docs/blockers.md`, and
  `docs/vendor-tickets.md`.
- G1 live record (this board, 2026-09-19 local): `artifacts/.g0g1-logs/micro-cyberdeck-20260920T055445Z/`
  (the dir's stamp is UTC — the board's clock is PDT).
  (smoke.log, summary.env: pass=7 warn=0 fail=0), invocation
  `LD_LIBRARY_PATH=~/vip vpm_run -s sample.txt -l 1 -d 0` from
  `~/ai-sdk/examples/vpm_run/operator/v3/`.
- Earlier "present and powered" evidence (`npu` thermal zone, PMIC rail
  consumer) stands — see [soc-thermal.md](soc-thermal.md),
  [pmic-axp8191.md](pmic-axp8191.md).

## What it portends

- **The gap moved from kernel to userspace to toolchain.** The earlier
  record said "no driver is bound"; that is now doubly outdated. Everything
  on the board side works: `vipcore` bound, DTB `okay`, runtime installed,
  **G1 green on this deck** (2026-09-19: demo NBG through `vpm_run`,
  `ret=0`, `cid=0x1000003b`, smoke test 7/7 — see
  [Exploration path](#exploration-path-what-needs-to-happen) for the rest).
- **Open questions (operator, root):** (1) does the kernel log confirm
  a clean probe, and how does the driver obtain its memory when the DTB
  declares neither `firmware-name` nor an NPU `reserved-memory` region —
  operator probe, root; (2) whether `modinfo vipcore` (root) names firmware
  expectations; (3) whether a newer VIPLite/ACUITY release un-blocks
  Qwen-class export — the vendor tickets in the submodule's
  `docs/vendor-tickets.md` are the canonical list of known gaps.
- **Plan every "local model" design around the hybrid split:** Ollama on
  CPU for language at useful sizes; NPU for vision encode + tiny-language;
  the two coexist (CPU inference can run alongside NPU workloads) and the
  NPU never blocks `ollama serve` for long. Only one NPU consumer at a
  time on `/dev/vipcore`.
- **Thermal:** the NPU is memory-bandwidth-limited (~6 GB/s effective on
  the 32-bit LPDDR5 bus), so sustained NPU use is DRAM/thermal traffic more
  than compute heat — but the `npu` thermal zone
  ([soc-thermal.md](soc-thermal.md)) remains the early-warning sensor for
  when this row becomes active, and fan margin ([fan.md](fan.md)) should
  account for simultaneous CPU + NPU decode.
- **No `setup.sh` verify row** covers it, and none should be added until a
  runtime exists to verify against (today the probeable surface is the node
  + module in the re-probe list above). The submodule's G0/G1 smoke script
  is the natural future verify-row body once VIPLite lands.
- **Memory headroom is the practical constraint**, not the NPU: the current
  Ollama deployment (9.7B Q4_K_M, ~4.4 GB RSS) fits today because the board
  reports **11 Gi** total (`free -h`, 2026-09-19) — NBG loads of 0.3–1 GB
  plus runner RSS have ample room, even alongside a small model. (A
  standing anomaly: the deck's spec/docs say 6 GB LPDDR5 but the system
  reports 11 Gi — see the re-probe notes; worth a one-line confirmation
  before anyone plans RAM budgets off the spec.)

## Exploration path — what needs to happen

Board state after G1: the **runtime is installed and proven**, but every
model the deck would actually use must be *compiled for this NPU* first —
the only green artifact on the board today is the SDK's v3 sample network
(the NPID trap above). The compiler lives **off-board**; the deck is a
runtime, not a build machine.

1. **Host toolchain (x86 box with Docker):**
   - the ACUITY image `ubuntu-npu:v2.0.10.1` comes from Allwinner's
     prebuilt archive `docker_images_v2.0.x.zip` (~11 GB, ACUITY 6.30.22
     inside), loaded with `docker load` per the submodule's
     `docs/01-setup-host.md` (which also lists a plain `docker pull` as
     the alternative);
   - a host-side `ai-sdk` checkout is also needed (the submodule's host
     setup clones the *same* repo — we already have `~/ai-sdk` on the
     deck; the host clone is what the converter scripts source);
   - ~30 GB free disk for ONNX sources + NBG packages.
2. **First real NBG:** run the submodule's
   `scripts/host/convert_onnx_to_nbg.sh` — for the A733 its defaults are
   already correct (`--image ubuntu-npu:v2.0.10.1`,
   `--target VIP9000NANODI_PLUS_PID0X1000003B` = v3 = our `0x1000003b`),
   i.e. ONNX + calibration dataset in, v3 NBG package out. This is gate
   `G2` in the submodule's `docs/roadmap.md`, where LeNet/Inception-v1
   int16/uint8 conversion already passed on identical hardware.
3. **Run it on the deck:** copy the NBG over, stage `sample.txt`
   (`[network]` + `[input]`) next to it, run with
   `LD_LIBRARY_PATH=~/vip vpm_run -s sample.txt -l 1 -d 0` exactly as in
   the re-probe block. Success = exit 0 + `cid=0x1000003b` + sane output
   (compare against a CPU inference of the same ONNX — the submodule's
   host oracle scripts exist for that).
4. **The working workloads in order of value:** SmolLM2-135M int16 W=32
   (cheapest LLM proof, ~281 MB NBG, 20.7 tok/s), SmolLM2-360M, then
   MobileCLIP-S0 (22.6 ms/frame) — the exact configs are in the
   "verified" table above, so expectations are known before we spend a
   cycle; the persistent runner (`npu_lm_runner`) is only wanted once one
   of these lands.
5. **Do not re-explore (vendor-gated, failed before):** Qwen-class NPU
   export, INT8/INT4 LLMs, SmolLM2-1.7B — the hard-limits list above is
   the stop-list.
6. **Re-instrument on reboot/re-flash:** the submodule's
   `scripts/board/a733-g0-g1-smoke.sh` (record: pass/warn/fail counts in
   `summary.env`); once the ACUITY path solidifies, that smoke output is
   the body of the future `setup.sh` verify row.

## References

- Prior work (pinned submodule): `third_party/a733_npu_driver`
  (github.com/petayyyy/a733_npu_driver, commit `aae9287`) —
  [`docs/import_chat.md`](../../third_party/a733_npu_driver/docs/import_chat.md),
  [`docs/02-board-bringup.md`](../../third_party/a733_npu_driver/docs/02-board-bringup.md),
  [`docs/07-porting-radxa-to-orangepi.md`](../../third_party/a733_npu_driver/docs/07-porting-radxa-to-orangepi.md),
  [`docs/RESULTS.md`](../../third_party/a733_npu_driver/docs/RESULTS.md),
  [`docs/blockers.md`](../../third_party/a733_npu_driver/docs/blockers.md),
  [`docs/vendor-tickets.md`](../../third_party/a733_npu_driver/docs/vendor-tickets.md),
  [`docs/01-setup-host.md`](../../third_party/a733_npu_driver/docs/01-setup-host.md) (ACUITY image source + host setup),
  [`docs/roadmap.md`](../../third_party/a733_npu_driver/docs/roadmap.md) (gate log G0–G7)
- Conversion (submodule, host-side): `scripts/host/convert_onnx_to_nbg.sh` —
  defaults already target this part (`--image ubuntu-npu:v2.0.10.1`,
  `--target VIP9000NANODI_PLUS_PID0X1000003B`)
- Session: `journal/2026-09-19-npu-ollama-path.md`