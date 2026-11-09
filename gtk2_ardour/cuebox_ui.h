/*
 * Copyright (C) 2021 Paul Davis <paul@linuxaudiosystems.com>
 * Author: Ben Loftis 2021 <ben@harrisonconsoles.com>
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

#ifndef _gtk_ardour_cuebox_ui_h_
#define _gtk_ardour_cuebox_ui_h_

#include <ytkmm/window.h>
#include <ytkmm/colorselection.h>
#include <ytkmm/menu.h>

#include "pbd/properties.h"

#include "ardour/triggerbox.h"

#include "canvas/canvas.h"
#include "canvas/rectangle.h"

#include "fitted_canvas_widget.h"

typedef std::list<ARDOUR::TriggerPtr > TriggerList;

namespace ArdourCanvas
{
	class Text;
	class Polygon;
	class Circle;
}

class CueEntry : public ArdourCanvas::Rectangle, public sigc::trackable
{
public:
	CueEntry (ArdourCanvas::Item* item, uint64_t cue_index);
	~CueEntry ();

	ArdourCanvas::Circle*    name_button;
	ArdourCanvas::Text*      jump_text;

	void render (ArdourCanvas::Rect const& area, Cairo::RefPtr<Cairo::Context> context) const;

	void _size_allocate (ArdourCanvas::Rect const&);
	bool event_handler (GdkEvent*);

	/* Spite: CueBoxUI calls this for all rows when the theme or the cue
	 * recording state changes, instead of each row connecting to both
	 */
	void set_default_colors ();

	/* Spite: the scene this row shows (rows are pooled, see CueBoxUI) */
	uint64_t index () const { return _cue_idx; }
	void     set_index (uint64_t);

private:
	void shape_play_button ();

	uint64_t _cue_idx;
	bool _grabbed;
};

class CueBoxUI : public ArdourCanvas::Rectangle, public ARDOUR::SessionHandlePtr, public sigc::trackable
{
public:
	CueBoxUI (ArdourCanvas::Item* parent);
	~CueBoxUI ();

	void trigger_cue_row (uint64_t n);

	void _size_allocate (ArdourCanvas::Rect const&);

	/* Spite: rows only for the scenes on screen, like the clip grids
	 * (TriggerBoxUI): scene s is in _slots[s % _slots.size ()]
	 */
	void set_session (ARDOUR::Session*);
	void build ();

private:
	bool row_event (GdkEvent*, CueEntry*);
	bool event (GdkEvent*, uint64_t);
	bool text_event (GdkEvent*, uint64_t);

	void context_menu (GdkEventButton*, uint64_t idx);
	void get_slots (TriggerList &triggerlist, uint64_t idx);

	void clear_all_triggers(uint64_t idx);
	void set_all_colors (uint64_t idx);
	void set_all_follow_action (ARDOUR::FollowAction const &, uint64_t idx);
	void set_all_launch_style (ARDOUR::Trigger::LaunchStyle, uint64_t idx);
	void set_all_quantization (Temporal::BBT_Offset const&, uint64_t idx);

	Gtk::Menu* _context_menu;
	Gtk::ColorSelectionDialog _color_dialog;

	typedef std::vector<CueEntry*> Slots;

	Slots                _slots;   /* rows past _pool_n are hidden, never deleted while laying out */
	size_t               _pool_n;
	std::vector<int64_t> _slot_of; /* the scene each row shows, -1 for none */
	double               _width;
	double               _height;
	double               _rows_w;
	double               _rows_h;
	sigc::connection     _scroll_connection;

	void     layout ();
	uint32_t n_scenes () const;

	/* Spite: for all rows, see CueEntry::set_default_colors () */
	PBD::ScopedConnectionList _cue_connections;
	void rows_colors_changed ();
	void ui_parameter_changed (std::string const&);
};

class CueBoxWidget : public FittedCanvasWidget, public ARDOUR::SessionHandlePtr
{
public:
	CueBoxWidget (float w, float h);

	void set_session (ARDOUR::Session* s);

	void on_map ();
	void on_unmap ();

	/* Spite: the column fills the master strip's height, see SceneScroll */
	void on_size_allocate (Gtk::Allocation&);
	bool on_scroll_event (GdkEventScroll*);

private:
	CueBoxUI* ui;
	PBD::ScopedConnection _scene_connection;

	void scenes_changed (); /* Spite: the session's scene count changed */
};

/* XXX probably for testing only */

class CueBoxWindow : public Gtk::Window
{
public:
	CueBoxWindow ();

	bool on_key_press_event (GdkEventKey*);
	bool on_key_release_event (GdkEventKey*);
};

#endif
