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

/* plugdata's cords, jacks and dot grid, drawn with Cairo: the MCR's look.
 *
 * Ported from plugdata (Connection.cpp, Iolet.cpp, Object.cpp, Canvas.cpp) and
 * from what its nanovg cable shader draws (nanovg_gl.h, the double stroke). Line
 * numbers refer to the plugdata fork in plugdata/Source.
 *
 * Everything is in canvas units, with cr already scaled by the canvas zoom, as
 * plugdata draws. GUI thread only.
 *
 * plugdata's draw order (Canvas.cpp:695-717, 774-816): the cords at rest, then the
 * selected ones, then the hovered one (draw_rank ()), then order badges, then the
 * Detective bubbles (PdCord::CordDetective), then the cord being dragged. plugdata
 * draws its cords behind the objects only when the patch is locked; the MCR draws
 * them over the faces.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <cairo.h>

#include "pd_theme.h"

typedef struct _PangoLayout PangoLayout;

namespace PdCord {

enum Kind {
	Data,   /* MIDI and parameter cords: plugdata's message cords */
	Signal  /* audio cords */
};

/* plugdata's connection styles, numbered as its themes store them (LookAndFeel.h:216-221) */
enum Style {
	Default = 1,
	Vanilla = 2,
	Thin = 3
};

struct Pt
{
	double x;
	double y;

	Pt () : x (0), y (0) {}
	Pt (double xx, double yy) : x (xx), y (yy) {}

	Pt operator+ (Pt const& o) const { return Pt (x + o.x, y + o.y); }
	Pt operator- (Pt const& o) const { return Pt (x - o.x, y - o.y); }
	Pt operator* (double k) const { return Pt (x * k, y * k); }
	bool operator== (Pt const& o) const { return x == o.x && y == o.y; }
	bool operator!= (Pt const& o) const { return !(*this == o); }

	double distance (Pt const& o) const;
};

/* the same fields as ArdourCanvas::Rect; x1 and y1 are just outside */
struct Rect
{
	double x0;
	double y0;
	double x1;
	double y1;

	Rect () : x0 (0), y0 (0), x1 (0), y1 (0) {}
	Rect (double a, double b, double c, double d) : x0 (a), y0 (b), x1 (c), y1 (d) {}

	static Rect sized (double x, double y, double w, double h) { return Rect (x, y, x + w, y + h); }
	static Rect around (Pt c, double w, double h) { return Rect (c.x - w / 2, c.y - h / 2, c.x + w / 2, c.y + h / 2); }
	static Rect spanning (Pt a, Pt b);

	double width () const { return x1 - x0; }
	double height () const { return y1 - y0; }
	Pt centre () const { return Pt ((x0 + x1) / 2, (y0 + y1) / 2); }
	bool empty () const { return x1 <= x0 || y1 <= y0; }
	bool contains (Pt p) const { return p.x >= x0 && p.y >= y0 && p.x < x1 && p.y < y1; }

	Rect expand (double d) const { return Rect (x0 - d, y0 - d, x1 + d, y1 + d); }
	Rect expand (double dx, double dy) const { return Rect (x0 - dx, y0 - dy, x1 + dx, y1 + dy); }
	Rect reduce (double d) const { return expand (-d); }
	Rect reduce (double dx, double dy) const { return expand (-dx, -dy); }
	Rect translate (Pt d) const { return Rect (x0 + d.x, y0 + d.y, x1 + d.x, y1 + d.y); }
	Rect unite (Rect const&) const;       /* an empty rect adds nothing */
	bool intersects (Rect const&) const;  /* the areas overlap; never with an empty rect */
};

/* ---- the path ---- */

class Path
{
public:
	Path () {}

	/* plugdata's curved cord between two jack centres (Connection::getNonSegmentedPath,
	 * Connection.cpp:994-1048). Flipped: the cord leaves the top of its source and enters
	 * the bottom of its target, so the curve is worked out upside down. */
	static Path curve (Pt from, Pt to, bool straight = false, bool flipped = false);

	/* Spite's jacks flip to the edge of their face that the cord faces (jack_on_top ()), so
	 * a cord can also run from a top edge to a top edge, or bottom to bottom: next to a
	 * taller face in a row, say. from_top: the source jack is on its face's top edge (an
	 * output is at the bottom unless flipped); to_top: the target jack is on its face's top
	 * edge (an input is, unless flipped). Bottom to top and top to bottom are plugdata's two
	 * curves above; for the other two the cord leaves and enters the same way, turning by
	 * plugdata's (at most) 20 units at each end. */
	static Path between (Pt from, bool from_top, Pt to, bool to_top, bool straight = false);

	/* through a segmented cord's plan, its corners rounded to radius: 8, or the cord's
	 * width when the theme's cords are straight (Connection.cpp:1125-1134) */
	static Path segmented (std::vector<Pt> const& plan, double radius = 8);

	static Path line (Pt from, Pt to);

	void append_to (cairo_t*) const; /* adds it to cr's current path */

	bool empty () const { return _flat.empty (); }
	double length () const { return _dist.empty () ? 0 : _dist.back (); }
	Pt start () const;
	Pt end () const;
	Pt point_at (double distance) const;     /* along the path from its start, kept on it */
	double distance_to (Pt, Pt* nearest = 0) const;
	Rect bounds () const;                    /* of the line, not of the stroke */
	bool intersects (Rect const&) const;     /* a lasso (Connection::intersects, Connection.cpp:547-574) */

	std::vector<Pt> const& polyline () const { return _flat; }

private:
	enum Op {
		MoveTo,
		LineTo,
		CurveTo
	};

	struct Step {
		Op op;
		Pt p[3];
	};

	std::vector<Step> _steps;
	std::vector<Pt> _flat;     /* the path cut into lines */
	std::vector<double> _dist; /* the length up to each point of _flat */

	void move_to (Pt);
	void line_to (Pt);
	void curve_to (Pt, Pt, Pt);
	void flatten ();
};

/* ---- the cord ---- */

/* the cord's width, its band's (Connection::getPathWidth, Connection.cpp:851-861) */
double width (Style, Kind);

/* how much wider plugdata's shader draws the cord's core when zoomed out: 1 at zoom 1 and
 * above, 2 at 0.5 and below (nanovg_gl.h:952-953) */
double line_scale (double zoom);

struct State
{
	bool selected;
	bool hovered;
	int handle;            /* the mouse is on a reconnect handle: 0 none, 1 start, 2 end */
	int nchans;            /* audio channels: more than one gives multichannel dashes */
	double zoom;           /* the canvas zoom, which cr is scaled by */
	bool activity;         /* plugdata's "connection activity" overlay, on data cords */
	double activity_phase; /* 0..1, once for each message: activity_phase () */
	double dash_phase;     /* Spite: moves an audio cord's dashes along, in dash periods (plugdata: 0) */

	State ()
		: selected (false)
		, hovered (false)
		, handle (0)
		, nchans (1)
		, zoom (1)
		, activity (false)
		, activity_phase (0)
		, dash_phase (0)
	{}
};

/* at rest every cord has the theme's connection colour; a selected one its kind's
 * colour, a hovered one that colour brightened (Connection::getConnectionColour,
 * Connection.cpp:249-262) */
Gtkmm2ext::Color colour (Kind, State const&, PdTheme const&);

/* the draw order of cords: 0 at rest, 1 selected, 2 hovered (draw the higher ones later) */
int draw_rank (State const&);

/* a cord (Connection::render, Connection.cpp:264-337): an outer band, the core, the
 * audio dashes; its reconnect handles when it is selected and hovered */
void render (cairo_t*, Path const&, Kind, Style, State const&, PdTheme const&);

/* the cord being dragged out of a jack (ConnectionBeingCreated::render, Connection.h:338-381),
 * its path a Path::curve from the source jack (or the cursor) to the cursor (or the target
 * jack), and a dot on the cursor */
void render_dragged (cairo_t*, Path const&, Pt cursor, Kind, Style, PdTheme const&, double zoom = 1);

/* a data cord's number among the cords leaving its jack, when there are several
 * (Connection::renderConnectionOrder, Connection.cpp:402-425) */
void render_order (cairo_t*, Path const&, int number, Kind, State const&, PdTheme const&);

/* the arrows of plugdata's "direction" overlay (Connection.cpp:349-398); give the plan for
 * a segmented cord, an empty one for a curve */
void render_direction (cairo_t*, Path const&, std::vector<Pt> const& plan, Kind, State const&, PdTheme const&);

/* the activity overlay's phase ms after a message: 0 to 1 in 140 ms, eased in and out
 * (Connection.h:240-247) */
double activity_phase (double ms);

/* ---- reconnect handles and hit testing ---- */

struct Handles
{
	Rect start;
	Rect end;
};

/* 5 px squares 8.5 px from each end (Connection::updateReconnectHandle, Connection.cpp:1089-1093) */
Handles handles (Path const&);

/* 1 when p is in the start handle, 2 in the end one, else 0 */
int handle_at (Handles const&, Pt p);

/* whether a click at p is on the cord: within 3 px of it but not within 8 px of an end,
 * or on a handle of a selected cord (Connection::hitTest, Connection.cpp:513-545) */
bool hit (Path const&, Pt p, bool selected = false);

/* ---- segmented cords ---- */

typedef std::vector<Pt> Plan;

/* plugdata's router (Connection::findPath, findLatticePaths, straightLineIntersectsObject,
 * Connection.cpp:1177-1382): an orthogonal plan from the source jack to the target jack
 * around the obstacles, the bounds of the faces other than the cord's own two. Where an
 * end segment is horizontal its end point is doubled, so that segment can be dragged
 * too. The plan comes snapped to the two jacks (snap ()). */
Plan route (Pt from, Pt to, std::vector<Rect> const& obstacles);

/* puts the plan's ends on the jacks, keeping its end segments straight
 * (Connection::updatePath, Connection.cpp:1106-1124). route () gives 4 points or more; a plan
 * of 2 (from a damaged session) becomes the straight line from jack to jack that plugdata
 * draws for one, and move_end () keeps it so. */
void snap (Plan&, Pt from, Pt to);

/* the segment of the plan under p, n for plan[n - 1] -> plan[n]; -1 for none.
 * Only the inner segments can be dragged (Connection::getClosestLineIdx, Connection.cpp:826-841) */
int segment_at (Plan const&, Pt p);
bool segment_vertical (Plan const&, int n);

/* a segment drag: where it starts from (Connection::mouseDown, Connection.cpp:742-750), and
 * moving segment n sideways by the mouse's offset from the press (mouseDrag, 780-795) */
double segment_origin (Plan const&, int n);
void drag_segment (Plan&, int n, double origin, Pt delta);

/* one of the cord's faces moved (Connection::componentMovedOrResized, Connection.cpp:903-973):
 * the jack's end of the plan follows it; both faces moved together: translate () */
void move_end (Plan&, bool target_end, Pt jack);
void translate (Plan&, Pt offset);

/* a plan as text for the session file, in whole units from the source jack ("empty" for a
 * cord that is not segmented), the same in every locale; plugdata keeps the same numbers in
 * Pd's patch file (Connection::pushPathState, popPathState, Connection.cpp:427-486).
 * plan_from_state () gives an empty plan (a curve) for "empty", for text it can't read and
 * for a single point; snap () the plan it gives onto the jacks. */
std::string plan_state (Plan const&, Pt from);
Plan plan_from_state (std::string const&, Pt from);

/* ---- jacks ---- */

/* plugdata's iolet size (LookAndFeel.cpp:1462-1465) */
static double const jack_size = 13;

struct JackState
{
	bool targeted; /* the mouse is on it, or the dragged cord would land on it */
	bool locked;   /* Play mode: grey, and no use */
	bool expanded; /* the mouse is on its face: whole circles instead of half-moons */

	JackState () : targeted (false), locked (false), expanded (false) {}
};

/* a jack (Iolet::render, Iolet.cpp:73-89) in its 13 px box, cut to its face (the half-moon)
 * unless the face is hovered or the jack targeted (Object::renderIolets, Object.cpp:1363-1392) */
void render_jack (cairo_t*, Rect const& box, Rect const& face, Kind, JackState const&, PdTheme const&);

/* the boxes plugdata gives n jacks on one edge of a face (Object::updateIoletGeometry,
 * Object.cpp:614-689, default style); their centres sit half a pixel inside the edge.
 * other: the number of jacks on the face's other edge, since plugdata sizes all of an
 * object's jacks to fit its busier edge. Lay out together all the jacks that end up on
 * one edge: a flipped jack (jack_on_top ()) joins the jacks of the edge it flips to. */
std::vector<Rect> jack_boxes (Rect const& face, int n, bool top, int other = 0);

/* whether a jack sits on its face's top edge: inputs on top, outputs at the bottom, each
 * flipping to the other edge when its cord's other end lies beyond it. Give other_end
 * where the other jack is before flipping (on its usual edge), then draw the cord with
 * Path::between (). In rows of faces as tall as each other, a cord to a row above flips
 * both ends (Path::curve's flipped pair) and every other cord keeps them. */
bool jack_on_top (bool input, Rect const& face, Pt other_end);

/* Iolet::hitTest (Iolet.cpp:91-116) */
bool jack_hit (Rect const& box, Pt p, bool locked = false);

/* the jack a dragged cord snaps to: of the boxes p is within reach (20 px) of, the one with
 * the nearest centre (Iolet::findNearestIolet, Iolet.cpp:330-356); -1 for none */
int nearest_jack (std::vector<Rect> const& boxes, Pt p, double reach = 20);

/* ---- the dot grid ---- */

/* plugdata's canvas grid (Canvas.cpp:468-533, 557-571), drawn from a cached 300 unit tile */
class Grid
{
public:
	Grid ();
	~Grid ();

	/* fills area (canvas units) with the canvas background and the dots, which sit at
	 * origin + n * spacing */
	void render (cairo_t*, Rect const& area, double zoom, PdTheme const&, Pt origin = Pt (), int spacing = 25);

	static double dot_radius (double zoom);
	static int decimation (int spacing, double zoom); /* 0: every dot */

private:
	cairo_surface_t* _tile;
	int _tile_units;
	double _zoom;
	double _scale;
	int _spacing;
	Gtkmm2ext::Color _bg;
	Gtkmm2ext::Color _dots;

	Grid (Grid const&);
	Grid& operator= (Grid const&);
};

/* ---- text ---- */

/* plugdata's font is Inter (InterRegular.ttf, plugdata/Resources/Fonts), and the family
 * is "Inter, Sans" so that it falls back where Spite's system has no Inter. load_font_file ()
 * adds a font file for the cords' text alone (their own fontconfig set; the rest of Spite's
 * fonts stay as they are), false where it can't (macOS) or the file isn't a font. */
bool load_font_file (std::string const& path);
void set_font_family (std::string const&);

/* a layout of text in plugdata's font at a height in canvas units: JUCE and nanovg size
 * fonts by ascent plus descent, Pango by the em. Unref it with g_object_unref. */
PangoLayout* text_layout (std::string const& text, double height);

}
