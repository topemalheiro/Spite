/*
 * Copyright (C) 2026 António Malheiro
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

#include <algorithm>
#include <cerrno>

#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <ytkmm/action.h>
#include <ytkmm/actiongroup.h>
#include <ytkmm/toggleaction.h>
#include <ytkmm/uimanager.h>

#include "pbd/compose.h"
#include "pbd/error.h"
#include "pbd/xml++.h"

#include "ardour/filesystem_paths.h"

#include "gtkmm2ext/actions.h"

#include "macro_manager.h"

#include "pbd/i18n.h"

using namespace std;
using namespace PBD;
using namespace Gtkmm2ext;

static string
mode_name (MacroManager::StepMode m)
{
	switch (m) {
		case MacroManager::TurnOn:
			return X_("on");
		case MacroManager::TurnOff:
			return X_("off");
		default:
			return X_("run");
	}
}

static string
strip_mnemonic (string const& label)
{
	string out;
	for (string::size_type i = 0; i < label.size (); ++i) {
		if (label[i] == '_') {
			if (i + 1 < label.size () && label[i + 1] == '_') {
				out += '_';
				++i;
			}
			continue;
		}
		out += label[i];
	}
	return out;
}

MacroManager&
MacroManager::instance ()
{
	static MacroManager m;
	return m;
}

MacroManager::MacroManager ()
	: _current (0)
	, _shortcuts (n_slots)
	, _depth (0)
	, _recording_slot (-1)
	, _activation_depth (0)
{
}

string
MacroManager::action_path (int slot)
{
	return string_compose (X_("Macros/macro-%1"), slot + 1);
}

string
MacroManager::action_label (string const& path)
{
	Glib::RefPtr<Gtk::Action> act = ActionManager::get_action (path, false);
	if (!act) {
		return path;
	}
	return strip_mnemonic (act->get_label ());
}

bool
MacroManager::is_toggle (string const& path)
{
	return (bool) ActionManager::get_toggle_action (path, false);
}

bool
MacroManager::exists (string const& path)
{
	return (bool) ActionManager::get_action (path, false);
}

string
MacroManager::filename () const
{
	return Glib::build_filename (ARDOUR::user_config_directory (), X_("macros.xml"));
}

void
MacroManager::register_actions ()
{
	_group = ActionManager::create_action_group (Bindings::get_bindings (X_("Global")), X_("Macros"));

	for (int i = 0; i < n_slots; ++i) {
		ActionManager::register_action (_group, string_compose (X_("macro-%1"), i + 1).c_str (), string_compose (_("Macro %1"), i + 1).c_str (),
		                                sigc::bind (sigc::mem_fun (*this, &MacroManager::run), i));
	}

	/* insensitive menu item that names the macro set in use */
	ActionManager::register_action (_group, X_("current-set"), _("Macro Set"));
	ActionManager::register_action (_group, X_("previous-set"), _("Previous Macro Set"), sigc::mem_fun (*this, &MacroManager::previous_set));
	ActionManager::register_action (_group, X_("next-set"), _("Next Macro Set"), sigc::mem_fun (*this, &MacroManager::next_set));

	ActionManager::ui_manager->signal_connect_proxy ().connect (sigc::mem_fun (*this, &MacroManager::proxy_connected));
}

void
MacroManager::proxy_connected (Glib::RefPtr<Gtk::Action> const& act, Gtk::Widget* widget)
{
	/* the menubar is shown with show_all (), which would also show the hidden empty slots */
	if (act->get_accel_path ().find (X_("<Actions>/Macros/macro-")) == 0) {
		widget->set_no_show_all (true);
		widget->set_visible (act->get_visible ());
	}
}

void
MacroManager::load ()
{
	_sets.clear ();
	_current = 0;
	_shortcuts.assign (n_slots, string ());

	string const path = filename ();
	XMLTree tree;
	string current_name;

	if (Glib::file_test (path, Glib::FILE_TEST_EXISTS) && tree.read (path) && tree.root ()) {
		XMLNode const& root (*tree.root ());
		root.get_property (X_("current-set"), current_name);

		for (XMLNodeConstIterator c = root.children ().begin (); c != root.children ().end (); ++c) {
			XMLNode const& node (**c);
			int slot;

			if (node.name () == X_("Shortcut")) {
				string key;
				if (node.get_property (X_("slot"), slot) && node.get_property (X_("key"), key) && slot >= 1 && slot <= n_slots) {
					_shortcuts[slot - 1] = key;
				}
				continue;
			}

			if (node.name () != X_("MacroSet")) {
				continue;
			}

			MacroSet set;
			set.macros.resize (n_slots);
			if (!node.get_property (X_("name"), set.name) || set.name.empty () || find_set (set.name)) {
				continue;
			}

			for (XMLNodeConstIterator m = node.children ().begin (); m != node.children ().end (); ++m) {
				if ((*m)->name () != X_("Macro") || !(*m)->get_property (X_("slot"), slot) || slot < 1 || slot > n_slots) {
					continue;
				}
				Macro& macro (set.macros[slot - 1]);
				(*m)->get_property (X_("name"), macro.name);

				for (XMLNodeConstIterator s = (*m)->children ().begin (); s != (*m)->children ().end (); ++s) {
					string action;
					string mode;
					if ((*s)->name () != X_("Step") || !(*s)->get_property (X_("action"), action) || action.empty ()) {
						continue;
					}
					(*s)->get_property (X_("mode"), mode);
					macro.steps.push_back (Step (action, mode == X_("on") ? TurnOn : (mode == X_("off") ? TurnOff : Run)));
				}
			}

			_sets.push_back (set);
		}
	}

	if (_sets.empty ()) {
		seed_starter_set ();
	}

	for (size_t n = 0; n < _sets.size (); ++n) {
		if (_sets[n].name == current_name) {
			_current = n;
		}
	}

	update_actions ();
	apply_shortcuts ();
}

void
MacroManager::seed_starter_set ()
{
	MacroSet set;
	set.name = _("Starter");
	set.macros.resize (n_slots);

	Macro& clean (set.macros[0]);
	clean.name = _("Cue Page Without Panels");
	clean.steps.push_back (Step (X_("Common/show-trigger"), Run));
	clean.steps.push_back (Step (X_("Cues/ToggleTriggerList"), TurnOff));
	clean.steps.push_back (Step (X_("Cues/ToggleTriggerProps"), TurnOff));

	Macro& browse (set.macros[1]);
	browse.name = _("Cue Page With Sidebar");
	browse.steps.push_back (Step (X_("Common/show-trigger"), Run));
	browse.steps.push_back (Step (X_("Cues/ToggleTriggerList"), TurnOn));
	browse.steps.push_back (Step (X_("Cues/ToggleTriggerProps"), TurnOn));

	Macro& edit (set.macros[2]);
	edit.name = _("Editor With Mixer Strip");
	edit.steps.push_back (Step (X_("Common/show-editor"), Run));
	edit.steps.push_back (Step (X_("Editor/show-editor-mixer"), TurnOn));
	edit.steps.push_back (Step (X_("Editor/show-editor-list"), TurnOff));

	_sets.push_back (set);
	_current = 0;
}

MacroManager::MacroSet&
MacroManager::current ()
{
	return _sets[_current];
}

MacroManager::MacroSet const&
MacroManager::current () const
{
	return _sets[_current];
}

MacroManager::MacroSet*
MacroManager::find_set (string const& name)
{
	for (vector<MacroSet>::iterator s = _sets.begin (); s != _sets.end (); ++s) {
		if (s->name == name) {
			return &(*s);
		}
	}
	return 0;
}

vector<string>
MacroManager::set_names () const
{
	vector<string> names;
	for (vector<MacroSet>::const_iterator s = _sets.begin (); s != _sets.end (); ++s) {
		names.push_back (s->name);
	}
	return names;
}

string const&
MacroManager::current_set_name () const
{
	return current ().name;
}

bool
MacroManager::use_set (string const& name)
{
	for (size_t n = 0; n < _sets.size (); ++n) {
		if (_sets[n].name == name) {
			if (n != _current) {
				_current = n;
				changed ();
			}
			return true;
		}
	}
	return false;
}

bool
MacroManager::add_set (string const& name, bool copy_current)
{
	if (name.empty () || find_set (name)) {
		return false;
	}
	MacroSet set;
	if (copy_current) {
		set = current ();
	} else {
		set.macros.resize (n_slots);
	}
	set.name = name;
	_sets.push_back (set);
	_current = _sets.size () - 1;
	changed ();
	return true;
}

bool
MacroManager::rename_set (string const& name)
{
	if (name.empty () || find_set (name)) {
		return false;
	}
	current ().name = name;
	changed ();
	return true;
}

bool
MacroManager::remove_set ()
{
	if (_sets.size () < 2) {
		return false;
	}
	_sets.erase (_sets.begin () + _current);
	_current = min (_current, _sets.size () - 1);
	changed ();
	return true;
}

void
MacroManager::next_set ()
{
	_current = (_current + 1) % _sets.size ();
	changed ();
}

void
MacroManager::previous_set ()
{
	_current = (_current + _sets.size () - 1) % _sets.size ();
	changed ();
}

MacroManager::Macro const&
MacroManager::macro (int slot) const
{
	return current ().macros[slot];
}

void
MacroManager::set_macro (int slot, Macro const& m)
{
	if (slot < 0 || slot >= n_slots) {
		return;
	}
	current ().macros[slot] = m;
	changed ();
}

void
MacroManager::changed ()
{
	update_actions ();
	save ();
	Changed (); /* EMIT SIGNAL */
}

void
MacroManager::update_actions ()
{
	for (int i = 0; i < n_slots; ++i) {
		Glib::RefPtr<Gtk::Action> act = ActionManager::get_action (action_path (i), false);
		if (!act) {
			continue;
		}
		Macro const& m (macro (i));
		bool const set = !m.steps.empty ();
		act->set_label (m.name.empty () ? string_compose (_("Macro %1"), i + 1) : m.name);
		act->set_sensitive (set);
		/* empty slots stay out of the Macros menu */
		act->set_visible (set);
	}

	Glib::RefPtr<Gtk::Action> act = ActionManager::get_action (X_("Macros/current-set"), false);
	if (act) {
		act->set_label (string_compose (_("Macro Set: %1"), current ().name));
		act->set_sensitive (false);
	}
}

void
MacroManager::apply_shortcuts ()
{
	Bindings* global = Bindings::get_bindings (X_("Global"));
	if (!global) {
		return;
	}

	for (int i = 0; i < n_slots; ++i) {
		string const path = action_path (i);
		KeyboardKey key;

		if (_shortcuts[i].empty () || !KeyboardKey::make_key (_shortcuts[i], key)) {
			global->remove (Bindings::Press, path, false);
			continue;
		}

		string const conflict = key_conflict (key, i);
		if (!conflict.empty ()) {
			warning << string_compose (_("Macro %1 keeps no shortcut: %2"), i + 1, conflict) << endmsg;
			global->remove (Bindings::Press, path, false);
			continue;
		}

		global->replace (key, Bindings::Press, path, false);
	}
}

KeyboardKey
MacroManager::shortcut (int slot) const
{
	KeyboardKey key;
	if (slot < 0 || slot >= n_slots || _shortcuts[slot].empty () || !KeyboardKey::make_key (_shortcuts[slot], key)) {
		return KeyboardKey::null_key ();
	}
	return key;
}

string
MacroManager::key_conflict (KeyboardKey const& key, int slot) const
{
	string const mine = action_path (slot);

	for (list<Bindings*>::const_iterator b = Bindings::bindings.begin (); b != Bindings::bindings.end (); ++b) {
		string path;
		if ((*b)->is_bound (key, Bindings::Press, &path) && path != mine) {
			return string_compose (_("%1 is already the shortcut for \"%2\" (%3)"), key.display_label (), action_label (path), (*b)->name ());
		}
	}
	return string ();
}

void
MacroManager::set_shortcut (int slot, KeyboardKey const& key)
{
	Bindings* global = Bindings::get_bindings (X_("Global"));
	if (!global || slot < 0 || slot >= n_slots || !key_conflict (key, slot).empty ()) {
		return;
	}
	global->replace (key, Bindings::Press, action_path (slot), false);
	_shortcuts[slot] = key.name ();
	changed ();
}

void
MacroManager::clear_shortcut (int slot)
{
	Bindings* global = Bindings::get_bindings (X_("Global"));
	if (!global || slot < 0 || slot >= n_slots) {
		return;
	}
	global->remove (Bindings::Press, action_path (slot), false);
	_shortcuts[slot].clear ();
	changed ();
}

void
MacroManager::run (int slot)
{
	if (slot < 0 || slot >= n_slots) {
		return;
	}

	/* a macro may run other macros; stop runaway loops */
	if (_depth >= 8) {
		warning << _("Macros call each other too deeply; stopped") << endmsg;
		return;
	}

	/* a copy, because a step may switch macro sets */
	Macro const m = macro (slot);

	++_depth;

	for (vector<Step>::const_iterator s = m.steps.begin (); s != m.steps.end (); ++s) {
		Glib::RefPtr<Gtk::Action> act = ActionManager::get_action (s->action, false);

		if (!act) {
			warning << string_compose (_("Macro \"%1\": the action %2 isn't available"), m.name, s->action) << endmsg;
			continue;
		}

		Glib::RefPtr<Gtk::ToggleAction> tact = Glib::RefPtr<Gtk::ToggleAction>::cast_dynamic (act);

		if (tact && s->mode != Run) {
			tact->set_active (s->mode == TurnOn);
		} else {
			act->activate ();
		}
	}

	--_depth;
}

void
MacroManager::start_recording (int slot)
{
	stop_recording ();
	if (slot < 0 || slot >= n_slots) {
		return;
	}
	_recording_slot = slot;
	_activation_depth = 0;
	_pre_activate_connection = ActionManager::ui_manager->signal_pre_activate ().connect (sigc::mem_fun (*this, &MacroManager::activating));
	_post_activate_connection = ActionManager::ui_manager->signal_post_activate ().connect (sigc::mem_fun (*this, &MacroManager::recorded));
	Changed (); /* EMIT SIGNAL */
}

void
MacroManager::stop_recording ()
{
	if (_recording_slot < 0) {
		return;
	}
	_pre_activate_connection.disconnect ();
	_post_activate_connection.disconnect ();
	_recording_slot = -1;
	Changed (); /* EMIT SIGNAL */
}

void
MacroManager::activating (Glib::RefPtr<Gtk::Action> const&)
{
	++_activation_depth;
}

void
MacroManager::recorded (Glib::RefPtr<Gtk::Action> const& act)
{
	/* record only the action the user triggered, not the actions it runs in turn */
	_activation_depth = max (0, _activation_depth - 1);
	if (_activation_depth > 0) {
		return;
	}

	/* steps run by a macro are not recorded one by one */
	if (_recording_slot < 0 || _depth > 0) {
		return;
	}

	string const accel = act->get_accel_path ();
	string const prefix = X_("<Actions>/");

	if (accel.compare (0, prefix.size (), prefix) != 0) {
		return;
	}

	string const path = accel.substr (prefix.size ());
	string::size_type const slash = path.find ('/');

	if (slash == string::npos) {
		return;
	}

	string const group = path.substr (0, slash);
	string const name = path.substr (slash + 1);

	/* menu headers and the editor's own window toggle are not steps */
	if (group == X_("Main Menu") || (name.size () > 4 && name.compare (name.size () - 4, 4, X_("Menu")) == 0) ||
	    path == X_("Window/toggle-macro-editor") || path == action_path (_recording_slot)) {
		return;
	}

	Glib::RefPtr<Gtk::ToggleAction> tact = Glib::RefPtr<Gtk::ToggleAction>::cast_dynamic (act);
	StepMode const mode = tact ? (tact->get_active () ? TurnOn : TurnOff) : Run;

	Macro m = macro (_recording_slot);
	if (m.name.empty ()) {
		m.name = string_compose (_("Macro %1"), _recording_slot + 1);
	}
	m.steps.push_back (Step (path, mode));
	set_macro (_recording_slot, m);
}

void
MacroManager::save () const
{
	XMLNode* root = new XMLNode (X_("Macros"));
	root->set_property (X_("version"), 1);
	root->set_property (X_("current-set"), current ().name);

	for (int i = 0; i < n_slots; ++i) {
		if (_shortcuts[i].empty ()) {
			continue;
		}
		XMLNode* node = root->add_child (X_("Shortcut"));
		node->set_property (X_("slot"), i + 1);
		node->set_property (X_("key"), _shortcuts[i]);
	}

	for (vector<MacroSet>::const_iterator s = _sets.begin (); s != _sets.end (); ++s) {
		XMLNode* set_node = root->add_child (X_("MacroSet"));
		set_node->set_property (X_("name"), s->name);

		for (int i = 0; i < n_slots; ++i) {
			Macro const& m (s->macros[i]);
			if (m.empty ()) {
				continue;
			}
			XMLNode* macro_node = set_node->add_child (X_("Macro"));
			macro_node->set_property (X_("slot"), i + 1);
			macro_node->set_property (X_("name"), m.name);

			for (vector<Step>::const_iterator st = m.steps.begin (); st != m.steps.end (); ++st) {
				XMLNode* step_node = macro_node->add_child (X_("Step"));
				step_node->set_property (X_("action"), st->action);
				step_node->set_property (X_("mode"), mode_name (st->mode));
			}
		}
	}

	XMLTree tree;
	tree.set_root (root);

	string const path = filename ();
	string const tmp = path + X_(".tmp");

	if (!tree.write (tmp)) {
		error << string_compose (_("Could not save macros to %1"), tmp) << endmsg;
		g_remove (tmp.c_str ());
		return;
	}

	if (g_rename (tmp.c_str (), path.c_str ()) != 0) {
		error << string_compose (_("Could not save macros to %1 (%2)"), path, g_strerror (errno)) << endmsg;
		g_remove (tmp.c_str ());
	}
}
