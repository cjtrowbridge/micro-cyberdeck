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
	 * Public ASR entry: every op hits the {@link Session} recording log, then ASR.
	 *
	 * Callers construct this only — never {@link Transcriber}. When {@link save},
	 * {@link Session.recording} is on for a listen and ''.chunks'' / ''.f32'' /
	 * ''.pending'' / ''.feedlog'' mirror {@link push} / {@link reset} /
	 * {@link flush} (''.chunks'' may include injection µs). When save is false,
	 * Session methods no-op and only ASR runs.
	 *
	 * ''.feedlog'' lines are checksummed on the ASR worker via {@link note_feed},
	 * then appended on Idle. Ops: ''R'' / ''P'' / ''E'' (endpoint cut at
	 * sample off) / ''F''. Replay writes the same shape to ''.feedlog.replay''.
	 *
	 * == Example ==
	 *
	 * {{{
	 * var live = new Capture(engine, config, true);
	 * live.load();
	 * live.start();
	 * live.stop(pending);
	 *
	 * var replay = new Capture(engine, config, false);
	 * replay.load();
	 * replay.reset();
	 * replay.push(chunk);
	 * replay.flush(pending);
	 * }}}
	 */
	public class Capture : Transcriber
	{
		/**
		 * When true, listen {@link start} opens a recording {@link Session}.
		 * When false, Session log calls no-op.
		 */
		public bool save { get; construct; }

		/** Debug op / PCM log for the current listen (or idle empty session). */
		private Session session = new Session();

		/** Samples accepted into the recognizer (''.feedlog'' sample offset). */
		private int feed_off = 0;

		/** Monotonic µs at first recognizer feed line (0 = not started). */
		private int64 feed_t0 = 0;

		/**
		 * Checksum + feed-time lines (recognizer accept path).
		 * ''t='' is µs since first line. Live copies into ''.feedlog'' on stop.
		 */
		public string feed_log { get; private set; default = ""; }

		/**
		 * @param engine owning engine (IBus or stub)
		 * @param config settings (already loaded)
		 * @param save record debug sidecars when true
		 */
		public Capture(Engine engine, Config config, bool save)
		{
			GLib.Object(
				engine: engine,
				config: config,
				save: save
			);
			this.language = engine.language;
			this.on_mic_pcm = this.push;
			this.endpoint.connect((text) => {
				this.session.record_endpoint(text);
			});
		}

		/**
		 * Clear the recording log, then load the recognizer.
		 *
		 * @throws GLib.Error if models / pipeline cannot be created
		 */
		public void load() throws GLib.Error
		{
			this.session = new Session();
			this.load_model();
		}

		/**
		 * Log PCM into Session, then queue for ASR.
		 *
		 * @param samples mono float PCM (ownership taken)
		 */
		public void push(owned float[] samples)
		{
			this.session.record_pcm(samples);
			this.queue_pcm((owned) samples);
		}

		/**
		 * Log reset into Session, then queue stream reset.
		 * Starts a new ''.feedlog'' (offsets / body cleared).
		 */
		public void reset()
		{
			this.feed_off = 0;
			this.feed_t0 = 0;
			this.feed_log = "";
			this.session.record_reset();
			this.queue_reset();
		}

		/**
		 * Log flush into Session, then queue ASR flush.
		 *
		 * @param pending stop partial to commit (may be empty)
		 */
		public void flush(string pending = "", bool discard = false)
		{
			this.session.record_flush(pending);
			this.queue_flush(pending, discard);
		}

		/** Open a recording session when {@link save}, log reset, start mic. */
		public void start()
		{
			this.session = this.save ? new Session() { 
				started = new GLib.DateTime.now_local(), 
				recording = true } 	
				: new Session();
			this.reset();
			this.start_mic();
		}

		/**
		 * Stop mic, log flush, persist sidecars when this listen was recorded.
		 * Waits for worker {@link flushed} so Idle feedlog lines are complete.
		 *
		 * @param pending unfinished partial from the engine (may be empty)
		 */
		public void stop(string pending = "", bool discard = false)
		{
			if (!this.listening) {
				return;
			}
			var recorded = this.session.recording;
			this.stop_mic();
			this.flush(pending, discard);
			if (!recorded) {
				return;
			}
			var sess = this.session;
			this.session = new Session();
			ulong hid = 0;
			hid = this.flushed.connect(() => {
				this.disconnect(hid);
				sess.feedlog_body = this.feed_log;
				sess.flush();
			});
		}

		/**
		 * Worker: checksum / marker, Idle-append to {@link feed_log}.
		 *
		 * @param op ''R'' / ''P'' / ''E'' / ''F''
		 * @param samples PCM for ''P''
		 */
		protected override void note_feed(char op, float[]? samples)
		{
			if (this.feed_t0 == 0) {
				this.feed_t0 = GLib.get_monotonic_time();
			}
			var t = GLib.get_monotonic_time() - this.feed_t0;
			var  line = "";
			if (op == 'P') {
				var sum = new GLib.Checksum(GLib.ChecksumType.SHA256);
				unowned uint8[] bytes = (uint8[]) samples;
				sum.update(bytes, samples.length * sizeof(float));
				line = "%d %d %s t=%lld".printf(this.feed_off, samples.length,
					sum.get_string().substring(0, 16), t);
				this.feed_off += samples.length;
			} else {
				line = "%c %d t=%lld".printf(op, this.feed_off, t);
			}
			var copy = line;
			GLib.Idle.add(() => {
				GLib.debug("#feed %s", copy);
				this.feed_log += copy + "\n";
				return GLib.Source.REMOVE;
			});
		}
	}
}
