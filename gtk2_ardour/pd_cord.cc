/*
 * Copyright (C) 2021-2025 Timothy Schoen (plugdata)
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
#include <cstdlib>
#include <locale>
#include <map>
#include <sstream>

#include <pango/pangocairo.h>
#ifndef __APPLE__
#include <fontconfig/fontconfig.h>
#include <pango/pangofc-fontmap.h>
#endif

#include "pd_cord.h"

using namespace PdCord;
using namespace PdColor;

/* ---- geometry ---- */

double
Pt::distance (Pt const& o) const
{
	return hypot (x - o.x, y - o.y);
}

Rect
Rect::spanning (Pt a, Pt b)
{
	return Rect (std::min (a.x, b.x), std::min (a.y, b.y), std::max (a.x, b.x), std::max (a.y, b.y));
}

Rect
Rect::unite (Rect const& o) const
{
	if (o.empty ()) {
		return *this;
	}
	if (empty ()) {
		return o;
	}
	return Rect (std::min (x0, o.x0), std::min (y0, o.y0), std::max (x1, o.x1), std::max (y1, o.y1));
}

bool
Rect::intersects (Rect const& o) const
{
	return !empty () && !o.empty () && x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1;
}

namespace {

double
segment_distance (Pt a, Pt b, Pt p, Pt* nearest = 0)
{
	double const dx = b.x - a.x;
	double const dy = b.y - a.y;
	double const len2 = dx * dx + dy * dy;
	double t = 0;
	if (len2 > 0) {
		t = std::min (1., std::max (0., ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2));
	}
	Pt const q (a.x + t * dx, a.y + t * dy);
	if (nearest) {
		*nearest = q;
	}
	return q.distance (p);
}

/* whether the line a-b touches the rect: an end inside it, or crossing an edge */
bool
segment_meets_rect (Pt a, Pt b, Rect const& r)
{
	if (r.contains (a) || r.contains (b)) {
		return true;
	}

	auto crosses = [] (Pt p, Pt q, Pt u, Pt v) {
		double const d = (q.x - p.x) * (v.y - u.y) - (q.y - p.y) * (v.x - u.x);
		if (d == 0) {
			return false;
		}
		double const s = ((u.x - p.x) * (v.y - u.y) - (u.y - p.y) * (v.x - u.x)) / d;
		double const t = ((u.x - p.x) * (q.y - p.y) - (u.y - p.y) * (q.x - p.x)) / d;
		return s >= 0 && s <= 1 && t >= 0 && t <= 1;
	};

	Pt const tl (r.x0, r.y0);
	Pt const tr (r.x1, r.y0);
	Pt const br (r.x1, r.y1);
	Pt const bl (r.x0, r.y1);
	return crosses (a, b, tl, tr) || crosses (a, b, tr, br) || crosses (a, b, br, bl) || crosses (a, b, bl, tl);
}

void
rounded_rect (cairo_t* cr, Rect const& r, double radius)
{
	double const rr = std::max (0., std::min (radius, std::min (r.width (), r.height ()) / 2));
	cairo_new_sub_path (cr);
	if (rr <= 0) {
		cairo_rectangle (cr, r.x0, r.y0, r.width (), r.height ());
		return;
	}
	cairo_arc (cr, r.x1 - rr, r.y0 + rr, rr, -M_PI / 2, 0);
	cairo_arc (cr, r.x1 - rr, r.y1 - rr, rr, 0, M_PI / 2);
	cairo_arc (cr, r.x0 + rr, r.y1 - rr, rr, M_PI / 2, M_PI);
	cairo_arc (cr, r.x0 + rr, r.y0 + rr, rr, M_PI, 3 * M_PI / 2);
	cairo_close_path (cr);
}

void
circle (cairo_t* cr, Pt c, double r)
{
	cairo_new_sub_path (cr);
	cairo_arc (cr, c.x, c.y, r, 0, 2 * M_PI);
}

}

/* ---- the path ---- */

void
Path::move_to (Pt p)
{
	Step s;
	s.op = MoveTo;
	s.p[0] = p;
	_steps.push_back (s);
}

void
Path::line_to (Pt p)
{
	Step s;
	s.op = LineTo;
	s.p[0] = p;
	_steps.push_back (s);
}

void
Path::curve_to (Pt c1, Pt c2, Pt p)
{
	Step s;
	s.op = CurveTo;
	s.p[0] = c1;
	s.p[1] = c2;
	s.p[2] = p;
	_steps.push_back (s);
}

void
Path::append_to (cairo_t* cr) const
{
	for (auto const& s : _steps) {
		switch (s.op) {
			case MoveTo:
				cairo_move_to (cr, s.p[0].x, s.p[0].y);
				break;
			case LineTo:
				cairo_line_to (cr, s.p[0].x, s.p[0].y);
				break;
			case CurveTo:
				cairo_curve_to (cr, s.p[0].x, s.p[0].y, s.p[1].x, s.p[1].y, s.p[2].x, s.p[2].y);
				break;
		}
	}
}

void
Path::flatten ()
{
	_flat.clear ();
	_dist.clear ();

	if (_steps.empty ()) {
		return;
	}

	/* Cairo cuts the curves into lines for us, finer than a tenth of a unit */
	cairo_surface_t* surface = cairo_image_surface_create (CAIRO_FORMAT_A8, 1, 1);
	cairo_t* cr = cairo_create (surface);
	cairo_set_tolerance (cr, 0.05);
	append_to (cr);

	cairo_path_t* flat = cairo_copy_path_flat (cr);
	for (int i = 0; i < flat->num_data; i += flat->data[i].header.length) {
		cairo_path_data_t const* d = &flat->data[i];
		if (d->header.type != CAIRO_PATH_MOVE_TO && d->header.type != CAIRO_PATH_LINE_TO) {
			continue;
		}
		Pt const p (d[1].point.x, d[1].point.y);
		if (_flat.empty ()) {
			_flat.push_back (p);
			_dist.push_back (0);
		} else if (p != _flat.back ()) {
			_dist.push_back (_dist.back () + p.distance (_flat.back ()));
			_flat.push_back (p);
		}
	}

	cairo_path_destroy (flat);
	cairo_destroy (cr);
	cairo_surface_destroy (surface);
}

Path
Path::line (Pt from, Pt to)
{
	Path p;
	p.move_to (from);
	p.line_to (to);
	p.flatten ();
	return p;
}

Path
Path::curve (Pt from, Pt to, bool straight, bool flipped)
{
	if (straight) {
		return line (from, to);
	}

	/* upside down, a cord that leaves the top of its source is an ordinary one */
	Pt start = from;
	Pt end = to;
	if (flipped) {
		start.y = -start.y;
		end.y = -end.y;
	}

	/* plugdata draws a line when the jacks are this close: its control points go wrong */
	if (end.distance (start) < 4) {
		return line (from, to);
	}

	double const w = std::max (start.x, end.x) - std::min (start.x, end.x);
	double const h = std::max (start.y, end.y) - std::min (start.y, end.y);
	double const lo = std::min (w, h);
	double const hi = std::max (w, h);

	double const max_shift_y = 20;
	double const max_shift_x = 20;

	double shift_y = std::min (max_shift_y, hi * 0.5);
	double const shift_x = (start.y >= end.y ? std::min (max_shift_x, lo * 0.5) : 0.) * (start.x < end.x ? -1. : 1.);

	/* a cord going up: pull its control points in so the curve does not loop away */
	double const x_offset = fabs (start.x - end.x);
	double const y_offset = start.y - end.y;

	if (x_offset <= 40 && start.y > end.y) {
		double const x_factor = pow (1. - x_offset / 40., 0.9);
		double const y_factor = pow (std::min (1., y_offset / 20.), 0.9);
		shift_y -= x_factor * y_factor * std::max (max_shift_y, y_offset * 0.5);

		if (x_offset <= 1 || shift_y <= (end.y - start.y) * 0.5) {
			return line (from, to);
		}
	}

	Pt c1 (start.x - shift_x, start.y + shift_y);
	Pt c2 (end.x + shift_x, end.y - shift_y);
	if (flipped) {
		c1.y = -c1.y;
		c2.y = -c2.y;
	}

	Path p;
	p.move_to (from);
	p.curve_to (c1, c2, to);
	p.flatten ();
	return p;
}

Path
Path::between (Pt from, bool from_top, Pt to, bool to_top, bool straight)
{
	if (!from_top && to_top) {
		return curve (from, to, straight, false);
	}
	if (from_top && !to_top) {
		return curve (from, to, straight, true);
	}
	if (straight || to.distance (from) < 4) {
		return line (from, to);
	}

	/* both on top edges: up out of the source and down into the target; both on bottom
	 * edges: down and up. As far as plugdata's curves turn, half the larger distance at most. */
	double const w = fabs (to.x - from.x);
	double const h = fabs (to.y - from.y);
	double const shift = std::min (20., std::max (w, h) * 0.5) * (from_top ? -1. : 1.);

	Path p;
	p.move_to (from);
	p.curve_to (Pt (from.x, from.y + shift), Pt (to.x, to.y + shift), to);
	p.flatten ();
	return p;
}

Path
Path::segmented (std::vector<Pt> const& plan, double radius)
{
	/* the corners, without the repeated points that keep the end segments draggable */
	std::vector<Pt> pts;
	for (auto const& p : plan) {
		if (pts.empty () || p != pts.back ()) {
			pts.push_back (p);
		}
	}

	Path path;
	if (pts.empty ()) {
		return path;
	}

	path.move_to (pts.front ());

	/* each corner takes up to radius, and at most half, of the two sides that meet there,
	 * and turns along a quadratic curve through the corner point (as a cubic for Cairo) */
	for (size_t i = 1; i + 1 < pts.size (); ++i) {
		Pt const& prev = pts[i - 1];
		Pt const& corner = pts[i];
		Pt const& next = pts[i + 1];

		Pt const a = corner + (prev - corner) * std::min (0.5, radius / prev.distance (corner));
		Pt const b = corner + (next - corner) * std::min (0.5, radius / next.distance (corner));

		path.line_to (a);
		path.curve_to (a + (corner - a) * (2. / 3.), b + (corner - b) * (2. / 3.), b);
	}

	path.line_to (pts.back ());
	path.flatten ();
	return path;
}

Pt
Path::start () const
{
	return _flat.empty () ? Pt () : _flat.front ();
}

Pt
Path::end () const
{
	return _flat.empty () ? Pt () : _flat.back ();
}

Pt
Path::point_at (double d) const
{
	if (_flat.empty ()) {
		return Pt ();
	}
	if (d <= 0 || _flat.size () == 1) {
		return _flat.front ();
	}
	if (d >= _dist.back ()) {
		return _flat.back ();
	}

	size_t const i = std::upper_bound (_dist.begin (), _dist.end (), d) - _dist.begin ();
	double const seg = _dist[i] - _dist[i - 1];
	double const t = seg > 0 ? (d - _dist[i - 1]) / seg : 0;
	return _flat[i - 1] + (_flat[i] - _flat[i - 1]) * t;
}

double
Path::distance_to (Pt p, Pt* nearest) const
{
	if (_flat.empty ()) {
		return HUGE_VAL;
	}

	double best = _flat.front ().distance (p);
	if (nearest) {
		*nearest = _flat.front ();
	}

	for (size_t i = 1; i < _flat.size (); ++i) {
		Pt q;
		double const d = segment_distance (_flat[i - 1], _flat[i], p, &q);
		if (d < best) {
			best = d;
			if (nearest) {
				*nearest = q;
			}
		}
	}
	return best;
}

Rect
Path::bounds () const
{
	if (_flat.empty ()) {
		return Rect ();
	}

	Rect r (_flat.front ().x, _flat.front ().y, _flat.front ().x, _flat.front ().y);
	for (auto const& p : _flat) {
		r.x0 = std::min (r.x0, p.x);
		r.y0 = std::min (r.y0, p.y);
		r.x1 = std::max (r.x1, p.x);
		r.y1 = std::max (r.y1, p.y);
	}
	return r;
}

bool
Path::intersects (Rect const& r) const
{
	if (_flat.size () == 1) {
		return r.contains (_flat.front ());
	}
	for (size_t i = 1; i < _flat.size (); ++i) {
		if (segment_meets_rect (_flat[i - 1], _flat[i], r)) {
			return true;
		}
	}
	return false;
}

/* ---- the cord ---- */

double
PdCord::width (Style style, Kind kind)
{
	switch (style) {
		case Vanilla:
			return kind == Signal ? 4.5 : 2.5;
		case Thin:
			return 3.0;
		default:
			return 4.5;
	}
}

double
PdCord::line_scale (double zoom)
{
	/* mix (1, 2, smoothstep (1, 0.5, zoom)) */
	double const t = std::min (1., std::max (0., (1. - zoom) / 0.5));
	return 1. + t * t * (3. - 2. * t);
}

Gtkmm2ext::Color
PdCord::colour (Kind kind, State const& st, PdTheme const& th)
{
	Gtkmm2ext::Color c = th.connection;
	if (st.selected || st.hovered) {
		c = kind == Signal ? th.signal : th.data;
	}
	return st.hovered ? brighter (c, 0.4f) : c;
}

int
PdCord::draw_rank (State const& st)
{
	return st.hovered ? 2 : st.selected ? 1 : 0;
}

namespace {

/* the shader's dashed (): in each period of rad stroke widths, a capsule with a half
 * width of thick widths centred on rad / 2 + shift; the stroke's own geometry cuts it to
 * the full width. Cairo draws it as a dash with round caps. */
void
capsule_dashes (cairo_t* cr, double w, double rad, double thick, double shift)
{
	double const length = fabs (rad / 2 - 2 * thick) + 2 * thick;
	double const width = std::min (2 * thick, 1.);
	double const on = std::max (0., length - width);
	double const period = rad * w;
	double const dashes[2] = { on * w, period - on * w };

	/* the pattern starts at the path's start; the dash begins half its length before the
	 * capsule's centre */
	double offset = fmod (period - (rad / 2 + shift - on / 2) * w, period);
	if (offset < 0) {
		offset += period;
	}

	cairo_set_dash (cr, dashes, 2, offset);
	cairo_set_line_width (cr, width * w);
}

/* nanovg's double stroke as plugdata's shader draws it (nanovg_gl.h:942-1002), in widths
 * of the stroke w: a band 0.98 wide, a core 0.44 wide (both times k when zoomed out, up to
 * the full width), the core's dashes for audio, and for data activity, capsules 0.8 wide
 * in the core's colour at 80 % */
void
double_stroke (cairo_t* cr, Path const& path, double w, double k, Gtkmm2ext::Color inner, Gtkmm2ext::Color outer,
               Gtkmm2ext::Color dash, double dash_size, double dash_shift, bool activity, double phase)
{
	double const core = 0.22 * k; /* the shader's innerSize and outerSize: half widths */
	double const band = 0.49 * k;

	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);
	cairo_new_path (cr);
	path.append_to (cr);

	cairo_set_dash (cr, 0, 0, 0);
	cairo_set_line_width (cr, std::min (2 * band, 1.) * w);
	set_source (cr, outer);
	cairo_stroke_preserve (cr);

	if (activity) {
		/* one period (3 widths) along for each message */
		capsule_dashes (cr, w, 3., 0.4 * k, 3. * phase);
		cairo_set_source_rgb (cr, red (inner) / 255. * 0.8, green (inner) / 255. * 0.8, blue (inner) / 255. * 0.8);
		cairo_stroke_preserve (cr);
		cairo_set_dash (cr, 0, 0, 0);
	}

	cairo_set_line_width (cr, std::min (2 * core, 1.) * w);
	set_source (cr, inner);
	cairo_stroke_preserve (cr);

	if (dash_size > 0) {
		capsule_dashes (cr, w, dash_size, core, dash_shift);
		set_source (cr, dash);
		cairo_stroke_preserve (cr);
		cairo_set_dash (cr, 0, 0, 0);
	}

	cairo_new_path (cr);
}

}

void
PdCord::render (cairo_t* cr, Path const& path, Kind kind, Style style, State const& st, PdTheme const& th)
{
	if (path.empty ()) {
		return;
	}

	double const w = width (style, kind);
	Gtkmm2ext::Color const c = colour (kind, st, th);

	cairo_save (cr);

	if (path.length () < 1) {
		/* a path this short does not draw: two dots instead (Connection.cpp:283-298) */
		cairo_new_path (cr);
		circle (cr, path.start (), w * 0.5);
		set_source (cr, th.band ());
		cairo_fill (cr);
		circle (cr, path.start (), w * 0.25);
		set_source (cr, c);
		cairo_fill (cr);
		cairo_restore (cr);
		return;
	}

	/* dashes on audio cords: 2.5 widths apart, 1.5 for several channels; none in the vanilla style */
	bool const dashed = kind == Signal && style != Vanilla;
	double const dash_size = dashed ? (st.nchans <= 1 ? 2.5 : 1.5) : 0;

	double_stroke (cr, path, w, line_scale (st.zoom), c, th.band (), th.audio_dash (), dash_size,
	               st.dash_phase * dash_size, st.activity && kind == Data, st.activity_phase);

	if (st.selected && st.hovered) {
		/* the handles have the other kind's colour, to stand out (Connection.cpp:234) */
		Handles const h = handles (path);
		Rect const s = st.handle == 1 ? h.start.expand (3) : h.start;
		Rect const e = st.handle == 2 ? h.end.expand (3) : h.end;
		set_source (cr, kind == Signal ? th.data : th.signal);
		circle (cr, s.centre (), s.width () / 2);
		cairo_fill (cr);
		circle (cr, e.centre (), e.width () / 2);
		cairo_fill (cr);
	}

	cairo_restore (cr);
}

void
PdCord::render_dragged (cairo_t* cr, Path const& path, Pt cursor, Kind kind, Style style, PdTheme const& th, double zoom)
{
	double const w = width (style, kind);

	cairo_save (cr);

	if (!path.empty ()) {
		if (kind == Signal && style != Vanilla) {
			double_stroke (cr, path, w, line_scale (zoom), brighter (th.signal, 0.6f), th.band (), th.audio_dash (), 2.5, 0, false, 0);
		} else {
			/* plugdata draws all other dragged cords, a vanilla audio one too, in the data colour */
			double_stroke (cr, path, w, line_scale (zoom), brighter (th.data, 0.6f), th.band (), 0, 0, 0, false, 0);
		}
	}

	circle (cr, cursor, 3.5);
	cairo_set_source_rgba (cr, 0.6, 0.6, 0.6, 0.7);
	cairo_fill (cr);

	cairo_restore (cr);
}

void
PdCord::render_order (cairo_t* cr, Path const& path, int number, Kind kind, State const& st, PdTheme const& th)
{
	if (kind != Data || path.empty ()) {
		return;
	}

	Pt const p = path.point_at (std::max (path.length () - 8.5 * 3, 9.5));
	double const radius = 7;

	cairo_save (cr);
	cairo_new_path (cr);
	circle (cr, p, radius);
	set_source (cr, colour (kind, st, th));
	cairo_fill_preserve (cr);
	cairo_set_line_width (cr, 1);
	set_source (cr, th.object_outline);
	cairo_stroke (cr);

	char text[16];
	snprintf (text, sizeof (text), "%d", number);
	PangoLayout* layout = text_layout (text, 9);
	PangoRectangle ink;
	PangoRectangle logical;
	pango_layout_get_extents (layout, &ink, &logical);
	cairo_move_to (cr, p.x - logical.width / (2. * PANGO_SCALE), p.y - logical.height / (2. * PANGO_SCALE));
	set_source (cr, th.badge_text ());
	pango_cairo_show_layout (cr, layout);
	g_object_unref (layout);

	/* cairo_restore () keeps the path: leave no current point behind */
	cairo_new_path (cr);
	cairo_restore (cr);
}

void
PdCord::render_direction (cairo_t* cr, Path const& path, std::vector<Pt> const& plan, Kind kind, State const& st, PdTheme const& th)
{
	double const arrow_width = 8;
	double const arrow_length = 12;
	Gtkmm2ext::Color const c = colour (kind, st, th);

	auto arrow = [&] (Path const& p) {
		double const centre = p.length () * 0.5;
		Pt const base = p.point_at (centre - arrow_length * 0.5);
		Pt const tip = p.point_at (centre + arrow_length * 0.5);
		double const len = base.distance (tip);
		if (len <= 0) {
			return;
		}
		/* the base's corners, half the arrow's width to each side */
		Pt const side (-(tip.y - base.y) / len * arrow_width * 0.5, (tip.x - base.x) / len * arrow_width * 0.5);

		cairo_new_path (cr);
		cairo_move_to (cr, tip.x, tip.y);
		cairo_line_to (cr, base.x - side.x, base.y - side.y);
		cairo_line_to (cr, base.x + side.x, base.y + side.y);
		cairo_close_path (cr);
		set_source (cr, c);
		cairo_fill_preserve (cr);
		cairo_set_line_width (cr, 1);
		set_source (cr, th.object_outline);
		cairo_stroke (cr);
	};

	cairo_save (cr);

	if (plan.size () > 1) {
		for (size_t i = 1; i < plan.size (); ++i) {
			double const length = plan[i - 1].distance (plan[i]);
			/* none on short end segments, to leave room for the reconnect handles */
			bool const end_segment = i == 1 || i == plan.size () - 1;
			if (length > arrow_length * (end_segment ? 3 : 2)) {
				arrow (Path::line (plan[i - 1], plan[i]));
			}
		}
	} else if (path.length () > arrow_length * 2) {
		arrow (path);
	}

	cairo_restore (cr);
}

double
PdCord::activity_phase (double ms)
{
	double const p = std::min (1., std::max (0., ms / 140.));

	/* the CSS ease-in-out curve, cubic-bezier (0.42, 0, 0.58, 1): find the curve's t
	 * for this x, then its y */
	double const x1 = 0.42;
	double const x2 = 0.58;
	auto bx = [=] (double t) { return 3 * (1 - t) * (1 - t) * t * x1 + 3 * (1 - t) * t * t * x2 + t * t * t; };
	auto by = [] (double t) { return 3 * (1 - t) * t * t + t * t * t; };

	double lo = 0;
	double hi = 1;
	for (int i = 0; i < 40; ++i) {
		double const mid = (lo + hi) / 2;
		if (bx (mid) < p) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	return by ((lo + hi) / 2);
}

/* ---- reconnect handles and hit testing ---- */

Handles
PdCord::handles (Path const& path)
{
	Handles h;
	h.start = Rect::around (path.point_at (8.5), 5, 5);
	h.end = Rect::around (path.point_at (std::max (path.length () - 8.5, 9.5)), 5, 5);
	return h;
}

int
PdCord::handle_at (Handles const& h, Pt p)
{
	if (h.start.contains (p)) {
		return 1;
	}
	if (h.end.contains (p)) {
		return 2;
	}
	return 0;
}

bool
PdCord::hit (Path const& path, Pt p, bool selected)
{
	if (path.empty ()) {
		return false;
	}
	if (selected && handle_at (handles (path), p)) {
		return true;
	}
	/* a click this near a jack is for the jack */
	if (path.start ().distance (p) < 8 || path.end ().distance (p) < 8) {
		return false;
	}
	return path.distance_to (p) < 3;
}

/* ---- the router ---- */

namespace {

/* the lattice search gives up after this many steps; plugdata has no limit, and with no way
 * through it can take very long */
size_t const route_budget = 200000;

struct Router
{
	Router (std::vector<Rect> const& o) : obstacles (o), budget (0) {}

	std::vector<Rect> const& obstacles;
	Pt incr;
	Plan best;
	Plan stack;
	size_t budget;

	bool crosses (Pt a, Pt b) const;
	int search (Pt cur, Pt target);
};

/* straightLineIntersectsObject (Connection.cpp:1349-1382): whether an axis-aligned step
 * crosses an edge of an obstacle grown by 1. Spite tests each step against every
 * obstacle, and both horizontal edges: plugdata only tests the faces that overlap the
 * rectangle from the step to the target, which is empty (so none) once the search is
 * level with it, and its bottom edge test never fires. */
bool
Router::crosses (Pt a, Pt b) const
{
	for (auto const& o : obstacles) {
		Rect const r = o.expand (1);
		if (a.x == b.x) {
			double const y0 = std::min (a.y, b.y);
			double const y1 = std::max (a.y, b.y);
			if (a.x > r.x0 && a.x < r.x1 && ((r.y0 > y0 && r.y0 < y1) || (r.y1 > y0 && r.y1 < y1))) {
				return true;
			}
		} else if (a.y == b.y) {
			double const x0 = std::min (a.x, b.x);
			double const x1 = std::max (a.x, b.x);
			if (a.y > r.y0 && a.y < r.y1 && ((r.x0 > x0 && r.x0 < x1) || (r.x1 > x0 && r.x1 < x1))) {
				return true;
			}
		}
	}
	return false;
}

/* findLatticePaths (Connection.cpp:1277-1347): a depth first search over the lattice, from
 * the target jack back to the source, one step at a time towards it */
int
Router::search (Pt cur, Pt target)
{
	if (!best.empty () || budget == 0) {
		return 0;
	}
	--budget;

	stack.push_back (cur);

	if (stack.size () > 1 && crosses (stack.back (), stack[stack.size () - 2])) {
		return 0;
	}

	bool const end_vertically = stack[0].y > target.y;

	if (fabs (cur.x - target.x) < incr.x * 0.5 && fabs (cur.y - target.y) < incr.y * 0.5) {
		best = stack;
		return 1;
	}

	int count = 0;

	/* plugdata copies the stack to go back to after each try; the tries only add to it, so
	 * cutting it back to this length is the same, without the copy */
	size_t const depth = stack.size ();

	auto follow = [&] (bool along_x) {
		Pt next = cur;
		double& c1 = along_x ? next.x : next.y;
		double const c2 = along_x ? target.x : target.y;
		double const inc = along_x ? incr.x : incr.y;

		if (fabs (c1 - c2) >= inc) {
			c1 = c1 > c2 ? c1 - inc : c1 + inc;
			count += search (next, target);
			stack.resize (depth);
		}
	};

	/* halfway there, prefer the other direction: a staircase */
	if (end_vertically) {
		if (fabs (cur.y - target.y) >= fabs (stack[0].y - target.y) * 0.5) {
			follow (false);
			follow (true);
		} else {
			follow (true);
			follow (false);
		}
	} else {
		if (fabs (cur.x - target.x) >= fabs (stack[0].x - target.x) * 0.5) {
			follow (true);
			follow (false);
		} else {
			follow (false);
			follow (true);
		}
	}

	return count;
}

bool
nearly_equal (double a, double b)
{
	return fabs (a - b) <= 1e-6 * std::max (1., std::max (fabs (a), fabs (b)));
}

}

Plan
PdCord::route (Pt from, Pt to, std::vector<Rect> const& obstacles)
{
	/* plugdata's names: pstart is the outlet (our source jack), pend the inlet */
	Pt const pstart = from;
	Pt const pend = to;

	/* every step of the search lies in the box between the two jacks, so only the faces that
	 * reach into it (grown by 1, as crosses () grows them) can be in the way */
	Rect const box = Rect::spanning (pstart, pend).expand (2);
	std::vector<Rect> near;
	for (auto const& o : obstacles) {
		if (o.x0 <= box.x1 && o.x1 >= box.x0 && o.y0 <= box.y1 && o.y1 >= box.y0) {
			near.push_back (o);
		}
	}

	Router router (near);

	double const distance = pstart.distance (pend);
	double const distance_x = fabs (pstart.x - pend.x);
	double const distance_y = fabs (pstart.y - pend.y);

	int const max_x = std::min (14, std::max (6, (int) (distance_x / 10)));
	int const max_y = std::min (14, std::max (6, (int) (distance_y / 10)));

	int res_x = 6;
	int res_y = 6;
	int found = 0;

	/* look for a way at ever finer lattices */
	while (!found && res_x < max_x && distance > 40) {
		router.incr = Pt (std::max (1., distance_x / res_x), std::max (1., distance_y / res_y));
		router.budget = route_budget;
		router.stack.clear ();

		found = router.search (pend, pstart);

		if (res_x < max_x) {
			++res_x;
		}
		if (res_y < max_x) { /* sic: plugdata limits Y by the X resolution here */
			++res_y;
		}
		if (res_x > max_x || res_y > max_y) {
			break;
		}
	}

	Plan plan;
	Plan const& best = router.best;

	if (best.size () > 1) {
		/* keep only the corners; double an end whose segment is horizontal */
		plan.push_back (best.front ());

		bool vertical = nearly_equal (best[0].x, best[1].x);
		if (!vertical) {
			plan.push_back (best.front ());
		}

		for (size_t n = 1; n < best.size (); ++n) {
			if ((best[n].x != best[n - 1].x && vertical) || (best[n].y != best[n - 1].y && !vertical)) {
				plan.push_back (best[n - 1]);
				vertical = !vertical;
			}
		}

		plan.push_back (best.back ());
		if (!vertical) {
			plan.push_back (best.back ());
		}
	} else if (pend.y < pstart.y) {
		/* no way round: across, up through the middle, across (whole units, as plugdata) */
		double const half_x = (int) ((pstart.x - pend.x) / 2);
		plan.push_back (pend);
		plan.push_back (pend);
		plan.push_back (Pt (pend.x + half_x, pend.y));
		plan.push_back (Pt (pend.x + half_x, pstart.y));
		plan.push_back (pstart);
		plan.push_back (pstart);
	} else {
		/* down, across in the middle, down */
		double const half_y = (int) ((pstart.y - pend.y) / 2);
		plan.push_back (pend);
		plan.push_back (Pt (pend.x, pend.y + half_y));
		plan.push_back (Pt (pstart.x, pend.y + half_y));
		plan.push_back (pstart);
	}

	std::reverse (plan.begin (), plan.end ());
	snap (plan, from, to);
	return plan;
}

void
PdCord::snap (Plan& plan, Pt from, Pt to)
{
	if (plan.size () < 2) {
		return;
	}

	if (plan.size () == 2) {
		/* no end segments to keep straight: plugdata draws such a plan from jack to jack
		 * (Connection.cpp:1126-1131), and snapping one end must not move the other */
		plan[0] = from;
		plan[1] = to;
		return;
	}

	auto snap_end = [&plan] (Pt p, size_t end, size_t next) {
		if (plan[end].x == plan[next].x) {
			plan[next].x = p.x;
		} else {
			plan[next].y = p.y;
		}
		plan[end] = p;
	};

	snap_end (from, 0, 1);
	snap_end (to, plan.size () - 1, plan.size () - 2);
}

int
PdCord::segment_at (Plan const& plan, Pt p)
{
	for (size_t n = 2; n + 1 < plan.size (); ++n) {
		if (segment_distance (plan[n - 1], plan[n], p) < 3) {
			return (int) n;
		}
	}
	return -1;
}

bool
PdCord::segment_vertical (Plan const& plan, int n)
{
	return n > 0 && (size_t) n < plan.size () && plan[n - 1].x == plan[n].x;
}

double
PdCord::segment_origin (Plan const& plan, int n)
{
	if (n <= 0 || (size_t) n >= plan.size ()) {
		return 0;
	}
	return segment_vertical (plan, n) ? plan[n].x : plan[n].y;
}

void
PdCord::drag_segment (Plan& plan, int n, double origin, Pt delta)
{
	if (n <= 0 || (size_t) n >= plan.size ()) {
		return;
	}
	if (segment_vertical (plan, n)) {
		plan[n - 1].x = origin + delta.x;
		plan[n].x = origin + delta.x;
	} else {
		plan[n - 1].y = origin + delta.y;
		plan[n].y = origin + delta.y;
	}
}

void
PdCord::move_end (Plan& plan, bool target_end, Pt jack)
{
	if (plan.size () < 2) {
		return;
	}

	size_t const end = target_end ? plan.size () - 1 : 0;
	size_t const next = target_end ? plan.size () - 2 : 1;

	if (plan.size () == 2) {
		/* a line from jack to jack (snap ()): only the moved end goes (plugdata snaps such
		 * a plan again, Connection.cpp:944-948) */
		plan[end] = jack;
		return;
	}

	/* twice, as plugdata does: an end segment of no length is vertical and horizontal */
	for (int pass = 0; pass < 2; ++pass) {
		if (plan[end].x == plan[next].x) {
			plan[next].x = jack.x;
		} else {
			plan[next].y = jack.y;
		}
		plan[end] = jack;
	}
}

void
PdCord::translate (Plan& plan, Pt offset)
{
	for (auto& p : plan) {
		p = p + offset;
	}
}

std::string
PdCord::plan_state (Plan const& plan, Pt from)
{
	if (plan.empty ()) {
		return "empty";
	}

	/* plain digits, whatever C++ locale a plugin may have set: "1500", never "1.500" */
	std::ostringstream s;
	s.imbue (std::locale::classic ());
	for (size_t i = 0; i < plan.size (); ++i) {
		s << (i ? " " : "") << lround (plan[i].x - from.x) << ',' << lround (plan[i].y - from.y);
	}
	return s.str ();
}

Plan
PdCord::plan_from_state (std::string const& state, Pt from)
{
	Plan plan;
	if (state.empty () || state == "empty") {
		return plan;
	}

	char const* s = state.c_str ();
	while (*s) {
		char* end;
		long const x = strtol (s, &end, 10);
		if (end == s || *end != ',') {
			return Plan ();
		}
		s = end + 1;
		long const y = strtol (s, &end, 10);
		if (end == s) {
			return Plan ();
		}
		plan.push_back (Pt (from.x + x, from.y + y));
		s = end;
		while (*s == ' ') {
			++s;
		}
	}

	/* one point makes no line (plugdata would read past its end, Connection.cpp:1122):
	 * the cord is a curve again */
	if (plan.size () < 2) {
		return Plan ();
	}
	return plan;
}

/* ---- jacks ---- */

void
PdCord::render_jack (cairo_t* cr, Rect const& box, Rect const& face, Kind kind, JackState const& st, PdTheme const& th)
{
	bool const hovering = st.targeted && !st.locked;
	Gtkmm2ext::Color const inner = st.locked ? th.locked_jack () : kind == Signal ? th.signal : th.data;

	Rect r = th.square_iolets ? box.reduce (2, 3.33) : box.reduce (2);
	if (hovering) {
		r = r.expand (1);
	}

	cairo_save (cr);
	cairo_new_path (cr);

	if ((st.locked || !st.expanded) && !st.targeted) {
		/* the half-moon: only the part on the face shows */
		cairo_rectangle (cr, face.x0, face.y0, face.width (), face.height ());
		cairo_clip (cr);
	}

	/* nvgDrawRoundedRect (nanovg.cpp:2940-2967): the shape filled, with a 1 px ring inside its edge */
	double const radius = th.square_iolets ? 0 : r.width () * 0.5;
	rounded_rect (cr, r, radius);
	set_source (cr, th.object_outline);
	cairo_fill (cr);
	rounded_rect (cr, r.reduce (1), std::max (0., std::min (radius, std::min (r.width (), r.height ()) / 2) - 1));
	set_source (cr, inner);
	cairo_fill (cr);

	cairo_restore (cr);
}

std::vector<Rect>
PdCord::jack_boxes (Rect const& face, int n, bool top, int other)
{
	std::vector<Rect> boxes;
	if (n <= 0) {
		return boxes;
	}

	/* plugdata's object is the face with a 6 px margin round it; it lays its iolets out in
	 * whole pixels from the object's corner */
	int const margin = 6;
	int const w = (int) lround (face.width ()) + 2 * margin;
	int const h = (int) lround (face.height ()) + 2 * margin;

	int const max_w = (w - 2 * margin) / std::max (n, other) - 4;
	int const max_h = (int) (h / 2.f - 2);
	int const size = std::max (std::min ({ (int) jack_size, max_w, max_h }), 10);
	int const border = (int) (7.f + (size - 10) * (12.f - 7.f) / (13.f - 10.f));

	int const hit_box = 6;
	int x = 0;
	int span = w;
	int const remove = std::min (border, std::max (0, w - hit_box * n - border));
	if (remove) {
		x += remove;
		span -= 2 * remove;
	}

	float const y = (top ? margin + 1 : h - margin) - size / 2.f;

	for (int i = 0; i < n; ++i) {
		float bx;
		if (n == 1) {
			bx = w < 25 + size ? w / 2 - (size - 1) / 2.f : x;
		} else {
			bx = x + (span - size) / (float) (n - 1) * i;
		}
		boxes.push_back (Rect::sized (face.x0 - margin + (int) bx, face.y0 - margin + (int) y, size, size));
	}
	return boxes;
}

bool
PdCord::jack_on_top (bool input, Rect const& face, Pt other_end)
{
	return input ? !(other_end.y > face.y1) : other_end.y < face.y0;
}

bool
PdCord::jack_hit (Rect const& box, Pt p, bool locked)
{
	return !locked && box.contains (p);
}

int
PdCord::nearest_jack (std::vector<Rect> const& boxes, Pt p, double reach)
{
	int best = -1;
	double best_distance = 0;

	for (size_t i = 0; i < boxes.size (); ++i) {
		if (!boxes[i].expand (reach).contains (p)) {
			continue;
		}
		double const d = boxes[i].centre ().distance (p);
		if (best < 0 || d < best_distance) {
			best = (int) i;
			best_distance = d;
		}
	}
	return best;
}

/* ---- the dot grid ---- */

Grid::Grid ()
	: _tile (0)
	, _tile_units (0)
	, _zoom (0)
	, _scale (0)
	, _spacing (0)
	, _bg (0)
	, _dots (0)
{
}

Grid::~Grid ()
{
	if (_tile) {
		cairo_surface_destroy (_tile);
	}
}

double
Grid::dot_radius (double zoom)
{
	/* from 3 units at zoom 0.25 down to 1 at zoom 1 */
	return zoom < 1 ? 3. + (zoom - 0.25) * (1. - 3.) / (1. - 0.25) : 1.;
}

int
Grid::decimation (int spacing, double zoom)
{
	switch (spacing) {
		case 5:
		case 10:
			return zoom < 0.5 ? 6 : zoom < 1 ? 4 : 0;
		case 15:
			return zoom < 0.5 ? 8 : zoom < 1 ? 4 : 0;
		case 20:
		case 25:
			return zoom < 0.5 ? 6 : zoom < 1 ? 3 : 0;
		case 30:
			return zoom < 1 ? 12 : 0;
		default:
			return 0;
	}
}

void
Grid::render (cairo_t* cr, Rect const& area, double zoom, PdTheme const& th, Pt origin, int spacing)
{
	spacing = std::max (1, spacing);

	cairo_save (cr);
	cairo_new_path (cr);
	cairo_rectangle (cr, area.x0, area.y0, area.width (), area.height ());
	set_source (cr, th.canvas_background);
	cairo_fill_preserve (cr);

	/* the tile is drawn at the target's resolution */
	double ux = 1;
	double uy = 0;
	cairo_user_to_device_distance (cr, &ux, &uy);
	double const scale = hypot (ux, uy);

	/* 300 units holds whole periods of every grid size plugdata has */
	int const units = 300 % spacing == 0 ? 300 : spacing * 12;

	if (!_tile || zoom != _zoom || scale != _scale || spacing != _spacing || th.grid_dots () != _dots || th.canvas_background != _bg) {
		if (_tile) {
			cairo_surface_destroy (_tile);
		}

		int const px = std::max (1, (int) ceil (units * scale));
		_tile = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, px, px);
		_tile_units = units;
		_zoom = zoom;
		_scale = scale;
		_spacing = spacing;
		_dots = th.grid_dots ();
		_bg = th.canvas_background;

		cairo_t* t = cairo_create (_tile);
		cairo_scale (t, px / (double) units, px / (double) units);

		double const radius = dot_radius (zoom);
		int const decim = decimation (spacing, zoom);
		Gtkmm2ext::Color const marking = _dots;
		Gtkmm2ext::Color const major = with_alpha (marking, std::min (zoom * 0.8, 1.));

		/* a dot every spacing units; zoomed out only every decim'th row and column, darker
		 * where they cross. 2.5 in, so no dot sits on the tile's edge. */
		for (int x = 0; x <= units; x += spacing) {
			for (int y = 0; y <= units; y += spacing) {
				Gtkmm2ext::Color c = major;
				if (decim) {
					if (x % decim && y % decim) {
						continue;
					}
					if (x % decim == 0 && y % decim == 0) {
						c = marking;
					}
				}
				circle (t, Pt (x + 2.5, y + 2.5), radius);
				set_source (t, c);
				cairo_fill (t);
			}
		}

		cairo_destroy (t);
	}

	cairo_pattern_t* pattern = cairo_pattern_create_for_surface (_tile);
	cairo_pattern_set_extend (pattern, CAIRO_EXTEND_REPEAT);

	double const px = cairo_image_surface_get_width (_tile);
	cairo_matrix_t m;
	cairo_matrix_init_scale (&m, px / _tile_units, px / _tile_units);
	cairo_matrix_translate (&m, -(origin.x - 2.5), -(origin.y - 2.5));
	cairo_pattern_set_matrix (pattern, &m);

	cairo_set_source (cr, pattern);
	cairo_fill (cr);
	cairo_pattern_destroy (pattern);

	cairo_restore (cr);
}

/* ---- text ---- */

namespace {

std::string&
family ()
{
	static std::string f ("Inter, Sans");
	return f;
}

/* the cords' own font map, so that a font loaded for them is theirs alone */
PangoFontMap*
font_map ()
{
	static PangoFontMap* m = 0;
	if (!m) {
		m = pango_cairo_font_map_new ();
	}
	return m;
}

/* counts the fonts loaded, so that font () measures them again */
int&
fonts_loaded ()
{
	static int n = 0;
	return n;
}

PangoContext*
context ()
{
	static PangoContext* c = 0;
	if (!c) {
		c = pango_font_map_create_context (font_map ());

		/* unhinted metrics: lines exactly as tall as the font, as JUCE lays them out */
		cairo_font_options_t* fo = cairo_font_options_create ();
		cairo_font_options_set_hint_metrics (fo, CAIRO_HINT_METRICS_OFF);
		pango_cairo_context_set_font_options (c, fo);
		cairo_font_options_destroy (fo);
	}
	return c;
}

/* a font whose ascent plus descent is height: JUCE's and nanovg's font size */
PangoFontDescription const*
font (double height)
{
	static std::map<double, PangoFontDescription*> fonts;
	static std::string fonts_family;
	static int fonts_measured = 0;

	if (fonts_family != family () || fonts_measured != fonts_loaded ()) {
		for (auto& f : fonts) {
			pango_font_description_free (f.second);
		}
		fonts.clear ();
		fonts_family = family ();
		fonts_measured = fonts_loaded ();
	}

	auto i = fonts.find (height);
	if (i != fonts.end ()) {
		return i->second;
	}

	PangoFontDescription* d = pango_font_description_from_string (family ().c_str ());
	pango_font_description_set_absolute_size (d, height * PANGO_SCALE);

	PangoFontMetrics* m = pango_context_get_metrics (context (), d, 0);
	double const full = (pango_font_metrics_get_ascent (m) + pango_font_metrics_get_descent (m)) / (double) PANGO_SCALE;
	pango_font_metrics_unref (m);

	if (full > 0) {
		pango_font_description_set_absolute_size (d, height * height / full * PANGO_SCALE);
	}

	fonts[height] = d;
	return d;
}

}

void
PdCord::set_font_family (std::string const& f)
{
	family () = f;
}

bool
PdCord::load_font_file (std::string const& path)
{
#ifdef __APPLE__
	(void) path;
	return false;
#else
	if (!PANGO_IS_FC_FONT_MAP (font_map ())) {
		return false;
	}

	PangoFcFontMap* const fm = PANGO_FC_FONT_MAP (font_map ());
	FcChar8 const* const file = reinterpret_cast<FcChar8 const*> (path.c_str ());
	FcConfig* config = pango_fc_font_map_get_config (fm);

	if (config) {
		/* the cords' own set of fonts, made by an earlier call */
		if (!FcConfigAppFontAddFile (config, file)) {
			return false;
		}
		pango_fc_font_map_config_changed (fm);
	} else {
		/* the system's fonts and this one, for the cords' font map only */
		config = FcInitLoadConfigAndFonts ();
		if (!config) {
			return false;
		}
		if (!FcConfigAppFontAddFile (config, file)) {
			FcConfigDestroy (config);
			return false;
		}
		pango_fc_font_map_set_config (fm, config); /* takes a reference */
		FcConfigDestroy (config);
	}

	++fonts_loaded ();
	return true;
#endif
}

PangoLayout*
PdCord::text_layout (std::string const& text, double height)
{
	PangoLayout* layout = pango_layout_new (context ());
	pango_layout_set_font_description (layout, font (height));
	pango_layout_set_text (layout, text.c_str (), -1);

#if PANGO_VERSION_CHECK(1, 44, 0)
	/* a word broken across lines gets no hyphen, as in plugdata */
	PangoAttrList* attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs, pango_attr_insert_hyphens_new (FALSE));
	pango_layout_set_attributes (layout, attrs);
	pango_attr_list_unref (attrs);
#endif

	return layout;
}
