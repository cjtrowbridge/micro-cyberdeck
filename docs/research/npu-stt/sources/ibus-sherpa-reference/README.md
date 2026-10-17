# ibus-sherpa-onnx

Speech-to-text for the Linux desktop: talk into your microphone and text
appears in the app you are using (IBus + sherpa-onnx / Nemotron, fully
offline). In recent testing we are seeing **near–100% dictation** —
essentially word-perfect on clean speech — after switching utterance
splits to our own audio-stream quiet detector.

**Multilingual:** English plus many other languages (Spanish, French, German,
Japanese, Chinese, and more) via preferences.

## Demo

Dictation in action:

<video src="https://github.com/user-attachments/assets/3db24932-0a0c-4d85-9209-0d7611687524" controls width="100%"></video>

Install and first-time setup:

<video src="https://github.com/user-attachments/assets/02104d53-0d63-4204-8baa-2e6bfe6a7c2f" controls width="100%"></video>

## Install

Packages are published from [roojs/repos](https://github.com/roojs/repos)
at **https://roojs.github.io/repos/**. The speech library
(`libsherpa-onnx-c-api`) is in the same repository and is pulled in as a
dependency.

### APT (Debian / Ubuntu)

Debian 13 (`trixie`). Ubuntu 25.04 (`plucky`), 25.10 (`questing`), 26.04
(`resolute`). Architecture: `amd64`.

Add the signing key and the sources file, replacing `@suite@` with your
suite from `lsb_release -cs`:

```bash
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://roojs.github.io/repos/key.gpg \
  | sudo gpg --dearmor -o /etc/apt/keyrings/roojs.gpg

curl -fsSL https://roojs.github.io/repos/sources \
  | sed "s/@suite@/$(lsb_release -cs)/" \
  | sudo tee /etc/apt/sources.list.d/roojs.sources

sudo apt update
sudo apt install ibus-sherpa-onnx
```

### DNF (Fedora)

Fedora 44.

```bash
sudo curl -fsSL https://roojs.github.io/repos/key.gpg \
  -o /etc/pki/rpm-gpg/RPM-GPG-KEY-roojs
sudo curl -fsSL https://roojs.github.io/repos/repo \
  -o /etc/yum.repos.d/roojs.repo
sudo dnf makecache
sudo dnf install ibus-sherpa-onnx
```

More details (supported suites, package list, and repository layout) are on
the [roojs package repositories](https://roojs.github.io/repos/) page.

To build from source instead, see [BUILD.md](BUILD.md).

## First-time setup

```bash
ibus-setup-sherpa-onnx
```

A window opens. Choose a **language**, pick a model, and close the window.
It downloads about **440 MB**, may ask for your password, turns Sherpa on as
your input method, and restarts IBus.

## Use it

1. Click in any text field (browser, editor, chat, ...).
2. Press **Ctrl+Shift+Space** to start listening.
3. Speak.
4. Press **Ctrl+Shift+Space** again (or start typing) to stop.

**Super+Space** still switches between input methods if you have more than one.

## Uninstall

**Ubuntu / Debian:**

```bash
sudo apt remove ibus-sherpa-onnx
ibus restart
```

**Fedora:**

```bash
sudo dnf remove ibus-sherpa-onnx
ibus restart
```

(`ibus restart` clears the old engine from your current login session.)

## Notes

- Speech models are **not** inside the packages; the setup window downloads them.
- Settings: `~/.config/ibus-sherpa-onnx/settings.ini`.

## Artificial Intelligence Usage

This project was developed with the assistance of artificial intelligence.

- Product design and code design were done by the author
- AI’s main role was writing implementation for review
- Most of the coding was performed by AI
- Code was then reviewed, revised, and approved by the author
- Every line of application code was reviewed and approved by the author
- Limited exceptions apply mainly to the build system
