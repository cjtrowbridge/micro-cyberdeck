# Past Plans Index

Format: `last_modified | path | title | summary`

2026-09-15-13-17-31 | plans/past/2026-09-15-10-01-56_hat-sound-setup-fold-in.md | Track hat-sound.c and fold real-audio into setup.sh | Make tools/hat-sound.c a tracked managed artifact, build+install it from source into /usr/local/bin, add a gamepi-sound.service, persist kernel.sched_rt_runtime_us=-1 via a sysctl.d drop-in, converge user_overlays to the full desired set, clean stale es8388/i2c overlays, and document every row in docs/setup.md in the same change; live apply->verify on the board before commit.
