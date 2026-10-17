# Measurement, evidence and delivery contract

The combined plan's acceptance gates are primary. This guide explains how to produce evidence without search, avoid misleading speed metrics and integrate safely with existing provisioning.

## Repeatable baseline

Record host commit, framework/submodule revisions, runtime/model/token hashes, compiler image ID if applicable, kernel, libc, Python ABI, online CPUs, CPU implementer/part IDs, cpufreq policies/current/max frequencies, memory/swap and thermal values. Historical A76 IDs 6/7 are from another execution context, not permission to assume current affinity. Record mic USB identity/ALSA ID, current GPIO table and current desktop/IBus processes. Do not dump environment variables or Docker credentials.

Before load, use read-only commands such as `uname -a`, `lscpu`, `/proc/cpuinfo`, `free -h`, `df -h`, `vmstat`, `/sys/class/thermal/thermal_zone*/{type,temp}`, cpufreq sysfs, `arecord -l`, `/proc/asound/cards`, `systemctl is-active` for named services and `DISPLAY=:1 xdpyinfo`. `dmesg`/kernel logs remain operator root evidence. Record fan/cooling state; do not change governor, fan override, PMIC or power configuration simply to improve a benchmark.

Create task-specific ignored `artifacts/npu-stt/<run>/` before executing. Preserve structured JSON/JSONL and a readable narrative after each material event. Record monotonic elapsed times plus wall-clock/boot identity; RTC/NTP drift means wall clock alone is insufficient. Use run IDs in metadata, readable fixture/attempt names for evidence. Do not use /tmp as the sole home for load-bearing logs.

## Minimum fixture matrix

Use identical approved recordings/transcripts across backends: 5, 15 and 60 seconds of real speech; short command; numbers; technical names; hesitation/self-correction; long silence; noise; word continuing across a chunk boundary; release mid-word; repeated rapid holds. A sine wave tests transport, not useful recognition. Silence output/hallucination is a failure metric. Don't download random private audio or commit voice recordings without the intended retention decision.

For input-method-only fixtures, use scripted partial revisions, duplicate finals, out-of-order/stale events, focus-switch while decoding, Unicode and selected text. Test microphone disconnection, GPIO service restart, ASR crash, NPU busy/unavailable and target app exit. Failed utterances remain in denominator. Explicitly distinguish uncommitted provisional text from earlier committed segments.

## Metrics and definitions

- Cold load and warmup: before the first utterance; report separately. Native-library import/model creation and NBG prepare are not free just because excluded from steady-state RTF.
- Steady-state RTF: total pipeline processing wall time divided by **real** audio duration, measured with faster-than-real-time replay. Report CPU-seconds/audio-second separately. Include all selected NPU transfer/layout/cache handling costs. Don't include sleeps from an audio-paced driver in the compute RTF numerator.
- Live replay: feed audio at real capture cadence. Log sample index, chunk arrival/start/end, partial emission, IBus delivery/commit and app-visible observation where measurable. Input-paced wall duration/audio duration will be approximately one even for a fast model; that ratio is not compute capacity.
- Latency: first nonempty AND first correct useful partial; word-age/update lag; release-to-final; queue age/high-water and recovery from outliers. Report p50/p95/p99 service times and number of utterances/repetitions. A model chunk's audio duration and its lookahead need to be known.
- Quality: report exact final text and punctuation plus declared normalized WER. WER is `(substitutions + deletions + insertions) / reference words`. Define normalization before comparison. Numeric state/oracle tolerance cannot establish transcript meaning.
- Resources: peak RSS/PSS and total worker set, CPU time, thread count, swap in/out, sustained frequencies/temperatures and display cadence with intended app load. Service cgroup memory or summed PSS avoids pretending a single process's RSS is whole-product usage. Measure idle resident state too.

Run 1/2 threads first, model resident. Add intended app/display load, then optional VNC, then concurrent LLM generation as separate conditions. Record different cooling/power states rather than merging results. Proposed small-baseline gate: <=512 MiB incremental memory, RTF <=0.5, p95 useful partial <=1s, p95 release-to-final <=1s, no rising backlog in 60s, no silent lost audio or unintended edits. These are targets, not measurements. If model lookahead cannot meet them, record it and revise/select deliberately.

## Host delivery and rollback

Read host `docs/setup.md` and `AGENTS.md`; current templates in setup.sh are desired-state authority. Preserve its flags, result line, convergence/reboot behavior and managed ownership. Existing setup already includes dbus-x11/gpiod/Xlib and two-card audio verification; stale journal prose saying the USB mic violates exactly-one-card verification is superseded by commit 582f545. No need to redesign the speaker/TTS or load LLMs for dictation.

Portable host bootstrap policy is at `agentic-pipelines/references/host_bootstrap_contract.md`: standard-library dispatcher to the chosen version, read-only `--check`, per-requirement checks and explicit idempotent repairs, no system-Python mutation. It should delegate appropriate board provisioning to canonical setup.sh; avoid having two independent owners write the same units or files. New desktop session-bus wrapper/environment changes are provisioning and need same-change docs plus live apply -> verify before commit. Add only entrypoints relevant to this feature; don't replace the user's primary provisioning play action with a recovery command.

Rollback must be concrete before apply: preserve old unit content/enabled state, desktop launch environment, selected input method, button behavior/mode and versioned configuration. Stop the new worker, restore previous input-method selection and TL mapping, then restore owned files via the intended provisioning path. Do not erase models/runtime data to roll back a service. Snapshotting rollback bytes is not proof; test restoration in a controlled window.

Final acceptance includes a live physical TL hold with words updating inline in the supported editor/browser, clean finalization, CPU fallback, service restart/reboot and normal existing hardware behavior. Pass first apply -> verify and a no-change second apply for provisioning edits. Update relevant hardware records/README status only when evidence warrants. Missing physical access means live gates remain pending; offline fixture success is useful progress, not on-device completion.
