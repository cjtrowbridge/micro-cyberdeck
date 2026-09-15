#!/usr/bin/env bash
#
# setup.sh — Reproducible bring-up for the SUNGOOYUE GamePi (ST7789V) HAT
#            on the Orange Pi Zero 3W.
#
# This script replays ONLY the verified, working setup steps from the
# "Diagnose XFCE Freeze" bring-up. All diagnostic / investigative steps
# (dmesg, gpiodetect, dtc decoding, DTB greps, failing color-fill tests,
# Spotpear reference downloads, kernel-config checks, etc.) are excluded.
#
# ASSUMPTION (as requested): an identical, freshly flashed device.
#   - Orange Pi Zero 3W (Allwinner A733)
#   - Armbian 26.8.1 Minimal, Debian 13 Trixie, vendor kernel 6.6.98
#   - The GamePi HAT is already attached to the 40-pin header
#   - You are logged in as root
#
# Result after reboot: a 960x960 Xvfb desktop (Openbox + xterm) is captured,
# scaled 4:1, rotated 180deg, and streamed to the 240x240 ST7789 over SPI3 at
# 48 MHz. VNC is available on localhost:5900 after an SSH tunnel.
#
# Run:  sudo bash setup.sh
#
set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration (override via environment if your device differs)
# ---------------------------------------------------------------------------
DECK_USER="${DECK_USER:-user}"               # desktop/user the X stack runs as
DECK_HOME="${DECK_HOME:-/home/${DECK_USER}}"
VNC_PASSWORD="${VNC_PASSWORD:-cyberdeck}"     # max 8 chars; VNC only uses the first 8
SPI_MHZ="${SPI_MHZ:-48}"                      # userspace + DT ceiling (48 MHz verified)
REBOOT_ON_DONE="${REBOOT_ON_DONE:-1}"         # 1 = reboot at the end, 0 = print instructions

ARMENV="/boot/armbianEnv.txt"
BRIDGE_PY="/usr/local/bin/xvfb-to-st7789.py"
OVERLAY_DTS="/root/spi3-cs0-${SPI_MHZ}mhz.dts"
SPI_HZ=$((SPI_MHZ * 1000000))

command -v printf >/dev/null 2>&1

banner() { printf '\n===== %s =====\n' "$*"; }

# ---------------------------------------------------------------------------
# 0. Pre-flight
# ---------------------------------------------------------------------------
banner "Pre-flight"

[ "$(id -u)" -eq 0 ] || { echo "ERROR: run as root (sudo bash setup.sh)"; exit 1; }
[ -f "$ARMENV" ]     || { echo "ERROR: $ARMENV not found — not an Armbian board?"; exit 1; }
command -v armbian-add-overlay >/dev/null 2>&1 \
    || { echo "ERROR: armbian-add-overlay not found"; exit 1; }

BOARD_MODEL="$(cat /proc/device-tree/model 2>/dev/null | tr -d '\0' || true)"
[ -n "${BOARD_MODEL}" ] || BOARD_MODEL="<unknown>"
echo "Running as root."
echo "Detected board: ${BOARD_MODEL}"
echo "Desktop user:   ${DECK_USER} (${DECK_HOME})"
echo "SPI ceiling:    ${SPI_MHZ} MHz"

# ---------------------------------------------------------------------------
# 1. Keep boot text-only (no graphical boot target)
# ---------------------------------------------------------------------------
banner "1. Force multi-user.target (text-only boot)"
systemctl set-default multi-user.target

# ---------------------------------------------------------------------------
# 2. Install the verified package set
#    (no xfce / lightdm / xorg — the Xvfb stack avoids the A733 GPU freeze)
# ---------------------------------------------------------------------------
banner "2. Install packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    xvfb \
    x11vnc \
    xterm \
    openbox \
    dbus-x11 \
    x11-utils \
    fonts-dejavu-core \
    python3-libgpiod \
    python3-spidev \
    python3-pil \
    python3-xlib \
    python3-numpy

# ---------------------------------------------------------------------------
# 3. VNC password (x11vnc -storepasswd, non-interactive)
# ---------------------------------------------------------------------------
banner "3. Store VNC password"
mkdir -p "${DECK_HOME}/.vnc"
chmod 700 "${DECK_HOME}/.vnc"
x11vnc -storepasswd "${VNC_PASSWORD}" "${DECK_HOME}/.vnc/passwd"
chmod 600 "${DECK_HOME}/.vnc/passwd"
chown -R "${DECK_USER}:${DECK_USER}" "${DECK_HOME}/.vnc"

# ---------------------------------------------------------------------------
# 4. Enable SPI3 (base overlay creates /dev/spidev3.0 + spidev3.1)
#    Armbian base overlay: sun60i-a733-spi3-cs0-cs1-spidev
# ---------------------------------------------------------------------------
banner "4. Enable SPI3 base overlay"
cp -a "$ARMENV" "${ARMENV}.before-gamepi" 2>/dev/null || true
grep -q 'spi3-cs0-cs1-spidev' "$ARMENV" \
    || echo 'overlays=spi3-cs0-cs1-spidev' >> "$ARMENV"
grep -E '^overlays=' "$ARMENV" || true

# ---------------------------------------------------------------------------
# 5. Raise the SPI3 CS0 speed ceiling to 48 MHz (custom user overlay)
# ---------------------------------------------------------------------------
banner "5. Install ${SPI_MHZ} MHz SPI speed overlay"
cat > "$OVERLAY_DTS" <<DTS
/dts-v1/;
/plugin/;

/ {
    fragment@0 {
        target-path = "/soc@3000000/spi@2543000/spidev@0";

        __overlay__ {
            spi-max-frequency = <${SPI_HZ}>;
        };
    };
};
DTS

armbian-add-overlay "$OVERLAY_DTS"

# Make sure only our speed overlay is active (drop any earlier test overlays).
# On a fresh image the line may not exist yet, so create it before the sed.
grep -q '^user_overlays=' "$ARMENV" || echo 'user_overlays=' >> "$ARMENV"
sed -i "s/^user_overlays=.*/user_overlays=spi3-cs0-${SPI_MHZ}mhz/" "$ARMENV"
grep -E '^overlays=|^user_overlays=' "$ARMENV" || true

# ---------------------------------------------------------------------------
# 6. Install the verified live bridge: Xvfb :1 -> ST7789 (NumPy, writebytes2,
#    48 MHz, 180deg rotation). This is the final production version.
# ---------------------------------------------------------------------------
banner "6. Install the Xvfb -> ST7789 bridge"
cat > "$BRIDGE_PY" <<PYEOF
#!/usr/bin/env python3

import time
import spidev
import gpiod
import numpy as np

from Xlib import X, display
from PIL import Image
from gpiod.line import Direction, Value

SOURCE_W = 960
SOURCE_H = 960
LCD_W = 240
LCD_H = 240

RST = 33
DC  = 96

# ------------------------------------------------------------
# GPIO
# ------------------------------------------------------------

chip = gpiod.Chip("/dev/gpiochip0")

rst_req = chip.request_lines(
    consumer="st7789-rst",
    config={
        RST: gpiod.LineSettings(
            direction=Direction.OUTPUT,
            output_value=Value.ACTIVE
        )
    }
)

dc_req = chip.request_lines(
    consumer="st7789-dc",
    config={
        DC: gpiod.LineSettings(
            direction=Direction.OUTPUT,
            output_value=Value.INACTIVE
        )
    }
)

def set_rst(v):
    rst_req.set_value(RST, Value.ACTIVE if v else Value.INACTIVE)

def set_dc(v):
    dc_req.set_value(DC, Value.ACTIVE if v else Value.INACTIVE)

# ------------------------------------------------------------
# SPI
# ------------------------------------------------------------

spi = spidev.SpiDev()
spi.open(3, 0)
spi.mode = 0
spi.bits_per_word = 8

spi.max_speed_hz = ${SPI_MHZ}_000_000

def command(c, data=()):
    set_dc(0)
    spi.xfer2([c])

    if data:
        set_dc(1)
        spi.xfer2(list(data))

def reset():
    set_rst(1)
    time.sleep(0.120)
    set_rst(0)
    time.sleep(0.120)
    set_rst(1)
    time.sleep(0.120)

def init_display():
    # Linux fb_st7789v-style init (no 0x01 SWRESET) — verified on this board.
    command(0x11)
    time.sleep(0.120)

    command(0x3A, [0x55])
    command(0xB2, [0x05, 0x05, 0x00, 0x33, 0x33])
    command(0xB7, [0x75])
    command(0xC2, [0x01, 0xFF])
    command(0xC3, [0x13])
    command(0xC4, [0x20])
    command(0xBB, [0x22])
    command(0xC5, [0x20])
    command(0xD0, [0xA4, 0xA1])

    command(0x36, [0x00])
    command(0x29)
    command(0x21)

    time.sleep(0.100)

def set_window(x0, y0, x1, y1):
    command(0x2A, [
        (x0 >> 8) & 0xff, x0 & 0xff,
        (x1 >> 8) & 0xff, x1 & 0xff,
    ])

    command(0x2B, [
        (y0 >> 8) & 0xff, y0 & 0xff,
        (y1 >> 8) & 0xff, y1 & 0xff,
    ])

    command(0x2C)

# ------------------------------------------------------------
# X11
# ------------------------------------------------------------

xd = display.Display(":1")
screen = xd.screen()
root = screen.root

geom = root.get_geometry()

print(f"X11 root: {geom.width}x{geom.height}")

if geom.width != SOURCE_W or geom.height != SOURCE_H:
    raise RuntimeError(
        f"Expected 960x960 Xvfb, got {geom.width}x{geom.height}"
    )

# ------------------------------------------------------------
# Image conversion
# ------------------------------------------------------------

def capture():
    raw = root.get_image(
        0,
        0,
        SOURCE_W,
        SOURCE_H,
        X.ZPixmap,
        0xffffffff,
    )

    # Xvfb :1 is 24-bit depth stored as 32bpp BGRX.
    img = Image.frombytes(
        "RGB",
        (SOURCE_W, SOURCE_H),
        raw.data,
        "raw",
        "BGRX",
    )

    # Exact 4:1 area downscale:
    # each LCD pixel represents one 4x4 area of the X desktop.
    img = img.resize(
        (LCD_W, LCD_H),
        Image.Resampling.BOX,
    )

    # Physical LCD is mounted upside down relative to X/VNC.
    return img.transpose(Image.Transpose.ROTATE_180)

def send_frame(img):
    # Pillow gives us a 240x240 RGB image here.
    # Convert the whole image to RGB565 using vectorized NumPy operations.
    a = np.asarray(img, dtype=np.uint8)

    r = a[:, :, 0].astype(np.uint16)
    g = a[:, :, 1].astype(np.uint16)
    b = a[:, :, 2].astype(np.uint16)

    rgb565 = (
        ((r & 0xF8) << 8)
        | ((g & 0xFC) << 3)
        | (b >> 3)
    )

    # ST7789 expects the high byte first.
    frame = rgb565.astype(">u2", copy=False).tobytes()

    # One full-screen RAM write.
    set_window(0, 0, LCD_W - 1, LCD_H - 1)
    set_dc(1)

    # writebytes2 accepts a buffer larger than spidev's 4096-byte
    # transfer limit and chunks it internally.
    spi.writebytes2(frame)

# ------------------------------------------------------------
# Main
# ------------------------------------------------------------

print("Resetting LCD")
reset()

print("Initializing LCD")
init_display()

print("Live bridge running: Xvfb :1 960x960 -> LCD 240x240")

try:
    while True:
        img = capture()
        send_frame(img)

except KeyboardInterrupt:
    print("\nStopped.")
PYEOF
chmod 755 "$BRIDGE_PY"

# ---------------------------------------------------------------------------
# 7. systemd services: Xvfb, Openbox, VNC, and the LCD bridge
# ---------------------------------------------------------------------------
banner "7. Install systemd services"

cat > /etc/systemd/system/gamepi-xvfb.service <<UNIT
[Unit]
Description=GamePi 960x960 Xvfb desktop
After=network.target

[Service]
Type=simple
User=${DECK_USER}
Environment=HOME=${DECK_HOME}
ExecStart=/usr/bin/Xvfb :1 -screen 0 960x960x24 -nolisten tcp -noreset
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
UNIT

cat > /etc/systemd/system/gamepi-openbox.service <<UNIT
[Unit]
Description=GamePi Openbox session
Requires=gamepi-xvfb.service
After=gamepi-xvfb.service

[Service]
Type=simple
User=${DECK_USER}
Environment=HOME=${DECK_HOME}
Environment=DISPLAY=:1
ExecStartPre=/bin/sh -c 'for i in \$(seq 1 50); do /usr/bin/xdpyinfo -display :1 >/dev/null 2>&1 && exit 0; sleep 0.2; done; exit 1'
ExecStart=/usr/bin/openbox-session
Restart=on-failure
RestartSec=1

[Install]
WantedBy=multi-user.target
UNIT

cat > /etc/systemd/system/gamepi-vnc.service <<UNIT
[Unit]
Description=GamePi localhost VNC server
Requires=gamepi-xvfb.service
After=gamepi-xvfb.service

[Service]
Type=simple
User=${DECK_USER}
Environment=HOME=${DECK_HOME}
Environment=DISPLAY=:1
ExecStartPre=/bin/sh -c 'for i in \$(seq 1 50); do /usr/bin/xdpyinfo -display :1 >/dev/null 2>&1 && exit 0; sleep 0.2; done; exit 1'
ExecStart=/usr/bin/x11vnc -display :1 -localhost -rfbport 5900 -forever -shared -rfbauth ${DECK_HOME}/.vnc/passwd
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
UNIT

cat > /etc/systemd/system/gamepi-lcd.service <<'UNIT'
[Unit]
Description=GamePi Xvfb to ST7789 LCD bridge
Requires=gamepi-xvfb.service
After=gamepi-xvfb.service

[Service]
Type=simple
Environment=HOME=/root
ExecStartPre=/bin/sh -c 'for i in $(seq 1 50); do /usr/bin/xdpyinfo -display :1 >/dev/null 2>&1 && exit 0; sleep 0.2; done; exit 1'
ExecStart=/usr/bin/python3 /usr/local/bin/xvfb-to-st7789.py
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
UNIT

# ---------------------------------------------------------------------------
# 8. Openbox autostart: visible background + terminal (so the LCD shows UI)
# ---------------------------------------------------------------------------
banner "8. Configure Openbox autostart"
mkdir -p "${DECK_HOME}/.config/openbox"
cat > "${DECK_HOME}/.config/openbox/autostart" <<'EOF'
# Visible desktop background
xsetroot -solid '#305080' &

# Temporary handheld UI: terminal
xterm \
    -fa "DejaVu Sans Mono" \
    -fs 18 \
    -geometry 80x33+8+8 \
    -title "Orange Pi Zero 3W" &
EOF
chown -R "${DECK_USER}:${DECK_USER}" "${DECK_HOME}/.config"

# ---------------------------------------------------------------------------
# 9. Enable the stack
# ---------------------------------------------------------------------------
banner "9. Enable services"
systemctl daemon-reload
systemctl enable \
    gamepi-xvfb.service \
    gamepi-openbox.service \
    gamepi-vnc.service \
    gamepi-lcd.service

# ---------------------------------------------------------------------------
# 10. Summary + reboot (reboot applies the SPI overlays)
# ---------------------------------------------------------------------------
banner "Setup complete"
cat <<SUMMARY
The display stack is configured. A reboot is required for the SPI
overlays (SPI3 enable + ${SPI_MHZ} MHz ceiling) to take effect.

After reboot, these services start automatically (no manual steps):
  gamepi-xvfb     Xvfb :1 960x960x24
  gamepi-openbox  Openbox session (blue background + terminal)
  gamepi-vnc      x11vnc on localhost:5900
  gamepi-lcd      Xvfb -> ST7789 240x240 bridge at ${SPI_MHZ} MHz

Within a few seconds of boot the LCD should show the blue background
and terminal (rotated correctly).

Connect to VNC from your PC with an SSH tunnel:
  ssh -L 5900:localhost:5900 root@<board-ip>
then point any VNC client at localhost:5900 (password: first 8 chars).

Verify after boot:
  systemctl status gamepi-xvfb gamepi-openbox gamepi-vnc gamepi-lcd
  DISPLAY=:1 xdpyinfo | grep dimensions      # expect 960x960
  ls -l /dev/spidev3.0
SUMMARY

if [ "${REBOOT_ON_DONE}" = "1" ]; then
    echo "Rebooting in 5 seconds (Ctrl-C to cancel)..."
    sleep 5
    reboot
else
    echo "REBOOT_ON_DONE=0 — run:  reboot"
fi
