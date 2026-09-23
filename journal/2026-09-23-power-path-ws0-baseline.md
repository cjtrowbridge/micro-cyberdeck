# Power path — WS0 re-baseline (2026-09-23)

Checkpoint for the power-path arc, plan
`plans/current/2026-09-23-01-02-41_power-path-trace-measure-document.md`
(created 2026-09-23, plan commit `b975a75`). All probes below are
unprivileged — nothing in WS0 touched the board's configuration, no root,
no writes anywhere. `setup.sh` is not involved.

## 0.1 — the power-observable tree (re-probed, verbatim)

- **`/sys/class/fuel/` does not exist** — `ls` exits 2. The earlier
  records say "empty"; the directory is absent entirely (no fuel-gauge
  driver registered, so the class never materialized). Same practical
  consequence: no `capacity` / `voltage_now` / `status` anywhere.
- `/sys/class/power_supply/` holds exactly one device:
  `tcpm-source-psy-14-0022`, `type=USB`.
- **Naming drift vs. the September-2026 records:** the *top-level* node's
  property files are no longer the hwmon-style `in0_input` / `curr1_input` —
  `cat` on both names at the top level returns "No such file or directory".
  The top level now carries upstream-style power-supply properties:
  `online=0`, `voltage_now=0`, `voltage_max=0`, `voltage_min=0`,
  `current_now=0`, `current_max=0`, `usb_type=[C] PD PD_PPS`. The legacy
  names were **moved, not removed**: the node now has a **`hwmon0` child**
  (a child the earlier records do not mention) whose files include
  `in0_input`, `in0_min`, `in0_max`, `curr1_input`, `curr1_max`, `name=
  tcpm_source_psy_14_0022` — all the input/current values still read
  **zero**, so the record's all-zero finding is unchanged, only the
  path moved. In the HAT-powered configuration the fUSB302 on `i2c-14` @
  `0x22` has nothing on the SBC's own port to negotiate, so the
  idle-stub reading is **expected, not a fault** (unchanged
  interpretation).
- `power-path.md`'s re-probe command was updated in this same change to
  the current property names.

## 0.2 — PMIC identity + full i2c census (re-probed)

- `13-0036/name` = **`axp8191`**, driver symlink → `axp20x-i2c` (binding
  unchanged; the PEK power button and the ~40 rails all hang off this).
- `13-0034/name` = **`axp515`**, `waiting_for_supplier=0`, no driver
  link — probed at boot, still inert, exactly as the record says.
- **Full i2c census — new baseline for this arc:**

  | adapter | device | `name` | role |
  |---|---|---|---|
  | `i2c-12` | `12-0014` | `gt9271` | touch controller |
  | `i2c-13` | `13-0034` | `axp515` | SBC PMIC companion (inert) |
  | `i2c-13` | `13-0036` | `axp8191` | SBC PMIC (active) |
  | `i2c-14` | `14-0022` | `fusb302` | SBC USB-C PD/TCPM |
  | `i2c-15` | `15-0051` | `hym8563` | RTC |
  | `i2c-9` | — | (none) | empty bus |
  | `i2c-11` | — | (none) | empty bus |
  | `i2c-20` | — | (none) | empty bus |

  Three of the seven adapters carry **no bound device**: `i2c-9`,
  `i2c-11`, `i2c-20`. Consequence for
  the arc: if the HAT's charge controller is I2C-addressable and wired to
  the SBC, it would surface as a **new device node** on one of these buses —
  none exists today, so either the HAT power IC is **not I2C-visible to
  the SBC** (e.g. wired by bare GPIOs/status pins only), it sits on a
  bus the kernel hasn't probed for it, or it simply has no I2C
  interface. The detect sweep in WS1.3 (operator, root) re-sweeps these
  same buses and diffs against this table; a new responder is the finding,
  and per the safety frame the sweep stops at "record it" with no further
  transaction.
- `/dev/i2c-*` nodes: all seven present, `root:i2c crw-rw----`
  (root-only, unchanged). `i2c-tools` **not installed** — decision D3
  (`sudo apt-get install -y i2c-tools`) is still pending the operator.

## 0.3 — regulator census

`/sys/class/regulator/` has **43 entries** (`regulator.0`–`regulator.42`)
today; `pmic-axp8191.md` says "roughly 40" — directionally right, the
exact count is 43. No per-rail live values appear anywhere in the tree
(unchanged: the bound `axp20x` variant exposes names, not µV).

## 0.4 — the three 2026-09-17 HAT underside photos, re-read at full resolution

All three viewed (overview `PXL_20260917_070632106.jpg`, lower
`PXL_20260917_070641017.jpg`, battery close-up `PXL_20260917_070648424.jpg`):

- The **eight-pin IC** (SOIC-8-style, two rows of ~4 pins) sits in the
  lower-right of the power section, between a `C101`/`C115` capacitor
  pair and the **`4R7`** inductor (4.7 µH, legible, as recorded). **No
  marking is legible in any of the three photos** — zero characters at any
  confidence. Per the plan's own rule, no part number is assigned from a
  non-legible mark; the ID comes from WS1's fresh macro photos (1.4) and
  the electrical census (1.2/1.3).
- **New observation not in the prior records:** a **16-pin socketed
  DIP-style IC** (white footprint, two white dots, notch left) sits at the
  top-center of the same power-section photo, above film caps `C115` /
  `C102` and an SMD resistor marked `5106`. Also unmarked in these views.
  Because it is *socketed* — unlike the eight-pin part — it is likely
  either a test/programming header populated with a SOIC device, or the
  second member of the power circuit (an I2C bridge between the HAT power
  IC and the SBC's `i2c-13` is one working hypothesis, explicitly labeled
  hypothesis). It becomes the second explicit question in WS1.6.
- The **two-pin cell connector** (right edge, red/black leads) is as
  recorded; the 11.1 Wh LiPo's state is unknown to the OS (nothing reads
  it) — decision D2 (cell present? state of charge?) is still pending the
  operator.
- The **large BGA/QFN IC** in the upper-left under the thermal sticker
  remains unidentifiable in these views, consistent with
  `docs/hardware/README.md`'s note.

## 0.5 — literature pass (agent-side, no board access)

- **Spotpear reference wiki** (the design this HAT clones): the wiki page
  was fetched in full. The reference board's power section is **not
  documented at all** — no charge controller, no battery connector,
  nothing about the 5 V input beyond "operating voltage: 3.3 V". The
  wiki's schematic PDF exists
  (`cdn.static.spotpear.com/.../1.54inch%20LCD.PDF`) but the fetcher
  cannot extract it (binary PDF, no text). **Operator option:** if they
  want the base design's power-net names, the PDF is worth a manual read
  — it documents the *unmodified* board, so it will not show the
  charge-controller modification, but it may show the top-connector 5 V
  net the clone feeds.
- **HAT sourcing:** the HAT is an unbranded white-label GamePi clone
  bought via an Amazon affiliate link (not resolvable in-session); no
  manufacturer, model, schematic, or forum thread is reachable without the
  operator's logged-in session. The author's project page
  (cjtrowbridge.com/projects/2026-09-09-micro-cyberdeck/, updated
  2026-09-14) is consistent with the README: a clone "modified with extra
  features like speakers, aux cord plug, and a battery charge
  controller" — the charge controller is **exactly** the part with no
  public documentation.
- **Candidate charge-controller families** (8-pin SOIC + 4.7 µH inductor
  + single-cell LiPo), per the plan's item 0.5:
  1. **Pure TP4056-class linear charger, SOIC-8** — *weak* candidate: a
     linear charger has no power inductor, so the legible `4R7` sitting
     directly beside the IC argues against it (the inductor could belong
     to a different circuit, but it is positioned as the IC's).
  2. **Buck-boost charge/boost controller, SOIC-8, external 4.7 µH**
     (the family seen on cheap single-cell "charge + step-up to 5V" HAT
     circuits) — **leading** candidate: switching topology requires the
     inductor, and a charge+boost combo would let the 3.3–4.2 V cell feed
     the SBC's 5 V input directly, matching "it powers the pi from the
     top" without a separate regulator.
  3. **Two-chip split** (SOIC-8 charger + a separate small boost) with
     the inductor belonging to one of them — fallback if the electrical
     reads show no single chip explaining both charge and boost.
- **Working hypothesis carried into WS1:** eight-pin IC = switching
  charge/boost controller; `4R7` = its inductor; the 16-pin socketed part
  = possible I2C bridge or test header (hypothesis, unconfirmed). The
  AXP pair at `0x34`/`0x36` on `i2c-13` is **SBC-side silicon** — a HAT
  charger with an I2C status path would appear as a *new* device node,
  and none exists in the 0.2 census.

## Status / next

WS0 complete (0.1–0.5 done, this note = 0.6). Blocked on the operator for
**D3** (i2c-tools install approval) and the **WS1.4 macro photos** of the
eight-pin IC and the 16-pin socketed part; **D1/D2/D4** block WS2 depth.
`power-path.md` updated in the same change (0.1 drift + this citation);
the arc's other record updates land as their workstreams complete.