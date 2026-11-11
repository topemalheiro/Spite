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

#ifdef WAF_BUILD
#include "gtk2ardour-config.h"
#endif

#include <algorithm>
#include <cmath>

#include <glib.h>
#include <pango/pangocairo.h>

#include <ytkmm/tooltip.h>

#include "gtkmm2ext/colors.h"

#include "mcr_bar.h"
#include "pd_theme.h"
#include "ui_config.h"

#include "pbd/i18n.h"

using PdCord::Rect;

/* plugdata's dark theme (plugdata/Source/LookAndFeel.cpp, "dark") */
static Gtkmm2ext::Color const toolbar_background = 0x191919ff; /* toolbar_background */
static Gtkmm2ext::Color const toolbar_hover      = 0x282828ff; /* toolbar_hover */
static Gtkmm2ext::Color const toolbar_outline    = 0x2f2f2fff; /* toolbar_outline_colour */
static Gtkmm2ext::Color const slider_background  = 0x232323ff; /* canvas_background (Slider::backgroundColourId) */
static Gtkmm2ext::Color const slider_track       = 0x2e2e2eff; /* levelmeter_background (Slider::trackColourId) */
static Gtkmm2ext::Color const slider_thumb       = 0xe3e3e3ff; /* levelmeter_thumb (Slider::thumbColourId) */

/* plugdata's measures (Statusbar.cpp), in its units */
static double const bar_height    = 30;  /* Statusbar::statusbarHeight */
static double const text_height   = 11;  /* DetectiveModeButton */
static double const spacing       = 10;  /* between groups */
static double const slider_width  = 120;
static double const value_width   = 34;  /* the speed, after the slider */
static double const thumb_size    = 12;  /* LookAndFeel_V4::getSliderThumbRadius */
static double const track_width   = 6;
static double const panel_width   = 250; /* plugdata's sidebars' */
static double const row_height    = 22;
static double const row_text      = 13;  /* ConnectionDebugPanel's rows */

static double
ui_scale ()
{
	return std::max (1.f, UIConfiguration::instance ().get_ui_scale ());
}

/* a layout's size in units */
static void
text_size (PangoLayout* l, double& w, double& h)
{
	PangoRectangle logical;
	pango_layout_get_extents (l, 0, &logical);
	w = logical.width / (double) PANGO_SCALE;
	h = logical.height / (double) PANGO_SCALE;
}

static double
text_width (std::string const& s, double height)
{
	PangoLayout* l = PdCord::text_layout (s, height);
	double       w;
	double       h;
	text_size (l, w, h);
	g_object_unref (l);
	return w;
}

/* text centred in a box */
static void
show_centred (cairo_t* cr, std::string const& s, double height, Rect const& r)
{
	PangoLayout* l = PdCord::text_layout (s, height);
	double       w;
	double       h;
	text_size (l, w, h);
	cairo_move_to (cr, rint (r.x0 + (r.width () - w) / 2), rint (r.y0 + (r.height () - h) / 2));
	pango_cairo_show_layout (cr, l);
	cairo_new_path (cr);
	g_object_unref (l);
}

static void
rounded (cairo_t* cr, Rect const& r, double radius)
{
	double const rad = std::min (radius, std::min (r.width (), r.height ()) / 2);
	cairo_new_sub_path (cr);
	cairo_arc (cr, r.x1 - rad, r.y0 + rad, rad, -M_PI / 2, 0);
	cairo_arc (cr, r.x1 - rad, r.y1 - rad, rad, 0, M_PI / 2);
	cairo_arc (cr, r.x0 + rad, r.y1 - rad, rad, M_PI / 2, M_PI);
	cairo_arc (cr, r.x0 + rad, r.y0 + rad, rad, M_PI, 3 * M_PI / 2);
	cairo_close_path (cr);
}

static std::string
speed_text (float s)
{
	char b[G_ASCII_DTOSTR_BUF_SIZE];
	g_ascii_formatd (b, sizeof (b), "%.1f", s);
	return std::string (b) + "\xc3\x97"; /* × */
}

/* ****************************************************************************
 * McrBar
 */

McrBar::McrBar ()
	: _hover (None)
	, _pressed (None)
	, _play (false)
	, _detective (false)
	, _speed (1)
	, _paused (false)
	, _history (false)
	, _min_width (0)
{
	add_events (Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK | Gdk::LEAVE_NOTIFY_MASK | Gdk::SCROLL_MASK);
	set_has_tooltip (true);
	set_name (X_("MCRBar"));
	place ();
}

double
McrBar::scale () const
{
	return ui_scale ();
}

void
McrBar::on_size_request (Gtk::Requisition* r)
{
	double const s = scale ();
	r->height      = (int) ceil (bar_height * s);
	/* everything with History right after Step: never the width it has now (place () puts
	 * History at its right end), or the window it is in could never be narrower again */
	r->width = (int) ceil (_min_width * s);
}

void
McrBar::on_size_allocate (Gtk::Allocation& a)
{
	Gtk::DrawingArea::on_size_allocate (a);
	place ();
}

/* plugdata's order: the modes in the corner, then the Detective's toggle and speed;
 * the history panel's toggle at the other end */
void
McrBar::place ()
{
	double const h = bar_height;
	double const y = 5;
	double const b = h - 2 * y;
	double       x = 6;

	auto button = [&] (Item i, std::string const& text, double gap) {
		double const w = std::max (44., ceil (text_width (text, text_height)) + 18);
		_rects[i]      = Rect (x, y, x + w, y + b);
		x += w + gap;
	};

	button (Patch, _("Patch"), 2);
	button (Play, _("Play"), 0);
	_seps[0] = x + spacing / 2;
	x += spacing;

	button (Detective, _("Detective"), 4);
	_rects[Speed] = Rect (x, y, x + slider_width + value_width, y + b);
	x += slider_width + value_width + spacing;

	button (Pause, _("Pause"), 4);
	button (Step, _("Step"), 0);
	_seps[1] = x + spacing / 2;
	x += spacing;

	/* at the right end, once allocated (or after the rest, until then) */
	double const ww    = get_width () > 1 ? get_width () / scale () : 0;
	double const hw    = std::max (60., ceil (text_width (_("History"), text_height)) + 18);
	double const right = std::max (x + hw, ww - 6);
	_rects[History]    = Rect (right - hw, y, right, y + b);
	_min_width         = x + hw + 6;
}

bool
McrBar::enabled (Item i) const
{
	switch (i) {
		case Speed:
		case Pause:
			return _detective;
		case Step:
			return _detective && _paused;
		default:
			return true;
	}
}

McrBar::Item
McrBar::item_at (double x, double y) const
{
	PdCord::Pt const p (x / scale (), y / scale ());
	for (int i = 0; i < n_items; ++i) {
		if (_rects[i].expand (0, 5).contains (p)) {
			return (Item) i;
		}
	}
	return None;
}

void
McrBar::set_hover (Item i)
{
	if (i != _hover) {
		_hover = i;
		queue_draw ();
	}
}

float
McrBar::speed_at (double x) const
{
	Rect const&  r = _rects[Speed];
	double const t = (x / scale () - (r.x0 + thumb_size / 2)) / (slider_width - thumb_size);
	float const  v = min_speed () + std::max (0., std::min (1., t)) * (max_speed () - min_speed ());
	return rintf (v * 10) / 10; /* steps of 0.1, as plugdata's */
}

void
McrBar::drag_speed (double x)
{
	float const v = speed_at (x);
	if (v != _speed) {
		_speed = v;
		queue_draw ();
		SpeedChanged (v);
	}
}

void
McrBar::set_play (bool yn)
{
	if (yn != _play) {
		_play = yn;
		queue_draw ();
	}
}

void
McrBar::set_detective (bool yn)
{
	if (yn != _detective) {
		_detective = yn;
		queue_draw ();
	}
}

void
McrBar::set_speed (float s)
{
	s = std::max (min_speed (), std::min (max_speed (), s));
	if (s != _speed) {
		_speed = s;
		queue_draw ();
	}
}

void
McrBar::set_paused (bool yn)
{
	if (yn != _paused) {
		_paused = yn;
		queue_draw ();
	}
}

void
McrBar::set_history (bool yn)
{
	if (yn != _history) {
		_history = yn;
		queue_draw ();
	}
}

bool
McrBar::on_button_press_event (GdkEventButton* ev)
{
	if (ev->button != 1 || ev->type != GDK_BUTTON_PRESS) {
		return true;
	}
	Item const i = item_at (ev->x, ev->y);
	if (i == None || !enabled (i)) {
		return true;
	}
	_pressed = i;
	if (i == Speed) {
		drag_speed (ev->x);
	}
	queue_draw ();
	return true;
}

bool
McrBar::on_button_release_event (GdkEventButton* ev)
{
	Item const p = _pressed;
	_pressed     = None;
	queue_draw ();
	if (ev->button != 1 || p == None || p == Speed || item_at (ev->x, ev->y) != p) {
		return true;
	}
	switch (p) {
		case Patch:
			PlayMode (false);
			break;
		case Play:
			PlayMode (true);
			break;
		case Detective:
			DetectiveClicked ();
			break;
		case Pause:
			PauseClicked ();
			break;
		case Step:
			StepClicked ();
			break;
		case History:
			HistoryClicked ();
			break;
		default:
			break;
	}
	return true;
}

bool
McrBar::on_motion_notify_event (GdkEventMotion* ev)
{
	if (_pressed == Speed) {
		drag_speed (ev->x);
		return true;
	}
	set_hover (item_at (ev->x, ev->y));
	return true;
}

bool
McrBar::on_leave_notify_event (GdkEventCrossing*)
{
	if (_pressed == None) {
		set_hover (None);
	}
	return false;
}

/* the wheel on the slider moves it a step */
bool
McrBar::on_scroll_event (GdkEventScroll* ev)
{
	if (item_at (ev->x, ev->y) != Speed || !enabled (Speed)) {
		return false;
	}
	float const v = std::max (min_speed (), std::min (max_speed (), _speed + (ev->direction == GDK_SCROLL_UP || ev->direction == GDK_SCROLL_RIGHT ? .1f : -.1f)));
	if (fabsf (v - _speed) > .001f) {
		_speed = rintf (v * 10) / 10;
		queue_draw ();
		SpeedChanged (_speed);
	}
	return true;
}

bool
McrBar::on_query_tooltip (int x, int y, bool, Glib::RefPtr<Gtk::Tooltip> const& tip)
{
	char const* t = 0;
	switch (item_at (x, y)) {
		case Patch:
			t = _("Patch mode: connect devices with cables (Ctrl+E switches)");
			break;
		case Play:
			t = _("Play mode: play the devices (Ctrl+E switches)");
			break;
		case Detective:
			t = _("Detective Mode: watch the messages and levels the cables carry");
			break;
		case Speed:
			t = _("How fast Detective Mode's messages travel");
			break;
		case Pause:
			t = _("Hold Detective Mode's messages where they are");
			break;
		case Step:
			t = _("Move held messages on a step");
			break;
		case History:
			t = _("The selected cable's messages");
			break;
		default:
			return false;
	}
	tip->set_text (t);
	return true;
}

void
McrBar::paint_button (cairo_t* cr, Item i, std::string const& text, bool active, bool on)
{
	Rect const& r = _rects[i];

	if (on && (i == _hover || i == _pressed)) {
		rounded (cr, r, 5);
		Gtkmm2ext::set_source_rgba (cr, toolbar_hover);
		cairo_fill (cr);
	}

	/* DetectiveModeButton's colours: grey when off, the active colour when on, brighter
	 * under the pointer */
	PdTheme const&   th = PdTheme::dark ();
	Gtkmm2ext::Color c  = th.toolbar_text;
	if (!on) {
		c = 0x808080ff; /* Colours::grey */
	} else if (active) {
		c = th.toolbar_active;
	} else if (i == _hover) {
		c = PdColor::brighter (th.toolbar_text, .8f);
	}
	PdColor::set_source (cr, c);
	show_centred (cr, text, text_height, r);
}

void
McrBar::paint_slider (cairo_t* cr)
{
	Rect const&  r    = _rects[Speed];
	bool const   on   = enabled (Speed);
	double const cy   = rint ((r.y0 + r.y1) / 2);
	double const x0   = r.x0 + thumb_size / 2;
	double const x1   = r.x0 + slider_width - thumb_size / 2;
	double const t    = (_speed - min_speed ()) / (max_speed () - min_speed ());
	double const kx   = x0 + t * (x1 - x0);
	double const fade = on ? 1. : .5; /* a disabled JUCE slider */

	cairo_save (cr);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_width (cr, track_width);
	cairo_move_to (cr, x0, cy);
	cairo_line_to (cr, x1, cy);
	Gtkmm2ext::set_source_rgb_a (cr, slider_background, fade);
	cairo_stroke (cr);
	cairo_move_to (cr, x0, cy);
	cairo_line_to (cr, kx, cy);
	Gtkmm2ext::set_source_rgb_a (cr, slider_track, fade);
	cairo_stroke (cr);
	cairo_arc (cr, kx, cy, thumb_size / 2, 0, 2 * M_PI);
	Gtkmm2ext::set_source_rgb_a (cr, slider_thumb, on ? (_hover == Speed || _pressed == Speed ? 1. : .9) : .4);
	cairo_fill (cr);
	cairo_restore (cr);

	PdTheme const& th = PdTheme::dark ();
	PdColor::set_source (cr, th.toolbar_text, on ? .8 : .4);
	show_centred (cr, speed_text (_speed), text_height, Rect (r.x0 + slider_width, r.y0, r.x1, r.y1));
}

bool
McrBar::on_expose_event (GdkEventExpose* ev)
{
	cairo_t* cr = gdk_cairo_create (ev->window);
	gdk_cairo_region (cr, ev->region);
	cairo_clip (cr);

	double const s = scale ();
	double const w = get_width () / s;
	double const h = get_height () / s;
	cairo_scale (cr, s, s);

	Gtkmm2ext::set_source_rgba (cr, toolbar_background);
	cairo_paint (cr);

	/* plugdata's outline over the bar, and between its groups */
	cairo_set_line_width (cr, 1);
	Gtkmm2ext::set_source_rgba (cr, toolbar_outline);
	cairo_move_to (cr, 0, .5);
	cairo_line_to (cr, w, .5);
	for (int i = 0; i < 2; ++i) {
		cairo_move_to (cr, rint (_seps[i]) + .5, 9);
		cairo_line_to (cr, rint (_seps[i]) + .5, h - 9);
	}
	cairo_stroke (cr);

	paint_button (cr, Patch, _("Patch"), !_play, true);
	paint_button (cr, Play, _("Play"), _play, true);
	paint_button (cr, Detective, _("Detective"), _detective, true);
	paint_slider (cr);
	paint_button (cr, Pause, _("Pause"), _paused, enabled (Pause));
	paint_button (cr, Step, _("Step"), false, enabled (Step));
	paint_button (cr, History, _("History"), _history, true);

	cairo_destroy (cr);
	return true;
}

/* ****************************************************************************
 * McrHistoryPanel
 */

McrHistoryPanel::McrHistoryPanel ()
	: _first (0)
{
	add_events (Gdk::SCROLL_MASK);
	set_name (X_("MCRHistory"));
}

double
McrHistoryPanel::scale () const
{
	return ui_scale ();
}

void
McrHistoryPanel::on_size_request (Gtk::Requisition* r)
{
	r->width  = (int) ceil (panel_width * scale ());
	r->height = (int) ceil (4 * row_height * scale ());
}

void
McrHistoryPanel::set_history (std::string const& title, std::vector<std::string> const& rows)
{
	if (title != _title) {
		_first = 0; /* another cable: from its newest */
	}
	_title = title;
	_rows  = rows;
	_first = std::max (0, std::min (_first, (int) _rows.size () - 1));
	queue_draw ();
}

bool
McrHistoryPanel::on_scroll_event (GdkEventScroll* ev)
{
	int const n = _first + (ev->direction == GDK_SCROLL_UP ? -3 : ev->direction == GDK_SCROLL_DOWN ? 3 : 0);
	int const f = std::max (0, std::min (n, (int) _rows.size () - 1));
	if (f != _first) {
		_first = f;
		queue_draw ();
	}
	return true;
}

/* ConnectionDebugPanel: its title, then the messages, newest first */
bool
McrHistoryPanel::on_expose_event (GdkEventExpose* ev)
{
	cairo_t* cr = gdk_cairo_create (ev->window);
	gdk_cairo_region (cr, ev->region);
	cairo_clip (cr);

	PdTheme const& th = PdTheme::dark ();
	double const   s  = scale ();
	double const   w  = get_width () / s;
	double const   h  = get_height () / s;
	cairo_scale (cr, s, s);

	PdColor::set_source (cr, th.panel_background);
	cairo_paint (cr);
	cairo_set_line_width (cr, 1);
	Gtkmm2ext::set_source_rgba (cr, toolbar_outline);
	cairo_move_to (cr, .5, 0);
	cairo_line_to (cr, .5, h);
	cairo_stroke (cr);

	double const pad = 8;
	double       y   = pad;

	PangoLayout* l = PdCord::text_layout (_("Debug Messages"), 16);
	double       tw;
	double       tht;
	text_size (l, tw, tht);
	PdColor::set_source (cr, th.panel_text);
	cairo_move_to (cr, pad, y);
	pango_cairo_show_layout (cr, l);
	g_object_unref (l);
	y += 24 + 2;

	/* which cable */
	l = PdCord::text_layout (_title.empty () ? std::string (_("Select a cable to see its messages")) : _title, 12);
	pango_layout_set_width (l, (int) ((w - 2 * pad) * PANGO_SCALE));
	pango_layout_set_ellipsize (l, PANGO_ELLIPSIZE_END);
	PdColor::set_source (cr, _title.empty () ? PdColor::interpolated (th.panel_text, th.panel_background, .45f) : th.toolbar_active);
	cairo_move_to (cr, pad, y);
	pango_cairo_show_layout (cr, l);
	g_object_unref (l);
	y += 18 + pad;

	if (!_title.empty ()) {
		for (size_t i = _first; i < _rows.size () && y + row_height <= h; ++i) {
			l = PdCord::text_layout (_rows[i], row_text);
			pango_layout_set_width (l, (int) ((w - 2 * pad - 8) * PANGO_SCALE));
			pango_layout_set_ellipsize (l, PANGO_ELLIPSIZE_END);
			text_size (l, tw, tht);
			PdColor::set_source (cr, th.panel_text);
			cairo_move_to (cr, pad + 4, rint (y + (row_height - tht) / 2));
			pango_cairo_show_layout (cr, l);
			g_object_unref (l);
			y += row_height;
		}
	}

	cairo_destroy (cr);
	return true;
}
