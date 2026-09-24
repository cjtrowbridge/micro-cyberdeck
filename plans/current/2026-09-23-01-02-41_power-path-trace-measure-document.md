---
plan_id: 2026-09-23-01-02-41_power-path-trace-measure-document
title: Power path — identify the HAT charge IC, run the 3-config input matrix, document
summary: Close out the power-path row (Not started -> Done, or Partial with explicit bench-tooling gap): (WS0) re-baseline today's observable state (tcpm all-zeros, no fuel node, axp8191 @0x36 + axp515 @0x34 on i2c-13) and re-examine the HAT underside photos; (WS1) operator-led READ-ONLY chip-ID reads on both i2c-13 addresses plus fresh macro photos and a literature pass on the Spotpear reference clone / GamePi HAT to identify the mysterious eight-pin IC + 4R7 inductor charge circuit; (WS2) the proposed 3-config input test matrix - (a) HAT microUSB only, (b) SBC USB-C only, (c) both - at standby and under nominal game workload, recording per-port input V/I, tcpm negotiated values, charge behavior, and stability; (WS3, conditional) minimal battery telemetry via whichever path WS1/WS2 indicate (AXP driver ADC, root userspace I2C helper, or HAT-chip exposure), folded into setup.sh with the docs/setup.md mandate and live apply->verify only if a service is needed; (WS4) same-change updates to all four hardware records (power-path, battery, usbc-power, pmic-axp8191) + README rows + journal. Safety carried over from the 2026-09-16 decision: register access is read-only, no agent-driven PMIC writes, controlled-discharge cap for config (b).
status: current
created_at: 2026-09-23-01-02-41
revised: 2026-09-23
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
  *(EXECUTION NOTE 2026-09-23: **D1–D4 all decided 2026-09-23** — USB
  power meter available (D1); cell installed, roughly 50 %+ charge (D2);
  i2c-tools install approved, operator runs the sudo (D3); config (b)
  discharge stops at ~80 % remaining with immediate recharge (D4).
  Everything run on the board so far is unprivileged sysfs reads.)*
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

- [x] **D1 — Metering.** Is a USB power meter / inline multimeter setup
      available for each input port (or should one be procured)? If no,
      WS2 proceeds in software-observable mode and the row caps at
      Partial. (Blocks 2.2–2.5 depth, not 2.1.) **Decided 2026-09-23:
      USB power meter available** — the matrix proceeds at full depth
      (2.2–2.5 live).
- [x] **D2 — Cell presence & state.** Is the 11.1 Wh LiPo currently
      installed, and what is its state of charge? Config (b) behavior
      differs materially between "cell present and charged" (expected to
      take over) and "cell absent" (expected to not boot — itself an
      answer). **Decided 2026-09-23: cell installed, roughly 50 %+ state
      of charge** — config (b) runs with the cell present; discharge
      floor per D4.
- [x] **D3 — i2c-tools approval.** Approve `sudo apt-get install -y
      i2c-tools` and the operator-run read-only reads in WS1 (the agent
      supplies the exact commands; the operator executes and we record
      verbatim). **Decided 2026-09-23: approved — operator runs the
      install (1.1) and the root reads (1.2–1.3); output recorded
      verbatim in the WS1 journal note.**
- [x] **D4 — Discharge depth for config (b).** Agreed stopping point for
      any controlled discharge of the cell (recommend: stop at ~80 %
      remaining if any readout path exists by then, else stop before the
      board's brownout and recharge immediately). Record the decision in
      the journal with the date. **Decided 2026-09-23: stop at ~80 %
      remaining (the safe default), immediate recharge to full; the HAT
      microUSB feed waits at hand before the SBC-USB-C feed is relied
      on in config (b).**

## WS0 — Re-baseline and evidence assembly

Agent-runnable (no sudo), done before any HW interaction; findings recorded
in a journal note that WS1/WS2 cite.

- [x] 0.1 Re-probe the power-observable tree and record verbatim.
      DONE 2026-09-23 (unprivileged): **`/sys/class/fuel/` does not
      exist** (not merely empty — `ls` exits 2; the class directory is
      absent because no driver registered a fuel-gauge device). `power_supply` holds exactly one
      device, `tcpm-source-psy-14-0022`, `type=USB`. **DRIFT vs. the
      Sept-2026 record:** the property files are **not** `in0_input` /
      `curr1_input` (the kernel's `power_supply` naming for this device
      changed; the old names no longer exist — `cat` exits "No such file"). The current
      set: `online=0`, `voltage_now=0`, `voltage_max=0`, `voltage_min=0`,
      `current_now=0`, `current_max=0`, `usb_type="[C] PD PD_PPS"`,
      plus a **`hwmon0` child** (its own hwmon node, unseen in the earlier
      record). All values remain zero — the node is the idle
      kernel stub, as expected for the HAT-powered configuration — but
      `power-path.md`'s re-probe command must be updated (same change).
      Journal: `2026-09-23-power-path-ws0-baseline.md`.
- [x] 0.2 Re-probe the PMIC identity (no sudo).
      DONE 2026-09-23: `13-0036/name` = `axp8191`, driver symlink →
      `axp20x-i2c`; `13-0034/name` = `axp515`, `waiting_for_supplier=0` —
      all as recorded. **Full i2c census (the new baseline):** devices
      `12-0014` = `gt9271` (touch), `13-0034` = `axp515`, `13-0036` =
      `axp8191`, `14-0022` = `fusb302`, `15-0051` = `hym8563` (RTC); adapters
      `i2c-9`, `i2c-11`, `i2c-12`, `i2c-13`, `i2c-14`, `i2c-15`, `i2c-20` —
      **`i2c-9`, `i2c-11`, `i2c-15`, `i2c-20` carry no bound device** (three
      of the seven adapters are empty buses; the HAT's charge IC, if it is
      I2C-addressable, would be a *new* node on one of these — the census
      is the baseline WS1's detect sweep compares against). `/dev/i2c-*`
      nodes exist for all seven, `root:i2c crw-rw----` — root-only as
      recorded; `i2c-tools` **not installed** (`which i2cdetect i2cget` →
      nothing), D3 still pending.
- [x] 0.3 Note the ~40 regulator names from `ls /sys/class/regulator/`.
      DONE 2026-09-23: **43 entries** `regulator.0`–`regulator.42` (the
      "~40" in `pmic-axp8191.md` is directionally right; the exact count is
      43 today — the names-per-rail detail is that record's territory, not
      re-derived here).
- [x] 0.4 Re-examine the three 2026-09-17 HAT underside photos at full
      resolution.
      DONE 2026-09-23 (all three viewed at full resolution). Findings:
      - **Eight-pin IC** (close-up `PXL_20260917_070648424.jpg`): two-row,
        ~4-pin-per-side SOIC-8-style package at the bottom-right of the power
        section, sitting between a `C101`/`C115` capacitor pair and the `4R7`
        inductor. **No marking is legible in any of the three photos** —
        zero characters with confidence, so no partial-string candidate and
        no part number is assigned (per the item's own rule).
      - **`4R7` inductor** confirmed legible (4.7 µH) in the close-up, as
        recorded.
      - **Cell connector**: two-pin header at the right edge, red/black
        leads, as recorded.
      - **New observation not in the prior record:** a **16-pin socketed
        DIP/lock-and-lock-style IC** (white footprint, two white dots, notch
        on the left side) sits at the top-center of the same power-section
        photo, directly above the film capacitors (`C115`, `C102`) and a SMD
        resistor (`5106`); its marking is not legible in these views either,
        and its package is **socketed** — unlike the eight-pin IC — which
        either (a) is a dip/lock-and-lock test/programming header or
        adapter intentionally populated with a SOIC chip, or (b)
        is the second member of the power circuit (e.g. an I2C-comms
        bridge between the HAT charger/ADC and the SBC's `i2c-13`).
      - **Upper-left corner:** the large BGA/QFN IC (the larger chip noted
        in `docs/hardware/README.md`) sits under/behind the thermal
        sticker; its marking is not legible in any view — consistent with
        the README's note that no part number has been assigned.
      - Conclusion for WS1: **the photos cannot identify the eight-pin IC
        by marking** — the ID has to come from the fresh macro photos
        (1.4) and/or the electrical census (1.2/1.3) plus the literature
        candidate list (0.5). The 16-pin socketed part becomes a second
        explicit question for 1.6.
- [x] 0.5 Literature pass (agent).
      DONE 2026-09-23. Findings:
      - **(a) Spotpear reference wiki** (the board this HAT clones): full
        page fetched — the reference design's power section is **not
        documented at all** on the wiki: no charge controller, no battery
        connector, no 5V-input section beyond "Operating voltage: 3.3V".
        The wiki's schematic PDF
        (`cdn.static.spotpear.com/.../1.54inch%20LCD.PDF`) could not be
        extracted to text (binary PDF through the fetcher) — **open item
        for the operator to read locally if they want the base design's
        power net names**. The reference board's power story is
        "Pi powers it via the top-connector passthrough" — i.e. **the
        charge controller + battery connector are exactly the
        clone-modification CJ notes**, and no public schematic of the
        modified design exists anywhere reachable.
      - **(b) The HAT's sourcing**: it's an Amazon link
        (`amzn.to/4xX0VVJ` affiliate redirect — not resolvable in this
        session) for an **unbranded/white-label GamePi-clone HAT**;
        no manufacturer, no model, no schematic, no forum thread
        reachable without the operator's logged-in session. The project
        page (cjtrowbridge.com/projects/2026-09-09-micro-cyberdeck/) is
        consistent with the README: "a clone ... modified with extra
        features like speakers, aux cord plug, and a battery charge
        controller." No identification path from the vendor side.
      - **(c) Candidate charge-controller families** matching an 8-pin
        SOIC + external 4.7 µH inductor + single-cell LiPo + a possible
        I2C/pin status path to the SBC, ranked:
        1. **TP4056-class (DW01+protected or plain) single-cell LiPo
           linear charger, 8-pin SOIC variant** (e.g. TP4056 in SOIC-8,
           or the common "TP4056 + DW01" combo board's IC). The 4.7 µH
           inductor is **NOT** a TP4056 thing (TP4056 is linear, no
           inductor) — so a pure TP4056 is a **weaker** candidate; flag
           for elimination in WS1 if the inductor is confirmed charge-circuit.
        2. **Buck-boost charge/boost controller in SOIC-8 with a 4.7 µH
           inductor** (e.g. BAX1212, PT4103, or a generic
           "LTC4015-clone"-class chip found on cheap LiPo battery HATs —
           the 4.7 µH inductor strongly suggests a **switching** topology,
           not linear). This is the **leading** candidate: the inductor
           value and the 8-pin package match the visible
           components, and a charge+boost combo explains how a single-cell
           pack can feed a 5V-rail SBC from a 3.3–4.2V cell without a
           separate regulator.
        3. **Two-chip solution: SOIC-8 linear/buck charger + a small
           boost/regulator** for the SBC feed, with the inductor belonging
           to one of them. Lower confidence in the package count, kept as a
           fallback if 1.2/1.3's electrical read shows no I2C chip on the
           HAT side.
      - **Working hypothesis for WS1**: the eight-pin IC is a switching
        charge/boost controller (candidate 2), the 4R7 is its power
        inductor, and the 16-pin socketed part (new finding from 0.4) may
        be the I2C bridge or a test header. The `i2c-13` addresses
        `0x34`/`0x36` are **on the SBC side** (the two AXP chips); a HAT
        charge controller that exposes I2C would be a **new address on one
        of the seven adapters** — the census in 0.2 is the baseline to
        diff against. This hypothesis is what 1.2/1.3/1.4 confirm or kill.
- [ ] 0.6 Journal checkpoint noting the baseline, the photo findings, and
      the WS0.5 candidate list; `power-path.md` "How we know" gains the
      re-probe citation if anything drifted (same change).

## WS1 — Chip identification (operator-led, read-only)

Every result feeds `pmic-axp8191.md` / `battery.md` in the same change as
its journal note.

- [x] 1.1 Operator (D3): `sudo apt-get install -y i2c-tools`.
      DONE 2026-09-23 — `dpkg -s i2c-tools` → `Status: install ok
      installed` (operator ran the sudo; verified agent-side). Note: the
      `/dev/i2c-*` nodes remained `root:i2c` after the install and user
      `cj` is not in the `i2c` group, so 1.2/1.3 still run under the
      operator's `sudo`. One-sudo wrapper: `tools/i2c_power_probe.sh`
      (sections 2–3 are the reads; section 1 is an unprivileged sysfs
      snapshot), run as
      `sudo bash tools/i2c_power_probe.sh 2>&1 | tee /tmp/ws1_i2c_probe.log`.
- [x] 1.2 Operator: read-only chip-ID reads on both live addresses —
      `sudo i2cget -y 13 0x34 0x00` … registers `0x00`–`0x06` on `0x34`,
      same on `0x36`. (Safe: read-only; the AXP ID register family sits in
      this range.) Record verbatim; map against known AXP ID values to
      confirm the `axp8191` / `axp515` kernel naming is correct and to
      learn what the inert `0x34` chip actually is.
      DONE 2026-09-23 (operator, one sudo, via `tools/i2c_power_probe.sh`):
      **`0x36` → EBUSY `Device or resource busy`** on all 7 regs (the bound
      `axp20x-i2c` holds the live chip exclusively — expected for an
      in-service PMIC; it confirms `0x36` is the live, driver-claimed PMIC
      and that raw reads are by design refused while it grips). **`0x34` →
      `Read failed` (wire NAK)** on all 7 regs — the silicon the DT expects
      at `pmu@34` (`x-powers,axp515`) **does not acknowledge on the wire**
      (absent / unpowered / not in the deck's power path). The kernel
      naming is confirmed as the *DT-provided* compatibility, not by a
      numeric ID (we deliberately did not unbind the live driver to force
      one — write-class disturbance of the live rail path). **No mainline
      driver/binding for `x-powers,axp515` exists** (text search of
      torvalds/linux: zero hits), so it could never bind on any mainline
      kernel. The September-2026 open question ("axp515 may be a second
      PMIC with a battery ADC") is **closed: No**. Journal:
      `2026-09-23-power-path-ws1-ic-identification.md`; verbatim output
      `/tmp/ws1_i2c_probe2.log`.
- [x] 1.3 Operator: **detect-only** `sudo i2cdetect -y` on the low bus
      numbers where a HAT-connected IC could plausibly sit (enumerate the
      present i2c adapters first via `ls /dev/i2c-*`, then sweep the board
      buses; we expect `13` and `14` to show only the known nodes — any
      *new* responding address is recorded and left alone, no further
      transaction). Run with the deck on its HAT-microUSB feed.
      DONE 2026-09-23 (operator, one sudo, same script run): swept **all
      seven instantiated adapters** — `9 11 12 13 14 15 20` (DT enumerates
      16 `twi@` nodes; only these 7 instantiate on this DTS — completeness
      verified). Result: **no new unclaimed responder on any bus reachable
      through the 40-pin header.** The only raw responder anywhere is `0x30`
      on **`i2c-20`**, which is the **HDMI controller's CEC/DDC bus**
      (`5520000.hdmi0`), not a power-path bus — recorded and left alone per
      the detect-only rule (out of this arc's scope). Unresolved-identity
      note: `12-0014` gt9271 and `15-0051` hym8563 are **enumerated but
      unbound** (no driver symlink; collateral, tracked by their own
      records, not power-path). **Conclusion: the HAT's eight-pin power IC
      is not I2C-visible to the SBC** — a HAT charge/gauge chip exposing
      I2C would have appeared as a new address; none did. Caveat recorded:
      the raw `i2cdetect` grids mangled in-paste and under-mark bound
      devices, so the sysfs claimer list (3a) + the 1.2 EBUSY/NAK split are
      the trustworthy evidence; the grids only catch *new* responders.
      Journal: `2026-09-23-power-path-ws1-ic-identification.md`.
- [x] 1.4 Operator: fresh **macro photos** of the eight-pin IC and its
      neighbor components on the HAT underside (bright, raking light for
      the laser marking; a phone close-up at the same angle as the
      existing set). The existing 2026-09-17 set stays as-is; new photos
      are added to `docs/hardware/images/` with dates and cited in the
      records.
      DONE 2026-09-24 (operator upload, 6 photos): the 09-17 set is kept;
      new set added to `docs/hardware/images/` — the decisive close-up
      `PXL_20260924_035455686.jpg` reads the eight-pin IC's laser marking
      **crisply: `9813` (line 1) / `2512` (line 2)**;
      `PXL_20260924_035505977.jpg` is the angled overview. New finding: a
      **second, separate IC** marked `NS8002` / `216Y1` sits just above the
      power IC (observed, not the power target, not yet analyzed). Cited in
      `power-path.md`, `battery.md`, `pmic-axp8191.md` (same change).
- [x] 1.5 ID synthesis (agent): match the 1.2/1.3 electrical IDs and the
      1.4 marking against the WS0.5 candidate list. Outcome is one of:
      (i) **confident ID** (marking + datasheet + electrical behavior align)
      → record part number, package, and the datasheet's charge topology;
      (ii) **family ID** (marking gives a manufacturer + family, exact
      variant uncertain) → record the family and the shared topology
      assumptions; (iii) **unidentified** → record the evidence and the
      elimination steps; WS2 then leans harder on bench behavior.
      DONE 2026-09-24 — **outcome (ii), family ID with a leading candidate,
      datasheet-unconfirmed.** Confirmed facts (photo evidence): SOP-8,
      marking `9813`/`2512` (`2512` = date code week 12 / 2025), adjacent
      `4R7` (4.7 µH) inductor + `W2A1` diode → **switching buck-boost**
      topology (rules out WS0.5 candidate 1, the TP4056-class linear
      charger, which has no inductor). **WS0.5 candidate 2 confirmed as
      the class** (switching charge/boost controller, SOP-8, single-cell);
      **leading candidate `SY89813`-class (Silex)** — last-4-digits marking
      convention, SOP-8, 1 A charge + 2 A boost, **non-I2C** interface
      (consistent with WS1.3: the IC is not I2C-visible to the SBC).
      **Not confirmed against a datasheet**: silex-semi.com failed to
      extract (twice), DigiKey PMIC search HTTP 403, LCSC/GitHub
      (GamePi/OrangePi/Allwinner scopes) text searches returned no
      `SY89813`/`89813`/`9813` hits. Recorded per the evidence-first
      mandate as *marking-confirmed, family-plausible, datasheet-unverified*
      in `power-path.md` + `battery.md` (same change). Topology (switching
      charge/boost) is the load-bearing conclusion; the part number is a
      hypothesis. WS2 leans on bench behavior for the charge/feed
      relationship (1.6).
- [ ] 1.6 Determine the relationship question: does the HAT charge circuit
      feed the SBC's VBUS net (via the "powers the pi from the top" path),
      sit downstream of it, or run in parallel with any AXP input —
      stated as the evidence supports, with the visible traces / connector
      evidence cited, and explicitly marked as inference where it is.
- [x] 1.7 Journal checkpoint + same-change updates to `pmic-axp8191.md`
      (chip-ID readout, any identity correction) and `battery.md`
      (candidate path confirmed/killed by the ID).
      DONE 2026-09-23: journal `2026-09-23-power-path-ws1-ic-identification.md`
      written; same-change record updates: `pmic-axp8191.md` (axp515 →
      wire-dead, no mainline driver, open question closed), `battery.md`
      (axp515 ADC path killed; I2C-helper path needs an I2C chip — none
      found; AXP8191-ADC-if-wired remains the only on-SBC candidate, to be
      tested in WS2), `power-path.md` (no-I2C-visible-HAT-IC census
      finding), `touch-gt9271.md` + `rtc-hym8563.md` (re-confirmed unbound,
      mechanism: module registered + compatible matched, so the unbind is a
      probe/config issue, operator-level `dmesg` is the next evidence).

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