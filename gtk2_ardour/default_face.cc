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

#include <algorithm>
#include <cmath>

#include <pango/pangocairo.h>
#include <pangomm/context.h>
#include <pangomm/layout.h>

#include "pbd/compose.h"

#include "ardour/plugin.h"
#include "ardour/plugin_insert.h"
#include "ardour/plugin_manager.h"

#include "gtkmm2ext/colors.h"
#include "gtkmm2ext/utils.h"

#include "default_face.h"
#include "spite_devices.h"
#include "ui_config.h"

#include "pbd/i18n.h"

using namespace ARDOUR;

/* plugdata's dark theme (PgD4S/Source/LookAndFeel.cpp) */
static Gtkmm2ext::Color const card_bg       = 0x191919ff; /* default_object_background */
static Gtkmm2ext::Color const title_bg      = 0x2c2c2cff; /* panel_background */
static Gtkmm2ext::Color const outline       = 0x555555ff; /* object_outline_colour */
static Gtkmm2ext::Color const outline_hover = 0x696969ff; /* gui_internal_outline_colour */
static Gtkmm2ext::Color const divider       = 0x393939ff; /* outline_colour */
static Gtkmm2ext::Color const text          = 0xe1e1e1ff; /* canvas_text */
static Gtkmm2ext::Color const active        = 0x42a2c8ff; /* toolbar_active */

/* plugdata's Corners::objectCornerRadius */
static double const corner_radius = 2.75;

DefaultFace::DefaultFace ()
	: on (true)
	, in_window (false)
	, live (false)
{
}

void
DefaultFace::read (std::shared_ptr<PluginInsert> pi)
{
	if (!pi) {
		return;
	}
	name = Spite::device_name (pi);
	on   = pi->enabled ();
	live = Spite::has_device_face (pi);

	std::shared_ptr<Plugin> p = pi->plugin ();
	char const*             m = p ? p->maker () : 0;
	maker                     = m ? m : "";

	if (live) {
		kind = _("plugdata for Spite");
	} else if (pi->is_instrument ()) {
		kind = string_compose (_("%1 instrument"), PluginManager::plugin_type_name (pi->type ()));
	} else {
		kind = PluginManager::plugin_type_name (pi->type ());
	}
}

/* as wide as Live makes a small device: three quarters of its height, and room
 * for some of its name */
int
DefaultFace::width (int face_h, int title_h)
{
	return std::max<int> (rintf (face_h * .75f), 6 * title_h);
}

DefaultFace::Part
DefaultFace::part_at (double x, double y, double w, double h, double title_h)
{
	if (x < 0 || y < 0 || x >= w || y >= h) {
		return Outside;
	}
	if (y < title_h) {
		return x < title_h + 2 ? Led : Title;
	}
	return Body;
}

/* one line of text from x, y, at most w wide (ellipsized), and its height */
static int
show_line (cairo_t* cr, Glib::RefPtr<Pango::Context> const& ctx, Pango::FontDescription const& font, std::string const& str, double x, double y, double w, Gtkmm2ext::Color c, double alpha, bool centred = false)
{
	if (str.empty () || w < 4) {
		return 0;
	}
	Glib::RefPtr<Pango::Layout> l = Pango::Layout::create (ctx);
	l->set_font_description (font);
	l->set_text (str);
	l->set_width ((int) (w * PANGO_SCALE));
	l->set_ellipsize (Pango::ELLIPSIZE_END);
	if (centred) {
		l->set_alignment (Pango::ALIGN_CENTER);
	}
	int tw, th;
	l->get_pixel_size (tw, th);
	Gtkmm2ext::set_source_rgb_a (cr, c, alpha);
	cairo_move_to (cr, x, y);
	pango_cairo_show_layout (cr, l->gobj ());
	cairo_new_path (cr);
	return th;
}

/* the height a line of the font takes */
static int
line_height (Glib::RefPtr<Pango::Context> const& ctx, Pango::FontDescription const& font)
{
	Glib::RefPtr<Pango::Layout> l = Pango::Layout::create (ctx);
	l->set_font_description (font);
	l->set_text (X_("Ag"));
	int tw, th;
	l->get_pixel_size (tw, th);
	return th;
}

void
DefaultFace::paint (cairo_t* cr, Glib::RefPtr<Pango::Context> const& ctx, double x, double y, double w, double h, double title_h, bool hovered) const
{
	if (w < 2 || h < 2) {
		return;
	}
	double const s   = std::max (1.f, UIConfiguration::instance ().get_ui_scale ());
	double const r   = corner_radius * s;
	double const pad = rint (6 * s);

	Pango::FontDescription const name_font = UIConfiguration::instance ().get_SmallFont ();
	Pango::FontDescription const body_font = UIConfiguration::instance ().get_SmallerFont ();

	cairo_save (cr);

	/* the box */
	Gtkmm2ext::rounded_rectangle (cr, x + .5, y + .5, w - 1, h - 1, r);
	Gtkmm2ext::set_source_rgba (cr, card_bg);
	cairo_fill_preserve (cr);
	cairo_clip (cr);

	/* the title strip */
	cairo_rectangle (cr, x, y, w, title_h);
	Gtkmm2ext::set_source_rgba (cr, title_bg);
	cairo_fill (cr);
	cairo_move_to (cr, x, y + title_h - .5);
	cairo_rel_line_to (cr, w, 0);
	cairo_set_line_width (cr, 1);
	Gtkmm2ext::set_source_rgba (cr, divider);
	cairo_stroke (cr);

	/* on or off */
	double const led_r = std::max (2., rint (title_h * .2));
	cairo_arc (cr, x + title_h * .5 + 1, y + title_h * .5, led_r, 0, 2 * M_PI);
	if (on) {
		Gtkmm2ext::set_source_rgba (cr, active);
		cairo_fill (cr);
	} else {
		cairo_set_line_width (cr, 1);
		Gtkmm2ext::set_source_rgba (cr, outline_hover);
		cairo_stroke (cr);
	}

	/* the name */
	int const    nh = line_height (ctx, name_font);
	double const nx = x + title_h + 3;
	show_line (cr, ctx, name_font, name, nx, y + rint ((title_h - nh) * .5), x + w - pad - nx, text, on ? 1. : .5);

	/* the maker and the kind */
	double const body_alpha = (on ? .6 : .35) * (in_window ? .6 : 1.);
	double       ty         = y + title_h + pad;
	ty += show_line (cr, ctx, body_font, maker, x + pad, ty, w - 2 * pad, text, body_alpha);
	show_line (cr, ctx, body_font, kind, x + pad, ty, w - 2 * pad, live ? active : text, live ? body_alpha * 1.4 : body_alpha);

	if (in_window) {
		/* where it is, as the Device view's faces say */
		int const bh = line_height (ctx, body_font);
		show_line (cr, ctx, body_font, _("In its window"), x + pad, y + title_h + rint ((h - title_h - bh) * .5), w - 2 * pad, text, on ? .85 : .5, true);
	}

	cairo_restore (cr);

	/* the outline, over the title strip */
	Gtkmm2ext::rounded_rectangle (cr, x + .5, y + .5, w - 1, h - 1, r);
	cairo_set_line_width (cr, 1);
	Gtkmm2ext::set_source_rgba (cr, hovered ? outline_hover : outline);
	cairo_stroke (cr);
}
