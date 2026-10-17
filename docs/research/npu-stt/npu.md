# NPU investigation: corrected contracts and stopping points

Read [acquisition](acquisition.md). Start from the [host NPU record](../../hardware/npu.md) and [historical live journal](../../../journal/2026-09-19-npu-ollama-path.md), then [pinned import handoff](sources/npu-prior/docs/import_chat.md), [roadmap](sources/npu-prior/docs/roadmap.md), [results](sources/npu-prior/docs/RESULTS.md), [blockers](sources/npu-prior/docs/blockers.md). Numeric results from the external project are evidence about that configuration, not benchmarks of this deck today.

## What is already known, and where the prior conflicts

- This host previously ran a v3 SDK demo with VIPLite 2.0.3.1-AW-2024-08-16, cid `0x1000003b`. The external board used 2.0.3.2-AW-2024-08-30. Don't replace working libraries just to make version strings match.
- Host's loaded module was `vipcore` (`CONFIG_AW_NNA_VIP`), while the external setup prose suggests `sunxi_npu`. Inspect the running kernel/config/module paths; do not issue an assumed modprobe or unload a live driver.
- Roadmap declares vision resolved by V2d but retains older text saying a V2 retry would unlock vision. [Final V2d report](sources/npu-prior/reports/v2d-vlm-npu-mmproj-glue.md) wins for that historical result. This is vision evidence, not STT acceleration. Its NPU preparation alone took 12.4 seconds, reinforcing load-once operation.
- "No KV-cache" is a conclusion about their LLM stack, not proof that a fixed-shape ASR graph with explicit cache input/output tensors cannot run. Feasibility still depends on operator/dtype support and copies.
- `int16` here is dynamic fixed point, not FP16. Quantization conclusions for Qwen do not transfer automatically to ASR. Existing Qwen/BF16/hybrid-table/1.7B dead ends remain outside this bounded pass.

## M1.1: revalidate existing state

Run from the host repository root on the deck, only when no other NPU consumer is using it. Check `fuser -v /dev/vipcore` when available; lack of permission is unknown ownership, not permission to kill jobs. Capture module/device/library identity first. `dmesg` and kernel logs require operator root; do not infer a clean probe from empty non-root output.

Historical paths below require existence checks. `sample.txt` resolves input/network files relative to the current working directory:

```bash
task_repo=$(pwd -P)
test -x "$HOME/ai-sdk/examples/vpm_run/vpm_run"
test -f "$HOME/vip/libVIPhal.so"
test -f "$HOME/ai-sdk/examples/vpm_run/operator/v3/sample.txt"
```

The historical successful run used the v3 directory and a nearby sample descriptor. If the `sample.txt` check fails, inspect the SDK tree's descriptor locations and relative `network_binary.nb` path; don't substitute the v2 graph. Keep execution in a writable evidence/staging directory if the tool writes outputs.

Example after resolving the actual descriptor/CWD (adapt paths, not the target):

```bash
task_smoke="$task_repo/third_party/a733_npu_driver/scripts/board/a733-g0-g1-smoke.sh"
cd "$HOME/ai-sdk/examples/vpm_run/operator/v3"
A733_LOG_ROOT="$task_repo/artifacts/npu-stt/smoke" \
A733_SEARCH_DIRS="$HOME/vip $HOME/ai-sdk/examples/vpm_run" \
A733_VPM_RUN="$HOME/ai-sdk/examples/vpm_run/vpm_run" \
A733_VPM_RUN_ARGS='-s sample.txt -l 1 -d 0' \
LD_LIBRARY_PATH="$HOME/vip${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
timeout 60 bash "$task_smoke"
```

These scripts write logs and run inference; they are not pure read-only audits. Preserve stdout/stderr, exit code, complete summary and actual demo output. Timeout is failure/incomplete, never a successful smoke. The smoke searches libraries and can alter its own LD_LIBRARY_PATH based on the first match; restrict search paths and verify the recorded selected library directory. Its exit 0 can include warnings/no inference; its banner check accepts generic VIPLite strings. Explicitly require the real run, `vpm run ret=0`, the intended cid/target, sane output and evidence—not just pass totals. The script's CPU-count/thermal-readable checks do not prove sustained thermal safety.

## M1.2: compiler control before ASR

Read the actual [conversion wrapper](sources/npu-prior/scripts/host/convert_onnx_to_nbg.sh) before running. It finds its repository root from its own path; copying it into an arbitrary folder changes its expected workspace. Run it from the populated pinned prior checkout, or create a host-owned adapted wrapper with explicit workspace paths. Its SDK helper path is `work/ai-sdk/ZIFENG278-ai-sdk/scripts`, outputs are `work/model-packages/<name>/<quant>/` relative to that prior.

Corrections to the upstream setup examples:

- The copied SDK has no top-level Makefile. The prior's `build-ai-sdk.sh` requires one and therefore cannot build this exact SDK as-is. Use the established host-built binary or inspect the SDK subdirectory Makefiles and create a narrow recorded build recipe. `examples/vpm_run/Makefile` references platform staging/include/lib paths; a bare top-level `make` is not validated.
- The converter copies only the supplied ONNX plus the special `<onnx-path>.data` sidecar when present. Arbitrarily named external-data payloads may be omitted. Inspect all external initializer references, stage every payload, and preserve basename resolution before import.
- It removes/recreates its model staging directory and empties the output package. Use a unique task-owned model name, resolved paths and preserved previous evidence; never point it at user artifacts. Its internal removals are not authorization to run unchecked shell deletion commands.
- Calibration entries must be paths relative to dataset.txt; each row must align with the exact graph input order. Audio encoder features and caches are tensor data, not RGB images. The wrapper patches image metadata only when all entries are `.npy`; inspect generated inputmeta for category/channel reversal/normalization before trusting quantization.
- Correct package filenames are `host_output_0.txt` and `host_output_0.raw.tensor`, not the prose's `output_0.txt`. Board output naming comes from the runner and must be inspected.
- [compare_outputs.py](sources/npu-prior/scripts/host/compare_outputs.py) accepts **positional** `golden board`; no `--golden`, `--board` or `--output-json` options. It prints metrics and returns 0 even when similarity is poor. Add a fixture-specific acceptance gate outside it; do not equate process success with numerical or semantic correctness.

For a known-good **ONNX** control, the [Inception report](sources/npu-prior/reports/g2-acuity-inception-v1.md) is appropriate; [LeNet](sources/npu-prior/reports/g2-acuity-lenet.md) is originally Caffe and cannot be passed unchanged to an ONNX-only wrapper. Preserve actual preprocessing and known inputs. Compiler availability, reference conversion and oracle comparison are separate gates. The hard-coded v3 mapping is shown in [SDK pegasus_setup.sh](sources/ai-sdk/scripts/pegasus_setup.sh).

## M1.3: streaming encoder feasibility is a multi-tensor problem

Inspect the actual acquired encoder with ONNX and ONNX Runtime, using a declared ignored host environment. Report every input/output name, fixed/dynamic shape, dtype, custom metadata, external-data file and operator inventory. Onnxruntime `InferenceSession(..., providers=['CPUExecutionProvider'])`, `get_inputs()`, `get_outputs()`, and `get_modelmeta().custom_metadata_map` expose the runtime ABI; `onnx.load(..., load_external_data=False)` exposes graph nodes and external tensor references. Do not run a model before recording its artifact identity.

The saved [Zipformer implementation](sources/sherpa-onnx/sherpa-onnx/csrc/online-zipformer-transducer-model.cc) reads `T`, `decode_chunk_len`, per-encoder dimensions/left context and constructs seven cache groups per encoder stack. These include int64 cached lengths plus floating cached averages, keys, values and convolution history. The source establishes a required pattern, **not the uninspected selected model's exact dimensions**. A Zipformer2 export may have a different ABI; match `model_type` and dispatch from [model creation](sources/sherpa-onnx/sherpa-onnx/csrc/online-transducer-model.cc).

For the candidate, write an ABI table: feature tensor; every cache input; corresponding cache output; dtype; shape; ONNX order versus VIPLite dimensions/layout; bytes; quantization scale/zero-point or fixed-point position; initial value; update rule. Quantized ONNX and NBG formats are not interchangeable. The prior records that QDQ scales were ignored by ACUITY; prefer an FP32 graph for vendor calibration with a matched CPU reference, rather than assuming an already INT8 ONNX can be directly imported correctly.

The [persistent LLM runner](sources/npu-prior/scripts/board/npu_lm_runner.c) explicitly rejects networks with other than one input and one output and implements token/logit behavior. It is **not an ASR runner**. The saved [vpm_run source](sources/ai-sdk/examples/vpm_run/vpm_run.c) demonstrates querying multiple buffers, quantization properties and cache flush/invalidation; the [matched VIPLite header](sources/ai-sdk/viplite-tina/lib/glibc-gcc13_2_0/v2.0/inc/vip_lite.h) is the API reference. Live multi-chunk use needs a narrow persistent encoder adapter or a small recognizer backend change; sherpa's CPU/RKNN/provider switch does not provide a ready-made Allwinner VIPLite backend.

A feasible bounded experiment keeps features, decoder/joiner and endpoint logic on CPU, loads/prepares the encoder once, and explicitly carries its cache outputs into the next chunk. Keep each stream's state isolated. If int64 state or an unsupported op cannot cross the target ABI without a justified equivalent transformation, log that incompatibility and select CPU; don't cast away state or strip caches to make compilation pass. A constant-zero-state first chunk is insufficient calibration or validation: use representative multi-chunk features and evolving caches. CPU oracle traces need matching frontend/chunk boundaries and all recurrent outputs, followed by transcript checks.

Stop at the plan's effort/attempt limits. Do not substitute repeated full-utterance NPU decoding for genuine streaming. Select A only if full-pipeline output/latency/CPU savings meet the plan; otherwise record B/C and proceed. Temporary experimental model load needs no setup.sh edit; persistent installed behavior does. Never promote an experimental NPU path on smoke alone.
