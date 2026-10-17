# Acquisition and prerequisite map

Read [README](README.md) first. These are execution recipes for the approved implementation stage, not commands already run. Repairs/installations must be encoded through reviewed host bootstrap and provisioning; the tables specify requirements rather than authorize arbitrary machine changes.

## Machine boundaries

| Location | Required for | Read-only readiness | Assets |
| --- | --- | --- | --- |
| x86_64 Linux host, or Linux Docker engine reachable from Windows | ACUITY conversion only | `docker version`, `docker image inspect ubuntu-npu:v2.0.10.1`, free disk/RAM, shell availability | vendor image; SDK helpers; FP32 ONNX plus calibration; optional host ONNX/ORT Python environment |
| A733 Linux ARM64 deck | Native ASR, capture, IBus, NPU runtime | `uname -m`, `/usr/bin/python3 -V`, `ldd --version`, `free -h`, `df -h`, `ls /dev/vipcore`, `DISPLAY=:1 xdpyinfo` | CPU runtime/model; system IBus/GI; existing gpiod/Xlib; optional matched VIPLite libs |
| Windows checkout | Documentation and staging | Python/SSH/git; Docker must run Linux containers for converter | sources and archives; never run Linux ARM64 executables locally |

Probe the board Python ABI rather than assuming the Windows Python 3.12 matches it. The inventoried sherpa wheels include CPython 3.11 and 3.13 ARM64; select the actual ABI. Native tarballs avoid Python ABI constraints but still require compatible glibc/libstdc++ and matching shared libraries. Inspect `file`, `readelf -d`/`ldd` and binary `--help` on the deck. Choose CPU ARM64, not ARM32 hard-float, RKNN, Axera, CUDA, or Jetson packages. The archived [ARM32 cross-build page](sources/sherpa-docs/docs/source/onnx/install/arm-embedded-linux.rst) is a counterexample, not this board's instructions. Use [Linux native](sources/sherpa-docs/docs/source/onnx/install/linux.rst) or [ARM64 cross-build](sources/sherpa-docs/docs/source/onnx/install/aarch64-embedded-linux.rst).

## Search-free CPU asset locations

Exact URLs and digests are in [artifact inventory](artifact-inventory.json). Prefer:

- sherpa `v1.13.8` native CPU shared executable archive: `sherpa-onnx-v1.13.8-linux-aarch64-shared-cpu.tar.bz2`, 28,091,778 bytes; published SHA-256 `4e3734f82bc1379fd91f219f5869c7e9d03b7a4f7561907d8abca4849c51a789`.
- Runtime-only CPU shared library archive is smaller (12,595,068 bytes) but does not substitute for needed executables. The 357 MB static executable archive is not necessary for the first proof.
- Model baseline: `sherpa-onnx-streaming-zipformer-en-20M-2023-02-17.tar.bz2`, 127,887,156 compressed bytes. This archive contains multiple precisions: roughly 43 MB selected inference graphs does **not** mean a 43 MB download. Published archive digest was absent in the release metadata; compute and record a staged SHA-256 and source/asset identity. Model contents and compatibility remain to be checked after acquisition.
- Larger English comparison: `sherpa-onnx-streaming-zipformer-en-2023-06-26.tar.bz2`, 310,414,022 bytes. Do not assume this has the same encoder ABI/chunk size as 20M.
- Optional January Nemotron archive: 468,994,262 compressed bytes, exact URL/digest inventoried. It is not necessarily the later model/export described by OpenWhispr; compare the exact artifact, not the name alone.
- Vosk baseline URL: `https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip`. The catalog describes 40 MB; this packet has not downloaded or independently hashed it. Record actual acquired size/hash. Packaged API 0.3.45 ARM64 wheel URL/hash is inventoried.

For Python sherpa, both **sherpa-onnx==1.13.8 and sherpa-onnx-core==1.13.8** are needed. Inventory includes both. Vosk declares cffi, requests, tqdm, srt and websockets. Microphone examples additionally require sounddevice (and its PortAudio runtime) and NumPy; these are not implied by installing the recognizer wheel. A lean ALSA native capture path can avoid that wrapper stack.

For a disconnected target, use a matching Linux ARM64/Python environment to stage a wheelhouse for the exact dependency closure, then install into an ignored project environment with `--no-index --find-links`. Do not use the Windows default wheels or a host interpreter's dependency resolution as proof of board compatibility. Record a lock/manifest after resolution. PyGObject/IBus GI should come from the board distro; if using a venv, explicitly account for access to distro GI rather than blindly pip-building PyGObject or altering system Python.

## Distro requirements to encode in bootstrap

Probe each package/version/feature before installation: `alsa-utils`; `ibus`; `gir1.2-ibus-1.0`; `python3-gi`; GTK/XIM bridges as required by actual applications (`ibus-gtk`, `ibus-gtk3`, optional `ibus-gtk4`); `dbus-x11`/`dbus-run-session`; optional `python3-venv`, PortAudio and development tools for the chosen binding. Existing setup already declares dbus-x11, gpiod and Xlib; installed presence does not establish a functioning session bus or input-method environment. No PulseAudio/PipeWire server is required merely to read the known ALSA microphone.

For truly offline apt, stage the exact board-release ARM64 `.deb` dependency closure through a compatible apt environment/local repository; the packet does not contain it. The optional Vala precedent has many more GTK/GStreamer/polkit dependencies and only advertises amd64 APT packages; don't add its repository to the deck as a shortcut.

## NPU assets

The historical deck locations are `~/vip`, `~/ai-sdk`, and `~/ai-sdk/examples/vpm_run/vpm_run`. Check existence/hashes before acquiring anything. Complete SDK clone URL/revision is in the source manifest. The local SDK snapshot includes inspection sources/header, **not the .so runtime or demo binaries**.

The pinned NPU prior expects `ubuntu-npu:v2.0.10.1`, ACUITY 6.30.22, IDE 5.11.0. Vendor reference now documents `ubuntu-npu:v2.0.10.2`, so never assume equivalent graph/export behavior. Compare actual tool versions and record the selected image ID/digest. Prefer an already verified old image. Missing image may be obtained from the vendor archive if available; don't repeatedly retry an inaccessible server.

Historical archive URL: `https://netstorage.allwinnertech.com:5001/fsdownload/Mh23BhPHq/docker_images_v2.0.x.zip`. Availability/content not verified here; historically ~11 GB and ~30 GB working disk. Current vendor page: [ACUITY environment](https://docs.radxa.com/en/cubie/a7z/app-dev/npu-dev/cubie-acuity-env), reviewed for version divergence. Public mirror `khalida5/ubuntu-npu:v2.0.10` appears in the pinned prior's LeNet report; this packet does not verify its provenance or availability. `docker pull ubuntu-npu:v2.0.10.1` in the old prose is not an established public registry location.

Use `docker load -i <actual-tar-path>` after inspecting archive members. The old PowerShell `docker load < file.tar` syntax is wrong in PowerShell. Neither unpacked member names nor image tags should be invented from the outer zip filename. Do not perform the vendor page's generic Docker removal/reinstallation steps on an already configured machine.

## Transfer checklist

Copy the host checkout including this packet and current plan/journals. Populate submodules at existing gitlinks before disconnecting, or use the packet's NPU source copies for reading. A gitlink alone does not carry submodule files. Do not commit generated dependencies, compiler images, model weights, recordings or credentials. Stage them under ignored `.agentic-pipelines/`/`artifacts/` or explicitly documented host paths. Validate archive paths before extraction and preserve user-owned data. Every missing asset is an explicit prerequisite failure; compiler absence routes to CPU, CPU asset absence requires staging.
