# NPU-first dictation plan

- Operator requested one plan: try the NPU before STT, then implement STT either way.
- Created `plans/future/2026-10-16-20-17-37_npu-first-streaming-dictation.md`; planning only, no board/software setup performed.
- Existing September evidence already proves a v3 SDK demo. New milestones distinguish revalidation, reference conversion, useful STT acceleration and final dictation acceptance.
- Proposed NPU budget: four active hours, one reference conversion and one STT candidate, one evidence-driven retry each. Unavailable toolchain/unsupported graph ends this pass and routes to CPU STT.
- Every NPU outcome enters CPU baseline/model selection and live inline text insertion. NPU failure cannot strand STT behind a compiler/vendor dependency.
- No host roadmap exists. External pinned NPU roadmap review is queued because the third-party submodule is not populated locally. September voice-assistant gates remain separate from dictation acceptance.
- New plan proposes implementation tasks and acceptance criteria; no model/backend or hardware-success claim made.
- Index regeneration was attempted but refuses three existing nonconforming current-plan filenames: September voice design and the two earlier October notes created in this session. The new future plan passes its own metadata checks; its index row was added manually. Broader filename/link migration is left outside this proposal.
