#!/usr/bin/env bash
# =============================================================================
# revert-480.sh
#
# Undo apply-960.sh and restore the previous two-desktop configuration:
#
#   :1 = 480x480   (LCD bridge + VNC 5900, 2:1 downscale)
#   :2 = 1280x720  (VNC 5901, localhost only)
#
# Restores the three *.before-960 backups created by apply-960.sh,
# restarts the :1 stack, and re-enables the :2 units.
#
# Nothing else is touched (no /etc, /boot, overlays, SSH, or VNC password
# changes). The backup files are left in place so you may re-apply.
#
# Run:    sudo bash revert-480.sh
# Re-apply: sudo bash apply-960.sh
# =============================================================================
set -euo pipefail

U_XVFB=/etc/systemd/system/gamepi-xvfb.service
U_BRIDGE=/usr/local/bin/xvfb-to-st7789.py
U_AUTO=/home/cj/.config/openbox/autostart
BK=.before-960

step() { printf '\n========== %s ==========\n' "$*"; }

[ "$(id -u)" -eq 0 ] || { echo "ERROR: run as root (sudo bash $0)"; exit 1; }

for f in "$U_XVFB$BK" "$U_BRIDGE$BK" "$U_AUTO$BK"; do
    [ -f "$f" ] || { echo "ERROR: $f not found -- has apply-960.sh been run?"; exit 1; }
done

step "1/4  Restore the three backed-up files"
cp -a "$U_XVFB$BK" "$U_XVFB"
cp -a "$U_BRIDGE$BK" "$U_BRIDGE"
cp -a "$U_AUTO$BK" "$U_AUTO"
chown cj:cj "$U_AUTO"
echo "-- bridge constants restored to: --"
grep -nE '^SOURCE_[WH] = ' "$U_BRIDGE"

step "2/4  Restart the :1 stack (order: xvfb -> openbox -> vnc -> lcd)"
systemctl daemon-reload
systemctl restart gamepi-xvfb
systemctl restart gamepi-openbox
systemctl restart gamepi-vnc
systemctl restart gamepi-lcd

step "3/4  Re-enable and start the :2 high-res units"
systemctl enable --now vnc-xvfb vnc-openbox vnc-highres

step "4/4  Verify"
sleep 2
printf -- '-- service states (expect seven lines of "active") --\n'
systemctl is-active \
    gamepi-xvfb gamepi-openbox gamepi-vnc gamepi-lcd \
    vnc-xvfb vnc-openbox vnc-highres || true

printf -- '\n-- display :1 (expect 480x480) --\n'
DISPLAY=:1 /usr/bin/xdpyinfo | grep -E 'dimensions|depth of root'

printf -- '\n-- display :2 (expect 1280x720) --\n'
DISPLAY=:2 /usr/bin/xdpyinfo | grep -E 'dimensions|depth of root'

printf -- '\n-- bridge handshake (expect "X11 root: 480x480") --\n'
journalctl -u gamepi-lcd --since "5 minutes ago" --no-pager 2>/dev/null \
    | grep -E 'X11 root|Live bridge' | tail -4 || echo '(no recent bridge log lines)'

printf -- '\n-- VNC sockets (expect 5900 and 5901) --\n'
ss -ltn | grep -E ':590[012]' || true

cat <<'NOTE'

=============================== DONE ===============================
The previous configuration is restored:
  :1 -> 480x480  -> physical LCD + VNC port 5900
  :2 -> 1280x720 -> VNC port 5901 (localhost only, SSH tunnel)
VNC password and SSH were never touched by either script.

Re-apply the single 960x960 setup with:
    sudo bash apply-960.sh
NOTE