# CPU recognizer contract and NPU fallback

Read [acquisition](acquisition.md) and [NPU guide](npu.md). These contracts describe the implementation to build; snippets are illustrative calls against the saved sources, not tested product code. CPU STT proceeds after any NPU branch. The existing whisper-server/Ollama voice-assistant chain is not a prerequisite.

## Keep a small, explicit backend interface

Use one resident model and one worker-owned active utterance. Proposed calls:

```text
begin(utterance_id, destination_generation)
feed(utterance_id, pcm16_16khz_mono, first_sample_index)
finish(utterance_id) -> drain accepted audio and model tail
cancel(utterance_id) -> discard uncommitted output
events: partial(segment_id, revision, full_text)
        final(segment_id, final_text)
        ended | cancelled | failed(reason, audio_position)
```

Full partial hypotheses replace one segment; they are not appended strings. Keep segment and utterance identities independent of editor focus IDs. Filter stale events by generation on the receiving side. Native decoding runs off the main/UI event loop; it owns the model state and preserves monotonic sample indices. No concurrent mutation of a stream. The CPU and NPU implementations must implement the same boundaries; backend selection is configuration, not an invitation to rerun already committed text.

## First CPU baseline: sherpa Zipformer 20M

Unpack the model into an ignored task directory; retain only selected graph paths in configuration and record tokens/graphs separately. The documented 20M selection is:

```text
encoder-epoch-99-avg-1.int8.onnx
decoder-epoch-99-avg-1.onnx
joiner-epoch-99-avg-1.int8.onnx
tokens.txt
```

Verify names after extraction. Do not mix tokenizer/decoder/joiner from a different model. Text case/punctuation and number formatting are model behavior, not a renderer promise. Preserve raw output in evaluation, then apply explicitly declared deterministic formatting if selected; no LLM is needed in the live path.

Read local examples [microphone](sources/sherpa-onnx/python-api-examples/speech-recognition-from-microphone.py), [endpoints](sources/sherpa-onnx/python-api-examples/speech-recognition-from-microphone-with-endpoint-detection.py), [file decode/flush](sources/sherpa-onnx/python-api-examples/online-decode-files.py), and [C API](sources/sherpa-onnx/sherpa-onnx/c-api/c-api.h). Python proof uses:

```python
recognizer = sherpa_onnx.OnlineRecognizer.from_transducer(
    tokens=tokens_path, encoder=encoder_path, decoder=decoder_path,
    joiner=joiner_path, sample_rate=16000, feature_dim=80,
    num_threads=1, decoding_method="greedy_search", provider="cpu",
)
stream = recognizer.create_stream()
stream.accept_waveform(16000, samples_float32)
while recognizer.is_ready(stream):
    recognizer.decode_stream(stream)
text = recognizer.get_result(stream)
```

Convert PCM16 to normalized float32 once (`samples / 32768.0` with proper signed little-endian interpretation), not once per rolling history. Respect any runtime resampling if device format differs. The microphone example intentionally uses 48 kHz/100 ms reads; that is demonstration setup, not our 20-40 ms capture target. Prove actual capture without overruns before tuning inference.

`is_ready` reflects the model's own feature/chunk/lookahead contract. Smaller capture reads do not create shorter model chunks. Read metadata/ABI as described in npu.md; document the actual model's minimum viable latency. Do not arbitrarily change export chunk settings in runtime config.

At an internal detected endpoint: obtain text, emit one final segment, then `recognizer.reset(stream)` according to the example. Continue recording during the physical hold. Endpoint timing controls commit stability; it must not stop capture or wait until release to show preedit. Default example silence thresholds (2.4/1.2 seconds) are illustrative and can dominate stable-text latency.

At physical release: stop accepting new capture after a recorded boundary, finish the accepted queue, add only the selected model's justified tail padding, call `stream.input_finished()`, decode while ready, and emit the remaining segment once. The file example adds **0.66 seconds of zero audio** before InputFinished; preserve it as a starting recipe to validate for this model, not a universal constant for Nemotron/other exports. Artificial padding is model work, not real captured speech; exclude it from speech-duration denominators and report its latency. InputFinished marks end-of-input; it does not itself decode or commit text. Never resume feeding a finished stream; create a fresh one for the next hold.

The C equivalents include `SherpaOnnxOnlineStreamAcceptWaveform`, `SherpaOnnxIsOnlineStreamReady`, `SherpaOnnxDecodeOnlineStream`, `SherpaOnnxGetOnlineStreamResult`, `SherpaOnnxOnlineStreamInputFinished`, reset/endpoint functions, and matching destroy functions. Inspect the saved header for ownership and names before coding; destroy result allocations and stream/recognizer objects according to that API.

## Lightweight comparison: Vosk 0.3.45

Saved [wrapper](sources/vosk-api/python/vosk/__init__.py), [microphone example](sources/vosk-api/python/example/test_microphone.py), [simple file example](sources/vosk-api/python/example/test_simple.py), and [native API](sources/vosk-api/src/vosk_api.h) show:

```python
model = vosk.Model(model_path)
rec = vosk.KaldiRecognizer(model, 16000)
if rec.AcceptWaveform(pcm16_bytes):
    segment = json.loads(rec.Result()).get("text", "")
else:
    hypothesis = json.loads(rec.PartialResult()).get("partial", "")
# Only after accepted real audio has drained at physical release:
tail = json.loads(rec.FinalResult()).get("text", "")
```

AcceptWaveform true signals a finalized segment, not that the entire hold is over. Emit it once and replace only the active segment's partials. FinalResult flushes tail; create a fresh recognizer for the next hold while sharing the resident Model, unless the deployed version's reset behavior is explicitly validated. Do not blindly emit both an earlier Result and the same tail as separate commits. The provided example uses an unbounded queue and 8,000-frame input block; replace those integration choices with bounded capture/error handling. Do not treat a generic printed exception/exit 0 as successful dictation.

Start unrestricted dictation without a grammar restriction. Domain vocabulary/beam/hotword changes need accuracy tests, not just throughput tests. sherpa hotwords require modified beam search in the saved config; greedy is the cheapest baseline, not an equivalent hotword configuration.

## CPU scheduling and memory

One versus two threads first; use verified A76 CPU IDs only after profiling scheduler placement. Leave display/input and capture responsive. Avoid real-time scheduling for neural inference or large whole-machine priority changes. Compare idle worker spinning, active CPU-seconds/audio-second and peak process memory. Saved [session source](sources/sherpa-onnx/sherpa-onnx/csrc/session.cc) forwards `SessionConfig.*` provider config keys; validate support against the pinned binary rather than assuming every binding exposes it. Standard ORT keys for exploration are `session.intra_op.allow_spinning` and `session.inter_op.allow_spinning`. Main reference: [ORT thread management](https://onnxruntime.ai/docs/performance/tune-performance/threading.html). No thread-tuning benchmark on this board is claimed.

Resident weights do not imply busy decode when idle. Segment history, old partials and retained PCM need explicit bounds; discard utterance state after completion/cancel. Keep one owner of model memory rather than loading a recognizer per IBus field. For a successful NPU option, retain a CPU model only if the measured memory budget permits; a cold CPU failover has loading latency that must be reported.

## Failure policy

On audio overflow, runtime death, model error, focus-generation mismatch or deadline breach: stop/mark incomplete, preserve truthful evidence, clear the uncommitted composition under policy, and retain prior final commits without replay. An NPU startup error may select CPU before begin. An NPU error mid-hold cannot silently start an empty CPU stream; cancel that hold and use CPU on the next one unless bounded audio replay and committed-prefix exclusion have been proven. Missing model/runtime does not qualify as a no-op success.
