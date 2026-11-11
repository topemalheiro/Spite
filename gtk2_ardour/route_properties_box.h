/*
 * Copyright (C) 2021 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2024 Ben Loftis <ben@harrisonconsoles.com>
 * Copyright (C) 2024 Robin Gareus <robin@gareus.org>
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

#pragma once

#include <list>
#include <string>
#include <vector>

#include <ydkmm/pixbuf.h>
#include <ytkmm/box.h>
#include <ytkmm/label.h>
#include <ytkmm/scrolledwindow.h>

#include "pbd/id.h"

#include "ardour/ardour.h"
#include "ardour/session_handle.h"

#include "widgets/frame.h"

namespace ARDOUR {
	class Route;
	class Processor;
	class Session;
}

class GenericPluginUI;
class LV2PluginUI;
class ProcessorBox;

/* Spite: a log of the Cue page's bottom panel sizes and its device faces' GUIs,
 * written afresh each run to cue-layout.log in Spite's cache folder */
void spite_cue_log (std::string const&);

/* Spite: the last picture the Device view kept of a device's face (by its processor),
 * for whatever stands in for the face (the MCR); none if the face never showed */
Glib::RefPtr<Gdk::Pixbuf> spite_face_picture (PBD::ID const&);
/* a newer picture of a device's face, taken elsewhere (the MCR): kept as the Device
 * view keeps its own, for this run and the next time the session opens */
void spite_keep_face_picture (PBD::ID const&, Glib::RefPtr<Gdk::Pixbuf>);

/* Spite: a window shows on screen: it is viewable, in a window that isn't minimized. A
 * window manager unmaps a minimized window (or says it is hidden), and GDK still calls
 * the windows in it viewable. */
bool spite_window_on_screen (Glib::RefPtr<Gdk::Window const> const&);

/* Spite: the face lease. A PgD4S device's face is its live GUI (an LV2PluginUI, which
 * takes 0.3-0.5 s to make), and a plugin has one GUI at a time. The Cue page's Device
 * view and the MCR both show devices' faces: whichever shows a device holds its GUI,
 * and takes it from the other by moving it into its own container, so it isn't made
 * again. With both on screen (the MCR in a window of its own), the one shown last keeps
 * the GUIs it took, and the other shows the faces' pictures. An open plugin window wins
 * over both: whoever holds the GUI frees it when the plugin's own window opens
 * (PluginUIWindow::CustomGUIShowing). */
class SpiteFaceHolder
{
public:
	virtual ~SpiteFaceHolder ();

	/* who holds the device's GUI, if anyone */
	static SpiteFaceHolder* holder (PBD::ID const& device);

	/* how long a moved GUI stays hidden before it shows (its picture stands in): moving
	 * hides a GUI's windows, which loses what they showed, and PgD4S only draws all of
	 * it again once it has seen it hidden, which it looks for every 200 ms */
	static int const settle_ms = 300;

	/* A holder stopped showing faces (its view went, or it let their GUIs go): the holders
	 * on screen take the GUIs of the devices they show, or make them. Emitted once Spite
	 * is idle, after the page switch (or the window that hid) is done. */
	static sigc::signal<void> FacesFree;

protected:
	/* The device's GUI, from whoever holds it, moved into `to` (packed last in a box, at
	 * 0,0 in a layout): this holder holds it now. Both containers must be realized: GTK
	 * then only unmaps the GUI while X moves its windows, the plugin's along.
	 * `shown_too`: this holder was just shown (the user turned to it), so it takes the GUI
	 * from a holder that shows the device too; else only from one that doesn't, so two
	 * views on screen at once never pass a GUI back and forth.
	 * 0 if no one has it (make one), the holder keeps it, or it couldn't move (it is freed
	 * then: make one). */
	LV2PluginUI* take_face (PBD::ID const& device, Gtk::Container& to, bool shown_too);
	/* this holder made the device's GUI */
	void hold_face (PBD::ID const& device);
	/* this holder freed its GUI of the device (or goes) */
	void drop_face (PBD::ID const& device);
	/* faces may be free now: see FacesFree */
	static void faces_free ();
	/* No one holds the device's GUI (its window had it, or a view freed it), and the view
	 * that held it last shows the device too: that one makes it again, not this one */
	bool leave_to_last (PBD::ID const& device) const;

	/* Another holder takes the device's GUI: let go of it (it stays in this holder's
	 * container until the taker moves it) and stand in with the face's picture. Returns
	 * the GUI, or 0 if this holder has none after all. */
	virtual LV2PluginUI* lend_face (PBD::ID const& device) = 0;
	/* this holder's face of the device is on screen now (its view is) */
	virtual bool face_shown (PBD::ID const& device) const = 0;
};

class RoutePropertiesBox : public Gtk::HBox, public ARDOUR::SessionHandlePtr
{
public:
	RoutePropertiesBox ();
	~RoutePropertiesBox ();

	void set_session (ARDOUR::Session*);
	void set_route (std::shared_ptr<ARDOUR::Route>, bool force = false);
	std::shared_ptr<ARDOUR::Route> route () const { return _route; }
	/* shown instead of an empty strip when the track has no plugins; empty for none */
	void set_empty_text (std::string const&);

	/* Spite: the Cue page's device view always shows the device list
	 * beside the device panels, at its natural (Live-like) height */
	void set_always_show_insert_list (bool);

	/* Spite: PgD4S devices show their own faces (the Cue page's Device view) */
	void set_device_faces (bool);
	/* the height the Device view has: faces zoom to fit it */
	void set_available_height (int);
	float face_zoom () const { return _face_zoom; }
	/* the Device view's height for a face zoom, and the least it can have */
	int height_for_zoom (float) const;
	float zoom_for_height (int) const;
	int min_height () const;

private:
	void property_changed (const PBD::PropertyChange& what_changed);
	void map_frozen ();
	void ui_actions_ready ();
	void update_processor_box_visibility ();
	void update_empty_label ();
	void session_going_away ();
	void drop_route ();
	void drop_plugin_uis ();
	void drop_generic_uis ();
	void refill_processors ();
	void add_processor_to_display (std::weak_ptr<ARDOUR::Processor> w);
	void face_gone (class DeviceFace*);
	void delete_face (class DeviceFace*);
	void idle_refill_processors ();
	void surround_master_added_or_removed ();

	static int _idle_refill_processors (gpointer);

	Gtk::ScrolledWindow _scroller;
	Gtk::HBox           _box;
	Gtk::Label          _empty_label;

	std::shared_ptr<ARDOUR::Route> _route;
	std::vector <GenericPluginUI*> _proc_uis;

	ArdourWidgets::Frame _insert_frame;
	ProcessorBox*        _insert_box;
	bool                 _show_insert;
	bool                 _always_show_insert;
	bool                 _device_faces;
	float                _face_zoom;
	std::vector<class DeviceFace*> _faces; /* the track's, as shown */
	/* Spite: the faces of the tracks shown last are kept, hidden, so going back to one is
	 * instant: all faces stay in _box (a live X11 GUI must never be re-parented) */
	std::vector<class DeviceFace*> _all_faces;
	std::list<PBD::ID>             _recent_routes; /* the one shown first */
	std::vector<Gtk::Widget*>      _generic_frames;
	int                            _display_pos;

	int face_height () const;
	int title_height () const;
	int face_overhead () const;

	/* Ctrl+wheel over the devices scrolls them sideways */
	void devices_viewport_realized ();
	void devices_viewport_unrealized ();
	static GdkFilterReturn devices_wheel_filter (GdkXEvent*, GdkEvent*, gpointer);
	void scroll_devices (int dir);
	/* the pointer left a face (into the devices' row, or out of it) */
	bool devices_crossing (GdkEventCrossing*);
	/* the page shows again or goes: the faces are told */
	void devices_shown ();
	void devices_hidden ();
	/* another view let faces go (SpiteFaceHolder::FacesFree): the faces shown take them */
	void devices_free ();
	/* the window the Device view is in is minimized or hidden, or shows again (GTK
	 * doesn't unmap the view then): the faces are told too */
	void watch_toplevel ();
	void unwatch_toplevel ();
	bool toplevel_mapped (GdkEventAny*);
	bool toplevel_unmapped (GdkEventAny*);
	bool toplevel_state (GdkEventWindowState*);

	/* the view's signals, and its window's: dropped before the faces go */
	std::vector<sigc::connection> _view_connections;
	std::vector<sigc::connection> _toplevel_connections;

	int _idle_refill_processors_id;

	PBD::ScopedConnectionList _processor_connections;
	PBD::ScopedConnectionList _route_connections;
	PBD::ScopedConnectionList _forever_connections;
};

