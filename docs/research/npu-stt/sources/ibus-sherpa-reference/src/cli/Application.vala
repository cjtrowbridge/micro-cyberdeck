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

namespace IBSO.Cli
{
	private static GLib.FileStream? debug_log_file = null;
	private static bool debug_log_in_progress = false;

	/** When true (''--debug''), messages go to stderr and the cache debug log. */
	public static bool debug_on = false;

	/** When true, critical log messages abort via {@link GLib.error}. */
	public static bool debug_critical_enabled = false;

	/**
	 * With ''--debug'', writes stderr and
	 * ''~/.cache/ibus-sherpa-onnx/sherpa-onnx-mic.debug.log''.
	 * Otherwise the normal GLib handler runs and no cache log is created.
	 *
	 * Same pattern as {@link IBSO.debug_log}.
	 */
	private static void debug_log(string in_domain, GLib.LogLevelFlags level, string message)
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
			var log_file_path = GLib.Path.build_filename(log_dir, "sherpa-onnx-mic.debug.log");
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
	 * Sideline mic / script / WAV CLI (''sherpa-onnx-mic'', ''-Dcli=true'').
	 *
	 * {{{
	 *   ./build/sherpa-onnx-mic --debug
	 *   ./build/sherpa-onnx-mic --debug --wav FILE [--from SEC] [--to SEC]
	 * }}}
	 */
	public class Application : GLib.Application
	{
		public static bool opt_debug = false;
		public static bool opt_debug_critical = false;
		public static bool opt_stats = false;
		public static bool opt_fast_transcribe = false;
		public static string opt_script = "";
		public static string opt_wav = "";
		public static string opt_language = "";
		public static double opt_from = 0.0;
		public static double opt_to = -1.0;
		public static int opt_chunk_ms = 200;
		public static string[] remaining = {};

		private const GLib.OptionEntry[] options = {
			{ "debug", 'd', 0, OptionArg.NONE, ref opt_debug, "Enable debug output", null },
			{ "debug-critical", 0, 0, OptionArg.NONE, ref opt_debug_critical,
				"Treat critical warnings as errors", null },
			{ "stats", 0, 0, OptionArg.NONE, ref opt_stats, "Print segment stats", null },
			{ "script", 0, 0, OptionArg.FILENAME, ref opt_script,
				"Scripted accuracy check (one utterance per line)", "FILE" },
			{ "wav", 0, 0, OptionArg.FILENAME, ref opt_wav,
				"Feed a mono 16 kHz S16LE debug recording", "FILE" },
			{ "from", 0, 0, OptionArg.DOUBLE, ref opt_from, "WAV start time (seconds)", "SEC" },
			{ "to", 0, 0, OptionArg.DOUBLE, ref opt_to, "WAV end time (seconds, default EOF)", "SEC" },
			{ "chunk-ms", 0, 0, OptionArg.INT, ref opt_chunk_ms, "WAV feed chunk size (ms)", "N" },
			{ "fast-transcribe", 0, 0, OptionArg.NONE, ref opt_fast_transcribe,
				"Feed WAV as fast as possible (not realtime)", null },
			{ "language", 0, 0, OptionArg.STRING, ref opt_language, "Catalog language code", "CODE" },
			{ GLib.OPTION_REMAINING, 0, 0, OptionArg.FILENAME_ARRAY, ref remaining,
				null, "[MODEL-DIR]" },
			{ null }
		};

		public Application()
		{
			GLib.Object(
				application_id: "org.roojs.sherpa-onnx-mic",
				flags: GLib.ApplicationFlags.DEFAULT_FLAGS
					| GLib.ApplicationFlags.NON_UNIQUE
					| GLib.ApplicationFlags.HANDLES_COMMAND_LINE
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
			debug_critical_enabled = opt_debug_critical;
		}

		public override int command_line(GLib.ApplicationCommandLine command_line)
		{
			if (opt_script != "" && opt_wav != "") {
				GLib.critical("Use only one of --script or --wav");
				return 1;
			}

			var model_dir = "";
			if (remaining != null && remaining.length > 0) {
				model_dir = remaining[0];
			}
			if (model_dir == "") {
				var link = GLib.Path.build_filename(GLib.Environment.get_user_config_dir(), "ibus-sherpa-onnx", "model");
				if (GLib.FileUtils.test(link, GLib.FileTest.IS_SYMLINK) || GLib.FileUtils.test(link, GLib.FileTest.IS_DIR)) {
					model_dir = link;
					try {
						if (GLib.FileUtils.test(link, GLib.FileTest.IS_SYMLINK)) {
							model_dir = GLib.FileUtils.read_link(link);
						}
					} catch (GLib.FileError err) {
					}
				}
			}
			if (model_dir == "") {
				model_dir = "models/sherpa-onnx-nemotron-speech-streaming-en-0.6b-560ms-int8-2026-04-25";
			}

			var language = opt_language;
			if (language == "") {
				try {
					var conf = GLib.Path.build_filename(GLib.Environment.get_user_config_dir(), "ibus-sherpa-onnx", "settings.ini");
					var key_file = new GLib.KeyFile();
					key_file.load_from_file(conf, GLib.KeyFileFlags.NONE);
					language = key_file.get_string("general", "language");
				} catch (GLib.Error err) {
					language = "en";
				}
			}

			var engine = new IBSO.Engine() {
				language = language
			};
			var config = IBSO.Config.load();
			try {
				if (config.key_file.get_boolean("general", "debug")) {
					debug_on = true;
				}
			} catch (GLib.Error err) {
			}
			var save = opt_wav == "";
			if (save) {
				try {
					save = config.key_file.get_boolean("general", "debug")
						&& config.key_file.get_boolean("general", "debug-recordings");
				} catch (GLib.Error err) {
					save = false;
				}
			}
			IBSO.Capture transcriber;
			try {
				transcriber = new IBSO.Capture(engine, config, save) {
					model_dir = model_dir
				};
				transcriber.load();
			} catch (GLib.Error err) {
				GLib.critical("%s", err.message);
				return 1;
			}

			if (opt_wav != "") {
				return this.run_wav(command_line, transcriber, language, model_dir);
			}
			if (opt_script != "") {
				return this.run_script(command_line, transcriber, language, model_dir);
			}
			return this.run_mic(command_line, transcriber, language, model_dir);
		}

		private int run_wav(
			GLib.ApplicationCommandLine command_line,
			IBSO.Capture transcriber,
			string language,
			string model_dir
		)
		{
			if (opt_from < 0.0) {
				GLib.critical("--from must be >= 0");
				return 1;
			}
			if (opt_to >= 0.0 && opt_to <= opt_from) {
				GLib.critical("--to must be greater than --from");
				return 1;
			}
			if (opt_chunk_ms < 1) {
				GLib.critical("--chunk-ms must be >= 1");
				return 1;
			}

			GLib.DataInputStream input;
			try {
				input = new GLib.DataInputStream(GLib.File.new_for_path(opt_wav).read()) {
					byte_order = GLib.DataStreamByteOrder.LITTLE_ENDIAN
				};
			} catch (GLib.Error err) {
				GLib.critical("%s", err.message);
				return 1;
			}

			var tag = new uint8[4];
			try {
				if (input.read(tag) != 4 || ((string) tag).substring(0, 4) != "RIFF") {
					GLib.critical("%s: not a RIFF file", opt_wav);
					return 1;
				}
				input.read_uint32();
				if (input.read(tag) != 4 || ((string) tag).substring(0, 4) != "WAVE") {
					GLib.critical("%s: not a WAVE file", opt_wav);
					return 1;
				}
			} catch (GLib.Error err) {
				GLib.critical("%s", err.message);
				return 1;
			}

			var rate = 0;
			var channels = 0;
			var bits = 0;
			var data_n = 0;
			try {
				while (true) {
					if (input.read(tag) != 4) {
						GLib.critical("%s: missing data chunk", opt_wav);
						return 1;
					}
					var id = ((string) tag).substring(0, 4);
					var sz = input.read_uint32();
					if (id == "fmt ") {
						input.read_uint16();
						channels = input.read_uint16();
						rate = (int) input.read_uint32();
						input.read_uint32();
						input.read_uint16();
						bits = input.read_uint16();
						if (sz > 16) {
							input.skip(sz - 16);
						}
						continue;
					}
					if (id == "data") {
						data_n = (int) sz;
						break;
					}
					input.skip(sz + (sz % 2));
				}
			} catch (GLib.Error err) {
				GLib.critical("%s", err.message);
				return 1;
			}
			if (rate != 16000 || channels != 1 || bits != 16) {
				GLib.critical("%s: need mono 16 kHz S16LE (got %d Hz, %d ch, %d bit)",
					opt_wav, rate, channels, bits);
				return 1;
			}

			var n_all = data_n / 2;
			var sess = new IBSO.Session.load(opt_wav);
			if (sess.pcm.length > 0) {
				n_all = (int) sess.pcm.length;
			}
			var i0 = (int) Math.floor(opt_from * 16000.0);
			var i1 = opt_to < 0.0 ? n_all : (int) Math.ceil(opt_to * 16000.0);
			if (i0 > n_all) {
				i0 = n_all;
			}
			if (i1 > n_all) {
				i1 = n_all;
			}
			var n = i1 - i0;
			float[] pcm;
			if (sess.pcm.length > 0) {
				pcm = new float[n];
				for (var i = 0; i < n; i++) {
					pcm[i] = sess.pcm.index(i0 + i);
				}
			} else {
				try {
					if (i0 > 0) {
						input.skip(i0 * 2);
					}
					pcm = new float[n];
					for (var i = 0; i < n; i++) {
						pcm[i] = input.read_int16() / 32768.0f;
					}
				} catch (GLib.Error err) {
					GLib.critical("%s", err.message);
					return 1;
				}
			}

			var run = new IBSO.Session();
			run.pending = sess.pending;
			run.feedlog_body = sess.feedlog_body;
			if (pcm.length > 0) {
				run.pcm.append_vals(pcm, pcm.length);
			}
			if (sess.chunk_n.length > 0) {
				var full = new int[sess.chunk_n.length];
				GLib.Memory.copy((void*) full, sess.chunk_n.data,
					sess.chunk_n.length * sizeof(int));
				var sliced = (i0 > 0 || i1 < n_all);
				var feed_chunks = sliced
					? IBSO.Debug.Recording.slice_chunks(full, i0, i1)
					: full;
				for (var i = 0; i < feed_chunks.length; i++) {
					var cn = feed_chunks[i];
					run.chunk_n.append_val(cn);
				}
				/* Full-session chunk_t only — slice_chunks drops reset/flush. */
				if (!sliced && sess.chunk_t.length == sess.chunk_n.length) {
					for (var i = 0; i < (int) sess.chunk_t.length; i++) {
						var t = sess.chunk_t.index(i);
						run.chunk_t.append_val(t);
					}
				}
			}
			if (sess.endpoint_off.length > 0) {
				for (var i = 0; i < (int) sess.endpoint_off.length; i++) {
					var off = sess.endpoint_off.index(i);
					if (off <= i0) {
						continue;
					}
					if (off > i1) {
						break;
					}
					var rel = off - i0;
					run.endpoint_off.append_val(rel);
				}
			}
			var file_end = opt_from + pcm.length / 16000.0;
			GLib.debug("#wav %s from=%.3f to=%.3f live_chunks=%u f32=%u endpoints=%u fast=%s samples=%d language=%s model=%s",
				opt_wav, opt_from, file_end,
				run.chunk_n.length, run.pcm.length, run.endpoint_off.length,
				opt_fast_transcribe.to_string(), pcm.length, language, model_dir);

			var replay = new IBSO.Debug.Replay(transcriber);

			var first_commit = true;
			var loop = new GLib.MainLoop();
			transcriber.partial.connect((text) => {
				GLib.debug("#partial t=%.3f text=%s", opt_from + transcriber.feed_pos_s, text);
			});
			transcriber.endpoint.connect((text) => {
				if (text.strip() == "") {
					return;
				}
				GLib.debug("#endpoint t=%.3f text=%s", opt_from + transcriber.feed_pos_s, text);
				if (opt_stats) {
					GLib.debug("#stats tokens=%d audio=%.2fs wall=%.2fs partials=%d",
						transcriber.last_token_count, transcriber.last_audio_s,
						transcriber.last_wall_s, transcriber.last_partial_count);
				}
				if (!first_commit) {
					command_line.print("\n");
				}
				first_commit = false;
				command_line.print("%s\n", text);
			});
			transcriber.flushed.connect(() => {
				loop.quit();
			});

			if (opt_fast_transcribe) {
				run.feed(transcriber);
				loop.run();
				var stem = opt_wav.slice(0, opt_wav.length - 4);
				try {
					GLib.FileUtils.set_contents(stem + ".feedlog.replay",
						transcriber.feed_log);
				} catch (GLib.Error err) {
					GLib.warning("feedlog.replay: %s", err.message);
				}
				return 0;
			}

			/* Default: same Browse Replay (speakers + Session.feed_next). */
			var path = opt_wav;
			string? tmp = null;
			if (opt_from > 0.0 || opt_to >= 0.0) {
				tmp = GLib.Path.build_filename(GLib.Environment.get_tmp_dir(),
					"sherpa-onnx-mic-%d.wav".printf((int) GLib.get_monotonic_time()));
				try {
					IBSO.Debug.Recording.write_wav_s16le(tmp, pcm, 16000);
				} catch (GLib.Error err) {
					GLib.critical("%s", err.message);
					return 1;
				}
				path = tmp;
			}
			GLib.debug("#replay %s", path);
			replay.start(path, run);
			loop.run();
			if (tmp != null) {
				GLib.FileUtils.unlink(tmp);
			}
			return 0;
		}

		private int run_script(
			GLib.ApplicationCommandLine command_line,
			IBSO.Capture transcriber,
			string language,
			string model_dir
		)
		{
			string script_body;
			try {
				GLib.FileUtils.get_contents(opt_script, out script_body);
			} catch (GLib.FileError err) {
				GLib.critical("%s", err.message);
				return 1;
			}
			string[] lines = {};
			foreach (var raw in script_body.split("\n")) {
				var line = raw.strip();
				if (line == "" || line.has_prefix("#")) {
					continue;
				}
				lines += line;
			}
			if (lines.length == 0) {
				GLib.critical("No utterances in %s", opt_script);
				return 1;
			}

			GLib.debug("Script %s (%d lines). Model: %s  language=%s",
				opt_script, lines.length, model_dir, language);
			var settle_ms = 4000;
			GLib.debug("Say each line; short pauses OK. Next line after ~%gs quiet. Ctrl+C aborts.",
				settle_ms / 1000.0);

			var sum_score = 0.0;
			var n_scored = 0;
			for (var i = 0; i < lines.length; i++) {
				var expected = lines[i];
				GLib.debug("[%d/%d] Say: %s", i + 1, lines.length, expected);
				command_line.print(">");
				GLib.stdout.flush();

				var got = "";
				var last_partial = "";
				var settle_id = (uint) 0;
				var utter = new GLib.MainLoop();
				var ep = transcriber.endpoint.connect((text) => {
					var piece = text.strip();
					if (piece == "") {
						return;
					}
					got = got == "" ? piece : got + " " + piece;
					last_partial = "";
					command_line.print("\r> %s", got);
					GLib.stdout.flush();
					if (settle_id != 0) {
						GLib.Source.remove(settle_id);
					}
					settle_id = GLib.Timeout.add(settle_ms, () => {
						settle_id = 0;
						utter.quit();
						return GLib.Source.REMOVE;
					});
				});
				var part = transcriber.partial.connect((text) => {
					last_partial = text.strip();
					var show = got == "" ? last_partial : got + " " + last_partial;
					command_line.print("\r> %s", show);
					GLib.stdout.flush();
					if (settle_id != 0) {
						GLib.Source.remove(settle_id);
					}
					settle_id = GLib.Timeout.add(settle_ms, () => {
						settle_id = 0;
						utter.quit();
						return GLib.Source.REMOVE;
					});
				});
				var quit = false;
				var sig = GLib.Unix.signal_add(GLib.ProcessSignal.INT, () => {
					quit = true;
					utter.quit();
					return GLib.Source.REMOVE;
				});
				transcriber.start();
				utter.run();
				transcriber.disconnect(ep);
				transcriber.disconnect(part);
				GLib.Source.remove(sig);
				if (settle_id != 0) {
					GLib.Source.remove(settle_id);
				}
				transcriber.stop();
				if (quit) {
					GLib.debug("Aborted.");
					return 130;
				}
				if (last_partial != "") {
					got = got == "" ? last_partial : got + " " + last_partial;
				}

				command_line.print("\nHeard:\n  %s\n", got);

				var a = expected.strip().down();
				var b = got.strip().down();
				while (a.contains("  ")) {
					a = a.replace("  ", " ");
				}
				while (b.contains("  ")) {
					b = b.replace("  ", " ");
				}
				var score = 0.0;
				if (a == b) {
					score = 100.0;
				} else if (a.char_count() == 0) {
					score = 0.0;
				} else {
					var an = a.char_count();
					var bn = b.char_count();
					var prev = new int[bn + 1];
					var cur = new int[bn + 1];
					for (var j = 0; j <= bn; j++) {
						prev[j] = j;
					}
					for (var ai = 1; ai <= an; ai++) {
						cur[0] = ai;
						var ca = a.get(a.index_of_nth_char(ai - 1));
						for (var bi = 1; bi <= bn; bi++) {
							var cb = b.get(b.index_of_nth_char(bi - 1));
							var cost = ca == cb ? 0 : 1;
							var del = prev[bi] + 1;
							var ins = cur[bi - 1] + 1;
							var sub = prev[bi - 1] + cost;
							var best = del < ins ? del : ins;
							cur[bi] = best < sub ? best : sub;
						}
						var swap = prev;
						prev = cur;
						cur = swap;
					}
					var dist = prev[bn];
					var denom = an > bn ? an : bn;
					score = denom > 0 ? 100.0 * (1.0 - (double) dist / denom) : 0.0;
					if (score < 0.0) {
						score = 0.0;
					}
				}
				sum_score += score;
				n_scored++;
				GLib.debug("Score: %.0f%%", score);
			}
			if (n_scored > 0) {
				GLib.debug("Average: %.0f%%  (%d lines)", sum_score / n_scored, n_scored);
			}
			return 0;
		}

		private int run_mic(
			GLib.ApplicationCommandLine command_line,
			IBSO.Capture transcriber,
			string language,
			string model_dir
		)
		{
			GLib.debug("Listening (Ctrl+C to quit). Model: %s  language=%s%s",
				model_dir, language, opt_stats ? " [stats]" : "");

			var segments = 0;
			var total_chars = 0;
			var total_tokens = 0;
			var total_audio_s = 0.0;
			var total_wall_s = 0.0;

			transcriber.partial.connect((text) => {
				command_line.print("\r%s", text);
				GLib.stdout.flush();
			});
			transcriber.endpoint.connect((text) => {
				command_line.print("\n");
				GLib.stdout.flush();
				if (!opt_stats) {
					return;
				}
				segments++;
				var chars = text.char_count();
				total_chars += chars;
				total_tokens += transcriber.last_token_count;
				total_audio_s += transcriber.last_audio_s;
				total_wall_s += transcriber.last_wall_s;
				GLib.debug("#seg %d  tokens=%d  chars=%d  audio=%.2fs  wall=%.2fs  partials=%d  rtf=%.2f",
					segments, transcriber.last_token_count, chars, transcriber.last_audio_s,
					transcriber.last_wall_s, transcriber.last_partial_count,
					transcriber.last_audio_s > 0.01 ? transcriber.last_wall_s / transcriber.last_audio_s : 0.0);
			});

			var loop = new GLib.MainLoop();
			GLib.Unix.signal_add(GLib.ProcessSignal.INT, () => {
				loop.quit();
				return GLib.Source.REMOVE;
			});

			transcriber.start();
			loop.run();
			transcriber.stop();
			command_line.print("\n");

			if (opt_stats && segments > 0) {
				GLib.debug("#total segments=%d  chars=%d  tokens=%d  audio=%.1fs  wall=%.1fs  avg_rtf=%.2f",
					segments, total_chars, total_tokens, total_audio_s, total_wall_s,
					total_audio_s > 0.01 ? total_wall_s / total_audio_s : 0.0);
			}
			return 0;
		}
	}
}
