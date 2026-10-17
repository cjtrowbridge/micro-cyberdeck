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

namespace IBSO.Setup
{
	private static GLib.FileStream? debug_log_file = null;
	private static bool debug_log_in_progress = false;

	/** When true (''--debug''), messages go to stderr and the cache debug log. */
	private static bool debug_on = false;

	/**
	 * With ''--debug'', writes stderr and
	 * ''~/.cache/ibus-sherpa-onnx/ibus-setup-sherpa-onnx.debug.log''.
	 * Otherwise the normal GLib handler runs and no cache log is created.
	 */
	private static void debug_log(string in_domain, GLib.LogLevelFlags level, string message)
	{
		if (debug_log_in_progress) {
			return;
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
			var log_file_path = GLib.Path.build_filename(log_dir, "ibus-setup-sherpa-onnx.debug.log");
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
	 * Preferences host application (component ''&lt;setup&gt;'').
	 *
	 * {{{
	 *   ./build/ibus-setup-sherpa-onnx
	 *   ./build/ibus-setup-sherpa-onnx --debug
	 * }}}
	 */
	public class Application : Adw.Application
	{
		public Config config;
		public static bool opt_debug = false;

		private const GLib.OptionEntry[] options = {
			{ "debug", 'd', 0, OptionArg.NONE, ref opt_debug, "Enable debug output", null },
			{ null }
		};

		public Application()
		{
			GLib.Object(
				application_id: "org.roojs.ibus-setup-sherpa-onnx",
				flags: GLib.ApplicationFlags.DEFAULT_FLAGS
			);
			this.add_main_option_entries(options);
			GLib.Log.set_default_handler((dom, lvl, msg) => {
				debug_log(dom != null ? dom : "", lvl, msg);
			});
		}

		public override void startup()
		{
			base.startup();
			debug_on = opt_debug;
		}

		public override void activate()
		{
			base.activate();
			if (this.config == null) {
				this.config = Config.load();
			}
			try {
				if (this.config.key_file.get_boolean("general", "debug")) {
					debug_on = true;
				}
			} catch (GLib.Error err) {
			}
			var win = this.active_window as Gtk.Window;
			if (win == null) {
				win = new Preferences(this);
			}
			win.present();
		}
	}
}
