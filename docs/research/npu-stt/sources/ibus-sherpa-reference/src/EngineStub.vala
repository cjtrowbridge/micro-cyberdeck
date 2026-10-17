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
	 * Stand-in so CLI/GTK can compile {@link Transcriber} without IBus.
	 * The engine binary uses the real {@link Engine} instead.
	 */
	public class Engine : GLib.Object
	{
		/** Catalog language code (CLI/GTK may set before building a Transcriber). */
		public string language { get; set; default = "en"; }

		public void on_partial(string text)
		{
		}

		public void on_endpoint(string text)
		{
		}
	}
}
