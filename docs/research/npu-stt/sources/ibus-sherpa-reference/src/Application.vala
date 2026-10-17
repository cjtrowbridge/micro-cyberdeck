/*
 * Copyright (C) 2026 Alan Knowles <alan@roojs.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

namespace IBSO
{
	private static GLib.FileStream? debug_log_file = null;
	private static bool debug_log_in_progress = false;

	/** When true (''--debug''), messages go to stderr and the cache debug log. */
	public static bool debug_on = false;

	/** When true, critical log messages abort via {@link GLib.error}. */
	public static bool debug_critical_enabled = false;

	/**
	 * With ''--debug'', writes stderr and
	 * ''~/.cache/ibus-sherpa-onnx/{app_id}.debug.log''.
	 * Otherwise the normal GLib handler runs and no cache log is created.
	 *
	 * Same pattern as RooTerm / OLLMchat ''ApplicationInterface.debug_log''.
	 *
	 * @param app_id log file basename stem
	 * @param in_domain GLib log domain (empty if unset)
	 * @param level log level flags
	 * @param message log message text
	 */
	public static void debug_log(string app_id, string in_domain, GLib.LogLevelFlags level, string message)
	{
		if (debug_log_in_progress) {
			return;
		}

		if ((level & GLib.LogLevelFlags.LEVEL_CRITICAL) != 0 && debug_critical_enabled) {
			GLib.error("Critical warning: [" + in_domain + "] " + message);
		}

		if (!debug_on && !Application.opt_debug) {
			GLib.Log.default_handler(in_domain, level, message);
			return;
		}

		var timestamp = (new GLib.DateTime.now_local()).format("%H:%M:%S.%f");
		GLib.stderr.printf(timestamp + ": " + level.to_string() + " : " + in_domain + " : " + message + "\n");

		debug_log_in_progress = true;
		if (debug_log_file == null) {
			var log_dir = GLib.Path.build_filename(
				GLib.Environment.get_user_cache_dir(), "ibus-sherpa-onnx"
			);
			var log_file_path = GLib.Path.build_filename(log_dir, app_id + ".debug.log");
			if (!GLib.FileUtils.test(log_dir, GLib.FileTest.IS_DIR)) {
				GLib.DirUtils.create_with_parents(log_dir, 0755);
			}
			debug_log_file = GLib.FileStream.open(log_file_path, "w");
			if (debug_log_file == null) {
				GLib.stderr.printf("ERROR: FAILED TO OPEN DEBUG LOG FILE: Unable to open file stream\n");
				debug_log_in_progress = false;
				return;
			}
		}
		if (debug_log_file != null) {
			debug_log_file.puts(timestamp + ": " + level.to_string() + " : " + message + "\n");
			debug_log_file.flush();
		}
		debug_log_in_progress = false;
	}

	/**
	 * ''GLib.Application'' host for the Sherpa ONNX IBus engine.
	 *
	 * Per-language model pack in ''packs.ini''; prefs in ''settings.ini''.
	 * Catalog via {@link Models}. ''--debug'' or Preferences “Turn debugging on”
	 * enables stderr and the cache debug log.
	 *
	 * == Usage Examples ==
	 *
	 * === Unpackaged engine (model already installed via Preferences) ===
	 *
	 * {{{
	 *   ./build/ibus-engine-sherpa-onnx --debug
	 * }}}
	 *
	 * @since 0.3
	 */
	public class Application : GLib.Application
	{
		public static bool opt_debug = false;
		public static bool opt_debug_critical = false;
		/** True when spawned by ibus-daemon (''--ibus''); skip register_component. */
		public static bool opt_ibus = false;

		private const GLib.OptionEntry[] options = {
			{ "ibus", 'i', 0, OptionArg.NONE, ref opt_ibus,
				"Executed by ibus-daemon (packaged component path)", null },
			{ "debug", 'd', 0, OptionArg.NONE, ref opt_debug, "Enable debug output", null },
			{ "debug-critical", 0, 0, OptionArg.NONE, ref opt_debug_critical,
				"Treat critical warnings as errors", null },
			{ null }
		};

		/**
		 * Create the application and install the debug log handler.
		 */
		public Application()
		{
			GLib.Object(
				application_id: "org.roojs.ibus-sherpa-onnx",
				flags: GLib.ApplicationFlags.DEFAULT_FLAGS | GLib.ApplicationFlags.NON_UNIQUE
			);
			this.add_main_option_entries(options);
			GLib.Log.set_default_handler((dom, lvl, msg) => {
				debug_log("ibus-sherpa-onnx", dom != null ? dom : "", lvl, msg);
			});
			var open_prefs = new GLib.SimpleAction("open-preferences", null);
			open_prefs.activate.connect(() => {
				this.launch_preferences();
			});
			this.add_action(open_prefs);
		}

		/**
		 * Apply ''--debug'' flags after options are parsed.
		 */
		public override void startup()
		{
			base.startup();
			debug_on = opt_debug;
			debug_critical_enabled = opt_debug_critical;
		}

		/**
		 * Attach to ibus-daemon (model optional — toggle notifies if missing).
		 *
		 * With ''--ibus'' (packaged path): factory + {@link IBus.Bus.request_name}.
		 * Without: runtime {@link IBus.Bus.register_component} for unpackaged smoke tests.
		 */
		public override void activate()
		{
			IBus.init();

			Engine.models = new Models();
			Engine.config = Config.load();
			try {
				if (Engine.config.key_file.get_boolean("general", "debug")) {
					debug_on = true;
				}
			} catch (GLib.Error err) {
			}
			Engine.config.seed_pack_from_symlink(Engine.models);

			Engine.bind_hotkey();

			var bus = new IBus.Bus();
			if (!bus.is_connected()) {
				GLib.critical("Cannot connect to ibus-daemon");
				GLib.Process.exit(1);
			}
			bus.disconnected.connect(() => {
				this.release();
			});

			var factory = new IBus.Factory(bus.get_connection());
			foreach (var code in Engine.models.languages.get_groups()) {
				var engine_id = code == "en" ? "sherpa-onnx" : "sherpa-onnx-" + code;
				factory.add_engine(engine_id, typeof(Engine));
			}

			if (opt_ibus) {
				bus.request_name("org.roojs.IBus.SherpaOnnx", 0);
				GLib.debug("ibus mode: requested org.roojs.IBus.SherpaOnnx. Toggle=%s",
					Engine.config.key_file.get_string("general", "hotkey"));
				this.hold();
				return;
			}

			var component = new IBus.Component(
				"org.roojs.IBus.SherpaOnnx", "Sherpa ONNX", "0.1.0", "LGPL",
				"Alan Knowles <alan@roojs.com>",
				"https://github.com/roojs/ibus-sherpa-onnx",
				"", "ibus-sherpa-onnx"
			);
			Engine.models.register_engines(component);
			if (!bus.register_component(component)) {
				GLib.critical("Failed to register IBus component");
				GLib.Process.exit(1);
			}
			GLib.debug("Registered sherpa-onnx (unpackaged). Toggle=%s",
				Engine.config.key_file.get_string("general", "hotkey"));
			this.hold();
		}

		/**
		 * Launch ''ibus-setup-sherpa-onnx'' (desktop file, then libexec / PATH).
		 */
		private void launch_preferences()
		{
			try {
				var info = new GLib.DesktopAppInfo("ibus-setup-sherpa-onnx.desktop");
				if (info != null) {
					info.launch(null, null);
					return;
				}
			} catch (GLib.Error err) {
				GLib.debug("desktop launch prefs: %s", err.message);
			}
			string[] argv = { "/usr/libexec/ibus-setup-sherpa-onnx" };
			if (!GLib.FileUtils.test(argv[0], GLib.FileTest.IS_EXECUTABLE)) {
				argv[0] = "ibus-setup-sherpa-onnx";
			}
			try {
				GLib.Process.spawn_async(null, argv, null, GLib.SpawnFlags.SEARCH_PATH, null, null);
			} catch (GLib.Error err) {
				GLib.warning("launch Preferences: %s", err.message);
			}
		}
	}
}
