# Streaming dictation research

Authority: operator request to focus on research item 1, streaming ASR and live insertion.

- [x] Verify upstream streaming implementations and distinguish live preview from live insertion.
- [x] Compare CPU/ARM64 candidates and X11 text-composition approaches.
- [x] Record a proposed prototype, measurement requirements, uncertainties, and corrected earlier claims in `journal/2026-10-16_streaming-dictation-research.md`. No board performance measurements claimed.
- [x] Operator-requested deeper dive: verify manufacturer CPU topology, distinguish artifact size from RSS, analyze streaming deadlines/thread pools/buffering, and record resource-aware benchmark criteria in `journal/2026-10-16_dictation-resource-budget.md`. Live CPU topology/frequencies and all board performance measurements remain unverified.

Research only: no installations, device probes, deployments, or engine selection treated as approved.
The existing voice-agent design/gate plan targets release-then-answer, not live dictation. No governing host roadmap exists; reconcile scope and acceptance before implementation.
