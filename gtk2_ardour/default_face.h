/*
 * Copyright (C) 2026 Spite
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#include <memory>
#include <string>

#include <cairo.h>

#include <glibmm/refptr.h>

namespace Pango {
	class Context;
}

namespace ARDOUR {
	class PluginInsert;
}

/* Spite: the card a device shows where it has no face of its own (any plugin but
 * PgD4S, and PgD4S until its live face is there): its name, maker and kind,
 * whether it is on, and whether its own window is open, in plugdata's look (an
 * object box of its dark theme, with a title strip).
 *
 * It is only data and a paint function: whoever shows the card paints it on its
 * own window (the MCR's canvas does, so cables can be drawn over it), and asks
 * part_at () what a click hit. */
struct DefaultFace
{
	DefaultFace ();

	/* what is at a point of the card */
	enum Part {
		Outside,
		Led,   /* turns the device on or off */
		Title, /* the title strip, with the device's name */
		Body
	};

	std::string name;
	std::string maker;
	std::string kind;      /* its plugin format, and whether it is an instrument */
	bool        on;
	bool        in_window; /* its own window is open */
	bool        live;      /* the device shows a live face of its own (PgD4S) */

	/* name, maker, kind, on and live, from the device */
	void read (std::shared_ptr<ARDOUR::PluginInsert>);

	/* a card's width for the Device view's face height and title bar height */
	static int  width (int face_h, int title_h);
	/* x and y from the card's top left */
	static Part part_at (double x, double y, double w, double h, double title_h);

	/* the card, its top left at x, y, its text in the widget's Pango context (the
	 * screen's resolution); hovered draws its outline lighter */
	void paint (cairo_t*, Glib::RefPtr<Pango::Context> const&, double x, double y, double w, double h, double title_h, bool hovered) const;
};
