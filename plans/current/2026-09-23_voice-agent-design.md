---
plan_id: 2026-09-23-voice-agent-design
title: Voice agent — design the push-to-talk runtime (whisper-medium + qwen3.5:4b resident, left-bumper PTT)
summary: Establish the voice agent as a tracked project (projects/voice-agent/) and settle the design: the topology (Ebitengine app on :1 + loopback sidecar), the residency policy (Ollama keep_alive -1 + whisper-server as a user unit), the audio-capture question (no input path exists on the board today — options A/B/C), and the gate plan G0–G3 from design to first push-to-talk round trip. This plan is DESIGN ONLY: nothing is provisioned, cloned, built, or installed. Gate execution is a follow-on plan keyed to G0's outcome.
status: current
created_at: 2026-09-23
revised: 2026-09-23
---

Key: `[ ]` pending task, `[x]` completed task, `[?]` needs validation, `[-]` closed task

# Voice agent — design the push-to-talk runtime

User intent (2026-09-23): "an efficient way to set up an ebe runtime that uses
the whole screen and keeps whisper-medium and qwen3.5:4b resident in memory,
so we would bind the left bumper button so that it becomes push-to-talk with
the qwen3.5:4b model."

Scope boundary (explicit, same session): the user asked to create this as a
**tracked part of the repo with its own docs** — discussion/design only. This
plan does **not** touch the board: no `setup.sh` changes, no apt installs, no
clones, no builds, no model pulls, no systemd units. The follow-on plan starts
at gate G0 and owns the provisioning (the design's §9 batch) once G0 has
decided the mic.

## Tasks

- [x] (WS1) Create the tracked project scaffold: `projects/voice-agent/`
      (README.md + `docs/design.md`), following the repo's project/doc
      conventions (evidence-first claims, linked hardware records, no
      secrets, "how we know it" discipline carried over from docs/hardware/)
- [x] (WS2) Write the design: topology (ebe app + sidecar + the loopback
      protocol), residency (keep_alive -1 / whisper-server user unit),
      capture options A/B/C (USB mic vs HAT codec input vs headphone-jack),
      ASR/LLM/TTS choices, the G0–G3 gate plan, and the open-decisions list
      with owners
- [x] (WS3) Wire the project into the top-level README: a section in the
      project body linking the design (the Known Unresolved Issues bullet —
      "Building voice-interactive local agent software" — remains until a
      gate flips green; the project row is the forward reference to its design)
- [ ] (WS4, deferred) The gates themselves — **a separate plan, in
      `plans/current/`, created when the operator says go**: G0 (the mic —
      arecord -l hw:0, jack probe, reference-clone pinout lookup; the
      decision that gates everything), G1 (both models resident, RAM
      numbers logged), G2 (the shell chain, headless, per-stage timings),
      G3 (the app on :1, the round trip). Its first task is the §9
      provisioning batch, then the mic decision from A/B/C
- [ ] (WS5, standing) Any gate finding that moves a device fact updates the
      relevant `docs/hardware/` record + README row in the same change
      (standing rule) — e.g., a working codec-input capture would flip
      audio-normal-device's framing and add/adjust a capture record

## Acceptance gate (this plan)

The project is "created" when: (1) `projects/voice-agent/` exists and is
tracked, with README + design; (2) every claim in the design cites a verified
record or is explicitly marked *to-probe* (no bare asserts about board state);
(3) the README links the project and the design, and the known-unresolved
bullet's forward reference is the project; (4) `git status` shows only the
intended additions.

## Non-goals (this plan)

- No board changes of any kind (the 2026-09-23 session's explicit
  "i just want to discuss it" — the project is the artifact of that
  discussion, and it stops at the design door)
- No decision recorded for the open items (mic option, 9b prune, sidecar vs
  in-app, HUD layout) — they are owned and deferred, not pre-decided
- No ebe-boilerplate clone, whisper.cpp vendor, or model download