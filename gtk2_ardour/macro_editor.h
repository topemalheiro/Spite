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


#ifndef __gtk2_ardour_macro_editor_h__
#define __gtk2_ardour_macro_editor_h__

#include <string>

#include <ytkmm/button.h>
#include <ytkmm/checkbutton.h>
#include <ytkmm/comboboxtext.h>
#include <ytkmm/entry.h>
#include <ytkmm/label.h>
#include <ytkmm/liststore.h>
#include <ytkmm/togglebutton.h>
#include <ytkmm/treestore.h>
#include <ytkmm/treeview.h>

#include "gtkmm2ext/bindings.h"

#include "ardour_window.h"

/* Lists every macro and every key binding. A key binding is shown as a
 * one-step entry: its key can be changed here, and adding steps turns it
 * into a macro on the same key.
 */
class MacroEditor : public ArdourWindow
{
public:
	MacroEditor ();

protected:
	bool on_key_press_event (GdkEventKey*);
	bool on_delete_event (GdkEventAny*);

private:
	enum RowKind {
		Heading,
		MacroRow,
		ShortcutRow,
	};

	class ListColumns : public Gtk::TreeModelColumnRecord
	{
	public:
		ListColumns ()
		{
			add (kind);
			add (slot);
			add (set);
			add (action);
			add (number);
			add (name);
			add (shortcut);
			add (where);
			add (steps);
			add (weight);
			add (keyname);
			add (defkey);
			add (runnable);
		}
		Gtk::TreeModelColumn<int>         kind;
		Gtk::TreeModelColumn<int>         slot;
		Gtk::TreeModelColumn<std::string> set;
		Gtk::TreeModelColumn<std::string> action;
		Gtk::TreeModelColumn<std::string> number;
		Gtk::TreeModelColumn<std::string> name;
		Gtk::TreeModelColumn<std::string> shortcut;
		Gtk::TreeModelColumn<std::string> where;
		Gtk::TreeModelColumn<std::string> steps;
		Gtk::TreeModelColumn<int>         weight;
		Gtk::TreeModelColumn<std::string> keyname; /* KeyboardKey::name () of a shortcut row */
		Gtk::TreeModelColumn<std::string> defkey;  /* its default key, if changed here */
		Gtk::TreeModelColumn<bool>        runnable; /* false for a template's keys */
	};

	class StepColumns : public Gtk::TreeModelColumnRecord
	{
	public:
		StepColumns ()
		{
			add (index);
			add (number);
			add (action);
			add (does);
		}
		Gtk::TreeModelColumn<int>         index;
		Gtk::TreeModelColumn<std::string> number;
		Gtk::TreeModelColumn<std::string> action;
		Gtk::TreeModelColumn<std::string> does;
	};

	ListColumns                  _list_columns;
	StepColumns                  _step_columns;
	Glib::RefPtr<Gtk::TreeStore> _list_store;
	Glib::RefPtr<Gtk::ListStore> _step_store;
	Gtk::TreeView                _list_view;
	Gtk::TreeView                _step_view;

	Gtk::ComboBoxText _set_combo;
	Gtk::Button       _new_set_button;
	Gtk::Button       _copy_set_button;
	Gtk::Button       _rename_set_button;
	Gtk::Button       _remove_set_button;

	Gtk::Entry        _search;
	Gtk::ComboBoxText _show_combo;
	Gtk::Button       _new_macro_button;
	Gtk::Button       _delete_macro_button;

	Gtk::Label        _kind_label;
	Gtk::Entry        _name_entry;
	Gtk::Label        _shortcut_label;
	Gtk::Button       _shortcut_button;
	Gtk::Button       _clear_shortcut_button;
	Gtk::Button       _reset_shortcut_button;
	Gtk::ComboBoxText _scope_combo;
	Gtk::Label        _scope_label;
	Gtk::CheckButton  _cycle_button;
	Gtk::Label        _status;

	Gtk::ComboBoxText _mode_combo;
	Gtk::Button       _add_step_button;
	Gtk::Button       _remove_step_button;
	Gtk::Button       _up_button;
	Gtk::Button       _down_button;
	Gtk::ToggleButton _record_button;
	Gtk::Button       _run_button;

	/* what is selected in the list */
	RowKind     _kind;
	int         _slot;
	std::string _set;
	std::string _action;
	std::string _key;     /* shortcut row: its key name, empty if none */
	std::string _default; /* shortcut row: its default key name, if changed */
	std::string _label;   /* shortcut row: what its action is called */
	bool        _runnable;

	bool _updating;
	bool _capturing_key;

	void redisplay ();
	void fill_list ();
	bool select_row (Gtk::TreeModel::Children);
	void show_selection ();
	void update_step_buttons ();
	int  selected_step ();
	void select_step (int);
	std::string selected_scope () const;
	bool matches (std::string const& haystack) const;

	void set_chosen ();
	void new_set ();
	void copy_set ();
	void rename_set ();
	void remove_set ();
	bool ask_name (std::string const& title, std::string const& initial, std::string& result);

	void list_selection_changed ();
	void step_selection_changed ();
	void search_changed ();
	void name_changed ();
	void mode_changed ();
	void scope_changed ();
	void cycle_toggled ();
	void start_key_capture ();
	void stop_key_capture ();
	void use_key (Gtkmm2ext::KeyboardKey const&);
	bool take_key_from_holder (Gtkmm2ext::KeyboardKey const&, std::string const& scope, std::string const& self);
	bool take_key_for (Gtkmm2ext::KeyboardKey const&, std::string const& scope, std::string const& self, std::string const& what);
	void clear_shortcut ();
	void reset_shortcut ();
	int  make_macro_from_shortcut ();
	void change_builtin_key (Gtkmm2ext::KeyboardKey const&);
	void new_macro ();
	void delete_macro ();
	void add_step ();
	void remove_step ();
	void move_step (int);
	void record_toggled ();
	void run_selected ();
};

#endif /* __gtk2_ardour_macro_editor_h__ */
