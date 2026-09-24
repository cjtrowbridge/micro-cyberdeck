# Incident — unclean reboot mid-G1 (thread sweep), 2026-09-23

**What happened.** The operator reported the device "suddenly rebooted" while the
G1 thread sweep was mid-run: `whisper-server` (medium, foreground) had just
completed a 1 s sine decode at `t=8` (≈ 51 s, 51× real-time) after the `t=4`
re-run (55 s), with Ollama's `qwen3.5:4b` pinned resident, x11vnc serving an
active client, and memory at 8.2 Gi used / 300 Mi free (active reclaim).
Post-reboot state was clean: repo intact, whisper-server binary intact
(build dir untracked in the submodule, weights `~/voice-agent/ggml-medium.bin`
1.5 G intact), Ollama auto-started, cyberdeck-api back on 8080. Nothing of
ours was damaged; the unpinned-forever Ollama pin and the whisper server (not
yet a unit) had to be re-established.

## Evidence collected (user-space, cj)

- **Crash-boot journal is missing.** `journalctl --list-boots` shows boots at
  09-20 16:04 (`08c5ffbc`), 09-23 20:52 (`201b7706`, **~2 s of records, ends
  mid-early-boot, no shutdown.target**), 09-23 21:00 (`a23903f8`, **~1 s of
  records, same shape**), 09-23 23:00 (`1fcf778c` = current). The boot that
  carried our whole G1 session (build ≈ 23:17–23:24, tests through ≈ 23:41
  by wall clock) appears nowhere as a separate boot, and the current boot's
  journal contains **zero whisper lines** and a 23:01–23:35 stamped gap with
  zero lines — the records of the dead boot (or of itself) did not persist to
  `/var/log.hdd/journal` (the persistent journal lives on the same eMMC file
  system, `/dev/mmcblk1p1`).
- **Current boot's clock jumped ~35 min at +40 s.** Kernel banner stamped
  23:00:04, `systemd-timesyncd: Initial clock synchronization to 23:35:39`
  about 40 s later; `uptime` proves kernel start ≈ 23:35:40. The RTC on this
  board is unreliable — any wall-clock reasoning before NTP sync is suspect.
- **No crash signatures in any surviving record.** Greps across all readable
  boots for `oom-kill | killed process | Kernel panic | thermal trip |
  watchdog | shutdown.target`: nothing fatal. `swapon --show` proves swap
  **does exist** — `/dev/zram0` 5.8 G (fully unused after reboot); my earlier
  "no swap" assumption was wrong. Current temps healthy: cpul 57.0 C,
  cpub 55.9 C, gpu 53.2 C, skin 35.6 C.
- **Vendor watchdog is live:** `sunxi-wdt 2050000.watchdog: Watchdog enabled
  (timeout=16 sec, nowayout=0)` on every boot. On sunxi hardware a watchdog
  expiry forces a full reset and leaves no journal trace — consistent with
  every observation above, but not provable from user space.
- **`/var/log/syslog` (615 K, last write 23:41) and `/var/log/kern.log`
  (349 K) are the complete record and are `adm`/root-only — `tail` as cj was
  permission-denied. They would settle OOM-vs-power-vs-hang.**

## Hypotheses (ordered by fit)

1. **Watchdog reset after a kernel/firmware hang** during the heaviest load
   of the session (t=8 decode + Ollama 3.65 G resident + active reclaim at
   300 Mi free + x11vnc client). 16 s `nowayout` vendor wdt; zero journal
   trace expected.
2. **Power-path brownout/reset** — the exact live question of the
   power-path plan (WS0: TCPM all-zeros baseline; WS1: IC identification
   pending). The two earlier hard resets tonight (20:52, 21:00 — each
   journaled only to early boot, no shutdown record) make a repeat
   instability rather than a one-off.
3. **OOM then something worse** — now the weakest fit: zram swap existed and
   was presumably absorbing pressure; no kill records at all.

## Open — needs operator (root) evidence

- `grep -inE 'oom|killed process|panic|watchdog|wdt' /var/log/kern.log | tail -30`
- `grep -inE 'oom|panic|watchdog|reset|tripped' /var/log/syslog | tail -30`
- `tail -60 /var/log/syslog` at the death boundary (≈ 23:41:28)
- `ls -l /var/log.hdd/journal/` — which boot files persisted, and whether any
  was truncated ~23:41
- Board power state at the moment: on external supply vs battery; any visible
  brownout on the HAT side (ties into the power-path plan's WS0/WS1)
- Were the 20:52 / 21:00 hard resets noticed or caused (e.g. a setup.sh
  reboot-scheduler, power cycle)?

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