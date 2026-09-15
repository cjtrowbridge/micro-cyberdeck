# Current Plans Index

Format: `last_modified | path | title | summary`

2026-09-15-13-20-03 | plans/current/2026-09-15-13-17-28_hat-alsa-device-and-tts.md | Make the HAT speaker play "like a normal sound device" (ALSA jury-rig) + fast TTS (espeak-ng) | Prove the speaker with real speech (espeak-ng, one-time gate), then integrate a root engine daemon (existing gamepi-sound unit, FROZEN tick path untouched) behind a thin user-space ALSA PCM plugin over a unix socket, so aplay / espeak-ng "text" work with no flags and no service stop/start; every setup.sh change documented in docs/setup.md in the same change and live apply->verify before each commit.
2026-09-15-00-14-44 | plans/current/2026-09-14-23-03-51_setup-script-reentrant-provisioning.md | Reentrant setup.sh as canonical provisioning entrypoint | Convert setup.sh from a one-shot installer into a converging, self-verifying, flag-driven provisioning tool; document the contract in docs/setup.md; add the single VS Code entrypoint; mandate setup.sh change documentation.
