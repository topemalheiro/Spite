/*
 * Copyright (C) 2021-2025 Timothy Schoen (plugdata)
 * Copyright (C) 2026 Timothy Schoen (plugdata)
 * Copyright (C) 2026 Spite
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
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
#include <cstdio>
#include <cstring>

#include <glib.h>
#include <pango/pangocairo.h>

#include "pd_detective.h"

using namespace PdCord;
using namespace PdColor;

static_assert (std::atomic<uint64_t>::is_always_lock_free, "the Detective's sequence counter must be lock free");
static_assert (std::atomic<size_t>::is_always_lock_free, "the Detective's rings must be lock free");

namespace {

/* the bubble's measures (Connection.cpp:32-38) */
double const bubble_padding_x = 8;
double const bubble_padding_y = 6;
double const bubble_outline = 1;
double const bubble_font_height = 13;
double const bubble_gap = 10;
double const bubble_max_width = 560;
double const bubble_radius = 6;

void
circle (cairo_t* cr, Pt c, double r)
{
	cairo_new_sub_path (cr);
	cairo_arc (cr, c.x, c.y, r, 0, 2 * M_PI);
}

void
rounded_rect (cairo_t* cr, Rect const& r, double radius)
{
	double const rr = std::max (0., std::min (radius, std::min (r.width (), r.height ()) / 2));
	cairo_new_sub_path (cr);
	cairo_arc (cr, r.x1 - rr, r.y0 + rr, rr, -M_PI / 2, 0);
	cairo_arc (cr, r.x1 - rr, r.y1 - rr, rr, 0, M_PI / 2);
	cairo_arc (cr, r.x0 + rr, r.y1 - rr, rr, M_PI / 2, M_PI);
	cairo_arc (cr, r.x0 + rr, r.y0 + rr, rr, M_PI, 3 * M_PI / 2);
	cairo_close_path (cr);
}

/* shifted into the area, and no bigger than it (as JUCE's Rectangle::constrainedWithin) */
Rect
constrained (Rect const& r, Rect const& area)
{
	double const w = std::min (r.width (), area.width ());
	double const h = std::min (r.height (), area.height ());
	double const x = std::max (area.x0, std::min (r.x0, area.x1 - w));
	double const y = std::max (area.y0, std::min (r.y0, area.y1 - h));
	return Rect::sized (x, y, w, h);
}

/* to whole units, its position and its size each rounded (JUCE's toNearestInt) */
Rect
rounded (Rect const& r)
{
	return Rect::sized (lround (r.x0), lround (r.y0), lround (r.width ()), lround (r.height ()));
}

size_t
utf8_length (std::string const& s)
{
	size_t n = 0;
	for (unsigned char c : s) {
		n += (c & 0xc0) != 0x80;
	}
	return n;
}

std::string
utf8_prefix (std::string const& s, size_t chars)
{
	size_t n = 0;
	for (size_t i = 0; i < s.size (); ++i) {
		if (((unsigned char) s[i] & 0xc0) != 0x80) {
			if (n == chars) {
				return s.substr (0, i);
			}
			++n;
		}
	}
	return s;
}

std::string
int_text (int v)
{
	char b[16];
	snprintf (b, sizeof (b), "%d", v);
	return b;
}

}

/* ---- the ring ---- */

TraceRing::TraceRing (size_t capacity)
	: _mask (0)
	, _write (0)
	, _read (0)
{
	size_t n = 1;
	while (n < capacity) {
		n <<= 1;
	}
	_buf.resize (n);
	_mask = n - 1;
}

bool
TraceRing::push (TraceEvent const& ev)
{
	size_t const w = _write.load (std::memory_order_relaxed);
	if (w - _read.load (std::memory_order_acquire) > _mask) {
		return false;
	}
	_buf[w & _mask] = ev;
	_write.store (w + 1, std::memory_order_release);
	return true;
}

bool
TraceRing::pop (TraceEvent& ev)
{
	size_t const r = _read.load (std::memory_order_relaxed);
	if (r == _write.load (std::memory_order_acquire)) {
		return false;
	}
	ev = _buf[r & _mask];
	_read.store (r + 1, std::memory_order_release);
	return true;
}

/* ---- the bubbles ---- */

struct CordDetective::Bubble
{
	PangoLayout* layout;
	int wrap;
	double w;
	double h;

	Bubble () : layout (0), wrap (-1), w (0), h (0) {}
	~Bubble ()
	{
		if (layout) {
			g_object_unref (layout);
		}
	}

private:
	Bubble (Bubble const&);
	Bubble& operator= (Bubble const&);
};

int
PdCord::bubble_wrap_width (Rect const& view)
{
	/* whole pixels: the text is measured at this width and drawn at it */
	double const widest = std::min (bubble_max_width, std::max (60., view.width () - 16));
	return std::max (1, (int) (widest - 2 * (bubble_padding_x + bubble_outline)));
}

Rect
PdCord::place_bubble (Rect const& view, Pt p, double w, double h, double offset)
{
	double x;
	double y;

	double const right_x = p.x + offset;
	double const left_x = p.x - w - offset;
	if (right_x + w <= view.x1 || left_x < view.x0) {
		x = right_x;
	} else if (left_x >= view.x0) {
		x = left_x;
	} else {
		x = std::max (view.x0, std::min (view.x1 - w, right_x));
	}

	double const above_y = p.y - h - offset;
	double const below_y = p.y + offset;
	if (above_y >= view.y0 || below_y + h > view.y1) {
		y = above_y;
	} else if (below_y + h <= view.y1) {
		y = below_y;
	} else {
		y = std::max (view.y0, std::min (view.y1 - h, below_y));
	}

	return constrained (Rect::sized (x, y, w, h), view);
}

Rect
PdCord::bubble_bounds (Rect const& view, Pt p, double text_w, double text_h, double offset)
{
	if (view.empty ()) {
		return Rect ();
	}
	return place_bubble (view, p, text_w + 2 * (bubble_padding_x + bubble_outline), text_h + 2 * (bubble_padding_y + bubble_outline), offset);
}

void
PdCord::render_dot (cairo_t* cr, Pt p, double w, PdTheme const& th)
{
	cairo_save (cr);
	cairo_new_path (cr);
	circle (cr, p, w);
	cairo_set_source_rgba (cr, 1, 1, 1, 0.6);
	cairo_fill (cr);
	circle (cr, p, w * 0.75);
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_fill (cr);
	circle (cr, p, w * 0.45);
	set_source (cr, th.data);
	cairo_fill (cr);
	cairo_restore (cr);
}

void
PdCord::render_detective_button (cairo_t* cr, Rect const& r, bool on, bool hovered, bool sensitive, PdTheme const& th)
{
	Gtkmm2ext::Color c = th.toolbar_text;
	if (!sensitive) {
		c = rgba (0x80, 0x80, 0x80);
	} else if (on) {
		c = th.toolbar_active;
	} else if (hovered) {
		c = brighter (th.toolbar_text, 0.8f);
	}

	PangoLayout* layout = text_layout ("Detective", 11);
	PangoRectangle ink;
	PangoRectangle logical;
	pango_layout_get_extents (layout, &ink, &logical);

	cairo_save (cr);
	cairo_new_path (cr);
	cairo_rectangle (cr, r.x0, r.y0, r.width (), r.height ());
	cairo_clip (cr);
	Pt const c0 = r.centre ();
	cairo_move_to (cr, c0.x - logical.width / (2. * PANGO_SCALE), c0.y - logical.height / (2. * PANGO_SCALE));
	set_source (cr, c);
	pango_cairo_show_layout (cr, layout);
	/* cairo_restore () keeps the path: leave no current point behind */
	cairo_new_path (cr);
	cairo_restore (cr);

	g_object_unref (layout);
}

/* ---- what a message says ---- */

std::string
PdCord::format_message (std::string const& selector, std::vector<std::string> const& atoms, bool truncated, int max)
{
	std::string text = selector;
	bool cut = truncated;

	for (auto const& a : atoms) {
		if (max >= 0 && utf8_length (text) >= (size_t) max) {
			cut = true;
			break;
		}
		text += " " + a;
	}

	if (max >= 0 && utf8_length (text) > (size_t) max) {
		text = utf8_prefix (text, max);
		cut = true;
	}

	if (cut) {
		text += " \xe2\x80\xa6";
	}
	return text;
}

std::string
PdCord::pd_float (double v)
{
	/* with a point whatever the user's locale, which Spite takes at startup: JUCE's String
	 * writes numbers in the classic locale (juce_String.cpp:472-477), Pd with a point */
	char b[G_ASCII_DTOSTR_BUF_SIZE];
	g_ascii_formatd (b, sizeof (b), "%g", v);
	return b;
}

std::string
PdCord::midi_text (uint8_t const* bytes, size_t size, bool truncated, bool with_channel)
{
	if (!bytes || size == 0) {
		return std::string ();
	}

	uint8_t const status = bytes[0];
	auto data = [&] (size_t i) { return i < size ? (int) (bytes[i] & 0x7f) : 0; };

	std::string selector;
	std::vector<std::string> atoms;

	switch (status & 0xf0) {
		case 0x80:
			/* Pd's notein gives a note off as velocity 0 */
			selector = "note";
			atoms.push_back (int_text (data (1)));
			atoms.push_back ("0");
			break;
		case 0x90:
			selector = "note";
			atoms.push_back (int_text (data (1)));
			atoms.push_back (int_text (data (2)));
			break;
		case 0xa0:
			/* value first, then the key, as Pd's polytouchin */
			selector = "polytouch";
			atoms.push_back (int_text (data (2)));
			atoms.push_back (int_text (data (1)));
			break;
		case 0xb0:
			/* value first, then the controller, as Pd's ctlin */
			selector = "ctl";
			atoms.push_back (int_text (data (2)));
			atoms.push_back (int_text (data (1)));
			break;
		case 0xc0:
			selector = "pgm";
			atoms.push_back (int_text (data (1) + 1));
			break;
		case 0xd0:
			selector = "touch";
			atoms.push_back (int_text (data (1)));
			break;
		case 0xe0:
			selector = "bend";
			atoms.push_back (int_text ((data (2) << 7) | data (1)));
			break;
		default:
			/* system messages: their bytes, as Pd's sysexin and midiin give them */
			selector = status == 0xf0 ? "sysex" : "midi";
			for (size_t i = 0; i < size; ++i) {
				atoms.push_back (int_text (bytes[i]));
			}
			return format_message (selector, atoms, truncated, CordDetective::max_text);
	}

	if (with_channel) {
		atoms.push_back (int_text ((status & 0x0f) + 1));
	}
	return format_message (selector, atoms, truncated, CordDetective::max_text);
}

std::string
PdCord::level_text (float const* peaks, size_t nchans)
{
	std::string s;
	for (size_t i = 0; peaks && i < nchans; ++i) {
		/* with a point, as pd_float (); room for the widest float */
		char b[64];
		g_ascii_formatd (b, sizeof (b), "%.3f", peaks[i]);
		if (i) {
			s += ' ';
		}
		s += b;
	}
	return s;
}

std::string
PdCord::value_text (float v)
{
	return format_message ("float", std::vector<std::string> (1, pd_float (v)));
}

std::string
PdCord::event_text (TraceEvent const& ev)
{
	switch (ev.type) {
		case TraceEvent::Midi:
			return midi_text (ev.midi, std::min<size_t> (ev.size, sizeof (ev.midi)), ev.truncated);
		case TraceEvent::Audio:
			/* more channels than the event holds: marked as cut, as a long MIDI message is */
			return format_message (level_text (ev.level, std::min<size_t> (ev.size, sizeof (ev.level) / sizeof (ev.level[0]))), std::vector<std::string> (), ev.truncated);
		case TraceEvent::Value:
			return value_text (ev.value);
	}
	return std::string ();
}

/* ---- one cord ---- */

char const* const CordDetective::no_history = "No messages recorded yet.";

CordDetective::CordDetective ()
	: _scheduled_done (false)
	, _count (0)
{
}

void
CordDetective::add_history (Message const& m)
{
	Message h (m.text, m.number, m.audio);
	_history.push_back (h);
	while (_history.size () > max_history) {
		_history.pop_front ();
	}
}

void
CordDetective::push (std::string const& text, bool audio)
{
	Message const m (text, ++_count, audio);
	_flying.push_back (m);
	while (_flying.size () > max_in_flight) {
		_flying.pop_front ();
	}
	add_history (m);
}

void
CordDetective::record (std::string const& text)
{
	add_history (Message (text, ++_count));
}

void
CordDetective::schedule (std::string const& text)
{
	_scheduled.reset (new Message (text, _count));
	_scheduled_done = false;
}

void
CordDetective::clear_scheduled ()
{
	_scheduled.reset ();
	_scheduled_done = false;
}

bool
CordDetective::advance (double ms)
{
	bool moving = false;

	for (auto& m : _flying) {
		m.progress = std::min (1., std::max (0., m.progress + ms / duration_ms));
		if (m.progress < 1) {
			moving = true;
		}
	}

	while (!_flying.empty () && _flying.front ().progress >= 1) {
		_flying.pop_front ();
	}

	if (_scheduled) {
		_scheduled->progress = std::min (1., std::max (0., _scheduled->progress + ms / duration_ms));
		if (_scheduled->progress >= 1) {
			_scheduled_done = true;
		} else {
			moving = true;
		}
	}

	return moving;
}

void
CordDetective::clear ()
{
	_flying.clear ();
	if (_scheduled) {
		/* done, so a scheduler waiting on it moves on */
		_scheduled.reset ();
		_scheduled_done = true;
	}
}

void
CordDetective::prepare (Message const& m, int wrap) const
{
	if (!m.bubble) {
		m.bubble.reset (new Bubble);
	}

	Bubble& b = *m.bubble;
	if (b.wrap == wrap || m.text.empty ()) {
		return;
	}

	if (!b.layout) {
		b.layout = text_layout (m.text, bubble_font_height);
		pango_layout_set_wrap (b.layout, PANGO_WRAP_WORD_CHAR);
	}

	/* between words, and inside a word too long for a line (Connection.cpp:116-122) */
	pango_layout_set_width (b.layout, wrap * PANGO_SCALE);

	PangoRectangle ink;
	PangoRectangle logical;
	pango_layout_get_extents (b.layout, &ink, &logical);

	/* whole pixels, so that rounding the bubble's place never cuts the text */
	b.w = ceil (logical.width / (double) PANGO_SCALE);
	b.h = ceil (logical.height / (double) PANGO_SCALE);
	b.wrap = wrap;
}

Rect
CordDetective::bounds (Path const& path, double cord_width, Rect const& view) const
{
	/* plugdata's cord bounds: its stroke grown to 12 wide, and 3 more */
	Rect r = path.bounds ().expand (9);
	if (!active () || path.length () <= 0) {
		return r;
	}

	double const len = path.length ();
	double const offset = std::max (bubble_gap, cord_width + 4);
	int const wrap = bubble_wrap_width (view);

	auto add = [&] (Message const& m) {
		prepare (m, wrap);
		if (m.text.empty ()) {
			return;
		}
		r = r.unite (bubble_bounds (view, path.point_at (m.progress * len), m.bubble->w, m.bubble->h, offset));
	};

	for (auto const& m : _flying) {
		add (m);
	}
	if (_scheduled) {
		add (*_scheduled);
	}
	return r;
}

void
CordDetective::render (cairo_t* cr, Path const& path, double cord_width, Rect const& view, PdTheme const& th) const
{
	if (!active () || path.empty ()) {
		return;
	}

	double const len = path.length ();
	double const offset = std::max (bubble_gap, cord_width + 4);
	int const wrap = bubble_wrap_width (view);

	cairo_save (cr);

	auto draw = [&] (Message const& m) {
		Pt const p = path.point_at (m.progress * len);

		prepare (m, wrap);

		if (!m.text.empty ()) {
			Rect const b = rounded (bubble_bounds (view, p, m.bubble->w, m.bubble->h, offset));
			if (!b.empty ()) {
				Rect const o = b.reduce (bubble_outline * 0.5);
				cairo_new_path (cr);
				rounded_rect (cr, o, bubble_radius);
				set_source (cr, th.bubble_fill ());
				cairo_fill_preserve (cr);
				set_source (cr, th.outline);
				cairo_set_line_width (cr, bubble_outline);
				cairo_stroke (cr);

				Rect const t = b.reduce (bubble_outline + bubble_padding_x, bubble_outline + bubble_padding_y);
				if (!t.empty ()) {
					cairo_save (cr);
					cairo_rectangle (cr, t.x0, t.y0, t.width (), t.height ());
					cairo_clip (cr);
					cairo_move_to (cr, t.x0, t.y0);
					set_source (cr, th.canvas_text);
					pango_cairo_show_layout (cr, m.bubble->layout);
					cairo_restore (cr);
				}
			}
		}

		/* the dot goes over its bubble */
		render_dot (cr, p, cord_width, th);
	};

	for (auto const& m : _flying) {
		draw (m);
	}
	if (_scheduled) {
		draw (*_scheduled);
	}

	cairo_restore (cr);
}

std::string
CordDetective::history_row (Message const& m)
{
	char b[32];
	snprintf (b, sizeof (b), "#%llu  ", (unsigned long long) m.number);
	return b + m.text;
}

/* ---- all cords ---- */

namespace {
uint32_t const index_bits = 12; /* max_cords is 1 << index_bits */
uint32_t const index_mask = (1u << index_bits) - 1;
}

static_assert ((1u << 12) == Detective::max_cords, "a cord's handle keeps its index in 12 bits");

Detective&
Detective::instance ()
{
	static Detective* d = new Detective; /* never freed: the engine may trace while the GUI goes */
	return *d;
}

Detective::Detective (size_t ring_size)
	: _ring_size (ring_size)
	, _slots (max_cords)
	, _table (new std::atomic<TraceRing*>[max_cords])
	, _handles (new std::atomic<uint32_t>[max_cords])
	, _next (0)
	, _generation (max_cords, 0)
	, _enabled (true)
	, _seq (0)
	, _speed (1.f)
	, _paused (false)
	, _step (false)
	, _execution_order (false)
	, _animating (false)
	, _last_us (0)
	, _current (0)
{
	for (uint32_t i = 0; i < max_cords; ++i) {
		_table[i].store (0, std::memory_order_relaxed);
		_handles[i].store (0, std::memory_order_relaxed);
	}
	_events.reserve (1024);
}

Detective::~Detective ()
{
}

Detective::Slot*
Detective::slot (uint32_t handle)
{
	if (!handle) {
		return 0;
	}
	Slot& s = _slots[handle & index_mask];
	return s.handle == handle ? &s : 0;
}

Detective::Slot const*
Detective::slot (uint32_t handle) const
{
	if (!handle) {
		return 0;
	}
	Slot const& s = _slots[handle & index_mask];
	return s.handle == handle ? &s : 0;
}

void
Detective::dirty (Slot& s)
{
	if (!s.dirty) {
		s.dirty = true;
		_changed.push_back (s.handle);
	}
}

uint32_t
Detective::add_cord ()
{
	uint32_t index;
	if (!_free.empty ()) {
		index = _free.back ();
		_free.pop_back ();
	} else if (_next < max_cords) {
		index = _next++;
	} else {
		return 0;
	}

	/* generations from 1, so that no handle is 0 */
	_generation[index] = (_generation[index] + 1) & (0xffffffffu >> index_bits);
	if (_generation[index] == 0) {
		_generation[index] = 1;
	}

	Slot& s = _slots[index];
	s.handle = (_generation[index] << index_bits) | index;
	s.state.reset (new CordDetective);
	s.audio.clear ();
	s.audio_pending = false;
	s.audio_queued = false;
	s.dirty = false;

	if (!s.ring) {
		s.ring.reset (new TraceRing (_ring_size));
	} else {
		/* what a late producer left in it belongs to the cord that had it */
		drain (s, false);
	}

	_handles[index].store (s.handle, std::memory_order_relaxed);
	_table[index].store (s.ring.get (), std::memory_order_release);
	return s.handle;
}

void
Detective::remove_cord (uint32_t handle)
{
	Slot* s = slot (handle);
	if (!s) {
		return;
	}

	uint32_t const index = handle & index_mask;

	/* the ring stays: a process thread may still be writing to it */
	_table[index].store (0, std::memory_order_release);
	_handles[index].store (0, std::memory_order_relaxed);
	s->handle = 0;
	s->state.reset ();
	s->audio.clear ();
	s->audio_pending = false;
	s->audio_queued = false;

	if (_current == handle) {
		_current = 0;
	}
	_free.push_back (index);
}

CordDetective*
Detective::cord (uint32_t handle)
{
	Slot* s = slot (handle);
	return s ? s->state.get () : 0;
}

CordDetective const*
Detective::cord (uint32_t handle) const
{
	Slot const* s = slot (handle);
	return s ? s->state.get () : 0;
}

void
Detective::set_enabled (bool yn)
{
	_enabled.store (yn, std::memory_order_relaxed);
}

void
Detective::set_speed (float s)
{
	_speed = std::min (5.f, std::max (0.1f, s));
}

void
Detective::set_paused (bool yn)
{
	_paused = yn;
}

void
Detective::step ()
{
	_step = true;
}

void
Detective::set_execution_order (bool yn)
{
	if (_execution_order == yn) {
		return;
	}
	_execution_order = yn;

	/* as plugdata's scheduler does when it is switched off: forget what it was playing */
	_queue.clear ();
	if (Slot* s = slot (_current)) {
		s->state->clear_scheduled ();
	}
	_current = 0;
	for (auto& s : _slots) {
		s.audio_queued = false;
	}
}

void
Detective::drain (Slot& s, bool keep)
{
	TraceEvent ev;
	while (s.ring->pop (ev)) {
		if (keep && ev.cord == s.handle) {
			_events.push_back (ev);
		}
	}
}

void
Detective::tick (int64_t now)
{
	for (auto h : _changed) {
		if (Slot* s = slot (h)) {
			s->dirty = false;
		}
	}
	_changed.clear ();

	if (!enabled ()) {
		/* switched off: what is still drawn goes (Connection.cpp:1524-1538) */
		for (auto& s : _slots) {
			if (!s.handle) {
				continue;
			}
			drain (s, false);
			s.audio_pending = false;
			s.audio_queued = false;
			if (s.state->active ()) {
				s.state->clear ();
				s.state->clear_scheduled ();
				dirty (s);
			}
		}
		_queue.clear ();
		_current = 0;
		_animating = false;
		return;
	}

	/* 1. the messages in flight move on (Connection::timerCallback, Connection.cpp:1522-1589) */
	double ms = _animating ? (now - _last_us) / 1000. : 0.;
	_last_us = now;

	bool const was_step = _step;
	_step = false;

	if (!_paused || was_step) {
		if (was_step) {
			ms = CordDetective::step_ms;
		}
		ms *= _speed;

		for (auto& s : _slots) {
			if (s.handle && s.state->active ()) {
				s.state->advance (ms);
				dirty (s);
			}
		}
	}

	/* 2. the engine's events, in the order they happened */
	_events.clear ();
	for (auto& s : _slots) {
		if (s.handle) {
			drain (s, true);
		}
	}
	std::sort (_events.begin (), _events.end (), [] (TraceEvent const& a, TraceEvent const& b) {
		return a.when != b.when ? a.when < b.when : a.seq < b.seq;
	});

	for (auto const& ev : _events) {
		Slot* s = slot (ev.cord);
		if (!s) {
			continue;
		}

		std::string const text = event_text (ev);
		bool const audio = ev.type == TraceEvent::Audio;

		if (_execution_order) {
			if (audio) {
				/* audio levels come every cycle: one waits in the queue, with the newest level */
				s->audio = text;
				if (!s->audio_queued && _queue.size () < max_queue) {
					Queued q = { ev.cord, std::string (), true };
					_queue.push_back (q);
					s->audio_queued = true;
				}
			} else {
				s->state->record (text);
				if (_queue.size () < max_queue) {
					Queued q = { ev.cord, text, false };
					_queue.push_back (q);
				}
			}
		} else if (audio) {
			/* one level at a time along an audio cord, the newest when the last one arrives */
			s->audio = text;
			s->audio_pending = true;
		} else {
			s->state->push (text);
			dirty (*s);
		}
	}

	for (auto& s : _slots) {
		if (s.handle && s.audio_pending && s.state->in_flight ().empty ()) {
			s.state->push (s.audio, true);
			s.audio_pending = false;
			dirty (s);
		}
	}

	/* 3. execution order: the next message once the last one arrived
	 * (DebugExecutionScheduler::update, DebugExecutionScheduler.cpp:182-219) */
	if (_execution_order) {
		if (_current) {
			Slot* s = slot (_current);
			if (!s || s->state->scheduled_done ()) {
				if (s) {
					s->state->clear_scheduled ();
					dirty (*s);
				}
				_current = 0;
			}
		}

		while (!_current && !_queue.empty ()) {
			Queued const q = _queue.front ();
			_queue.pop_front ();

			Slot* s = slot (q.cord);
			if (!s) {
				continue;
			}

			std::string text = q.text;
			if (q.audio) {
				text = s->audio;
				s->audio_queued = false;
				s->state->record (text);
			}

			s->state->schedule (text);
			_current = q.cord;
			dirty (*s);
		}
	}

	_animating = !_queue.empty ();
	for (auto const& s : _slots) {
		if (s.handle && (s.state->active () || s.audio_pending)) {
			_animating = true;
			break;
		}
	}
}

bool
Detective::trace (TraceEvent const& ev)
{
	if (!ev.cord || !_enabled.load (std::memory_order_relaxed)) {
		return false;
	}

	uint32_t const index = ev.cord & index_mask;
	TraceRing* ring = _table[index].load (std::memory_order_acquire);
	if (!ring || _handles[index].load (std::memory_order_relaxed) != ev.cord) {
		/* a cord that went; should one slip by as its index is reused, tick () drops it */
		return false;
	}

	TraceEvent e = ev;
	e.seq = _seq.fetch_add (1, std::memory_order_relaxed) + 1;
	return ring->push (e);
}

bool
Detective::trace_midi (uint32_t cord, int64_t when, uint8_t const* bytes, size_t size)
{
	if (!bytes || size == 0) {
		return false;
	}

	TraceEvent ev;
	memset (&ev, 0, sizeof (ev));
	ev.cord = cord;
	ev.type = TraceEvent::Midi;
	ev.when = when;
	ev.size = (uint8_t) std::min (size, sizeof (ev.midi));
	ev.truncated = size > sizeof (ev.midi);
	memcpy (ev.midi, bytes, ev.size);
	return trace (ev);
}

bool
Detective::trace_audio (uint32_t cord, int64_t when, float const* peaks, uint32_t nchans)
{
	if (!peaks || nchans == 0) {
		return false;
	}

	TraceEvent ev;
	memset (&ev, 0, sizeof (ev));
	ev.cord = cord;
	ev.type = TraceEvent::Audio;
	ev.when = when;
	ev.size = (uint8_t) std::min<uint32_t> (nchans, sizeof (ev.level) / sizeof (ev.level[0]));
	ev.truncated = nchans > ev.size;
	for (uint32_t i = 0; i < ev.size; ++i) {
		ev.level[i] = peaks[i];
	}
	return trace (ev);
}

bool
Detective::trace_value (uint32_t cord, int64_t when, float value)
{
	TraceEvent ev;
	memset (&ev, 0, sizeof (ev));
	ev.cord = cord;
	ev.type = TraceEvent::Value;
	ev.when = when;
	ev.value = value;
	return trace (ev);
}
