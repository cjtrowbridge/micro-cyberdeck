# Speaker (HAT amp)

> Status: **Done** (mirrors the README table)

## What it is

The GamePi13 HAT's small speaker, driven by an on-HAT audio amplifier. The
decisive hardware fact: **the amp is driven by a single GPIO/PWM line, not by
I2S** — header pin 12 = `PB5` = `gpiochip0` line 37. The deck's engine streams
a 1-bit sigma-delta bitstream to that pin from a real-time process.

## What we know

- **Wrong path already ruled out (and kept ruled out):** an `es8388-audio`
  DT overlay with I2S probe was tried earlier and bound nothing — the HAT
  codec simply is not an I²S codec. `setup.sh` removes any stale
  `es8388`/`i2c0` dtbos and its verify row **`stale-overlays`** re-asserts the
  board has no `i2c-0` adapter and no stale overlays; verify row
  **`audio-cards`** asserts *exactly one* ALSA card exists
  (`allwinnerhdmi`, see [hdmi-audio.md](hdmi-audio.md)) with the note
  "HAT audio is GPIO/PWM pin 12, not I2S".
- **Engine:** `tools/hat-sound.c` → installed `/usr/local/bin/hat-sound`,
  currently **v3.6** (installed binary md5
  `6b7b9ea18c5ac3560132eb5f3d0a4008`):
  - 1-bit sigma-delta stream: tick clock `TICK_HZ = 192000`, oversampling
    `OS = 4` → 48 kHz audio grid, `SD_FS = 8192`, `GAIN = 0.85`.
  - Real-time pinned (`SCHED_FIFO`); the verify row **`rt-sysctl`** asserts
    `sched_rt_runtime_us = -1` (no RT bandwidth throttle), persisted by a
    sysctl drop-in.
  - **v3.4** fixed a signed-compare bug that paced the tick loop 4× fast;
    **v3.5** added `-i` idle (pin held LOW when no job is running — silent);
    **v3.6** is the **socket daemon**: the engine acquires gpiochip0 line 37
    *once* at start, binds `/run/gamepi-sound.sock` (0666 — on this kernel
    the engine must do **both** `fchmod(fd, 0666)` and `chmod(path, 0666)`,
    see `socket_bind()` in `tools/hat-sound.c`), and serves serialized
    **raw s16le 48 kHz mono** jobs, one client at a time. Per-job stats go to
    the journal, not to `/run` stats files.
- **Service:** `gamepi-sound.service` — `Type=simple`,
  `ExecStart=/usr/local/bin/hat-sound -d /run/gamepi-sound.sock`,
  `Restart=always`/`RestartSec=1`; stop is clean (signals installed via
  `sigaction` **without** `SA_RESTART`, so the engine exits its blocking
  `accept()` promptly instead of the old 90 s `TimeoutStopSec` + SIGKILL
  path). No `RuntimeDirectory=` — the socket lives directly in `/run` (tmpfs,
  cleared at boot; the engine unlinks it on clean exit and re-unlinks a stale
  one at bind).
- **TTS today:** `espeak-ng` 1.52.0, piped into the socket.
- **Provisioning:** `setup.sh` builds the engine from `tools/hat-sound.c` with
  fixed flags and does a **compare-then-install**: if the fresh build is
  byte-identical to the installed binary nothing is touched (this is the
  guard against the stale-binary drift that burned the 2026-09-15 sessions);
  any install lands in `CHANGED_FILES` and the unit-repair phase restarts the
  service, so the running daemon is always the installed binary.

## How we know

- Re-probe (mostly no sudo): `md5sum /usr/local/bin/hat-sound` (expect
  `6b7b9ea18c5ac3560132eb5f3d0a4008`); `test -S /run/gamepi-sound.sock && ls
  -l /run/gamepi-sound.sock` (expect socket, 0666); `systemctl is-active
  gamepi-sound`; `cat /proc/sys/kernel/sched_rt_runtime_us` (expect `-1`);
  `cat /proc/asound/cards` (expect exactly `allwinnerhdmi`).
- Operator: `sudo bash setup.sh --plan` → verify items `audio-cards`,
  `sound-socket`, `rt-sysctl`, `stale-overlays`; `journalctl -u
  gamepi-sound` for per-job stats.
- Journals: [`journal/2026-09-15-hat-audio-v3.3-status.md`](../../journal/2026-09-15-hat-audio-v3.3-status.md)
  (pin discovery + the wrong-I2S-path diagnosis),
  [`journal/2026-09-16-hat-plugin-builds-audit-and-redesign.md`](../../journal/2026-09-16-hat-plugin-builds-audit-and-redesign.md)
  and [`journal/2026-09-16-hat-plugin-v2-complete.md`](../../journal/2026-09-16-hat-plugin-v2-complete.md)
  (build audit, v2 decisions).

## What it portends

- The **public audio API today is the socket**: one client at a time, raw
  s16le 48 kHz mono only. Anything else (file playback at 22.05 kHz,
  samples, a future voice agent) must either resample to 48 kHz before
  connecting, or wait for the ALSA plugin (see
  [audio-normal-device.md](audio-normal-device.md)).
- `GAIN 0.85` and the sigma-delta parameters are **compile-time constants**
  in `tools/hat-sound.c` — volume is not tunable at runtime.
- The amp path is shared with the headphone jack
  ([headphone-jack.md](headphone-jack.md)).
- This engine is **frozen** (the v3.3 tick grid is declared final); new audio
  work happens at the *client* layer, not in the engine.