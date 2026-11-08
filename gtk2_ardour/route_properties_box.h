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

#include <string>
#include <vector>

#include <ytkmm/box.h>
#include <ytkmm/label.h>
#include <ytkmm/scrolledwindow.h>

#include "ardour/ardour.h"
#include "ardour/session_handle.h"

#include "widgets/frame.h"

namespace ARDOUR {
	class Route;
	class Processor;
	class Session;
}

class GenericPluginUI;
class ProcessorBox;

/* Spite: a log of the Cue page's bottom panel sizes and its device faces' GUIs,
 * written afresh each run to cue-layout.log in Spite's cache folder */
void spite_cue_log (std::string const&);

class RoutePropertiesBox : public Gtk::HBox, public ARDOUR::SessionHandlePtr
{
public:
	RoutePropertiesBox ();
	~RoutePropertiesBox ();

	void set_session (ARDOUR::Session*);
	void set_route (std::shared_ptr<ARDOUR::Route>, bool force = false);
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
	void refill_processors ();
	void add_processor_to_display (std::weak_ptr<ARDOUR::Processor> w);
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
	std::vector<class DeviceFace*> _faces;

	int face_height () const;
	int title_height () const;
	int face_overhead () const;

	/* Ctrl+wheel over the devices scrolls them sideways */
	void devices_viewport_realized ();
	void devices_viewport_unrealized ();
	static GdkFilterReturn devices_wheel_filter (GdkXEvent*, GdkEvent*, gpointer);
	void scroll_devices (int dir);

	int _idle_refill_processors_id;

	PBD::ScopedConnectionList _processor_connections;
	PBD::ScopedConnectionList _route_connections;
	PBD::ScopedConnectionList _forever_connections;
};

