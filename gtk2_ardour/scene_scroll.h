/*
 * Copyright (C) 2026 Spite
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include <cstdint>

#include <sigc++/signal.h>
#include <ydk/gdkevents.h>
#include <ytkmm/adjustment.h>

/* Spite: the Cue page's scenes scroll together, as in Live's Session View.
 * Every track's clip grid and the scene column show the rows of this one
 * adjustment (its value is in pixels; rows are row_height () apart), with a
 * scrollbar beside the scene column. The strips only scroll sideways, above
 * a mixer that stays put. A grid only has rows for the scenes on screen
 * (pool_size ()), so the page costs the same with 16 scenes or 100,000.
 */
class SceneScroll
{
public:
	static SceneScroll& instance ();

	Gtk::Adjustment& adjustment () { return _adj; }

	/* the strips' sideways scrolling, for Shift+wheel over a grid */
	void set_hadjustment (Gtk::Adjustment* a) { _hadj = a; }

	/* one scene's row, in pixels (the UI scale included) */
	static double row_height ();

	void     set_rows (uint32_t);
	uint32_t rows () const { return _rows; }

	/* the grids' height on screen: they all have the same */
	void   set_view_height (double);
	double view_height () const { return _view_h; }

	double offset () const { return _adj.get_value (); }

	/* the first scene with any of its row on screen */
	uint32_t first_row () const;
	/* rows a view of height h needs, a part shown at both ends */
	static uint32_t pool_size (double h);

	/* scroll the least that puts the scene's whole row in view */
	void show_row (uint32_t);
	/* a wheel over a grid or the scene column: scenes, or tracks with Shift */
	bool scroll_event (GdkEventScroll*);
	/* Page Up/Down, Home and End */
	void scroll_pages (int);
	void scroll_to_end (bool end);

private:
	SceneScroll ();
	void update ();

	Gtk::Adjustment  _adj;
	Gtk::Adjustment* _hadj;
	uint32_t         _rows;
	double           _view_h;
};
