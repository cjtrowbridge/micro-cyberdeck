# micro-cyberdeck


The micro cyberdeck is a full arm64 computer in the palm of your hand. With similar specs to a steamdeck, but significantly more local-AI capability.

**IMPORTANT:** this is a brand new board and support is limited. Parts of it are not going to easily work out of the box. It's going to take some work, and some reading, and some terminal use, and probably some vibe coding, but people are already gaming on these. I've got mine up and running to the point that it is working independently with local AI to figure out how to get the rest of the things working that aren't already working.

## Why

<img src="https://cjtrowbridge.com/projects/2026-09-09-micro-cyberdeck/kit.jpg" class="full-width-photo" alt="Current travel kit with pwnagotchi, Flipper Zero, new micro-cyberdeck (with quarter for size comparison), HackRF Portapack H4M, and uConsole">

For me, having a tiny, powerful, AI-capable device as part of my travel kit is going to be immensely helpful. This will be a huge improvement over the uConsole which is enormous and comparatively far less powerful. 

I know what you're thinking, and yes all of these devices talk to each other! Having the AI on the micro-cyberdeck be able to integrate with the other tools and operate them is a big part of the motivation for this project.

<img src="https://cjtrowbridge.com/projects/2026-09-09-micro-cyberdeck/reduced-kit.jpg" class="full-width-photo" alt="Current travel kit with pwnagotchi, Flipper Zero, HackRF Portapack H4M, and new micro-cyberdeck">

You can see in the second photo what a difference this makes, not just because it's much smaller and easier to pack but also because the micro-cyberdeck is far more powerful than the uConsole.

<img src="https://cjtrowbridge.com/projects/2026-09-09-micro-cyberdeck/micro-cyberdeck-early-draft-case.jpg" class="full-width-photo" alt="Early draft case design showing bash terminal on screen and keypads to both sides" ><br>
***This is an early draft case design with the screen showing a bash terminal window, and the keypads on both sides of the screen. All my case designs are available for free in the link at the bottom.***

## The Capabilities

This is a full arm64 computer. It should technically support most Steam games through Proton, though not all of this is realistically going to work perfectly out of the box because a lot of it is still being implemented in the drivers and software.

The arm64 CPU has eight cores, plus a separate RISC-V coprocessor, a GPU, an NPU (3 TOPS @ INT8), and up to 16gb of LPDDR5 RAM.

It has a 1.54" x 1.54" 240x240 screen, a speaker, a headphone jack, a bunch of buttons, and a battery that should theoretically last about 13.9 hours doing normal tasks.

AND, you can plug this into any USB-C docking station and essentially have a full working desktop machine with a normal monitor, keyboard, and mouse. It even supports a full desktop GPU through the PCI-E-3 port.


<img src="https://cjtrowbridge.com/projects/2026-09-09-micro-cyberdeck/micro-cyberdeck-side.jpg" class="full-width-photo" alt="Micro Cyberdeck Side View" >


## Parts List

- [New Pi](https://amzn.to/4cEsACl)
  New Pi SBC (Zero form-factor):
    - Up to 16gb DDR5 (You choose the amount you want)
    - Onboard GPU
    - NPU (3 TOPS @ INT8)
    - PCI-E-3.0
- [The Hat](https://amzn.to/4xX0VVJ)
  There are a lot of similar hats but this is the only one I have been able to find in the form factor of the Pi Zero which has a 240x240 screen, a lot of buttons, a speaker, a headphone jack, and a charge controller and battery connection. Also, it powers the pi from the top which is the only way with this pi.
- [The Battery](https://amzn.to/46zPjfv)
  Theoretically, this battery should last about 13.9 hours doing normal tasks. Your mileage may vary depending on what you are doing with the device. Playing cyberpunk or doing AI is going to cut that down significantly.
  - (11.1 wh battery / 0.8 wh rated standby consumption = 13.9 hrs battery life)
- [My favorite SD Card](https://amzn.to/3UFslAQ)
  I've stress tested a lot of SD cards and this one is the best I've found.
- I recommend running [Armbian](https://armbian.com/boards/orangepizero3w) instead of the sketchy OEM image. This is a brand new board so support is still a work in progress. Don't expect everything to be perfect yet, or ever. But my experience has been good so far.


## Hardware Status

The persistent progress tracker for every peripheral on the deck — the
HAT, the SBC silicon, and the expansion ideas. When work lands, update
the row (and the journal/plan it links to) so this table stays the
single source of truth for **what works**.

Every part has a detailed record under
[docs/hardware/](docs/hardware/) — what is known, **how we know it**
(re-probe evidence), and what it portends. **Mandate: any new research on any
part of the device — a probe, a measurement, a journal finding, a fix that
landed — must update the relevant `docs/hardware/` record, and this row if the
status moved, in the same change.** If a part has no record yet, create it (and
the row, if it has none) in the same change.

### On the deck

| Component | Status | Record | Notes |
|---|---|---|---|
| Display — ST7789 240x240 (SPI3) | **Done** | [doc](docs/hardware/display-st7789.md) | provisioned by `setup.sh`; the bridge scales the 960x960 desktop down to the 240x240 panel |
| Screen touch (gt9271) | **Not started** | [doc](docs/hardware/touch-gt9271.md) | controller on the I2C bus, driver not bound yet, no input device |
| Speaker (HAT amp) | **Done** | [doc](docs/hardware/speaker-hat-amp.md) | custom `hat-sound` engine (1-bit sigma-delta, real-time) under the `gamepi-sound` service; TTS via `espeak-ng` |
| Audio as a normal device | **In progress — paused** | [doc](docs/hardware/audio-normal-device.md) | ALSA plugin so `aplay`/`espeak-ng` work with no flags; 48 kHz verified, a 22.05 kHz stall is under classification ([plan](plans/future/2026-09-15-13-17-28_hat-alsa-device-and-tts.md)) |
| Audio input (microphone) | **Not started** | [doc](docs/hardware/audio-input-mic.md) | no input path exists: the only ALSA capture subdevice is the HDMI-Rx codec (bit-exact silence, capture confirmed dead 2026-09-23), the HAT's amp is output-only, and the last unidentified IC in the chain is a power part (WS1.5, `9813`/`2512`) not an audio input; USB mic via OTG is the declared path for the voice agent's push-to-talk (G0 in [plans/current/2026-09-23-22-57-17_voice-agent-gates.md](plans/current/2026-09-23-22-57-17_voice-agent-gates.md) — hardware proof pending) |
| Headphone jack | **Partial** | [doc](docs/hardware/headphone-jack.md) | shares the speaker's amp path — inherits its status, never tested on its own |
| Membrane key buttons (13) | **In progress** | [doc](docs/hardware/membrane-buttons.md) | 9 of 13 keys live via the userspace `gamepi-buttons` daemon (gpiod→X11 XTest on `:1`); the vendor kernel has no button-driver path (`gpio-keys`/`uinput`/overlay all absent), 3 keys parked pending a physical re-test, the 13th suspected fused to the PMIC power-key |
| Power button (PMIC key) | **Done** | [doc](docs/hardware/power-button.md) | the `axp8191` PEK (power key) is a kernel input device → `/dev/input/event0` |
| Battery (11.1 Wh LiPo) | **Partial** | [doc](docs/hardware/battery.md) | charge circuitry identified (WS1.5) as a **switching buck-boost controller** (SOP-8, marking `9813`/`2512`, 4.7 µH inductor — TP4056-class linear charger ruled out), likely a SY89813-class part (datasheet unconfirmed); but battery voltage / charge state is **not** exposed to Linux (no fuel gauge, no `power_supply` battery node, HAT IC not I2C-visible) — without an ADC readout we can't tell charging from draining, or estimate the ~14 h life in practice |
| Power path (USB-C / HAT microUSB / battery) | **Partial** | [doc](docs/hardware/power-path.md) | device runs on the HAT's microUSB today, which feeds the PMIC's charge path; the HAT's eight-pin power IC is now **marking-confirmed** (`9813`/`2512`, SOP-8, switching buck-boost) and **not I2C-visible** to the SBC (WS1.3/1.5); the SBC's own USB-C port (fusb302 PD chip) is a separate input whose interaction with the HAT path is **untested** (WS2 matrix pending); the PD PSU stub reports 0 |
| PMIC (AXP8191 rails) | **Partial** | [doc](docs/hardware/pmic-axp8191.md) | ~40 regulator rails power the board (SoC, NPU, UFS/PCIe, HDMI, fan, i2c…); the driver exposes names only — no live voltages; the companion `axp515` chip on the same bus is probed but inert |
| SoC temperature (CPU/DDR/GPU/NPU) | **Done** | [doc](docs/hardware/soc-thermal.md) | the SoC's own thermal sensors via `thermal_zone*`; distinct from the PMIC's internal `temp-ctrl`, which is present but not exposed |
| Fan | **Partial** | [doc](docs/hardware/fan.md) | a single 40 mm Noctua (12 V) in the case is wired into the SBC fan header and kernel-driven by `pwm-fan` (`cooling_device9`, ladder `0/5/102/170/255`; state 0 = hard off, state 1 ≈2% stall); the root cause of the flapping + heat is running that 12 V fan on the port's 5 V-class rail — fix: a 5 V 40 mm PWM fan (ordered 2026-09-19), then optionally a DTB overlay to floor the ladder + trip hysteresis |
| RTC (hym8563) | **Not started** | [doc](docs/hardware/rtc-hym8563.md) | on the I2C bus, unbound; NTP stands in for now |
| USB-C power negotiation (fusb302/TCPM) | **Automatic** | [doc](docs/hardware/usbc-power.md) | kernel USB-source stub only; no user-visible feature |
| NPU (3 TOPS INT8) | **In progress** | [doc](docs/hardware/npu.md) | vendor kernel module `vipcore` is bound and `/dev/vipcore` (199,0) exists; the DTB node is `status = "okay"`; vendor VIPLite runtime (2.0.3.1-AW-2024-08-16) installed from the public Allwinner SDK and **G1 passed 2026-09-19** (demo NBG through `vpm_run`, `ret=0`, `cid=0x1000003b`, smoke test 7/7). Still to procure: the ACUITY compiler (ONNX→NBG) — the SDK's own sample models target a different NPID. Prior work pinned at `third_party/a733_npu_driver` — Ollama has no NPU backend: hybrid split (CPU for Qwen-class, NPU for vision + tiny LLMs) is the plan |
| HDMI audio card | **Unused (by design)** | [doc](docs/hardware/hdmi-audio.md) | there for an external monitor's sound; deck audio is the HAT amp |

### Expansion ideas

All of these are possible today by plugging an external device into the
ports; the goal is to fit them into the micro form-factor. These carry no
per-device records yet — when one is planned, create its
[docs/hardware](docs/hardware/) file and link it here.

| Idea | Status | Notes |
|---|---|---|
| PCI-3 SSD or AI accelerator (e.g. LLM8850) | **Idea** | no NVMe-capable hat in this form factor yet |
| Zero-form USB + ethernet HAT | **Idea** | existing hats need a micro-USB power lead this board can't take |
| Software-defined radio (CaribouLite-style) | **Idea** | no longer available in a usable form factor |
| Meshtastic | **Idea** | no zero-form HAT found yet |


## Recursive Self-Improvement Out Of The Box

<img src="https://cjtrowbridge.com/projects/2026-09-09-micro-cyberdeck/vscode.jpg" class="full-width-image" alt="VSCode is running!" >  

Because it’s now so easy to run models locally, and because this device has a powerful NPU built in, it can take over the work on itself essentially as soon as you flash Armbian and install your preferred agentic harness.

I started out by installing a desktop environment which was a little complicated, and then installing vs code so I could have it work on itself [in the public repo](https://github.com/cjtrowbridge/micro-cyberdeck) so you can see what it’s doing. Also because I want to figure out how to get the development environment to use higher resolutions than the screen natively supports on VNC or USB while still having it be somewhat legible on the screen. 

My plan is to use my [ebe pipeline](https://github.com/cjtrowbridge/ebe-boilerplate) to build an interactive game-engine style interface that plays nice with the buttons and AI tools. The voice half of that has a design of record now: the **voice agent** — a push-to-talk local agent (hold the left bumper → whisper-medium transcribes, resident qwen3.5:4b answers out loud) running in a full-screen Ebitengine app on the 960×960 desktop. Its design and gate plan (this is the item behind the "voice-interactive local agent software" unresolved issue below) live at [projects/voice-agent/](projects/voice-agent/).

Here you can see she has vs code open and she is working on herself. Hopefully she will be able to figure out these unresolved issues without much help from me. 💅

### Known Unresolved Issues

(peripheral-level status is tracked in [Hardware Status](#hardware-status) above — only the non-peripheral items remain here)

- Testing [case designs](https://github.com/cjtrowbridge/vibe-modeling/tree/main/output/micro_cyberdeck_case) with better thermal management because the little fan struggles to keep up by itself and I want to keep the battery insulated from the SBC's heat
- Testing [manufacturer's recommended ROMs](https://spotpear.com/wiki/Raspberry-Pi-Game-1.54inch-LCD-touchscreen-display-ST7789.html)
- Getting steam/proton working
- Building voice-interactive local agent software
  - Integrating with peripherals like Flipper Zero and pwnagotchi
  - Design of record: [projects/voice-agent/](projects/voice-agent/) (push-to-talk runtime — whisper-medium + qwen3.5:4b resident, left-bumper PTT; gates G0–G3 in its [design](projects/voice-agent/docs/design.md); G0 decided — USB mic via OTG, hardware proof pending; **G1 in progress**)

## Setup

One entrypoint provisions and heals the whole machine — display stack, services,
VNC, the web dashboard + its metrics API, the browser SSH/VNC bridges, hostname,
SPI overlay — as a **re-entrant converge**:

```bash
sudo bash setup.sh
```

- Safe to run any time, on any board state: it converges to the desired
  configuration and writes only what differs. On a healthy board a re-run
  changes nothing, re-verifies the live system, and exits 0.
- Audit-only mode (writes nothing): `sudo bash setup.sh --plan`.
  Unattended/agent mode: `sudo bash setup.sh --yes --reboot --json`
  (reboots only when an overlay change actually requires it). Flags, phases,
  exit codes, and the final `RESULT:` line are documented in
  [docs/setup.md](docs/setup.md).
- VS Code: the **GamePi: set up the machine** task / play action
  (`.vscode/`) runs the same command in an integrated terminal, so the sudo
  prompt and the one reboot question stay visible and answerable.
- The deck is **web-first**: from any device on the LAN you open the web
  dashboard and get browser-based **SSH** and **VNC** with no client software
  to install. Apache on `:80` is the single browser origin — every surface
  below is reached from its URLs:
  - **Dashboard** `http://<board>/` — a self-contained system page (live
    temperatures, CPU, RAM/swap, disk, fan) fed by the Go API through Apache
    `/api/`, plus an **Access** card carrying the two links below.
  - **Desktop (VNC)** `http://<board>/vnc/vnc.html` — the vendored noVNC
    client; its WebSocket tunnels over Apache (`mod_proxy_wstunnel`) to
    `websockify` on `:6080`, which bridges to the desktop's `x11vnc` on
    `:5900`. Auth is the existing VNC password.
  - **Terminal (SSH)** `http://<board>/shell/` — ShellInABox on `:4200`:
    a real SSH session (PAM login, same credentials as `ssh`) in the
    browser, rendered **dark** to match the dashboard (a managed theme is
    appended over the binary's stock light css — `docs/setup.md`,
    "Browser access").
  - Both bridge listeners bind `0.0.0.0` by direction on a trusted-LAN board
    and are reached same-origin through Apache; raw VNC never crosses the
    wire (the `6080` leg is the browser's WebSocket — `websockify` dials
    `x11vnc` on loopback). Auth/TLS and action endpoints remain deferred
    (plan 2026-09-19-23-23-36 §3.1).
- Ports (all provisioned + verified by `setup.sh` — see the matrix in
  [docs/setup.md](docs/setup.md)):

  | Port | Bind | Surface | Notes |
  |---|---|---|---|
  | `22` | `0.0.0.0` | OpenSSH | native `ssh` (same PAM credentials as the browser Terminal) |
  | `80` | `0.0.0.0` | Apache — browser origin | dashboard, `/api/`, `/vnc/` client, `/shell/` |
  | `8080` | `127.0.0.1` | Go metrics API | loopback-only by design, reached via `/api/` |
  | `4200` | `0.0.0.0` | ShellInABox | browser SSH (PAM), dark-themed; also at `/shell/` |
  | `5900` | `0.0.0.0` | x11vnc (RFB) | the 960x960 `:1` desktop; the raw target behind noVNC |
  | `6080` | `0.0.0.0` | websockify (WS→RFB) | bridges `/vnc/websockify` to `x11vnc`; VNC password |
- The display is a single 960x960 `:1` desktop by design (the ST7789 panel
  is 4:1 down-scaled from it; VNC serves the 960 view on 5900). The one-time
  480->960 migration is complete and its rescue scripts retired (2026-09-19);
  a stuck display is a fresh-flash + `setup.sh` case like any other.

### More Resources

- This hat is essentially a clone of [this board](https://spotpear.com/wiki/Raspberry-Pi-Game-1.54inch-LCD-touchscreen-display-ST7789.html) but this is but this clone has been modified with extra features like speakers, aux cord plug, and a battery charge controller which for me makes this clone much better than anything else on the market in this form factor.
- A lot of the drivers and details related to the original board are super helpful with getting it set up and running.
- Also, here is [a repo](https://github.com/cjtrowbridge/micro-cyberdeck) containing my canonical setup script which includes all the step's I've taken to get it up and running. I will continue to update this as I get more parts of it working, with the goal of eventually geting Cyberpunk 2077 to run on it.
- Here are all [my case designs](https://github.com/cjtrowbridge/vibe-modeling/tree/main/output/micro_cyberdeck_case) for this micro-cyberdeck.
