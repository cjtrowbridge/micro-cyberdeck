---
plan_id: 2026-10-16-20-17-37_npu-first-streaming-dictation
title: NPU feasibility first, then streaming dictation on NPU or CPU
summary: Time-box A733 NPU revalidation and STT acceleration feasibility, then implement measured live push-to-talk dictation with a CPU fallback regardless of NPU outcome.
status: current
created_at: 2026-10-16-20-17-37
revised: 2026-10-16
---

# NPU feasibility first, then streaming dictation

Key: `[ ]` pending task, `[x]` completed task, `[?]` needs validation, `[-]` closed task

## Objective and authority

Operator requested one plan to try the NPU first and implement STT either way.
This file is the proposed execution plan; the current checkpoint prepares its
offline documentation only, as requested by the operator.
Promote it to current when implementation begins. Documentation downloads and
pinned submodule initialization are part of the authorized handoff preparation;
no runtime installation, model/compiler download, board probe, deployment,
commit or push occurs as part of authoring.

End behavior: hold the physical left trigger, speak, see revisable words in the
focused text field during the hold, release to finalize. Recognition is local.
English is the initial benchmark language; confirm additional language needs
before locking a model. A separate preview followed only by paste on release
does not satisfy the requirement. First implementation scope is the board's
X11 desktop on :1; remote client text fields are not automatically included.

## Evidence and roadmap alignment

**Offline executor: read `docs/research/npu-stt/README.md` before item 1.**
Its acquisition, NPU, STT, insertion and validation guides preserve local API
sources, exact downloadable artifact metadata and corrections to upstream
examples. Run its read-only `verify_packet.py` before relying on snapshots.
Search access is unnecessary for the reading route; missing binary/model/apt
assets still require explicit staging, as the acquisition guide explains.

- Host NPU evidence: `docs/hardware/npu.md` and
  `journal/2026-09-19-npu-ollama-path.md`: vipcore/VIPLite and a v3 SDK demo
  already passed on this board in September. Current state is unverified.
- Pinned external prior: `third_party/a733_npu_driver`; initialized locally at
  its recorded gitlink during documentation preparation. On the executing host,
  verify availability and initialize only the recorded gitlink revision if absent;
  read its AGENTS.md if present and `docs/import_chat.md` first, then the
  relevant setup, results, blockers, conversion scripts and `docs/roadmap.md`.
  Claims in host summaries are historical evidence, not present guarantees.
- Resource analysis: `journal/2026-10-16_dictation-resource-budget.md` and
  `journal/2026-10-16_streaming-dictation-research.md`. A733 is specified as
  2 A76 + 6 A55; verify actual CPU IDs and usable frequencies before affinity.
- No governing host roadmap exists. This plan's milestones below provide the
  delivery sequence for this task; do not invent completion of a separate
  roadmap. The pinned external roadmap was reviewed during handoff preparation:
  G0/G1/G2 and LLM/vision milestones describe another board configuration;
  it does not cover STT. Its old V2 unlock language is superseded by its V2d
  success report. Preserve its history; this plan adds STT graph/cache/oracle
  and dictation milestones. External roadmap completion is not host acceptance.
- Existing September voice-agent design/gates remain a separate spoken-answer
  feature (ASR -> LLM -> TTS/HUD). They do not govern dictation acceptance.
  Before implementation, update their scope pointers and index summaries so
  this plan alone governs NPU-first dictation. Reconcile the already evidenced
  USB-mic G0 outcome and system-unit/8086 correction without marking pending
  board actions complete. Preserve unrelated power-path and ALSA plans.

## Limits and branch policy

Proposed default NPU budget: **four hours of active investigation**, at most
one known-good reference conversion and one STT encoder/model conversion
candidate, with at most one evidence-driven correction/retry per conversion.
Record elapsed effort after each material step. External downloads/vendor
responses are not active effort, but missing access ends this pass rather than
making STT wait indefinitely. A missing toolchain, unavailable hardware, unsafe
thermal/power state, or nonviable graph transitions immediately to CPU STT
development; final live acceptance still needs a healthy board.

Do not spend the pass on Qwen-on-NPU, new generic execution-provider work,
firmware/kernel replacement, broad unsupported-operator implementation, or
unbounded model search. Those require a separate follow-up proposal. No raw
PMIC writes. NPU access has one owner at a time; do not disrupt another job.

No large acquisition by default: inspect free disk and existing artifacts
before obtaining the historically ~11 GB compiler archive/~30 GB host working
set. Record exact source/version, prerequisites, download size and cleanup
ownership before using reviewed host bootstrap. Prefer existing verified
assets; do not silently install framework bootstrap examples over setup.sh.

## Milestones and executable work

- [x] 0. Prepare the operator-requested offline documentation handoff (authoring
      checkpoint only; does not begin device implementation).
  - [x] 0.1 Read pinned NPU handoff, roadmap, reports, scripts and host contracts;
        correct stale examples and separate prior-board results from this deck.
  - [x] 0.2 Preserve versioned upstream STT/input-method sources and acquisition
        metadata locally; supply exact API/finalization and resource guidance.
  - [x] 0.3 Write local execution guides, artifact/dependency inventory and branch
        troubleshooting; verify links/hashes and update relevant hardware records.

- [ ] 1. Establish prerequisites and evidence (M0).
  - [ ] 1.1 Record git status, selected host/framework/third-party revisions,
        device connection and current service configuration; preserve local work.
  - [ ] 1.2 Initialize the pinned NPU submodule if absent; inspect its instructions,
        import handoff, blockers and roadmap; record only relevant claims.
  - [ ] 1.3 Re-probe CPU topology/flags, memory/swap, free disk, thermal/fan state,
        display baseline, USB mic identity and the current TL mapping.
        Root-only kernel-log evidence is operator-level and labeled as such.
  - [ ] 1.4 Record scoped setup actions in the host-owned portable bootstrap
        path required by current framework policy; reconcile its delegation
        to canonical setup.sh and platform-specific prerequisites. Its check
        mode must not mutate. Do not build a general machine-management project.
  - [ ] 1.5 Reconcile host plan/index scope pointers described above; record
        roadmap absence and external roadmap review outcome in the checkpoint.

- [ ] 2. Run the bounded NPU pass (M1: explicit outcome, never an STT blocker).
  - [ ] 2.1 Check existing vipcore, device access, VIPLite libraries and target
        compatibility. Run the existing v3 smoke with a timeout and no other
        NPU consumer. Persist versions, hashes, logs, return codes and output.
  - [ ] 2.2 If reference smoke fails, perform only a diagnosed reversible repair
        within the budget and repeat once; otherwise record NPU unavailable
        for this pass and proceed to item 3.
  - [ ] 2.3 Inventory authorized compiler/runtime assets and host capacity.
        Confirm the converter supports target 0x1000003b. If absent/unobtainable,
        record the exact acquisition blocker and proceed to item 3.
  - [ ] 2.4 If conversion is available, compile one known-good reference ONNX
        to v3 NBG; compare actual board output to its CPU oracle with a declared
        fixture-specific tolerance. Demo success alone is not useful STT offload.
  - [ ] 2.5 Inspect one small streaming STT model's encoder graph: operators,
        shapes, chunk/lookahead, cache inputs/outputs, quantization/calibration,
        CPU/NPU partition and VIPLite state-transfer requirements. Select the
        candidate only after this inspection; prefer the small Zipformer
        baseline if its graph is compatible.
  - [ ] 2.6 If compatible within budget, convert that candidate using representative
        speech calibration and run a persistent, preloaded runner. Compare
        multi-chunk outputs/state against the CPU oracle and test recorded
        speech through the full CPU+NPU recognizer. Do not infer recognition
        correctness from cosine similarity or one successful graph invocation.
  - [ ] 2.7 Measure complete wall time including features, layout conversions,
        CPU/NPU copies, cache transfer, encoder, decoder/joiner and finalization;
        include CPU time, memory, NPU ownership and thermal behavior. A fast
        kernel alone does not establish faster recognition.
  - [ ] 2.8 Write one branch decision with evidence: (A) STT-capable NPU path,
        (B) NPU reference works but STT offload unsupported/not worthwhile, or
        (C) NPU/toolchain unavailable. Close unattempted conditional steps with
        a reason. All branches advance to item 3; B/C record a deferred NPU
        follow-up, not a delivery dependency. A requires transcript integrity,
        stable chunk state and either >=20% end-to-end speed improvement or
        >=25% CPU-time saving without worse latency/accuracy, compared on the
        same clips/configuration. These are proposed selection thresholds.

- [ ] 3. Select a deployable streaming recognizer (M2).
  - [ ] 3.1 Establish CPU Zipformer-small and Vosk-small baselines using pinned
        runtime/model artifacts, native inference, one stream and 1/2 threads.
        Separate cold initialization, warmup and steady-state timings. Keep
        weights resident across holds and bound utterance/state lifetime.
  - [ ] 3.2 Compare any successful NPU candidate on identical speech, accuracy
        normalization, input pacing and intended application/display load.
        Inspect NPU acquisition/resource contention and switching failure modes.
  - [ ] 3.3 Measure queue age, useful-partial lag, finalization, transcript errors,
        peak RSS/PSS, CPU-seconds/audio-second, swap activity and temperature.
        Add hotwords/beam search only as a separately measured configuration.
  - [ ] 3.4 Select the smallest configuration meeting acceptance. If neither
        small model meets usable accuracy, benchmark one larger streaming
        candidate (e.g. Nemotron) with an explicit resource budget. If none
        meets live latency, document the measured gap and revise parameters
        within this plan; do not silently replace streaming with batch mode.

- [ ] 4. Prove and implement live insertion (M3).
  - [ ] 4.1 Build a scripted-partial IBus proof for :1, with the necessary desktop
        session bus and app environment; validate actual inline preedit/commit
        behavior in a scratch editor and browser before connecting recognition.
  - [ ] 4.2 Verify Unicode, selected text, corrections, cancellation, focus loss
        and normal keyboard coexistence. Report terminal/Electron compatibility
        separately. Unsupported apps must be visible limitations, not claims
        of universal support. Seek a scope decision if required apps cannot
        support inline composition; preview-only fallback is not acceptance.
  - [ ] 4.3 Implement a nonblocking input-method adapter with bounded persistent
        IPC: coalesce outdated partials, preserve ordered final segments, and
        bound composition to the current segment. Inference cannot block its
        event loop. Do not use blind suffix deletion as the general solution.

- [ ] 5. Integrate microphone, physical trigger and recognizer (M4).
  - [ ] 5.1 Capture the stable ALSA device at 16 kHz mono, using measured period
        settings and independent capture/decode queues. Bound pre-roll, audio
        duration, queue capacity and evidence retention; surface overruns.
  - [ ] 5.2 Fix trigger edge semantics through one GPIO owner: send start/stop
        dictation events and consume TL for this mode rather than holding
        Control_L in the destination. Preserve other mapped buttons and verify
        the current HAT's mapping rather than copying historical assumptions.
  - [ ] 5.3 Connect partials to preedit and finalized segments to commits. Release
        drains queued audio and performs the chosen runtime's final flush.
        Cancel/suspend on destination focus change; do not redirect pending
        words into another field. Do not inject Enter automatically.
  - [ ] 5.4 Support CPU selection explicitly and default to it for branches B/C.
        For A, test startup/runtime NPU failure: switch to CPU only at a clean
        utterance boundary unless tested bounded replay can prevent duplicate
        commits. Never discard state mid-hold and report successful dictation.

- [ ] 6. Provision, validate and close (M5).
  - [ ] 6.1 Add narrow managed configuration/service/dependency integration
        through the reviewed bootstrap and canonical provisioning contract.
        Document every setup.sh/provisioning change in docs/setup.md in the
        same change, including backend selection, checks, rollback and disabled
        state. Preserve the separate voice-assistant model choices.
  - [ ] 6.2 Add meaningful fixture tests for event ordering, revised hypotheses,
        focus/cancel/flush, queue overflow, duplicate commits and CPU fallback;
        no tests that merely mirror constants or implementation structure.
  - [ ] 6.3 Run recorded speech replay and live holds on the board, including
        5/15/60-second speech, silence, rapid repeats, names, punctuation and
        microphone disconnect. Repeat with intended application/display load;
        measure concurrent LLM generation separately if required for deployment.
  - [ ] 6.4 Exercise both CPU operation and any chosen NPU operation, including
        service restart/reboot, unavailable NPU and bounded failure reporting.
        Confirm normal buttons, display and speaker services still function.
  - [ ] 6.5 Pass live apply -> verify before committing provisioning changes,
        then prove a second apply converges. Persist benchmark evidence outside
        /tmp, with artifact/version identities and links in the journal.
  - [ ] 6.6 Update NPU, mic, buttons, thermal/display records as affected and
        README Hardware Status rows only when supported outcomes move status.
        Publish app compatibility, accuracy results, resource limits and NPU
        decision. Move this plan to past and refresh indexes only when all
        required acceptance criteria have evidence.

### Trigger event design (milestone 5.2)

Design of record for 5.2 — how the physical trigger becomes **start/stop
dictation events** rather than a held modifier key in the destination.

**The problem with the current mapping.** `tools/hat-buttons.py` maps the left
trigger (TL) to *hold down* `Control_L`: it sends a `Control_L` keydown while
the button is held and a keyup on release. That is the right shape for a
modifier-key button, but it is the wrong shape for dictation — the destination
IME sees a *held `Control_L`* (a half-edge that was patched in 2026-10-16 to
make the held state well-defined), not a discrete *start* then *stop*. Dictation
needs two clean edges: **press = start capturing** (falling edge), **release =
stop + final flush** (rising edge).

**Invariant — one GPIO owner.** Exactly one process reads the TL GPIO edge and
decides what it means; nothing else synthesizes TL input. Keeping the existing
`hat-buttons.py` as that owner is the low-risk choice: it already owns the TL
pin, already has working edge handling (the 2026-10-16 half-edge fix), and
preserves every other button mapping untouched. TL's *action* changes from
"emit a held `Control_L` to the active window" to "emit `dictation.start` on
falling edge and `dictation.stop` on rising edge to the dictation runtime."
The other mapped buttons (the rest of the 13) keep their current mappings
verbatim; verify them against this HAT's live pinmap via `button-map.py`
rather than copying historical assumptions (the 5.2 text already requires this).

**Edge semantics (the contract 5.3 will consume).**
- Falling edge (press): emit `start`; the runtime begins capture (5.1) and
  preedit accumulation (5.3).
- Rising edge (release): emit `stop`; the runtime drains queued audio and does
  its chosen runtime's final flush (5.3).
- **No re-trigger while held:** after a falling edge the next event must be the
  matching rising edge; ignore further falls until a clean release. A button
  already held at daemon start is treated as *released-then-pressed* so the
  first edge is unambiguous (debounce the first edge).
- **TL is consumed in dictation mode:** while dictation is the active mode the
  destination IME must **not** receive a held `Control_L` for TL. If the
  operator wants the old modifier behavior back, that is an explicit mode
  change, not the default.
- **No automatic Enter:** releasing never commits a newline (5.3); stop only
  finalizes the in-progress utterance.

**Handoff to the runtime.** `hat-buttons.py` (the single owner) delivers the
`start`/`stop` events to the dictation runtime over the same local channel the
runtime already uses (the unix-socket the ALSA plugin / engine pattern uses),
so the trigger does not need its own transport. This is the same
"thin owner + engine" split the HAT speaker already uses (`gamepi-sound`);
it keeps the GPIO/edge logic out of the model/runtime process.

**Status of the transitional fix.** The 2026-10-16 half-edge correction in
`tools/hat-buttons.py` remains valid for the *other* keys (its held/modified
behavior is now well-defined). For **TL specifically** it is superseded by the
`start`/`stop` event model above — but the underlying edge detection it added
is reused, so nothing in the daemon's plumbing is thrown away.

## Acceptance and limits

Proposed targets to validate: sustained processing RTF <=0.5 under intended
load; no growing queue age over a 60-second hold; p95 first useful partial
<=1 second and release-to-final <=1 second; no silent audio loss, swap-induced
stall, unintended deletion or focus leakage. Count failed utterances explicitly.
Record p50/p95/p99 chunk service times and display responsiveness; hardware
headroom and accuracy on representative operator speech decide the model.
Small-model total incremental memory target is <=512 MiB; any larger choice
needs measured peak memory and an explicit documented budget. Record cold-start
delay separately from resident performance and match actual model lookahead.

Accuracy acceptance uses operator-representative phrases, including technical
names, numbers and corrections. Report both verbatim text/punctuation behavior
and normalized WER; choose its acceptable threshold before selecting a backend.
An ASR NPU path that changes meaning or misses chunk state fails regardless of
its numerical similarity or kernel speed.

NPU success is not required to finish STT. A driver node, loaded module, demo
inference, or vision model proves only its own milestone. Delivery requires
working live dictation on CPU or a validated NPU/CPU hybrid. If board access is
unavailable, continue offline implementation/tests while marking live gates
pending; do not claim acceptance. Unsafe board conditions defer live testing,
not CPU code development. No firmware flashing, PMIC experiments, cloud speech,
LLM responses, TTS redesign, broad NPU tooling, or unrelated bug fixes are in scope.
