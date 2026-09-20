#!/usr/bin/env bash
# =============================================================================
# setup.sh — GamePi deck provisioning (re-entrant)
#
# MANDATE (governance — enforced by AGENTS.md and README.md):
#   Any change to this script — or to the provisioning behavior it
#   implements — MUST be documented in docs/setup.md in the same change,
#   and must pass the live-board verification described there before being
#   committed.
#
# What this does:
#   Convergence-style provisioning for the Orange Pi Zero 3W GamePi deck
#   (960x960 Xvfb desktop + Openbox + VNC :5900 + ST7789 240x240 SPI LCD
#   bridge). One command, every run, in any state — fresh flash, partial
#   state, or drifted:
#
#     sudo bash setup.sh                 # interactive: prompts only when needed
#     sudo bash setup.sh --plan          # read-only audit, never reboots
#     sudo bash setup.sh --yes --reboot  # unattended (agent)
#
#   Phases (fixed order): preflight -> converge -> verify -> reboot-decision
#   -> report. Every file write is compare-then-write: identical bytes never
#   touch disk, so a second run on a healthy board changes nothing and exits
#   0. (Only the overlay .sig stamp is refreshed in place; it is bookkeeping,
#   excluded from the change list.)
#
# Exit codes:
#   0   converged; verify passed (plan mode: no drift, verify passed)
#   1   drift and/or verify failure (plan: nothing applied)
#   2   preflight failure (nothing applied)
#   3   converged + verify passed, reboot required and not performed
#   130 interrupted (Ctrl+C); truthful state summary printed
#
# The last line of output is always:  RESULT: <STATUS>
# See docs/setup.md for the full provisioning contract (managed scope,
# verification matrix, reboot policy, exit codes, agent usage).
# =============================================================================

set -euo pipefail

# ── §0: usage ──────────────────────────────────────────────────────────────
usage() {
  cat <<'USAGE'
usage: sudo setup.sh [options]

Options:
  --user NAME         deck user whose desktop the services run as
                      (default: auto-detect — non-root invoker, else SUDO_USER)
  --hostname NAME     target hostname (default: micro-cyberdeck)
  --spi-mhz N         SPI max frequency in MHz (default: 48)
  --vnc-pass PW       VNC password; used only if ~/.vnc/passwd is absent
                      (VNC stores at most 8 chars; also: VNC_PASSWORD env)
  --vnc-localhost     add -localhost to the generated VNC unit (tunnel only)
  -y, --yes           unattended; never prompts
  --plan              read-only audit: desired-vs-current + verify; writes
                      nothing, never reboots (wins over all mode flags)
  --reboot            reboot at the end iff a reboot-requiring change was
                      applied this run and verify passed
  --no-reboot         never reboot; conflicts with --reboot
  --json              also emit a machine-readable JSON object
  -h, --help          this help

Exit codes: 0 converged  1 drift/verify  2 preflight  3 reboot pending
The last output line is always: RESULT: <STATUS>
See docs/setup.md for the provisioning contract.
USAGE
}

# ── §1: desired state + arguments ─────────────────────────────────────────
# The only template values that may be changed here (see docs/setup.md).
DESIRED_WIDTH=960
DESIRED_HEIGHT=960
DESIRED_DEPTH=24
DESIRED_BOARD_MODEL="orange pi zero 3w"   # /proc/device-tree/model (lowercased)
DESIRED_BOARD_ID="orangepizero3w"         # BOARD= in /etc/armbian-release
DESIRED_OVERLAY_CORE="spi3-cs0-cs1-spidev"
DESIRED_VNC_PORT=5900
# Local inference (docs/hardware/npu.md — Ollama runs CPU-only on this SoC).
# The models the deck's pipelines are expected to be able to point at.
# setup.sh does not manage the service's install (current deployment: a
# Docker container `ollama` — see resolve_ollama_cmd); it only verifies the
# local API is up and pulls these models when absent.
OLLAMA_HOST_URL="http://localhost:11434"
# Tag convention: ONE colon separates name from tag (qwen3.5:2b -> tag "2b");
# a second colon is a 400 Bad Request from the registry, so the Q8_0 variant
# is name:tag `qwen3.5:2b-q8_0`, not `qwen3.5:2b:q8_0`.
OLLAMA_MODELS=( qwen3.5:2b qwen3.5:2b-q8_0 )
BRIDGE_PY="/usr/local/bin/xvfb-to-st7789.py"
SOUND_BIN="/usr/local/bin/hat-sound"      # built from SOUND_SRC (repo)
SOUND_SRC="tools/hat-sound.c"              # repo-relative to the caller's CWD
SYSCTL_RT_DROPIN="/etc/sysctl.d/99-sched-rt.conf"   # board has no sysctl binary
MARK_DIR="/var/lib/micro-cyberdeck"

HOSTNAME_DESIRED="micro-cyberdeck"
SPI_MHZ="48"
VNC_PASS="${VNC_PASSWORD:-}"
VNC_LOCALHOST=0
DECK_USER=""
ASSUME_YES=0
PLAN_MODE=0
REBOOT_YES=0
REBOOT_NO=0
JSON_OUT=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)          DECK_USER="${2:---user requires a value}"; shift 2 ;;
    --hostname)      HOSTNAME_DESIRED="${2:---hostname requires a value}"; shift 2 ;;
    --spi-mhz)       SPI_MHZ="${2:---spi-mhz requires a value}"; shift 2 ;;
    --vnc-pass)      VNC_PASS="${2:---vnc-pass requires a value}"; shift 2 ;;
    --vnc-localhost) VNC_LOCALHOST=1; shift ;;
    -y|--yes)        ASSUME_YES=1; shift ;;
    --plan)          PLAN_MODE=1; shift ;;
    --reboot)        REBOOT_YES=1; shift ;;
    --no-reboot)     REBOOT_NO=1; shift ;;
    --json)          JSON_OUT=1; shift ;;
    -h|--help)       usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done
[[ "$SPI_MHZ" =~ ^[1-9][0-9]*$ ]] || { echo "invalid --spi-mhz: $SPI_MHZ (positive integer)" >&2; exit 2; }
if (( REBOOT_YES && REBOOT_NO )); then echo "--reboot and --no-reboot are mutually exclusive" >&2; exit 2; fi
if (( ${#VNC_PASS} > 8 )); then echo "--vnc-pass: VNC uses at most 8 characters; got ${#VNC_PASS}" >&2; exit 2; fi

SPI_HZ=$(( SPI_MHZ * 1000000 ))
OVERLAY_NAME="spi3-cs0-${SPI_MHZ}mhz"
OVERLAY_DTS="/root/${OVERLAY_NAME}.dts"

log()   { printf '[setup] %s\n' "$*"; }
die2()  { log "RESULT: PREFLIGHT_FAILED:$1" >&2; exit 2; }

CHANGED_FILES=()          # truth: what this run actually wrote
DRIFT_COUNT=0             # plan mode: desired-vs-current comparisons that failed
REBOOT_NEEDED=0
REBOOT_PENDING=0
VERIFY_FAILS=0

ITEMS_NAME=()
ITEMS_STATE=()
ITEMS_DETAIL=()
add_item() {
  # always returns 0 (set -e safe)
  ITEMS_NAME+=("$1"); ITEMS_STATE+=("$2"); ITEMS_DETAIL+=("${3:-}")
  if [[ "$2" == "DRIFT" ]]; then DRIFT_COUNT=$(( DRIFT_COUNT + 1 )); fi
  return 0
}

on_int() {
  log "INTERRUPTED (Ctrl+C) — truthful state:"
  if (( ${#CHANGED_FILES[@]} > 0 )); then
    log "files modified by this run:"
    local f
    for f in "${CHANGED_FILES[@]}"; do log "  $f"; done
  else
    log "  no files were modified"
  fi
  log "RESULT: INCOMPLETE:interrupted"
  exit 130
}
trap on_int INT

# write_if_changed TARGET MODE OWNER CONTENT — compare-then-write: never
# touches disk when bytes are identical. In plan mode it only records the
# drift item; in apply mode the parent directory is ensured first.
write_if_changed() {
  local target="$1" mode="$2" owner="$3" content="$4"
  local tmp dr
  tmp="$(mktemp /tmp/setup.sh.XXXXXX)"
  printf '%s\n' "$content" > "$tmp"
  if [[ -f "$target" ]] && cmp -s "$tmp" "$target"; then
    rm -f "$tmp"
    log "unchanged  $target"
    return 0
  fi
  if (( PLAN_MODE == 0 )); then
    dr="$(dirname "$target")"
    if [[ ! -d "$dr" ]]; then
      mkdir -p "$dr"
      chown "$owner" "$dr"
    fi
    install -m "$mode" "$tmp" "$target"
    chown -R "$owner" "$target"
    rm -f "$tmp"
    log "applied    $target"
    CHANGED_FILES+=("$target")
  else
    rm -f "$tmp"
    log "drift      $target"
    add_item "$(basename "$target")" "DRIFT" "absent or differs from the in-script template"
  fi
  return 0
}

# ── §2: preflight ──────────────────────────────────────────────────────────
preflight() {
  local errs=() bm bid
  [[ "$EUID" -eq 0 ]] || errs+=("must_run_as_root")
  [[ -f /etc/armbian-release ]] || errs+=("not_armbian")
  command -v armbian-add-overlay >/dev/null 2>&1 || errs+=("missing_armbian-add-overlay")

  # Accept either the DTS model string or the armbian-release BOARD id
  # (they differ even on the same board; either is sufficient).
  bm="$(cat /proc/device-tree/model 2>/dev/null | tr -d '\0' || true)"
  bm="$(printf '%s' "$bm" | tr '[:upper:]' '[:lower:]')"
  bid="$(awk -F'=' '$1 ~ /^[ \t]*BOARD$/ { sub(/ .*/, "", $2); print $2 }' /etc/armbian-release 2>/dev/null | tr -d ' \r')"
  [[ "$bm" == *"$DESIRED_BOARD_MODEL"* || "$bid" == *"$DESIRED_BOARD_ID"* ]] \
    || errs+=("board_model_not_orangpi_zero3w")

  # deck user: --user > non-root invoker > SUDO_USER
  if [[ -z "$DECK_USER" ]]; then
    if (( EUID != 0 )); then
      DECK_USER="$(id -un)"
    elif [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
      DECK_USER="$SUDO_USER"
    fi
  fi
  if [[ -z "$DECK_USER" ]]; then
    errs+=("deck_user_unknown(provide_--user_NAME_or_sudo_as_the_deck_user)")
  elif ! id -u "$DECK_USER" >/dev/null 2>&1; then
    errs+=("unknown_user:${DECK_USER}")
  fi

  [[ -f /boot/armbianEnv.txt ]] || errs+=("missing_/boot/armbianEnv.txt")

  # sound artifact: repo source (resolved against the caller's CWD — the
  # documented run form is `sudo bash setup.sh` from the repo root, which
  # sudo preserves) + build toolchain.
  if [[ ! -f "$SOUND_SRC" ]]; then errs+=("missing_sound_src:${SOUND_SRC}(run_from_repo_root)"); fi
  command -v gcc >/dev/null 2>&1 || errs+=("missing_gcc")

  # Ollama model management (§3g / verify row 13) shells out through curl + jq.
  command -v curl >/dev/null 2>&1 || errs+=("missing_curl")
  command -v jq   >/dev/null 2>&1 || errs+=("missing_jq")

  if (( ${#errs[@]} > 0 )); then
    local r
    for r in "${errs[@]}"; do log "preflight: $r"; done
    die2 "$(IFS=,; echo "${errs[*]}")"
  fi
  DECK_HOME="$(getent passwd "$DECK_USER" | cut -d: -f6)"
  log "preflight ok  board='$bm' user=$DECK_USER home=$DECK_HOME spi=${SPI_MHZ}MHz mode=$([[ $PLAN_MODE -eq 1 ]] && echo plan || echo apply)"
}

# ── §5: overlay templates + logic ──────────────────────────────────────────
generate_dts() {
  cat <<DTS
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
}

# Compiled user-overlay location. Armbian builds user overlays into
# /boot/overlay-user/; /boot/armbian-overlays/ is the older name and is
# tolerated for compatibility. Prints the path when found; returns 1 (with
# the expected location) when the overlay has not been compiled here.
find_dtbo() {
  local d
  for d in /boot/overlay-user /boot/armbian-overlays; do
    if [[ -f "$d/${OVERLAY_NAME}.dtbo" ]]; then
      printf '%s\n' "$d/${OVERLAY_NAME}.dtbo"
      return 0
    fi
  done
  printf '%s\n' "/boot/overlay-user/${OVERLAY_NAME}.dtbo"
  return 1
}

# Overlay state signature: what the machine should boot into, from disk.
overlay_sig() {
  local dtbo h
  if ! dtbo="$(find_dtbo)"; then
    if [[ -f "$OVERLAY_DTS" ]]; then
      h="$(sha256sum "$OVERLAY_DTS" | awk '{print $1}')"
    else
      h="absent"
    fi
  else
    h="$(sha256sum "$dtbo" | awk '{print $1}')"
  fi
  local ov uv
  ov="$(awk -F'=' '$1=="overlays" {print $2}' /boot/armbianEnv.txt 2>/dev/null | xargs 2>/dev/null || true)"
  uv="$(awk -F'=' '$1=="user_overlays" {print $2}' /boot/armbianEnv.txt 2>/dev/null | xargs 2>/dev/null || true)"
  printf '%s:%s|%s' "$h" "$ov" "$uv"
}

# user_overlays set semantics: the *set* of user overlays converges, not a
# single value. overlay_desired_set() is the desired set, sorted and
# space-joined for a byte-compare; overlay_uvs() is the same normalization of
# what is on disk (Armbian separates with spaces; order is not guaranteed).
# Today the set is {spi3-cs0-<MHZ>mhz}; the buttons' gpio-keys overlay joins
# it later by extending this one function (docs/setup.md "Overlay set").
overlay_desired_set() {
  printf '%s\n' $OVERLAY_NAME | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//'
}
overlay_uvs() {
  local uv
  uv="$(awk -F'=' '$1=="user_overlays" {print $2}' /boot/armbianEnv.txt 2>/dev/null | xargs 2>/dev/null || true)"
  [[ -n "$uv" ]] || return 0
  printf '%s\n' $uv | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//'
}

apply_overlay() {
  local backup="/root/armbianEnv.txt.before-gamepi"
  local sig_before sig_after
  local uv ov dtbo

  sig_before="$(overlay_sig)"
  local stamp="$MARK_DIR/overlay.sig"
  if [[ -f "$stamp" ]] && [[ "$(cat "$stamp" 2>/dev/null || true)" != "$sig_before" ]]; then
    log "warn: overlay state differs from the last record written by this script"
    REBOOT_NEEDED=1
  fi

  if (( PLAN_MODE == 1 )); then
    if ! cmp -s <(generate_dts) "$OVERLAY_DTS" 2>/dev/null; then
      add_item "overlay-dts" "DRIFT" "$OVERLAY_DTS absent or differs from template (apply mode rebuilds it)"
    fi
    if ! find_dtbo >/dev/null; then
      add_item "overlay-dtbo" "DRIFT" "/boot/overlay-user/${OVERLAY_NAME}.dtbo missing (armbian-add-overlay has not run here)"
    else
      log "unchanged  $(find_dtbo)"
    fi
    uv="$(awk -F'=' '$1=="user_overlays" {print $2}' /boot/armbianEnv.txt 2>/dev/null | xargs 2>/dev/null || true)"
    ov="$(awk -F'=' '$1=="overlays" {print $2}' /boot/armbianEnv.txt 2>/dev/null | xargs 2>/dev/null || true)"
    if [[ "$(overlay_uvs)" != "$(overlay_desired_set)" ]]; then
      add_item "user_overlays" "DRIFT" "value='${uv}' desired='${OVERLAY_NAME}' (set-converge)"
    fi
    if [[ "$ov" != *"$DESIRED_OVERLAY_CORE"* ]]; then
      add_item "overlays" "DRIFT" "missing stock core overlay $DESIRED_OVERLAY_CORE"
    fi
    # NOTE: pure [-f] tests, not `ls a b | grep -q .` — under set -o pipefail
    # that pipeline reports failure whenever EITHER operand is missing (GNU ls
    # exits 2), so a stale file present in only one of the two dirs went
    # undetected (observed live 2026-09-15).
    for f in es8388-audio i2c0; do
      if [[ -f "/boot/overlay-user/${f}.dtbo" || -f "/boot/armbian-overlays/${f}.dtbo" ]]; then
        add_item "stale-overlay:${f}" "DRIFT" "wrong-audio-path overlay present (removed by apply)"
      fi
    done
    return 0
  fi

  # one-time safety backup of the pristine env file (never churned again)
  if [[ ! -f "$backup" ]]; then
    cp -a /boot/armbianEnv.txt "$backup"
    log "backup     $backup"
    CHANGED_FILES+=("$backup")
  fi

  # DTS: compare-then-write; rebuild the .dtbo only when needed
  local dts_write=0
  if ! cmp -s <(generate_dts) "$OVERLAY_DTS" 2>/dev/null; then
    generate_dts > "$OVERLAY_DTS"
    log "applied    $OVERLAY_DTS"
    dts_write=1
    CHANGED_FILES+=("$OVERLAY_DTS")
  else
    log "unchanged  $OVERLAY_DTS"
  fi

  if ! dtbo="$(find_dtbo)"; then dtbo="/boot/overlay-user/${OVERLAY_NAME}.dtbo"; fi
  if (( dts_write == 1 )) || [[ ! -f "$dtbo" ]]; then
    log "armbian-add-overlay ${OVERLAY_NAME} ..."
    local out
    if ! out="$(armbian-add-overlay "$OVERLAY_DTS" 2>&1)"; then
      log "overlay apply failed:"
      printf '%s\n' "$out"
      log "RESULT: INCOMPLETE:overlay"
      exit 1
    fi
    dtbo="$(find_dtbo || printf '%s' "$dtbo")"
    log "overlay    built $dtbo by armbian"
  else
    log "unchanged  $dtbo"
  fi

  # env lines: additive normalization (only our two lines, exact values)
  # user_overlays converges to the DESIRED SET (not a single value): the
  # current set is {spi3-cs0-<MHZ>mhz}; future overlays (the buttons'
  # gpio-keys) are added to overlay_desired_set() and the line is rebuilt,
  # never sed-overwritten.
  if ! grep -q "^overlays=.*${DESIRED_OVERLAY_CORE}\b" /boot/armbianEnv.txt; then
    echo "overlays=${DESIRED_OVERLAY_CORE}" >> /boot/armbianEnv.txt
    log "env        +overlays=${DESIRED_OVERLAY_CORE}"
    CHANGED_FILES+=("/boot/armbianEnv.txt")
  fi
  # stale audio-path overlays: the HAT amplifier is GPIO/PWM (header pin 12
  # = PB5 = gpiochip0 line 37), NOT I2S. es8388-audio + i2c0 were the wrong
  # I2C/I2S audio path probed on 2026-09-15; they probe and bind nothing on
  # this HAT (verified: no i2c-0 adapter, no extra ALSA card) — a zombie
  # probe at every boot. Remove with a logged why; do not re-add.
  local stale f
  for f in es8388-audio i2c0; do
    for stale in /boot/overlay-user/${f}.dtbo /boot/armbian-overlays/${f}.dtbo; do
      if [[ -f "$stale" ]]; then
        rm -f "$stale"
        log "removed    $stale (es8388/i2c0 = wrong I2S audio path; HAT amp is GPIO/PWM)"
        CHANGED_FILES+=("$stale")
      fi
    done
  done

  uv="$(overlay_uvs)"
  uvd="$(overlay_desired_set)"
  if [[ "$uv" == "$uvd" ]]; then
    log "unchanged  env:user_overlays"
  else
    if grep -q "^user_overlays=" /boot/armbianEnv.txt; then
      sed -i "s|^user_overlays=.*|user_overlays=${uvd}|" /boot/armbianEnv.txt
    else
      echo "user_overlays=${uvd}" >> /boot/armbianEnv.txt
    fi
    log "env        set user_overlays=${uvd}"
    CHANGED_FILES+=("/boot/armbianEnv.txt")
  fi

  # stamp: record the overlay state this run put on disk; refreshed in place
  # (bookkeeping — never reported as a change), so the second run on a
  # healthy board stays CONVERGED
  sig_after="$(overlay_sig)"
  mkdir -p "$MARK_DIR"
  printf '%s\n' "$sig_after" > "$stamp"

  if [[ "$sig_before" != "$sig_after" ]] || (( dts_write == 1 )); then
    REBOOT_NEEDED=1
  fi
}

# ── §6: artifact templates (byte-canonical with the verified live
#    configuration: units, bridge, autostart — see docs/setup.md) ──────────
generate_bridge() {
  cat <<BRIDGE
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

# Keep known-good speed for first live test.
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
BRIDGE
}

generate_unit() {
  local name="$1" localhost_flag=""
  case "$name" in
    xvfb)
      cat <<EOF
[Unit]
Description=GamePi ${DESIRED_WIDTH}x${DESIRED_HEIGHT} Xvfb desktop
After=network.target

[Service]
Type=simple
User=${DECK_USER}
Environment=HOME=${DECK_HOME}
ExecStart=/usr/bin/Xvfb :1 -screen 0 ${DESIRED_WIDTH}x${DESIRED_HEIGHT}x${DESIRED_DEPTH} -nolisten tcp -noreset
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
EOF
      ;;
    openbox)
      cat <<EOF
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
EOF
      ;;
    vnc)
      if (( VNC_LOCALHOST )); then localhost_flag=" -localhost"; fi
      cat <<EOF
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
ExecStart=/usr/bin/x11vnc -display :1 -rfbport ${DESIRED_VNC_PORT} -forever -shared${localhost_flag} -rfbauth ${DECK_HOME}/.vnc/passwd
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
EOF
      ;;
    lcd)
      cat <<EOF
[Unit]
Description=GamePi Xvfb to ST7789 LCD bridge
Requires=gamepi-xvfb.service
After=gamepi-xvfb.service

[Service]
Type=simple
Environment=HOME=/root
# Python3 block-buffers stdout when not on a TTY; without this the bridge's
# startup self-check lines ("X11 root: 960x960", the verify's journal proof)
# never reach the systemd journal. One intentional deviation from the
# original live unit (see docs/setup.md).
Environment=PYTHONUNBUFFERED=1
ExecStartPre=/bin/sh -c 'for i in \$(seq 1 50); do /usr/bin/xdpyinfo -display :1 >/dev/null 2>&1 && exit 0; sleep 0.2; done; exit 1'
ExecStart=/usr/bin/python3 ${BRIDGE_PY}
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
EOF
      ;;
    sound)
      cat <<EOF
[Unit]
Description=GamePi HAT sound daemon (hat-sound v3.6: pin hold + socket engine)
After=multi-user.target

[Service]
Type=simple
Environment=HOME=/root
# v3.6 daemon: acquires gpiochip0 line 37 (PB5, header pin 12) ONCE at start,
# binds /run/gamepi-sound.sock (0666, set engine-side: fchmod(fd) +
# chmod(path) — this kernel needs the path chmod to move the path-mode view,
# see socket_bind in tools/hat-sound.c), and serves serialized-on-accept PCM jobs
# (raw s16le 48 kHz mono, one client at a time). The tick path is the frozen
# v3.3 grid. Per-job stats go to the journal, not to /run stats files.
# No RuntimeDirectory: the socket lives at /run/gamepi-sound.sock (directly in
# /run, which is tmpfs — cleared at boot; the engine unlinks it on clean
# exit and re-unlinks a stale one at bind).
# Stop is clean: SIGTERM/SIGINT are installed via sigaction WITHOUT
# SA_RESTART, so the engine exits promptly from its blocking accept()
# (pin released LOW) instead of the silently-restarted accept that
# forced a 90 s TimeoutStopSec + SIGKILL;
# Restart=always re-owns the pin and re-binds the socket if the process dies.
ExecStart=/usr/local/bin/hat-sound -d /run/gamepi-sound.sock
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
EOF
      ;;
    *) echo "generate_unit: unknown unit $name" >&2; return 1 ;;
  esac
}

generate_autostart() {
  cat <<'EOF'
xsetroot -solid '#305080' &

# Terminal is sized to the 960x960 :1 canvas. The Xvfb reports 100 DPI,
# so xterm -fs <points> turns into ~1.33 px/pt per cell. fs 13 (DejaVu)
# -> 11x22 px cells -> 888x908 px outer window, centered with ~38 px
# margin. fs 18 (the original 960 value) -> ~24 px cells -> a 1206x1019
# window that overflows the canvas; do not raise the font without also
# cutting columns/rows to stay <= 952x952 px.
xterm \
    -fa "DejaVu Sans Mono" \
    -fs 13 \
    -geometry 80x33+38+8 \
    -title "Orange Pi Zero 3W" &
EOF
}

# Build tools/hat-sound.c -> SOUND_BIN with the repo's canonical flags.
# compare-then-install: if the fresh build is byte-identical to the installed
# binary, nothing is touched — a healthy re-run writes zero bytes (the
# stale-binary drift that burned the 2026-09-15 sessions is exactly what this
# checks for at provision time). Any install lands in CHANGED_FILES, which the
# unit-repair phase turns into a restart, so the running idle process is always
# on the installed binary.
# Plan mode: report source presence only; build nothing, install nothing.
build_sound() {
  local src="$SOUND_SRC" tmp out
  if (( PLAN_MODE == 1 )); then
    if [[ -f "$src" ]]; then
      add_item "sound-src" "OK" "source $src present (build happens in apply mode)"
    else
      add_item "sound-src" "DRIFT" "missing $src"
    fi
    return 0
  fi
  if [[ ! -f "$src" ]]; then
    log "RESULT: INCOMPLETE:sound-src — missing $src"
    exit 1
  fi
  tmp="$(mktemp "${TMPDIR:-/tmp}/hat-sound-build.XXXXXX")"
  if ! out="$(gcc -O2 -Wall -Wextra "$src" -o "$tmp" -lm -lpthread 2>&1)"; then
    log "hat-sound build FAILED:"
    printf '%s\n' "$out"
    rm -f "$tmp"
    log "RESULT: INCOMPLETE:sound-build"
    exit 1
  fi
  if [[ -f "$SOUND_BIN" ]] && cmp -s "$SOUND_BIN" "$tmp"; then
    rm -f "$tmp"
    log "unchanged  $SOUND_BIN (built from $src)"
  else
    install -m 755 "$tmp" "$SOUND_BIN"
    rm -f "$tmp"
    log "applied    $SOUND_BIN (rebuilt from $src)"
    CHANGED_FILES+=("$SOUND_BIN")
  fi
  return 0
}

# ── Ollama local models (docs/setup.md: Managed scope, docs/hardware/npu.md) ─
# The Ollama binary installs with the vendor image; setup.sh manages its model
# library: verify row 13 proves the local API is up and the desired models are
# present, §3g pulls any that are absent. OLLAMA_HOST is not honored (the deck's
# service is expected on localhost:11434; an override is drift the verify names).
ollama_api_ok() {
  curl -sf --max-time 3 "$OLLAMA_HOST_URL/api/version" 2>/dev/null >/dev/null
}
# Prints the names Ollama currently serves (empty when the service cannot be
# reached or the response is not the expected JSON).
ollama_model_names() {
  local out
  if out="$(curl -sf --max-time 3 "$OLLAMA_HOST_URL/api/tags" 2>/dev/null | jq -r '.models[].name' 2>/dev/null)"; then
    [[ -n "$out" ]] && printf '%s\n' "$out"
  fi
  return 0
}
ollama_missing_models() {
  local m n found
  local -a names=()
  names=( $(ollama_model_names || true) )
  for m in "${OLLAMA_MODELS[@]}"; do
    found=0
    for n in "${names[@]:-}"; do
      if [[ "$n" == "$m" ]]; then found=1; break; fi
    done
    if (( found == 0 )); then printf '%s\n' "$m"; fi
  done
  return 0
}
# A pull can run through a host `ollama` CLI or through a container: the
# current board's deployment IS a Docker container (name `ollama`, 11434
# published; model volume /var/lib/docker/volumes/ollama/_data <->
# /root/.ollama inside the container; the HOST has no ollama binary at all —
# that is the resolved "/bin/ollama identity anomaly", docs/hardware/npu.md).
# resolve_ollama_cmd tries the host CLI first, then the Docker fallback;
# ollama_pull uses whichever was resolved.
OLLAMA_PULL_KIND=""
OLLAMA_BIN=""
resolve_ollama_cmd() {
  local c
  for c in $(command -v ollama 2>/dev/null || true) /usr/bin/ollama /usr/local/bin/ollama /bin/ollama; do
    if [[ -n "$c" && -x "$c" ]]; then OLLAMA_PULL_KIND="host"; OLLAMA_BIN="$c"; return 0; fi
  done
  if command -v docker >/dev/null 2>&1 \
     && docker ps --format '{{.Names}}' 2>/dev/null | grep -qx 'ollama'; then
    OLLAMA_PULL_KIND="docker"
    return 0
  fi
  return 1
}
# Bounded pull: Ollama's progress output is swallowed; the exit status
# carries the result (0 = the model is now in the local library).
ollama_pull() {
  local rc=0 out=""
  case "$OLLAMA_PULL_KIND" in
    host)   out="$("$OLLAMA_BIN" pull "$1" 2>&1)" || rc=$? ;;
    docker) out="$(docker exec ollama ollama pull "$1" 2>&1)" || rc=$? ;;
    *)      return 1 ;;
  esac
  if (( rc != 0 )); then
    log "ollama pull error: $(tail -n 3 <<<"$out" | tr '\n' ' ')"
    return 1
  fi
  return 0
}

# ── §3: converge ───────────────────────────────────────────────────────────
converge() {
  local pkg curdef curhost vncfile
  local -a PKGS=( x11vnc xvfb xterm openbox dbus-x11 x11-utils fonts-dejavu-core \
                  python3-xlib python3-libgpiod python3-spidev python3-pil python3-numpy )
  local -a missing=()

  # (a) default target: multi-user (no console getty; headless)
  curdef="$(systemctl get-default 2>/dev/null || true)"
  if [[ "$curdef" != "multi-user.target" ]]; then
    if (( PLAN_MODE == 1 )); then
      add_item "default-target" "DRIFT" "current=$curdef desired=multi-user.target"
    else
      log "default-target $curdef -> multi-user.target"
      systemctl set-default multi-user.target
      CHANGED_FILES+=("/etc/systemd/system/default.target")
    fi
  fi

  # (b) packages — apt only runs when something is missing
  for pkg in "${PKGS[@]}"; do dpkg -s "$pkg" >/dev/null 2>&1 || missing+=("$pkg"); done
  if (( ${#missing[@]} > 0 )); then
    if (( PLAN_MODE == 1 )); then
      add_item "packages" "DRIFT" "missing: ${missing[*]}"
    else
      log "installing packages: ${missing[*]}"
      export DEBIAN_FRONTEND=noninteractive
      apt-get update -qq
      apt-get install -y --no-install-recommends "${missing[@]}"
    fi
  fi

  # (c) hostname
  curhost="$(hostnamectl --static 2>/dev/null || hostname)"
  if [[ "$curhost" != "$HOSTNAME_DESIRED" ]]; then
    if (( PLAN_MODE == 1 )); then
      add_item "hostname" "DRIFT" "current=$curhost desired=$HOSTNAME_DESIRED"
    else
      log "hostname $curhost -> $HOSTNAME_DESIRED"
      hostnamectl set-hostname "$HOSTNAME_DESIRED"
    fi
  fi

  # (g) ollama models: verify-or-pull. setup.sh does not manage the service
  # install (current deployment: Docker container `ollama`), so an
  # unreachable API is verify drift/failure, not something this step can
  # repair. Plan mode only reports.
  local m missing_models
  if ollama_api_ok; then
    missing_models="$(ollama_missing_models)"
    if (( PLAN_MODE == 1 )); then
      if [[ -n "$missing_models" ]]; then
        add_item "ollama-models" "DRIFT" "absent from the local library (apply pulls): $(printf '%s' "$missing_models" | tr '\n' ' ')"
      fi
    else
      if [[ -n "$missing_models" ]]; then
        if resolve_ollama_cmd; then
          while IFS= read -r m; do
            [[ -n "$m" ]] || continue
            log "pulling $m via $OLLAMA_PULL_KIND (this can take a while)"
            if ollama_pull "$m"; then
              log "pulled   $m"
            else
              log "pull failed: $m (verify row 13 reports it)"
            fi
          done <<< "$missing_models"
        else
          add_item "ollama-pull-path" "DRIFT" "no way to pull models: no host ollama CLI and no running 'ollama' docker container (fix the deployment, re-run)"
        fi
      fi
    fi
  fi

  # (d) VNC password — USER STATE: create-only, never overwritten, never deleted
  vncfile="$DECK_HOME/.vnc/passwd"
  if [[ ! -f "$vncfile" ]]; then
    if (( PLAN_MODE == 1 )); then
      add_item "vnc-passwd" "DRIFT" "absent — next apply run creates it (provided or generated password)"
    else
      mkdir -p "$DECK_HOME/.vnc"
      chmod 700 "$DECK_HOME/.vnc"
      if [[ -z "$VNC_PASS" ]]; then
        VNC_PASS="$(LC_ALL=C tr -dc 'A-Za-z0-9' </dev/urandom | head -c 8)"
        log "VNC password generated (shown once, not stored in log): $VNC_PASS"
      else
        log "VNC password: using provided password"
      fi
      /usr/bin/x11vnc -storepasswd "$VNC_PASS" "$vncfile" >/dev/null
      chown -R "$DECK_USER:$DECK_USER" "$DECK_HOME/.vnc"
      chmod 600 "$vncfile"
      log "applied    $vncfile (created; this script never touches it again)"
      CHANGED_FILES+=("$vncfile")
    fi
  fi

  # (e) overlay (§5)
  apply_overlay

  # (f) artifacts (§6): bridge, sound binary, RT drop-in, five units, autostart
  local bridge_content unit target content autostart autostart_content
  bridge_content="$(generate_bridge)"
  write_if_changed "$BRIDGE_PY" 755 root "$bridge_content"

  build_sound
  # The board has no sysctl binary; the drop-in is what systemd-sysctl
  # (static, always present) applies at every boot. Runtime today happens to
  # be -1 already; the file is what makes that survive.
  write_if_changed "$SYSCTL_RT_DROPIN" 644 root 'kernel.sched_rt_runtime_us = -1'

  for unit in xvfb openbox vnc lcd sound; do
    target="/etc/systemd/system/gamepi-${unit}.service"
    content="$(generate_unit "$unit")"
    write_if_changed "$target" 644 root "$content"
  done

  autostart="$DECK_HOME/.config/openbox/autostart"
  autostart_content="$(generate_autostart)"
  write_if_changed "$autostart" 644 "$DECK_USER" "$autostart_content"

  # (g) units repair: a unit file written this run reaches the running
  #     instance only via `systemctl restart` — `enable --now` is a no-op
  #     on an already-active unit and would silently keep the stale process
  #     running on the old file/environment (live bug found 2026-09-14).
  #     Units that are simply not active get `enable --now`. The managed
  #     units carry no user state; the verify that runs immediately after
  #     proves the (re)start worked.
  if (( PLAN_MODE == 0 )); then
    local u f svc touch_units=0
    local -A unit_written=()
    for f in "${CHANGED_FILES[@]}"; do
      if [[ "$f" == /etc/systemd/system/gamepi-*.service ]]; then
        unit_written["${f#/etc/systemd/system/}"]=1
        touch_units=1
      fi
    done
    # A new hat-sound binary is a unit-file change in disguise: the running
    # process must be restarted onto it or the service keeps the old code.
    for f in "${CHANGED_FILES[@]}"; do
      if [[ "$f" == "$SOUND_BIN" ]]; then
        unit_written["gamepi-sound.service"]=1
        touch_units=1
      fi
    done
    for u in xvfb openbox vnc lcd sound; do
      if [[ "$(systemctl is-active "gamepi-$u.service" 2>/dev/null || true)" != "active" ]]; then
        touch_units=1
      fi
    done
    if (( touch_units == 1 )); then
      log "systemd: daemon-reload + restart changed / start inactive managed units"
      systemctl daemon-reload
      for u in xvfb openbox vnc lcd sound; do
        svc="gamepi-$u.service"
        if [[ -n "${unit_written[$svc]:-}" ]]; then
          log "systemd: restarting $svc (unit file written this run)"
          systemctl restart "$svc"
        elif [[ "$(systemctl is-active "$svc" 2>/dev/null || true)" != "active" ]]; then
          log "systemd: (re)starting $svc (was not active)"
          systemctl enable --now "$svc" >/dev/null
        fi
      done
      sleep 2
      local t
      for t in $(seq 1 10); do
        if DISPLAY=:1 xdpyinfo >/dev/null 2>&1; then break; fi
        sleep 1
      done
    fi
  fi
}

# ── §4: verify (pure observation, no writes) ───────────────────────────────
# On a run that just applied overlay changes, items that can only be proven
# after reboot (spidev node, bridge boot output, units that start at boot on
# a fresh board) report SKIPPED — not FAIL — so the reboot prompt can fire.
run_verify() {
  local xd u hz uv ov st ts ep jview alc ncards rtv stale_found i2cdev

  if xd="$(DISPLAY=:1 /usr/bin/xdpyinfo 2>/dev/null)"; then
    if printf '%s\n' "$xd" | grep -Eq 'dimensions:[[:space:]]+960x960' \
       && printf '%s\n' "$xd" | grep -Eq 'depth of root window:[[:space:]]+24'; then
      add_item "xvfb-screen" "PASS" "960x960 depth 24"
    else
      add_item "xvfb-screen" "FAIL" "xdpyinfo dimension/depth mismatch"
    fi
  else
    add_item "xvfb-screen" "FAIL" "xdpyinfo :1 unreachable"
  fi

  for u in xvfb openbox vnc lcd sound; do
    st="$(systemctl is-active "gamepi-$u.service" 2>/dev/null || true)"
    if [[ "$st" == "active" ]]; then
      add_item "unit:$u" "PASS"
    elif (( PLAN_MODE == 0 )) && [[ "$st" == "inactive" ]] && (( REBOOT_NEEDED == 1 )); then
      add_item "unit:$u" "SKIPPED" "not started yet; starts after reboot"
    else
      add_item "unit:$u" "FAIL" "is-active: $st"
    fi
  done

  if ss -lnt 2>/dev/null | grep -Eq "[:.]5900[[:space:]]"; then
    add_item "vnc-listen" "PASS" "port 5900 listening"
  else
    add_item "vnc-listen" "FAIL" "nothing listening on 5900"
  fi

  if [[ -s "$DECK_HOME/.vnc/passwd" ]]; then
    add_item "vnc-passwd" "PASS"
  else
    add_item "vnc-passwd" "FAIL" "absent or empty"
  fi

  uv="$(awk -F'=' '$1=="user_overlays" {print $2}' /boot/armbianEnv.txt 2>/dev/null | xargs 2>/dev/null || true)"
  ov="$(awk -F'=' '$1=="overlays" {print $2}' /boot/armbianEnv.txt 2>/dev/null | xargs 2>/dev/null || true)"
  if [[ "$uv" == "$OVERLAY_NAME" && "$ov" == *"$DESIRED_OVERLAY_CORE"* ]]; then
    if [[ -c /dev/spidev3.0 ]]; then
      add_item "spi-overlay" "PASS" "env lines ok; /dev/spidev3.0 present"
    elif (( PLAN_MODE == 0 )) && (( REBOOT_NEEDED == 1 )); then
      add_item "spi-overlay" "SKIPPED" "env lines ok; /dev/spidev3.0 appears after reboot"
    else
      add_item "spi-overlay" "FAIL" "env lines ok but /dev/spidev3.0 missing"
    fi
  else
    add_item "spi-overlay" "FAIL" "user_overlays='${uv}' desired='${OVERLAY_NAME}'; core-present=$([[ "$ov" == *"$DESIRED_OVERLAY_CORE"* ]] && echo yes || echo no)"
  fi

  # sound: RT budget + audio topology (fold-in of 2026-09-15, docs/setup.md)
  rtv="$(cat /proc/sys/kernel/sched_rt_runtime_us 2>/dev/null || true)"
  if [[ "$rtv" == "-1" ]]; then
    add_item "rt-sysctl" "PASS" "sched_rt_runtime_us=-1 (persisted by $SYSCTL_RT_DROPIN)"
  else
    add_item "rt-sysctl" "FAIL" "sched_rt_runtime_us='${rtv}' desired='-1'" 
  fi

  # -F'[][]' fields: $1=index, $2=CARD NAME, $3=driver string — the name is $2
  # (verified live: ' 0 [allwinnerhdmi  ]: allwinner-hdmi - allwinner-hdmi').
  # $2 (card name) + an explicit count enforce "exactly one card" per docs/setup.md.
  alc="$(awk -F'[][]' '/^[[:space:]]*[0-9]/ {print $2}' /proc/asound/cards 2>/dev/null | tr -d ' \r' | tr '\n' ' ' | sed 's/ $//')"
  ncards="$(grep -c '^[[:space:]]*[0-9]' /proc/asound/cards 2>/dev/null || true)"
  if [[ "$alc" == "allwinnerhdmi" && "$ncards" == "1" ]]; then
    add_item "audio-cards" "PASS" "single card allwinnerhdmi (HAT audio is GPIO/PWM pin 12, not I2S)"
  else
    add_item "audio-cards" "FAIL" "cards='${alc}' count='${ncards}' desired='exactly one card: allwinnerhdmi'"
  fi

  # v3.6: the gamepi-sound unit runs the socket engine (plan 2026-09-15 WS2),
  # so a healthy board has /run/gamepi-sound.sock present. Plan mode before
  # the first v3.6 apply legitimately FAILs this (the old unit ran -i).
  if [[ -S /run/gamepi-sound.sock ]]; then
    add_item "sound-socket" "PASS" "daemon socket present (hat-sound v3.6 engine)"
  else
    add_item "sound-socket" "FAIL" "/run/gamepi-sound.sock absent (gamepi-sound not on the v3.6 daemon)"
  fi

  # same [-f] rationale as the plan-mode loop above (pipefail + GNU ls exit 2)
  stale_found=""
  for f in es8388-audio i2c0; do
    if [[ -f "/boot/overlay-user/${f}.dtbo" || -f "/boot/armbian-overlays/${f}.dtbo" ]]; then
      stale_found+="$f "
    fi
  done
  i2cdev="$(ls -d /sys/bus/i2c/devices/i2c-0 2>/dev/null || true)"
  if [[ -n "$i2cdev" ]]; then stale_found+="i2c-0 "; fi
  if [[ -z "$stale_found" ]]; then
    add_item "stale-overlays" "PASS" "no es8388/i2c0 overlays, no i2c-0 adapter"
  else
    add_item "stale-overlays" "FAIL" "stale: ${stale_found% }"
  fi

  # The self-check line is printed once, at process start. Anchor the
  # journal window to the unit's CURRENT start so (a) a healthy long-running
  # bridge keeps passing once the line has rotated out of the full history
  # (stale-evidence trap) and (b) a dead unit's ancient lines cannot make it
  # pass. Proven on the live board 2026-09-14: `date -d` parses the
  # `systemctl show` timestamp and `journalctl --since "@<epoch>"` works.
  ts="$(systemctl show -P ExecMainStartTimestamp --value gamepi-lcd.service 2>/dev/null || true)"
  ep="$(date -d "$ts" +%s 2>/dev/null || true)"
  if [[ -n "$ep" ]]; then
    jview="$(journalctl -u gamepi-lcd.service --no-pager --since "@$ep" 2>/dev/null || true)"
  else
    jview="$(journalctl -u gamepi-lcd.service --no-pager 2>/dev/null || true)"
  fi
  if printf '%s\n' "$jview" | grep -q "X11 root: 960x960"; then
    add_item "bridge-live" "PASS" "journal since current unit start: 'X11 root: 960x960'"
  elif (( PLAN_MODE == 0 )) && (( REBOOT_NEEDED == 1 )); then
    add_item "bridge-live" "SKIPPED" "bridge boot output appears after reboot"
  elif (( PLAN_MODE == 0 )); then
    # apply mode, no reboot pending: the bridge connects a beat after Xvfb
    # comes up — one grace re-read of the same window before declaring FAIL
    sleep 5
    if [[ -n "$ep" ]]; then
      jview="$(journalctl -u gamepi-lcd.service --no-pager --since "@$ep" 2>/dev/null || true)"
    else
      jview="$(journalctl -u gamepi-lcd.service --no-pager 2>/dev/null || true)"
    fi
    if printf '%s\n' "$jview" | grep -q "X11 root: 960x960"; then
      add_item "bridge-live" "PASS" "journal since current unit start: 'X11 root: 960x960' (after grace wait)"
    else
      add_item "bridge-live" "FAIL" "no 'X11 root: 960x960' since the current unit start (stdout may be buffered, or the process predates the current unit file — see docs/setup.md)"
    fi
  else
    add_item "bridge-live" "FAIL" "no 'X11 root: 960x960' since the current unit start"
  fi

  if hz="$(cat /sys/class/spi-master/spi3/max_speed_hz 2>/dev/null)"; then
    log "info: spi3 max_speed_hz=$hz (informational, not gating)"
  fi

  # Ollama local inference: API reachability, then the desired model set.
  # Apply mode pulls absent models in §3g before this row runs; plan mode
  # reports the gap as drift instead.
  local m missing_models
  if ollama_api_ok; then
    missing_models="$(ollama_missing_models)"
    if [[ -z "$missing_models" ]]; then
      add_item "ollama-models" "PASS" "$(IFS=' '; echo "${OLLAMA_MODELS[*]}") present via $OLLAMA_HOST_URL"
    elif (( PLAN_MODE == 1 )); then
      add_item "ollama-models" "FAIL" "absent (apply pulls): $(printf '%s' "$missing_models" | tr '\n' ' ')"
    else
      add_item "ollama-models" "FAIL" "pull did not land: $(printf '%s' "$missing_models" | tr '\n' ' ')"
    fi
  else
    add_item "ollama-models" "FAIL" "$OLLAMA_HOST_URL unreachable (ollama service absent or down — setup.sh does not manage the service install)"
  fi

  local i
  for i in "${!ITEMS_STATE[@]}"; do
    if [[ "${ITEMS_STATE[$i]}" == "FAIL" ]]; then
      VERIFY_FAILS=$(( VERIFY_FAILS + 1 ))
    fi
  done
}

# ── §7: reboot decision + report ───────────────────────────────────────────
reboot_decision() {
  if (( PLAN_MODE == 1 || REBOOT_NEEDED == 0 || VERIFY_FAILS > 0 )); then
    return 0
  fi
  local do_reboot=0 ans=""
  if (( REBOOT_NO == 0 )) && [[ -t 0 ]] && (( ASSUME_YES == 0 )); then
    printf 'Reboot now to apply the overlay change? [Y/n] '
    read -r ans || ans="Y"
    case "$ans" in
      [nN]*) do_reboot=0 ;;
      *)     do_reboot=1 ;;
    esac
  elif (( REBOOT_YES == 1 )); then
    do_reboot=1
  fi

  if (( do_reboot == 1 )); then
    log "Rebooting in 3 seconds (re-run after: sudo bash setup.sh --plan)..."
    sleep 3
    reboot
    exit 0
  fi
  REBOOT_PENDING=1
  log "converged — reboot required for the overlay change (run: sudo reboot)"
  return 0
}

report_and_exit() {
  local i f mode result code changed_json="" items_json=""

  log "---- verify / audit ----"
  for i in "${!ITEMS_NAME[@]}"; do
    log "  [${ITEMS_STATE[$i]}] ${ITEMS_NAME[$i]}${ITEMS_DETAIL[$i]:+ — ${ITEMS_DETAIL[$i]}}"
  done

  if (( ${#CHANGED_FILES[@]} > 0 )); then
    log "---- changed this run ----"
    for f in "${CHANGED_FILES[@]}"; do log "  $f"; done
  elif (( PLAN_MODE == 0 )); then
    log "---- no file changes (byte-stable) ----"
  fi

  mode="$([[ $PLAN_MODE -eq 1 ]] && echo plan || echo apply)"
  if (( PLAN_MODE == 1 )); then
    if (( DRIFT_COUNT + VERIFY_FAILS == 0 )); then
      result="CONVERGED"
      code=0
    else
      result="DRIFT:$(( DRIFT_COUNT + VERIFY_FAILS ))"
      code=1
    fi
  elif (( VERIFY_FAILS > 0 )); then
    result="INCOMPLETE:verify"
    code=1
  elif (( REBOOT_PENDING == 1 )); then
    result="CONVERGED_REBOOT_PENDING"
    code=3
  elif (( ${#CHANGED_FILES[@]} > 0 )); then
    result="DRIFT:${#CHANGED_FILES[@]}"
    code=0
  else
    result="CONVERGED"
    code=0
  fi

  if (( JSON_OUT == 1 )); then
    for f in "${CHANGED_FILES[@]}"; do
      if [[ -n "$changed_json" ]]; then changed_json+=", "; fi
      changed_json+="\"$f\""
    done
    for i in "${!ITEMS_NAME[@]}"; do
      if [[ -n "$items_json" ]]; then items_json+=", "; fi
      items_json+="{\"name\":\"${ITEMS_NAME[$i]}\",\"state\":\"${ITEMS_STATE[$i]}\""
      if [[ -n "${ITEMS_DETAIL[$i]}" ]]; then
        items_json+=",\"detail\":\"${ITEMS_DETAIL[$i]}\""
      fi
      items_json+="}"
    done
    printf '{"result":"%s","mode":"%s","preflight":"ok","changed":[%s],"items":[%s],"reboot":{"needed":%s,"performed":false,"pending":%s}}\n' \
      "$result" "$mode" "$changed_json" "$items_json" \
      "$( (( REBOOT_NEEDED == 1 )) && echo true || echo false )" \
      "$( (( REBOOT_PENDING == 1 )) && echo true || echo false )"
  fi

  log "RESULT: $result"
  exit "$code"
}

# ── main ───────────────────────────────────────────────────────────────────
preflight

mode="$([[ $PLAN_MODE -eq 1 ]] && echo plan || echo apply)"
log "phase=converge (mode=$mode)"
if (( PLAN_MODE == 0 )); then
  mkdir -p "$MARK_DIR"
fi
converge

if (( PLAN_MODE == 0 )); then
  log "phase=verify"
fi
run_verify

log "phase=reboot-decision"
reboot_decision

if (( PLAN_MODE == 0 )); then
  printf '%s mode=%s changed=%s reboot_needed=%s\n' \
    "$(date '+%F %T')" "$mode" "${#CHANGED_FILES[@]}" "$REBOOT_NEEDED" >> "$MARK_DIR/setup.log"
fi

report_and_exit