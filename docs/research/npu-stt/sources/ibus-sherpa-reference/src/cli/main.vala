/**
 * Sideline mic / script / WAV CLI (''-Dcli=true'').
 *
 * {{{
 *   ./build/sherpa-onnx-mic --debug
 * }}}
 */
int main(string[] args)
{
	Gst.init(ref args);
	return new IBSO.Cli.Application().run(args);
}
