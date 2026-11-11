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

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <cairo.h>

#include <ydkmm/pixbuf.h>
#include <ytkmm/layout.h>

#include "pbd/id.h"
#include "pbd/signals.h"

#include "ardour/data_type.h"
#include "ardour/session_handle.h"

#include "canvas/types.h"

#include "default_face.h"

namespace ARDOUR {
	class PlugInsertBase;
	class PluginInsert;
	class Route;
}

/* Spite: the Modular Channels Rack's layout, in canvas pixels (the canvas's bin
 * window's), kept in plain structs apart from the drawing, so later phases (cables,
 * slivers, Detective Mode) can hit-test and route cables over it */

/* one of a device's pins: an input on its top edge, an output on its bottom edge */
struct McrJack {
	McrJack (ARDOUR::DataType t, bool in, uint32_t p, std::string const& n, bool w)
		: type (t), input (in), pin (p), name (n), wired (w), top (in), shown (false), x (0), y (0) {}

	ARDOUR::DataType type;
	bool             input;
	uint32_t         pin;   /* of its type, over all the plugin's instances */
	std::string      name;
	bool             wired; /* the track feeds it (an input) or takes from it (an output) */
	bool             top;   /* on the top edge, else the bottom one (it will flip to face its cable) */
	bool             shown; /* placed on its edge: a device with more pins than fit shows its wired ones and a free one of each type */
	double           x;     /* its centre */
	double           y;
};

struct McrDevice {
	McrDevice () : custom_open (false), more_inputs (0), more_outputs (0) {}

	std::weak_ptr<ARDOUR::PluginInsert> insert;
	PBD::ID                             id;      /* the processor's */
	DefaultFace                         card;    /* what it shows (card.live: it has a face of its own; card.in_window: any window of its own is open) */
	bool                                custom_open; /* its own GUI is in its window (PgD4S: the patch editor), not just its generic controls */
	Glib::RefPtr<Gdk::Pixbuf>           picture; /* a live face's last picture (the Device view's), at the face's height */
	Glib::RefPtr<Gdk::Pixbuf>           source;  /* the Device view's picture it was scaled from */
	ArdourCanvas::Rect                  rect;    /* its title strip and face */
	std::vector<McrJack>                jacks; /* inputs, then outputs */
	uint32_t                            more_inputs;  /* not shown */
	uint32_t                            more_outputs;
};

struct McrRow {
	McrRow () : colour (0), active (true), sliver_open (false) {}

	std::weak_ptr<ARDOUR::Route> route;
	PBD::ID                      id;     /* the track's */
	std::string                  name;
	uint32_t                     colour; /* RGBA */
	bool                         active;
	ArdourCanvas::Rect           rect;    /* the whole row, across the canvas */
	ArdourCanvas::Rect           header;  /* the track's name and colour */
	std::vector<McrDevice>       devices; /* in processor order */
	ArdourCanvas::Rect           sliver;  /* between this row and the next (empty for the last) */
	bool                         sliver_open;
};

/* The MCR's canvas: one row per track, each a header and its devices, with slivers
 * between the rows. It paints everything itself, on its bin window, with no child
 * widget per device: cables can then be drawn over everything, and live PgD4S faces
 * can later be put () in it, as composited child windows of that same bin window. */
class McrCanvas : public Gtk::Layout, public ARDOUR::SessionHandlePtr
{
public:
	McrCanvas ();
	~McrCanvas ();

	void set_session (ARDOUR::Session*);

	std::vector<McrRow> const& rows () const { return _rows; }

	/* Patch mode (false) or Play mode (true): nothing changes with it yet */
	void set_play_mode (bool);
	bool play_mode () const { return _play; }

	/* what is under a point of the canvas: a row and a device in it (-1 for none) */
	struct Hit {
		Hit () : row (-1), device (-1), part (DefaultFace::Outside) {}
		int               row;
		int               device;
		DefaultFace::Part part;
	};
	Hit hit (double x, double y) const;

	/* the Device view's sizes, at its first zoom */
	static int face_height ();
	static int title_height ();

protected:
	void on_map ();
	bool on_expose_event (GdkEventExpose*);
	bool on_button_press_event (GdkEventButton*);
	bool on_motion_notify_event (GdkEventMotion*);
	bool on_leave_notify_event (GdkEventCrossing*);

private:
	void session_going_away ();

	/* everything again: tracks added, removed, hidden or reordered */
	void queue_rebuild ();
	void rebuild ();
	/* a track's devices again: devices added, removed, moved, their pins changed */
	void queue_refill (PBD::ID const&);
	bool idle_refill ();
	void fill_row (McrRow&, std::shared_ptr<ARDOUR::Route>);
	void take_picture (McrDevice&) const;
	/* the Device view's newer pictures of the live faces */
	void refresh_pictures ();
	void layout ();
	void layout_jacks (McrDevice&) const;

	McrRow*    row_by_id (PBD::ID const&);
	McrDevice* device_by_id (PBD::ID const& route, PBD::ID const& device);

	void route_property_changed (PBD::PropertyChange const&, PBD::ID);
	void route_pi_changed (PBD::PropertyChange const&, PBD::ID);
	void route_active_changed (PBD::ID);
	void device_changed (PBD::ID, PBD::ID);
	void device_window_changed (PBD::ID, PBD::ID, bool open);
	void custom_gui_showing (ARDOUR::PlugInsertBase*);
	void custom_gui_hidden (ARDOUR::PlugInsertBase*);
	void pi_change (PBD::PropertyChange const&);

	void device_menu (std::shared_ptr<ARDOUR::PluginInsert>, bool live, GdkEventButton*);
	void set_hover (int row, int device);

	void invalidate (ArdourCanvas::Rect const&);
	void invalidate_device (McrDevice const&);
	ArdourCanvas::Rect device_area (McrDevice const&) const;

	void paint (cairo_t*, ArdourCanvas::Rect const&);
	void paint_grid (cairo_t*, ArdourCanvas::Rect const&);
	void paint_row (cairo_t*, McrRow const&, int row, ArdourCanvas::Rect const&);
	void paint_header (cairo_t*, McrRow const&);
	void paint_picture (cairo_t*, McrDevice const&);
	void paint_jacks (cairo_t*, McrDevice const&, bool hovered);
	void drop_grid ();

	std::vector<McrRow> _rows;
	bool                _play;
	int                 _hover_row;
	int                 _hover_device;
	double              _width;  /* the canvas's */
	double              _height;
	cairo_surface_t*    _grid;   /* one tile of plugdata's dot grid */

	sigc::connection   _rebuild_idle;
	sigc::connection   _refill_idle;
	std::set<PBD::ID>  _to_refill;

	/* every track's own signals, dropped at each rebuild */
	PBD::ScopedConnectionList _route_connections;

	/* a row's devices' signals, dropped when it is refilled */
	struct RowConnections {
		~RowConnections ();
		PBD::ScopedConnectionList     devices;
		std::vector<sigc::connection> windows;
	};
	std::map<PBD::ID, std::shared_ptr<RowConnections> > _row_connections;
};
