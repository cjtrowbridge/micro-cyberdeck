project(
  'ibus-sherpa-onnx',
  ['c', 'cpp', 'vala'],
  version: '0.3.8',
  meson_version: '>= 1.0.0',
  default_options: [
    'warning_level=1',
    'buildtype=debugoptimized',
    # Match Debian/Fedora layout; avoid accidental /usr/local installs.
    'prefix=/usr',
  ],
)

glib_dep = dependency('glib-2.0')
gobject_dep = dependency('gobject-2.0')
gio_unix_dep = dependency('gio-unix-2.0')
gst_dep = dependency('gstreamer-1.0')
gst_app_dep = dependency('gstreamer-app-1.0')
sherpa_dep = dependency('sherpa-onnx')
ibus_dep = dependency('ibus-1.0')
m_dep = meson.get_compiler('c').find_library('m')
gnome = import('gnome')

catalog_resources = gnome.compile_resources(
  'ibus-sherpa-onnx-resources',
  'resources/gresources.xml',
  source_dir: 'resources',
  c_name: 'ibus_sherpa_onnx_resources',
)

version_conf = configuration_data()
version_conf.set('VERSION', meson.project_version())
version_vala = configure_file(
  input: 'src/Version.vala.in',
  output: 'Version.vala',
  configuration: version_conf,
)

# Hand-written bindings: src/vapi/*.vapi (sherpa-onnx, icu-uc, rapidfuzz).
vapi_dir = meson.current_source_dir() / 'src' / 'vapi'
add_project_arguments(
  '--vapidir=' + vapi_dir,
  language: 'vala',
)

# Debug recordings / Browse. The Debug tab stays hidden until Preferences
# “Turn debugging on”.
debug_core_sources = [
  'src/debug/Recording.vala',
  'src/debug/Feed.vala',
  'src/debug/Replay.vala',
]
debug_ui_sources = [
  'src/debug/Dialog.vala',
  'src/debug/HistoryItem.vala',
  'src/debug/History.vala',
  'src/debug/Stars.vala',
]

# rapidfuzz-cpp is header-only (cmake package; no .pc). Tiny C ABI for Vala.
cpp = meson.get_compiler('cpp')
rapidfuzz_inc = include_directories('src/vapi')
# Confirm headers exist (dev package installed).
cpp.check_header('rapidfuzz/fuzz.hpp', required: true)
rapidfuzz_bridge = static_library(
  'rapidfuzz-bridge',
  'src/vapi/rapidfuzz_bridge.cpp',
  include_directories: rapidfuzz_inc,
  # Vala/C callers need the C ABI; hide C++ symbols.
  cpp_args: ['-fvisibility=hidden'],
  install: false,
)
rapidfuzz_dep = declare_dependency(
  link_with: rapidfuzz_bridge,
  include_directories: rapidfuzz_inc,
)

# Main product: IBus engine (entry: src/main.vala).
executable(
  'ibus-engine-sherpa-onnx',
  [
    'src/main.vala',
    'src/Application.vala',
    'src/Engine.vala',
    'src/Transcriber.vala',
    'src/Capture.vala',
    'src/Session.vala',
    'src/PcmChunk.vala',
    'src/Config.vala',
    'src/Models.vala',
    catalog_resources,
  ] + debug_core_sources,
  dependencies: [
    glib_dep,
    gobject_dep,
    gio_unix_dep,
    gst_dep,
    gst_app_dep,
    sherpa_dep,
    ibus_dep,
    m_dep,
    rapidfuzz_dep,
  ],
  vala_args: [
    '--pkg=posix',
    '--pkg=rapidfuzz',
  ],
  install: true,
  install_dir: get_option('libexecdir'),
)

install_data(
  'data/sherpa-onnx.xml',
  install_dir: get_option('datadir') / 'ibus' / 'component',
)

install_data(
  'scripts/cli-fetch-nemotron-model.sh',
  install_dir: get_option('datadir') / 'ibus-sherpa-onnx',
  install_mode: 'rwxr-xr-x',
)

libexec_abs = get_option('prefix') / get_option('libexecdir')

install_data(
  'scripts/install-model.sh',
  rename: 'ibus-sherpa-onnx-install-model',
  install_dir: get_option('libexecdir'),
  install_mode: 'rwxr-xr-x',
)

policy_conf = configuration_data()
policy_conf.set('LIBEXECDIR', libexec_abs)
configure_file(
  input: 'data/polkit/org.roojs.ibus-sherpa-onnx.policy.in',
  output: 'org.roojs.ibus-sherpa-onnx.policy',
  configuration: policy_conf,
  install: true,
  install_dir: get_option('datadir') / 'polkit-1' / 'actions',
)

# Sideline: CLI stdout mic smoke test (off by default; not packaged).
if get_option('cli')
  executable(
    'sherpa-onnx-mic',
    [
      'src/cli/main.vala',
      'src/cli/Application.vala',
      'src/Transcriber.vala',
      'src/Capture.vala',
      'src/Session.vala',
      'src/PcmChunk.vala',
      'src/EngineStub.vala',
      'src/Config.vala',
      'src/Models.vala',
      catalog_resources,
    ] + debug_core_sources,
    dependencies: [
      glib_dep,
      gobject_dep,
      gio_unix_dep,
      gst_dep,
      gst_app_dep,
      sherpa_dep,
      ibus_dep,
      m_dep,
    ],
    vala_args: [
      '--pkg=posix',
    ],
    install: false,
  )
endif

gtk_dep = dependency('gtk4')
adw_dep = dependency('libadwaita-1')
soup_dep = dependency('libsoup-3.0')
json_dep = dependency('json-glib-1.0')
archive_dep = dependency('libarchive')
# Hand-written binding: src/vapi/icu-uc.vapi + src/vapi/Icu.vala (UTF-8 API).
# Use icu-uc (not icu-i18n): uloc_* lives in libicuuc; Fedora's icu-i18n.pc
# does not pull -licuuc into the shared link line under --as-needed.
icu_dep = dependency('icu-uc')
gee_dep = dependency('gee-0.8')
atspi_dep = dependency('atspi-2')

# IBus Preferences / component <setup> (RooTerm Dialog naming: Preferences, Row*).
executable(
  'ibus-setup-sherpa-onnx',
  [
    'src/setup/main.vala',
    'src/setup/Application.vala',
    'src/Config.vala',
    'src/Models.vala',
    'src/vapi/Icu.vala',
    'src/setup/Row.vala',
    'src/setup/RowKeySelect.vala',
    'src/setup/RowSwitch.vala',
    'src/setup/RowMicText.vala',
    'src/setup/RowSelectModel.vala',
    'src/setup/RowComboLanguage.vala',
    'src/setup/ModelDownload.vala',
    'src/setup/Preferences.vala',
    version_vala,
    'src/Transcriber.vala',
    'src/Capture.vala',
    'src/Session.vala',
    'src/PcmChunk.vala',
    'src/EngineStub.vala',
    catalog_resources,
  ] + debug_core_sources + debug_ui_sources,
  dependencies: [
    glib_dep,
    gobject_dep,
    gio_unix_dep,
    gtk_dep,
    adw_dep,
    soup_dep,
    json_dep,
    archive_dep,
    icu_dep,
    gee_dep,
    ibus_dep,
    gst_dep,
    gst_app_dep,
    sherpa_dep,
    atspi_dep,
    m_dep,
    rapidfuzz_dep,
  ],
  vala_args: [
    '--pkg=posix',
    '--pkg=rapidfuzz',
  ],
  install: true,
  install_dir: get_option('libexecdir'),
)

# PATH wrapper → gtk4-launch (activation token); real binary stays in libexec.
install_data(
  'scripts/ibus-setup-sherpa-onnx',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

install_data(
  'data/ibus-setup-sherpa-onnx.desktop',
  install_dir: get_option('datadir') / 'applications',
)

# Matches GLib.Application id so GNotification banners are allowed on GNOME.
install_data(
  'data/org.roojs.ibus-sherpa-onnx.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_emptydir(get_option('datadir') / 'ibus-sherpa-onnx' / 'models')

# Sideline: GTK composer demo (off by default; not packaged).
if get_option('gtk_demo')
  executable(
    'sherpa-onnx-gtk',
    [
      'src/gtk/main.vala',
      'src/gtk/ComposerInput.vala',
      'src/gtk/ScrolledView.vala',
      'src/Transcriber.vala',
      'src/Capture.vala',
      'src/Session.vala',
      'src/PcmChunk.vala',
      'src/EngineStub.vala',
      'src/Config.vala',
      'src/Models.vala',
      catalog_resources,
    ] + debug_core_sources,
    dependencies: [
      glib_dep,
      gobject_dep,
      gio_unix_dep,
      gst_dep,
      gst_app_dep,
      sherpa_dep,
      gtk_dep,
      adw_dep,
      ibus_dep,
      m_dep,
    ],
    vala_args: [
      '--pkg=posix',
    ],
    install: false,
  )
endif
