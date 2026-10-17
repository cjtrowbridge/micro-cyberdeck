/**
 * Sideline GTK composer + mic demo (see docs/plans/0.3 for IBus).
 *
 * Usage:
 *   sherpa-onnx-gtk [model-dir]
 *
 * Default model-dir:
 *   models/sherpa-onnx-nemotron-speech-streaming-en-0.6b-560ms-int8-2026-04-25
 *
 * Mic or Ctrl+Shift+Space starts listening; Escape or typing stops.
 */
int main(string[] args)
{
	Gst.init(ref args);

	var model_dir = "models/sherpa-onnx-nemotron-speech-streaming-en-0.6b-560ms-int8-2026-04-25";
	for (var i = 1; i < args.length; i++) {
		if (args[i].has_prefix("-")) {
			continue;
		}
		model_dir = args[i];
	}

	var app = new Adw.Application("com.roojs.sherpa-onnx-gtk", GLib.ApplicationFlags.DEFAULT_FLAGS);
	app.activate.connect(() => {
		var css = new Gtk.CssProvider();
		css.load_from_path("data/style.css");
		Gtk.StyleContext.add_provider_for_display(
			Gdk.Display.get_default(),
			css,
			Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
		);

		var engine = new IBSO.Engine();
		IBSO.Capture transcriber;
		try {
			var config = IBSO.Config.load();
			var save = false;
			try {
				save = config.key_file.get_boolean("general", "debug")
					&& config.key_file.get_boolean("general", "debug-recordings");
			} catch (GLib.Error err) {
			}
			transcriber = new IBSO.Capture(engine, config, save) {
				model_dir = model_dir
			};
			transcriber.load();
		} catch (GLib.Error err) {
			var fail = new Adw.ApplicationWindow(app) {
				title = "Sherpa ONNX",
				default_width = 420,
				default_height = 120
			};
			fail.set_content(new Gtk.Label(err.message) {
				wrap = true,
				margin_top = 16,
				margin_bottom = 16,
				margin_start = 16,
				margin_end = 16
			});
			fail.present();
			return;
		}

		var window = new Adw.ApplicationWindow(app) {
			title = "Sherpa ONNX",
			default_width = 520,
			default_height = 200
		};

		var header = new Adw.HeaderBar();
		var toolbar = new Adw.ToolbarView();
		toolbar.add_top_bar(header);

		var input = new IBSO.ComposerInput();
		input.scrolled.max_height = 160;

		/* Committed utterances + active partial (main loop only). */
		string[] committed = {""};
		string[] active = {""};

		input.mic_clicked.connect(() => {
			if (transcriber.listening) {
				return;
			}
			var existing = input.text();
			committed[0] = existing.length > 0 ? existing + " " : "";
			active[0] = "";
			transcriber.start();
			input.set_listening(true);
		});
		input.stop_requested.connect(() => {
			if (!transcriber.listening) {
				return;
			}
			transcriber.stop();
			input.set_listening(false);
		});

		var start_action = new GLib.SimpleAction("start-listen", null);
		start_action.activate.connect(() => {
			if (transcriber.listening) {
				return;
			}
			var existing = input.text();
			committed[0] = existing.length > 0 ? existing + " " : "";
			active[0] = "";
			transcriber.start();
			input.set_listening(true);
		});
		app.add_action(start_action);
		app.set_accels_for_action("app.start-listen", {"<Control><Shift>space"});

		transcriber.partial.connect((text) => {
			active[0] = text;
			input.update_entry(committed[0] + active[0]);
		});
		transcriber.endpoint.connect((text) => {
			committed[0] = committed[0] + text + " ";
			active[0] = "";
			input.update_entry(committed[0]);
		});

		window.close_request.connect(() => {
			transcriber.stop();
			return false;
		});

		var box = new Gtk.Box(Gtk.Orientation.VERTICAL, 0) {
			valign = Gtk.Align.CENTER,
			hexpand = true,
			vexpand = true
		};
		box.append(input);
		toolbar.set_content(box);
		window.set_content(toolbar);
		window.present();
		GLib.Idle.add(input.focus_idle);
	});
	return app.run(args);
}
