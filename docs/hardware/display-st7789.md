# Display — ST7789 240×240 (SPI3)

> Status: **Done** (mirrors the README table)

## What it is

The 240×240 SPI LCD module on the GamePi13 HAT. It is not a DRM/DRM-KMS
display — it is driven entirely in userspace: a 960×960 off-screen X11 root
(Xvfb on `:1`) is scaled down to the panel's native resolution and written to
`/dev/spidev3.0` by the bridge service.

## What we know

- **Desktop resolution is 960×960 @ depth 24** (Xvfb, `:1`). The bridge
  down-scales each frame to 240×240 for the panels. VNC serves the 960 view
  directly on port 5900 — that is how the screen stays "legible" remotely.
- **Services:** `gamepi-xvfb.service` (the X server) and
  `gamepi-lcd.service` (the bridge, `/usr/local/bin/xvfb-to-st7789.py`;
  `Requires=`/`After=` the xvfb unit). The bridge unit waits up to 10 s for
  `xdpyinfo :1` (`ExecStartPre` poll loop) and runs with
  `PYTHONUNBUFFERED=1` — without it, the bridge's startup self-check line
  `X11 root: 960x960` (stdout, Python block-buffers when not on a TTY) never
  reaches the journal, and the verify cannot see it.
- **SPI wiring:** SPI3, CS0, 48 MHz. The kernel overlay
  `spi3-cs0-48mhz` is owned by `setup.sh`; the `spi-overlay` verify item checks
  both the `user_overlays` env lines *and* the presence of `/dev/spidev3.0`.
- **Verify proof (live line):** `setup.sh` §4 verify greps the journal *since
  the current unit start* for `X11 root: 960x960` (verify item `bridge-live`;
  with a grace-wait retry path — see `docs/setup.md` for the buffering caveat).
- **Resolution migration history:** `apply-960.sh` / `revert-480.sh` at the
  repo root migrate the desktop between 960 and 480; pre-migration bridge
  backups survive on disk (`/usr/local/bin/xvfb-to-st7789.py.before-960`,
  `.before-rotation`, `.before-software-rotation`).
- **Provenance:** the custom minimal X stack (Xvfb + bridge, no full DE) exists
  because Armbian ships only CLI images for this board, and stock
  XFCE/LightDM install wedged the kernel on the A733 display driver — the full
  diagnosis is in [`Diagnose XFCE Freeze.md`](../../Diagnose%20XFCE%20Freeze.md).

## How we know

- Re-probe (as operator): `sudo bash setup.sh --plan` → verify items
  `xvfb-screen` (960x960 depth 24 via `xdpyinfo`), `unit:gamepi-xvfb`,
  `unit:gamepi-lcd`, `vnc-listen` (port 5900), `vnc-passwd`, `spi-overlay`,
  `bridge-live`.
- Bare checks (no sudo): `cat /sys/class/...` is not needed here; the unit
  states and the overlay env lines are the observable surface:
  `systemctl is-active gamepi-xvfb gamepi-lcd`,
  `cat /boot/armbian-environment | grep -i overlay`.
- The `apply-960.sh` / `revert-480.sh` scripts document every step of the
  migration in their own headers.

## What it portends

- **UIs should render at 960×960** and accept that the bridge is the only
  down-scaler. The per-frame PIL/ImageMagick pass is a real CPU cost on an 8-core
  CPU that may simultaneously be running local inference — keep that in mind
  when scheduling heavy work against the display.
- 960 is a **soft** limit: the rescue scripts prove the resolution swap is
  scripted and reversible, so the desktop can be re-tuned without a reflash.
- The screen is **display-only today** — the touch controller on the same HAT
  is unbound (see [touch-gt9271.md](touch-gt9271.md)); when it lands, its
  input coordinates will need to map to the 960×960 root, not the panel.