# Current Plans Index

Format: `last_modified | path | title | summary`

2026-09-15-10-07-40 | plans/current/2026-09-15-10-01-56_hat-sound-setup-fold-in.md | Track hat-sound.c and fold real-audio into setup.sh | Make tools/hat-sound.c a tracked managed artifact, build+install it from source into /usr/local/bin, add a gamepi-sound.service, persist kernel.sched_rt_runtime_us=-1 via a sysctl.d drop-in, converge user_overlays to the full desired set, clean stale es8388/i2c overlays, and document every row in docs/setup.md in the same change; live apply->verify on the board before commit.
2026-09-15-00-14-44 | plans/current/2026-09-14-23-03-51_setup-script-reentrant-provisioning.md | Reentrant setup.sh as canonical provisioning entrypoint | Convert setup.sh from a one-shot installer into a converging, self-verifying, flag-driven provisioning tool; document the contract in docs/setup.md; add the single VS Code entrypoint; mandate setup.sh change documentation.
