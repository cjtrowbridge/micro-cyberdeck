# Streaming dictation: resource and throughput analysis

Authority: operator requested a deeper resource/throughput investigation. Literature and source-code review only; no live board measurements or installations. Extends [initial research](2026-10-16_streaming-dictation-research.md).

## Hardware budget correction

Allwinner specifies **2 Cortex-A76 + 6 Cortex-A55**, up to 2 GHz, for A733. The earlier October 16 journal's 8-A76 assertion and the older design's four-A76 affinity assumption must not guide scheduling. Source: [manufacturer product specification](https://www.allwinnertech.com/index.php?a=index&c=product&id=139). Actual online CPU IDs, cpufreq policies, supported instructions, and sustained frequencies still require a board probe.

Recorded board evidence, not current measurements: September 29 had 11 GiB total RAM, 9.6 GiB available at idle, no zram used; display bridge about 63% CPU and Xvfb about 21% CPU. The process percentages ordinarily mean fractions of one logical CPU, not 84% of the eight-core machine; their core placement and sampling convention need confirmation. Under the earlier ASR+LLM test memory was much tighter. Idle RAM cannot substitute for peak working-set and memory-bandwidth measurements. [Checkpoint](2026-09-29-voice-agent-g1-status-checkpoint.md), [incident](2026-09-23-g1-reboot-incident.md).

## Quantities that decide whether live dictation works

Use **RTF = processing wall time / newly consumed audio duration**. Lower is better. RTFx in some papers is its reciprocal. A single continuous stream needs long-run RTF below 1; propose at most 0.5 under the intended foreground workload to leave recovery margin. This is a proposed engineering target, not an established device capability.

For audio arriving every C seconds, approximate serial queue growth by `Q_next = max(0, Q + processing_time - C)`. Here Q is seconds of waiting wall time, not bytes. At RTF 1.2, 60 seconds of speech takes 72 seconds of compute and leaves roughly 12 seconds of extra completion delay in this simplified model. Merely enlarging a queue preserves audio at the expense of ever-growing latency.

Latency includes capture buffering, acoustic/model lookahead, waiting for a decode slot, decode time, hypothesis stabilization, input-method delivery, and app painting. A short algorithmic chunk does not imply a short end-to-end delay. Measure first nonempty output AND first correct useful words; a recognizer emitting an early wrong fragment can win the former metric while feeling worse.

Example budget, illustrative only: 40 ms capture buffering + 320 ms effective model lookahead/chunk fill + 120 ms decode + 20 ms IPC/UI = roughly 500 ms before additional queueing/stabilization. Do not assume a model supports that 320 ms configuration.

## Candidate costs and evidence

| Candidate | Published artifact/resource evidence | Throughput conclusion |
| --- | --- | --- |
| sherpa-onnx English Zipformer 20M | INT8 encoder 41 MB, FP32 decoder 2 MB, INT8 joiner 253 KB; tokens/runtime additional. Trained on LibriSpeech. | Best initial lightweight native-streaming baseline; exact board RTF, chunk stride/lookahead, RSS and dictation accuracy unmeasured. |
| Vosk small English 0.15 | 40 MB download; Vosk estimates about 300 MB runtime memory for small models generally. | Credible low-resource comparison; validate current speech/accent/names, punctuation policy, and partial stability. |
| Nemotron 0.6B online | OpenWhispr's cited export is about 632 MB; this is not peak RSS. Encoder caches, activations, graph packing and allocator arenas add memory. | Quality challenger if the two-fast-core budget can sustain it; no verified A733 throughput found. |
| Whisper tiny/base rolling decode | Artifact size and speed depend on runtime/export/quantization. | Requires the complete repeated-window schedule to keep up, not just a fast one-off file decode. |

Sources: [Zipformer files/model provenance](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/online-transducer/zipformer-transducer-models.html), [Vosk catalog](https://alphacephei.com/vosk/models), [OpenWhispr export description](https://openwhispr.com/blog/local-streaming-speech-to-text).

**Do not treat model size as RAM usage or quantization as a universal speed multiplier.** Different exporters can duplicate weights, leave operators in FP32, or require different kernels. Compare pinned artifacts and runtime versions; record hashes, CPU flags, graph settings and real resident/high-water memory. For the small-model prototype, a proposed total incremental budget is <=512 MiB for recognizer plus dictation integration, verified rather than asserted. Larger-model budget is a separate decision after measured RSS and user workload.

### Benchmark traps found

- sherpa's Whisper tiny.en ONNX benchmark on Pi 4 reports INT8 RTF 0.547 (one thread) and 0.386 (four). That is a 16.715-second file, not rolling subsecond streaming, and is not whisper.cpp. [Benchmark](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/whisper/tiny.en.html).
- For a rolling implementation, measure `decode_time(window) / update_stride`. A five-second window refreshed every 0.5 seconds needs each decode below 0.5 seconds, preferably below 0.25 seconds with margin. Equivalent window-relative RTF thresholds are 0.1 and 0.05. Approximate repeated coverage is 10x; padding and autoregressive overhead mean actual cost is not strictly linear. Upstream whisper.cpp's sample warns and drops audio when it cannot keep up. [Implementation](https://github.com/ggml-org/whisper.cpp/blob/master/examples/stream/stream.cpp).
- A recent Nemotron compression paper's CPU runs were pinned to **32 AMD EPYC cores**, not an embedded ARM CPU. It also shows throughput depends strongly on chunk size. Useful architecture/quantization evidence, not evidence for A733 speed. [Study, sections 3.3 and 7](https://arxiv.org/html/2604.14493v1).
- Seeed's comparative page has a Pi5 value 0.172 that is **TTS**, not ASR. Its ASR finalize-only timing excludes processing during capture. Do not quote either as complete ASR throughput. Its concurrent table also does not justify transferring a multi-stream capacity claim to this board. [Benchmark definitions/results](https://github.com/Seeed-Solution/openvoicestream/blob/main/docs/performance-comparison.md).

## Runtime tuning that matters

Start with one recognizer, one stream and greedy decoding; test one versus two threads before four. Two fast cores are the main resource to protect. Verify topology before affinity; measure unrestricted scheduler placement versus a verified fast-core set, not assumed CPU IDs. Consider little-core execution only after it meets deadlines under load. More threads may reduce one decode's wall time yet increase CPU-seconds, heat and interference.

ONNX Runtime has per-session thread pools and configurable spinning; its generic defaults can use physical-core-wide parallelism. sherpa sets explicit thread counts in session creation, so do not assume it necessarily uses those generic defaults. Multi-graph models still warrant inspection of process-wide thread activity. Current sherpa source forwards explicit `SessionConfig.*` options from provider configuration; support must be checked at the selected release. Compare spinning disabled to supported bounded/default spinning, and record both idle power/CPU and deadline misses. [ORT threading](https://onnxruntime.ai/docs/performance/tune-performance/threading.html), [sherpa session source](https://github.com/k2-fsa/sherpa-onnx/blob/master/sherpa-onnx/csrc/session.cc).

Hotword support can require modified beam search in sherpa rather than greedy mode. Evaluate its accuracy benefit and cost separately; do not silently add a language model/large decoder to the small baseline. [Recognizer configuration](https://github.com/k2-fsa/sherpa-onnx/blob/master/sherpa-onnx/csrc/online-recognizer.cc).

Keep weights resident across holds to avoid storage, initialization and warmup on each press; reset utterance state instead. While inactive, stop decode work. A held-resident ASR model does not require resident Ollama or whisper-medium. Benchmark idle LLM residency separately from concurrent LLM generation; the latter consumes CPU and memory bandwidth even when models fit in RAM.

CPU-only is the reproducible starting path. Existing VIPLite demo success does not establish streaming ASR model conversion, state-cache support, or useful NPU speed. Rockchip RKNN results do not apply to the Allwinner backend.

## Audio and insertion budgets

16 kHz mono PCM16 is 32,000 B/s: 20 ms is 640 B; 40 ms 1,280 B; 250 ms pre-roll 8 KB; a 2-second bounded queue 64 KB; 60 seconds 1.92 MB. Float32 doubles these numbers. Audio transport memory/bandwidth is modest; neural working sets dominate. Keep capture independent of decode and never block the capture callback on recognition or IBus. Avoid unbounded in-memory recordings; if retaining a full utterance for recovery, declare duration and retention limits.

The Vosk microphone sample uses 8,000-frame blocks: at 16 kHz, that alone batches 500 ms. sherpa's Python sample intentionally captures 48 kHz and reads 100 ms at a time, demonstrating internal resampling. These are examples, not latency-optimal defaults. Start from the board's already proven 16 kHz mono path with 20-40 ms capture reads, then tune measured device period/overruns. Small capture reads do not change a model's fixed lookahead. [Vosk example](https://github.com/alphacep/vosk-api/blob/master/python/example/test_microphone.py), [sherpa example](https://github.com/k2-fsa/sherpa-onnx/blob/master/python-api-examples/speech-recognition-from-microphone.py).

For insertion, IBus is primarily a correctness/integration choice, not an established performance winner. Keep the engine responsive and inference outside its event loop. Publish changed partials at a proposed maximum 5-10 Hz, coalescing obsolete partials only; preserve every final segment and order. Bound preedit to the current uncommitted segment rather than rewriting an entire minute of dictation. At 8 display frames/s, successive physical-panel observations are about 125 ms apart under ideal cadence, independent of whether the app already received text. App rendering and bridge scheduling still need measurement.

Avoid spawning clipboard/typing subprocesses per word or per partial. A persistent IPC connection to the input method is the proposed baseline; a small Python adapter can call native inference without making Python itself the main neural compute cost. Measure wrapper/copy overhead before rewriting everything in C++. No measured IBus RSS or latency is available on this image.

## Staged measurements and decision rule

1. Record live baseline: CPU topology/flags/frequencies, memory available, swap in/out, display frame cadence, thermal state, storage free, and existing services. Earlier snapshots are context only.
2. Independently test scripted preedit/commit with timestamps. Verify focus changes, selection, Unicode and no accidental Control modifier. Record application compatibility.
3. Pin small Zipformer and Vosk artifacts. Measure cold load, warm first decode, complete-file RTF, and audio-paced per-chunk service times. Use 1 and 2 threads; expand only if justified. Keep initialization outside steady-state RTF and report it separately.
4. Replay the same 5/15/60-second speech clips, including pauses and technical names, at real audio rate. Repeat baseline and intended app+display load; measure VNC connected separately. Track queue age/high-water, per-chunk p50/p95/p99 service time, first-useful-partial lag, revision count, release-to-final, peak RSS/PSS, CPU-seconds/audio-second, temperature/frequency and display drops. Use monotonic timestamps; save evidence outside /tmp.
5. Evaluate Nemotron only if smaller models fail the accuracy requirement or it earns a better measured tradeoff. Compare exact model/runtime pairs; runtime chunk options are not interchangeable across exports.

Proposed acceptance: sustained RTF <=0.5 under intended load, no upward queue-age trend during 60-second speech, p95 first-useful-partial <=1 second, p95 release-to-final <=1 second, no audio loss or swap-driven stalls, and acceptable display responsiveness. Model lookahead must permit those targets. More stringent per-chunk deadlines can be evaluated separately; a p99 overrun can be absorbed by headroom, so report recovery rather than equating one overrun with permanent failure. Audio backlog threshold breach produces a visible incomplete/error result, never a silent sample drop reported as a successful transcript.

No A733 ASR RTF, insertion latency, runtime RSS, battery power or current cooling condition was measured in this research. Proposed budgets and arithmetic are not benchmark results.
