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
#include <cstdlib>

#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <ytkmm/action.h>
#include <ytkmm/actiongroup.h>
#include <ytkmm/toggleaction.h>
#include <ytkmm/uimanager.h>

#include "pbd/compose.h"
#include "pbd/error.h"
#include "pbd/file_utils.h"
#include "pbd/xml++.h"

#include "ardour/filesystem_paths.h"

#include "gtkmm2ext/actions.h"

#include "ardour_ui.h"
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

static string const macro_prefix = X_("Macros/macro-");

MacroManager&
MacroManager::instance ()
{
	static MacroManager m;
	return m;
}

MacroManager::MacroManager ()
	: _current (0)
	, _registered (0)
	, _merge_id (0)
	, _menus_loaded (false)
	, _depth (0)
	, _recording_slot (-1)
	, _activation_depth (0)
{
}

string
MacroManager::action_path (int slot)
{
	return string_compose (X_("%1%2"), macro_prefix, slot + 1);
}

int
MacroManager::slot_of (string const& path)
{
	if (path.compare (0, macro_prefix.size (), macro_prefix) != 0) {
		return -1;
	}
	int const n = atoi (path.substr (macro_prefix.size ()).c_str ());
	return n >= 1 ? n - 1 : -1;
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

vector<pair<string, string> >
MacroManager::scopes ()
{
	vector<pair<string, string> > s;
	s.push_back (make_pair (string (X_("Global")), string (_("Everywhere"))));
	s.push_back (make_pair (string (X_("Editor")), string (_("Editor"))));
	s.push_back (make_pair (string (X_("Mixer")), string (_("Mixer"))));
	s.push_back (make_pair (string (X_("Cues")), string (_("Cues"))));
	s.push_back (make_pair (string (X_("Recorder")), string (_("Recorder"))));
	s.push_back (make_pair (string (X_("MCR")), string (_("Modular Channels Rack"))));
	return s;
}

string
MacroManager::scope_label (string const& set)
{
	vector<pair<string, string> > const s = scopes ();
	for (vector<pair<string, string> >::const_iterator i = s.begin (); i != s.end (); ++i) {
		if (i->first == set) {
			return i->second;
		}
	}
	return set;
}

string
MacroManager::set_label (string const& set)
{
	for (list<Bindings*>::const_iterator b = Bindings::bindings.begin (); b != Bindings::bindings.end (); ++b) {
		if ((*b)->parent () && (*b)->parent ()->name () == set) {
			return string_compose (_("%1, every editor"), scope_label (set));
		}
	}
	return scope_label (set);
}

string
MacroManager::binding_label (Bindings const* set, string const& action)
{
	if (exists (action)) {
		return action_label (action);
	}
	/* a template's names are relative: each editor's copy prefixes its own name */
	for (list<Bindings*>::const_iterator b = Bindings::bindings.begin (); b != Bindings::bindings.end (); ++b) {
		if ((*b)->parent () == set && exists ((*b)->name () + action)) {
			return action_label ((*b)->name () + action);
		}
	}
	return string ();
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

	ensure_actions (min_actions);

	/* insensitive menu item that names the macro set in use */
	ActionManager::register_action (_group, X_("current-set"), _("Macro Set"));
	ActionManager::register_action (_group, X_("previous-set"), _("Previous Macro Set"), sigc::mem_fun (*this, &MacroManager::previous_set));
	ActionManager::register_action (_group, X_("next-set"), _("Next Macro Set"), sigc::mem_fun (*this, &MacroManager::next_set));

	ActionManager::ui_manager->signal_connect_proxy ().connect (sigc::mem_fun (*this, &MacroManager::proxy_connected));
}

void
MacroManager::ensure_actions (int count)
{
	if (!_group) {
		return;
	}
	count = min (count, (int) max_macros);
	for (; _registered < count; ++_registered) {
		ActionManager::register_action (_group, string_compose (X_("macro-%1"), _registered + 1).c_str (), string_compose (_("Macro %1"), _registered + 1).c_str (),
		                                sigc::bind (sigc::mem_fun (*this, &MacroManager::run), _registered));
	}
}

void
MacroManager::proxy_connected (Glib::RefPtr<Gtk::Action> const& act, Gtk::Widget* widget)
{
	/* the menubar is shown with show_all (), which would also show hidden macros */
	if (act->get_accel_path ().find (X_("<Actions>/") + macro_prefix) == 0) {
		widget->set_no_show_all (true);
		widget->set_visible (act->get_visible ());
	}
}

void
MacroManager::load ()
{
	_sets.clear ();
	_current = 0;
	_shortcuts.clear ();
	_key_changes.clear ();
	_cycle_position.clear ();

	string const path = filename ();
	XMLTree tree;
	string current_name;
	int version = 1;
	bool from_file = false;

	if (Glib::file_test (path, Glib::FILE_TEST_EXISTS) && tree.read (path) && tree.root ()) {
		XMLNode const& root (*tree.root ());
		root.get_property (X_("current-set"), current_name);
		root.get_property (X_("version"), version);
		from_file = true;

		for (XMLNodeConstIterator c = root.children ().begin (); c != root.children ().end (); ++c) {
			XMLNode const& node (**c);
			int slot;

			if (node.name () == X_("Shortcut")) {
				Binding b;
				if (!node.get_property (X_("slot"), slot) || slot < 1 || slot > max_macros) {
					continue;
				}
				node.get_property (X_("key"), b.key);
				if (!node.get_property (X_("scope"), b.scope) || b.scope.empty ()) {
					b.scope = X_("Global");
				}
				if (b.scope == X_("Editing")) {
					/* a template copied into every editor, not the Editor page */
					b.scope = X_("Editor");
				}
				if ((int) _shortcuts.size () < slot) {
					_shortcuts.resize (slot);
				}
				_shortcuts[slot - 1] = b;
				continue;
			}

			if (node.name () == X_("KeyChange")) {
				KeyChange kc;
				if (node.get_property (X_("set"), kc.set) && node.get_property (X_("action"), kc.action) && !kc.set.empty () && !kc.action.empty ()) {
					node.get_property (X_("from"), kc.from);
					node.get_property (X_("to"), kc.to);
					if (!kc.from.empty () && kc.from != kc.to) {
						_key_changes.push_back (kc);
					}
				}
				continue;
			}

			if (node.name () != X_("MacroSet")) {
				continue;
			}

			MacroSet set;
			if (!node.get_property (X_("name"), set.name) || set.name.empty () || find_set (set.name)) {
				continue;
			}

			for (XMLNodeConstIterator m = node.children ().begin (); m != node.children ().end (); ++m) {
				if ((*m)->name () != X_("Macro") || !(*m)->get_property (X_("slot"), slot) || slot < 1 || slot > max_macros) {
					continue;
				}
				if ((int) set.macros.size () < slot) {
					set.macros.resize (slot);
				}
				Macro& macro (set.macros[slot - 1]);
				(*m)->get_property (X_("name"), macro.name);
				(*m)->get_property (X_("cycle"), macro.cycle);

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

	/* files from before version 3 get the Cue page's Clip/Device switch
	 * once, in the first number of the current set that is free and has
	 * no key
	 */
	bool migrated = false;
	if (from_file && version < 3 && !has_clip_device_macro ()) {
		vector<Macro> const& macros (current ().macros);
		int slot = 0;
		while (slot < max_macros && ((slot < (int) macros.size () && !macros[slot].empty ()) || (slot < (int) _shortcuts.size () && !_shortcuts[slot].key.empty ()))) {
			++slot;
		}
		if (slot < max_macros) {
			add_clip_device_macro (slot);
			migrated = true;
		}
	}

	apply_key_changes ();
	update_actions ();
	apply_shortcuts ();
	update_menu ();

	if (migrated) {
		/* keep the file as it was, once */
		string const backup = path + X_(".v2.bak");
		if (!Glib::file_test (backup, Glib::FILE_TEST_EXISTS) && !PBD::copy_file (path, backup)) {
			warning << string_compose (_("Could not keep a copy of %1 as %2"), path, backup) << endmsg;
		}
		save ();
	}
}

MacroManager::Macro
MacroManager::clip_device_macro ()
{
	Macro m;
	m.name  = _("Clip and Device");
	m.cycle = true;
	m.steps.push_back (Step (X_("Cues/show-device-view"), TurnOn));
	m.steps.push_back (Step (X_("Cues/show-clip-view"), TurnOn));
	return m;
}

bool
MacroManager::has_clip_device_macro () const
{
	for (vector<MacroSet>::const_iterator s = _sets.begin (); s != _sets.end (); ++s) {
		for (vector<Macro>::const_iterator m = s->macros.begin (); m != s->macros.end (); ++m) {
			for (vector<Step>::const_iterator st = m->steps.begin (); st != m->steps.end (); ++st) {
				if (st->action == X_("Cues/show-device-view") || st->action == X_("Cues/toggle-clip-device-view")) {
					return true;
				}
			}
		}
	}
	return false;
}

void
MacroManager::add_clip_device_macro (int slot)
{
	if (slot < 0 || slot >= max_macros) {
		return;
	}

	vector<Macro>& macros (current ().macros);
	if ((int) macros.size () <= slot) {
		macros.resize (slot + 1);
	}
	macros[slot] = clip_device_macro ();

	/* Tab, on the Cue page only, unless something there already has it */
	KeyboardKey tab;
	if (!KeyboardKey::make_key (X_("Tab"), tab)) {
		return;
	}

	bool taken = !key_holder (tab, X_("Cues"), action_path (slot)).empty ();
	for (size_t i = 0; i < _shortcuts.size (); ++i) {
		if ((int) i != slot && _shortcuts[i].key == tab.name () && _shortcuts[i].scope == X_("Cues")) {
			taken = true;
		}
	}

	if (taken) {
		warning << string_compose (_("Macro %1 (\"%2\") gets no key: Tab is already used on the Cue page"), slot + 1, macros[slot].name) << endmsg;
		return;
	}

	if ((int) _shortcuts.size () <= slot) {
		_shortcuts.resize (slot + 1);
	}
	_shortcuts[slot].key   = tab.name ();
	_shortcuts[slot].scope = X_("Cues");
}

void
MacroManager::menus_loaded ()
{
	_menus_loaded = true;
	update_menu ();
}

void
MacroManager::seed_starter_set ()
{
	MacroSet set;
	set.name = _("Starter");
	set.macros.resize (4);

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

	Macro& cycle (set.macros[3]);
	cycle.name = _("Cycle Cues and Editor");
	cycle.cycle = true;
	cycle.steps.push_back (Step (X_("Common/show-trigger"), Run));
	cycle.steps.push_back (Step (X_("Common/show-editor"), Run));

	_sets.push_back (set);
	_current = 0;

	/* macro 5: Tab switches the Cue page between Clip and Device view */
	add_clip_device_macro (4);
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

int
MacroManager::slot_count () const
{
	return current ().macros.size ();
}

MacroManager::Macro const&
MacroManager::macro (int slot) const
{
	static Macro const none;
	if (slot < 0 || slot >= (int) current ().macros.size ()) {
		return none;
	}
	return current ().macros[slot];
}

void
MacroManager::set_macro (int slot, Macro const& m)
{
	if (slot < 0 || slot >= max_macros) {
		return;
	}
	if ((int) current ().macros.size () <= slot) {
		current ().macros.resize (slot + 1);
	}
	current ().macros[slot] = m;
	changed ();
}

int
MacroManager::new_macro (string const& name, vector<Step> const& steps)
{
	vector<Macro>& macros (current ().macros);
	int slot = 0;
	while (slot < (int) macros.size () && !macros[slot].empty ()) {
		++slot;
	}
	if (slot >= max_macros) {
		return -1;
	}
	Macro m;
	m.name  = name.empty () ? string_compose (_("Macro %1"), slot + 1) : name;
	m.steps = steps;
	set_macro (slot, m);
	return slot;
}

void
MacroManager::delete_macro (int slot)
{
	if (slot < 0 || slot >= (int) current ().macros.size ()) {
		return;
	}
	if (_recording_slot == slot) {
		stop_recording ();
	}
	current ().macros[slot] = Macro ();

	/* keys belong to macro numbers, shared by every set: keep the key
	 * while another set still has a macro with this number
	 */
	bool used = false;
	for (vector<MacroSet>::const_iterator s = _sets.begin (); s != _sets.end (); ++s) {
		if (slot < (int) s->macros.size () && !s->macros[slot].empty ()) {
			used = true;
		}
	}
	if (!used && slot < (int) _shortcuts.size ()) {
		_shortcuts[slot] = Binding ();
		apply_shortcuts ();
	}
	changed ();
}

void
MacroManager::changed ()
{
	/* trim empty macros at the end of each set, and unused keys */
	for (vector<MacroSet>::iterator s = _sets.begin (); s != _sets.end (); ++s) {
		while (!s->macros.empty () && s->macros.back ().empty ()) {
			s->macros.pop_back ();
		}
	}
	while (!_shortcuts.empty () && _shortcuts.back ().key.empty () && (_shortcuts.back ().scope.empty () || _shortcuts.back ().scope == X_("Global"))) {
		_shortcuts.pop_back ();
	}

	_cycle_position.clear ();
	update_actions ();
	update_menu ();
	save ();
	Changed (); /* EMIT SIGNAL */
}

void
MacroManager::update_actions ()
{
	int needed = max ((int) min_actions, (int) _shortcuts.size ());
	for (vector<MacroSet>::const_iterator s = _sets.begin (); s != _sets.end (); ++s) {
		needed = max (needed, (int) s->macros.size ());
	}
	ensure_actions (needed);

	for (int i = 0; i < _registered; ++i) {
		Glib::RefPtr<Gtk::Action> act = ActionManager::get_action (action_path (i), false);
		if (!act) {
			continue;
		}
		Macro const& m (macro (i));
		bool const set = !m.steps.empty ();
		act->set_label (m.name.empty () ? string_compose (_("Macro %1"), i + 1) : m.name);
		act->set_sensitive (set);
		/* empty macros stay out of menus */
		act->set_visible (set);
	}

	Glib::RefPtr<Gtk::Action> act = ActionManager::get_action (X_("Macros/current-set"), false);
	if (act) {
		act->set_label (string_compose (_("Macro Set: %1"), current ().name));
		act->set_sensitive (false);
	}
}

void
MacroManager::update_menu ()
{
	if (!_menus_loaded || !ActionManager::ui_manager) {
		return;
	}

	if (_merge_id) {
		ActionManager::ui_manager->remove_ui (_merge_id);
	}
	_merge_id = ActionManager::ui_manager->new_merge_id ();

	MacroSet const& set (current ());
	for (size_t i = 0; i < set.macros.size (); ++i) {
		if (set.macros[i].steps.empty ()) {
			continue;
		}
		ActionManager::ui_manager->add_ui (_merge_id, X_("/Main/Macros/MacroList"), string_compose (X_("macro-%1"), i + 1), action_path (i), Gtk::UI_MANAGER_MENUITEM, false);
	}
	ActionManager::ui_manager->ensure_update ();
}

/* ---- keys ---- */

void
MacroManager::apply_shortcuts ()
{
	for (int i = 0; i < _registered; ++i) {
		string const path = action_path (i);

		for (list<Bindings*>::const_iterator b = Bindings::bindings.begin (); b != Bindings::bindings.end (); ++b) {
			if ((*b)->is_registered (Bindings::Press, path)) {
				(*b)->remove (Bindings::Press, path, false);
			}
		}

		if (i >= (int) _shortcuts.size () || _shortcuts[i].key.empty ()) {
			continue;
		}

		KeyboardKey key;
		if (!KeyboardKey::make_key (_shortcuts[i].key, key)) {
			continue;
		}

		Bindings* target = Bindings::get_bindings (_shortcuts[i].scope);
		if (!target) {
			warning << string_compose (_("Macro %1 keeps no shortcut: there are no \"%2\" key bindings"), i + 1, _shortcuts[i].scope) << endmsg;
			continue;
		}

		string const holder = key_holder (key, _shortcuts[i].scope, path);
		if (!holder.empty ()) {
			warning << string_compose (_("Macro %1 keeps no shortcut: %2 already runs \"%3\""), i + 1, key.display_label (), action_label (holder)) << endmsg;
			continue;
		}

		target->replace (key, Bindings::Press, path, false);
	}
}

KeyboardKey
MacroManager::shortcut (int slot) const
{
	KeyboardKey key;
	if (slot < 0 || slot >= (int) _shortcuts.size () || _shortcuts[slot].key.empty () || !KeyboardKey::make_key (_shortcuts[slot].key, key)) {
		return KeyboardKey::null_key ();
	}
	return key;
}

string
MacroManager::shortcut_scope (int slot) const
{
	if (slot < 0 || slot >= (int) _shortcuts.size () || _shortcuts[slot].scope.empty ()) {
		return X_("Global");
	}
	return _shortcuts[slot].scope;
}

void
MacroManager::set_shortcut (int slot, KeyboardKey const& key, string const& scope)
{
	if (slot < 0 || slot >= max_macros || !Bindings::get_bindings (scope) || !key_holder (key, scope, action_path (slot)).empty ()) {
		return;
	}
	if ((int) _shortcuts.size () <= slot) {
		_shortcuts.resize (slot + 1);
	}
	_shortcuts[slot].key   = key.name ();
	_shortcuts[slot].scope = scope;
	update_actions ();
	apply_shortcuts ();
	changed ();
}

void
MacroManager::set_shortcut_scope (int slot, string const& scope)
{
	if (slot < 0 || slot >= max_macros || !Bindings::get_bindings (scope)) {
		return;
	}
	KeyboardKey const key = shortcut (slot);
	if (!(key == KeyboardKey::null_key ()) && !key_holder (key, scope, action_path (slot)).empty ()) {
		return;
	}
	if ((int) _shortcuts.size () <= slot) {
		_shortcuts.resize (slot + 1);
	}
	_shortcuts[slot].scope = scope;
	apply_shortcuts ();
	changed ();
}

void
MacroManager::clear_shortcut (int slot)
{
	if (slot < 0 || slot >= (int) _shortcuts.size ()) {
		return;
	}
	_shortcuts[slot].key.clear ();
	apply_shortcuts ();
	changed ();
}

string
MacroManager::key_holder (KeyboardKey const& key, string const& scope, string const& self) const
{
	Bindings* b = Bindings::get_bindings (scope);
	string path;
	if (b && b->is_bound (key, Bindings::Press, &path) && path != self) {
		return path;
	}
	return string ();
}

vector<MacroManager::KeyUse>
MacroManager::key_uses (KeyboardKey const& key, string const& self) const
{
	vector<KeyUse> out;
	for (list<Bindings*>::const_iterator b = Bindings::bindings.begin (); b != Bindings::bindings.end (); ++b) {
		string path;
		/* each editor's copy of a template follows the template */
		if ((*b)->parent () || !(*b)->is_bound (key, Bindings::Press, &path) || path == self) {
			continue;
		}
		KeyUse u;
		u.set    = (*b)->name ();
		u.action = path;
		u.label  = slot_of (path) >= 0 ? action_label (path) : binding_label (*b, path);
		if (u.label.empty ()) {
			u.label = path;
		}
		out.push_back (u);
	}
	return out;
}

void
MacroManager::take_key (KeyboardKey const& key, string const& scope, string const& self)
{
	string const holder = key_holder (key, scope, self);
	if (holder.empty ()) {
		return;
	}
	int const slot = slot_of (holder);
	if (slot >= 0) {
		if (slot < (int) _shortcuts.size ()) {
			_shortcuts[slot].key.clear ();
		}
		apply_shortcuts ();
		changed ();
	} else {
		set_builtin_key (scope, holder, key, KeyboardKey::null_key ());
	}
}

vector<MacroManager::Shortcut>
MacroManager::shortcuts () const
{
	vector<Shortcut> out;
	for (list<Bindings*>::const_iterator b = Bindings::bindings.begin (); b != Bindings::bindings.end (); ++b) {
		/* each editor's copy of a template is shown once, as the template */
		if ((*b)->parent ()) {
			continue;
		}
		string const set = (*b)->name ();
		vector<pair<KeyboardKey, string> > keys;
		(*b)->get_key_bindings (Bindings::Press, keys);
		for (vector<pair<KeyboardKey, string> >::const_iterator k = keys.begin (); k != keys.end (); ++k) {
			if (slot_of (k->second) >= 0) {
				continue;
			}
			string const label = binding_label (*b, k->second);
			if (label.empty ()) {
				continue;
			}
			Shortcut s;
			s.set      = set;
			s.action   = k->second;
			s.key      = k->first;
			s.label    = label;
			s.runnable = exists (k->second);
			string const name = k->first.name ();
			vector<KeyChange>::const_iterator kc = find_key_change (set, k->second, 0, &name);
			if (kc != _key_changes.end ()) {
				s.default_key = kc->from;
			}
			out.push_back (s);
		}
	}

	/* default keys that were taken away stay listed, so they can be reset */
	for (vector<KeyChange>::const_iterator kc = _key_changes.begin (); kc != _key_changes.end (); ++kc) {
		Bindings const* b = Bindings::get_bindings (kc->set);
		string const label = b ? binding_label (b, kc->action) : string ();
		if (kc->to.empty () && !label.empty ()) {
			Shortcut s;
			s.set         = kc->set;
			s.action      = kc->action;
			s.key         = KeyboardKey::null_key ();
			s.default_key = kc->from;
			s.label       = label;
			s.runnable    = exists (kc->action);
			out.push_back (s);
		}
	}
	return out;
}

vector<MacroManager::KeyChange>::iterator
MacroManager::find_key_change (string const& set, string const& action, string const* from, string const* to)
{
	for (vector<KeyChange>::iterator k = _key_changes.begin (); k != _key_changes.end (); ++k) {
		if (k->set == set && k->action == action && (!from || k->from == *from) && (!to || k->to == *to)) {
			return k;
		}
	}
	return _key_changes.end ();
}

vector<MacroManager::KeyChange>::const_iterator
MacroManager::find_key_change (string const& set, string const& action, string const* from, string const* to) const
{
	for (vector<KeyChange>::const_iterator k = _key_changes.begin (); k != _key_changes.end (); ++k) {
		if (k->set == set && k->action == action && (!from || k->from == *from) && (!to || k->to == *to)) {
			return k;
		}
	}
	return _key_changes.end ();
}

void
MacroManager::apply_key_changes ()
{
	for (vector<KeyChange>::const_iterator k = _key_changes.begin (); k != _key_changes.end (); ++k) {
		Bindings* b = Bindings::get_bindings (k->set);
		KeyboardKey from;
		KeyboardKey to;
		string path;
		if (!b) {
			continue;
		}
		if (KeyboardKey::make_key (k->from, from) && b->is_bound (from, Bindings::Press, &path) && path == k->action) {
			b->remove_key (from, Bindings::Press, false);
		}
		if (!k->to.empty () && KeyboardKey::make_key (k->to, to) && !b->add_key (to, Bindings::Press, k->action, false)) {
			warning << string_compose (_("\"%1\" keeps no shortcut: %2 is taken"), action_label (k->action), to.display_label ()) << endmsg;
		}
	}
}

void
MacroManager::set_builtin_key (string const& set, string const& action, KeyboardKey const& old_key, KeyboardKey const& new_key, string const& default_key)
{
	Bindings* b = Bindings::get_bindings (set);
	if (!b) {
		return;
	}
	bool const had = !(old_key == KeyboardKey::null_key ());
	bool const gets = !(new_key == KeyboardKey::null_key ());
	string const old_name = had ? old_key.name () : string ();
	string const new_name = gets ? new_key.name () : string ();

	if (old_name == new_name || (gets && !key_holder (new_key, set, action).empty ())) {
		return;
	}

	if (had) {
		string path;
		if (!b->is_bound (old_key, Bindings::Press, &path) || path != action) {
			return;
		}
		b->remove_key (old_key, Bindings::Press, false);
	}
	if (gets) {
		b->add_key (new_key, Bindings::Press, action, false);
	}

	/* remember what this key was by default, to save it and to reset it */
	vector<KeyChange>::iterator kc = had ? find_key_change (set, action, 0, &old_name) : find_key_change (set, action, &default_key, 0);
	if (kc == _key_changes.end ()) {
		if (had) {
			KeyChange change;
			change.set    = set;
			change.action = action;
			change.from   = old_name;
			change.to     = new_name;
			_key_changes.push_back (change);
		}
	} else {
		kc->to = new_name;
		if (kc->to == kc->from) {
			_key_changes.erase (kc);
		}
	}

	changed ();
}

string
MacroManager::reset_builtin_key (string const& set, string const& action, string const& default_key)
{
	vector<KeyChange>::iterator kc = find_key_change (set, action, &default_key, 0);
	Bindings* b = Bindings::get_bindings (set);
	if (kc == _key_changes.end () || !b) {
		return string ();
	}

	KeyboardKey from;
	if (!KeyboardKey::make_key (kc->from, from)) {
		_key_changes.erase (kc);
		changed ();
		return string ();
	}

	string const holder = key_holder (from, set, action);
	if (!holder.empty ()) {
		return string_compose (_("%1 now runs \"%2\". Give that another key first."), from.display_label (), action_label (holder));
	}

	KeyboardKey to;
	string path;
	if (!kc->to.empty () && KeyboardKey::make_key (kc->to, to) && b->is_bound (to, Bindings::Press, &path) && path == action) {
		b->remove_key (to, Bindings::Press, false);
	}
	b->add_key (from, Bindings::Press, action, false);
	_key_changes.erase (kc);
	changed ();
	return string ();
}

/* ---- running ---- */

bool
MacroManager::changes_nothing (Step const& s) const
{
	Glib::RefPtr<Gtk::Action> act = ActionManager::get_action (s.action, false);
	if (!act || !act->get_sensitive ()) {
		return true;
	}
	Glib::RefPtr<Gtk::ToggleAction> tact = Glib::RefPtr<Gtk::ToggleAction>::cast_dynamic (act);
	if (tact && s.mode != Run) {
		return tact->get_active () == (s.mode == TurnOn);
	}
	return ARDOUR_UI::instance () && ARDOUR_UI::instance ()->shows_focused_page (s.action);
}

void
MacroManager::run_step (Step const& s)
{
	Glib::RefPtr<Gtk::Action> act = ActionManager::get_action (s.action, false);

	if (!act) {
		warning << string_compose (_("Macro step: the action %1 isn't available"), s.action) << endmsg;
		return;
	}

	Glib::RefPtr<Gtk::ToggleAction> tact = Glib::RefPtr<Gtk::ToggleAction>::cast_dynamic (act);

	if (tact && s.mode != Run) {
		tact->set_active (s.mode == TurnOn);
	} else {
		act->activate ();
	}
}

void
MacroManager::run (int slot)
{
	if (slot < 0 || slot >= max_macros) {
		return;
	}

	/* a macro may run other macros; stop runaway loops */
	if (_depth >= 8) {
		warning << _("Macros call each other too deeply; stopped") << endmsg;
		return;
	}

	/* a copy, because a step may switch macro sets */
	Macro const m = macro (slot);
	size_t const n = m.steps.size ();

	if (n == 0) {
		return;
	}

	++_depth;

	if (m.cycle) {
		/* one step per run: the next one that would change something */
		map<int, size_t>::const_iterator p = _cycle_position.find (slot);
		size_t const start = (p == _cycle_position.end ()) ? 0 : (p->second + 1) % n;

		for (size_t k = 0; k < n; ++k) {
			size_t const i = (start + k) % n;
			if (changes_nothing (m.steps[i])) {
				continue;
			}
			_cycle_position[slot] = i;
			run_step (m.steps[i]);
			break;
		}
	} else {
		for (vector<Step>::const_iterator s = m.steps.begin (); s != m.steps.end (); ++s) {
			run_step (*s);
		}
	}

	--_depth;
}

void
MacroManager::start_recording (int slot)
{
	stop_recording ();
	if (slot < 0 || slot >= max_macros) {
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
	root->set_property (X_("version"), 3); /* 3: has the Cue page's Clip/Device switch */
	root->set_property (X_("current-set"), current ().name);

	for (size_t i = 0; i < _shortcuts.size (); ++i) {
		if (_shortcuts[i].key.empty () && (_shortcuts[i].scope.empty () || _shortcuts[i].scope == X_("Global"))) {
			continue;
		}
		XMLNode* node = root->add_child (X_("Shortcut"));
		node->set_property (X_("slot"), (int) i + 1);
		node->set_property (X_("key"), _shortcuts[i].key);
		node->set_property (X_("scope"), _shortcuts[i].scope.empty () ? string (X_("Global")) : _shortcuts[i].scope);
	}

	for (vector<KeyChange>::const_iterator k = _key_changes.begin (); k != _key_changes.end (); ++k) {
		XMLNode* node = root->add_child (X_("KeyChange"));
		node->set_property (X_("set"), k->set);
		node->set_property (X_("action"), k->action);
		node->set_property (X_("from"), k->from);
		node->set_property (X_("to"), k->to);
	}

	for (vector<MacroSet>::const_iterator s = _sets.begin (); s != _sets.end (); ++s) {
		XMLNode* set_node = root->add_child (X_("MacroSet"));
		set_node->set_property (X_("name"), s->name);

		for (size_t i = 0; i < s->macros.size (); ++i) {
			Macro const& m (s->macros[i]);
			if (m.empty ()) {
				continue;
			}
			XMLNode* macro_node = set_node->add_child (X_("Macro"));
			macro_node->set_property (X_("slot"), (int) i + 1);
			macro_node->set_property (X_("name"), m.name);
			if (m.cycle) {
				macro_node->set_property (X_("cycle"), true);
			}

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
