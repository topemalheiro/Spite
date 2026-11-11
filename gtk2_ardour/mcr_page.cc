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

#include "pbd/xml++.h"

#include "ardour/session.h"

#include "gtkmm2ext/actions.h"
#include "gtkmm2ext/bindings.h"

#include "widgets/tooltips.h"

#include "actions.h"
#include "ardour_ui.h"
#include "mcr_page.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ArdourWidgets;
using namespace Gtkmm2ext;
using namespace Gtk;

MCRPage::MCRPage ()
	: Tabbable (_("Modular Channels Rack"), X_("mcr"), NULL, true, Tabbable::NoPanes)
	, bindings (0)
	, _patch_button (_("Patch"))
	, _play_button (_("Play"))
{
	load_bindings ();
	register_actions ();

	/* Patch or Play, as plugdata's edit and run modes */
	_patch_button.set_name (X_("generic button"));
	_play_button.set_name (X_("generic button"));
	_patch_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MCRPage::set_play_mode), false));
	_play_button.signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &MCRPage::set_play_mode), true));
	set_tooltip (_patch_button, _("Patch mode: connect devices with cables"));
	set_tooltip (_play_button, _("Play mode: play the devices"));

	_toolbar.set_spacing (2);
	_toolbar.set_border_width (4);
	_toolbar.pack_start (_patch_button, false, false);
	_toolbar.pack_start (_play_button, false, false);

	/* the canvas scrolls itself (a Gtk::Layout): no viewport around it */
	_scroller.set_policy (POLICY_AUTOMATIC, POLICY_AUTOMATIC);
	_scroller.add (_canvas);

	content_app_bar.add (_application_bar);
	content_main_top.add (_toolbar);
	content_main.add (_scroller);

	_toolbar.show_all ();
	_canvas.show ();
	_scroller.show ();

	play_mode_toggled (); /* the buttons show the mode */

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
}

void
MCRPage::register_actions ()
{
	Glib::RefPtr<ActionGroup> group = ActionManager::create_action_group (bindings, X_("MCR"));

	_play_action = ActionManager::register_toggle_action (group, X_("play-mode"), _("MCR: Play Mode"), sigc::mem_fun (*this, &MCRPage::play_mode_toggled));
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
	_patch_button.set_active (!play);
	_play_button.set_active (play);
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

	set_state (*ARDOUR_UI::instance ()->mcr_page_settings (), Stateful::loading_state_version);
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
	node->set_property (X_("play-mode"), _play_action->get_active ());
	return *node;
}

int
MCRPage::set_state (const XMLNode& node, int /* version */)
{
	/* not Tabbable::set_state (): it would open the page in a window of its own */
	bool play;
	if (node.get_property (X_("play-mode"), play)) {
		_play_action->set_active (play);
	}
	return 0;
}
