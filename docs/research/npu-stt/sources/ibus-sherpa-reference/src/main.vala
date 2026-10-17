/**
 * IBus Sherpa ONNX engine process entry.
 *
 * {{{
 *   ./build/ibus-setup-sherpa-onnx   # install / select model
 *   ./build/ibus-engine-sherpa-onnx
 * }}}
 *
 * Model: ''~/.config/ibus-sherpa-onnx/model''. Prefs: ''settings.ini'' (KeyFile).
 * Toggle default: Ctrl+Shift+Space.
 * ''--debug'' / ''-d'', or Preferences “Turn debugging on”: stderr +
 * ~/.cache/ibus-sherpa-onnx/ibus-sherpa-onnx.debug.log.
 */
int main(string[] args)
{
	Gst.init(ref args);
	return new IBSO.Application().run(args);
}
