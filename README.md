# micro-cyberdeck


The micro cyberdeck is a full arm64 computer in the palm of your hand. With similar specs to a steamdeck, but significantly more local-AI capability.

**IMPORTANT:** this is a brand new board and support is limited. Parts of it are not going to easily work out of the box. It's going to take some work, and some reading, and some terminal use, and probably some vibe coding, but people are already gaming on these. I've got mine up and running to the point that it is working independently with local AI to figure out how to get the rest of the things working that aren't already working.

## Why

<img src="https://cjtrowbrdige.com/projects/2026-09-09-micro-cyberdeck/kit.jpg" class="full-width-photo" alt="Current travel kit with pwnagotchi, Flipper Zero, new micro-cyberdeck (with quarter for size comparison), HackRF Portapack H4M, and uConsole">

For me, having a tiny, powerful, AI-capable device as part of my travel kit is going to be immensely helpful. This will be a huge improvement over the uConsole which is enormous and comparatively far less powerful. 

I know what you're thinking, and yes all of these devices talk to each other! Having the AI on the micro-cyberdeck be able to integrate with the other tools and operate them is a big part of the motivation for this project.

<img src="https://cjtrowbrdige.com/projects/2026-09-09-micro-cyberdeck/reduced-kit.jpg" class="full-width-photo" alt="Current travel kit with pwnagotchi, Flipper Zero, HackRF Portapack H4M, and new micro-cyberdeck">

You can see in the second photo what a difference this makes, not just because it's much smaller and easier to pack but also because the micro-cyberdeck is far more powerful than the uConsole.

<img src="https://cjtrowbrdige.com/projects/2026-09-09-micro-cyberdeck/micro-cyberdeck-early-draft-case.jpg" class="full-width-photo" alt="Early draft case design showing bash terminal on screen and keypads to both sides" ><br>
***This is an early draft case design with the screen showing a bash terminal window, and the keypads on both sides of the screen. All my case designs are available for free in the link at the bottom.***

## The Capabilities

This is a full arm64 computer. It should technically support most Steam games through Proton, though not all of this is realistically going to work perfectly out of the box because a lot of it is still being implemented in the drivers and software.

The arm64 CPU has eight cores, plus a separate RISC-V coprocessor, a GPU, an NPU (3 TOPS @ INT8), and up to 16gb of LPDDR5 RAM.

It has a 1.54" x 1.54" 240x240 screen, a speaker, a headphone jack, a bunch of buttons, and a battery that should theoretically last about 13.9 hours doing normal tasks.

AND, you can plug this into any USB-C docking station and essentially have a full working desktop machine with a normal monitor, keyboard, and mouse. It even supports a full desktop GPU through the PCI-E-3 port.


<img src="https://cjtrowbrdige.com/projects/2026-09-09-micro-cyberdeck/micro-cyberdeck-side.jpg" class="full-width-photo" alt="Micro Cyberdeck Side View" >


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


## Recursive Self-Improvement Out Of The Box

<img src="https://cjtrowbrdige.com/projects/2026-09-09-micro-cyberdeck/vscode.jpg" class="full-width-image" alt="VSCode is running!" >  

Because it’s now so easy to run models locally, and because this device has a powerful NPU built in, it can take over the work on itself essentially as soon as you flash Armbian and install your preferred agentic harness.

I started out by installing a desktop environment which was a little complicated, and then installing vs code so I could have it work on itself [in the public repo](https://github.com/cjtrowbridge/micro-cyberdeck) so you can see what it’s doing. Also because I want to figure out how to get the development environment to use higher resolutions than the screen natively supports on VNC or USB while still having it be somewhat legible on the screen. 

My plan is to use my [ebe pipeline](https://github.com/cjtrowbridge/ebe-boilerplate) to build an interactive game-engine style interface that plays nice with the buttons and AI tools.

Here you can see she has vs code open and she is working on herself. Hopefully she will be able to figure out these unresolved issues without much help from me. 💅

### Known Unresolved Issues
- I am still working on getting the built-in speaker and keypads working
- Testing [case designs](https://github.com/cjtrowbridge/vibe-modeling/tree/main/output/micro_cyberdeck_case) with better thermal management because the little fan struggles to keep up by itself and I want to keep the battery insulated from the SBC's heat
- Testing [manufacturer's recommended ROMs](https://spotpear.com/wiki/Raspberry-Pi-Game-1.54inch-LCD-touchscreen-display-ST7789.html)
- Getting steam/proton working
- Building voice-interactive local agent software
  - Integrating with peripherals like Flipper Zero and pwnagotchi
- It seems like there is a way to get the SBC to be aware of the battery's charge status but I haven't figured that out yet

## Setup

One entrypoint provisions and heals the whole machine — display stack, services,
VNC, hostname, SPI overlay — as a **re-entrant converge**:

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
- If the board is stuck in one of the two known broken 480-mode states, the
  one-time rescue scripts `apply-960.sh` / `revert-480.sh` cover it; anything
  else is a fresh flash + `setup.sh`.

### Future Opportunities

All of these things are currently possible by simply plugging external devices into the ports, but I'd like to fit these capabilites into the micro form-factor.

#### PCI-3 Expansion

I think the biggest long-term opportunity is to take advantage of the PCI-3 port by adding some kind of more powerful SSD or AI Accelerator (Like the LLM8850 which is out of stock everywhere).

The big problem with both of these options is that there isn't any nvme hat that fits this board at this point.

#### Peripheral Ports

There are [some hats](https://www.crowdsupply.com/cariboulabs/cariboulite-rpi-hat) for Pi Zero that give you a few usb ports and an ethernet port, but they all need a micro usb connection to the pi which is not possible with this pi. It seems unlikely that this will ever be resolved since this is a small batch SBC, but it would be cool to see that someday.

#### Software-Defined Radio

It would be awesome to find something like a [CaribouLite](https://www.crowdsupply.com/cariboulabs/cariboulite-rpi-hat) in the zero form factor, but these are also no longer available.

#### Meshtastic

I haven't been able to find a meshtastic hat for the zero form factor, but it would be cool to add one someday.


### More Resources

- This hat is essentially a clone of [this board](https://spotpear.com/wiki/Raspberry-Pi-Game-1.54inch-LCD-touchscreen-display-ST7789.html) but this is but this clone has been modified with extra features like speakers, aux cord plug, and a battery charge controller which for me makes this clone much better than anything else on the market in this form factor.
- A lot of the drivers and details related to the original board are super helpful with getting it set up and running.
- Also, here is [a repo](https://github.com/cjtrowbridge/micro-cyberdeck) containing my canonical setup script which includes all the step's I've taken to get it up and running. I will continue to update this as I get more parts of it working, with the goal of eventually geting Cyberpunk 2077 to run on it.
- Here are all [my case designs](https://github.com/cjtrowbridge/vibe-modeling/tree/main/output/micro_cyberdeck_case) for this micro-cyberdeck.
