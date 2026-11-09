/*
 * Copyright (C) 2022 Ben Loftis <ben@harrisonconsoles.com>
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

#include <cstdlib>

#include <ytkmm/scrolledwindow.h>
#include <ytkmm/stock.h>
#include <ytkmm/table.h>

#include "gtkmm2ext/utils.h"

#include "pbd/compose.h"

#include "widgets/ardour_button.h"

#include "ardour/triggerbox.h"

#include "trigger_jump_dialog.h"
#include "ui_config.h"

#include "pbd/i18n.h"

using namespace std;
using namespace Gtk;
using namespace Gtkmm2ext;
using namespace ARDOUR;
using namespace PBD;
using namespace ArdourWidgets;

/**
 *    EditNoteDialog constructor.
 *
 *    @param n Notes to edit.
 */

TriggerJumpDialog::TriggerJumpDialog (bool right)
	: ArdourDialog ("")
	, _right_fa(right)
	, _use_runs (false)
{
//	add_button (Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
//	add_button (Gtk::Stock::APPLY, Gtk::RESPONSE_ACCEPT);
	set_default_response (Gtk::RESPONSE_ACCEPT);
}

void
TriggerJumpDialog::on_trigger_set ()
{
	_table.set_border_width (4);
	_table.set_spacings (4);

	int r = 0;

	int const n_scenes = tref.box () ? (int) triggerbox ().n_triggers () : TriggerBox::default_triggers_per_box; /* Spite */

	if (n_scenes > 64) {
		/* Spite: a list of scene numbers and runs, as many scenes as there are */
		_use_runs = true;
		Gtk::Label* l = manage (new Gtk::Label (string_compose (_("Scenes to jump to (1 to %1),\nfor example: 3, 10-40, 512"), n_scenes), ALIGN_START));
		get_vbox ()->pack_start (*l, false, false, 4);
		get_vbox ()->pack_start (_runs_entry, false, false, 4);
		get_vbox ()->pack_start (_runs_note, false, false, 0);
		_runs_entry.set_width_chars (32);
		_runs_entry.signal_activate ().connect (sigc::mem_fun (*this, &TriggerJumpDialog::runs_activated));
		_runs_entry.signal_focus_out_event ().connect (sigc::mem_fun (*this, &TriggerJumpDialog::runs_focus_out));
		l->show ();
		_runs_entry.show ();
		_runs_note.show ();
	}

	for (int i = 0; !_use_runs && i < n_scenes; i++) {
		ArdourButton* b = manage (new ArdourButton (ArdourButton::led_default_elements));

		b->signal_clicked.connect(sigc::bind(sigc::mem_fun(*this, &TriggerJumpDialog::button_clicked), i));

		Gtk::Label* l = manage(new Gtk::Label (cue_marker_name(i), ALIGN_END));
		_table.attach (*l, 0, 1, r,r+1, Gtk::FILL,        Gtk::FILL);
		_table.attach (*b, 1, 2, r,r+1, Gtk::FILL|EXPAND, Gtk::FILL);

		_buttonlist.push_back(b);

		++r;
	}

	if (_use_runs) {
		/* no buttons */
	} else if (n_scenes > 16) {
		/* Spite: scenes can number in the hundreds; show 16 rows and scroll */
		Gtk::ScrolledWindow* sw = manage (new Gtk::ScrolledWindow);
		sw->set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
		sw->add (_table);
		sw->set_size_request (-1, rintf (16 * 26 * std::max (1.f, UIConfiguration::instance ().get_ui_scale ()))); /* about 16 rows */
		get_vbox()->pack_start (*sw, true, true, 4);
	} else {
		get_vbox()->pack_start (_table, false, false, 4);
	}

	Gtk::Label *label = manage (new Gtk::Label(_("Enabling multiple slots will result\n"
	                                             "in an equal random probability of\n"
	                                             "the selected slots being launched."), ALIGN_CENTER));
	get_vbox()->pack_start (*label, false, false, 4);

	PropertyChange pc;
	pc.add (Properties::name);
	pc.add (Properties::follow_action0);
	on_trigger_changed(pc);
}

void
TriggerJumpDialog::button_clicked (int b)
{
	TriggerPtr t (writable ());
	if (!t) {
		return;
	}
	FollowAction jump_fa = _right_fa ? t->follow_action1() : t->follow_action0();

	jump_fa.type = FollowAction::JumpTrigger;  //should already be the case if we are in this dialog, but let's take no chances
	jump_fa.targets.flip(b);

	if (_right_fa) {
		t->set_follow_action1(jump_fa);
	} else {
		t->set_follow_action0(jump_fa);
	}
}

void
TriggerJumpDialog::on_trigger_changed (PropertyChange const& what)
{
	std::shared_ptr<Trigger const> const v (view ());
	if (!v) {
		return;
	}
	set_title(string_compose(_("Jump Target for: %1"), v->name()));
	
	TriggerBox &box = triggerbox ();

	FollowAction jump_fa = _right_fa ? v->follow_action1() : v->follow_action0();

	if (_use_runs) {
		if (!_runs_entry.has_focus ()) {
			_runs_entry.set_text (targets_to_runs (jump_fa, box.n_triggers ()));
		}
		return;
	}

	//update button display state
	ButtonList::const_iterator b = _buttonlist.begin ();
	for (int i = 0; i < (int) box.n_triggers (); i++) {

		if (b==_buttonlist.end()) {
			break;
		}

		TriggerPtr const ti (box.trigger (i)); /* Spite: none for an empty slot */
		(*b)->set_text (ti ? ti->name () : std::string ());
		(*b)->set_active_state(jump_fa.targets.test(i) ? Gtkmm2ext::ExplicitActive : Gtkmm2ext::Off);

		++b;
	}
	
}

std::string
TriggerJumpDialog::targets_to_runs (FollowAction const& fa, uint32_t n_scenes)
{
	std::string    out;
	uint32_t const m = std::min<uint32_t> (n_scenes, fa.targets.size ());
	for (uint32_t i = 0; i < m;) {
		if (!fa.targets.test (i)) {
			++i;
			continue;
		}
		uint32_t j = i;
		while (j + 1 < m && fa.targets.test (j + 1)) {
			++j;
		}
		if (!out.empty ()) {
			out += ", ";
		}
		out += (i == j) ? std::to_string (i + 1) : string_compose ("%1-%2", i + 1, j + 1);
		i = j + 1;
	}
	return out;
}

bool
TriggerJumpDialog::runs_to_targets (std::string const& text, uint32_t n_scenes, FollowAction& fa)
{
	FollowAction::Targets t;
	std::string           tok;
	std::string           rest (text + ",");
	for (char c : rest) {
		if (c != ',' && c != ' ' && c != '\t' && c != ';') {
			tok += c;
			continue;
		}
		if (tok.empty ()) {
			continue;
		}
		char*               end = 0;
		unsigned long const a   = strtoul (tok.c_str (), &end, 10);
		unsigned long       b   = a;
		if (end && *end == '-') {
			b = strtoul (end + 1, &end, 10);
		}
		if (!end || *end != '\0' || a < 1 || b < a || b > n_scenes || b > t.size ()) {
			return false;
		}
		for (unsigned long i = a; i <= b; ++i) {
			t.set (i - 1);
		}
		tok.clear ();
	}
	fa.targets = t;
	return true;
}

void
TriggerJumpDialog::runs_activated ()
{
	TriggerPtr t (writable ());
	if (!t) {
		return;
	}
	FollowAction jump_fa = _right_fa ? t->follow_action1 () : t->follow_action0 ();
	if (!runs_to_targets (_runs_entry.get_text (), triggerbox ().n_triggers (), jump_fa)) {
		_runs_note.set_text (_("Scene numbers, and runs like 10-40, separated by commas"));
		return;
	}
	_runs_note.set_text ("");
	jump_fa.type = FollowAction::JumpTrigger;
	if (_right_fa) {
		t->set_follow_action1 (jump_fa);
	} else {
		t->set_follow_action0 (jump_fa);
	}
}

bool
TriggerJumpDialog::runs_focus_out (GdkEventFocus*)
{
	runs_activated ();
	return false;
}

void
TriggerJumpDialog::done (int r)
{
	if (r != RESPONSE_ACCEPT) {
		return;
	}
}
