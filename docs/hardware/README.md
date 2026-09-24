# Device Hardware Records

One evidence-first file per part of the deck — every row of the
**[Hardware Status](../../README.md#hardware-status)** "On the deck" table in
the README links to its record here. The README table is the at-a-glance
tracker; these files carry the detail: **what we know**, **how we know it**
(concrete evidence: re-probe commands, `setup.sh` verify rows, journal/plan
links, binary md5s), and **what it portends** (implications, next steps, what
is blocked on each part).

## Format (every file follows this)

1. **Status** — mirrored from the README table. Controlled vocabulary:
   `Done` · `In progress — paused` · `Partial` · `Not started` · `Automatic` ·
   `Unused (by design)` · `Idea`.
2. **What it is** — the part and its role in the deck.
3. **What we know** — claims, each backed by the evidence section.
4. **How we know** — re-probe commands an agent can run bare (no sudo where
   noted), which `setup.sh` verify row covers it, journal/plan citations,
   binary identity (md5) where a firmware-adjacent binary is involved.
   Kernel probe logs (`dmesg` / `/var/log/kern.log`) need the operator.
5. **What it portends** — implications, next steps, gating, open questions.

## Standing rule

**Any new research on any part of the device** — a probe, a measurement, a
journal finding, a fix that landed — **must update the relevant file here in
the same change**, and flip the README row if the status moved. If the part is
not documented yet (e.g. an expansion idea turning into a real plan, or a newly
discovered peripheral), create its doc and add the README row in the same
change.

## HAT underside photos (2026-09-17 and 2026-09-24)

**2026-09-17 set:**
- [Full underside view](images/PXL_20260917_070632106.jpg)
- [Lower section, including speaker and headphone jack](images/PXL_20260917_070641017.jpg)
- [Battery connector and upper power section](images/PXL_20260917_070648424.jpg)

**2026-09-24 macro set (WS1.4, operator upload) — the eight-pin power IC's
marking now reads crisply:**
- [Eight-pin IC + `4R7` inductor + `NS8002` neighbor (decisive)](images/PXL_20260924_035455686.jpg)
- [Angled overview (16-pin part, speaker, fan, battery JST)](images/PXL_20260924_035505977.jpg)
- plus `PXL_20260924_035513411.jpg`, `PXL_20260924_035521781.jpg`,
  `PXL_20260924_035531972.jpg`, `PXL_20260924_035538807.jpg`

These are original-resolution operator photos. The `NS8002` marking
identifies the speaker amplifier (see [speaker-hat-amp.md](speaker-hat-amp.md)).
**Updated 2026-09-24 (WS1.5):** the eight-pin IC next to the `4R7` inductor
now reads **`9813` / `2512`** (date code week 12 / 2025) — a **switching
buck-boost charge controller, SOP-8**, likely a SY89813-class part
(datasheet-unconfirmed), and **not I2C-visible** to the SBC; its exact
part number and the trace to the SBC PMIC remain open (see
[battery.md](battery.md) and [power-path.md](power-path.md)). The larger IC
at the opposite corner is visible but its marking is still not reliably
legible, so no part number has been assigned.

## Index (On the deck)

| Part | Record | Status |
|---|---|---|
| Display — ST7789 240×240 (SPI3) | [display-st7789.md](display-st7789.md) | Done |
| Screen touch (gt9271) | [touch-gt9271.md](touch-gt9271.md) | Not started |
| Speaker (HAT amp) | [speaker-hat-amp.md](speaker-hat-amp.md) | Done |
| Audio as a normal device | [audio-normal-device.md](audio-normal-device.md) | In progress — paused |
| Audio input (microphone) | [audio-input-mic.md](audio-input-mic.md) | Not started |
| Headphone jack | [headphone-jack.md](headphone-jack.md) | Partial |
| Membrane key buttons (13) | [membrane-buttons.md](membrane-buttons.md) | In progress |
| Power button (PMIC key) | [power-button.md](power-button.md) | Done |
| Battery (11.1 Wh LiPo) | [battery.md](battery.md) | Partial |
| Power path (USB-C / HAT microUSB / battery) | [power-path.md](power-path.md) | Partial |
| PMIC (AXP8191 rails) | [pmic-axp8191.md](pmic-axp8191.md) | Partial |
| SoC temperature (CPU/DDR/GPU/NPU) | [soc-thermal.md](soc-thermal.md) | Done |
| Fan | [fan.md](fan.md) | Partial |
| RTC (hym8563) | [rtc-hym8563.md](rtc-hym8563.md) | Not started |
| USB-C power negotiation (fusb302/TCPM) | [usbc-power.md](usbc-power.md) | Automatic |
| NPU (3 TOPS INT8) | [npu.md](npu.md) | In progress — VIPLite runtime installed, G1 green 2026-09-19; ACUITY (ONNX→NBG) still to procure |
| HDMI audio card | [hdmi-audio.md](hdmi-audio.md) | Unused (by design) |

**Expansion ideas** (the other table in the README) have no records yet — they
carry no known device state. When one is planned, create its doc and link it
from the expansion row.

**Verification baseline:** all live evidence below was probed on the board
during the September 2026 sessions (the journals dated 2026-09-14 through
2026-09-16 are the primary written record). Re-run the per-file re-probe
commands to confirm before relying on a claim.
