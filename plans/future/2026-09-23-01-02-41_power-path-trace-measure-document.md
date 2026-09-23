---
plan_id: 2026-09-23-01-02-41_power-path-trace-measure-document
title: Power path — identify the HAT charge IC, run the 3-config input matrix, document
summary: Close out the power-path row (Not started -> Done, or Partial with explicit bench-tooling gap): (WS0) re-baseline today's observable state (tcpm all-zeros, no fuel node, axp8191 @0x36 + axp515 @0x34 on i2c-13) and re-examine the HAT underside photos; (WS1) operator-led READ-ONLY chip-ID reads on both i2c-13 addresses plus fresh macro photos and a literature pass on the Spotpear reference clone / GamePi HAT to identify the mysterious eight-pin IC + 4R7 inductor charge circuit; (WS2) the proposed 3-config input test matrix - (a) HAT microUSB only, (b) SBC USB-C only, (c) both - at standby and under nominal game workload, recording per-port input V/I, tcpm negotiated values, charge behavior, and stability; (WS3, conditional) minimal battery telemetry via whichever path WS1/WS2 indicate (AXP driver ADC, root userspace I2C helper, or HAT-chip exposure), folded into setup.sh with the docs/setup.md mandate and live apply->verify only if a service is needed; (WS4) same-change updates to all four hardware records (power-path, battery, usbc-power, pmic-axp8191) + README rows + journal. Safety carried over from the 2026-09-16 decision: register access is read-only, no agent-driven PMIC writes, controlled-discharge cap for config (b).
status: future
created_at: 2026-09-23-01-02-41
---

Key: `[ ]` pending task, `[x]` completed task, `[?]` needs validation, `[-]` closed task

# Power path — identify, measure, document

Repo goal: the power-path row stops being **Not started**. By the end of this
arc we will have: (1) established what the HAT's eight-pin power IC beside
the cell connector actually is and how its circuit relates to the SBC's
AXP8191; (2) run the proposed three-configuration input test matrix
(HAT-microUSB-only / SBC-USB-C-only / both) with real numbers for each;
(3) answered "can the SBC's own USB-C port power this deck?" with evidence
rather than assumption; (4) either an OS-visible battery charge state via
the path the evidence points to, or a documented "there is no path, and
here's why"; and (5) all four affected hardware records, the README
Hardware Status rows, and the journal updated in the same changes as their
findings (standing rule).

## Scope and safety frame

- **Register access is READ-ONLY, full stop** (the 2026-09-16 battery-ADC
  decision carried over verbatim): chip-ID register reads (`0x00`–`0x06`)
  are safe; anything that writes to a live, powered PMIC — especially
  charge-path MOSFET enable bits — is explicitly out of scope for this plan
  and for unattended/agent-driven work.
- **Root-only steps are operator steps.** The `i2c-dev` nodes are
  root-only and `i2c-tools` is not installed on the board; sudo prompts for
  a password here. The agent prepares and explains each command; the
  operator runs it and the output is recorded verbatim in the journal.
- **Bus probing is detect-only.** Any `i2cdetect` sweep targets only the
  buses where a HAT chip could plausibly sit, is read-class (quick-write
  detection only), stops at "an unknown address responded — record it, do
  no further transaction against it," and is run while the deck is on its
  main power feed (HAT microUSB) so a bus hiccup cannot ride on the cell
  alone.
- **No provisioning changes in WS0–WS2.** This is investigation +
  documentation; `setup.sh` is touched only if WS3 lands a service, in
  which case the standing mandate applies: `docs/setup.md` updated in the
  same change and live `apply -> verify` on this board before commit.
- **Cell health is a constraint.** The config-(b) work can discharge the
  11.1 Wh LiPo. Discharge tests are controlled: a plug-in power source is
  at hand before the feed is removed, and the test stops at the agreed
  depth (decision D4 below) with an immediate recharge afterward.

## Grounded research (state as of 2026-09-23, from the records)

- The deck runs today from the **HAT's microUSB** port — observed directly
  (the cable that keeps it alive is the HAT's, not the SBC's). The HAT
  carries an **eight-pin IC beside the two-pin battery connector plus a
  `4R7` inductor**; its marking is not reliably legible in the 2026-09-17
  underside photos, and its role (charge? boost? measure?) is unestablished.
- **Linux sees no battery at all**: `/sys/class/fuel/` is empty;
  `/sys/class/power_supply/` holds only `tcpm-source-psy-14-0022` (type
  `USB`, every property 0/empty). No `capacity`, `voltage_now`, or
  `status` exists anywhere in the tree.
- **SBC USB-C** is negotiated by the fUSB302 on `i2c-14` @ `0x22`
  (bounds `typec_fusb302`); the node is the kernel stub and reads all
  zeros in the current (HAT-powered) configuration — expected, not a fault.
- **Two PMIC addresses on `i2c-13`**: `axp8191` @ `0x36` (active, binds
  `axp20x-i2c`, provides ~40 regulator names and the PEK power button) and
  `axp515` @ `0x34` (probed but inert; whether it carries a battery ADC is
  an open question, and even if it does its connection to the HAT cell is
  unproven). No rail exposes live µV; the on-die `temp-ctrl` is unused.
- The HAT is described in the README as **a clone of the Spotpear
  Raspberry Pi Game 1.54" LCD board, modified with speaker, aux jack, and
  a battery charge controller** — the reference design is the primary
  literature candidate for the charge-IC identity.
- The vendor spec says "it powers the pi from the top which is the only
  way with this pi" — the HAT feed and the board's native USB-C input are
  two distinct sources whose interaction has never been tested.
- The 13.9 h / 0.8 Wh standby figures are **vendor math, not measured** —
  no current-draw measurement of this deck is on file.

## Design decisions

1. **Evidence-first ordering: identify before measuring, measure before
   implementing.** WS1 (what is the circuit) shapes how WS2 (what does it
   do) is instrumented, and both shape whether WS3 (make it visible in the
   OS) is a driver patch, a root helper, a HAT-chip read, or a documented
   dead end. We do not start WS3 until WS1/WS2 have pointed at a specific
   chip and register map.
2. **Bench measurement is the primary instrument; software observables are
   the fallback, not the goal.** A USB power meter (or equivalent
   inline meter) on each input port is what answers "which source wins /
   does USB-C alone work." If the operator decides against metering
   (decision D1), the matrix degrades to a software-observable version
   (boot/no-boot, tcpm values, stability, thermal) and the row lands at
   **Partial** with the bench gap stated, not at Done.
3. **WS3 is deliberately conditional and minimal.** The target is the
   smallest telemetry that answers "charging / discharging / full and
   roughly how much": `voltage_now` + `status` at least. No full fuel
   gauge, no coulomb-counting project, no kernel recompile unless a
   mainline driver patch obviously covers the identified chip (a distro
   kernel patch on this board is a much bigger commitment and would be a
   separate decision).
4. **Every finding lands in its record in the same change** (standing
   rule from AGENTS.md / docs/hardware/README.md): a chip-ID read updates
   `pmic-axp8191.md` + `battery.md` in the same commit as the journal note
   recording the read; each matrix config updates `power-path.md` (and
   `usbc-power.md` for the tcpm halves); a README status flip happens in
   the same commit as the evidence that justifies it.
5. **The row's exit is bounded and explicit** (see Exit criteria): either
   **Done** with all four answers, or **Partial** with the single missing
   instrument or measurement named — no open-ended "mostly characterized."

## Operator decisions needed up front

- [ ] **D1 — Metering.** Is a USB power meter / inline multimeter setup
      available for each input port (or should one be procured)? If no,
      WS2 proceeds in software-observable mode and the row caps at
      Partial. (Blocks 2.2–2.5 depth, not 2.1.)
- [ ] **D2 — Cell presence & state.** Is the 11.1 Wh LiPo currently
      installed, and what is its state of charge? Config (b) behavior
      differs materially between "cell present and charged" (expected to
      take over) and "cell absent" (expected to not boot — itself an
      answer).
- [ ] **D3 — i2c-tools approval.** Approve `sudo apt-get install -y
      i2c-tools` and the operator-run read-only reads in WS1 (the agent
      supplies the exact commands; the operator executes and we record
      verbatim).
- [ ] **D4 — Discharge depth for config (b).** Agreed stopping point for
      any controlled discharge of the cell (recommend: stop at ~80 %
      remaining if any readout path exists by then, else stop before the
      board's brownout and recharge immediately). Record the decision in
      the journal with the date.

## WS0 — Re-baseline and evidence assembly

Agent-runnable (no sudo), done before any HW interaction; findings recorded
in a journal note that WS1/WS2 cite.

- [ ] 0.1 Re-probe the power-observable tree and record verbatim:
      `ls /sys/class/fuel/` (expect empty);
      `for d in /sys/class/power_supply/*; do echo "$d: $(cat $d/type 2>/dev/null)"; done`
      (expect only `tcpm-source-psy-14-0022`, type `USB`);
      `cat /sys/class/power_supply/tcpm-source-psy-14-0022/{in0_input,curr1_input}`
      (expect 0/empty).
- [ ] 0.2 Re-probe the PMIC identity (no sudo):
      `cat /sys/bus/i2c/devices/13-0036/name` (expect `axp8191`),
      `cat /sys/bus/i2c/devices/13-0034/name` (expect `axp515`),
      `ls -l /sys/bus/i2c/devices/13-0036/driver` (expect `axp20x-i2c`),
      `cat /sys/bus/i2c/devices/13-0034/waiting_for_supplier` (expect 0),
      plus a full `ls /sys/bus/i2c/devices/` to capture every i2c node on the
      board (so an unexpected HAT-connected node stands out later).
- [ ] 0.3 Note the ~40 regulator names from `ls /sys/class/regulator/`
      (unchanged expected; snapshot for the record).
- [ ] 0.4 Re-examine the three 2026-09-17 HAT underside photos at full
      resolution (overview `PXL_20260917_070632106.jpg`, lower
      `PXL_20260917_070641017.jpg`, battery close-up
      `PXL_20260917_070648424.jpg`): record every legible or partial
      marking on the eight-pin IC and its neighbors, the inductor value
      (`4R7` = 4.7 µH expected), and the cell connector. **Do not assign a
      part number on a partial mark** — record the letters visible, with
      confidence noted; the ID is made in WS1 from better photos +
      literature.
- [ ] 0.5 Literature pass (agent): (a) the Spotpear reference-board wiki
      and any community schematics/teardowns of that design's power
      section; (b) GamePi HAT documentation / GitHub for a schematic,
      pinout, or "connect an external charger" note for the battery
      connector; (c) candidate charge-controller families matching an
      8-pin package + external inductor + single-cell LiPo (with charge /
      boost / status-pin possibilities), ranked against whatever marking
      fragments 0.4 produced. Produce a short candidate list (≤3) with the
      evidence for each — this is the working hypothesis WS1 confirms or
      kills.
- [ ] 0.6 Journal checkpoint noting the baseline, the photo findings, and
      the WS0.5 candidate list; `power-path.md` "How we know" gains the
      re-probe citation if anything drifted (same change).

## WS1 — Chip identification (operator-led, read-only)

Every result feeds `pmic-axp8191.md` / `battery.md` in the same change as
its journal note.

- [ ] 1.1 Operator (D3): `sudo apt-get install -y i2c-tools`.
- [ ] 1.2 Operator: read-only chip-ID reads on both live addresses —
      `sudo i2cget -y 13 0x34 0x00` … registers `0x00`–`0x06` on `0x34`,
      same on `0x36`. (Safe: read-only; the AXP ID register family sits in
      this range.) Record verbatim; map against known AXP ID values to
      confirm the `axp8191` / `axp515` kernel naming is correct and to
      learn what the inert `0x34` chip actually is.
- [ ] 1.3 Operator: **detect-only** `sudo i2cdetect -y` on the low bus
      numbers where a HAT-connected IC could plausibly sit (enumerate the
      present i2c adapters first via `ls /dev/i2c-*`, then sweep the board
      buses; we expect `13` and `14` to show only the known nodes — any
      *new* responding address is recorded and left alone, no further
      transaction). Run with the deck on its HAT-microUSB feed.
- [ ] 1.4 Operator: fresh **macro photos** of the eight-pin IC and its
      neighbor components on the HAT underside (bright, raking light for
      the laser marking; a phone close-up at the same angle as the
      existing set). The existing 2026-09-17 set stays as-is; new photos
      are added to `docs/hardware/images/` with dates and cited in the
      records.
- [ ] 1.5 ID synthesis (agent): match the 1.2/1.3 electrical IDs and the
      1.4 marking against the WS0.5 candidate list. Outcome is one of:
      (i) **confident ID** (marking + datasheet + electrical behavior align)
      → record part number, package, and the datasheet's charge topology;
      (ii) **family ID** (marking gives a manufacturer + family, exact
      variant uncertain) → record the family and the shared topology
      assumptions; (iii) **unidentified** → record the evidence and the
      elimination steps; WS2 then leans harder on bench behavior.
- [ ] 1.6 Determine the relationship question: does the HAT charge circuit
      feed the SBC's VBUS net (via the "powers the pi from the top" path),
      sit downstream of it, or run in parallel with any AXP input —
      stated as the evidence supports, with the visible traces / connector
      evidence cited, and explicitly marked as inference where it is.
- [ ] 1.7 Journal checkpoint + same-change updates to `pmic-axp8191.md`
      (chip-ID readout, any identity correction) and `battery.md`
      (candidate path confirmed/killed by the ID).

## WS2 — Input-source test matrix (operator-led, meter depth per D1)

The three configurations from `power-path.md`'s proposed matrix, each run
at **standby** (idle desktop) and under a **nominal game workload** (a
known sustained game/agent load, fan allowed to ramp), with the board's
stability (no reset, no brownout artifacts in the log) recorded.

Record format per configuration (one table row set in the journal, carried
into `power-path.md`):

| Config | Feed present | Port metered | V_in | I_in (standby) | I_in (load) | tcpm node | Charge behavior (cell) | Stability |
|---|---|---|---|---|---|---|---|---|

- [ ] 2.1 **Config (a) — HAT microUSB only** (the status quo baseline):
      meter the HAT port; record standby + load input V/I; record the
      tcpm node (expect all zeros — nothing on the SBC port) and the
      cell's charge behavior if any readout path exists (else meter the
      cell connector in series, or mark "not observable").
- [ ] 2.2 **Config (b) — SBC USB-C only** (the headline question):
      remove the HAT feed, plug a known supply into the SBC port (supply
      capability noted in the record — e.g. 5 V / 3 A bank). Record: does
      it boot at all (cell present/absent per D2)? tcpm node values
      (`type`, `in0_input`, `curr1_input` — this is the
      `usbc-power.md` half, updated same change)? Can it sustain standby,
      and the game workload (with fan), or does it brownout? Cell charge
      behavior (charging from the SBC feed? floating? discharging?) to the
      agreed depth, then restore the verified feed and recharge per D4.
- [ ] 2.3 **Config (c) — both feeds at once**: both ports plugged. The
      decisive data is the **meter current per port**: which source
      carries the load, does either bounce/fight (meter stability, no
      restarts, log clean), and what the tcpm node reports while the HAT
      feed is also present. Cell charge behavior under the combined load.
- [ ] 2.4 **Stability pass under workload** for each config the matrix
      completed: a fixed nominal session (time-boxed, same game/agent
      task across configs for comparability) and a post-session check —
      no unexpected reboots (journal timestamps), no thermal runaway
      (thermal zones per `soc-thermal.md` stayed in envelope).
- [ ] 2.5 **Answer the two open questions from the record in writing**:
      (i) *"What happens when both are plugged in?"* — with the meter
      evidence; (ii) *"Can the SBC-side USB-C alone power the whole deck?"*
      — yes/no/partial with the measured headroom.
- [ ] 2.6 Update `power-path.md`: "What we know" becomes the matrix,
  "How we know" gains the meter/journal citations (bench meter model
      noted; config-b tcpm readings re-probeable via the same sysfs
      commands as today), "What it portends" replaced by the deployment
      guidance the matrix just earned (the record currently says we must
      not write "just plug in the SBC's USB-C" advice on a hunch — that
      sentence now gets its evidence).
- [ ] 2.7 Journal checkpoint with the full table and the two answers;
      README row for power-path moved only if the status actually moved
      (see Exit criteria).

## WS3 — Battery visibility (conditional on WS1/WS2)

Enter only after 1.5 and the matrix exist, with the path chosen by the
evidence. If WS1 says "no chip on this board measures the cell, and no
SBC ADC input is connected to it," the honest outcome is **document the
dead end** in `battery.md` (and note the row stays as-is) — that is a
complete, correct WS3, and no service is built.

- [ ] 3.1 **Path selection** (recorded as a decision in the journal):
      (1) `axp20x`-driver ADC/fuel-gauge side enabled for the identified
      chip — only if the WS1 wiring evidence shows the cell reaches that
      chip's ADC input; (2) a small **root userspace I2C helper** reading
      the identified charger/gauge chip's status registers, exposing the
      minimum (`voltage_now`, `status`, plus `energy_full`/`capacity` if
      the chip genuinely has them); (3) a **driver-level minimal patch**
      only if a mainline driver for the identified chip already has the
      properties and is simply unbound/distinct — a from-scratch driver is
      out of scope for this plan; (4) **dead end documented**, no code.
- [ ] 3.2 If (2): the helper is a small tracked tool (C or python3) under
      `tools/`, a root service unit modeled on the existing `gamepi-*`
      units, exposing its reading either as a sysfs-style file under
      `RuntimeDirectory` or a unix-socket query — whichever is smaller —
      and a `power_supply` battery node is the stretch goal only if it
      can be done without a driver.
- [ ] 3.3 If (1)/(2)/(3) land anything on the board: **setup.sh fold-in
      with the standing mandate** — `docs/setup.md` documents the new
      managed unit/file, a verify row asserts the telemetry exists and is
      sane (e.g. battery node present with non-zero `voltage_now`, or the
      helper unit active with a last-read sanity check), and the change
      passes live `apply -> verify` on this board before commit. Verify
      rows are additive only — no existing row semantics change.
- [ ] 3.4 **Charge-awareness follow-up is explicitly deferred** (out of
      scope here, noted for the future-plan index): "low-battery warning"
      and "stop-charging to preserve the cell" behaviors wait on 3.2/3.3
      having landed and proven stable; this plan only gets the OS to *know*.
- [ ] 3.5 Journal checkpoint: the chosen path, the evidence that
      justified it, and what was rejected and why; `battery.md` updated
      same change (readout path or documented dead end), README row for
      battery moved if and only if the status moved.

## WS4 — Documentation and close-out

- [ ] 4.1 **Standing-rule audit**: `power-path.md`, `battery.md`,
      `usbc-power.md`, and `pmic-axp8191.md` all reflect every finding of
      this arc (each "What we know" claim carries a "How we know" citation
      — re-probe command, meter reading + journal link, photo + date, or
      datasheet citation); the four files still cross-link each other
      consistently.
- [ ] 4.2 **README Hardware Status**: flip the power-path row (and the
      battery row, independently) in the same commit as the evidence that
      supports the flip; the row's short text explains the one-line
      takeaway (e.g. which feeds verified, what the OS can now tell you).
- [ ] 4.3 **Journal**: one final arc note tying WS0–WS4 together with the
      plan link — the same "one dated note per checkpoint" rhythm as the
      rest of `journal/`.
- [ ] 4.4 **`TODO.md` / future-plan index**: if 3.4's charge-awareness
      follow-up or any bench gap needs tracking, it gets a line there (or
      a new future plan), not silence.
- [ ] 4.5 **Plan close-out**: flip `status:` to `past` *before*
      `git mv` to `plans/past/`, then regenerate indexes:
      `python3 agentic-pipelines/scripts/regenerate_plan_indexes.py --repo-root .`.

## Exit criteria (what "Done" means for the row)

The power-path README row flips to **Done** when *all* of:

1. the HAT power IC is identified per 1.5 outcome (i) or (ii), with its
   charge topology and its relationship to the SBC VBUS/AXP input stated
   with evidence (1.6);
2. the three-config matrix is complete with the per-config numbers (2.1–
   2.4), and the two record-level open questions are answered (2.5);
3. battery visibility is either live in the OS (3.1–3.3, proven by its
   verify row) or the dead end is documented with the evidence (3.1(4));
4. all four records + README + journal are consistent (4.1–4.3).

If the operator declines metering (D1), the achievable landing is
**Partial** — the row's text names the specific missing measurement
("per-port input current") as the single gap instead of pretending the
matrix ran.

## Rollback

WS0–WS2 deploy nothing and change nothing on the board (WS1's `i2c-tools`
install is an inert read tool; removing it is `sudo apt-get remove -y
i2c-tools`). WS3 rollbacks are per-its-shape: the root helper = stop +
remove the managed unit (setup.sh apply re-converges the rest); a driver
patch = kernel revert and reboot (and is why 3.1(3) is a mainline-only,
separate decision). The board's verified power feed (HAT microUSB) is
restored immediately after every config in WS2 — the deck is never left on
an untested feed.