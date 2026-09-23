# 2026-09-23 — power-path WS1: IC identification (read-only wire reads + detect sweep)

**Plan:** [`plans/current/2026-09-23-01-02-41_power-path-trace-measure-document.md`](../plans/current/2026-09-23-01-02-41_power-path-trace-measure-document.md) (WS1)

**Operator steps, run under `sudo`** (root only: `/dev/i2c-*` is `root:i2c`; `cj` not in `i2c` group). Tooling: `tools/i2c_power_probe.sh` (one `sudo`; sections 2–3 are the reads/sweeps, section 1 is the unprivileged sysfs snapshot). Deck remained on its main power feed (HAT microUSB) for the whole run, per the plan's safety frame. Verbatim output captured at `/tmp/ws1_i2c_probe2.log` (12:00:41) — this note quotes it.

## WS1.2 — chip-ID reads on both `i2c-13` addresses (read-only, regs `0x00`–`0x06`)

`sudo i2cget -y 13 {0x34,0x36} {0x00..0x06}`, READ-ONLY. Result (verbatim, condensed):

```
--- bus 13, addr 0x34 ---
13 0x34 reg 00 = Error: Read failed
13 0x34 reg 01 = Error: Read failed
   ... (0x00–0x06 all "Read failed")
--- bus 13, addr 0x36 ---
13 0x36 reg 00 = Error: Could not set address to 0x36: Device or resource busy
   ... (0x00–0x06 all "Device or resource busy")
```

Two different errors on the two addresses, and they are **the** finding:

- **`0x36` (kernel name `axp8191`) → `Device or resource busy` (EBUSY) on every register.** EBUSY means the transaction *connected* but the device is held exclusively by the bound driver — the live `axp20x-i2c` grip. This is **expected and correct** for an in-service PMIC, and it **confirms `0x36` is the live, driver-claimed PMIC** (the raw chip-ID read is, by design, refused while the driver owns it; a live PMIC must never be raw-read against its driver or written). Its identity is therefore the DTS-provided `x-powers,axp8191` compatibility, corroborated by the driver binding + EBUSY. (We deliberately did *not* unbind the driver to get a numeric ID — that would be a write-class disturbance of the live rail path.)
- **`0x34` (kernel name `axp515`) → `Read failed` (NAK) on every register.** "Read failed" here is a wire NAK, not a tooling error — the contrast with the successful-connect-then-busy `0x36` proves the bus and the `i2cget` path work. **The `pmu@34` address does not acknowledge on the wire.**

**WS1.2 conclusion:** `i2c-13` carries **exactly one live PMIC — the AXP8191 @ `0x36`** (driver held). The second DT node, `pmu@34` (`compatible = x-powers,axp515`), produces an enumerated `i2c_client 13-0034` and an `OF` modalias, but **the silicon at `0x34` is not answering** — the chip the DT expects there is absent, unpowered, or not on the deck's live power path. It is *not* a functional companion PMIC. Reinforcing literature check (this session): a text sweep of `torvalds/linux` for `axp515` returns **zero hits** — there is **no mainline driver and no DT binding for `x-powers,axp515`**, so it could never bind on this kernel regardless of the wire result. (For contrast, `x-powers,axp8191` *is* in the built-in match table and *is* bound; `haoyu,hym8563` and `goodix,gt9271` are both present in the running kernel's driver strings.)

This **closes the September-2026 open question** carried in `pmic-axp8191.md` / `battery.md` ("axp515 may be a second PMIC with a battery ADC"): the answer is *no, as far as this board is concerned* — no driver can bind it, and the address doesn't even ACK.

## WS1.3 — detect-only sweep of all instantiated SBC buses

`sudo i2cdetect -y {9 11 12 13 14 15 20}` (quick-probe, detect-only; any new responder is recorded and **left alone** — no further transaction). The seven instantiated adapters are the full set on this board (DT enumerates 16 `twi@` controllers, but only these map to `/sys/bus/i2c/devices/`; the rest are not instantiated).

Reliable evidence (sysfs claimers, from the script's 3a) vs. the grid (3b):

| Bus | DT / role | Kernel claimers (3a, sysfs) | New unclaimed responder (3b) |
|---|---|---|---|
| i2c-9  | `2519000.twi`  | (none)   | **none** |
| i2c-11 | `251b000.twi`  | (none)   | **none** |
| i2c-12 | `251c000.twi`  | `12-0014 gt9271` **unbound** | **none** |
| i2c-13 | `7083000.twi`  | `13-0034 axp515` **unbound** (wire NAK); `13-0036 axp8191` → `axp20x-i2c` | **none** |
| i2c-14 | `7084000.twi`  | `14-0022 fusb302` → `typec_fusb302` | **none** |
| i2c-15 | `7085000.twi`  | `15-0051 hym8563` **unbound** | **none** |
| i2c-20 | `5520000.hdmi0` (HDMI CEC/DDC) | (none)  | one raw ack at **`0x30`** (see below) |

- **No new HAT charge/IC responder on any 40-pin-header-reachable SBC bus.** The empty diff vs. the WS0.2 census is the point: a HAT charge controller that exposed I2C would have appeared as a *new* address here; none did. **The HAT's eight-pin power IC is not I2C-visible to the SBC** (consistent with a non-I2C charger family — see WS0.5 candidates).
- The one raw responder, **`0x30` on `i2c-20`**, sits on the **HDMI controller's CEC/DDC bus**, not a power-path bus. It is outside this arc's scope (likely a display/CEC-related node or a stale/held line). Per the detect-only rule it is **recorded and left alone** — no follow-up transaction.
- **Grid caveat (why 3a + WS1.2 are the trustworthy evidence):** the `i2cdetect` grids mangled through the operator's paste and have a known quirk here — `i2cdetect` does not mark every bound device (`0x36`=live-AXP8191 printed `--` rather than `UU` because the driver-hold suppresses its quick-probe), and the columns shift in transit. The sysfs claimer list (3a) plus the `i2cget` EBUSY-vs-NAK split (WS1.2) are the reliable record; the sweep rows are used only to catch *new, previously-unclaimed* responders, of which there were none on the power-path buses.

## Reaffirmed (not new, cited for the record)

- **`12-0014` gt9271 (touch) — unbound** (no `driver` symlink, no `/dev/input` touch event). DT `compatible = goodix,gt9271`; the module *is* registered and its match table includes `goodix,gt9271`, so this is an unbound-not-unsupported state — consistent with the missing INT/RST GPIO mapping noted in `touch-gt9271.md`. Out of power-path scope.
- **`15-0051` hym8563 (RTC) — unbound** (no `driver` symlink; `/sys/class/rtc/` empty, no `rtc1`; no `/dev/rtc*`). DT `compatible = haoyu,hym8563`; the driver is compiled in (`CONFIG_RTC_DRV_HYM8563=y`). Out of power-path scope.
- **`13-0036` axp8191** bound to `axp20x-i2c`; its child platform nodes include `axp2101-pek.0` (the PSK power key → `/sys/class/input/event0`) and `axp8191-temp-ctrl.0` (temp-ctrl enumerated but no hwmon/temp exposed, as `pmic-axp8191.md` records).
- **`14-0022` fusb302** bound to `typec_fusb302`; its `power_supply` node is the idle `tcpm-source-psy-14-0022` (all zeros — HAT-powered config, as `usbc-power.md` records).

## Forward pointers (what WS1.2/1.3 change downstream)

- **WS1.5 (ID synthesis) — I2C branch resolved, NEGATIVE.** IC identity cannot come from I2C (no I2C HAT IC). It now rests entirely on the **1.4 macro photos (laser marking)** plus the **1.6 topology/traces**. WS0.5's candidate ranking stands, with the I2C-bridge sub-option for the 16-pin socketed part weakened (nothing on the wire to bridge to).
- **WS3 (battery visibility) — two of four paths effectively dead:**
  - (2) *root userspace I2C helper reading the charger/gauge chip* — **no such chip is I2C-addressable** (WS1.3 empty diff). No chip to read.
  - (via axp515) *the "second PMIC battery ADC"* — **killed** (WS1.2 NAK + no mainline driver).
  - Remaining on-SBC candidates: (1) *the live **AXP8191's own** battery ADC/charger-status*, **only if** the HAT cell actually reaches its battery `VIN`/ADC pin — an unproven wiring claim, which is exactly WS1.6's job and which WS2 will probe empirically. (4) *document the dead end* — the increasingly likely honest landing, per the plan.
- **WS2 (3-config matrix) is now the primary instrument** for both charge behavior and the "does the HAT feed reach the AXP8191 ADC" question, with the USB power meter (D1) and cell-present (D2) in hand.

## Evidence

- Verbatim operator output: `/tmp/ws1_i2c_probe2.log` (12:00:41) and, for the pre-hardening raw grids, `/tmp/ws1_i2c_probe.log` (11:58) on the board.
- Tooling: `tools/i2c_power_probe.sh` (sections 2 & 3).
- Literature: `github_text_search` of `torvalds/linux` — `axp515`: 0 hits; `x-powers,axp8191`, `haoyu,hym8563`, `goodix,gt9271` all present.
- DT/CONFIG (unprivileged): `touchscreen@14` → `goodix,gt9271`; `rtc@51` → `haoyu,hym8563`; `pmu@36` → `x-powers,axp8191`; `pmu@34` → `x-powers,axp515`; `CONFIG_TOUCHSCREEN_GOODIX=m` (module — loaded), `CONFIG_RTC_DRV_HYM8563=y` (built-in — hence no `/sys/module/rtc_hym8563`, expected; the `rtc-hym8563` driver dir **is** registered under `/sys/bus/i2c/drivers/`). Full PMIC config (verified 2026-09-23 against `/boot/config-6.6.98-vendor-sun60iw2`): **`# CONFIG_MFD_AXP20X_I2C is not set`** and **`# CONFIG_MFD_AXP20X_RSB is not set`** — neither mainline AXP20X path is built — while the **vendor Allwinner set is**: `CONFIG_AW_MFD_AXP2101=y`, `CONFIG_AW_MFD_AXP2101_I2C=y`, `CONFIG_AW_REGULATOR_AXP2101=y`, `CONFIG_AW_INPUT_AXP2101_PEK=y`, `CONFIG_AW_AXP515_POWER=y` (built-in power-supply driver for the absent `0x34` chip), `CONFIG_AW_AXP8191_TEMP_CTRL=y`. So the `axp20x-i2c` driver dir gripping `13-0036` is the vendor AW AXP2101-family MFD (i2c client keeps the historic name), and its platform children `axp2101-pek.0` / `axp2101-regulator.0` / `axp8191-temp-ctrl.0` map one-for-one to the `AW_INPUT_AXP2101_PEK` / `AW_REGULATOR_AXP2101` / `AW_AXP8191_TEMP_CTRL` symbols.