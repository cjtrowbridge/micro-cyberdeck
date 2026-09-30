# 2026-09-29 — voice agent G1 status checkpoint (six-day gap)

Checkpoint of where the voice agent stands, six days after the
[2026-09-23-g1-reboot-incident](2026-09-23-g1-reboot-incident.md) and its
corrected forensics (`3b3c462`). Trigger: a **replacement HAT was ordered** to
test whether the failing buttons on the current one are simply defective
(relevant to the 3 parked keys and the pin-5/PEK question — see below).
Nothing on the board changed to produce this state; everything below was
probed live, 2026-09-29 ~21:30 local.

## Board health (6 days clean)

- **Uptime 5 d 21:57** — the current boot (post-incident, boot_id `1fcf778c`)
  has held through the entire gap: **no further unclean power events** since
  the 09-23 cluster of four.
- Load 3.44 / 1.99 / 1.63, all benign: the SPI bridge
  `xvfb-to-st7789.py` (~63 % CPU), `Xvfb :1` (~21 %), VS Code remote.
- Memory: 2.0 G used, 9.6 G available; **zram swap 0 B used**.
- Temps normal after 6 days: cpul 51.2 / cpub 49.3 / gpu 48.0 / ddr 44.6 /
  npu 45.1 / skin 34.0 C.
- Disk: 22 G used / 7.0 G free (76 %).
- VNC: `x11vnc`/`gamepi-vnc` up, **one client connected** (192.168.1.134) —
  per the new test discipline this should be dropped before timed runs.

## What actually happened in the six days

Nothing operator-run. No unit/install/sweep/mic work landed. But the
post-incident "re-established the pin" claim (carried in the incident
journal's "in flight" note and the gate plan's G1-d row) **did not happen** —
probes below show exactly what is and isn't running.

### G1 state (probed live)

- **Ollama: container up since 09-23, but empty.** `/api/ps` →
  `{"models":[]}`. The `keep_alive: -1` pin is in-process and was lost on the
  09-23 reboot; **it has not been re-established for six days.** G1-d's
  ≥10-min idle verification is therefore still owed, from a cold start.
- **Whisper: assets intact, service absent.**
  `third_party/whisper.cpp/build/bin/whisper-server` (1.2 M, mtime 09-23
  23:24) and `~/voice-agent/ggml-medium.bin` (1.48 G, mtime 09-23 23:07) are
  untouched. The system unit
  [`projects/voice-agent/systemd/whisper-server.service`](../projects/voice-agent/systemd/whisper-server.service)
  (committed `0fcff04`) exists **only in the repo** —
  `systemctl: whisper-server.service: not-found`, nothing on :8086. The
  operator sudo install (the one explicitly deferred G1-c step) has not run.
- **G1-e / clean sweep: still invalidated** by the incident (correlated
  t=8-finish death); no new numbers since.
- **G2**: `g2-chain.sh` not yet written. **G3**: ebe app not yet built.

### New finding — the PTT input path is currently down

- `gamepi-buttons.service` is **loaded, disabled, inactive** — **no
  `hat-buttons.py` process running.** The unit file is on disk
  (`/etc/systemd/system/gamepi-buttons.service`, 09-23 22:28 — the same
  night the buttons landed), but its **enablement symlink is absent**
  (`/etc/systemd/system/multi-user.target.wants/` has no entry) — i.e. it
  was `disable`d at some point after 09-23 (or never `enable`[d, which is
  unlikely], since a 09-23 boot-time `setup.sh` pass would `enable --now`
  it if it found it inactive, and
  [`docs/setup.md`](../docs/setup.md)'s verify row 7 checks it). That
  looks like operator action before the HAT-swap decision (plausible: the
  buttons being tested out). Net: **the left bumper (PL2 → `Control_L`)
  currently produces no key events at all.** G3's round trip is blocked on
  this until it is restored — `sudo systemctl enable --now
  gamepi-buttons` — and **re-mapping will be needed after the replacement
  HAT lands** (the 9-key table is from the old HAT's `btnmap` session).
  Evidence recorded in
  [`docs/hardware/membrane-buttons.md`](../docs/hardware/membrane-buttons.md).
- The daemon runs **as root** (`/dev/gpiochip*` is root:0600) — its restore
  is operator-side (`sudo`).
- **The X side of the path is still healthy** (probed 2026-09-29): `:1`
  opens, `xtest_fake_input` auto-bound (`Xlib` 0.33's auto-bind quirk — the
  daemon never imports `Xlib.XTest`; that module is missing and needs to
  stay that way in any selftest logic); `Xvfb :1` 960×960 + the
  `xvfb-to-st7789.py` bridge both up. So restoration is just the daemon
  start, not any X plumbing.

### G0-b: still awaiting the physical USB mic

Unchanged: Option A declared (the only ALSA capture subdevice is the dead
HDMI-Rx codec — bit-exact silence), hardware proof pending the actual mic.

### Incident root cause: still open, plus a data point

The physical-world questions from the incident journal (what the deck was
plugged into at real ≈23:35; whether it was held/touched; the pin-5
power-key repro; what made the two 21:xx boots) remain **unanswered** —
nothing logged them. Six days without a further unclean event is weakly
inconsistent with a flaky supply, but 23 h of clustering plus a 6-day gap is
not a sample; the evidence stays "hard power loss leading, unproven".

## The replacement HAT (operator-ordered, in transit)

Motivation: the current HAT's 3 parked keys (`PD1/PD3/PD4`, header pins
32/33/37, idle-low, zero transitions) and the pin-5/PEK question — are they
just a bad HAT? What it changes when it lands:

- Full key re-mapping (the 9-key table in `membrane-buttons.md` is
  old-HAT evidence; fresh macro photos + `btnmap` session).
- Re-probe the 3 parked pins and the pin-5 long-press/power-key hypothesis
  (does the new HAT's pin 5 also cut power? if the old one is defective, the
  fusion hypothesis weakens or the failure mode is HAT-specific).
- Same-change rule in force: `docs/hardware/membrane-buttons.md` +
  `power-button.md` (+ README rows) updated in the swap's change.
- The old HAT is retained as a suspect artifact (for the power-path arc,
  which the parallel session owns — coordinate, don't clobber).

## Outstanding list (resume order)

1. **Operator, sudo, ~2 min** — install the whisper unit (G1-c close):
   `sudo cp projects/voice-agent/systemd/whisper-server.service
   /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl
   enable --now whisper-server` → verify `ss -tlnp | grep 8086` +
   `curl -s http://127.0.0.1:8086/health` → `{"status":"ok"}` (~6 s model
   load).
2. **Operator, sudo, ~2 min** — restore the PTT path: `sudo systemctl
   enable --now gamepi-buttons`, then one `--inject-test` (after unit stop)
   to re-prove `Control_L` end-to-end.
3. **Re-pin Ollama** (`keep_alive:-1`, requests **must carry
   `think:false`**), then G1-d: verify still resident after ≥10 min idle.
4. **G1-e / clean sweep** — drop the VNC client, pin idle, log free/temps/
   load around each run, t=2/4/8 + one 5–10 s clip; this feeds the design §5
   small-vs-medium decision (last sweep ≈50× real-time, already past the §5
   >2× revisit trigger).
5. **G2** — write `tools/g2-chain.sh`, run the headless round trip with
   per-stage wall times.
6. **G3** — ebe app on `:1`, physical-bumper round trip, flip the README
   bullet, close the plan to `plans/past/`.
7. **G0-b** — USB mic when the operator plugs one in.
8. **HAT swap** — re-map + re-probe parked keys + pin-5, when the
   replacement arrives.

All probes in this note were read-only; no board changes were made.