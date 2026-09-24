# Incident — unclean reboot mid-G1 (thread sweep), 2026-09-23

**What happened.** The operator reported the device "suddenly rebooted" while the
G1 thread sweep was mid-run: `whisper-server` (medium, foreground) had just
completed a 1 s sine decode at `t=8` (≈ 51 s, 51× real-time) after the `t=4`
re-run (55 s), with Ollama's `qwen3.5:4b` pinned resident, x11vnc connected,
memory at 8.2 Gi used / 300 Mi free (active reclaim). Corrected timeline
(after the RTC story — see below): the **previous boot started real ≈ 21:36**
(stamped 21:00:32 — its clock ran ~35 min slow; the RTC is unbound, see
[docs/hardware/rtc-hym8563.md](../docs/hardware/rtc-hym8563.md)) and carried the
entire G1 session (apt batch, CMake build, weights, Ollama pin, t=2/4/8 sweep)
for ≈ 2 hours, **dying at real ≈ 23:35:35 — the exact moment the t=8 decode
finished**. Post-reboot state was clean: repo intact, binary intact, weights
1.5 G intact, Ollama auto-started, cyberdeck-api back on 8080. The Ollama pin
and the whisper server (not yet a unit then) had to be re-established — done /
in flight.

## Evidence (cj user-space + operator sudo, all 2026-09-23 23:xx)

- **The dead boot's records did not survive — structurally.**
  `/var/log.hdd/journal/9b5c090a…/` holds exactly **one 2.6 MB segment per
  booted record** (mtimes 20:52:35, 21:00:33, 23:00:04 — the stamped,
  clock-stuck times). The image runs a **tmpfs runtime journal with
  flush-at-boot** (`/run/log/journal`, Armbian ramlog pipeline) plus
  `journald.conf: SystemMaxUse=20M`: on an unclean power loss the whole
  boot's in-RAM records die, and only a bootstrap sliver flushes at the next
  boot. The session boot's persisted segment holds **one stamped second**
  (21:00:32→21:00:33); its ~2 h of session records (build, pin, sweeps)
  died with it — it is not "unrecovered", it is unrecoverable here.
- **rsyslog text logs are boot-bound** (operator `sudo grep`): `/var/log/kern.log`
  contains only two kernel boots — the 09-20 16:04 flash and the current
  one — with **zero `oom-kill`, `killed process`, `Kernel panic`, or watchdog
  reset lines** in either, only the two `sunxi-wdt … Watchdog enabled
  (timeout=16 sec, nowayout=0)` lines. `syslog`/`kern.log` are truncated at
  every boot; `armbian-hardware-monitor.log` likewise (its last lines are
  the current boot's 23:00-stamped boot health table).
- **Net: the death-boundary evidence for this reboot does not exist on the
  deck.** Dirty power = no record, given tmpfs journal + rsyslog-at-boot +
  `SystemMaxUse=20M` rotation on this vendor image.
- **The current boot's clock jumped ~35 min at NTP:** banner 23:00:04,
  `systemd-timesyncd … Initial clock synchronization to 23:35:39` ~40 s
  later, `uptime` proving kernel start ≈ 23:35:35. With the RTC unbound and
  non-functional ([docs/hardware/rtc-hym8563.md](../docs/hardware/rtc-hym8563.md)),
  all pre-NTP wall-clock reasoning is suspect: the 20:52/21:00-stamped boots
  are real **≈ 21:28 / ≈ 21:36**.
- **Boots tonight (stamped → real):** `201b7706` 20:52:34 (≈ 21:28) — **died
  ~2 s after start** (journal ends mid-early-userspace: bluetoothd/ntp/cron,
  no `shutdown.target`); `a23903f8` 21:00:32 (≈ 21:36) — **the G1 session
  boot; ran ≈ 2 h, died at real ≈ 23:35:35**; `1fcf778c` 23:00:04 (≈ 23:35:35,
  current — boot_id confirmed via `/proc/sys/kernel/random/boot_id`).
  Earlier today, the 2-day flash boot (`08c5ffbc`, 09-20 16:04 → stamped
  09-23 00:15:43) also ended with **no `shutdown.target`** — the **fourth
  unclean power event in ~23 h**.
- **A 2-second boot cannot be a watchdog trip:** `sunxi-wdt` (16 s,
  `nowayout=0`) only starts counting from kernel start; the ≈ 21:28 boot
  died within 2 s of it. Those events are hard power losses (or an early
  boot-fail loop) — i.e. **power-side, not software-reset**.
- **No fatal kernel signatures in the surviving 2-day boot**, which spans the
  earlier Ollama/heavy work of 09-20→09-23: no OOM-kills, no panics, no
  thermal trips; post-mortem temps healthy (cpul 57.0 C, cpub 55.9 C,
  gpu 53.2 C, skin 35.6 C — logged in
  [docs/hardware/soc-thermal.md](../docs/hardware/soc-thermal.md)); swap
  **exists** (`/dev/zram0` 5.8 G — my earlier "no swap" assumption was wrong)
  so an OOM would have had a buffer.
- **A hard power cut is the *zero-trace* mechanism on this deck:** the power
  button is the PMIC's PEK input (`axp8191-pek`, `/dev/input/event0`,
  `KEY_POWER`; [docs/hardware/power-button.md](../docs/hardware/power-button.md))
  and a PEK long-press makes the AXP8191 **cut power immediately — no journal
  line, ever**. The membrane-buttons record holds an unconfirmed hypothesis
  that keypad pin 5 (PB2) is fused with this PEK — an accidental long-press
  on the deck would hard-cut it. The deck had an active x11vnc client
  (192.168.1.134, key events visible) on both the dying and the next boot.

## Hypotheses (reordered after the evidence above)

1. **Hard power loss** — external supply/cable/HAT path, or a
   PEK/pin-5 long-press cutting power via the PMIC. It fits every
   observation: all four unclean events leave exactly the no-record journal
   state of a power loss; the 2 s boot *rules out* a watchdog for that event;
   the kernel is 2-day-proven stable under similar load (zero fatal
   signatures in the surviving record); and the power path is a known open
   question (parallel session's power-path plan; WS0 tcpm all-zeros
   baseline). The t=8 decode simply happened to end at the moment of the cut.
2. **Watchdog reset after a kernel hang** — possible, and also zero-trace,
   but the 2 s micro-boot does not fit it, and the board already survived
   2 days of this image under comparable workload without a single hang
   signature. The 16 s `nowayout` timer stays a standing risk for any
   future hang regardless.
3. **OOM** — weakest: zram 5.8 G was available, no kill records anywhere,
   and the OOM-killer terminating a user process would not reboot the SoC.

## Open — physical-world questions (board evidence is exhausted)

The sudo probes (operator, 23:xx) ran and found nothing recoverable —
everything above is the full surviving record. What remains is human-world:

- **Power at real ≈ 23:35:35:** which supply and cable, which outlet,
  anything else on that feed; did the screen/LED visibly drop at the moment
  (the operator's "suddenly rebooted" is the observation of record)?
- **Was the deck held or touched** at the moment of death — or during the
  ≈ 21:28 / ≈ 21:36 power events? A cheap repro for the operator, **after any
  in-flight parallel work and before the heavy-load sweep re-run**:
  long-press the power button (and pin 5) for a few seconds and confirm
  whether the board hard-cuts. If it does, every long decode from now on is
  exposed to accidental press-cut — document it in
  [docs/hardware/power-button.md](../docs/hardware/power-button.md) and
  treat pin 5 exactly as the power key.
- **What ran between real ≈ 21:00–23:35 — and did anything schedule a
  reboot?** `setup.sh` (the "GamePi: set up the machine" task) or any other
  reboot-scheduling mechanism? Two boots 8 min apart smells like a
  provisioning pass + reboot policy, or a flaky supply. (Operator recollection
  here is worth more than any log at this point.)
- The "4 unclean power events in ~23 h" statistic belongs in the power-path
  record when that arc next lands — **coordinate with the parallel session,
  don't clobber** `docs/hardware/power-path.md` (their live file).

## Impact on the gate plan

- The clean `t=2/4/8` sweep is **invalidated/confounded**: the final numbers
  (t=4 re-run 55 s, t=8 51 s) sit in a boomerang rather than monotonic shape
  (2:56 → 4:46→55 → 8:51) inside a machine at 300 Mi free with active
  reclaim — treat all as "≈ 50 s class, not separable in-thread-count".
  Re-run the sweep as a **controlled, whisper-only-load** exercise (Ollama
  pinned but idle, no VNC client, log `free` + zone temps + load around each
  run) before the §5 small-vs-medium decision.
- G1-d/G1-e stay open; the Ollama re-pin and the (pending unit) whisper
  server had to be re-established post-reboot — already done / in flight.
- New test discipline from here on: **no sustained decode while a VNC client
  is connected or the board's clock/thermal state is unknown**; temperatures
  + memory recorded around each timed run.