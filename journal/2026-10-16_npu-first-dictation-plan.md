# NPU-first dictation plan

- Operator requested one plan: try the NPU before STT, then implement STT either way.
- Created `plans/future/2026-10-16-20-17-37_npu-first-streaming-dictation.md`; planning only, no board/software setup performed.
- Existing September evidence already proves a v3 SDK demo. New milestones distinguish revalidation, reference conversion, useful STT acceleration and final dictation acceptance.
- Proposed NPU budget: four active hours, one reference conversion and one STT candidate, one evidence-driven retry each. Unavailable toolchain/unsupported graph ends this pass and routes to CPU STT.
- Every NPU outcome enters CPU baseline/model selection and live inline text insertion. NPU failure cannot strand STT behind a compiler/vendor dependency.
- No host roadmap exists. External pinned NPU roadmap review is queued because the third-party submodule is not populated locally. September voice-assistant gates remain separate from dictation acceptance.
- New plan proposes implementation tasks and acceptance criteria; no model/backend or hardware-success claim made.
- Index regeneration was attempted but refuses three existing nonconforming current-plan filenames: September voice design and the two earlier October notes created in this session. The new future plan passes its own metadata checks; its index row was added manually. Broader filename/link migration is left outside this proposal.

## 2026-10-16 01:47 (session fc10b9e1) — Plan finalized and promoted to `current`

- **Item 1.3 closed:** CPU topology confirmed from the device tree —
  2× Cortex-A76 (policy6, 416 MHz–2.002 GHz) + 6× Cortex-A55 (policy0,
  416 MHz–1.794 GHz); `/proc/cpuinfo`'s union-flags mask the A55 cluster.
  Recorded in `docs/hardware/soc-thermal.md` (status Done); the kernel's
  `cpub`/`cpul` thermal zones noted as the live per-cluster throttle surface.
- **5.2 TL trigger-event design drafted** into the plan: press = falling edge
  = start, release = rising edge = stop + final flush; start/stop EVENTS, not a
  held modifier; no re-trigger while held, no auto-Enter; focus change
  cancels/suspends; `tools/hat-buttons.py` stays the single GPIO owner and
  hands off over the existing unix-socket channel (same thin-owner/engine
  split as the HAT speaker). Supersedes the 2026-10-16 half-edge fix for TL
  (its edge detection is reused); that fix's held/modified behavior stays for
  the other keys.
- **Promoted `future` → `current`** (status: current, revised 2026-10-16) —
  the first non-trivial implementation edit (M0/M2 tooling) is imminent.
  `current/index.md` (top row, M0–M5) and `future/index.md` refreshed by hand
  to match their existing style; the strict regenerate script still rejects the
  3 legacy-named host plans. Scope pointers on both voice-agent plans and the
  README mic row now point dictation here.
- **Committed and pushed as `a6f0318`** (submodule untouched; a mistaken index
  regeneration in the submodule was reverted first).
- **Next:** board is ready for live verification — M0/M2 CPU-ASR tooling
  (whisper.cpp v1.9.4 + system service with `User=cj`) and the live
  partial→final stream test (the real gate). NPU first, CPU fallback ready.
