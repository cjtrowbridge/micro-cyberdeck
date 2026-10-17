# Streaming dictation: research checkpoint

Scope: operator selected research item 1. No board access, installation, runtime benchmark, or provisioning change. Plan: `plans/current/2026-10-16_streaming-dictation-research.md`.

## Requirement and corrections

- Requirement of record: hold TL, speak, see interim words in the focused field while still holding, release to finalize. A floating preview followed by paste on release does not by itself satisfy it.
- The earlier October 16 journal's categorical rejection of Whisper streaming was too strong. Upstream whisper.cpp has a continuous microphone example, sampling every half second and repeatedly decoding a rolling window. This is incremental batch inference, not a cache-aware online recognizer, but can still provide interim output if hardware keeps up. It does not prove the pinned board build meets latency targets. [Upstream example](https://github.com/ggml-org/whisper.cpp/blob/master/examples/stream/README.md).
- The earlier OpenWhispr assessment is outdated/incorrect: its July 18, 2026 engineering article describes local streaming with Nemotron through sherpa-onnx. Its architecture is Electron, not Tauri. The described dictation surface replaces text in its own preview and commits the transcript on stop; this is evidence for streaming recognition, not continuous target-field insertion. [Maintainer article](https://openwhispr.com/blog/local-streaming-speech-to-text), [build configuration](https://github.com/OpenWhispr/openwhispr/blob/main/electron-builder.json).
- Xvfb alone does not rule out a desktop GUI. Actual integration constraints are session services, audio capture, hotkeys, native dependencies, supported architecture, and CPU overhead. Linux ARM64 packaging and successful operation on this image remain unverified.

## Recognition shortlist (English assumed for first comparison)

1. **First prototype: sherpa-onnx streaming Zipformer, small INT8 English model.** The runtime documents ARM64 CPU support and C/C++/Python/Go APIs. Its English 20M model has a documented INT8 encoder around 41 MB and a microphone example. This makes it a credible low-resource baseline, not an accuracy or board-speed guarantee. Compare a larger streaming English Zipformer if accuracy is inadequate. [Platforms](https://k2-fsa.github.io/sherpa/intro.html), [models and microphone commands](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/online-transducer/zipformer-transducer-models.html), [Go API](https://k2-fsa.github.io/sherpa/onnx/go-api/index.html).
2. **Vosk small English: lightweight control candidate.** The official model catalog lists a 40 MB US English model for Android/Raspberry Pi. The microphone example exposes separate partial and finalized segment results. Test proper nouns, developer vocabulary, punctuation, and accents; published WER on unrelated datasets cannot choose the winner for this user. [Models](https://alphacephei.com/vosk/models), [microphone implementation](https://github.com/alphacep/vosk-api/blob/master/python/example/test_microphone.py), [installation/platforms](https://alphacephei.com/vosk/install).
3. **Nemotron via sherpa-onnx: quality candidate after the small baseline.** OpenWhispr documents a roughly 632 MB INT8 English model, online decoding, and runtime-version sensitivity. Its modern-laptop performance is not an A733 benchmark. Validate exact runtime/model compatibility and board resource cost before adoption. Same runtime family could support a model comparison without adopting the whole desktop application.
4. **Whisper base/tiny rolling decode: comparison or later refinement.** Keep as a benchmark candidate, not the default medium-model live path. Repeated windows, hypothesis reconciliation, and compute cost need measurement. Do not assume Whisper is impossible, or that the earlier medium timings predict base/tiny performance.

Do not rank unspecified Paraformer/SenseVoice variants as streaming engines. Select an explicitly online model and supported runtime before treating one as a candidate. Cloud ASR and NPU integration are not prerequisites for this local prototype.

## Insertion: the architectural decision

Recognition partials are revisable full hypotheses, not necessarily append-only word deltas. Blindly appending each one duplicates words. Backspacing an old suffix assumes the caret, selection, editor behavior, and focus have not changed; it can delete unrelated text. Unicode graphemes further complicate character-count deletion.

**Preferred investigation: an IBus input-method engine.** Update the current uncommitted composition (preedit) as recognition changes, commit finalized segments during the hold, and flush/commit the remaining segment on release. IBus explicitly exposes preedit and commit events, with an X11/XIM bridge. App/toolkit support and actual placement of preedit vary: an editor, browser text field, terminal, and Electron app must each be tested. This is a proposed architecture, not a promise of universal application compatibility. [Input-context API implementation](https://github.com/ibus/ibus/blob/main/src/ibusinputcontext.c), [X11 bridge](https://github.com/ibus/ibus/blob/main/client/x11/main.c).

The board's previously documented absent user session/disabled session bus makes session-bus/bootstrap integration an explicit prerequisite, not a one-command assumption. Validate a private desktop session bus and input-method environment for :1 before planning deployment.

**Fallback:** insertion of finalized segments via clipboard or key synthesis while provisional text appears in a separate preview. This can show committed words during a long hold, but does not meet the strict inline-interim requirement. Declare this reduced behavior explicitly if selected. Clipboard snapshots also need ownership/lifetime handling; Ctrl+V is not a universal terminal paste binding.

XTest simulates keycodes, not arbitrary editable text or a composition range. [XTEST specification](https://xorg.freedesktop.org/archive/current/doc/libXtst/xtestlib.html).

## Prototype sequence and proposed acceptance

1. Prove the insertion surface with scripted partials (no ASR): `I want a` -> `I want an apple` -> final. Verify replacement, Unicode, selected text, focus changes, cancellation, and application-specific composition behavior. Use a scratch editor and browser first; report terminal/Electron compatibility separately.
2. Replay representative recorded speech into the small streaming recognizer at audio rate. Record audio-relative first-partial times, finalization lag, hypothesis revisions, exact final transcripts, peak RSS, CPU, and queue depth. Faster-than-real-time file decoding alone is not a live-latency measurement.
3. Connect live USB capture, using a stable ALSA identity verified on the board rather than assuming card index 1 persists. Use bounded queues and report capture overruns/backlog. A continuous capture ring with short pre-roll is worth testing to avoid clipping the first word at trigger press; disclose capture lifetime.
4. Integrate TL start/stop as a dedicated event, consuming it rather than leaving Control_L held in the destination. The existing daemon currently emits full taps on both physical edges; it needs a separate reviewed integration change. Only one process may own PL2's GPIO request.
5. On release, finish queued audio and the model's tail using that model/runtime's documented finalization API; do not equate microphone stop with final transcript. Focus change cancels/suspends the active destination; do not redirect pending words into a newly focused field. Runtime loss must preserve truthful partial/failure status.

Proposed targets, not measured promises or operator-approved limits: first meaningful partial within 1 s of speech onset; continuing updates within roughly 500 ms after startup; final text within 1 s of release; no steadily growing audio backlog during a 60 s hold; no unintended deletions or insertion after focus changes. Report p50/p95 and worst cases across repeated phrases, and decide acceptable transcription errors from representative user speech.

## Plan reconciliation

The September voice-agent design and G2/G3 acceptance describe release -> ASR -> LLM -> TTS/HUD. Preserve that feature's history; a distinct dictation implementation plan should govern live target-field text. Ollama, TTS, Ebitengine, and whisper-medium residency are unnecessary dependencies for the first dictation proof. G0 USB signal evidence already exists, but recognition quality and current device availability remain to be tested. No governing host roadmap exists; that absence remains explicit. No existing plan or provisioning behavior was rewritten here.
