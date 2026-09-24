#!/usr/bin/env python3
"""hat-buttons.py — GamePi membrane-button daemon (gpiod -> X11 XTest).

The HAT's 13 membrane keys are plain GPIO lines, and this board's vendor
kernel cannot turn them into an input device (no gpio-keys, no /dev/uinput,
no runtime DT overlay — see docs/hardware/membrane-buttons.md), so this
daemon IS the input path: a root service reads the mapped lines through
python3-gpiod (polling) and synthesizes standard X11 key events on the :1
desktop via the XTEST extension (pure-Python python-xlib — Display()
auto-binds the xtest_* methods; no libxtst6 needed).

KEY TABLE — single source of truth. Verified in the live mapping session
2026-09-23 (/home/cj/btnmap-run.log — every entry below showed a clean
paired press/release in that log):

    keysym     : (chip node,   line, A733, idle, active)

Nine lines idle HIGH and go LOW on press (active-low); PD2 (Left) is the
one active-HIGH line.

PARKED lines — never transitioned in the whole mapping log; all idle LOW,
which under a grounded probe is also what a *dead contact* (that side of
the membrane never closes) looks like. Re-test procedure:
plans/current/2026-09-23-22-21-19_membrane-buttons-userspace-daemon.md,
task "Parked keys". A confirmed round adds ROWS TO KEYS — nothing else
changes.
    pin 32 -> /dev/gpiochip0 line 97   (PD1, expected: face Y)
    pin 33 -> /dev/gpiochip0 line 99   (PD3, expected: d-pad Right)
    pin 37 -> /dev/gpiochip0 line 100  (PD4, expected: Select)

The 13th key (pin 5, /dev/gpiochip0 line 34 / PB2) is deliberately absent:
it is suspected fused into the AXP PMIC PEK input, which already delivers
KEY_POWER on /dev/input/event0 (docs/hardware/power-button.md).

Modes:
    (none)              run the daemon (the unit's ExecStart)
    --selftest          read-only: opens X :1 and resolves every keycode,
                        opens both gpiochip nodes and prints the table.
                        The gpio part needs root (/dev/gpiochip* are
                        root:root 0600) — EPERM there is reported, not
                        fatal to the X side. Exit 0 on success.
    --selftest --inject-test
                        additionally sends ONE Control_L press+release at
                        the end (a focused X11 window on :1 sees it; where
                        none is focused it is a harmless no-op), proving
                        the whole injection path end-to-end.
"""
import logging
import signal
import sys
import time

import gpiod
from gpiod import Chip, LineSettings
from gpiod.line import Direction
from Xlib import X, XK, display

log = logging.getLogger("hat-buttons")

KEYS = {
    #  keysym    : (chip node,         line, A733, idle, active)  pin  role
    "Up":         ("/dev/gpiochip0",  140, "PE12",  1, 0),  # 29  d-pad up
    "Down":       ("/dev/gpiochip0",  141, "PE13",  1, 0),  # 31  d-pad down
    "Left":       ("/dev/gpiochip0",   98, "PD2",   0, 1),  # 36  d-pad left (ACTIVE-HIGH)
    "a":          ("/dev/gpiochip0",   39, "PB7",   1, 0),  # 40  face A
    "b":          ("/dev/gpiochip0",   40, "PB8",   1, 0),  # 38  face B
    "x":          ("/dev/gpiochip0",   42, "PB10",  1, 0),  # 10  face X
    "Control_L":  ("/dev/gpiochip1",    2, "PL2",   1, 0),  # 16  left shoulder
    "Control_R":  ("/dev/gpiochip0",   41, "PB9",   1, 0),  #  8  right shoulder
    "Return":     ("/dev/gpiochip0",   38, "PB6",   1, 0),  # 35  Start
}
DISPLAY = ":1"
POLL_S = 0.005        # 5 ms poll (gpiod value reads are sysfs-cheap)
DEBOUNCE_S = 0.030    # a press/release edge only commits once the line has been
                      # STABLE at its new level for this long; any raw level
                      # motion (including a bounce back) cancels it. 30 ms is
                      # well above membrane bounce and well below the ~50 ms
                      # human-press floor, so a quick release mid-press simply
                      # never commits — no double edges, no phantom taps.
X_RETRY_S = 5.0       # X reconnect backoff (also the unit's RestartSec)


def _label(chip):
    """Chip label for logging, read via gpiod's kernel chip-info query.

    We deliberately do NOT derive /sys/class/gpio/gpiochipN from the
    /dev/gpiochipN node number: the node number is the pinctrl *registration*
    order, but the sysfs directory is named by the chip's *line-offset base*
    (on this board /dev/gpiochip1 is sysfs gpiochip352, there is no
    gpiochip1 dir), so a path derived from the node number misses. Asking the
    kernel directly always resolves the right chip.
    """
    try:
        label = chip.get_info().label
        return label if label else "?"
    except Exception:
        return "?"


def open_chips():
    """One gpiod LineRequest per chip, covering ALL of that chip's mapped
    lines (the A733 gpiolib rejects a second request that includes a line
    an already-live request owns — the mapping probe learned that the hard
    way). Returns [(path, req, {line: keysym}), ...]."""
    per_chip = {}
    for keysym, (path, line, _n, _i, _a) in KEYS.items():
        per_chip.setdefault(path, {})[line] = keysym
    chips = []
    # Expected label per node: the key table was verified against exactly
    # these. Warn (never fail) on a mismatch — node numbering can shift
    # between vendor kernels, in which case the (node, line) pairs above may
    # no longer be right.
    expected = {
        "/dev/gpiochip0": "2000000.pinctrl",
        "/dev/gpiochip1": "7025000.pinctrl",
    }
    for path, lines in per_chip.items():
        chip = Chip(path)
        label = _label(chip)
        log.info("chip %s label=%s (%d lines)", path, label, len(lines))
        want = expected.get(path)
        if want and label != want:
            log.warning("chip %s label is %r, not %s — node numbering may "
                        "have shifted; verify the key table before trusting "
                        "it", path, label, want)
        cfg = {ln: LineSettings(direction=Direction.INPUT) for ln in lines}
        req = chip.request_lines(config=cfg, consumer="hat-buttons")
        chips.append((path, req, lines))
    return chips


class XSide:
    """The X11/XTest half. Display() auto-binds the xtest_* extension
    methods — xtest.init(Display, None) double-registers them, so we simply
    never call it. Kept as a class because a reconnection (Xvfb restarted
    under us) means a fresh Display and fresh keycode table."""

    def __init__(self):
        self.d = None
        self.codes = {}
        self.root = X.NONE

    def open(self):
        self.d = display.Display(DISPLAY)
        self.root = self.d.screen().root
        self.codes = {}
        for keysym in KEYS:
            ks = XK.string_to_keysym(keysym)
            code = self.d.keysym_to_keycode(ks) if ks else 0
            if not code:
                self.close()
                raise RuntimeError(f"no keycode for keysym {keysym!r}")
            self.codes[keysym] = code

    def press(self, keysym):
        """One full press+release of KEYS[keysym] on the root window
        (XTest delivers to whatever window has focus)."""
        if not self.d:
            return
        code = self.codes[keysym]
        for etype in (X.KeyPress, X.KeyRelease):
            self.d.xtest_fake_input(etype, code, X.CurrentTime, self.root,
                                    0, 0)
        self.d.sync()

    def close(self):
        if self.d is not None:
            try:
                self.d.close()
            except Exception:
                pass
            self.d = None
            self.root = X.NONE

    @property
    def ok(self):
        return self.d is not None


def poll_once(chips, states, edges, now):
    """One poll tick across every mapped line; appends (keysym, 'press' |
    'release') to `edges` for edges that COMMITTED this tick.

    State per line: raw = last physical level seen; committed = last level
    we already reported a press/release for; cand/t = the level a press or
    release has been *pending at* since (its debounce start). A commit
    requires DEBOUNCE_S of stability at the opposite level, so bounce and a
    return-to-committed before the window closes both cancel cleanly."""
    for path, req, lines in chips:
        for line, keysym in lines.items():
            try:
                lvl = int(req.get_value(line).value)
            except Exception as e:
                log.warning("read %s line %d failed: %s", path, line, e)
                continue
            st = states.get(line)
            idle, active = KEYS[keysym][3], KEYS[keysym][4]
            if st is None:
                # First sighting: trust the table as the baseline (all nine
                # lines were idle at the moment of the unit's start).
                states[line] = {"raw": lvl, "committed": lvl,
                                "cand": None, "t": 0.0,
                                "idle": idle, "active": active}
                continue
            if lvl != st["raw"]:
                st["raw"] = lvl
                if lvl == st["committed"]:
                    st["cand"] = None      # bounced back: cancel pending edge
                else:
                    st["cand"] = lvl       # moving toward the other rail
                    st["t"] = now
            if (st["cand"] is not None and st["cand"] != st["committed"]
                    and now - st["t"] >= DEBOUNCE_S):
                edges.append((keysym, "press" if st["cand"] == st["active"]
                              else "release"))
                st["committed"] = st["cand"]
                st["cand"] = None


def run(_args):
    stop = {"run": True}
    signal.signal(signal.SIGTERM, lambda *a: stop.update(run=False))
    signal.signal(signal.SIGINT, lambda *a: stop.update(run=False))

    chips = open_chips()
    xs = XSide()
    try:
        xs.open()
    except Exception as e:
        log.warning("X %s unavailable (%s); reconnecting every %.0fs",
                    DISPLAY, e, X_RETRY_S)

    log.info("hat-buttons: %d keys mapped on %s", len(KEYS), DISPLAY)
    states, edges = {}, []
    try:
        while stop["run"]:
            if not xs.ok:
                try:
                    xs.open()
                    log.info("X %s connected; %d keys resolved", DISPLAY,
                             len(xs.codes))
                except Exception:
                    time.sleep(X_RETRY_S)
                    continue
            now = time.monotonic()
            edges.clear()
            poll_once(chips, states, edges, now)
            for keysym, kind in edges:
                log.debug("%s %s", kind, keysym)
                try:
                    xs.press(keysym)
                except Exception as e:  # display died mid-edge: reconnect
                    log.warning("xtest: %s: %s", type(e).__name__, e)
                    xs.close()
                    break
            time.sleep(POLL_S)
    finally:
        for _p, req, _l in chips:
            try:
                req.release()
            except Exception:
                pass
        xs.close()
        log.info("hat-buttons: stopped (lines released, X closed)")
    return 0


def selftest(args):
    """Read-only proof used by setup.sh --plan / live bring-up."""
    inject = "--inject-test" in args
    ok = True
    # X side — must fully pass
    xs = XSide()
    try:
        xs.open()
        print(f"X OK : display {DISPLAY}, {len(xs.codes)} keycodes resolved")
        for keysym, code in sorted(xs.codes.items(), key=lambda kv: kv[1]):
            print(f"       {keysym:12} -> keycode {code}")
    except Exception as e:
        print(f"X FAIL: {e}")
        return 1
    # GPIO side — root expected; EPERM is reported, not fatal to the X side
    for path in sorted({meta[0] for meta in KEYS.values()}):
        try:
            chip = Chip(path)
            print(f"GPIO OK: {path} ({_label(chip)})")
            for keysym, (p, line, _n, _i, _a) in KEYS.items():
                if p != path:
                    continue
                req = chip.request_lines(
                    config={line: LineSettings(direction=Direction.INPUT)},
                    consumer="hat-buttons-selftest")
                val = int(req.get_value(line).value)
                want = KEYS[keysym][3]
                flag = "ok" if val == want else f"! expected idle {want}"
                print(f"       line {line:3d} {keysym:12} idle={val} {flag}")
                if val != want:
                    ok = False
                req.release()
            chip.close()
        except PermissionError as e:
            ok = False
            print(f"GPIO DENIED (needs root): {path}: {e}")
        except Exception as e:
            ok = False
            print(f"GPIO FAIL: {path}: {e}")
    # Optional end-to-end injection proof (one harmless Control_L tap)
    if inject:
        try:
            xs.press("Control_L")
            print("INJECT : sent Control_L press+release on "
                  f"{DISPLAY} (a focused window will briefly see it)")
        except Exception as e:
            ok = False
            print(f"INJECT FAIL: {e}")
    xs.close()
    print("SELFTEST " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    logging.basicConfig(level=logging.INFO, stream=sys.stdout,
                        format="%(asctime)s %(levelname)s %(message)s",
                        datefmt="%H:%M:%S")
    sys.excepthook = lambda t, v, tb: (
        log.exception("fatal: %s", v), sys.exit(1))
    args = sys.argv[1:]
    if "--selftest" in args:
        return selftest(args)
    return run(args)


if __name__ == "__main__":
    sys.exit(main())