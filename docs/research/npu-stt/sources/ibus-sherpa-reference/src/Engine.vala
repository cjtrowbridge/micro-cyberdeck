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
	/**
	 * IBus input-method engine for local speech dictation.
	 *
	 * Toggle hotkey (or panel property) starts/stops the mic. Partials →
	 * preedit; endpoints → {@link IBus.Engine.commit_text}. Listening state
	 * uses a normal IBus panel property (like Hangul/Pinyin mode).
	 *
	 * @since 0.3
	 */
	public class Engine : IBus.Engine
	{
		/** Process-wide catalog (set by {@link Application}). */
		public static Models models = new Models();

		/** Process-wide prefs (''settings.ini''); set by {@link Application} / {@link focus_in}. */
		public static Config config = new Config();

		/** Parsed toggle keysym from config ''general/hotkey''. */
		public static uint toggle_keyval = 0;

		/** Parsed toggle modifiers from config ''general/hotkey''. */
		public static uint toggle_mods = 0;

		/**
		 * Process-wide recognizer + mic (IBus constructs one {@link Engine} per
		 * text field; they all use this). Null if no pack is installed.
		 */
		private static Capture? shared;

		/** {@link shared} for this context (null if this language has no pack). */
		private Capture? transcriber;

		private IBus.Property prop;
		private IBus.PropList props;
		private uint anim_source = 0;
		private int anim_phase = 0;
		private bool saw_partial = false;
		/** Last language we already notified as having no pack (avoid focus spam). */
		private string notified_missing_lang = "";
		/**
		 * ASR / panel language from the active IBus engine id only
		 * (''sherpa-onnx-es-ES'' → ''es-ES''; bare ''sherpa-onnx'' → ''en'').
		 */
		public string language { get; private set; default = "en"; }
		/** Idle panel badge: ''en-v'', ''en-us-v''; same lang/region collapses (''ro-v''). */
		private string panel_symbol = "en-v";

		construct
		{
			var id = this.engine_name;
			if (id != null && id.has_prefix("sherpa-onnx-")) {
				this.language = id.substring("sherpa-onnx-".length);
			}
			var tag = this.language.down().split("-", 2);
			this.panel_symbol = tag[0] + "-v";
			if (tag.length >= 2 && tag[0] != tag[1]) {
				this.panel_symbol = tag[0] + "-" + tag[1] + "-v";
			}
			this.prop = new IBus.Property(
				"listening", IBus.PropType.TOGGLE, new IBus.Text.from_string("Mic off"),
				"microphone-sensitivity-muted", new IBus.Text.from_string("Toggle speech dictation"),
				true, true, IBus.PropState.UNCHECKED, null
			);
			this.prop.set_symbol(new IBus.Text.from_string(this.panel_symbol));
			this.props = new IBus.PropList();
			this.props.append(this.prop);
		}

		public override void enable()
		{
			base.enable();
			var listening = this.transcriber != null && this.transcriber.listening;
			GLib.debug("enable: listening=%s has_focus=%s enabled=%s engine=%s",
				listening.to_string(), this.has_focus.to_string(), this.enabled.to_string(),
				this.engine_name ?? "");
			this.ensure_transcriber();
			this.register_properties(this.props);
			this.update_ui();
		}

		public override void focus_in()
		{
			base.focus_in();
			var listening = this.transcriber != null && this.transcriber.listening;
			GLib.debug("focus_in: listening=%s has_focus=%s enabled=%s engine=%s",
				listening.to_string(), this.has_focus.to_string(), this.enabled.to_string(),
				this.engine_name ?? "");
			Engine.config = Config.load();
			Engine.bind_hotkey();
			this.ensure_transcriber();
		}

		public override void focus_out()
		{
			var listening = this.transcriber != null && this.transcriber.listening;
			GLib.debug("focus_out: listening=%s saw_partial=%s has_focus=%s enabled=%s engine=%s",
				listening.to_string(), this.saw_partial.to_string(), this.has_focus.to_string(),
				this.enabled.to_string(), this.engine_name ?? "");
			/*
			 * COMMIT first (IBus puts the draft in the widget — play/send can
			 * read it), then clear our preedit so reset cannot inject again.
			 * Do not commit_text here (would double). Restart dots if still on.
			 */
			var had_partial = this.saw_partial;
			base.focus_out();
			if (!listening || !had_partial) {
				return;
			}
			this.saw_partial = false;
			this.stop_preedit_animation();
			this.update_preedit_text_with_mode(new IBus.Text.from_string(""),
				0, false, IBus.PreeditFocusMode.CLEAR);
			this.hide_preedit_text();
			if (this.transcriber.listening) {
				this.start_preedit_animation();
			}
		}

		/**
		 * Richer focus-in when the engine is constructed with ''has_focus_id''
		 * (may not fire on the stock Factory path — logged if it does).
		 */
		public override void focus_in_id(string object_path, string client)
		{
			var listening = this.transcriber != null && this.transcriber.listening;
			GLib.debug("focus_in_id: listening=%s path=%s client=%s engine=%s",
				listening.to_string(), object_path, client, this.engine_name ?? "");
			base.focus_in_id(object_path, client);
		}

		public override void focus_out_id(string object_path)
		{
			var listening = this.transcriber != null && this.transcriber.listening;
			GLib.debug("focus_out_id: listening=%s path=%s engine=%s",
				listening.to_string(), object_path, this.engine_name ?? "");
			base.focus_out_id(object_path);
		}

		public override void set_content_type(uint purpose, uint hints)
		{
			var listening = this.transcriber != null && this.transcriber.listening;
			GLib.debug("set_content_type: listening=%s purpose=%u(%s) hints=%u engine=%s",
				listening.to_string(), purpose, ((IBus.InputPurpose) purpose).to_string(),
				hints, this.engine_name ?? "");
			base.set_content_type(purpose, hints);
		}

		/**
		 * Client reset (composition clear / Post / focus churn): stop listening
		 * if on, and discard any pending partial (do not commit into the field).
		 */
		public override void reset()
		{
			var listening = this.transcriber != null && this.transcriber.listening;
			GLib.debug("reset: listening=%s has_focus=%s enabled=%s engine=%s",
				listening.to_string(), this.has_focus.to_string(), this.enabled.to_string(),
				this.engine_name ?? "");
			if (this.transcriber != null && this.transcriber.listening) {
				this.update_listening(false, false, false);
			}
			base.reset();
		}

		/**
		 * Parse config ''general/hotkey'' into {@link toggle_keyval} / {@link toggle_mods}.
		 */
		public static void bind_hotkey()
		{
			uint keyval;
			IBus.ModifierType mods;
			if (!Engine.config.hotkey(out keyval, out mods)) {
				var hotkey = "";
				try {
					hotkey = Engine.config.key_file.get_string("general", "hotkey");
				} catch (GLib.KeyFileError err) {
				}
				IBus.accelerator_parse("<Control><Shift>space", out keyval, out mods);
				GLib.warning("Could not parse hotkey '%s'; using Control+Shift+space", hotkey);
			}
			Engine.toggle_keyval = keyval;
			Engine.toggle_mods = (uint) mods;
		}

		public override void property_activate(string prop_name, uint prop_state)
		{
			if (prop_name != "listening") {
				return;
			}
			var on = this.transcriber == null || !this.transcriber.listening;
			GLib.debug("listening activate turn_on=%s prop_state=%u has_focus=%s enabled=%s path=%s engine=%s",
				on.to_string(), prop_state, this.has_focus.to_string(), this.enabled.to_string(),
				this.object_path ?? "", this.engine_name ?? "");
			this.update_listening(on, true);
		}

		public override bool process_key_event(uint keyval, uint keycode, uint state)
		{
			if ((state & IBus.ModifierType.RELEASE_MASK) != 0) {
				return false;
			}

			var mods = state & (IBus.ModifierType.CONTROL_MASK | IBus.ModifierType.SHIFT_MASK
				| IBus.ModifierType.MOD1_MASK | IBus.ModifierType.SUPER_MASK
				| IBus.ModifierType.META_MASK | IBus.ModifierType.HYPER_MASK);
			if (keyval == Engine.toggle_keyval && mods == Engine.toggle_mods) {
				var on = this.transcriber == null || !this.transcriber.listening;
				this.update_listening(on, true);
				return true;
			}

			// Pure modifiers must not interrupt listening.
			if (keyval == IBus.Shift_L || keyval == IBus.Shift_R
					|| keyval == IBus.Control_L || keyval == IBus.Control_R
					|| keyval == IBus.Alt_L || keyval == IBus.Alt_R
					|| keyval == IBus.Meta_L || keyval == IBus.Meta_R
					|| keyval == IBus.Super_L || keyval == IBus.Super_R
					|| keyval == IBus.Hyper_L || keyval == IBus.Hyper_R
					|| keyval == IBus.ISO_Level3_Shift || keyval == IBus.Caps_Lock
					|| keyval == IBus.Num_Lock || keyval == IBus.Scroll_Lock) {
				return false;
			}

			/*
			 * WM / app accelerators (Alt+Tab, Super+…, Ctrl+Alt+Left/Right,
			 * Ctrl+Tab, …) still reach the IME. Do not treat them as “typing
			 * interrupts dictation” — pass through and keep listening.
			 * Shift alone (e.g. capital letter) is still a typing interrupt.
			 */
			var accel = mods & (IBus.ModifierType.CONTROL_MASK | IBus.ModifierType.MOD1_MASK
				| IBus.ModifierType.SUPER_MASK | IBus.ModifierType.META_MASK
				| IBus.ModifierType.HYPER_MASK);
			if (accel != 0) {
				if (this.transcriber != null && this.transcriber.listening) {
					GLib.debug("key ignore (accel while listening): keyval=0x%x mods=0x%x",
						keyval, mods);
				}
				return false;
			}

			// While listening, a real (non-accelerator) key stops + commits, then delivers.
			if (this.transcriber != null && this.transcriber.listening) {
				this.update_listening(false, false);
			}

			// Everything else (Shift+A, Enter, …): pass through to the client.
			// Do not forward_key_event — that drops or mishandles modifiers.
			return false;
		}

		public override void disable()
		{
			var listening = this.transcriber != null && this.transcriber.listening;
			GLib.debug("disable: listening=%s has_focus=%s enabled=%s engine=%s",
				listening.to_string(), this.has_focus.to_string(), this.enabled.to_string(),
				this.engine_name ?? "");
			if (this.transcriber != null && this.transcriber.listening) {
				this.update_listening(false, false);
				return;
			}
			this.update_ui();
		}

		/**
		 * Load or recreate {@link transcriber} for {@link language} from ''packs.ini''.
		 */
		private void ensure_transcriber()
		{
			Engine.config = Config.load();
			var pack = "";
			try {
				pack = Engine.config.packs.get_string("packs", this.language);
			} catch (GLib.Error err) {
			}
			/* No prefs row yet: use an installed pack for this language’s family. */
			if (pack == "") {
				try {
					var family = Engine.models.languages.get_string(this.language, "family");
					var best_chunk = -1;
					foreach (var pack_id in Engine.models.packs.get_groups()) {
						if (Engine.models.packs.get_string(pack_id, "family") != family) {
							continue;
						}
						var name = Engine.models.packs.get_string(pack_id, "name");
						var dir = GLib.Path.build_filename(Engine.models.system_prefix, "models", name);
						if (!GLib.FileUtils.test(GLib.Path.build_filename(dir, ".sha256"),
								GLib.FileTest.IS_REGULAR)) {
							continue;
						}
						var chunk = Engine.models.packs.get_integer(pack_id, "chunk");
						if (chunk < best_chunk) {
							continue;
						}
						best_chunk = chunk;
						pack = pack_id;
					}
					if (pack != "") {
						Engine.config.packs.set_string("packs", this.language, pack);
						Engine.config.save_packs();
					}
				} catch (GLib.Error err) {
				}
			}
			var model_dir = "";
			if (pack != "") {
				try {
					var name = Engine.models.packs.get_string(pack, "name");
					var dir = GLib.Path.build_filename(Engine.models.system_prefix, "models", name);
					if (GLib.FileUtils.test(GLib.Path.build_filename(dir, ".sha256"),
							GLib.FileTest.IS_REGULAR)) {
						model_dir = dir;
					}
				} catch (GLib.Error err) {
				}
			}
			if (pack == "" || model_dir == "") {
				this.transcriber = null;
				if (this.notified_missing_lang != this.language) {
					this.notified_missing_lang = this.language;
					this.notify_no_model();
				}
				return;
			}
			this.transcriber = Engine.shared;
			if (this.transcriber != null && this.transcriber.pack == pack) {
				this.transcriber.config = Engine.config;
				return;
			}

			var app = GLib.Application.get_default();
			if (app != null) {
				var note = new GLib.Notification("Sherpa ONNX");
				note.set_body("Loading speech model (" + this.language + ")...");
				app.send_notification("sherpa-onnx-loading", note);
			}
			if (Engine.shared != null) {
				if (Engine.shared.listening) {
					Engine.shared.stop();
				}
				Engine.shared.dispose();
				Engine.shared = null;
			}
			this.transcriber = null;
			try {
				GLib.debug("Loading model pack %s (%s): %s", pack, this.language, model_dir);
				var save = false;
				try {
					save = Engine.config.key_file.get_boolean("general", "debug")
						&& Engine.config.key_file.get_boolean("general", "debug-recordings");
				} catch (GLib.Error err) {
				}
				var t = new Capture(this, Engine.config, save) {
					model_dir = model_dir,
					pack = pack
				};
				t.load();
				Engine.shared = t;
				this.transcriber = t;
			} catch (GLib.Error err) {
				GLib.critical("%s", err.message);
				this.transcriber = null;
			}
			if (app != null) {
				/* GNOME often never shows a banner that is withdrawn immediately. */
				GLib.Timeout.add_seconds(3, () => {
					app.withdraw_notification("sherpa-onnx-loading");
					return GLib.Source.REMOVE;
				});
			}
		}

		/**
		 * Live hypothesis from {@link Transcriber} (main loop).
		 */
		public void on_partial(string text)
		{
			if (this.transcriber == null || !this.transcriber.listening) {
				return;
			}
			this.saw_partial = true;
			this.stop_preedit_animation();
			/* COMMIT = on focus loss, IBus puts this draft into the text box. */
			this.update_preedit_text_with_mode(new IBus.Text.from_string(text),
				(uint) text.length, true, IBus.PreeditFocusMode.COMMIT);
		}

		/**
		 * Endpoint commit from {@link Transcriber} (main loop).
		 * When voice commands are on, match the last few words to a configured
		 * phrase (RapidFuzz), strip it, and inject paragraph / line break / stop.
		 */
		public void on_endpoint(string text)
		{
			if (this.transcriber == null) {
				return;
			}
			this.saw_partial = false;

			var commit = text + " ";
			var stop = false;
			if (Engine.config.key_file.get_boolean("general", "voice-commands")) {
				var paragraph = this.normalize_voice(
					Engine.config.key_file.get_string("general", "voice-paragraph"));
				var line_break = this.normalize_voice(
					Engine.config.key_file.get_string("general", "voice-line-break"));
				var stop_phrase = this.normalize_voice(
					Engine.config.key_file.get_string("general", "voice-stop"));
				var norm = this.normalize_voice(text);
				string[] words = norm.split(" ");
				if (words.length > 0 && words[0] != "") {
					var take = int.min(4, words.length);
					var candidate_parts = new string[take];
					for (var i = 0; i < take; i++) {
						candidate_parts[i] = words[words.length - take + i];
					}
					var candidate = string.joinv(" ", candidate_parts);
					string[] choices = { paragraph, line_break, stop_phrase };
					double score = 0;
					var idx = RapidFuzz.best_ratio(candidate, choices, 80.0, out score);
					GLib.debug("voice candidate=\"%s\" idx=%d score=%.1f", candidate, idx, score);
					if (idx >= 0) {
						var strip_n = choices[idx].split(" ").length;
						string[] raw = text.strip().split(" ");
						var kept = new GLib.GenericArray<string>();
						foreach (var w in raw) {
							if (w != "") {
								kept.add(w);
							}
						}
						var remainder = "";
						if (kept.length > strip_n) {
							var left = new string[kept.length - strip_n];
							for (var i = 0; i < left.length; i++) {
								left[i] = kept[i];
							}
							remainder = string.joinv(" ", left);
						}
						if (idx == 0) {
							commit = remainder == "" ? "\n\n" : remainder + "\n\n";
						} else if (idx == 1) {
							commit = remainder == "" ? "\n" : remainder + "\n";
						} else {
							commit = remainder == "" ? "" : remainder + " ";
							stop = true;
						}
					}
				}
			}

			if (commit != "") {
				this.commit_text(new IBus.Text.from_string(commit));
			}
			this.hide_preedit_text();
			if (stop) {
				this.update_listening(false, true);
				return;
			}
			if (this.transcriber.listening) {
				this.start_preedit_animation();
			}
		}

		/**
		 * Lowercase, strip punctuation, squeeze spaces, map spoken ''ok'' → ''okay''.
		 */
		private string normalize_voice(string text)
		{
			var down = text.down();
			var buf = new GLib.StringBuilder();
			unichar c;
			var i = 0;
			while (down.get_next_char(ref i, out c)) {
				if (c.isalnum() || c.isspace()) {
					buf.append_unichar(c);
				} else {
					buf.append_c(' ');
				}
			}
			var out_buf = new GLib.StringBuilder();
			var first = true;
			foreach (var w in buf.str.split(" ")) {
				if (w == "") {
					continue;
				}
				if (!first) {
					out_buf.append_c(' ');
				}
				first = false;
				out_buf.append(w == "ok" ? "okay" : w);
			}
			return out_buf.str;
		}

		/**
		 * Start or stop the mic. Stopping lets the worker drain the encoder
		 * tail and commit that, unless ''commit_pending'' is false
		 * (client {@link reset} / Post churn).
		 *
		 * @param listening desired mic state
		 * @param notify emit the listening desktop notification
		 * @param commit_pending when stopping, commit the last partial (default true)
		 */
		private void update_listening(bool listening, bool notify, bool commit_pending = true)
		{
			if (listening) {
				this.ensure_transcriber();
			}
			if (this.transcriber == null) {
				return;
			}
			if (listening == this.transcriber.listening) {
				return;
			}
			if (listening) {
				GLib.debug("listening ON (has_focus=%s)", this.has_focus.to_string());
				this.transcriber.engine = this;
				this.transcriber.start();
				this.update_ui();
				if (notify) {
					this.maybe_notify_toggle();
				}
				return;
			}

			GLib.debug("listening OFF (has_focus=%s commit_pending=%s)",
				this.has_focus.to_string(), commit_pending.to_string());
			/* Worker drains the encoder tail, then commits. A snapshot of
			 * last_text here is missing the last word still in the chunk.
			 * commit_pending false (client reset) discards that tail. */
			this.transcriber.stop("", !commit_pending);
			this.stop_preedit_animation();
			this.saw_partial = false;
			this.update_preedit_text(new IBus.Text.from_string(""), 0, false);
			this.hide_preedit_text();
			this.prop.set_label(new IBus.Text.from_string("Mic off"));
			this.prop.set_symbol(new IBus.Text.from_string(this.panel_symbol));
			this.prop.set_icon("microphone-sensitivity-muted");
			this.prop.set_state(IBus.PropState.UNCHECKED);
			this.update_property(this.prop);
			if (notify) {
				this.maybe_notify_toggle();
			}
		}

		/** Panel label/symbol + preedit hint from current mic state. */
		private void update_ui()
		{
			if (this.transcriber != null && this.transcriber.listening) {
				this.prop.set_label(new IBus.Text.from_string("Listening..."));
				this.prop.set_symbol(new IBus.Text.from_string("..."));
				this.prop.set_icon("audio-input-microphone");
				this.prop.set_state(IBus.PropState.CHECKED);
				this.update_property(this.prop);
				this.saw_partial = false;
				this.start_preedit_animation();
				return;
			}

			this.stop_preedit_animation();
			this.saw_partial = false;
			this.prop.set_label(new IBus.Text.from_string("Mic off"));
			this.prop.set_symbol(new IBus.Text.from_string(this.panel_symbol));
			this.prop.set_icon("microphone-sensitivity-muted");
			this.prop.set_state(IBus.PropState.UNCHECKED);
			this.update_preedit_text(new IBus.Text.from_string(""), 0, false);
			this.hide_preedit_text();
			this.update_property(this.prop);
		}

		private void start_preedit_animation()
		{
			this.stop_preedit_animation();
			if (!Engine.config.key_file.get_boolean("general", "preedit-animation")) {
				var hint = "Listening...";
				this.update_preedit_text_with_mode(new IBus.Text.from_string(hint),
					(uint) hint.length, true, IBus.PreeditFocusMode.CLEAR);
				return;
			}

			this.anim_phase = 0;
			this.show_anim_preedit();
			this.anim_source = GLib.Timeout.add(300, () => {
				if (this.transcriber == null || !this.transcriber.listening || this.saw_partial) {
					this.anim_source = 0;
					return GLib.Source.REMOVE;
				}
				/* Bounce . → .. → ... → .. → . ... */
				this.anim_phase = (this.anim_phase + 1) % 4;
				this.show_anim_preedit();
				return GLib.Source.CONTINUE;
			});
		}

		private void show_anim_preedit()
		{
			string dots;
			switch (this.anim_phase) {
			case 0:
				dots = ".";
				break;
			case 1:
			case 3:
				dots = "..";
				break;
			default:
				dots = "...";
				break;
			}
			this.update_preedit_text_with_mode(new IBus.Text.from_string(dots),
				(uint) dots.length, true, IBus.PreeditFocusMode.CLEAR);
		}

		private void stop_preedit_animation()
		{
			if (this.anim_source == 0) {
				return;
			}
			GLib.Source.remove(this.anim_source);
			this.anim_source = 0;
		}

		private void maybe_notify_toggle()
		{
			if (!Engine.config.key_file.get_boolean("general", "notifications")) {
				return;
			}
			var app = GLib.Application.get_default();
			if (app == null || this.transcriber == null) {
				return;
			}
			var note = new GLib.Notification("Sherpa ONNX");
			if (this.transcriber.listening) {
				note.set_body("Listening...");
			} else {
				note.set_body("Mic off");
			}
			app.send_notification("sherpa-onnx-listening", note);
		}

		/**
		 * Always notify when this language has no usable pack (not gated by the
		 * listening-notifications pref). Action opens Preferences.
		 */
		private void notify_no_model()
		{
			var app = GLib.Application.get_default();
			if (app == null) {
				return;
			}
			var note = new GLib.Notification("Sherpa ONNX");
			note.set_body("No speech model for " + this.language);
			note.set_default_action("app.open-preferences");
			note.add_button("Open Preferences", "app.open-preferences");
			app.send_notification("sherpa-onnx-no-model", note);
		}
	}
}
