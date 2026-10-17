/**
 * IBus Sherpa ONNX preferences (GNOME IME Preferences / component setup).
 *
 * {{{
 *   ./build/ibus-setup-sherpa-onnx
 * }}}
 */
int main(string[] args)
{
	Gst.init(ref args);
	IBus.init();
	return new IBSO.Setup.Application().run(args);
}
