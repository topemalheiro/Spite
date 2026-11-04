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
#include <vector>

#include <glibmm/refptr.h>
#include <sigc++/signal.h>

#include "gtkmm2ext/bindings.h"

namespace Gtk {
	class Action;
	class ActionGroup;
	class Widget;
}

/* Each slot is the action Macros/macro-N, so menus, keys and control surfaces can run it. */
class MacroManager
{
public:
	static MacroManager& instance ();

	static const int n_slots = 32;

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
		std::string       name;
		std::vector<Step> steps;
		bool empty () const { return name.empty () && steps.empty (); }
	};

	struct MacroSet {
		std::string        name;
		std::vector<Macro> macros; /* n_slots entries */
	};

	void register_actions ();
	void load ();

	std::vector<std::string> set_names () const;
	std::string const& current_set_name () const;
	bool use_set (std::string const& name);
	bool add_set (std::string const& name, bool copy_current);
	bool rename_set (std::string const& name);
	bool remove_set ();
	void next_set ();
	void previous_set ();

	Macro const& macro (int slot) const;
	void set_macro (int slot, Macro const&);

	Gtkmm2ext::KeyboardKey shortcut (int slot) const;
	std::string key_conflict (Gtkmm2ext::KeyboardKey const&, int slot) const;
	void set_shortcut (int slot, Gtkmm2ext::KeyboardKey const&);
	void clear_shortcut (int slot);

	void run (int slot);

	void start_recording (int slot);
	void stop_recording ();
	int recording_slot () const { return _recording_slot; }

	static std::string action_path (int slot);
	static std::string action_label (std::string const& path);
	static bool is_toggle (std::string const& path);
	static bool exists (std::string const& path);

	sigc::signal<void> Changed;

private:
	MacroManager ();

	std::vector<MacroSet>    _sets;
	size_t                   _current;
	/* Kept here, not in the .keys file: saving a .keys file freezes every default binding. */
	std::vector<std::string> _shortcuts; /* KeyboardKey::name () per slot */
	Glib::RefPtr<Gtk::ActionGroup> _group;
	int                      _depth;
	int                      _recording_slot;
	int                      _activation_depth;
	sigc::connection         _pre_activate_connection;
	sigc::connection         _post_activate_connection;

	MacroSet&       current ();
	MacroSet const& current () const;
	MacroSet*       find_set (std::string const&);

	void seed_starter_set ();
	void update_actions ();
	void apply_shortcuts ();
	void save () const;
	void changed ();
	void activating (Glib::RefPtr<Gtk::Action> const&);
	void recorded (Glib::RefPtr<Gtk::Action> const&);
	void proxy_connected (Glib::RefPtr<Gtk::Action> const&, Gtk::Widget*);
	std::string filename () const;
};

#endif /* __gtk2_ardour_macro_manager_h__ */
