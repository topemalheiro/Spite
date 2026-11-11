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

#include <string>
#include <vector>

#include <cairo.h>

#include <ytkmm/drawingarea.h>

#include "pd_cord.h"

/* Spite: the MCR's bottom bar, drawn as plugdata's status bar is (Statusbar.cpp): Patch
 * and Play (plugdata's edit and run modes), Detective Mode's toggle and speed (0.1 to
 * 5), Pause and Step, and the toggle of the history panel. It only shows what it is
 * told and says what was clicked; the MCR page's actions do the rest. */
class McrBar : public Gtk::DrawingArea
{
public:
	McrBar ();

	void set_play (bool);
	void set_detective (bool);
	void set_speed (float);
	void set_paused (bool);
	void set_history (bool);

	sigc::signal<void, bool>  PlayMode;  /* Patch (false) or Play (true) was clicked */
	sigc::signal<void>        DetectiveClicked;
	sigc::signal<void, float> SpeedChanged;
	sigc::signal<void>        PauseClicked;
	sigc::signal<void>        StepClicked;
	sigc::signal<void>        HistoryClicked;

	static float min_speed () { return .1f; }
	static float max_speed () { return 5.f; }

protected:
	bool on_expose_event (GdkEventExpose*);
	void on_size_request (Gtk::Requisition*);
	void on_size_allocate (Gtk::Allocation&);
	bool on_button_press_event (GdkEventButton*);
	bool on_button_release_event (GdkEventButton*);
	bool on_motion_notify_event (GdkEventMotion*);
	bool on_leave_notify_event (GdkEventCrossing*);
	bool on_scroll_event (GdkEventScroll*);
	bool on_query_tooltip (int x, int y, bool keyboard, Glib::RefPtr<Gtk::Tooltip> const&);

private:
	enum Item {
		None = -1,
		Patch,
		Play,
		Detective,
		Speed,
		Pause,
		Step,
		History,
		n_items
	};

	void  place ();
	Item  item_at (double x, double y) const;
	void  set_hover (Item);
	void  drag_speed (double x);
	float speed_at (double x) const;
	void  paint_button (cairo_t*, Item, std::string const&, bool active, bool enabled);
	void  paint_slider (cairo_t*);
	bool  enabled (Item) const;
	double scale () const;

	PdCord::Rect _rects[n_items]; /* units */
	double       _seps[2];        /* the separators' x (units) */
	Item         _hover;
	Item         _pressed;
	bool         _play;
	bool         _detective;
	float        _speed;
	bool         _paused;
	bool         _history;
	double       _min_width; /* all of it, History right after Step (units) */
};

/* the selected cable's messages, newest first, as plugdata's ConnectionDebugPanel
 * shows a connection's (Sidebar/ConnectionDebugPanel.cpp) */
class McrHistoryPanel : public Gtk::DrawingArea
{
public:
	McrHistoryPanel ();

	/* title: the cable ("1 KICK OUT1 → 2 COMP SC"), empty with none selected */
	void set_history (std::string const& title, std::vector<std::string> const& rows);

protected:
	bool on_expose_event (GdkEventExpose*);
	void on_size_request (Gtk::Requisition*);
	bool on_scroll_event (GdkEventScroll*);

private:
	double scale () const;

	std::string              _title;
	std::vector<std::string> _rows;
	int                      _first; /* the first row shown (scrolled) */
};
