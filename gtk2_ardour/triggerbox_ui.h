/*
 * Copyright (C) 2021 Paul Davis <paul@linuxaudiosystems.com>
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

#ifndef _gtk_ardour_triggerbox_ui_h_
#define _gtk_ardour_triggerbox_ui_h_

#include <ytkmm/colorselection.h>

#include "pbd/properties.h"

#include "ardour/triggerbox.h"

#include "canvas/canvas.h"
#include "canvas/rectangle.h"

#include "gtkmm2ext/colors.h"

#include "fitted_canvas_widget.h"

#include "trigger_ui.h"

namespace Temporal
{
	struct BBT_Offset;
}

namespace ArdourCanvas
{
	class Text;
	class Polygon;
}

class TriggerStrip;

/* Spite: one row of the clip grid, one per scene and track. Its box's
 * signals (PropertyChanged, TriggerSwapped, RecEnableChanged), its track's
 * color, the theme and the canvas's drag signals reach it through its
 * TriggerBoxUI, which connects to each of them once: the engine emits the
 * box's signals from the audio thread, and a PBD::Signal's emission costs
 * O(subscribers^2), so one subscriber per scene made every clip start and
 * stop take milliseconds there with hundreds of scenes.
 */
class TriggerEntry : public ArdourCanvas::Rectangle, public TriggerUI
{
public:
	TriggerEntry (ArdourCanvas::Item* item, TriggerStrip&, ARDOUR::TriggerReference rf);
	~TriggerEntry ();

	/* Spite: show another slot (scenes were inserted or removed) */
	void repoint (ARDOUR::TriggerReference);

	ArdourCanvas::Rectangle* play_button;
	ArdourCanvas::Rectangle* name_button;
	ArdourCanvas::Rectangle* follow_button;
	ArdourCanvas::Text*      name_text;

	/* Spite: relayed by TriggerBoxUI */
	void rec_enable_change ();
	void owner_color_changed ();
	bool drag_active () const { return _drag_active; }
	void drag_begin (Glib::RefPtr<Gdk::DragContext> const&);
	void drag_end (Glib::RefPtr<Gdk::DragContext> const&);
	void drag_data_get (Glib::RefPtr<Gdk::DragContext> const&, Gtk::SelectionData&, guint, guint);
	bool drawn_selected () const { return _drawn_selected; }

	void draw_launch_icon (Cairo::RefPtr<Cairo::Context> context, float size, float scale) const;
	void draw_follow_icon (Cairo::RefPtr<Cairo::Context> context, ARDOUR::FollowAction const & icon, float size, float scale) const;

	void render (ArdourCanvas::Rect const& area, Cairo::RefPtr<Cairo::Context> context) const;

	void _size_allocate (ArdourCanvas::Rect const&);

	void on_trigger_changed (PBD::PropertyChange const& change);

	void selection_change ();

	enum EnteredState {
		PlayEntered,
		NameEntered,
		FollowEntered,
		NoneEntered
	};

	void set_widget_colors (TriggerEntry::EnteredState es = NoneEntered);

	bool name_button_event (GdkEvent*);

	TriggerStrip& strip() const { return _strip; }

private:
	TriggerStrip& _strip;
	bool   _grabbed;

	int  _drag_start_x;
	int  _drag_start_y;
	bool _drag_active;
	bool  rec_blink_on;
	bool _drawn_selected; /* Spite: drawn with the selection's border */

	bool event (GdkEvent*);

	void connect_to_trigger () override; /* TriggerUI's, and the trigger's ArmChanged */

	bool play_button_event (GdkEvent*);
	bool follow_button_event (GdkEvent*);

	void set_play_button_tooltip ();
	Gtkmm2ext::Color bg_color () const;
	void blink_rec_enable (bool);
	sigc::connection rec_blink_connection;
};

class TriggerBoxUI : public ArdourCanvas::Rectangle, public sigc::trackable
{
public:
	TriggerBoxUI (ArdourCanvas::Item* parent, TriggerStrip&, ARDOUR::TriggerBox&);
	~TriggerBoxUI ();

	void _size_allocate (ArdourCanvas::Rect const&);

	TriggerStrip& strip() const { return _strip; }
	TriggerEntry* entry_by_trigger (ARDOUR::Trigger const &) const;
	/* Spite: the row showing scene s, if it is on screen */
	TriggerEntry* entry_for_slot (uint32_t s) const;

	void build (); /* Spite: also to rebuild, when scenes are inserted or removed */
	ARDOUR::TriggerBox& triggerbox () const { return _triggerbox; }

	static Glib::RefPtr<Gtk::TargetList> dnd_src ()
	{
		return _dnd_src;
	}

private:
	typedef std::vector<TriggerEntry*> Slots;

	/* Spite: rows only for the scenes on screen (SceneScroll): scene s is
	 * in row _slots[s % _pool_n], at y = s * row height - the scroll
	 * offset. Scrolling by k scenes points k rows at other slots. Rows are
	 * never deleted while the grid lays out (one may be handling the event
	 * that made the view shorter): rows past _pool_n are hidden.
	 */
	ARDOUR::TriggerBox& _triggerbox;
	std::vector<ARDOUR::Trigger const*> _shown;   /* the trigger each row last showed */
	std::vector<int64_t>                _slot_of; /* the scene each row shows, -1 for none */
	Slots               _slots;
	size_t              _pool_n;  /* rows in use */
	TriggerStrip&       _strip;
	double              _width;
	double              _height;
	double              _rows_w;  /* the size the rows were given */
	double              _rows_h;
	sigc::connection    _scroll_connection;

	void layout ();

	static Glib::RefPtr<Gtk::TargetList> _dnd_src;

	void selection_changed ();

	bool drag_motion (Glib::RefPtr<Gdk::DragContext> const&, int, int, guint);
	void drag_leave (Glib::RefPtr<Gdk::DragContext> const&, guint);
	void drag_data_received (Glib::RefPtr<Gdk::DragContext> const&, int, int, Gtk::SelectionData const&, guint, guint);

	bool triggerbox_event (GdkEvent*);

	uint64_t slot_at_y (int) const;

	sigc::connection _selection_connection;

	/* Spite: the box's signals, the track's color, the theme and the
	 * canvas's drag signals, for all rows (see TriggerEntry)
	 */
	PBD::ScopedConnectionList _box_connections;
	sigc::connection          _cue_check_connection;
	int32_t                   _cue_row;    /* the row of the box's current trigger, as drawn */
	bool                      _cue_active; /* whether that trigger was drawn playing */

	void box_property_changed (PBD::PropertyChange const&);
	void update_cue_rows ();
	void trigger_swapped (uint32_t slot);
	void rec_enable_changed ();
	void defaults_changed ();
	void owner_prop_change (PBD::PropertyChange const&);
	void ui_parameter_changed (std::string const&);
	int32_t cue_row () const;
	TriggerEntry* dragging_row () const;
	void drag_begin (Glib::RefPtr<Gdk::DragContext> const&);
	void drag_end (Glib::RefPtr<Gdk::DragContext> const&);
	void drag_data_get (Glib::RefPtr<Gdk::DragContext> const&, Gtk::SelectionData&, guint, guint);
};

class TriggerBoxWidget : public FittedCanvasWidget
{
public:
	TriggerBoxWidget (TriggerStrip&, float w, float h);

	void set_triggerbox (ARDOUR::TriggerBox* tb);
	TriggerStrip& strip() const { return _strip; }
	TriggerBoxUI* ui() const { return _ui; }

	static float slot_height () { return 16.f; } /* Spite: one scene's row */

	/* Spite: the grid fills the strip's height above its mixer and shows the
	 * scenes the page's scene scroll is at (SceneScroll) */
	void on_size_allocate (Gtk::Allocation&);
	bool on_scroll_event (GdkEventScroll*);
private:
	TriggerBoxUI* _ui;
	TriggerStrip& _strip;
	PBD::ScopedConnection _scene_connection;

	void scenes_changed (); /* Spite: the session's scene count changed */
};

#endif
