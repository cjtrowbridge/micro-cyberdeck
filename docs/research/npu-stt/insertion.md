# Inline text, X11 session and trigger ownership

Read [STT contract](stt.md). Correct recognition is separate from correct insertion. The target is a text field in the board's :1 desktop, not a field in the browser/client that happens to display VNC. XTest sends keycodes; it does not own a Unicode composition range. Do not backspace arbitrary target text to implement general hypothesis replacement.

## Session contract before coding

Host `setup.sh` runs `gamepi-openbox` as the desktop user with DISPLAY=:1, Xvfb separately, and buttons as root. The earlier probe found no user session bus. Installed dbus-x11 is not proof that desktop applications, ibus-daemon and engine share a bus.

Create one controlled desktop session bus, then launch the desktop/input method/target apps within that environment. `dbus-run-session -- <session-command>` creates a bus for that command and its descendants and terminates it with the session; a process started later through an unrelated SSH/systemd environment does not inherit its address. Starting a fresh bus for every helper isolates them and breaks integration. Main reference: [dbus-run-session manual](https://dbus.freedesktop.org/doc/dbus-run-session.1.html).

During implementation, choose a reviewed session wrapper/dependency arrangement. It must publish only the intended user-session connection information to that user's managed services; check ownership and lifetime. It must not restart the desktop unexpectedly during data-entry tests. `DBUS_SESSION_BUS_ADDRESS`, `DISPLAY`, actual `XAUTHORITY` if configured, and the desktop user's HOME must be correct. Don't copy the old `DBUS_SESSION_BUS_ADDRESS=disabled:` environment. Run the IBus engine/ASR under the desktop user; keep root privilege only for the existing GPIO ownership boundary.

Target application environment must be set **before launch**: relevant GTK input module, Qt input module if needed, and `XMODIFIERS=@im=ibus` for XIM clients. GTK/Qt/Electron versions differ; inspect actual integrations rather than promising those variables alone suffice. Existing running apps may require a controlled restart. Verify IBus connectivity, registered engine, selected engine and a scripted preedit in each target. An IBus panel is not evidence that the app receives composition.

## Engine registration and composition API

Use the saved [engine implementation/API](sources/ibus/src/ibusengine.c), [header](sources/ibus/src/ibusengine.h), [types](sources/ibus/src/ibustypes.h), [Python GI overrides](sources/ibus/bindings/pygobject/gi/overrides/IBus.py), and [XIM bridge](sources/ibus/client/x11/main.c). Register an Engine factory/component on the correct bus; declare a stable engine identifier and activation command in the managed component configuration. Static component installation and dynamic registration have different lifetimes; select one and test process restart. `ibus engine <id>` requires a real registered engine and shared session, not merely a binary with that filename.

An IBus engine subclass receives enable/disable, focus-in/out, reset and key events. Do not load a full ASR model per field/engine object. A separate resident ASR worker plus per-context engine state keeps focus lifetimes distinct from model lifetime. Marshal worker results onto the GLib main loop rather than calling UI methods from a decoding thread.

Map backend events:

| Event | Engine action | Required guard |
| --- | --- | --- |
| Partial revision | `update_preedit_text_with_mode` on current text | active destination + utterance generation; replace, never append full hypothesis |
| Final segment | clear active preedit, `commit_text` once | monotonically ordered segment ID; ignore duplicates |
| Release final | flush backend, commit remaining final segment, clear preedit | same destination still active; no synthetic Enter |
| Focus out/disable/reset/cancel | invalidate generation, cancel worker, clear provisional text | never send late events into new context |
| Runtime failure | visible incomplete/error; clear provisional text according to policy | keep earlier final commits; no automatic duplicated replay |

Use `IBUS_ENGINE_PREEDIT_CLEAR` for focus-out behavior when cancellation must discard provisional text. `...PREEDIT_COMMIT` auto-commits on focus loss and would contradict that policy. In GI bindings these are exposed through IBus.PreeditFocusMode; inspect installed enum names. Cursor position is the API's character position, not UTF-8 byte count. Verify multibyte text and combining characters in actual apps. An app may display preedit outside the inline field even when input is working; mark that app unsupported for strict inline-interim acceptance.

A minimal scripted trial feeds `I want a` -> `I want an apple` -> final, then focus switch, Unicode, selection replacement and cancel. No microphone needed. It must not modify pre-existing text outside the composition/selected insertion target. Already committed text cannot be undone by clearing preedit. Whether future user cancel should retract earlier final segments is a product decision; default here is leave committed text and discard only provisional text, with explicit user feedback.

## Existing implementation precedent (reference, not adoption)

An upstream [IBus/sherpa prototype](sources/ibus-sherpa-reference/README.md) already connects streaming recognition to preedit/commit. Read [Engine.vala](sources/ibus-sherpa-reference/src/Engine.vala), [Transcriber.vala](sources/ibus-sherpa-reference/src/Transcriber.vala), [Capture.vala](sources/ibus-sherpa-reference/src/Capture.vala) and [Application.vala](sources/ibus-sherpa-reference/src/Application.vala) for lifecycle patterns. Its [build guide](sources/ibus-sherpa-reference/BUILD.md) brings GTK4, GStreamer, polkit and other dependencies; its published APT instructions target amd64, not this ARM64 image. No repository-wide license was detected. It is an inspected precedent, not a dependency, source-copy permission, accuracy guarantee or GPIO integration. Model-specific quiet-detection/flush behavior should not be transplanted unchanged. No need to port its whole preferences/UI stack for the small engine proof.

## Physical trigger and lifecycle

Host TL is `/dev/gpiochip1` line 2, PL2, active-low in historical evidence. Current HAT identity must be checked. `tools/hat-buttons.py` owns GPIO requests by chip; a second reader requesting PL2 while the daemon owns it can fail with EBUSY. Integrate start/stop through that owner or a deliberately replaced shared input service; don't launch a competing root monitor as the product.

Existing daemon calls a complete XTest tap for **both** physical edges. For dictation mode, consume TL and emit actual start/stop to a user-side worker over a narrowly scoped local IPC boundary. Do not leave Control_L logically pressed while injecting text, and don't invent a phantom modifier release that disrupts an independently held keyboard key. Declare mode selection so other buttons keep their established behavior and normal Control mapping can be restored. Poll/debounce on a monotonic clock; recognition latency must not block polling.

On daemon exit, disconnect, desktop restart or mic loss, end/cancel the utterance with bounded reporting. Bind IPC to the intended local user/group and validate message type/size, sequence and peer authority. Root GPIO access is not a reason to run model inference and desktop text insertion as root.
