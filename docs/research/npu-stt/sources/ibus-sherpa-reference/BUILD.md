# Building ibus-sherpa-onnx

Local Meson/Ninja build, Debian packaging (`dpkg-buildpackage`), and what GitHub Actions publishes on `v*` tags.

**Distro note:** Build against Ubuntu **25.04 (Plucky)** or similar — `libonnxruntime` and the [roojs/sherpa-onnx](https://github.com/roojs/sherpa-onnx/releases) `.deb`s target that generation. Noble (24.04) lacks those ONNX packages in the archive.

## Dependencies

### Build tools

```bash
sudo apt-get install -y \
  build-essential \
  valac \
  meson \
  ninja-build \
  pkg-config \
  debhelper \
  devscripts \
  fakeroot
```

### Libraries (dev)

From `meson.build` / `debian/control`:

```bash
sudo apt-get install -y \
  libglib2.0-dev \
  libibus-1.0-dev \
  libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  libgtk-4-dev \
  libadwaita-1-dev \
  libsoup-3.0-dev \
  libjson-glib-dev \
  libarchive-dev \
  libicu-dev \
  libgee-0.8-dev \
  libatspi2.0-dev \
  librapidfuzz-cpp-dev \
  g++ \
  libonnxruntime-dev
```

### Runtime GStreamer plugins (mic)

Needed to *run* the engine / CLI mic demos (also listed as package `Depends`):

```bash
sudo apt-get install -y \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-base \
  gstreamer1.0-pipewire \
  gstreamer1.0-pulseaudio
```

(`pipewire` **or** `pulseaudio` plugin is enough at package install time.)

Preferences installs models into `/usr/share/...` via polkit (`pkexec` / `polkitd`) — already in the package `Depends`.

### sherpa-onnx (not in the Ubuntu archive)

Download from [roojs/sherpa-onnx releases](https://github.com/roojs/sherpa-onnx/releases):

```bash
sudo dpkg -i libsherpa-onnx-c-api1_*.deb libsherpa-onnx-c-api-dev_*.deb
```

`pkg-config --exists sherpa-onnx` should succeed after that. Runtime package alone is enough to *run* a built engine; `-dev` is required to *compile*.

## Build (developer tree)

```bash
meson setup build
ninja -C build
```

Default build is the IBus engine + Preferences only. Optional sideline demos:

```bash
meson setup build -Dcli=true -Dgtk_demo=true
ninja -C build
```

Binaries under `build/`:

| Binary | Default? | Packaged? | Role |
| --- | --- | --- | --- |
| `ibus-engine-sherpa-onnx` | yes | yes | IBus engine |
| `ibus-setup-sherpa-onnx` | yes | yes | Preferences / model download |
| `sherpa-onnx-mic` | `-Dcli=true` | no | CLI mic → stdout smoke test |
| `sherpa-onnx-gtk` | `-Dgtk_demo=true` | no | GTK composer demo |

The Preferences **Debug** tab stays hidden until **Turn debugging on** (under
Mute speakers while listening). That switch also enables the cache debug log.
**Save debug recordings** on the Debug tab writes listen sessions.

Install into `/usr` (optional; for day-to-day prefer the `.deb`):

```bash
# Fresh setup uses prefix=/usr (meson.build default_options).
meson setup build
ninja -C build
sudo ninja -C build install

# Existing build dirs still on /usr/local need a reconfigure:
meson configure build -Dprefix=/usr
ninja -C build
sudo ninja -C build install
```

**Avoid `/usr/local`:** a plain `ninja install` on an old Meson tree used to land there and can shadow the packaged IBus component. Prefer `dpkg -i` for the session.

Model weights are **not** built or installed by Meson. Use Preferences, or:

```bash
./scripts/fetch-nemotron-model.sh
```

## Debian package

Packaging lives in `debian/` (`debhelper` + Meson via `debian/rules`). Source package name: **`ibus-sherpa-onnx`**.

```bash
# From the repo root, with build-deps + sherpa -dev already installed:
dpkg-buildpackage -us -uc -b
```

Produces `../ibus-sherpa-onnx_*.deb` (and related `.buildinfo` / `.changes`). Install with **`dpkg -i`**, not `apt install .../path.deb`:

```bash
sudo dpkg -i ../ibus-sherpa-onnx_*.deb
```

What the `.deb` ships (see `meson.build` `install:`):

- `/usr/libexec/ibus-engine-sherpa-onnx`
- `/usr/libexec/ibus-setup-sherpa-onnx` (prefs binary)
- `/usr/bin/ibus-setup-sherpa-onnx` (wrapper → `gtk4-launch`)
- IBus component XML, setup `.desktop`, fetch script, empty `models/` dir
  (archive digests come from the GitHub Releases API at download time)

Sideline demos are off by default and **not** in the package (`install: false`).

Uninstall:

```bash
sudo dpkg -r ibus-sherpa-onnx
```

## Fedora RPM (local)

Spec: [`rpm/ibus-sherpa-onnx.spec`](rpm/ibus-sherpa-onnx.spec). Needs `libsherpa-onnx-c-api` + `-devel` from [sherpa-onnx releases](https://github.com/roojs/sherpa-onnx/releases) first.

```bash
ver="$(scripts/sync-changelog.sh version)"
mkdir -p rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
tar --exclude=.git --exclude=build --exclude=rpmbuild --exclude=obj-* \
  -czf "rpmbuild/SOURCES/ibus-sherpa-onnx-${ver}.tar.gz" \
  --transform "s,^,ibus-sherpa-onnx-${ver}/," .
cp rpm/ibus-sherpa-onnx.spec rpmbuild/SPECS/
rpmbuild -bb --define "_topdir $PWD/rpmbuild" rpmbuild/SPECS/ibus-sherpa-onnx.spec
```

## GitHub Actions / release

Source of truth for version + notes: top-level **`CHANGELOG`**.

```bash
# after editing CHANGELOG (and committing):
scripts/release.sh    # verifies, tags vX.Y.Z, pushes → CI
```

[`.github/workflows/release.yml`](.github/workflows/release.yml) on **`v*`** tags:

1. Sync `debian/changelog` from `CHANGELOG`
2. Build `.deb` (ubuntu:25.04) and `.rpm` (fedora:44)
3. Publish GitHub Release assets; release body = `CHANGELOG` notes for that version

`workflow_dispatch` builds artifacts only (no release unless tagged).

## Quick checklist

1. Plucky-class host (or matching container) for Debian; Fedora 44-class for RPM
2. Build tools + `-dev` / `-devel` packages
3. `libsherpa-onnx-c-api` (+ devel) via `dpkg -i` / `dnf install`
4. `meson setup build && ninja -C build` **or** package build
5. Install engine package, fetch/select a model, `ibus restart`
