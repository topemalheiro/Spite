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
#include <sstream>

#include <glibmm/ustring.h>

#include <ytkmm/box.h>
#include <ytkmm/scrolledwindow.h>
#include <ytkmm/stock.h>
#include <ytkmm/table.h>
#include <ytkmm/treemodelfilter.h>

#include "pbd/compose.h"
#include "pbd/unwind.h"

#include "gtkmm2ext/actions.h"
#include "gtkmm2ext/keyboard.h"

#include "widgets/prompter.h"

#include "ardour_dialog.h"
#include "ardour_message.h"
#include "macro_editor.h"
#include "macro_manager.h"

#include "pbd/i18n.h"

using namespace std;
using namespace Gtk;
using namespace Gtkmm2ext;

namespace {

string
lower (string const& s)
{
	return Glib::ustring (s).lowercase ().raw ();
}

string
does_text (MacroManager::Step const& s)
{
	switch (s.mode) {
		case MacroManager::TurnOn:
			return _("Turn on");
		case MacroManager::TurnOff:
			return _("Turn off");
		default:
			return MacroManager::is_toggle (s.action) ? _("Toggle") : _("Run");
	}
}

class MacroActionPicker : public ArdourDialog
{
public:
	MacroActionPicker (Gtk::Window& parent)
		: ArdourDialog (parent, _("Add Macro Step"), true)
	{
		vector<string> paths;
		vector<string> labels;
		vector<string> tooltips;
		vector<string> keys;
		vector<Glib::RefPtr<Action> > actions;
		ActionManager::get_all_actions (paths, labels, tooltips, keys, actions);

		vector<Entry> entries;
		for (vector<string>::const_iterator p = paths.begin (); p != paths.end (); ++p) {
			string::size_type const slash = p->find ('/');
			if (slash == string::npos) {
				continue;
			}
			Entry e;
			e.group = p->substr (0, slash);
			string const name = p->substr (slash + 1);
			if (e.group == X_("Main Menu") || (name.size () > 4 && name.compare (name.size () - 4, 4, X_("Menu")) == 0) || *p == X_("Macros/current-set")) {
				continue;
			}
			e.path  = *p;
			e.label = MacroManager::action_label (*p);
			if (e.label.empty ()) {
				e.label = name;
			}
			entries.push_back (e);
		}
		sort (entries.begin (), entries.end ());

		_store = ListStore::create (_columns);
		for (vector<Entry>::const_iterator e = entries.begin (); e != entries.end (); ++e) {
			TreeModel::Row row = *_store->append ();
			row[_columns.label] = e->label;
			row[_columns.group] = e->group;
			row[_columns.path]  = e->path;
		}

		_filter = TreeModelFilter::create (_store);
		_filter->set_visible_func (sigc::mem_fun (*this, &MacroActionPicker::visible));

		_view.set_model (_filter);
		_view.append_column (_("Action"), _columns.label);
		_view.append_column (_("Group"), _columns.group);
		_view.get_column (0)->set_expand (true);
		_view.set_enable_search (false);
		_view.signal_row_activated ().connect (sigc::mem_fun (*this, &MacroActionPicker::row_activated));
		_view.get_selection ()->signal_changed ().connect (sigc::mem_fun (*this, &MacroActionPicker::selection_changed));

		_search.signal_changed ().connect (sigc::mem_fun (*this, &MacroActionPicker::search_changed));

		HBox* search_box = manage (new HBox);
		search_box->set_spacing (6);
		search_box->pack_start (*manage (new Label (_("Search:"))), false, false);
		search_box->pack_start (_search, true, true);

		ScrolledWindow* scroller = manage (new ScrolledWindow);
		scroller->set_policy (POLICY_NEVER, POLICY_AUTOMATIC);
		scroller->add (_view);

		get_vbox ()->set_spacing (6);
		get_vbox ()->pack_start (*search_box, false, false);
		get_vbox ()->pack_start (*scroller, true, true);

		add_button (Stock::CANCEL, RESPONSE_CANCEL);
		add_button (_("Add Step"), RESPONSE_ACCEPT);
		set_response_sensitive (RESPONSE_ACCEPT, false);
		set_default_size (600, 600);

		show_all ();
		_search.grab_focus ();
	}

	string chosen ()
	{
		TreeModel::iterator i = _view.get_selection ()->get_selected ();
		if (!i) {
			return string ();
		}
		return (*i)[_columns.path];
	}

private:
	struct Entry {
		string group;
		string label;
		string path;
		bool operator< (Entry const& o) const
		{
			return group != o.group ? group < o.group : label < o.label;
		}
	};

	class Columns : public TreeModelColumnRecord
	{
	public:
		Columns ()
		{
			add (label);
			add (group);
			add (path);
		}
		TreeModelColumn<string> label;
		TreeModelColumn<string> group;
		TreeModelColumn<string> path;
	};

	Columns                          _columns;
	Glib::RefPtr<ListStore>          _store;
	Glib::RefPtr<TreeModelFilter>    _filter;
	TreeView                         _view;
	Gtk::Entry                       _search;

	bool visible (TreeModel::const_iterator const& i)
	{
		string const words = lower (_search.get_text ());
		if (words.empty ()) {
			return true;
		}
		string const haystack = lower ((*i).get_value (_columns.label) + " " + (*i).get_value (_columns.group) + " " + (*i).get_value (_columns.path));
		istringstream stream (words);
		string word;
		while (stream >> word) {
			if (haystack.find (word) == string::npos) {
				return false;
			}
		}
		return true;
	}

	void search_changed ()
	{
		_filter->refilter ();
	}

	void selection_changed ()
	{
		set_response_sensitive (RESPONSE_ACCEPT, _view.get_selection ()->count_selected_rows () == 1);
	}

	void row_activated (TreeModel::Path const&, TreeViewColumn*)
	{
		response (RESPONSE_ACCEPT);
	}
};

} // namespace

MacroEditor::MacroEditor ()
	: ArdourWindow (_("Macro Editor"))
	, _new_set_button (_("New"))
	, _copy_set_button (_("Duplicate"))
	, _rename_set_button (_("Rename"))
	, _remove_set_button (_("Delete"))
	, _shortcut_button (_("Set Shortcut"))
	, _clear_shortcut_button (_("Clear"))
	, _add_step_button (_("Add Step..."))
	, _remove_step_button (_("Remove Step"))
	, _up_button (_("Move Up"))
	, _down_button (_("Move Down"))
	, _record_button (_("Record"))
	, _run_button (_("Run Macro"))
	, _clear_button (_("Clear Macro"))
	, _slot (-1)
	, _updating (false)
	, _capturing_key (false)
{
	_macro_store = ListStore::create (_macro_columns);
	for (int i = 0; i < MacroManager::n_slots; ++i) {
		TreeModel::Row row = *_macro_store->append ();
		row[_macro_columns.slot]   = i;
		row[_macro_columns.number] = string_compose ("%1", i + 1);
	}
	_macro_view.set_model (_macro_store);
	_macro_view.append_column (_("#"), _macro_columns.number);
	_macro_view.append_column (_("Macro"), _macro_columns.name);
	_macro_view.append_column (_("Shortcut"), _macro_columns.shortcut);
	_macro_view.append_column (_("Steps"), _macro_columns.steps);
	_macro_view.get_column (1)->set_expand (true);
	_macro_view.get_selection ()->signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::macro_selection_changed));

	_step_store = ListStore::create (_step_columns);
	_step_view.set_model (_step_store);
	_step_view.append_column (_("#"), _step_columns.number);
	_step_view.append_column (_("Action"), _step_columns.action);
	_step_view.append_column (_("Does"), _step_columns.does);
	_step_view.get_column (1)->set_expand (true);
	_step_view.get_selection ()->signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::step_selection_changed));

	/* row numbers match MacroManager::StepMode */
	_mode_combo.append_text (_("Run or toggle"));
	_mode_combo.append_text (_("Turn on"));
	_mode_combo.append_text (_("Turn off"));

	_set_combo.signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::set_chosen));
	_new_set_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::new_set));
	_copy_set_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::copy_set));
	_rename_set_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::rename_set));
	_remove_set_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::remove_set));
	_name_entry.signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::name_changed));
	_shortcut_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::start_key_capture));
	_clear_shortcut_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::clear_shortcut));
	_mode_combo.signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::mode_changed));
	_add_step_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::add_step));
	_remove_step_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::remove_step));
	_up_button.signal_clicked ().connect (sigc::bind (sigc::mem_fun (*this, &MacroEditor::move_step), -1));
	_down_button.signal_clicked ().connect (sigc::bind (sigc::mem_fun (*this, &MacroEditor::move_step), 1));
	_record_button.signal_toggled ().connect (sigc::mem_fun (*this, &MacroEditor::record_toggled));
	_run_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::run_macro));
	_clear_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::clear_macro));

	HBox* set_box = manage (new HBox);
	set_box->set_spacing (6);
	set_box->pack_start (*manage (new Label (_("Macro set:"))), false, false);
	set_box->pack_start (_set_combo, false, false);
	set_box->pack_start (_new_set_button, false, false);
	set_box->pack_start (_copy_set_button, false, false);
	set_box->pack_start (_rename_set_button, false, false);
	set_box->pack_start (_remove_set_button, false, false);

	ScrolledWindow* macro_scroller = manage (new ScrolledWindow);
	macro_scroller->set_policy (POLICY_NEVER, POLICY_AUTOMATIC);
	macro_scroller->add (_macro_view);
	macro_scroller->set_size_request (420, -1);

	Table* details = manage (new Table (2, 4));
	details->set_spacings (6);
	Label* label = manage (new Label (_("Name:")));
	label->set_alignment (1.0, 0.5);
	details->attach (*label, 0, 1, 0, 1, FILL, SHRINK);
	details->attach (_name_entry, 1, 4, 0, 1, EXPAND | FILL, SHRINK);
	label = manage (new Label (_("Shortcut:")));
	label->set_alignment (1.0, 0.5);
	_shortcut_label.set_alignment (0.0, 0.5);
	details->attach (*label, 0, 1, 1, 2, FILL, SHRINK);
	details->attach (_shortcut_label, 1, 2, 1, 2, EXPAND | FILL, SHRINK);
	details->attach (_shortcut_button, 2, 3, 1, 2, FILL, SHRINK);
	details->attach (_clear_shortcut_button, 3, 4, 1, 2, FILL, SHRINK);

	ScrolledWindow* step_scroller = manage (new ScrolledWindow);
	step_scroller->set_policy (POLICY_NEVER, POLICY_AUTOMATIC);
	step_scroller->add (_step_view);

	HBox* step_buttons = manage (new HBox);
	step_buttons->set_spacing (6);
	step_buttons->pack_start (_add_step_button, false, false);
	step_buttons->pack_start (_remove_step_button, false, false);
	step_buttons->pack_start (_up_button, false, false);
	step_buttons->pack_start (_down_button, false, false);
	step_buttons->pack_end (_mode_combo, false, false);
	step_buttons->pack_end (*manage (new Label (_("Selected step:"))), false, false);

	HBox* macro_buttons = manage (new HBox);
	macro_buttons->set_spacing (6);
	macro_buttons->pack_start (_record_button, false, false);
	macro_buttons->pack_start (_run_button, false, false);
	macro_buttons->pack_end (_clear_button, false, false);

	Label* steps_title = manage (new Label (_("Steps, run in order:")));
	steps_title->set_alignment (0.0, 0.5);

	VBox* right = manage (new VBox);
	right->set_spacing (6);
	right->pack_start (*details, false, false);
	right->pack_start (*steps_title, false, false);
	right->pack_start (*step_scroller, true, true);
	right->pack_start (*step_buttons, false, false);
	right->pack_start (*macro_buttons, false, false);

	HBox* main_box = manage (new HBox);
	main_box->set_spacing (12);
	main_box->pack_start (*macro_scroller, false, false);
	main_box->pack_start (*right, true, true);

	/* fixed size, so status messages never move the buttons */
	_status.set_alignment (0.0, 0.0);
	_status.set_line_wrap (true);
	_status.set_size_request (960, 36);

	Label* help = manage (new Label (_("A macro runs Spite actions in order. Run it from the Macros menu, its shortcut, or a control surface: map a button to the action Macros/macro-N in the surface's settings or Generic MIDI map. Record adds every action you use until you click Stop Recording.")));
	help->set_alignment (0.0, 0.5);
	help->set_line_wrap (true);
	help->set_size_request (960, -1);

	VBox* vbox = manage (new VBox);
	vbox->set_spacing (8);
	vbox->set_border_width (8);
	vbox->pack_start (*set_box, false, false);
	vbox->pack_start (*main_box, true, true);
	vbox->pack_start (_status, false, false);
	vbox->pack_start (*help, false, false);
	add (*vbox);
	vbox->show_all ();

	set_default_size (1000, 620);

	MacroManager::instance ().Changed.connect (sigc::mem_fun (*this, &MacroEditor::redisplay));

	redisplay ();
	_macro_view.get_selection ()->select (TreeModel::Path ("0"));
}

void
MacroEditor::redisplay ()
{
	MacroManager& mm (MacroManager::instance ());
	PBD::Unwinder<bool> uw (_updating, true);

	vector<string> const names = mm.set_names ();
	_set_combo.clear_items ();
	for (vector<string>::const_iterator n = names.begin (); n != names.end (); ++n) {
		_set_combo.append_text (*n);
	}
	_set_combo.set_active_text (mm.current_set_name ());
	_remove_set_button.set_sensitive (names.size () > 1);

	TreeModel::Children rows = _macro_store->children ();
	for (TreeModel::Children::iterator r = rows.begin (); r != rows.end (); ++r) {
		int const slot = (*r)[_macro_columns.slot];
		MacroManager::Macro const& m (mm.macro (slot));
		KeyboardKey const key = mm.shortcut (slot);
		size_t const n = m.steps.size ();

		(*r)[_macro_columns.name]     = m.name;
		(*r)[_macro_columns.shortcut] = (key == KeyboardKey::null_key ()) ? string () : key.display_label ();
		(*r)[_macro_columns.steps]    = n == 0 ? string () : (n == 1 ? string (_("1 step")) : string_compose (_("%1 steps"), n));
	}

	show_macro ();
}

void
MacroEditor::show_macro ()
{
	MacroManager& mm (MacroManager::instance ());
	PBD::Unwinder<bool> uw (_updating, true);
	bool const have = _slot >= 0;

	_name_entry.set_sensitive (have);
	_shortcut_button.set_sensitive (have);
	_add_step_button.set_sensitive (have);
	_record_button.set_sensitive (have);

	int const selected = selected_step ();
	_step_store->clear ();

	if (!have) {
		_name_entry.set_text (string ());
		_shortcut_label.set_text (string ());
		_clear_shortcut_button.set_sensitive (false);
		_run_button.set_sensitive (false);
		_clear_button.set_sensitive (false);
		update_step_buttons ();
		return;
	}

	MacroManager::Macro const& m (mm.macro (_slot));

	if (_name_entry.get_text () != m.name) {
		_name_entry.set_text (m.name);
	}

	KeyboardKey const key = mm.shortcut (_slot);
	bool const has_key = !(key == KeyboardKey::null_key ());
	if (!_capturing_key) {
		_shortcut_label.set_text (has_key ? key.display_label () : string (_("None")));
	}
	_clear_shortcut_button.set_sensitive (has_key);

	for (size_t n = 0; n < m.steps.size (); ++n) {
		MacroManager::Step const& s (m.steps[n]);
		TreeModel::Row row = *_step_store->append ();
		row[_step_columns.index]  = n;
		row[_step_columns.number] = string_compose ("%1", n + 1);
		row[_step_columns.action] = MacroManager::exists (s.action) ? MacroManager::action_label (s.action) : string_compose (_("%1 (not available now)"), s.action);
		row[_step_columns.does]   = does_text (s);
	}

	select_step (min (selected, (int) m.steps.size () - 1));

	_record_button.set_active (mm.recording_slot () == _slot);
	_record_button.set_label (mm.recording_slot () == _slot ? _("Stop Recording") : _("Record"));
	_run_button.set_sensitive (!m.steps.empty ());
	_clear_button.set_sensitive (!m.empty ());

	update_step_buttons ();
}

void
MacroEditor::update_step_buttons ()
{
	PBD::Unwinder<bool> uw (_updating, true);
	int const step = selected_step ();
	int const count = _step_store->children ().size ();

	_remove_step_button.set_sensitive (step >= 0);
	_up_button.set_sensitive (step > 0);
	_down_button.set_sensitive (step >= 0 && step < count - 1);

	if (step < 0 || _slot < 0) {
		_mode_combo.set_active (-1);
		_mode_combo.set_sensitive (false);
		return;
	}

	MacroManager::Step const& s (MacroManager::instance ().macro (_slot).steps[step]);
	_mode_combo.set_sensitive (MacroManager::is_toggle (s.action));
	_mode_combo.set_active ((int) s.mode);
}

int
MacroEditor::selected_step ()
{
	TreeModel::iterator i = _step_view.get_selection ()->get_selected ();
	if (!i) {
		return -1;
	}
	return (*i)[_step_columns.index];
}

void
MacroEditor::select_step (int n)
{
	if (n < 0 || n >= (int) _step_store->children ().size ()) {
		return;
	}
	TreeModel::Path path;
	path.push_back (n);
	_step_view.get_selection ()->select (path);
	_step_view.scroll_to_row (path);
}

void
MacroEditor::macro_selection_changed ()
{
	if (_updating) {
		return;
	}
	stop_key_capture ();
	TreeModel::iterator i = _macro_view.get_selection ()->get_selected ();
	_slot = i ? (int) (*i)[_macro_columns.slot] : -1;
	_status.set_text (string ());
	show_macro ();
}

void
MacroEditor::step_selection_changed ()
{
	if (_updating) {
		return;
	}
	update_step_buttons ();
}

void
MacroEditor::name_changed ()
{
	if (_updating || _slot < 0) {
		return;
	}
	MacroManager::Macro m = MacroManager::instance ().macro (_slot);
	m.name = _name_entry.get_text ();
	MacroManager::instance ().set_macro (_slot, m);
}

void
MacroEditor::mode_changed ()
{
	int const step = selected_step ();
	int const mode = _mode_combo.get_active_row_number ();
	if (_updating || _slot < 0 || step < 0 || mode < 0) {
		return;
	}
	MacroManager::Macro m = MacroManager::instance ().macro (_slot);
	m.steps[step].mode = (MacroManager::StepMode) mode;
	MacroManager::instance ().set_macro (_slot, m);
	select_step (step);
}

void
MacroEditor::start_key_capture ()
{
	if (_slot < 0) {
		return;
	}
	_capturing_key = true;
	_shortcut_label.set_text (_("Press the keys now (Esc cancels)"));
	_shortcut_button.set_sensitive (false);
}

void
MacroEditor::stop_key_capture ()
{
	if (!_capturing_key) {
		return;
	}
	_capturing_key = false;
	_shortcut_button.set_sensitive (_slot >= 0);
	show_macro ();
}

bool
MacroEditor::on_key_press_event (GdkEventKey* ev)
{
	if (!_capturing_key) {
		return ArdourWindow::on_key_press_event (ev);
	}

	if (ev->is_modifier) {
		return true;
	}

	if (ev->keyval == GDK_Escape) {
		stop_key_capture ();
		return true;
	}

	GdkModifierType const mod = (GdkModifierType) (ev->state & Keyboard::RelevantModifierKeyMask);
	KeyboardKey const key (mod, gdk_keyval_to_lower (ev->keyval));
	string const conflict = MacroManager::instance ().key_conflict (key, _slot);

	stop_key_capture ();

	if (!conflict.empty ()) {
		_status.set_text (string_compose (_("%1. Pick another key, or change that shortcut in Window > Keyboard Shortcuts first."), conflict));
		return true;
	}

	MacroManager::instance ().set_shortcut (_slot, key);
	_status.set_text (string_compose (_("%1 now runs this macro. Menus show new shortcuts after a restart."), key.display_label ()));
	return true;
}

bool
MacroEditor::on_delete_event (GdkEventAny* ev)
{
	stop_key_capture ();
	MacroManager::instance ().stop_recording ();
	return ArdourWindow::on_delete_event (ev);
}

void
MacroEditor::clear_shortcut ()
{
	if (_slot >= 0) {
		MacroManager::instance ().clear_shortcut (_slot);
	}
}

void
MacroEditor::add_step ()
{
	if (_slot < 0) {
		return;
	}

	MacroActionPicker picker (*this);
	if (picker.run () != RESPONSE_ACCEPT) {
		return;
	}
	string const path = picker.chosen ();
	if (path.empty ()) {
		return;
	}

	MacroManager::Macro m = MacroManager::instance ().macro (_slot);
	if (m.name.empty ()) {
		m.name = MacroManager::action_label (path);
	}
	int const after = selected_step ();
	int const at = after < 0 ? (int) m.steps.size () : after + 1;
	m.steps.insert (m.steps.begin () + at, MacroManager::Step (path, MacroManager::Run));
	MacroManager::instance ().set_macro (_slot, m);
	select_step (at);
}

void
MacroEditor::remove_step ()
{
	int const step = selected_step ();
	if (_slot < 0 || step < 0) {
		return;
	}
	MacroManager::Macro m = MacroManager::instance ().macro (_slot);
	m.steps.erase (m.steps.begin () + step);
	MacroManager::instance ().set_macro (_slot, m);
	select_step (min (step, (int) m.steps.size () - 1));
}

void
MacroEditor::move_step (int direction)
{
	int const step = selected_step ();
	if (_slot < 0 || step < 0) {
		return;
	}
	MacroManager::Macro m = MacroManager::instance ().macro (_slot);
	int const to = step + direction;
	if (to < 0 || to >= (int) m.steps.size ()) {
		return;
	}
	swap (m.steps[step], m.steps[to]);
	MacroManager::instance ().set_macro (_slot, m);
	select_step (to);
}

void
MacroEditor::record_toggled ()
{
	if (_updating) {
		return;
	}
	MacroManager& mm (MacroManager::instance ());

	if (!_record_button.get_active ()) {
		mm.stop_recording ();
		_status.set_text (_("Recording stopped."));
		return;
	}

	if (_slot < 0) {
		PBD::Unwinder<bool> uw (_updating, true);
		_record_button.set_active (false);
		return;
	}

	mm.start_recording (_slot);
	_status.set_text (string_compose (_("Recording macro %1: every menu item, shortcut or button action you use is added as a step. Click Stop Recording to finish."), _slot + 1));
}

void
MacroEditor::run_macro ()
{
	if (_slot >= 0) {
		MacroManager::instance ().run (_slot);
	}
}

void
MacroEditor::clear_macro ()
{
	if (_slot < 0) {
		return;
	}
	ArdourMessageDialog msg (*this, string_compose (_("Clear macro %1? Its shortcut stays with the slot."), _slot + 1), false, MESSAGE_QUESTION, BUTTONS_YES_NO, true);
	if (msg.run () != RESPONSE_YES) {
		return;
	}
	MacroManager::instance ().set_macro (_slot, MacroManager::Macro ());
}

void
MacroEditor::set_chosen ()
{
	if (_updating) {
		return;
	}
	MacroManager::instance ().use_set (_set_combo.get_active_text ());
}

bool
MacroEditor::ask_name (string const& title, string const& initial, string& result)
{
	ArdourWidgets::Prompter prompter (*this, true);
	prompter.set_title (title);
	prompter.set_prompt (_("Name:"));
	prompter.set_initial_text (initial);
	prompter.add_button (Stock::OK, RESPONSE_ACCEPT);
	prompter.show_all ();

	if (prompter.run () != RESPONSE_ACCEPT) {
		return false;
	}
	prompter.get_result (result);
	return !result.empty ();
}

void
MacroEditor::new_set ()
{
	string name;
	if (ask_name (_("New Macro Set"), string (), name) && !MacroManager::instance ().add_set (name, false)) {
		_status.set_text (string_compose (_("There is already a macro set called \"%1\"."), name));
	}
}

void
MacroEditor::copy_set ()
{
	string name;
	string const initial = string_compose (_("%1 copy"), MacroManager::instance ().current_set_name ());
	if (ask_name (_("Duplicate Macro Set"), initial, name) && !MacroManager::instance ().add_set (name, true)) {
		_status.set_text (string_compose (_("There is already a macro set called \"%1\"."), name));
	}
}

void
MacroEditor::rename_set ()
{
	string name;
	if (ask_name (_("Rename Macro Set"), MacroManager::instance ().current_set_name (), name) && !MacroManager::instance ().rename_set (name)) {
		_status.set_text (string_compose (_("There is already a macro set called \"%1\"."), name));
	}
}

void
MacroEditor::remove_set ()
{
	ArdourMessageDialog msg (*this, string_compose (_("Delete the macro set \"%1\"?"), MacroManager::instance ().current_set_name ()), false, MESSAGE_QUESTION, BUTTONS_YES_NO, true);
	if (msg.run () != RESPONSE_YES) {
		return;
	}
	MacroManager::instance ().remove_set ();
}
