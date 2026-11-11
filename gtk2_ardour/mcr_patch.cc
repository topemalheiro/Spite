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
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <tuple>

#include <glib.h>
#include <glibmm/main.h>
#include <pango/pangocairo.h>

#include "pbd/controllable.h"
#include "pbd/id.h"

#include "ardour/audioengine.h"
#include "ardour/dB.h"
#include "ardour/data_type.h"
#include "ardour/gain_control.h"
#include "ardour/plugin_insert.h"
#include "ardour/route.h"
#include "ardour/send.h"

#include "mcr_cables.h"
#include "mcr_patch.h"

#include "pbd/i18n.h"

using PdCord::Path;
using PdCord::Pt;
using PdCord::Rect;

namespace {

/* the layer's measures, in plugdata units */
double const min_open      = 24;  /* an open sliver dragged smaller than this closes */
double const max_open      = 600;
double const lane_step     = 3;   /* between a closed sliver's three lanes */
double const stub_drop     = 16;  /* an open sliver's stubs: their first lane, from the edge they hang from */
double const stub_step     = 13;  /* between an open sliver's stub lanes */
double const letter_height = 8;   /* tiny caps lettering */
double const glyph_size    = 7;
double const last_room     = 44;  /* Detective Mode: room after a stub's name for the last message */
double const edge_reach    = 4;   /* an open sliver's bottom edge, for dragging */
double const snap_reach    = 20;  /* plugdata's (Iolet::findNearestIolet, Iolet.cpp:330-356) */
double const note_height   = 13;  /* a note's text, as the Detective's bubbles (Connection.cpp:32-38) */
double const note_pad_x    = 8;
double const note_pad_y    = 6;
double const note_radius   = 6;
double const note_offset   = 10;
double const label_height  = 9;   /* a jack's name, under the pointer */

int64_t const spring_us       = 300000; /* a dragged cord resting on a closed sliver opens it */
int64_t const blink_us        = 250000; /* a folded cord's glyphs, after a message */
int64_t const fade_us         = 300000; /* a note fades out */
int64_t const double_click_us = 400000;
int64_t const wheel_gesture_us = 400000; /* wheel steps this close keep scrolling the canvas */

uint32_t const no_pin = McrPatchJack::no_pin;

std::string
upper (std::string const& s)
{
	std::string u (s);
	for (auto& c : u) {
		c = toupper ((unsigned char) c);
	}
	return u;
}

/* a device's short name for lettering: "ACE Compressor" -> "COMP" */
std::string
abbreviate (std::string const& name)
{
	static char const* const vendors[] = { "ACE", "A", "X42", "LSP", "CALF", "ZAM", "TAL", "GX", "PLUGDATA", 0 };
	static char const* const words[][2] = {
		{ "COMPRESSOR", "COMP" }, { "COMP", "COMP" }, { "REVERB", "VERB" }, { "DELAY", "DLY" }, { "ECHO", "ECHO" },
		{ "EQUALIZER", "EQ" }, { "EQUALISER", "EQ" }, { "EQ", "EQ" }, { "SYNTHESIZER", "SYNTH" }, { "SYNTH", "SYNTH" },
		{ "SAMPLER", "SMPL" }, { "FILTER", "FILT" }, { "LIMITER", "LIM" }, { "GATE", "GATE" }, { "EXPANDER", "EXP" },
		{ "CHORUS", "CHOR" }, { "FLANGER", "FLNG" }, { "PHASER", "PHSR" }, { "DISTORTION", "DIST" }, { "OVERDRIVE", "OD" },
		{ "AMPLIFIER", "AMP" }, { "AMP", "AMP" }, { "DRUMS", "DRUM" }, { "DRUM", "DRUM" }, { "VOCODER", "VOC" },
		{ 0, 0 }
	};

	std::vector<std::string> ws;
	std::string w;
	for (char c : name) {
		if (isalnum ((unsigned char) c)) {
			w += toupper ((unsigned char) c);
		} else if (!w.empty ()) {
			ws.push_back (w);
			w.clear ();
		}
	}
	if (!w.empty ()) {
		ws.push_back (w);
	}

	for (auto const& x : ws) {
		for (int i = 0; words[i][0]; ++i) {
			if (x == words[i][0]) {
				return words[i][1];
			}
		}
	}
	for (auto const& x : ws) {
		if (x == "PLUGDATA" || x == "PGD4S") {
			return "PD"; /* "plugdata for Spite" */
		}
	}
	for (auto const& x : ws) {
		bool vendor = false;
		for (int i = 0; vendors[i] && !vendor; ++i) {
			vendor = x == vendors[i];
		}
		if (!vendor) {
			return x.substr (0, 5);
		}
	}
	return ws.empty () ? std::string ("DEV") : ws.front ().substr (0, 5);
}

/* a jack's short name for lettering: "SC", "IN3" (PgD4S's [adc~ 3]), "OUT1", "MIDI" */
std::string
jack_tag (McrPatchJack const& j, std::vector<McrPatchJack> const& all)
{
	if (j.pin == no_pin) {
		return "?";
	}

	int n   = 0; /* jacks like it */
	int nth = 0; /* its place among them */
	for (auto const& o : all) {
		if (o.input != j.input || o.type != j.type || (j.type == McrPatchJack::Audio && o.sidechain != j.sidechain)) {
			continue;
		}
		++n;
		if (o.instance < j.instance || (o.instance == j.instance && o.pin < j.pin)) {
			++nth;
		}
	}

	char b[32];
	if (j.type == McrPatchJack::Midi) {
		if (n < 2) {
			return "MIDI";
		}
		snprintf (b, sizeof (b), "MIDI%d", nth + 1);
		return b;
	}
	if (j.input && j.sidechain && n <= 2) {
		if (n < 2) {
			return "SC";
		}
		snprintf (b, sizeof (b), "SC%d", nth + 1);
		return b;
	}
	snprintf (b, sizeof (b), "%s%u", j.input ? "IN" : "OUT", j.pin + 1);
	return b;
}

/* tiny caps lettering in plugdata's font, spaced out a little */
PangoLayout*
lettering (std::string const& text)
{
	PangoLayout*   l = PdCord::text_layout (text, letter_height);
	PangoAttrList* a = pango_attr_list_new ();
	pango_attr_list_insert (a, pango_attr_letter_spacing_new ((int) (0.5 * PANGO_SCALE)));
	pango_layout_set_attributes (l, a);
	pango_attr_list_unref (a);
	return l;
}

void
layout_size (PangoLayout* l, double& w, double& h)
{
	PangoRectangle ink;
	PangoRectangle logical;
	pango_layout_get_extents (l, &ink, &logical);
	w = logical.width / (double) PANGO_SCALE;
	h = logical.height / (double) PANGO_SCALE;
}

double
lettering_width (std::string const& text)
{
	PangoLayout* l = lettering (text);
	double       w;
	double       h;
	layout_size (l, w, h);
	g_object_unref (l);
	return w;
}

/* a layout from x, centred on y */
void
show (cairo_t* cr, PangoLayout* l, double x, double y)
{
	double w;
	double h;
	layout_size (l, w, h);
	cairo_move_to (cr, x, y - h / 2);
	pango_cairo_show_layout (cr, l);
	cairo_new_path (cr);
}

void
rounded (cairo_t* cr, Rect const& r, double radius)
{
	double const rad = std::max (0., std::min (radius, std::min (r.width (), r.height ()) / 2));
	cairo_new_sub_path (cr);
	cairo_arc (cr, r.x1 - rad, r.y0 + rad, rad, -M_PI / 2, 0);
	cairo_arc (cr, r.x1 - rad, r.y1 - rad, rad, 0, M_PI / 2);
	cairo_arc (cr, r.x0 + rad, r.y1 - rad, rad, M_PI / 2, M_PI);
	cairo_arc (cr, r.x0 + rad, r.y0 + rad, rad, M_PI, 3 * M_PI / 2);
	cairo_close_path (cr);
}

/* "+3" on a pill as tall as a small jack */
Rect
pill_rect (Pt centre, std::string const& text)
{
	return Rect::around (centre, std::max (lettering_width (text) + 7, 13.), 11);
}

void
draw_pill (cairo_t* cr, Rect const& r, std::string const& text, PdTheme const& th, double alpha = 1)
{
	cairo_save (cr);
	cairo_new_path (cr);
	rounded (cr, r, r.height () / 2);
	PdColor::set_source (cr, th.object_outline, alpha);
	cairo_fill (cr);
	PangoLayout* l = lettering (text);
	double       w;
	double       h;
	layout_size (l, w, h);
	PdColor::set_source (cr, th.canvas_text, alpha);
	show (cr, l, r.centre ().x - w / 2, r.centre ().y);
	g_object_unref (l);
	cairo_restore (cr);
}

double
segment_distance (Pt a, Pt b, Pt p)
{
	Pt const     d    = b - a;
	double const len2 = d.x * d.x + d.y * d.y;
	double       t    = len2 > 0 ? ((p.x - a.x) * d.x + (p.y - a.y) * d.y) / len2 : 0;
	t                 = std::max (0., std::min (1., t));
	return p.distance (a + d * t);
}

/* at most n characters of UTF-8 text, marked when cut */
std::string
clip (std::string const& s, size_t n)
{
	size_t chars = 0;
	for (size_t i = 0; i < s.size (); ++i) {
		if (((unsigned char) s[i] & 0xc0) != 0x80) {
			if (chars == n) {
				return s.substr (0, i) + "\xe2\x80\xa6";
			}
			++chars;
		}
	}
	return s;
}

PdCord::Kind
kind_of (McrPatchJack::Type t)
{
	return t == McrPatchJack::Midi ? PdCord::Data : PdCord::Signal;
}

} // namespace

/* ---- identities ---- */

bool
McrPatch::JackId::operator< (JackId const& o) const
{
	return std::tie (device, type, input, instance, pin) < std::tie (o.device, o.type, o.input, o.instance, o.pin);
}

bool
McrPatch::JackId::operator== (JackId const& o) const
{
	return device == o.device && type == o.type && input == o.input && instance == o.instance && pin == o.pin;
}

bool
McrPatch::Key::operator< (Key const& o) const
{
	return std::tie (source_row, source, target_row, target, type, source_instance, source_pin, target_instance, target_pin)
	       < std::tie (o.source_row, o.source, o.target_row, o.target, o.type, o.source_instance, o.source_pin, o.target_instance, o.target_pin);
}

bool
McrPatch::Key::operator== (Key const& o) const
{
	return !(*this < o) && !(o < *this);
}

McrPatch::Key
McrPatch::key_of (McrPatchCable const& c)
{
	Key k;
	k.source_row      = c.source_row;
	k.source          = c.source;
	k.target_row      = c.target_row;
	k.target          = c.target;
	k.type            = c.type;
	k.source_instance = c.source_instance;
	k.source_pin      = c.source_pin;
	k.target_instance = c.target_instance;
	k.target_pin      = c.target_pin;
	return k;
}

McrPatch::JackId
McrPatch::jack_id (Device const& d, Jack const& j) const
{
	JackId id;
	id.device   = d.id;
	id.type     = j.pin.type;
	id.input    = j.pin.input;
	id.instance = j.pin.instance;
	id.pin      = j.pin.pin;
	return id;
}

McrPatchEnd
McrPatch::end_of (Device const& d, Jack const& j) const
{
	McrPatchEnd e;
	e.device   = d.id;
	e.insert   = d.insert;
	e.type     = j.pin.type;
	e.instance = j.pin.instance;
	e.pin      = j.pin.pin;
	return e;
}

/* ---- the layer ---- */

McrPatch::McrPatch ()
	: _engine (0)
	, _theme (&PdTheme::dark ())
	, _scale (1)
	, _play (false)
	, _selected (-1)
	, _hover_cord (-1)
	, _hover_device (-1)
	, _hover_jack (-1)
	, _hover_edge (-1)
	, _hover_chevron (-1)
	, _pointer_in (false)
	, _cursor (Arrow)
	, _press_sliver (-1)
	, _opened_sliver (-1)
	, _opened_at (0)
	, _wheel_scrolled (0)
	, _last_tick (0)
	, _relayout (false)
{
	/* plugdata starts its Detective on; the MCR's toggle starts this one */
	_detective.set_enabled (false);
	_clock = sigc::ptr_fun (&g_get_monotonic_time);
}

McrPatch::~McrPatch ()
{
	_timer.disconnect ();
	_engine_changed.disconnect ();
}

void
McrPatch::set_theme (PdTheme const& th)
{
	_theme = &th;
	rebuild ();
	redraw_all ();
	flush ();
}

void
McrPatch::set_clock (sigc::slot<int64_t> const& c)
{
	_clock = c;
}

int64_t
McrPatch::now () const
{
	return _clock ();
}

/* ---- input ---- */

void
McrPatch::set_engine (McrPatchEngine* e)
{
	if (_drag.on) {
		end_drag (false);
	}
	_engine_changed.disconnect ();
	_engine = e;
	_pins.clear ();
	_cables.clear ();
	for (auto& w : _watches) {
		w.second.tap.reset ();
	}

	if (_engine) {
		_engine_changed = _engine->Changed.connect (sigc::mem_fun (*this, &McrPatch::engine_changed));
		engine_changed ();
	} else {
		/* no cords: their Detective handles go too */
		rebuild ();
		redraw_all ();
		flush ();
	}
}

void
McrPatch::engine_changed ()
{
	/* the pin maps change with the cables (a jack's "wired") */
	_pins.clear ();
	_cables = _engine ? _engine->cables () : std::vector<McrPatchCable> ();
	/* a cable's record is new: so are its taps */
	for (auto& w : _watches) {
		w.second.tap.reset ();
	}
	rebuild ();
	if (_pointer_in && !_drag.on && !_stretch.on) {
		hover (_pointer);
	}
	redraw_all ();
	flush ();
}

void
McrPatch::set_model (McrPatchModel const& m)
{
	double const scale = m.scale > 0 ? m.scale : 1;
	if (scale != _scale && m.rows.size () > 1) {
		/* the slivers' heights in pixels changed with it */
		_relayout = true;
	}
	_scale = scale;
	_view  = to_units (_view_px);

	_rows.clear ();
	_devices.clear ();
	_device_index.clear ();

	for (size_t i = 0; i < m.rows.size (); ++i) {
		McrPatchRow const& r = m.rows[i];
		Row                row;
		row.id     = r.id;
		row.number = r.number > 0 ? r.number : (int) i + 1;
		row.name   = r.name;
		row.rect   = to_units (r.rect);
		row.header = to_units (r.header);
		_rows.push_back (row);
	}

	std::set<uint64_t> ids;
	for (auto const& d : m.devices) {
		if (!d.id || d.row < 0 || d.row >= (int) _rows.size () || !ids.insert (d.id).second) {
			continue;
		}
		Device dev;
		dev.id     = d.id;
		dev.row    = d.row;
		dev.insert = d.insert;
		dev.name   = d.name;
		dev.tag    = d.tag.empty () ? abbreviate (d.name) : upper (d.tag);
		dev.rect   = to_units (d.rect);
		_device_index[d.id] = _devices.size ();
		_devices.push_back (dev);
	}

	/* each track's header: the track itself, as the source of a send made elsewhere
	 * with no device before it; its jack sits at the header's right end, next to
	 * the devices, clear of the slivers' toggles at the left */
	for (size_t i = 0; i < _rows.size (); ++i) {
		Rect const& h = _rows[i].header;
		Device      dev;
		dev.row        = i;
		dev.name       = _rows[i].name;
		dev.tag        = "TRK";
		dev.rect       = Rect (std::max (h.x0, h.x1 - 24), h.y0, h.x1, h.y1);
		_rows[i].track = _devices.size ();
		_devices.push_back (dev);
	}

	for (auto i = _pins.begin (); i != _pins.end ();) {
		if (ids.count (i->first)) {
			++i;
		} else {
			_pins.erase (i++);
		}
	}

	rebuild ();
	if (_pointer_in && !_drag.on && !_stretch.on) {
		hover (_pointer);
	}
	redraw_all ();
	flush ();
}

void
McrPatch::set_visible (Rect const& r)
{
	_view_px = r;
	_view    = to_units (r);
}

void
McrPatch::set_play_mode (bool yn)
{
	if (_drag.on) {
		end_drag (false);
	}
	_stretch.on    = false;
	_press_sliver = -1;
	_play         = yn;

	/* Play mode closes the slivers not pinned open; Patch mode opens those with cords */
	for (size_t i = 0; i < _slivers.size (); ++i) {
		SliverState& st   = _sliver_state[_slivers[i].row];
		bool const   want = yn ? (st.open && st.pinned) : (st.open || faces_cord (i));
		if (want != st.open) {
			st.open   = want;
			_relayout = true;
		}
		st.sprung = false;
	}
	_known.clear ();
	if (!yn) {
		for (auto const& c : _cords) {
			_known.insert (c.key);
		}
	}

	_hover_cord = _hover_device = _hover_jack = _hover_edge = _hover_chevron = -1;
	_cursor     = Arrow;
	rebuild ();
	if (_pointer_in) {
		hover (_pointer);
	}
	redraw_all ();
	flush ();
}

void
McrPatch::jacks_changed ()
{
	_pins.clear ();
	rebuild ();
	redraw_all ();
	flush ();
}

/* ---- layout ---- */

void
McrPatch::rebuild ()
{
	/* each device's pins, as the engine gives them */
	for (auto& d : _devices) {
		d.jacks.clear ();
		d.hidden[0] = d.hidden[1] = 0;
		d.more[0] = d.more[1] = Rect ();
		if (!d.id) {
			continue;
		}
		auto i = _pins.find (d.id);
		if (i == _pins.end ()) {
			std::vector<McrPatchJack> pins;
			if (_engine) {
				McrPatchDevice md;
				md.id     = d.id;
				md.row    = d.row;
				md.insert = d.insert;
				md.name   = d.name;
				md.tag    = d.tag;
				md.rect   = to_px (d.rect);
				pins      = _engine->jacks (md);
			}
			i = _pins.insert (std::make_pair (d.id, pins)).first;
		}
		for (auto const& p : i->second) {
			Jack j;
			j.pin = p;
			j.tag = jack_tag (p, i->second);
			d.jacks.push_back (j);
		}
	}

	resolve_cords ();

	/* a drag: its jack, and the edges the jacks that could take its cord face it from */
	if (_drag.on) {
		_drag.device = _drag.jack = -1;
		_drag.snap_device = _drag.snap_jack = -1;
		for (size_t di = 0; di < _devices.size () && _drag.device < 0; ++di) {
			Device const& d = _devices[di];
			if (d.id != _drag.source.device) {
				continue;
			}
			for (size_t ji = 0; ji < d.jacks.size (); ++ji) {
				if (jack_id (d, d.jacks[ji]) == _drag.source) {
					_drag.device = di;
					_drag.jack   = ji;
					break;
				}
			}
		}

		if (_drag.device < 0) {
			/* its device went */
			_drag = Drag ();
			_refusals.clear ();
			_cursor = Arrow;
		} else {
			Device const& s = _devices[_drag.device];
			for (size_t di = 0; di < _devices.size (); ++di) {
				Device& d = _devices[di];
				if (!d.id || (int) di == _drag.device) {
					continue;
				}
				for (auto& j : d.jacks) {
					auto r = _refusals.find (jack_id (d, j));
					if (r == _refusals.end ()) {
						continue;
					}
					j.candidate = true;
					j.refusal   = r->second;
					if (!j.refusal.empty ()) {
						/* it can't take the cord: it stays where it is, greyed */
						continue;
					}
					if (d.row == s.row) {
						j.drag_edge = 0;
					} else if (_drag.from_input) {
						j.drag_edge = PdCord::jack_on_top (false, d.rect, Pt (s.rect.x0, s.rect.y0)) ? 1 : 0;
					} else {
						j.drag_edge = PdCord::jack_on_top (true, d.rect, Pt (s.rect.x0, s.rect.y1)) ? 1 : 0;
					}
				}
			}
			_devices[_drag.device].jacks[_drag.jack].drag_edge = _drag.edge;
		}
	}

	place_jacks ();
	place_ends ();
	place_slivers ();
	auto_open ();
	sync_watches ();
	restore_selection ();

	_hover_cord = _hover_device = _hover_jack = _hover_edge = _hover_chevron = -1;
}

int
McrPatch::find_jack (int device, bool input, McrPatchJack::Type type, uint32_t instance, uint32_t pin)
{
	Device& d = _devices[device];
	for (size_t i = 0; i < d.jacks.size (); ++i) {
		McrPatchJack const& p = d.jacks[i].pin;
		if (p.input == input && p.type == type && p.instance == instance && p.pin == pin) {
			return i;
		}
	}

	/* a pin the engine doesn't list: the track itself sending, or a cable that reaches no pin */
	Jack j;
	j.pin.type     = type;
	j.pin.input    = input;
	j.pin.instance = instance;
	j.pin.pin      = pin;
	j.pin.name     = d.name;
	j.tag          = d.id ? jack_tag (j.pin, std::vector<McrPatchJack> (1, j.pin)) : std::string ();
	d.jacks.push_back (j);
	return d.jacks.size () - 1;
}

void
McrPatch::resolve_cords ()
{
	_cords.clear ();

	for (auto const& c : _cables) {
		int s = -1;
		if (c.source) {
			auto i = _device_index.find (c.source);
			if (i != _device_index.end ()) {
				s = i->second;
			}
		} else {
			for (auto const& r : _rows) {
				if (r.id == c.source_row) {
					s = r.track;
					break;
				}
			}
		}
		auto t = _device_index.find (c.target);
		if (s < 0 || t == _device_index.end ()) {
			/* not in the MCR: a bus, a hidden track */
			continue;
		}

		Cord cord;
		cord.key       = key_of (c);
		cord.cable     = c;
		cord.kind      = kind_of (c.type);
		cord.device[0] = s;
		cord.device[1] = t->second;
		cord.jack[0]   = find_jack (s, false, c.type, c.source_instance, c.source_pin);
		cord.jack[1]   = find_jack (t->second, true, c.type, c.target_instance, c.target_pin);
		_cords.push_back (cord);
	}

	/* the flip: each end sits on the edge of its face that faces the other end
	 * (judged from where the other jack is before flipping); in one row, a U below */
	for (auto& c : _cords) {
		Device const& s = _devices[c.device[0]];
		Device const& t = _devices[c.device[1]];
		if (s.row == t.row) {
			c.top[0] = c.top[1] = false;
		} else {
			c.top[0] = PdCord::jack_on_top (false, s.rect, Pt (t.rect.x0, t.rect.y0));
			c.top[1] = PdCord::jack_on_top (true, t.rect, Pt (s.rect.x0, s.rect.y1));
		}
		_devices[c.device[0]].jacks[c.jack[0]].cords[c.top[0]]++;
		_devices[c.device[1]].jacks[c.jack[1]].cords[c.top[1]]++;
	}
}

/* how many jacks fit on an edge of a face, laid out as plugdata lays them out, with room between them */
int
McrPatch::fit (Rect const& face) const
{
	int n = 1;
	while (n < 64) {
		std::vector<Rect> const b = PdCord::jack_boxes (face, n + 1, true, n + 1);

		bool roomy = true;
		for (size_t i = 1; i < b.size () && roomy; ++i) {
			roomy = b[i].x0 >= b[i - 1].x1 + 4;
		}
		if (!roomy) {
			break;
		}
		++n;
	}
	return n;
}

void
McrPatch::place_jacks ()
{
	for (size_t di = 0; di < _devices.size (); ++di) {
		Device& d = _devices[di];
		d.hidden[0] = d.hidden[1] = 0;
		if (d.jacks.empty ()) {
			continue;
		}

		int const room = fit (d.rect);

		/* the jacks each edge shows, in their order along it; drag: as a drag has them, else as they are at rest */
		auto choose = [&] (bool drag, std::vector<int>* keep, int* hidden) {
			for (int e = 0; e < 2; ++e) {
				std::vector<int> optional;
				keep[e].clear ();
				hidden[e] = 0;

				for (size_t j = 0; j < d.jacks.size (); ++j) {
					Jack const& k      = d.jacks[j];
					bool const  source = drag && _drag.on && (int) di == _drag.device && (int) j == _drag.jack;
					int const   home   = k.pin.input ? 1 : 0;

					if (k.cords[e] || (source && _drag.edge == e)) {
						keep[e].push_back (j);
						continue;
					}
					if (source) {
						continue;
					}
					if (drag && k.drag_edge >= 0) {
						/* a drag's candidate faces the jack the cord comes from */
						if (k.drag_edge == e) {
							optional.push_back (j);
						}
						continue;
					}
					/* a jack with cords shows where they are: it slid off its own edge when they all flipped */
					if (!k.cords[0] && !k.cords[1] && home == e) {
						optional.push_back (j);
					}
				}

				if (keep[e].size () + optional.size () <= (size_t) room) {
					keep[e].insert (keep[e].end (), optional.begin (), optional.end ());
				} else {
					/* crowded: the jacks the track uses, one free jack of each type, then "+N" */
					std::set<int> types;
					for (int j : optional) {
						Jack const& k        = d.jacks[j];
						bool const  usable   = !drag || !k.candidate || k.refusal.empty ();
						bool const  free_one = k.pin.cables && !k.cords[0] && !k.cords[1] && usable && types.insert (k.pin.type).second;
						if (k.pin.wired || free_one) {
							keep[e].push_back (j);
						} else {
							++hidden[e];
						}
					}
				}

				/* the edge's own jacks first, in their order, then those flipped to it */
				std::sort (keep[e].begin (), keep[e].end (), [&d, e] (int a, int b) {
					bool const fa = (d.jacks[a].pin.input ? 1 : 0) != e;
					bool const fb = (d.jacks[b].pin.input ? 1 : 0) != e;
					return fa != fb ? !fa : a < b;
				});
			}
		};

		std::vector<int> keep[2];
		int              hidden[2];
		choose (true, keep, hidden);

		/* during a drag, an edge that only lost jacks (they slid off to face the cord) keeps
		 * the places it has at rest, so that what stays on it doesn't move */
		std::vector<int> rest[2];
		int              rest_hidden[2] = { 0, 0 };
		bool             stable[2]      = { false, false };
		if (_drag.on) {
			choose (false, rest, rest_hidden);
			for (int e = 0; e < 2; ++e) {
				stable[e] = !hidden[e] || rest_hidden[e];
				for (size_t i = 0; i < keep[e].size () && stable[e]; ++i) {
					stable[e] = std::find (rest[e].begin (), rest[e].end (), keep[e][i]) != rest[e].end ();
				}
			}
		}

		int n[2];
		int m[2];
		for (int e = 0; e < 2; ++e) {
			n[e] = keep[e].size () + (hidden[e] ? 1 : 0);
			m[e] = rest[e].size () + (rest_hidden[e] ? 1 : 0);
		}

		for (int e = 0; e < 2; ++e) {
			d.hidden[e] = hidden[e];
			if (!(stable[e] ? m[e] : n[e])) {
				continue;
			}
			std::vector<Rect> const boxes = stable[e] ? PdCord::jack_boxes (d.rect, m[e], e == 1, m[1 - e]) : PdCord::jack_boxes (d.rect, n[e], e == 1, n[1 - e]);
			std::vector<int> const& order = stable[e] ? rest[e] : keep[e];
			for (int j : keep[e]) {
				size_t const i = std::find (order.begin (), order.end (), j) - order.begin ();
				Jack&        k = d.jacks[j];
				k.shown[e]     = true;
				k.box[e]       = boxes[i];
			}
			if (hidden[e]) {
				char b[16];
				snprintf (b, sizeof (b), "+%d", hidden[e]);
				d.more[e] = pill_rect (boxes.back ().centre (), b);
			}
		}
	}
}

int
McrPatch::sliver_facing (int row, bool top) const
{
	if (top) {
		return row - 1; /* -1 above the first row */
	}
	return row + 1 < (int) _rows.size () ? row : -1;
}

void
McrPatch::place_ends ()
{
	for (auto& c : _cords) {
		for (int k = 0; k < 2; ++k) {
			Device const& d = _devices[c.device[k]];
			c.at[k]         = d.jacks[c.jack[k]].box[c.top[k]].centre ();
			c.sliver[k]     = sliver_facing (d.row, c.top[k]);
		}
		c.path = Path::between (c.at[0], c.top[0], c.at[1], c.top[1], _theme->straight_connections);
	}
}

std::string
McrPatch::far_end (Cord const& c, int end) const
{
	int const     o = 1 - end;
	Device const& d = _devices[c.device[o]];
	Jack const&   j = d.jacks[c.jack[o]];

	std::string s (c.at[o].y > c.at[end].y ? "\xe2\x86\x93" : "\xe2\x86\x91"); /* ↓, ↑ */
	char        b[16];
	snprintf (b, sizeof (b), "%d ", _rows[d.row].number);
	s += b;
	s += d.tag;
	if (!j.tag.empty ()) {
		s += ' ';
		s += j.tag;
	}
	return s;
}

void
McrPatch::place_slivers ()
{
	_slivers.clear ();

	for (size_t i = 0; i + 1 < _rows.size (); ++i) {
		Rect const& a = _rows[i].rect;
		Rect const& b = _rows[i + 1].rect;

		Sliver s;
		s.row  = _rows[i].id;
		s.band = Rect (std::min (a.x0, b.x0), a.y1, std::max (a.x1, b.x1), std::max (a.y1, b.y0));

		auto st = _sliver_state.find (s.row);
		s.open  = st != _sliver_state.end () && st->second.open;

		double const cy = s.open ? s.band.y0 + std::min (8., s.band.height () / 2) : (s.band.y0 + s.band.y1) / 2;
		s.chevron       = Rect::around (Pt (s.band.x0 + 8, cy), 12, std::max (8., std::min (12., s.band.height ())));
		_slivers.push_back (s);
	}

	/* the folded cords' marks */
	for (size_t ci = 0; ci < _cords.size (); ++ci) {
		Cord&      c     = _cords[ci];
		bool const open0 = c.sliver[0] < 0 || _slivers[c.sliver[0]].open;
		bool const open1 = c.sliver[1] < 0 || _slivers[c.sliver[1]].open;

		c.folded = !(open0 && open1);
		if (!c.folded) {
			continue;
		}

		if (c.sliver[0] == c.sliver[1]) {
			Part p;
			p.kind    = Part::Flat;
			p.cord    = ci;
			p.jack[0] = c.at[0];
			p.jack[1] = c.at[1];
			p.x0      = std::min (c.at[0].x, c.at[1].x) - glyph_size;
			p.x1      = std::max (c.at[0].x, c.at[1].x) + glyph_size;
			_slivers[c.sliver[0]].parts.push_back (p);
			continue;
		}

		for (int k = 0; k < 2; ++k) {
			if (c.sliver[k] < 0) {
				continue;
			}
			Part p;
			p.kind     = Part::Stub;
			p.cord     = ci;
			p.end      = k;
			p.jack[0]  = p.jack[1] = c.at[k];
			p.text     = far_end (c, k);
			p.text_w   = lettering_width (p.text);
			p.near_top = !c.top[k]; /* a bottom edge's stub hangs into the sliver below, from its top */
			p.x0       = c.at[k].x - glyph_size;
			p.x1       = c.at[k].x + glyph_size / 2 + 3 + p.text_w + 3 + (_detective.enabled () ? last_room : 0);
			_slivers[c.sliver[k]].parts.push_back (p);
		}
	}

	for (auto& s : _slivers) {
		place_lanes (s);
	}

	/* what each cord draws over */
	for (auto& c : _cords) {
		c.bounds = c.folded ? Rect () : c.path.bounds ().expand (8);
	}
	for (auto const& s : _slivers) {
		for (auto const& p : s.parts) {
			Rect r = Rect::spanning (p.jack[0], Pt (p.jack[0].x, p.lane)).expand (4);
			r      = r.unite (Rect::spanning (p.jack[1], Pt (p.jack[1].x, p.lane)).expand (4));
			r      = r.unite (Rect (p.x0, p.lane - 7, p.x1 + last_room, p.lane + 7));

			_cords[p.cord].bounds = _cords[p.cord].bounds.unite (r);
		}
	}
}

/* each folded mark in a sliver: every end shows its drop and glyph; flat lines take a lane
 * where one is free; a far end's name shows where it covers no other glyph or name, and
 * "+N" counts the names left out */
void
McrPatch::place_lanes (Sliver& s)
{
	std::stable_sort (s.parts.begin (), s.parts.end (), [] (Part const& a, Part const& b) { return a.x0 < b.x0; });

	typedef std::pair<double, double> Span;

	double const      mid   = (s.band.y0 + s.band.y1) / 2;
	double const      g     = glyph_size / 2 + 1;
	double            right = s.chevron.x1;
	std::vector<Span> glyphs;
	std::vector<Span> names;

	s.overflow = 0;

	auto free_of = [] (std::vector<Span> const& v, double a, double b) {
		for (auto const& x : v) {
			if (x.second > a && x.first < b) {
				return false;
			}
		}
		return true;
	};

	if (!s.open) {
		/* three lanes 3 units apart: flat lines that overlap take different ones */
		double const lanes[3] = { mid, mid - lane_step, mid + lane_step };
		double       end[3];
		end[0] = end[1] = end[2] = -std::numeric_limits<double>::max ();

		for (auto& p : s.parts) {
			p.shown = true;
			p.lane  = mid;
			if (p.kind == Part::Flat) {
				for (int l = 0; l < 3; ++l) {
					if (p.x0 >= end[l]) {
						p.lane = lanes[l];
						end[l] = p.x1 + 2;
						break;
					}
				}
				glyphs.push_back (Span (p.jack[1].x - g, p.jack[1].x + g));
				right = std::max (right, p.x1);
			}
			glyphs.push_back (Span (p.jack[0].x - g, p.jack[0].x + g));
			right = std::max (right, p.jack[0].x + g);
		}
	} else {
		/* stubs hang from the edge their jack is on, a lane under another where names would meet */
		double const        drop  = std::min (stub_drop, s.band.height () / 2);
		int const           lanes = std::max (1, (int) floor ((s.band.height () / 2 - drop) / stub_step) + 1);
		std::vector<double> ends[2];

		for (auto& p : s.parts) {
			std::vector<double>& e = ends[p.near_top ? 0 : 1];
			p.shown                = true;
			p.lane                 = p.near_top ? s.band.y0 + drop : s.band.y1 - drop;
			for (int l = 0; l < lanes; ++l) {
				if ((int) e.size () <= l) {
					e.push_back (-std::numeric_limits<double>::max ());
				}
				if (p.x0 >= e[l]) {
					p.lane  = p.near_top ? s.band.y0 + drop + l * stub_step : s.band.y1 - drop - l * stub_step;
					e[l]    = p.x1 + 2;
					p.named = true;
					break;
				}
			}
			if (p.named) {
				names.push_back (Span (p.jack[0].x + g, p.x1));
				right = std::max (right, p.x1);
			} else {
				++s.overflow;
				right = std::max (right, p.jack[0].x + g);
			}
		}
	}

	if (!s.open) {
		for (auto& p : s.parts) {
			if (p.kind != Part::Stub) {
				continue;
			}
			double const a = p.jack[0].x + g;
			if (free_of (glyphs, a, p.x1) && free_of (names, a, p.x1)) {
				p.named = true;
				names.push_back (Span (a, p.x1));
				right = std::max (right, p.x1);
			} else {
				++s.overflow;
			}
		}
	}

	if (s.overflow) {
		char b[16];
		snprintf (b, sizeof (b), "+%d", s.overflow);
		Rect const r = pill_rect (Pt (0, mid), b);
		s.more       = Rect (right + 6, r.y0, right + 6 + r.width (), r.y1);
	}
}

bool
McrPatch::faces_cord (int sliver) const
{
	for (auto const& c : _cords) {
		if (c.sliver[0] == sliver || c.sliver[1] == sliver) {
			return true;
		}
	}
	return false;
}

/* Patch mode opens the slivers a new cord faces */
void
McrPatch::auto_open ()
{
	std::set<Key> live;
	bool          opened = false;

	for (auto const& c : _cords) {
		live.insert (c.key);
		if (_play || _known.count (c.key)) {
			continue;
		}
		_known.insert (c.key);
		for (int k = 0; k < 2; ++k) {
			if (c.sliver[k] < 0) {
				continue;
			}
			SliverState& st = _sliver_state[_slivers[c.sliver[k]].row];
			if (!st.open) {
				st.open = true;
				opened  = true;
			}
		}
	}

	for (auto i = _known.begin (); i != _known.end ();) {
		if (live.count (*i)) {
			++i;
		} else {
			_known.erase (i++);
		}
	}

	if (opened) {
		_relayout = true;
		place_slivers ();
	}
}

void
McrPatch::sync_watches ()
{
	bool const    on = _detective.enabled ();
	std::set<Key> live;

	for (auto const& c : _cords) {
		live.insert (c.key);
		Watch& w = _watches[c.key];
		if (!w.handle) {
			w.handle = _detective.add_cord ();
		}
		if (on && !w.tap && _engine) {
			w.tap = _engine->tap (c.cable);
		} else if (!on) {
			w.tap.reset ();
		}
	}

	for (auto i = _watches.begin (); i != _watches.end ();) {
		if (live.count (i->first)) {
			++i;
			continue;
		}
		if (i->second.handle) {
			_detective.remove_cord (i->second.handle);
		}
		_watches.erase (i++);
	}
}

void
McrPatch::restore_selection ()
{
	if (_selected < 0) {
		return;
	}
	for (size_t i = 0; i < _cords.size (); ++i) {
		if (_cords[i].key == _selected_key) {
			_selected = i;
			return;
		}
	}
	_selected = -1;
	HistoryChanged ();
}

/* ---- output ---- */

double
McrPatch::sliver_height (size_t i) const
{
	if (i >= _rows.size ()) {
		return rint (closed_sliver * _scale);
	}
	return sliver_height_below (_rows[i].id);
}

double
McrPatch::sliver_height_below (uint64_t row) const
{
	auto         i = _sliver_state.find (row);
	double const u = (i != _sliver_state.end () && i->second.open) ? i->second.height : closed_sliver;
	return rint (u * _scale);
}

std::vector<McrPatch::JackPlace>
McrPatch::jacks (uint64_t device) const
{
	std::vector<JackPlace> rv;
	auto                   i = _device_index.find (device);
	if (i == _device_index.end ()) {
		return rv;
	}
	Device const& d = _devices[i->second];
	for (auto const& k : d.jacks) {
		for (int e = 1; e >= 0; --e) {
			if (!k.shown[e]) {
				continue;
			}
			JackPlace jp;
			jp.jack    = k.pin;
			jp.top     = e == 1;
			jp.box     = to_px (k.box[e]);
			jp.greyed  = _drag.on && !(k.candidate && k.refusal.empty ());
			jp.flipped = (k.pin.input ? 1 : 0) != e;
			rv.push_back (jp);
		}
	}
	return rv;
}

int
McrPatch::hidden_jacks (uint64_t device, bool top) const
{
	auto i = _device_index.find (device);
	return i == _device_index.end () ? 0 : _devices[i->second].hidden[top ? 1 : 0];
}

bool
McrPatch::sliver_open (size_t i) const
{
	return i < _slivers.size () && _slivers[i].open;
}

/* ---- slivers ---- */

void
McrPatch::set_sliver_open (size_t i, bool open, bool pinned)
{
	if (i >= _slivers.size ()) {
		return;
	}
	SliverState& st = _sliver_state[_slivers[i].row];
	st.open         = open;
	st.pinned       = open && pinned;
	st.sprung       = false;
	if (open && st.height < min_open) {
		st.height = open_sliver;
	}
	_relayout = true;
	rebuild ();
	flush ();
}

void
McrPatch::open_all_slivers ()
{
	for (auto const& s : _slivers) {
		SliverState& st = _sliver_state[s.row];
		st.open = st.pinned = true;
	}
	_relayout = true;
	rebuild ();
	flush ();
}

void
McrPatch::close_all_slivers ()
{
	for (auto const& s : _slivers) {
		SliverState& st = _sliver_state[s.row];
		st.open = st.pinned = st.sprung = false;
	}
	_relayout = true;
	rebuild ();
	flush ();
}

void
McrPatch::open_slivers_with_cords ()
{
	for (size_t i = 0; i < _slivers.size (); ++i) {
		if (faces_cord (i)) {
			SliverState& st = _sliver_state[_slivers[i].row];
			st.open = st.pinned = true;
		}
	}
	_relayout = true;
	rebuild ();
	flush ();
}

void
McrPatch::open_slivers_around (uint64_t row)
{
	for (size_t r = 0; r < _rows.size (); ++r) {
		if (_rows[r].id != row) {
			continue;
		}
		for (int i = (int) r - 1; i <= (int) r; ++i) {
			if (i >= 0 && i < (int) _slivers.size ()) {
				SliverState& st = _sliver_state[_slivers[i].row];
				st.open = st.pinned = true;
			}
		}
	}
	_relayout = true;
	rebuild ();
	flush ();
}

std::vector<McrPatch::SliverSave>
McrPatch::sliver_states () const
{
	std::vector<SliverSave> rv;
	for (auto const& i : _sliver_state) {
		if (!i.second.open && i.second.height == open_sliver) {
			continue; /* as a new one is */
		}
		SliverSave s;
		s.row    = i.first;
		s.open   = i.second.open;
		s.pinned = i.second.pinned;
		s.height = i.second.height;
		rv.push_back (s);
	}
	return rv;
}

void
McrPatch::set_sliver_states (std::vector<SliverSave> const& v)
{
	if (_drag.on) {
		end_drag (false);
	}
	_stretch.on = false;
	_sliver_state.clear ();
	for (auto const& s : v) {
		SliverState& st = _sliver_state[s.row];
		st.open         = s.open;
		st.pinned       = s.open && s.pinned;
		st.height       = std::max (min_open, std::min (max_open, s.height));
	}
	/* the cables there now keep these slivers as they are (Patch mode opens those of new ones) */
	for (auto const& c : _cords) {
		_known.insert (c.key);
	}
	_relayout = true;
	rebuild ();
	redraw_all ();
	flush ();
}

/* ---- cords ---- */

void
McrPatch::select_cord (int c)
{
	if (c >= (int) _cords.size ()) {
		c = -1;
	}
	if (c == _selected) {
		return;
	}
	if (_selected >= 0) {
		redraw (cord_bounds (_selected));
	}
	_selected = c;
	if (c >= 0) {
		_selected_key = _cords[c].key;
		redraw (cord_bounds (c));
	}
	HistoryChanged ();
}

std::vector<uint64_t>
McrPatch::selected_rows () const
{
	std::vector<uint64_t> rv;
	if (_selected >= 0) {
		rv.push_back (_cords[_selected].cable.source_row);
		if (_cords[_selected].cable.target_row != _cords[_selected].cable.source_row) {
			rv.push_back (_cords[_selected].cable.target_row);
		}
	}
	return rv;
}

bool
McrPatch::remove_selected ()
{
	if (_selected < 0 || !_engine) {
		return false;
	}
	Cord const& c  = _cords[_selected];
	Key const   k  = c.key;
	Pt const    at = c.folded ? c.at[0] : c.path.point_at (c.path.length () / 2);
	select_cord (-1);

	/* the engine's records are snapshots: disconnect a fresh one */
	for (auto const& f : _engine->cables ()) {
		if (key_of (f) == k) {
			std::string const why = _engine->disconnect (f);
			if (!why.empty ()) {
				show_note (why, at);
			}
			break;
		}
	}
	return true;
}

McrPatch::Watch const*
McrPatch::watch (int cord) const
{
	if (cord < 0 || cord >= (int) _cords.size ()) {
		return 0;
	}
	auto i = _watches.find (_cords[cord].key);
	return i == _watches.end () ? 0 : &i->second;
}

PdCord::State
McrPatch::cord_state (int i) const
{
	PdCord::State st;
	st.selected = i == _selected;
	st.hovered  = i == _hover_cord;
	st.zoom     = _scale;
	if (Watch const* w = watch (i)) {
		st.dash_phase = w->dashes;
	}
	return st;
}

McrPatch::Rect
McrPatch::cord_bounds (int i) const
{
	if (i < 0 || i >= (int) _cords.size ()) {
		return Rect ();
	}
	Rect r = _cords[i].bounds;
	if (Watch const* w = watch (i)) {
		r = r.unite (w->drawn);
	}
	return r;
}

/* ---- hit testing ---- */

bool
McrPatch::jack_at (Pt p, int& device, int& jack, int& edge) const
{
	for (size_t di = 0; di < _devices.size (); ++di) {
		Device const& d = _devices[di];
		if (d.jacks.empty () || !d.rect.expand (PdCord::jack_size).contains (p)) {
			continue;
		}
		for (size_t ji = 0; ji < d.jacks.size (); ++ji) {
			for (int e = 0; e < 2; ++e) {
				if (d.jacks[ji].shown[e] && PdCord::jack_hit (d.jacks[ji].box[e], p)) {
					device = di;
					jack   = ji;
					edge   = e;
					return true;
				}
			}
		}
	}
	return false;
}

int
McrPatch::device_at (Pt p) const
{
	for (size_t di = 0; di < _devices.size (); ++di) {
		if (_devices[di].id && _devices[di].rect.contains (p)) {
			return di;
		}
	}
	return -1;
}

int
McrPatch::sliver_at (Pt p, double reach) const
{
	for (size_t i = 0; i < _slivers.size (); ++i) {
		Rect const b = _slivers[i].band.expand (0, reach);
		if (p.x >= b.x0 && p.x < b.x1 && p.y >= b.y0 && p.y < b.y1) {
			return i;
		}
	}
	return -1;
}

bool
McrPatch::sliver_edge_at (Pt p, int& sliver) const
{
	for (size_t i = 0; i < _slivers.size (); ++i) {
		Sliver const& s = _slivers[i];
		if (s.open && p.x >= s.band.x0 && p.x < s.band.x1 && fabs (p.y - s.band.y1) <= edge_reach) {
			sliver = i;
			return true;
		}
	}
	return false;
}

int
McrPatch::part_at (Pt p, int& sliver) const
{
	for (size_t si = 0; si < _slivers.size (); ++si) {
		Sliver const& s = _slivers[si];
		for (size_t pi = s.parts.size (); pi-- > 0;) {
			Part const& q = s.parts[pi];
			if (!q.shown) {
				continue;
			}
			Pt const a (q.jack[0].x, q.lane);
			bool     on = segment_distance (q.jack[0], a, p) <= 3;
			if (q.kind == Part::Flat) {
				Pt const b (q.jack[1].x, q.lane);
				on = on || segment_distance (a, b, p) <= 3 || segment_distance (b, q.jack[1], p) <= 3;
			} else {
				double const x1 = q.named ? q.x1 : q.jack[0].x + glyph_size;
				on              = on || Rect (q.jack[0].x - glyph_size, q.lane - 5, x1, q.lane + 5).contains (p);
			}
			if (on) {
				sliver = si;
				return pi;
			}
		}
	}
	return -1;
}

int
McrPatch::cord_at (Pt p) const
{
	/* the one drawn last first: hovered, selected, then the rest from the end */
	if (_hover_cord >= 0 && !_cords[_hover_cord].folded && PdCord::hit (_cords[_hover_cord].path, p, _hover_cord == _selected)) {
		return _hover_cord;
	}
	if (_selected >= 0 && !_cords[_selected].folded && PdCord::hit (_cords[_selected].path, p, true)) {
		return _selected;
	}
	for (size_t i = _cords.size (); i-- > 0;) {
		if (!_cords[i].folded && PdCord::hit (_cords[i].path, p, (int) i == _selected)) {
			return i;
		}
	}
	int       s;
	int const part = part_at (p, s);
	return part >= 0 ? _slivers[s].parts[part].cord : -1;
}

/* ---- events ---- */

bool
McrPatch::button_press (double x, double y, unsigned button, int clicks, unsigned state)
{
	Pt const p (x / _scale, y / _scale);
	_pointer    = p;
	_pointer_in = true;

	if ((_drag.on || _stretch.on) && button == 1 && clicks == 1) {
		/* a new press of the button the drag holds: its release went elsewhere */
		cancel ();
	}
	if (_drag.on || _stretch.on) {
		return true; /* another button, or the double click of the press that started it */
	}
	if (button != 1) {
		return false;
	}

	/* a jack: drag a cord out of it */
	if (!_play && clicks == 1) {
		int d;
		int j;
		int e;
		if (jack_at (p, d, j, e) && _devices[d].id) {
			start_drag (d, j, e);
			flush ();
			return true;
		}
	}

	/* an open sliver's bottom edge: make it taller or shorter */
	int s = -1;
	if (clicks == 1 && sliver_edge_at (p, s)) {
		_stretch.on     = true;
		_stretch.sliver = s;
		_stretch.y      = p.y;
		_stretch.height = _slivers[s].band.height ();
		_cursor        = Resize;
		return true;
	}

	s = sliver_at (p);
	if (s >= 0 && _slivers[s].chevron.contains (p)) {
		if (clicks == 1) {
			set_sliver_open (s, !_slivers[s].open, true);
		}
		return true;
	}

	/* a cord: anywhere in Patch mode; in Play mode on a sliver or a folded cord's mark */
	int ps;
	int const part = part_at (p, ps);
	int const c    = (!_play || s >= 0) ? cord_at (p) : (part >= 0 ? _slivers[ps].parts[part].cord : -1);
	if (c >= 0) {
		select_cord (c);
		if (part >= 0 && _slivers[ps].parts[part].cord == c && _slivers[ps].parts[part].kind == Part::Stub) {
			/* a stub names the far end: show it */
			Part const& q = _slivers[ps].parts[part];
			Reveal (to_px (Rect::around (_cords[c].at[1 - q.end], 240, 160)));
		}
		return true;
	}

	if (s >= 0) {
		if (!_slivers[s].open) {
			if (clicks == 1) {
				_press_sliver = s;
			}
			return true;
		}
		if (clicks == 2 && !(s == _opened_sliver && now () - _opened_at < double_click_us)) {
			set_sliver_open (s, false);
			return true;
		}
		select_cord (-1);
		return true;
	}

	select_cord (-1);
	return false;
}

bool
McrPatch::button_release (double x, double y, unsigned button, unsigned state)
{
	Pt const p (x / _scale, y / _scale);
	_pointer = p;

	if (_drag.on) {
		update_drag (p);
		end_drag (_drag.moved);
		flush ();
		return true;
	}

	if (_stretch.on) {
		end_stretch ();
		hover (p);
		flush ();
		return true;
	}

	if (_press_sliver >= 0) {
		int const s   = _press_sliver;
		_press_sliver = -1;
		if (s < (int) _slivers.size () && !_slivers[s].open && _slivers[s].band.expand (0, 3).contains (p)) {
			_opened_sliver = s;
			_opened_at     = now ();
			set_sliver_open (s, true, true);
		}
		return true;
	}

	return false;
}

/* a sliver's edge was let go: too short, it closes */
void
McrPatch::end_stretch ()
{
	_stretch.on = false;
	if (_stretch.sliver < 0 || _stretch.sliver >= (int) _slivers.size ()) {
		return;
	}
	SliverState& st = _sliver_state[_slivers[_stretch.sliver].row];
	if (st.open && st.height < min_open) {
		st.open   = false;
		st.pinned = false;
		st.height = open_sliver;
		_relayout = true;
		rebuild ();
	}
}

bool
McrPatch::motion (double x, double y, unsigned state)
{
	Pt const p (x / _scale, y / _scale);
	_pointer    = p;
	_pointer_in = true;

	if ((_drag.on || _stretch.on) && !(state & button1_mask)) {
		/* the button is up, and its release went elsewhere: the drag is over */
		cancel ();
	}

	if (_drag.on) {
		update_drag (p);
		flush ();
		return true;
	}

	if (_stretch.on) {
		if (_stretch.sliver < (int) _slivers.size ()) {
			SliverState& st = _sliver_state[_slivers[_stretch.sliver].row];
			double const h  = std::max (closed_sliver, std::min (max_open, _stretch.height + p.y - _stretch.y));
			if (h != st.height || !st.open) {
				st.open   = true;
				st.pinned = true;
				st.height = h;
				_relayout = true;
				rebuild ();
			}
		}
		flush ();
		return true;
	}

	hover (p);
	return false;
}

/* The wheel scrolls the canvas, as in plugdata, except over the selected cord, where it
 * sets the cord's own gain: selecting it first says the wheel is meant for it. Not while
 * the wheel is scrolling the canvas, though: as the canvas moves under the pointer, the
 * selected cord may pass under it, and the scroll must not stop there. */
bool
McrPatch::scroll (double x, double y, int dy, unsigned state)
{
	Pt const      p (x / _scale, y / _scale);
	int64_t const t         = now ();
	bool const    scrolling = _wheel_scrolled && t - _wheel_scrolled < wheel_gesture_us;

	int c = -1;
	if (!_play && !_drag.on && _engine && dy && !scrolling && _selected >= 0 && !_cords[_selected].folded
	    && PdCord::hit (_cords[_selected].path, p, true)) {
		c = _selected;
	}

	/* the cord's gain, when it has one of its own (its send carries no other cord) */
	double g;
	if (c < 0 || _cords[c].kind != PdCord::Signal || !_engine->gain (_cords[c].cable, g)) {
		_wheel_scrolled = t; /* the canvas scrolls */
		return false;
	}
	double const step   = (state & control_mask) ? 0.1 : 1;
	double const most   = 20 * log10 (2.); /* a send's gain goes up to +6 dB */
	bool const   silent = g <= 0;
	double       db     = silent ? -60 : 20 * log10 (g);

	if (dy < 0) {
		db = silent ? -60 : std::min (most, db + step);
	} else {
		db = silent ? -61 : db - step;
	}
	double const ng = db < -60 ? 0 : pow (10, db / 20);
	if (!_engine->set_gain (_cords[c].cable, ng)) {
		_wheel_scrolled = t;
		return false;
	}

	char b[32];
	if (ng > 0) {
		g_ascii_formatd (b, sizeof (b), "%.1f", fabs (db) < 0.05 ? 0. : db);
		show_note (std::string (db >= 0.05 ? "+" : "") + b + " dB", p, 1);
	} else {
		show_note ("-inf dB", p, 1);
	}
	flush ();
	return true;
}

bool
McrPatch::key_press (unsigned keyval, unsigned state)
{
	switch (keyval) {
		case 0xffff: /* GDK_KEY_Delete */
		case 0xff08: /* GDK_KEY_BackSpace */
			if (_play || _selected < 0 || _drag.on) {
				return false;
			}
			remove_selected ();
			flush ();
			return true;
		case 0xff1b: /* GDK_KEY_Escape */
			return escape ();
		default:
			return false;
	}
}

bool
McrPatch::escape ()
{
	if (_drag.on || _stretch.on) {
		cancel ();
		return true;
	}
	if (_selected >= 0) {
		select_cord (-1);
		return true;
	}
	return false;
}

void
McrPatch::cancel ()
{
	_press_sliver = -1;
	if (!_drag.on && !_stretch.on) {
		return;
	}
	if (_drag.on) {
		end_drag (false); /* no cable */
	}
	if (_stretch.on) {
		end_stretch ();
		_cursor = Arrow;
	}
	flush ();
}

void
McrPatch::leave ()
{
	_pointer_in = false;
	if (_drag.on || _stretch.on) {
		return;
	}
	if (_hover_device >= 0 && _hover_device < (int) _devices.size ()) {
		redraw (_devices[_hover_device].rect.expand (PdCord::jack_size));
		redraw (jack_label (_hover_device, _hover_jack, _hover_edge));
	}
	if (_hover_chevron >= 0 && _hover_chevron < (int) _slivers.size ()) {
		redraw (_slivers[_hover_chevron].chevron.expand (2));
	}
	set_hover_cord (-1);
	_hover_device = _hover_jack = _hover_edge = _hover_chevron = -1;
	_cursor       = Arrow;
}

void
McrPatch::hover (Pt p)
{
	int    d   = -1;
	int    j   = -1;
	int    e   = -1;
	int    c   = -1;
	int    ch  = -1;
	Cursor cur = Arrow;

	int const s = sliver_at (p);
	if (s >= 0 && _slivers[s].chevron.contains (p)) {
		ch = s;
	}

	if (!_play) {
		if (jack_at (p, d, j, e)) {
			cur = Hand;
		} else {
			d = device_at (p);
		}
	}

	if (j < 0 && ch < 0) {
		if (!_play || s >= 0) {
			c = cord_at (p);
		} else {
			int       ps;
			int const part = part_at (p, ps);
			c              = part >= 0 ? _slivers[ps].parts[part].cord : -1;
		}
	}

	int es;
	if (j < 0 && sliver_edge_at (p, es)) {
		cur = Resize;
	}

	if (d != _hover_device || j != _hover_jack || e != _hover_edge) {
		if (_hover_device >= 0 && _hover_device < (int) _devices.size ()) {
			redraw (_devices[_hover_device].rect.expand (PdCord::jack_size));
			redraw (jack_label (_hover_device, _hover_jack, _hover_edge));
		}
		_hover_device = d;
		_hover_jack   = j;
		_hover_edge   = e;
		if (d >= 0) {
			redraw (_devices[d].rect.expand (PdCord::jack_size));
			redraw (jack_label (d, j, e));
		}
	}

	if (ch != _hover_chevron) {
		if (_hover_chevron >= 0 && _hover_chevron < (int) _slivers.size ()) {
			redraw (_slivers[_hover_chevron].chevron.expand (2));
		}
		_hover_chevron = ch;
		if (ch >= 0) {
			redraw (_slivers[ch].chevron.expand (2));
		}
	}

	set_hover_cord (c);
	_cursor = cur;
}

void
McrPatch::set_hover_cord (int c)
{
	if (c == _hover_cord) {
		return;
	}
	redraw (cord_bounds (_hover_cord));
	_hover_cord = c;
	redraw (cord_bounds (c));
}

/* ---- the drag ---- */

void
McrPatch::start_drag (int di, int ji, int edge)
{
	Device const& d = _devices[di];
	Jack const&   j = d.jacks[ji];

	_drag            = Drag ();
	_drag.on         = true;
	_drag.source     = jack_id (d, j);
	_drag.device     = di;
	_drag.jack       = ji;
	_drag.from_input = j.pin.input;
	_drag.edge       = edge;
	_drag.pointer    = _pointer;
	_drag.press      = _pointer;

	/* the jacks of other devices the cord could go to, and whether the engine takes it */
	_refusals.clear ();
	McrPatchEnd const self = end_of (d, j);
	for (auto const& o : _devices) {
		if (!o.id || o.id == d.id) {
			continue;
		}
		for (auto const& k : o.jacks) {
			if (k.pin.input == j.pin.input || k.pin.type != j.pin.type || k.pin.pin == no_pin) {
				continue;
			}
			McrPatchEnd const other = end_of (o, k);
			std::string       why   = _("No session");
			if (_engine) {
				why = j.pin.input ? _engine->can_connect (other, self) : _engine->can_connect (self, other);
			}
			_refusals[jack_id (o, k)] = why;
		}
	}

	set_hover_cord (-1);
	_cursor = Hand;
	rebuild ();
	snap (_pointer);
	redraw_all ();
}

void
McrPatch::snap (Pt p)
{
	std::vector<Rect>                boxes;
	std::vector<std::pair<int, int>> who;

	for (size_t di = 0; di < _devices.size (); ++di) {
		Device const& d = _devices[di];
		for (size_t ji = 0; ji < d.jacks.size (); ++ji) {
			Jack const& k = d.jacks[ji];
			if (!k.candidate || !k.refusal.empty () || k.drag_edge < 0 || !k.shown[k.drag_edge]) {
				continue;
			}
			boxes.push_back (k.box[k.drag_edge]);
			who.push_back (std::make_pair ((int) di, (int) ji));
		}
	}

	int const n       = PdCord::nearest_jack (boxes, p, snap_reach);
	_drag.snap_device = n >= 0 ? who[n].first : -1;
	_drag.snap_jack   = n >= 0 ? who[n].second : -1;
}

void
McrPatch::update_drag (Pt p)
{
	if (!_drag.on) {
		return;
	}
	Rect const before = drag_bounds ();

	_drag.pointer = p;
	if (!_drag.moved && p.distance (_drag.press) > 3) {
		_drag.moved = true;
	}

	snap (p);

	/* the jack it came out of faces the pointer, or the jack it would land on */
	Device const& s = _devices[_drag.device];
	bool          top;
	if (_drag.snap_device >= 0) {
		Device const& t = _devices[_drag.snap_device];
		if (t.row == s.row) {
			top = false;
		} else if (_drag.from_input) {
			top = PdCord::jack_on_top (true, s.rect, Pt (t.rect.x0, t.rect.y1));
		} else {
			top = PdCord::jack_on_top (false, s.rect, Pt (t.rect.x0, t.rect.y0));
		}
	} else if (_drag.from_input) {
		top = !(p.y > s.rect.y1);
	} else {
		top = p.y < s.rect.y0;
	}

	if ((top ? 1 : 0) != _drag.edge) {
		redraw (s.rect.expand (PdCord::jack_size));
		_drag.edge = top ? 1 : 0;
		rebuild ();
		if (!_drag.on) {
			redraw_all ();
			return;
		}
		snap (p);
		redraw (_devices[_drag.device].rect.expand (PdCord::jack_size));
	}

	/* resting on a closed sliver opens it */
	int const sl = sliver_at (p, 2);
	if (sl >= 0 && !_slivers[sl].open) {
		if (sl != _drag.sliver) {
			_drag.sliver = sl;
			_drag.since  = now ();
			update_timer ();
		} else if (now () - _drag.since >= spring_us) {
			spring ();
		}
	} else {
		_drag.sliver = -1;
	}

	redraw (before.unite (drag_bounds ()));
}

void
McrPatch::spring ()
{
	int const s  = _drag.sliver;
	_drag.sliver = -1;
	if (s < 0 || s >= (int) _slivers.size () || _slivers[s].open) {
		return;
	}
	SliverState& st = _sliver_state[_slivers[s].row];
	st.open         = true;
	st.sprung       = true;
	_relayout       = true;
	rebuild ();
	snap (_drag.pointer);
}

void
McrPatch::end_drag (bool drop)
{
	Rect const before = drag_bounds ();
	Drag const d      = _drag;

	bool        make = false;
	McrPatchEnd from;
	McrPatchEnd to;
	std::string refusal;

	if (drop && d.snap_device >= 0) {
		McrPatchEnd const a = end_of (_devices[d.device], _devices[d.device].jacks[d.jack]);
		McrPatchEnd const b = end_of (_devices[d.snap_device], _devices[d.snap_device].jacks[d.snap_jack]);
		from                = d.from_input ? b : a;
		to                  = d.from_input ? a : b;
		make                = true;
	} else if (drop) {
		/* on a jack that can't take it: say why */
		for (auto const& o : _devices) {
			for (auto const& k : o.jacks) {
				for (int e = 0; e < 2; ++e) {
					if (k.candidate && !k.refusal.empty () && k.shown[e] && k.box[e].expand (2).contains (d.pointer)) {
						refusal = k.refusal;
					}
				}
			}
		}
	}

	_drag = Drag ();
	_refusals.clear ();
	_cursor = Arrow;

	std::set<Key> had;
	for (auto const& c : _cords) {
		had.insert (c.key);
	}

	if (make && _engine) {
		/* the engine's Changed may come from inside connect () */
		std::string const why = _engine->connect (from, to);
		if (!why.empty ()) {
			show_note (why, d.pointer);
		}
	} else if (!refusal.empty ()) {
		show_note (refusal, d.pointer);
	}

	rebuild ();

	/* the slivers it sprang open close again, unless the cord it made runs there */
	for (size_t i = 0; i < _slivers.size (); ++i) {
		SliverState& st = _sliver_state[_slivers[i].row];
		if (!st.sprung) {
			continue;
		}
		st.sprung = false;
		bool used = false;
		for (auto const& c : _cords) {
			used = used || (!had.count (c.key) && (c.sliver[0] == (int) i || c.sliver[1] == (int) i));
		}
		if (!used && !st.pinned) {
			st.open   = false;
			_relayout = true;
		}
	}
	if (_relayout) {
		rebuild ();
	}

	redraw (before);
	redraw_all ();
	if (_pointer_in) {
		hover (_pointer);
	}
}

Path
McrPatch::drag_path () const
{
	if (!_drag.on || _drag.device < 0) {
		return Path ();
	}
	Jack const& j = _devices[_drag.device].jacks[_drag.jack];
	if (!j.shown[_drag.edge]) {
		return Path ();
	}

	bool const top      = _drag.edge == 1;
	Pt const   at       = j.box[_drag.edge].centre ();
	bool const straight = _theme->straight_connections;

	if (_drag.snap_device >= 0) {
		Jack const& t      = _devices[_drag.snap_device].jacks[_drag.snap_jack];
		Pt const    to     = t.box[t.drag_edge].centre ();
		bool const  to_top = t.drag_edge == 1;
		return _drag.from_input ? Path::between (to, to_top, at, top, straight) : Path::between (at, top, to, to_top, straight);
	}
	/* the pointer stands in for the other jack, on the edge that faces this one */
	return _drag.from_input ? Path::between (_drag.pointer, !top, at, top, straight) : Path::between (at, top, _drag.pointer, !top, straight);
}

McrPatch::Rect
McrPatch::drag_bounds () const
{
	if (!_drag.on) {
		return Rect ();
	}
	Rect r = drag_path ().bounds ().expand (12).unite (Rect::around (_drag.pointer, 16, 16));
	if (_drag.snap_device >= 0) {
		r = r.unite (_devices[_drag.snap_device].rect.expand (PdCord::jack_size));
		r = r.unite (jack_label (_drag.snap_device, _drag.snap_jack, _devices[_drag.snap_device].jacks[_drag.snap_jack].drag_edge));
	}
	return r;
}

/* ---- notes ---- */

void
McrPatch::show_note (std::string const& text, Pt at, double seconds)
{
	redraw (_note.drawn);
	_note.text  = text;
	_note.at    = at;
	_note.shown = now ();
	_note.until = _note.shown + (int64_t) (seconds * 1e6);
	_note.drawn = note_bounds ();
	redraw (_note.drawn);
	update_timer ();
}

McrPatch::Rect
McrPatch::note_bounds () const
{
	if (!_note.until) {
		return Rect ();
	}
	Rect const   view = bubble_view ();
	PangoLayout* l    = PdCord::text_layout (_note.text, note_height);
	pango_layout_set_wrap (l, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_width (l, PdCord::bubble_wrap_width (view) * PANGO_SCALE);
	double w;
	double h;
	layout_size (l, w, h);
	g_object_unref (l);
	return PdCord::bubble_bounds (view, _note.at, ceil (w), ceil (h), note_offset);
}

McrPatch::Rect
McrPatch::bubble_view () const
{
	if (!_view.empty ()) {
		return _view;
	}
	Rect r;
	for (auto const& row : _rows) {
		r = r.unite (row.rect);
	}
	return r.expand (40);
}

/* ---- drawing ---- */

void
McrPatch::draw_under_faces (cairo_t* cr, Rect const& area_px)
{
	Rect const area = to_units (area_px);

	cairo_save (cr);
	cairo_scale (cr, _scale, _scale);
	draw_slivers (cr, area);
	if (_play) {
		/* plugdata's run mode: cords behind the objects */
		draw_cords (cr, area);
		draw_jacks (cr, area);
	}
	cairo_restore (cr);
}

void
McrPatch::draw_over_faces (cairo_t* cr, Rect const& area_px)
{
	Rect const area = to_units (area_px);

	cairo_save (cr);
	cairo_scale (cr, _scale, _scale);
	if (!_play) {
		draw_cords (cr, area);
		draw_jacks (cr, area);
	}
	draw_detective (cr, area);
	if (!_play) {
		draw_drag (cr);
		draw_label (cr);
	}
	draw_note (cr);
	cairo_restore (cr);
}

void
McrPatch::draw_slivers (cairo_t* cr, Rect const& area)
{
	PdTheme const& th = *_theme;

	for (size_t i = 0; i < _slivers.size (); ++i) {
		Sliver const& s = _slivers[i];
		if (!s.band.expand (0, 40).intersects (area)) {
			continue;
		}

		cairo_save (cr);
		cairo_new_path (cr);
		cairo_set_line_width (cr, 1);
		PdColor::set_source (cr, th.outline);
		if (!s.open) {
			/* closed: the line between two tracks */
			double const y = floor ((s.band.y0 + s.band.y1) / 2) + .5;
			cairo_move_to (cr, s.chevron.x1 + 2, y);
			cairo_line_to (cr, s.band.x1, y);
			cairo_stroke (cr);
		} else {
			/* open: its edges; the bottom one can be dragged */
			double const y0 = floor (s.band.y0) + .5;
			double const y1 = ceil (s.band.y1) - .5;
			cairo_move_to (cr, s.chevron.x1 + 2, y0);
			cairo_line_to (cr, s.band.x1, y0);
			cairo_stroke (cr);
			int        es;
			bool const grabbed = (_stretch.on && _stretch.sliver == (int) i) || (_cursor == Resize && sliver_edge_at (_pointer, es) && es == (int) i);
			cairo_move_to (cr, s.band.x0, y1);
			cairo_line_to (cr, s.band.x1, y1);
			PdColor::set_source (cr, grabbed ? th.object_outline : th.outline);
			cairo_set_line_width (cr, grabbed ? 2 : 1);
			cairo_stroke (cr);
		}

		/* the toggle: ▸ closed, ▾ open */
		Pt const c = s.chevron.centre ();
		if (s.open) {
			cairo_move_to (cr, c.x - 3.5, c.y - 2);
			cairo_line_to (cr, c.x + 3.5, c.y - 2);
			cairo_line_to (cr, c.x, c.y + 2.5);
		} else {
			cairo_move_to (cr, c.x - 2, c.y - 3.5);
			cairo_line_to (cr, c.x - 2, c.y + 3.5);
			cairo_line_to (cr, c.x + 2.5, c.y);
		}
		cairo_close_path (cr);
		PdColor::set_source (cr, th.canvas_text, (int) i == _hover_chevron ? .9 : .4);
		cairo_fill (cr);
		cairo_restore (cr);

		for (auto const& p : s.parts) {
			if (p.shown) {
				draw_part (cr, s, p);
			}
		}

		if (s.overflow) {
			char b[16];
			snprintf (b, sizeof (b), "+%d", s.overflow);
			draw_pill (cr, s.more, b, th);
		}
	}
}

void
McrPatch::draw_part (cairo_t* cr, Sliver const& s, Part const& p)
{
	PdTheme const& th       = *_theme;
	Cord const&    c        = _cords[p.cord];
	bool const     selected = p.cord == _selected;
	bool const     hovered  = p.cord == _hover_cord;
	Watch const*   w        = watch (p.cord);
	bool const     on       = _detective.enabled ();
	bool const     blink    = on && w && w->blink && w->blink > now ();

	Gtkmm2ext::Color col = c.kind == PdCord::Signal ? th.signal : th.data;
	if (hovered) {
		col = PdColor::brighter (col, .4f);
	}
	double const lw = selected || hovered ? 2.5 : 2;

	cairo_save (cr);

	/* the drops from the jacks, and the flat line between them */
	cairo_new_path (cr);
	cairo_move_to (cr, p.jack[0].x, p.jack[0].y);
	cairo_line_to (cr, p.jack[0].x, p.lane);
	if (p.kind == Part::Flat) {
		cairo_line_to (cr, p.jack[1].x, p.lane);
		cairo_line_to (cr, p.jack[1].x, p.jack[1].y);
	}
	cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	PdColor::set_source (cr, th.band ());
	cairo_set_line_width (cr, lw + 1.5);
	cairo_stroke_preserve (cr);
	PdColor::set_source (cr, col);
	cairo_set_line_width (cr, lw);
	cairo_stroke (cr);

	draw_glyph (cr, Pt (p.jack[0].x, p.lane), c.kind, col, blink);
	if (p.kind == Part::Flat) {
		draw_glyph (cr, Pt (p.jack[1].x, p.lane), c.kind, col, blink);
	}

	/* a stub names the far end */
	double x = p.jack[0].x + glyph_size / 2 + 3;
	if (p.kind == Part::Stub && p.named) {
		PangoLayout* l = lettering (p.text);
		double       tw;
		double       tht;
		layout_size (l, tw, tht);
		rounded (cr, Rect (x - 1.5, p.lane - tht / 2, x + tw + 1.5, p.lane + tht / 2), 2);
		PdColor::set_source (cr, th.canvas_background, .85);
		cairo_fill (cr);
		PdColor::set_source (cr, selected || hovered ? th.canvas_text : PdColor::interpolated (th.canvas_text, col, .3f));
		show (cr, l, x, p.lane);
		g_object_unref (l);
		x += tw + 4;
	}

	/* Detective Mode: the last message it carried */
	if (on && w && !w->last.empty () && (p.kind == Part::Flat || p.named)) {
		PangoLayout* l = lettering (clip (w->last, 14));
		double       tw;
		double       tht;
		layout_size (l, tw, tht);
		double const tx = p.kind == Part::Flat ? (p.jack[0].x + p.jack[1].x) / 2 - tw / 2 : x;
		rounded (cr, Rect (tx - 2, p.lane - tht / 2, tx + tw + 2, p.lane + tht / 2), 2);
		PdColor::set_source (cr, blink ? th.data : th.canvas_background, blink ? 1. : .9);
		cairo_fill (cr);
		PdColor::set_source (cr, blink ? th.canvas_background : th.data);
		show (cr, l, tx, p.lane);
		g_object_unref (l);
	}

	cairo_restore (cr);
}

void
McrPatch::draw_glyph (cairo_t* cr, Pt at, PdCord::Kind kind, Gtkmm2ext::Color col, bool blink)
{
	PdTheme const& th = *_theme;
	double const   r  = glyph_size / 2;

	cairo_save (cr);
	cairo_new_path (cr);
	cairo_arc (cr, at.x, at.y, r + 1.25, 0, 2 * M_PI);
	PdColor::set_source (cr, blink ? PdColor::brighter (col, .6f) : th.canvas_background);
	cairo_fill (cr);

	cairo_set_line_width (cr, 1.25);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	if (kind == PdCord::Signal) {
		/* a sine: audio */
		for (int i = 0; i <= 14; ++i) {
			double const x = -r + i * glyph_size / 14;
			double const y = -1.9 * sin (2 * M_PI * (x + r) / glyph_size);
			if (i == 0) {
				cairo_move_to (cr, at.x + x, at.y + y);
			} else {
				cairo_line_to (cr, at.x + x, at.y + y);
			}
		}
	} else {
		/* a ring: MIDI */
		cairo_arc (cr, at.x, at.y, 2.4, 0, 2 * M_PI);
	}
	PdColor::set_source (cr, blink ? th.canvas_background : col);
	cairo_stroke (cr);
	cairo_restore (cr);
}

void
McrPatch::draw_cords (cairo_t* cr, Rect const& area)
{
	PdTheme const&      th    = *_theme;
	PdCord::Style const style = (PdCord::Style) th.connection_style;

	/* plugdata's order: at rest, selected, hovered */
	std::vector<int> order;
	for (size_t i = 0; i < _cords.size (); ++i) {
		if (!_cords[i].folded && _cords[i].bounds.intersects (area)) {
			order.push_back (i);
		}
	}
	std::stable_sort (order.begin (), order.end (), [this] (int a, int b) {
		return PdCord::draw_rank (cord_state (a)) < PdCord::draw_rank (cord_state (b));
	});

	for (int i : order) {
		PdCord::render (cr, _cords[i].path, _cords[i].kind, style, cord_state (i), th);
	}
}

void
McrPatch::draw_jacks (cairo_t* cr, Rect const& area)
{
	PdTheme const& th = *_theme;

	for (size_t di = 0; di < _devices.size (); ++di) {
		Device const& d = _devices[di];
		if (d.jacks.empty () || !d.rect.expand (PdCord::jack_size).intersects (area)) {
			continue;
		}

		for (size_t ji = 0; ji < d.jacks.size (); ++ji) {
			Jack const& k      = d.jacks[ji];
			bool const  source = _drag.on && (int) di == _drag.device && (int) ji == _drag.jack;
			bool const  valid  = k.candidate && k.refusal.empty ();

			PdCord::JackState js;
			/* hovering one of a jack's two places lights the other too */
			js.targeted = ((int) di == _hover_device && (int) ji == _hover_jack) || ((int) di == _drag.snap_device && (int) ji == _drag.snap_jack) || source;
			js.locked   = _play || (_drag.on && !valid && !source);
			js.expanded = !_play && ((int) di == _hover_device || (_drag.on && valid));

			for (int e = 0; e < 2; ++e) {
				if (k.shown[e]) {
					PdCord::render_jack (cr, k.box[e], d.rect, kind_of (k.pin.type), js, th);
				}
			}
		}

		for (int e = 0; e < 2; ++e) {
			if (d.hidden[e] && !_play) {
				char b[16];
				snprintf (b, sizeof (b), "+%d", d.hidden[e]);
				draw_pill (cr, d.more[e], b, th, _drag.on ? .6 : 1);
			}
		}
	}
}

/* a jack's name, in a small bubble off its face's edge: the jack under the pointer, or the
 * one a dragged cord would land on (plugdata names an iolet under the pointer) */
McrPatch::Rect
McrPatch::jack_label (int device, int jack, int edge, std::string* text) const
{
	if (device < 0 || jack < 0 || edge < 0 || device >= (int) _devices.size () || jack >= (int) _devices[device].jacks.size ()) {
		return Rect ();
	}
	Device const& d = _devices[device];
	Jack const&   k = d.jacks[jack];
	if (!k.shown[edge]) {
		return Rect ();
	}
	std::string t = d.id ? k.pin.name : d.name;
	if (t.empty ()) {
		t = k.tag;
	}
	PangoLayout* l = PdCord::text_layout (t, label_height);
	double       w;
	double       h;
	layout_size (l, w, h);
	g_object_unref (l);
	if (text) {
		*text = t;
	}

	Rect const&  b  = k.box[edge];
	double const bw = ceil (w) + 8;
	double const bh = ceil (h) + 4;
	double const cx = b.centre ().x;
	double const y  = edge == 1 ? b.y0 - 3 - bh : b.y1 + 3;
	Rect         r  = Rect::sized (rint (cx - bw / 2), rint (y), bw, bh);

	/* kept in view */
	Rect const v = bubble_view ();
	if (!v.empty ()) {
		r = r.translate (Pt (std::max (0., v.x0 - r.x0) - std::max (0., r.x1 - v.x1), 0));
	}
	return r;
}

void
McrPatch::draw_label (cairo_t* cr)
{
	int d = -1;
	int j = -1;
	int e = -1;
	if (_drag.on) {
		if (_drag.snap_device >= 0) {
			d = _drag.snap_device;
			j = _drag.snap_jack;
			e = _devices[d].jacks[j].drag_edge;
		}
	} else if (!_play) {
		d = _hover_device;
		j = _hover_jack;
		e = _hover_edge;
	}
	std::string t;
	Rect const  r = jack_label (d, j, e, &t);
	if (r.empty ()) {
		return;
	}
	PdTheme const& th = *_theme;
	cairo_save (cr);
	cairo_new_path (cr);
	rounded (cr, r.reduce (.5), 3);
	PdColor::set_source (cr, th.bubble_fill ());
	cairo_fill_preserve (cr);
	PdColor::set_source (cr, th.outline);
	cairo_set_line_width (cr, 1);
	cairo_stroke (cr);
	PangoLayout* l = PdCord::text_layout (t, label_height);
	PdColor::set_source (cr, th.canvas_text);
	show (cr, l, r.x0 + 4, r.centre ().y);
	g_object_unref (l);
	cairo_restore (cr);
}

void
McrPatch::draw_detective (cairo_t* cr, Rect const& area)
{
	if (!_detective.enabled ()) {
		return;
	}
	PdTheme const&      th    = *_theme;
	PdCord::Style const style = (PdCord::Style) th.connection_style;
	Rect const          view  = bubble_view ();

	for (size_t i = 0; i < _cords.size (); ++i) {
		Cord const& c = _cords[i];
		if (c.folded) {
			continue;
		}
		Watch const* w = watch (i);
		if (!w || !w->handle) {
			continue;
		}
		PdCord::CordDetective const* cd = _detective.cord (w->handle);
		if (!cd || !cd->active () || !(w->drawn.intersects (area) || c.bounds.intersects (area))) {
			continue;
		}
		cd->render (cr, c.path, PdCord::width (style, c.kind), view, th);
	}
}

void
McrPatch::draw_drag (cairo_t* cr)
{
	if (!_drag.on || _drag.device < 0) {
		return;
	}
	Jack const& j = _devices[_drag.device].jacks[_drag.jack];
	PdCord::render_dragged (cr, drag_path (), _drag.pointer, kind_of (j.pin.type), (PdCord::Style) _theme->connection_style, *_theme, _scale);
}

void
McrPatch::draw_note (cairo_t* cr)
{
	if (!_note.until) {
		return;
	}
	PdTheme const& th   = *_theme;
	int64_t const  t    = now ();
	double const   left = (double) (_note.until - t);
	double const   a    = left < fade_us ? std::max (0., left / fade_us) : 1.;
	if (a <= 0) {
		return;
	}

	Rect const   view = bubble_view ();
	PangoLayout* l    = PdCord::text_layout (_note.text, note_height);
	pango_layout_set_wrap (l, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_width (l, PdCord::bubble_wrap_width (view) * PANGO_SCALE);
	double w;
	double h;
	layout_size (l, w, h);
	Rect const b = PdCord::bubble_bounds (view, _note.at, ceil (w), ceil (h), note_offset);

	if (!b.empty ()) {
		/* a Detective bubble (Connection.cpp:1719-1736) */
		Rect const r (rint (b.x0), rint (b.y0), rint (b.x1), rint (b.y1));
		cairo_save (cr);
		cairo_push_group (cr);
		cairo_new_path (cr);
		rounded (cr, r.reduce (.5), note_radius);
		PdColor::set_source (cr, th.bubble_fill ());
		cairo_fill_preserve (cr);
		PdColor::set_source (cr, th.outline);
		cairo_set_line_width (cr, 1);
		cairo_stroke (cr);
		cairo_move_to (cr, r.x0 + 1 + note_pad_x, r.y0 + 1 + note_pad_y);
		PdColor::set_source (cr, th.canvas_text);
		pango_cairo_show_layout (cr, l);
		cairo_new_path (cr);
		cairo_pop_group_to_source (cr);
		cairo_paint_with_alpha (cr, a);
		cairo_restore (cr);
	}
	g_object_unref (l);
}

/* ---- units and pixels ---- */

McrPatch::Rect
McrPatch::to_px (Rect const& r) const
{
	return Rect (r.x0 * _scale, r.y0 * _scale, r.x1 * _scale, r.y1 * _scale);
}

McrPatch::Rect
McrPatch::to_units (Rect const& r) const
{
	return Rect (r.x0 / _scale, r.y0 / _scale, r.x1 / _scale, r.y1 / _scale);
}

void
McrPatch::redraw (Rect const& u)
{
	if (u.empty ()) {
		return;
	}
	Rect const p = to_px (u.expand (2));
	Redraw (Rect (floor (p.x0), floor (p.y0), ceil (p.x1), ceil (p.y1)));
}

void
McrPatch::redraw_all ()
{
	Rect r = _view;
	for (auto const& row : _rows) {
		r = r.unite (row.rect);
	}
	redraw (r.expand (40));
}

/* ---- Detective Mode ---- */

void
McrPatch::set_detective (bool yn)
{
	if (yn == _detective.enabled ()) {
		return;
	}
	_detective.set_enabled (yn);
	if (!yn) {
		/* one tick clears what is drawn */
		_detective.tick (now ());
		for (auto& w : _watches) {
			w.second.level = 0;
			w.second.blink = 0;
			w.second.drawn = Rect ();
		}
	}
	rebuild (); /* taps on or off; room for the last messages */
	redraw_all ();
	update_timer ();
	flush ();
}

bool
McrPatch::detective () const
{
	return _detective.enabled ();
}

void
McrPatch::set_detective_speed (float s)
{
	_detective.set_speed (s);
}

float
McrPatch::detective_speed () const
{
	return _detective.speed ();
}

void
McrPatch::set_detective_paused (bool yn)
{
	_detective.set_paused (yn);
}

bool
McrPatch::detective_paused () const
{
	return _detective.paused ();
}

void
McrPatch::detective_step ()
{
	_detective.step ();
}

std::string
McrPatch::history_title () const
{
	if (_selected < 0) {
		return std::string ();
	}
	Cord const& c   = _cords[_selected];
	auto        end = [&] (int k) {
		Device const& d = _devices[c.device[k]];
		Jack const&   j = d.jacks[c.jack[k]];
		char          b[16];
		snprintf (b, sizeof (b), "%d ", _rows[d.row].number);
		return std::string (b) + d.tag + (j.tag.empty () ? std::string () : " " + j.tag);
	};
	return end (0) + " \xe2\x86\x92 " + end (1); /* → */
}

std::vector<std::string>
McrPatch::history () const
{
	std::vector<std::string> rv;
	if (_selected < 0) {
		return rv;
	}
	Watch const*                       w  = watch (_selected);
	PdCord::CordDetective const* const cd = w ? _detective.cord (w->handle) : 0;
	if (!cd || cd->history ().empty ()) {
		rv.push_back (PdCord::CordDetective::no_history);
		return rv;
	}
	for (auto i = cd->history ().rbegin (); i != cd->history ().rend (); ++i) {
		rv.push_back (PdCord::CordDetective::history_row (*i));
	}
	return rv;
}

bool
McrPatch::needs_timer () const
{
	return _detective.enabled () || (_drag.on && _drag.sliver >= 0) || _note.until != 0;
}

void
McrPatch::update_timer ()
{
	if (needs_timer () && !_timer.connected ()) {
		_timer = Glib::signal_timeout ().connect (sigc::mem_fun (*this, &McrPatch::timer), 16);
	}
}

bool
McrPatch::timer ()
{
	tick (now ());
	flush ();
	return needs_timer ();
}

void
McrPatch::flush ()
{
	if (!_relayout) {
		return;
	}
	_relayout = false;
	Relayout ();
	redraw_all ();
}

void
McrPatch::tick (int64_t t)
{
	double const dt = _last_tick ? std::max (0., (t - _last_tick) / 1e6) : 0;
	_last_tick      = t;

	if (_drag.on && _drag.sliver >= 0 && t - _drag.since >= spring_us) {
		spring ();
	}

	if (_note.until && t >= _note.until - fade_us) {
		redraw (_note.drawn);
		if (t >= _note.until) {
			_note = Note ();
		}
	}

	if (!_detective.enabled ()) {
		flush ();
		return;
	}

	/* the taps, on the GUI thread */
	for (auto const& c : _cords) {
		auto i = _watches.find (c.key);
		if (i == _watches.end () || !i->second.tap) {
			continue;
		}
		i->second.tap->poll (_detective, i->second.handle);
		i->second.level = i->second.tap->level ();
	}

	_detective.tick (t);

	std::set<uint32_t> const changed (_detective.changed ().begin (), _detective.changed ().end ());
	PdCord::Style const      style = (PdCord::Style) _theme->connection_style;
	bool                     hist  = false;

	for (size_t ci = 0; ci < _cords.size (); ++ci) {
		Cord const& c = _cords[ci];
		auto        i = _watches.find (c.key);
		if (i == _watches.end ()) {
			continue;
		}
		Watch&                             w  = i->second;
		PdCord::CordDetective const* const cd = _detective.cord (w.handle);
		if (!cd) {
			continue;
		}

		bool dirty = changed.count (w.handle) > 0;

		/* a new message: a closed sliver shows it, and blinks */
		if (!cd->history ().empty () && cd->history ().back ().number != w.seen) {
			w.seen  = cd->history ().back ().number;
			w.last  = cd->history ().back ().text;
			w.blink = t + blink_us;
			dirty   = true;
			hist    = hist || (int) ci == _selected;
		} else if (w.blink && t >= w.blink) {
			w.blink = 0;
			dirty   = true;
		}

		/* audio dashes march with the level */
		if (c.kind == PdCord::Signal && w.level > 1e-4f && !_detective.paused ()) {
			w.dashes += dt * 4 * std::min (1.f, w.level) * _detective.speed ();
			w.dashes -= floor (w.dashes);
			dirty = dirty || !c.folded;
		}

		if (dirty) {
			Rect const before = c.bounds.unite (w.drawn);
			w.drawn           = c.folded ? Rect () : cd->bounds (c.path, PdCord::width (style, c.kind), bubble_view ());
			redraw (before.unite (w.drawn));
		}
	}

	if (hist) {
		HistoryChanged ();
	}
	flush ();
}

/* ---- McrPatchCables: the session's cables ---- */

namespace {

class CableTap : public McrPatchTap
{
public:
	CableTap (std::shared_ptr<McrLevelTap> level, std::shared_ptr<McrMidiTap> midi)
		: _level (level)
		, _midi (midi)
	{}

	void poll (PdCord::Detective& d, uint32_t cord)
	{
		if (_midi) {
			_midi->poll ();
			McrMidiTap::Event ev[64];
			size_t            n;
			while ((n = _midi->read (ev, 64)) > 0) {
				for (size_t i = 0; i < n; ++i) {
					PdCord::TraceEvent te = PdCord::TraceEvent ();
					te.cord      = cord;
					te.type      = PdCord::TraceEvent::Midi;
					te.size      = (uint8_t) std::min<size_t> (ev[i].size, sizeof (ev[i].data));
					te.truncated = ev[i].size > sizeof (ev[i].data);
					te.when      = ev[i].time;
					memcpy (te.midi, ev[i].data, te.size);
					d.trace (te);
				}
				if (n < 64) {
					break;
				}
			}
			return;
		}
		float const l = level ();
		if (l > 1e-4f) {
			d.trace_audio (cord, ARDOUR::AudioEngine::instance ()->sample_time (), &l, 1);
		}
	}

	float level ()
	{
		if (!_level || _midi) {
			return 0;
		}
		float const db = _level->level ();
		if (!(db > -200.f)) {
			return 0;
		}
		return dB_to_coefficient (db);
	}

private:
	std::shared_ptr<McrLevelTap> _level;
	std::shared_ptr<McrMidiTap>  _midi;
};

McrPin
pin_of (McrPatchEnd const& e)
{
	return McrPin (e.insert, e.type == McrPatchJack::Midi ? ARDOUR::DataType::MIDI : ARDOUR::DataType::AUDIO, e.pin, e.instance);
}

std::shared_ptr<McrCable>
record_of (McrPatchCable const& c)
{
	return std::static_pointer_cast<McrCable> (c.record);
}

} // namespace

McrPatchCables::McrPatchCables (McrCables& c)
	: _cables (c)
{
	_cables.Changed.connect_same_thread (_changed, [this] () { Changed (); });
}

McrPatchCables::~McrPatchCables ()
{
}

uint64_t
McrPatchCables::key (PBD::ID const& id)
{
	return strtoull (id.to_s ().c_str (), 0, 10);
}

std::vector<McrPatchJack>
McrPatchCables::jacks (McrPatchDevice const& d)
{
	std::vector<McrPatchJack> rv;
	if (!d.insert) {
		return rv;
	}
	for (auto const& j : McrCables::jacks (d.insert)) {
		if (j.type != ARDOUR::DataType::AUDIO && j.type != ARDOUR::DataType::MIDI) {
			continue;
		}
		McrPatchJack p;
		p.type      = j.type == ARDOUR::DataType::MIDI ? McrPatchJack::Midi : McrPatchJack::Audio;
		p.input     = j.input;
		p.instance  = j.instance;
		p.pin       = j.pin;
		p.name      = j.name;
		p.sidechain = j.sidechain;
		p.cables    = j.input ? j.aux : true;
		p.wired     = j.main || j.wired;
		rv.push_back (p);
	}
	return rv;
}

std::vector<McrPatchCable>
McrPatchCables::cables ()
{
	std::vector<McrPatchCable> rv;
	for (auto const& c : _cables.cables ()) {
		if (c.type != ARDOUR::DataType::AUDIO && c.type != ARDOUR::DataType::MIDI) {
			continue;
		}
		std::shared_ptr<ARDOUR::Route>        sr (c.source_route.lock ());
		std::shared_ptr<ARDOUR::Route>        tr (c.target_route.lock ());
		std::shared_ptr<ARDOUR::PluginInsert> s (c.source.lock ());
		std::shared_ptr<ARDOUR::PluginInsert> t (c.target.lock ());
		if (!sr || !tr || !t) {
			continue;
		}
		McrPatchCable p;
		p.type            = c.type == ARDOUR::DataType::MIDI ? McrPatchJack::Midi : McrPatchJack::Audio;
		p.source_row      = key (sr->id ());
		p.target_row      = key (tr->id ());
		p.source          = s ? key (s->id ()) : 0;
		p.target          = key (t->id ());
		p.source_instance = c.source_instance;
		p.source_pin      = c.source_pin == McrCable::no_pin ? McrPatchJack::no_pin : c.source_pin;
		p.target_instance = c.target_instance;
		p.target_pin      = c.target_pin == McrCable::no_pin ? McrPatchJack::no_pin : c.target_pin;
		p.mcr             = c.mcr;
		p.record          = std::make_shared<McrCable> (c);
		rv.push_back (p);
	}
	return rv;
}

std::string
McrPatchCables::can_connect (McrPatchEnd const& from, McrPatchEnd const& to)
{
	if (!from.insert || !to.insert) {
		return _("No device");
	}
	return _cables.can_connect (pin_of (from), pin_of (to));
}

std::string
McrPatchCables::connect (McrPatchEnd const& from, McrPatchEnd const& to)
{
	if (!from.insert || !to.insert) {
		return _("No device");
	}
	return _cables.connect (pin_of (from), pin_of (to));
}

std::string
McrPatchCables::disconnect (McrPatchCable const& c)
{
	std::shared_ptr<McrCable> r (record_of (c));
	if (!r) {
		return _("That cable is gone");
	}
	return _cables.disconnect (*r);
}

bool
McrPatchCables::gain (McrPatchCable const& c, double& factor)
{
	std::shared_ptr<McrCable> r (record_of (c));
	if (!r || r->type != ARDOUR::DataType::AUDIO) {
		return false;
	}
	std::shared_ptr<ARDOUR::Send> s (r->send.lock ());
	if (!s || !s->gain_control ()) {
		return false;
	}

	/* one send carries all of a device's cables: its gain is this cable's alone only
	 * when no other cable goes through it */
	int n = 0;
	for (auto const& o : _cables.cables ()) {
		if (o.send.lock () == s) {
			++n;
		}
	}
	if (n != 1) {
		return false;
	}
	factor = s->gain_control ()->get_value ();
	return true;
}

bool
McrPatchCables::set_gain (McrPatchCable const& c, double factor)
{
	double g;
	if (!gain (c, g)) {
		return false;
	}
	std::shared_ptr<ARDOUR::Send> s (record_of (c)->send.lock ());
	s->gain_control ()->set_value (factor, PBD::Controllable::NoGroup);
	return true;
}

std::shared_ptr<McrPatchTap>
McrPatchCables::tap (McrPatchCable const& c)
{
	std::shared_ptr<McrCable> r (record_of (c));
	if (!r) {
		return std::shared_ptr<McrPatchTap> ();
	}
	if (r->type == ARDOUR::DataType::MIDI) {
		std::shared_ptr<McrMidiTap> m (_cables.midi_tap (*r));
		if (!m) {
			return std::shared_ptr<McrPatchTap> ();
		}
		return std::shared_ptr<McrPatchTap> (new CableTap (std::shared_ptr<McrLevelTap> (), m));
	}
	std::shared_ptr<McrLevelTap> l (_cables.level_tap (*r));
	if (!l) {
		return std::shared_ptr<McrPatchTap> ();
	}
	return std::shared_ptr<McrPatchTap> (new CableTap (l, std::shared_ptr<McrMidiTap> ()));
}
