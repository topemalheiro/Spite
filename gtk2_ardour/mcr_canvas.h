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
#include "mcr_patch.h"
#include "route_properties_box.h"

namespace ARDOUR {
	class PlugInsertBase;
	class PluginInsert;
	class Route;
}

class LV2PluginUI;
class McrCables;

/* Spite: the Modular Channels Rack's layout, in canvas pixels (the canvas's bin
 * window's), kept in plain structs apart from the drawing. The patching layer
 * (McrPatch: jacks, cables, slivers, Detective Mode) gets it after each layout. */

struct McrDevice {
	McrDevice () : custom_open (false) {}

	std::weak_ptr<ARDOUR::PluginInsert> insert;
	PBD::ID                             id;      /* the processor's */
	DefaultFace                         card;    /* what it shows (card.live: it has a face of its own; card.in_window: any window of its own is open) */
	bool                                custom_open; /* its own GUI is in its window (PgD4S: the patch editor), not just its generic controls */
	Glib::RefPtr<Gdk::Pixbuf>           picture; /* a live face's last picture (the Device view's), at the face's height */
	Glib::RefPtr<Gdk::Pixbuf>           source;  /* the Device view's picture it was scaled from */
	ArdourCanvas::Rect                  rect;    /* its title strip and face: its jacks sit on its edges */
};

struct McrRow {
	McrRow () : colour (0), number (0), active (true) {}

	std::weak_ptr<ARDOUR::Route> route;
	PBD::ID                      id;     /* the track's */
	std::string                  name;
	uint32_t                     colour; /* RGBA */
	int                          number; /* the track's, 0 if it has none */
	bool                         active;
	ArdourCanvas::Rect           rect;    /* the whole row, across the canvas */
	ArdourCanvas::Rect           header;  /* the track's name and colour */
	std::vector<McrDevice>       devices; /* in processor order */
	ArdourCanvas::Rect           sliver;  /* between this row and the next (empty for the last): the patching layer's */
};

/* a PgD4S device's live face in the MCR: its GUI, put () in the canvas, with the GUI's
 * window composited, so the canvas paints it itself (from the window's pixmap) and can
 * draw over it */
struct McrFace {
	McrFace () : ui (0), window (0), pixmap (0), surface (0), width (0), height (0), x (-1), y (-1), hidden_w (0), hidden_h (0) {}

	LV2PluginUI*      ui;
	GdkWindow*        window;  /* the GUI's own (suil's GtkSocket's), composited */
	unsigned long     pixmap;  /* that window's pixmap (an X Pixmap), named by Composite */
	cairo_surface_t*  surface; /* to paint it */
	int               width;   /* the pixmap's */
	int               height;
	int               x;       /* where the GUI was put, in canvas pixels */
	int               y;
	int               hidden_w; /* its size when the canvas was last hidden */
	int               hidden_h;
	sigc::connection  resized;
};

/* The MCR's canvas: one row per track, each a header and its devices, with slivers
 * between the rows. It paints everything itself, on its bin window, with no child
 * widget per device, so cables can be drawn over everything: live PgD4S faces too,
 * which are put () in it as composited child windows of that bin window, and painted
 * by the canvas where they are (1:1, as input still goes to the windows themselves).
 *
 * Its patching layer (McrPatch) draws the jacks, the cables and the slivers, under the
 * faces in Play mode and over them in Patch mode, and gets the pointer and the keys
 * first; it reaches the session's cables through McrCables, made with the session.
 *
 * Every PgD4S device's face is live here (made one by one, nearest to the view first,
 * its picture showing meanwhile), in Play and Patch mode alike. In Patch mode an input
 * shield over the canvas takes every pointer event, so faces keep moving but can't be
 * touched. Faces are leased with the Cue page's Device view (SpiteFaceHolder): the MCR
 * takes the ones the Device view has when it shows, and gives them back. */
class McrCanvas : public Gtk::Layout, public ARDOUR::SessionHandlePtr, public SpiteFaceHolder
{
public:
	McrCanvas ();
	~McrCanvas ();

	void set_session (ARDOUR::Session*);

	std::vector<McrRow> const& rows () const { return _rows; }

	/* the jacks, cables, slivers and Detective Mode */
	McrPatch&       patch () { return _patch; }
	McrPatch const& patch () const { return _patch; }
	/* the session's cables; 0 without a session */
	McrCables* cables () const { return _cables; }
	/* the rows of the tracks selected in the session (McrPatchCables::key) */
	std::vector<uint64_t> selected_rows () const;

	/* Patch mode (false) or Play mode (true): in Patch mode the input shield covers the
	 * faces (they are dimmed a little) and the cables go over them; in Play mode the faces
	 * get their input and the cables go behind them */
	void set_play_mode (bool);
	bool play_mode () const { return _play; }

	/* Layers a later stage paints with the canvas, in canvas pixels, clipped to the area
	 * being repainted: one behind the devices and their faces (Play mode's cables), one
	 * over everything, live faces included (Patch mode's cables, Detective Mode) */
	sigc::signal<void, cairo_t*, ArdourCanvas::Rect const&> PaintBehindFaces;
	sigc::signal<void, cairo_t*, ArdourCanvas::Rect const&> PaintOverFaces;

	/* repaint an area of the canvas (canvas pixels) */
	void invalidate (ArdourCanvas::Rect const&);

	/* the canvas's own windows, where its pointer events come from, both in canvas
	 * pixels: its bin window, and the shield over the faces (Patch mode) */
	bool is_canvas_window (GdkWindow*) const;

	/* the device's face is live here, and shows */
	bool face_is_live (McrDevice const&) const;

	/* the canvas is on screen: mapped, in a window that is (a window of its own, once the
	 * MCR is detached, hides without unmapping the canvas) and isn't minimized */
	bool showing () const;

	/* Escape in the MCR: a cable being dragged goes, else the selected cable is
	 * deselected; false if there was neither */
	bool escape ();
	/* a cable (or a sliver's edge) being dragged stops, as if Escape was pressed */
	void cancel_drag ();

	/* what is under a point of the canvas: a row and a device in it (-1 for none) */
	struct Hit {
		Hit () : row (-1), device (-1), part (DefaultFace::Outside) {}
		int               row;
		int               device;
		DefaultFace::Part part;
	};
	Hit hit (double x, double y) const;

	/* the Device view's sizes: its faces' height (all faces', see face_height ()), and
	 * its title bars' */
	static int face_height ();
	static int title_height ();

protected:
	void on_realize ();
	void on_unrealize ();
	void on_map ();
	void on_unmap ();
	void on_size_allocate (Gtk::Allocation&);
	bool on_expose_event (GdkEventExpose*);
	bool on_button_press_event (GdkEventButton*);
	bool on_button_release_event (GdkEventButton*);
	bool on_motion_notify_event (GdkEventMotion*);
	bool on_scroll_event (GdkEventScroll*);
	bool on_key_press_event (GdkEventKey*);
	bool on_enter_notify_event (GdkEventCrossing*);
	bool on_leave_notify_event (GdkEventCrossing*);

	LV2PluginUI* lend_face (PBD::ID const&);
	bool         face_shown (PBD::ID const&) const;

private:
	void session_going_away ();

	/* the canvas comes on screen, or goes: its page or its window shows or hides, or its
	 * window is minimized */
	void shown ();
	void hidden ();
	bool on_screen () const; /* shown, and still showing () */
	bool idle_shown ();
	bool toplevel_mapped (GdkEventAny*);
	bool toplevel_unmapped (GdkEventAny*);
	bool toplevel_state (GdkEventWindowState*);
	bool grab_broken (GdkEventGrabBroken*);
	void grab_notify (bool was_grabbed);

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
	void place ();
	void update_model ();

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

	void invalidate_device (McrDevice const&);
	ArdourCanvas::Rect device_area (McrDevice const&) const;

	void paint (cairo_t*, ArdourCanvas::Rect const&);
	void paint_grid (cairo_t*, ArdourCanvas::Rect const&);
	void paint_row (cairo_t*, McrRow const&, int row, ArdourCanvas::Rect const&);
	void paint_header (cairo_t*, McrRow const&);
	void paint_picture (cairo_t*, McrDevice const&);
	bool paint_face (cairo_t*, McrDevice const&);
	void paint_debug (cairo_t*, ArdourCanvas::Rect const&);
	void drop_grid ();

	/* the patching layer */
	void patch_relayout ();
	void patch_redraw (PdCord::Rect);
	void patch_reveal (PdCord::Rect);
	void view_changed ();
	void set_patch_cursor ();
	void drop_cables ();
	/* a cable dragged near an edge of the view scrolls the canvas */
	void autoscroll_check (double x, double y, unsigned state);
	bool autoscroll ();

	/* live faces */
	McrFace* face_of (PBD::ID const&) const;
	void     queue_faces ();
	bool     idle_faces ();
	bool     take_faces ();
	void     make_face (McrDevice&);
	void     adopt_face (PBD::ID const&, LV2PluginUI*, bool show);
	bool     reveal_faces ();
	void     free_face (PBD::ID const&);
	void     free_faces ();
	void     prune_faces ();
	void     composite_face (McrFace&);
	void     uncomposite_face (McrFace&);
	void     keep_picture (PBD::ID const&, McrFace&);
	cairo_surface_t* face_surface (McrFace&);
	void     drop_face_surface (McrFace&);
	void     face_resized (PBD::ID);
	void     queue_layout ();
	bool     idle_layout ();
	static GdkFilterReturn face_filter (GdkXEvent*, GdkEvent*, gpointer);

	/* the shield over the faces, in Patch mode */
	void size_shield ();
	void show_shield ();

	std::vector<McrRow> _rows;
	McrPatch            _patch;
	McrCables*          _cables;      /* the session's cables, made with it */
	McrPatchEngine*     _engine;      /* the patching layer's way to them */
	bool                _in_layout;
	bool                _relayout;    /* the layer asked again during a layout */
	int                 _cursor;      /* McrPatch::Cursor shown, -1 none yet */
	sigc::connection    _hscroll;
	sigc::connection    _vscroll;
	sigc::connection    _autoscroll;
	double              _drag_x;      /* the pointer during a cable drag, canvas pixels */
	double              _drag_y;
	unsigned            _drag_state;
	bool                _play;
	int                 _hover_row;
	int                 _hover_device;
	double              _width;  /* the canvas's */
	double              _height;
	cairo_surface_t*    _grid;   /* one tile of plugdata's dot grid */

	std::map<PBD::ID, McrFace*> _faces;       /* the live faces the MCR holds, by device */
	std::set<PBD::ID>           _face_failed; /* GUIs that couldn't be made: not tried again */
	GdkWindow*                  _shield;      /* InputOnly, over the faces */
	bool                        _composite;   /* the display composites windows: faces can be live */
	bool                        _debug;       /* SPITE_MCR_DEBUG: a line over each live face */
	bool                        _shown;       /* see shown () */
	bool                        _turned_to;   /* just shown: it takes the faces a view on screen has too */
	sigc::connection            _top_map;     /* its window's */
	sigc::connection            _top_unmap;
	sigc::connection            _top_state;

	sigc::connection   _shown_idle;
	sigc::connection   _rebuild_idle;
	sigc::connection   _refill_idle;
	sigc::connection   _face_idle;
	sigc::connection   _reveal_timer;
	sigc::connection   _layout_idle;
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
