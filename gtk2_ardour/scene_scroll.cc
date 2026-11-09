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

#include <algorithm>
#include <cmath>

#include "scene_scroll.h"
#include "ui_config.h"

SceneScroll&
SceneScroll::instance ()
{
	static SceneScroll* s = new SceneScroll; /* never freed: widgets use it while the GUI goes away */
	return *s;
}

SceneScroll::SceneScroll ()
	: _adj (0, 0, 1, 1, 1, 1)
	, _hadj (0)
	, _rows (0)
	, _view_h (0)
{
}

double
SceneScroll::row_height ()
{
	/* the clip slots' height, as FittedCanvasWidget scales them */
	return 16. * UIConfiguration::instance ().get_ui_scale ();
}

uint32_t
SceneScroll::first_row () const
{
	double const v = std::max (0., offset ());
	return (uint32_t) floor (v / row_height ());
}

uint32_t
SceneScroll::pool_size (double h)
{
	return h > 0 ? (uint32_t) ceil (h / row_height ()) + 1 : 0;
}

void
SceneScroll::set_rows (uint32_t n)
{
	if (n != _rows) {
		_rows = n;
		update ();
	}
}

void
SceneScroll::set_view_height (double h)
{
	if (h != _view_h) {
		_view_h = h;
		update ();
	}
}

void
SceneScroll::update ()
{
	double const rh    = row_height ();
	double const upper = std::max (1., _rows * rh);
	double const page  = std::max (1., std::min (_view_h, upper));

	_adj.set_lower (0);
	_adj.set_upper (upper);
	_adj.set_page_size (page);
	_adj.set_step_increment (rh);
	_adj.set_page_increment (std::max (rh, page - rh));
	/* the last scene stays at the bottom when the view grows or scenes go */
	double const v = std::max (0., std::min (_adj.get_value (), upper - page));
	if (v != _adj.get_value ()) {
		_adj.set_value (v);
	}
	_adj.changed ();
}

void
SceneScroll::show_row (uint32_t n)
{
	double const rh = row_height ();
	double const y0 = n * rh;
	double const y1 = y0 + rh;
	double       v  = _adj.get_value ();
	if (y0 < v) {
		v = y0;
	} else if (y1 > v + _adj.get_page_size ()) {
		v = y1 - _adj.get_page_size ();
	}
	_adj.set_value (std::max (0., std::min (v, _adj.get_upper () - _adj.get_page_size ())));
}

bool
SceneScroll::scroll_event (GdkEventScroll* ev)
{
	bool const sideways = (ev->state & GDK_SHIFT_MASK) || ev->direction == GDK_SCROLL_LEFT || ev->direction == GDK_SCROLL_RIGHT;

	if (sideways) {
		if (!_hadj) {
			return false;
		}
		bool const back = ev->direction == GDK_SCROLL_UP || ev->direction == GDK_SCROLL_LEFT;
		double const step = pow (_hadj->get_page_size (), 2. / 3.); /* GTK's own wheel step */
		_hadj->set_value (std::max (_hadj->get_lower (), std::min (_hadj->get_upper () - _hadj->get_page_size (), _hadj->get_value () + (back ? -step : step))));
		return true;
	}

	if (ev->direction != GDK_SCROLL_UP && ev->direction != GDK_SCROLL_DOWN) {
		return false;
	}
	double const step = 3 * row_height ();
	double const v    = _adj.get_value () + (ev->direction == GDK_SCROLL_UP ? -step : step);
	_adj.set_value (std::max (0., std::min (v, _adj.get_upper () - _adj.get_page_size ())));
	return true;
}

void
SceneScroll::scroll_pages (int n)
{
	double const v = _adj.get_value () + n * _adj.get_page_increment ();
	_adj.set_value (std::max (0., std::min (v, _adj.get_upper () - _adj.get_page_size ())));
}

void
SceneScroll::scroll_to_end (bool end)
{
	_adj.set_value (end ? std::max (0., _adj.get_upper () - _adj.get_page_size ()) : 0.);
}
