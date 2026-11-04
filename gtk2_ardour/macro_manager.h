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

#ifndef __gtk2_ardour_macro_manager_h__
#define __gtk2_ardour_macro_manager_h__

#include <string>
#include <map>
#include <vector>

#include <glibmm/refptr.h>
#include <sigc++/signal.h>

#include "gtkmm2ext/bindings.h"

namespace Gtk {
	class Action;
	class ActionGroup;
	class Widget;
}

/* Macro N is the action Macros/macro-N and runs macro N of the current set,
 * so menus, keys and control surfaces can run it. There is no fixed number
 * of macros: actions are registered as macros are added.
 */
class MacroManager
{
public:
	static MacroManager& instance ();

	/* actions registered even when unused, so control-surface maps keep working */
	static const int min_actions = 32;
	static const int max_macros  = 4096;

	enum StepMode {
		Run,     /* activate the action; for a toggle this flips it */
		TurnOn,  /* toggles only: make sure it is on */
		TurnOff, /* toggles only: make sure it is off */
	};

	struct Step {
		Step () : mode (Run) {}
		Step (std::string const& a, StepMode m) : action (a), mode (m) {}
		std::string action; /* "Group/name" */
		StepMode    mode;
	};

	struct Macro {
		Macro () : cycle (false) {}
		std::string       name;
		std::vector<Step> steps;
		bool              cycle; /* each run does only the next step */
		bool empty () const { return name.empty () && steps.empty (); }
	};

	struct MacroSet {
		std::string        name;
		std::vector<Macro> macros; /* macro N is macros[N-1]; empty ones are gaps */
	};

	/* a key binding that is not a macro */
	struct Shortcut {
		std::string            set;         /* Bindings name */
		std::string            action;      /* "Group/name" */
		Gtkmm2ext::KeyboardKey key;         /* null if its default key was taken away */
		std::string            default_key; /* KeyboardKey::name () of the key it had by default, if changed here */
		std::string            label;
		bool                   runnable;    /* false for a template's keys, copied into every editor */
	};

	/* something a key already does, in one binding set */
	struct KeyUse {
		std::string set;
		std::string action;
		std::string label;
	};

	void register_actions ();
	void load ();
	void menus_loaded ();

	std::vector<std::string> set_names () const;
	std::string const& current_set_name () const;
	bool use_set (std::string const& name);
	bool add_set (std::string const& name, bool copy_current);
	bool rename_set (std::string const& name);
	bool remove_set ();
	void next_set ();
	void previous_set ();

	/* slots are 0-based: slot 0 is macro 1 */
	int  slot_count () const;
	Macro const& macro (int slot) const;
	void set_macro (int slot, Macro const&);
	int  new_macro (std::string const& name, std::vector<Step> const& steps = std::vector<Step> ());
	void delete_macro (int slot);

	Gtkmm2ext::KeyboardKey shortcut (int slot) const;
	std::string shortcut_scope (int slot) const;
	void set_shortcut (int slot, Gtkmm2ext::KeyboardKey const&, std::string const& scope);
	void set_shortcut_scope (int slot, std::string const& scope);
	void clear_shortcut (int slot);

	/* the action that already has this key in the binding set `scope`
	 * (ignoring `self`), or an empty string if the key is free there
	 */
	std::string key_holder (Gtkmm2ext::KeyboardKey const&, std::string const& scope, std::string const& self) const;
	/* everything the key does, in every binding set (ignoring `self`) */
	std::vector<KeyUse> key_uses (Gtkmm2ext::KeyboardKey const&, std::string const& self) const;
	/* free the key in `scope`, taking it from whatever holds it */
	void take_key (Gtkmm2ext::KeyboardKey const&, std::string const& scope, std::string const& self);

	std::vector<Shortcut> shortcuts () const;
	/* change one key of a key binding that is not a macro; `default_key` names
	 * the entry when `old_key` is null (a default key that was taken away)
	 */
	void set_builtin_key (std::string const& set, std::string const& action, Gtkmm2ext::KeyboardKey const& old_key,
	                      Gtkmm2ext::KeyboardKey const& new_key, std::string const& default_key = std::string ());
	std::string reset_builtin_key (std::string const& set, std::string const& action, std::string const& default_key);

	void run (int slot);

	void start_recording (int slot);
	void stop_recording ();
	int recording_slot () const { return _recording_slot; }

	static std::string action_path (int slot);
	static int slot_of (std::string const& path); /* -1 if not a macro action */
	static std::string action_label (std::string const& path);
	static bool is_toggle (std::string const& path);
	static bool exists (std::string const& path);

	/* binding sets a macro key can be given to, as {set name, label} */
	static std::vector<std::pair<std::string, std::string> > scopes ();
	static std::string scope_label (std::string const& set);
	/* scope_label, saying so for a template copied into every editor */
	static std::string set_label (std::string const& set);

	sigc::signal<void> Changed;

private:
	MacroManager ();

	struct Binding {
		std::string key;   /* KeyboardKey::name (), empty for none */
		std::string scope; /* Bindings name */
	};

	/* one default key of `action` in `set` replaced (or removed, `to` empty) */
	struct KeyChange {
		std::string set;
		std::string action;
		std::string from;
		std::string to;
	};

	std::vector<MacroSet>    _sets;
	size_t                   _current;
	/* Kept here, not in the .keys file: saving a .keys file freezes every default binding. */
	std::vector<Binding>     _shortcuts; /* per slot, shared by every set */
	std::vector<KeyChange>   _key_changes;
	std::map<int, size_t>    _cycle_position;
	Glib::RefPtr<Gtk::ActionGroup> _group;
	int                      _registered;
	unsigned int             _merge_id;
	bool                     _menus_loaded;
	int                      _depth;
	int                      _recording_slot;
	int                      _activation_depth;
	sigc::connection         _pre_activate_connection;
	sigc::connection         _post_activate_connection;

	MacroSet&       current ();
	MacroSet const& current () const;
	MacroSet*       find_set (std::string const&);

	void seed_starter_set ();
	void ensure_actions (int count);
	void update_actions ();
	void update_menu ();
	void apply_key_changes ();
	void apply_shortcuts ();
	std::vector<KeyChange>::iterator find_key_change (std::string const& set, std::string const& action, std::string const* from, std::string const* to);
	std::vector<KeyChange>::const_iterator find_key_change (std::string const& set, std::string const& action, std::string const* from, std::string const* to) const;
	void save () const;
	void changed ();
	bool changes_nothing (Step const&) const;
	void run_step (Step const&);
	void activating (Glib::RefPtr<Gtk::Action> const&);
	void recorded (Glib::RefPtr<Gtk::Action> const&);
	void proxy_connected (Glib::RefPtr<Gtk::Action> const&, Gtk::Widget*);
	std::string filename () const;
	static std::string binding_label (Gtkmm2ext::Bindings const*, std::string const& action);
};

#endif /* __gtk2_ardour_macro_manager_h__ */
