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
#include <ytkmm/comboboxtext.h>
#include <ytkmm/entry.h>
#include <ytkmm/label.h>
#include <ytkmm/liststore.h>
#include <ytkmm/togglebutton.h>
#include <ytkmm/treeview.h>

#include "ardour_window.h"

class MacroEditor : public ArdourWindow
{
public:
	MacroEditor ();

protected:
	bool on_key_press_event (GdkEventKey*);
	bool on_delete_event (GdkEventAny*);

private:
	class MacroColumns : public Gtk::TreeModelColumnRecord
	{
	public:
		MacroColumns ()
		{
			add (slot);
			add (number);
			add (name);
			add (shortcut);
			add (steps);
		}
		Gtk::TreeModelColumn<int>         slot;
		Gtk::TreeModelColumn<std::string> number;
		Gtk::TreeModelColumn<std::string> name;
		Gtk::TreeModelColumn<std::string> shortcut;
		Gtk::TreeModelColumn<std::string> steps;
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

	MacroColumns                 _macro_columns;
	StepColumns                  _step_columns;
	Glib::RefPtr<Gtk::ListStore> _macro_store;
	Glib::RefPtr<Gtk::ListStore> _step_store;
	Gtk::TreeView                _macro_view;
	Gtk::TreeView                _step_view;

	Gtk::ComboBoxText _set_combo;
	Gtk::Button       _new_set_button;
	Gtk::Button       _copy_set_button;
	Gtk::Button       _rename_set_button;
	Gtk::Button       _remove_set_button;

	Gtk::Entry  _name_entry;
	Gtk::Label  _shortcut_label;
	Gtk::Button _shortcut_button;
	Gtk::Button _clear_shortcut_button;

	Gtk::ComboBoxText _mode_combo;
	Gtk::Button       _add_step_button;
	Gtk::Button       _remove_step_button;
	Gtk::Button       _up_button;
	Gtk::Button       _down_button;
	Gtk::ToggleButton _record_button;
	Gtk::Button       _run_button;
	Gtk::Button       _clear_button;
	Gtk::Label        _status;

	int  _slot;
	bool _updating;
	bool _capturing_key;

	void redisplay ();
	void show_macro ();
	void update_step_buttons ();
	int  selected_step ();
	void select_step (int);

	void set_chosen ();
	void new_set ();
	void copy_set ();
	void rename_set ();
	void remove_set ();
	bool ask_name (std::string const& title, std::string const& initial, std::string& result);

	void macro_selection_changed ();
	void step_selection_changed ();
	void name_changed ();
	void mode_changed ();
	void start_key_capture ();
	void stop_key_capture ();
	void clear_shortcut ();
	void add_step ();
	void remove_step ();
	void move_step (int);
	void record_toggled ();
	void run_macro ();
	void clear_macro ();
};

#endif /* __gtk2_ardour_macro_editor_h__ */
