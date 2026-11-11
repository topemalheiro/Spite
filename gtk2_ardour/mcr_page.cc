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

#ifdef WAF_BUILD
#include "gtk2ardour-config.h"
#endif

#include <ydk/gdkkeysyms.h>

#include <ytkmm/window.h>

#include "pbd/xml++.h"

#include "ardour/session.h"

#include "gtkmm2ext/actions.h"
#include "gtkmm2ext/bindings.h"
#include "gtkmm2ext/keyboard.h"
#include "gtkmm2ext/window_title.h"

#include "actions.h"
#include "ardour_ui.h"
#include "gui_thread.h"
#include "mcr_page.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ArdourWidgets;
using namespace Gtkmm2ext;
using namespace Gtk;

MCRPage::MCRPage ()
	: Tabbable (_("Modular Channels Rack"), X_("mcr"), NULL, true, Tabbable::NoPanes)
	, bindings (0)
{
	load_bindings ();
	register_actions ();

	/* the canvas scrolls itself (a Gtk::Layout): no viewport around it */
	_scroller.set_policy (POLICY_AUTOMATIC, POLICY_AUTOMATIC);
	_scroller.add (_canvas);

	_middle.pack_start (_scroller, true, true);
	_middle.pack_start (_history, false, false);
	_history.set_no_show_all (true); /* its toggle shows it */
	_main.pack_start (_middle, true, true);
	_main.pack_start (_bar, false, false);

	content_app_bar.add (_application_bar);
	content_main.add (_main);

	/* the bar says what was clicked; the actions do it (macros and keys use them too) */
	_bar.PlayMode.connect (sigc::mem_fun (*this, &MCRPage::set_play_mode));
	_bar.DetectiveClicked.connect ([this] () { _detective_action->set_active (!_detective_action->get_active ()); });
	_bar.SpeedChanged.connect (sigc::mem_fun (*this, &MCRPage::speed_changed));
	_bar.PauseClicked.connect ([this] () { _pause_action->set_active (!_pause_action->get_active ()); });
	_bar.StepClicked.connect (sigc::mem_fun (*this, &MCRPage::detective_step));
	_bar.HistoryClicked.connect ([this] () { _history_action->set_active (!_history_action->get_active ()); });

	_canvas.patch ().HistoryChanged.connect (sigc::mem_fun (*this, &MCRPage::history_changed));

	_canvas.show ();
	_scroller.show ();
	_middle.show ();
	_bar.show ();
	_main.show ();

	play_mode_toggled (); /* the bar shows the mode */
	history_toggled ();
	history_changed ();

	set_widget_bindings (contents (), *bindings, ARDOUR_BINDING_KEY);
}

MCRPage::~MCRPage ()
{
}

void
MCRPage::load_bindings ()
{
	bindings = Bindings::get_bindings (X_("MCR"));
	if (!bindings) {
		/* a keys file of the user's own from before the MCR: an empty set, so the
		 * MCR's actions and the "MCR" macro scope still have one */
		bindings = new Bindings (X_("MCR"));
	}
	/* Escape is the MCR's own, before the global one (see escape ()): also with a keys
	 * file from before it */
	bindings->add (KeyboardKey (0, GDK_Escape), Bindings::Press, X_("MCR/escape"), 0, false);
}

void
MCRPage::register_actions ()
{
	Glib::RefPtr<ActionGroup> group = ActionManager::create_action_group (bindings, X_("MCR"));

	/* Ctrl+E inside the MCR (Quick Export elsewhere) */
	_play_action = ActionManager::register_toggle_action (group, X_("play-mode"), _("MCR: Play Mode"), sigc::mem_fun (*this, &MCRPage::play_mode_toggled));
	/* Escape inside the MCR */
	ActionManager::register_action (group, X_("escape"), _("MCR: Escape (let go of the cable)"), sigc::mem_fun (*this, &MCRPage::escape));

	ActionManager::register_action (group, X_("open-slivers"), _("MCR: Open All Slivers"), sigc::mem_fun (*this, &MCRPage::open_slivers));
	ActionManager::register_action (group, X_("close-slivers"), _("MCR: Close All Slivers"), sigc::mem_fun (*this, &MCRPage::close_slivers));
	ActionManager::register_action (group, X_("open-slivers-with-cords"), _("MCR: Open the Slivers With Cables"), sigc::mem_fun (*this, &MCRPage::open_slivers_with_cords));
	ActionManager::register_action (group, X_("open-slivers-around-selected"), _("MCR: Open the Slivers Around the Selected Tracks"), sigc::mem_fun (*this, &MCRPage::open_slivers_around_selected));

	_detective_action = ActionManager::register_toggle_action (group, X_("toggle-detective"), _("MCR: Detective Mode"), sigc::mem_fun (*this, &MCRPage::detective_toggled));
	_pause_action     = ActionManager::register_toggle_action (group, X_("detective-pause"), _("MCR: Pause Detective Mode"), sigc::mem_fun (*this, &MCRPage::pause_toggled));
	ActionManager::register_action (group, X_("detective-step"), _("MCR: Step Detective Mode"), sigc::mem_fun (*this, &MCRPage::detective_step));
	_history_action = ActionManager::register_toggle_action (group, X_("toggle-history"), _("MCR: Cable History"), sigc::mem_fun (*this, &MCRPage::history_toggled));
}

/* as the Cue page's own window: Spite's keys, its title, the MCR's bindings */
Gtk::Window*
MCRPage::use_own_window (bool and_fill_it)
{
	bool const new_window = !own_window ();

	Gtk::Window* win = Tabbable::use_own_window (and_fill_it);

	if (win && new_window) {
		win->set_name (X_("MCRWindow"));
		ARDOUR_UI::instance ()->setup_toplevel_window (*win, _("Modular Channels Rack"), this);
		win->signal_event ().connect (sigc::bind (sigc::ptr_fun (&Keyboard::catch_user_event_for_pre_dialog_focus), win));
		set_widget_bindings (*win, *bindings, ARDOUR_BINDING_KEY);
		update_title ();
	}

	contents ().show ();
	return win;
}

void
MCRPage::update_title ()
{
	if (!own_window ()) {
		return;
	}

	if (_session) {
		std::string n = _session->snap_name () != _session->name () ? _session->snap_name () : _session->name ();
		if (_session->dirty ()) {
			n = "*" + n;
		}
		WindowTitle title (n);
		title += S_("Window|MCR");
		title += Glib::get_application_name ();
		own_window ()->set_title (title.get_string ());
	} else {
		WindowTitle title (S_("Window|MCR"));
		title += Glib::get_application_name ();
		own_window ()->set_title (title.get_string ());
	}
}

void
MCRPage::set_play_mode (bool yn)
{
	_play_action->set_active (yn); /* play_mode_toggled () when it changes */
}

void
MCRPage::play_mode_toggled ()
{
	bool const play = _play_action->get_active ();
	_canvas.set_play_mode (play);
	_bar.set_play (play);
}

void
MCRPage::detective_toggled ()
{
	bool const on = _detective_action->get_active ();
	_canvas.patch ().set_detective (on);
	_bar.set_detective (on);
	if (!on && _pause_action->get_active ()) {
		_pause_action->set_active (false);
	}
}

void
MCRPage::pause_toggled ()
{
	bool const yn = _pause_action->get_active ();
	if (yn && !_detective_action->get_active ()) {
		/* only Detective Mode pauses (a macro or a key may ask while it is off) */
		_pause_action->set_active (false);
		return;
	}
	_canvas.patch ().set_detective_paused (yn);
	_bar.set_paused (yn);
}

/* Escape inside the MCR: the canvas's (a cable being dragged goes, else the selected
 * one is deselected), else what Escape does everywhere (the editor's and the mixer's
 * selections go) */
void
MCRPage::escape ()
{
	if (_canvas.escape ()) {
		return;
	}
	if (Glib::RefPtr<Gtk::Action> act = ActionManager::get_action (X_("Main"), X_("Escape"), false)) {
		act->activate ();
	}
}

void
MCRPage::detective_step ()
{
	_canvas.patch ().detective_step ();
}

void
MCRPage::speed_changed (float s)
{
	_canvas.patch ().set_detective_speed (s);
	_bar.set_speed (s);
}

void
MCRPage::history_toggled ()
{
	bool const yn = _history_action->get_active ();
	_bar.set_history (yn);
	if (yn) {
		history_changed ();
		_history.show ();
	} else {
		_history.hide ();
	}
}

void
MCRPage::history_changed ()
{
	if (_history.get_visible ()) {
		_history.set_history (_canvas.patch ().history_title (), _canvas.patch ().history ());
	}
}

void
MCRPage::open_slivers ()
{
	_canvas.patch ().open_all_slivers ();
}

void
MCRPage::close_slivers ()
{
	_canvas.patch ().close_all_slivers ();
}

void
MCRPage::open_slivers_with_cords ()
{
	_canvas.patch ().open_slivers_with_cords ();
}

/* the selected cable's ends', or with no cable selected, the selected tracks' */
void
MCRPage::open_slivers_around_selected ()
{
	std::vector<uint64_t> rows = _canvas.patch ().selected_rows ();
	if (rows.empty ()) {
		rows = _canvas.selected_rows ();
	}
	for (auto const& r : rows) {
		_canvas.patch ().open_slivers_around (r);
	}
}

void
MCRPage::set_session (Session* s)
{
	SessionHandlePtr::set_session (s);

	_canvas.set_session (s);

	if (!_session) {
		return;
	}

	_application_bar.set_session (_session);

	_session->DirtyChanged.connect (_session_connections, invalidator (*this), std::bind (&MCRPage::update_title, this), gui_context ());
	_session->StateSaved.connect (_session_connections, invalidator (*this), std::bind (&MCRPage::update_title, this), gui_context ());

	set_state (*ARDOUR_UI::instance ()->mcr_page_settings (), Stateful::loading_state_version);
	update_title ();
}

void
MCRPage::focus_on_clock ()
{
	_application_bar.focus_on_clock ();
}

XMLNode&
MCRPage::get_state () const
{
	XMLNode* node = new XMLNode (X_("MCRPage"));
	node->add_child_nocopy (Tabbable::get_state ()); /* tabbed or in its own window */
	node->set_property (X_("play-mode"), _play_action->get_active ());
	node->set_property (X_("detective"), _detective_action->get_active ());
	node->set_property (X_("detective-speed"), _canvas.patch ().detective_speed ());
	node->set_property (X_("history"), _history_action->get_active ());

	/* the slivers open, and how tall (the session's tracks' ids name them) */
	XMLNode* slivers = new XMLNode (X_("Slivers"));
	for (auto const& s : _canvas.patch ().sliver_states ()) {
		XMLNode* n = new XMLNode (X_("Sliver"));
		n->set_property (X_("row"), s.row);
		n->set_property (X_("open"), s.open);
		n->set_property (X_("pinned"), s.pinned);
		n->set_property (X_("height"), s.height);
		slivers->add_child_nocopy (*n);
	}
	node->add_child_nocopy (*slivers);
	return *node;
}

int
MCRPage::set_state (const XMLNode& node, int version)
{
	bool yn;
	if (node.get_property (X_("play-mode"), yn)) {
		_play_action->set_active (yn);
	}

	if (!node.get_property (X_("detective"), yn)) {
		yn = false;
	}
	_pause_action->set_active (false);
	_detective_action->set_active (yn);

	float speed;
	if (!node.get_property (X_("detective-speed"), speed)) {
		speed = 1.f;
	}
	speed_changed (std::max (McrBar::min_speed (), std::min (McrBar::max_speed (), speed)));

	if (!node.get_property (X_("history"), yn)) {
		yn = false;
	}
	_history_action->set_active (yn);

	/* after the mode, which opens or closes slivers itself */
	std::vector<McrPatch::SliverSave> slivers;
	if (XMLNode const* sn = node.child (X_("Slivers"))) {
		for (auto const& c : sn->children (X_("Sliver"))) {
			McrPatch::SliverSave s;
			if (!c->get_property (X_("row"), s.row)) {
				continue;
			}
			c->get_property (X_("open"), s.open);
			c->get_property (X_("pinned"), s.pinned);
			c->get_property (X_("height"), s.height);
			slivers.push_back (s);
		}
	}
	_canvas.patch ().set_sliver_states (slivers);

	/* opens the page in its own window again if it was detached and shown */
	return Tabbable::set_state (node, version);
}
