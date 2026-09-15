#!/usr/bin/env bash
# =============================================================================
# apply-960.sh
#
# Upgrade the micro-cyberdeck display stack to a single 960x960 desktop.
#
#   BEFORE:  :1 = 480x480   (LCD bridge + VNC 5900)
#            :2 = 1280x720  (VNC 5901, localhost only)
#   AFTER:   :1 = 960x960   (LCD bridge + VNC 5900)  -- one screen, total
#
# What changes:
#   gamepi-xvfb.service       480x480x24 -> 960x960x24
#   /usr/local/bin/xvfb-to-st7789.py
#                             SOURCE_W/H 480 -> 960 (2:1 downscale -> 4:1)
#   ~/.config/openbox/autostart
#                             xterm resized for the larger canvas
#
# What does NOT change:
#   - SSH session (runs outside X, port 22)
#   - VNC port 5900, bind address, and password (~/.vnc/passwd is untouched)
#   - the 240x240 ST7789 panel: same init, same 48 MHz SPI, same 180 deg
#     software rotation -- the LCD just samples 4x4 X pixels per pixel now
#   - /etc, /boot, overlays, users
#
# The :2 high-res units are DISABLED, not deleted (re-enabled by
# revert-480.sh). Expected effect: the physical LCD frame rate drops
# somewhat (the bridge now transfers 4x the raw pixels per frame); VNC
# smoothness is unaffected (x11vnc has its own capture path).
#
# Run:    sudo bash apply-960.sh
# Undo:   sudo bash revert-480.sh
# =============================================================================
set -euo pipefail

U_XVFB=/etc/systemd/system/gamepi-xvfb.service
U_BRIDGE=/usr/local/bin/xvfb-to-st7789.py
U_AUTO=/home/cj/.config/openbox/autostart
BK=.before-960

step() { printf '\n========== %s ==========\n' "$*"; }

[ "$(id -u)" -eq 0 ] || { echo "ERROR: run as root (sudo bash $0)"; exit 1; }

for f in "$U_XVFB" "$U_BRIDGE" "$U_AUTO"; do
    [ -f "$f" ] || { echo "ERROR: $f not found"; exit 1; }
done

# Safety: never clobber a pristine pre-change backup.
if [ -e "$U_XVFB$BK" ] || [ -e "$U_BRIDGE$BK" ] || [ -e "$U_AUTO$BK" ]; then
    if [ "${FORCE:-0}" != "1" ]; then
        echo "ERROR: a $BK backup already exists."
        echo "Re-running apply-960.sh would overwrite the original 480x480"
        echo "backup and break revert-480.sh."
        echo "If you really want to back up the CURRENT (960) state:"
        echo "    FORCE=1 sudo bash $0"
        exit 1
    fi
fi

step "1/7  Back up the three files we will modify"
cp -a "$U_XVFB" "$U_XVFB$BK"
cp -a "$U_BRIDGE" "$U_BRIDGE$BK"
cp -a "$U_AUTO" "$U_AUTO$BK"
ls -l "$U_XVFB$BK" "$U_BRIDGE$BK" "$U_AUTO$BK"

step "2/7  gamepi-xvfb.service: 480x480 -> 960x960"
sed -i \
    -e 's/480x480x24/960x960x24/' \
    -e 's/GamePi 480x480 Xvfb desktop/GamePi 960x960 Xvfb desktop/' \
    "$U_XVFB"
grep -n '960x960' "$U_XVFB"

step "3/7  bridge script: source 480 -> 960 (downscale 2:1 -> 4:1)"
sed -i \
    -e 's/^SOURCE_W = 480$/SOURCE_W = 960/' \
    -e 's/^SOURCE_H = 480$/SOURCE_H = 960/' \
    -e 's|Expected 480x480 Xvfb, got|Expected 960x960 Xvfb, got|' \
    -e 's|Live bridge running: Xvfb :1 480x480 -> LCD 240x240|Live bridge running: Xvfb :1 960x960 -> LCD 240x240|' \
    -e 's/Exact 2:1 area downscale:/Exact 4:1 area downscale:/' \
    -e 's/each LCD pixel represents one 2x2 area of the X desktop\./each LCD pixel represents one 4x4 area of the X desktop./' \
    "$U_BRIDGE"
grep -nE '^SOURCE_[WH] = ' "$U_BRIDGE"
grep -nF '960x960' "$U_BRIDGE" || { echo "ERROR: 960x960 marker missing in bridge"; exit 1; }
grep -nF '4x4 area' "$U_BRIDGE" || { echo "ERROR: 4x4 comment marker missing in bridge"; exit 1; }

step "4/7  Openbox autostart: xterm sized for 960x960"
# (font size and geometry are tunable later, from VNC itself)
cat > "$U_AUTO" <<'EOF'
xsetroot -solid '#305080' &

xterm \
    -fa "DejaVu Sans Mono" \
    -fs 18 \
    -geometry 80x33+8+8 \
    -title "Orange Pi Zero 3W" &
EOF
chown cj:cj "$U_AUTO"
cat "$U_AUTO"

step "5/7  Restart the :1 stack (order: xvfb -> openbox -> vnc -> lcd)"
systemctl daemon-reload
systemctl restart gamepi-xvfb
systemctl restart gamepi-openbox
systemctl restart gamepi-vnc
systemctl restart gamepi-lcd

step "6/7  Disable the :2 high-res units (kept on disk)"
systemctl disable --now vnc-xvfb vnc-openbox vnc-highres

step "7/7  Verify"
sleep 2
printf -- '-- service states (expect four lines of "active") --\n'
systemctl is-active gamepi-xvfb gamepi-openbox gamepi-vnc gamepi-lcd || true

printf -- '\n-- display :1 dimensions (expect 960x960, depth 24) --\n'
DISPLAY=:1 /usr/bin/xdpyinfo | grep -E 'dimensions|depth of root'

printf -- '\n-- :1 Xvfb process --\n'
ps aux | grep -E '[X]vfb :1' || echo '!! Xvfb :1 not running'

printf -- '\n-- :2 processes (expect none) --\n'
ps aux | grep -E '[X]vfb :2' || echo 'no Xvfb :2 (as expected)'

printf -- '\n-- bridge handshake (expect "X11 root: 960x960") --\n'
journalctl -u gamepi-lcd --since "5 minutes ago" --no-pager 2>/dev/null \
    | grep -E 'X11 root|Live bridge' | tail -4 || echo '(no recent bridge log lines)'

printf -- '\n-- VNC sockets (expect 5900 only) --\n'
ss -ltn | grep -E ':590[012]' || true

cat <<'NOTE'

=============================== DONE ===============================
Connect to VNC exactly as before: same IP, port 5900, same password.
The single 960x960 desktop now feeds both VNC and the physical LCD
(4:1 downscale, 180 deg rotation unchanged). Expect a lower frame
rate on the physical LCD than before -- on screen it will look the
same, just a bit less smooth.

Undo everything (restores 480x480 :1 + 1280x720 :2):
    sudo bash revert-480.sh
NOTE