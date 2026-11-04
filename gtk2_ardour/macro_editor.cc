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
#include <ytkmm/cellrenderertext.h>
#include <glibmm/markup.h>

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

namespace {

int const bold   = 700;
int const normal = 400;

/* Global first, then the pages, then the rest by name */
int
set_order (string const& set)
{
	vector<pair<string, string> > const s = MacroManager::scopes ();
	for (size_t i = 0; i < s.size (); ++i) {
		if (s[i].first == set) {
			return i;
		}
	}
	return s.size ();
}

struct ShortcutOrder {
	bool operator() (MacroManager::Shortcut const& a, MacroManager::Shortcut const& b) const
	{
		int const oa = set_order (a.set);
		int const ob = set_order (b.set);
		if (oa != ob) {
			return oa < ob;
		}
		if (a.set != b.set) {
			return a.set < b.set;
		}
		return lower (a.label) < lower (b.label);
	}
};

string
key_text (KeyboardKey const& key)
{
	return (key == KeyboardKey::null_key ()) ? string () : key.display_label ();
}

} // namespace

MacroEditor::MacroEditor ()
	: ArdourWindow (_("Macro Editor"))
	, _new_set_button (_("New"))
	, _copy_set_button (_("Duplicate"))
	, _rename_set_button (_("Rename"))
	, _remove_set_button (_("Delete"))
	, _new_macro_button (_("New Macro"))
	, _delete_macro_button (_("Delete Macro"))
	, _shortcut_button (_("Set Shortcut"))
	, _clear_shortcut_button (_("Clear"))
	, _reset_shortcut_button (_("Reset to Default"))
	, _cycle_button (_("Cycle: each press runs only the next step"))
	, _add_step_button (_("Add Step..."))
	, _remove_step_button (_("Remove Step"))
	, _up_button (_("Move Up"))
	, _down_button (_("Move Down"))
	, _record_button (_("Record"))
	, _run_button (_("Run"))
	, _kind (Heading)
	, _slot (-1)
	, _updating (false)
	, _runnable (false)
	, _capturing_key (false)
{
	_list_store = TreeStore::create (_list_columns);
	_list_view.set_model (_list_store);
	_list_view.append_column (_("#"), _list_columns.number);
	_list_view.append_column (_("Name"), _list_columns.name);
	_list_view.append_column (_("Shortcut"), _list_columns.shortcut);
	_list_view.append_column (_("Works in"), _list_columns.where);
	_list_view.append_column (_("Steps"), _list_columns.steps);
	_list_view.get_column (1)->set_expand (true);
	_list_view.set_enable_search (false);
	for (int c = 0; c < 5; ++c) {
		TreeViewColumn* col = _list_view.get_column (c);
		CellRendererText* r = dynamic_cast<CellRendererText*> (col->get_first_cell_renderer ());
		if (r) {
			col->add_attribute (r->property_weight (), _list_columns.weight);
		}
	}
	_list_view.get_selection ()->signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::list_selection_changed));

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

	_show_combo.append_text (_("Everything"));
	_show_combo.append_text (_("Macros"));
	_show_combo.append_text (_("Shortcuts"));
	_show_combo.set_active (0);

	vector<pair<string, string> > const scopes = MacroManager::scopes ();
	for (vector<pair<string, string> >::const_iterator s = scopes.begin (); s != scopes.end (); ++s) {
		_scope_combo.append_text (s->second);
	}

	_set_combo.signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::set_chosen));
	_new_set_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::new_set));
	_copy_set_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::copy_set));
	_rename_set_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::rename_set));
	_remove_set_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::remove_set));
	_search.signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::search_changed));
	_show_combo.signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::search_changed));
	_new_macro_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::new_macro));
	_delete_macro_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::delete_macro));
	_name_entry.signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::name_changed));
	_shortcut_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::start_key_capture));
	_clear_shortcut_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::clear_shortcut));
	_reset_shortcut_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::reset_shortcut));
	_scope_combo.signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::scope_changed));
	_cycle_button.signal_toggled ().connect (sigc::mem_fun (*this, &MacroEditor::cycle_toggled));
	_mode_combo.signal_changed ().connect (sigc::mem_fun (*this, &MacroEditor::mode_changed));
	_add_step_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::add_step));
	_remove_step_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::remove_step));
	_up_button.signal_clicked ().connect (sigc::bind (sigc::mem_fun (*this, &MacroEditor::move_step), -1));
	_down_button.signal_clicked ().connect (sigc::bind (sigc::mem_fun (*this, &MacroEditor::move_step), 1));
	_record_button.signal_toggled ().connect (sigc::mem_fun (*this, &MacroEditor::record_toggled));
	_run_button.signal_clicked ().connect (sigc::mem_fun (*this, &MacroEditor::run_selected));

	HBox* set_box = manage (new HBox);
	set_box->set_spacing (6);
	set_box->pack_start (*manage (new Label (_("Macro set:"))), false, false);
	set_box->pack_start (_set_combo, false, false);
	set_box->pack_start (_new_set_button, false, false);
	set_box->pack_start (_copy_set_button, false, false);
	set_box->pack_start (_rename_set_button, false, false);
	set_box->pack_start (_remove_set_button, false, false);

	/* left: search and the list */
	HBox* search_box = manage (new HBox);
	search_box->set_spacing (6);
	search_box->pack_start (*manage (new Label (_("Search:"))), false, false);
	search_box->pack_start (_search, true, true);
	search_box->pack_start (*manage (new Label (_("Show:"))), false, false);
	search_box->pack_start (_show_combo, false, false);

	ScrolledWindow* list_scroller = manage (new ScrolledWindow);
	list_scroller->set_policy (POLICY_AUTOMATIC, POLICY_AUTOMATIC);
	list_scroller->add (_list_view);

	HBox* list_buttons = manage (new HBox);
	list_buttons->set_spacing (6);
	list_buttons->pack_start (_new_macro_button, false, false);
	list_buttons->pack_start (_delete_macro_button, false, false);

	VBox* left = manage (new VBox);
	left->set_spacing (6);
	left->pack_start (*search_box, false, false);
	left->pack_start (*list_scroller, true, true);
	left->pack_start (*list_buttons, false, false);
	left->set_size_request (560, -1);

	/* right: the selected macro or shortcut */
	_kind_label.set_alignment (0.0, 0.5);
	/* shown or hidden per selection, not by show_all () */
	_reset_shortcut_button.set_no_show_all (true);
	_scope_combo.set_no_show_all (true);
	_scope_label.set_no_show_all (true);
	_scope_combo.show ();

	Table* details = manage (new Table (3, 5));
	details->set_spacings (6);
	Label* label = manage (new Label (_("Name:")));
	label->set_alignment (1.0, 0.5);
	details->attach (*label, 0, 1, 0, 1, FILL, SHRINK);
	details->attach (_name_entry, 1, 5, 0, 1, EXPAND | FILL, SHRINK);

	label = manage (new Label (_("Shortcut:")));
	label->set_alignment (1.0, 0.5);
	_shortcut_label.set_alignment (0.0, 0.5);
	details->attach (*label, 0, 1, 1, 2, FILL, SHRINK);
	details->attach (_shortcut_label, 1, 2, 1, 2, EXPAND | FILL, SHRINK);
	details->attach (_shortcut_button, 2, 3, 1, 2, FILL, SHRINK);
	details->attach (_clear_shortcut_button, 3, 4, 1, 2, FILL, SHRINK);
	details->attach (_reset_shortcut_button, 4, 5, 1, 2, FILL, SHRINK);

	label = manage (new Label (_("Works in:")));
	label->set_alignment (1.0, 0.5);
	HBox* scope_box = manage (new HBox);
	scope_box->set_spacing (6);
	scope_box->pack_start (_scope_combo, false, false);
	scope_box->pack_start (_scope_label, false, false);
	details->attach (*label, 0, 1, 2, 3, FILL, SHRINK);
	details->attach (*scope_box, 1, 5, 2, 3, EXPAND | FILL, SHRINK);

	/* fixed size, so messages never move the buttons */
	_status.set_alignment (0.0, 0.0);
	_status.set_line_wrap (true);
	_status.set_size_request (440, 54);

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

	HBox* run_buttons = manage (new HBox);
	run_buttons->set_spacing (6);
	run_buttons->pack_start (_record_button, false, false);
	run_buttons->pack_start (_run_button, false, false);

	Label* steps_title = manage (new Label (_("Steps, run in order:")));
	steps_title->set_alignment (0.0, 0.5);

	VBox* right = manage (new VBox);
	right->set_spacing (6);
	right->pack_start (_kind_label, false, false);
	right->pack_start (*details, false, false);
	right->pack_start (_status, false, false);
	right->pack_start (_cycle_button, false, false);
	right->pack_start (*steps_title, false, false);
	right->pack_start (*step_scroller, true, true);
	right->pack_start (*step_buttons, false, false);
	right->pack_start (*run_buttons, false, false);

	HBox* main_box = manage (new HBox);
	main_box->set_spacing (12);
	main_box->pack_start (*left, true, true);
	main_box->pack_start (*right, true, true);

	Label* help = manage (new Label (_("Every macro and every shortcut is listed here. Pick a shortcut to change its key, or add steps to turn it into a macro on the same key. \"Works in\" makes a key work everywhere or on one page only, so one key can do something different on each page. A cycle runs one step per press. Control surfaces run macro N through the action Macros/macro-N.")));
	help->set_alignment (0.0, 0.5);
	help->set_line_wrap (true);
	help->set_size_request (1060, -1);

	VBox* vbox = manage (new VBox);
	vbox->set_spacing (8);
	vbox->set_border_width (8);
	vbox->pack_start (*set_box, false, false);
	vbox->pack_start (*main_box, true, true);
	vbox->pack_start (*help, false, false);
	add (*vbox);
	vbox->show_all ();

	set_default_size (1120, 720);

	MacroManager::instance ().Changed.connect (sigc::mem_fun (*this, &MacroEditor::redisplay));

	redisplay ();

	/* start on the first macro */
	TreeModel::Children rows = _list_store->children ();
	if (!rows.empty () && !rows.begin ()->children ().empty ()) {
		_list_view.get_selection ()->select (rows.begin ()->children ().begin ());
	}
}

bool
MacroEditor::matches (string const& haystack) const
{
	string const words = lower (_search.get_text ());
	if (words.empty ()) {
		return true;
	}
	string const hay = lower (haystack);
	istringstream stream (words);
	string word;
	while (stream >> word) {
		if (hay.find (word) == string::npos) {
			return false;
		}
	}
	return true;
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

	fill_list ();
	show_selection ();
}

void
MacroEditor::fill_list ()
{
	MacroManager& mm (MacroManager::instance ());
	int const show = _show_combo.get_active_row_number ();

	_list_store->clear ();

	if (show != 2) {
		TreeModel::Row heading = *_list_store->append ();
		heading[_list_columns.kind]   = Heading;
		heading[_list_columns.slot]   = -1;
		heading[_list_columns.name]   = string_compose (_("Macros in \"%1\""), mm.current_set_name ());
		heading[_list_columns.weight] = bold;

		for (int slot = 0; slot < mm.slot_count (); ++slot) {
			MacroManager::Macro const& m (mm.macro (slot));
			if (m.empty ()) {
				continue;
			}
			KeyboardKey const key = mm.shortcut (slot);
			string const where = MacroManager::scope_label (mm.shortcut_scope (slot));
			size_t const n = m.steps.size ();
			string steps = n == 1 ? string (_("1 step")) : string_compose (_("%1 steps"), n);
			if (m.cycle) {
				steps = string_compose (_("cycle, %1"), steps);
			}
			if (!matches (string_compose ("%1 %2 %3 %4", m.name, key_text (key), where, slot + 1))) {
				continue;
			}
			TreeModel::Row row = *_list_store->append (heading.children ());
			row[_list_columns.kind]     = MacroRow;
			row[_list_columns.slot]     = slot;
			row[_list_columns.number]   = string_compose ("%1", slot + 1);
			row[_list_columns.name]     = m.name;
			row[_list_columns.shortcut] = key_text (key);
			row[_list_columns.where]    = key_text (key).empty () ? string () : where;
			row[_list_columns.steps]    = steps;
			row[_list_columns.weight]   = normal;
		}
	}

	if (show != 1) {
		vector<MacroManager::Shortcut> shortcuts = mm.shortcuts ();
		sort (shortcuts.begin (), shortcuts.end (), ShortcutOrder ());

		TreeModel::Row heading = *_list_store->append ();
		heading[_list_columns.kind]   = Heading;
		heading[_list_columns.slot]   = -1;
		heading[_list_columns.name]   = _("Shortcuts");
		heading[_list_columns.weight] = bold;

		string group_set;
		TreeModel::iterator group;

		for (vector<MacroManager::Shortcut>::const_iterator s = shortcuts.begin (); s != shortcuts.end (); ++s) {
			string const label = s->label;
			string const where = MacroManager::set_label (s->set);
			string key = key_text (s->key);
			KeyboardKey dk;
			if (key.empty () && KeyboardKey::make_key (s->default_key, dk)) {
				key = string_compose (_("None (was %1)"), dk.display_label ());
			}
			if (!matches (string_compose ("%1 %2 %3 %4", label, key, where, s->action))) {
				continue;
			}
			if (!group || group_set != s->set) {
				group = _list_store->append (heading.children ());
				(*group)[_list_columns.kind]   = Heading;
				(*group)[_list_columns.slot]   = -1;
				(*group)[_list_columns.name]   = where;
				(*group)[_list_columns.weight] = bold;
				group_set = s->set;
			}
			TreeModel::Row row = *_list_store->append (group->children ());
			row[_list_columns.kind]     = ShortcutRow;
			row[_list_columns.slot]     = -1;
			row[_list_columns.set]      = s->set;
			row[_list_columns.action]   = s->action;
			row[_list_columns.name]     = label;
			row[_list_columns.shortcut] = key;
			row[_list_columns.where]    = where;
			row[_list_columns.steps]    = s->default_key.empty () ? string (_("1 step")) : string (_("1 step, key changed"));
			row[_list_columns.weight]   = normal;
			row[_list_columns.keyname]  = (s->key == KeyboardKey::null_key ()) ? string () : s->key.name ();
			row[_list_columns.defkey]   = s->default_key;
			row[_list_columns.runnable] = s->runnable;
		}
	}

	_list_view.expand_all ();

	/* keep the same entry selected */
	if (!select_row (_list_store->children ())) {
		_list_view.get_selection ()->unselect_all ();
	}
}

bool
MacroEditor::select_row (TreeModel::Children rows)
{
	for (TreeModel::Children::iterator r = rows.begin (); r != rows.end (); ++r) {
		int const kind = (*r)[_list_columns.kind];
		bool found = false;
		if (kind == MacroRow && _kind == MacroRow) {
			int const slot = (*r)[_list_columns.slot];
			found = slot == _slot;
		} else if (kind == ShortcutRow && _kind == ShortcutRow) {
			string const set = (*r)[_list_columns.set];
			string const action = (*r)[_list_columns.action];
			string const keyname = (*r)[_list_columns.keyname];
			string const defkey = (*r)[_list_columns.defkey];
			found = set == _set && action == _action && keyname == _key && defkey == _default;
		}
		if (found) {
			_list_view.get_selection ()->select (r);
			_list_view.scroll_to_row (_list_store->get_path (r));
			return true;
		}
		if (select_row (r->children ())) {
			return true;
		}
	}
	return false;
}

string
MacroEditor::selected_scope () const
{
	int const n = _scope_combo.get_active_row_number ();
	vector<pair<string, string> > const scopes = MacroManager::scopes ();
	if (n < 0 || n >= (int) scopes.size ()) {
		return (_kind == MacroRow && _slot >= 0) ? MacroManager::instance ().shortcut_scope (_slot) : string (X_("Global"));
	}
	return scopes[n].first;
}

void
MacroEditor::show_selection ()
{
	MacroManager& mm (MacroManager::instance ());
	PBD::Unwinder<bool> uw (_updating, true);

	bool const is_macro    = _kind == MacroRow && _slot >= 0;
	bool const is_shortcut = _kind == ShortcutRow && !_action.empty ();
	bool const have        = is_macro || is_shortcut;

	_name_entry.set_sensitive (is_macro);
	_shortcut_button.set_sensitive (have && !_capturing_key);
	_add_step_button.set_sensitive (have);
	_record_button.set_sensitive (have);
	_run_button.set_sensitive (false);
	_delete_macro_button.set_sensitive (is_macro);
	_cycle_button.set_sensitive (is_macro);
	_reset_shortcut_button.set_visible (is_shortcut);
	_scope_combo.set_visible (!is_shortcut);
	_scope_combo.set_sensitive (is_macro);
	_scope_label.set_visible (is_shortcut);

	int const selected = selected_step ();
	_step_store->clear ();

	KeyboardKey key = KeyboardKey::null_key ();

	if (is_macro) {
		MacroManager::Macro const& m (mm.macro (_slot));
		_kind_label.set_markup (string_compose (_("<b>Macro %1</b>"), _slot + 1));
		if (_name_entry.get_text () != m.name) {
			_name_entry.set_text (m.name);
		}
		key = mm.shortcut (_slot);

		vector<pair<string, string> > const scopes = MacroManager::scopes ();
		string const scope = mm.shortcut_scope (_slot);
		bool listed = false;
		_scope_combo.set_active (-1);
		for (size_t i = 0; i < scopes.size (); ++i) {
			if (scopes[i].first == scope) {
				_scope_combo.set_active (i);
				listed = true;
			}
		}
		/* made from a shortcut of another page, such as MIDI */
		if (!listed) {
			_scope_label.set_text (string_compose (_("%1 (pick another to move it)"), MacroManager::scope_label (scope)));
			_scope_label.show ();
		}

		_cycle_button.set_active (m.cycle);

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

	} else if (is_shortcut) {
		_kind_label.set_markup (string_compose (_("<b>Shortcut</b>  %1"), Glib::Markup::escape_text (_action)));
		_name_entry.set_text (_label);

		if (!KeyboardKey::make_key (_key, key)) {
			key = KeyboardKey::null_key ();
		}

		_scope_label.set_text (_runnable
		                       ? string_compose (_("%1 (the page this shortcut belongs to)"), MacroManager::set_label (_set))
		                       : string_compose (_("%1: copied into every editor, so it can't run on its own or become a macro"), MacroManager::set_label (_set)));
		_add_step_button.set_sensitive (_runnable);
		_record_button.set_sensitive (_runnable);
		_reset_shortcut_button.set_sensitive (!_default.empty ());
		_cycle_button.set_active (false);

		TreeModel::Row row = *_step_store->append ();
		row[_step_columns.index]  = 0;
		row[_step_columns.number] = X_("1");
		row[_step_columns.action] = _label;
		row[_step_columns.does]   = does_text (MacroManager::Step (_action, MacroManager::Run));

		_record_button.set_active (false);
		_record_button.set_label (_("Record"));
		_run_button.set_sensitive (_runnable);

	} else {
		_kind_label.set_markup (_("<b>Pick a macro or a shortcut</b>"));
		_name_entry.set_text (string ());
		_scope_combo.set_active (-1);
		_cycle_button.set_active (false);
		_record_button.set_active (false);
		_record_button.set_label (_("Record"));
	}

	if (!_capturing_key) {
		_shortcut_label.set_text (have ? (key == KeyboardKey::null_key () ? string (_("None")) : key.display_label ()) : string ());
	}
	_clear_shortcut_button.set_sensitive (have && !(key == KeyboardKey::null_key ()));

	update_step_buttons ();
}

void
MacroEditor::update_step_buttons ()
{
	PBD::Unwinder<bool> uw (_updating, true);
	bool const is_macro = _kind == MacroRow && _slot >= 0;
	int const step = is_macro ? selected_step () : -1;
	int const count = _step_store->children ().size ();

	_remove_step_button.set_sensitive (step >= 0);
	_up_button.set_sensitive (step > 0);
	_down_button.set_sensitive (step >= 0 && step < count - 1);

	if (step < 0) {
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
MacroEditor::list_selection_changed ()
{
	if (_updating) {
		return;
	}
	stop_key_capture ();

	TreeModel::iterator i = _list_view.get_selection ()->get_selected ();
	if (!i) {
		_kind = Heading;
		_slot = -1;
		_set.clear ();
		_action.clear ();
		_key.clear ();
		_default.clear ();
		_label.clear ();
		_runnable = false;
	} else {
		_kind   = (RowKind) (int) (*i)[_list_columns.kind];
		_slot   = (*i)[_list_columns.slot];
		_set     = (string) (*i)[_list_columns.set];
		_action  = (string) (*i)[_list_columns.action];
		_key      = (string) (*i)[_list_columns.keyname];
		_default  = (string) (*i)[_list_columns.defkey];
		_label    = (string) (*i)[_list_columns.name];
		_runnable = (*i)[_list_columns.runnable];
	}
	_status.set_text (string ());
	show_selection ();
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
MacroEditor::search_changed ()
{
	if (_updating) {
		return;
	}
	PBD::Unwinder<bool> uw (_updating, true);
	fill_list ();
}

void
MacroEditor::name_changed ()
{
	if (_updating || _kind != MacroRow || _slot < 0) {
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
	if (_updating || _kind != MacroRow || _slot < 0 || step < 0 || mode < 0) {
		return;
	}
	MacroManager::Macro m = MacroManager::instance ().macro (_slot);
	m.steps[step].mode = (MacroManager::StepMode) mode;
	MacroManager::instance ().set_macro (_slot, m);
	select_step (step);
}

void
MacroEditor::cycle_toggled ()
{
	if (_updating || _kind != MacroRow || _slot < 0) {
		return;
	}
	MacroManager::Macro m = MacroManager::instance ().macro (_slot);
	m.cycle = _cycle_button.get_active ();
	MacroManager::instance ().set_macro (_slot, m);
	_status.set_text (m.cycle ? _("Each press now runs the next step, skipping steps that would change nothing (like showing the page you are already on).") : _("Each press now runs every step in order."));
}

bool
MacroEditor::take_key_from_holder (KeyboardKey const& key, string const& scope, string const& self)
{
	MacroManager& mm (MacroManager::instance ());
	string const holder = mm.key_holder (key, scope, self);
	if (holder.empty ()) {
		return true;
	}

	string const holder_label = MacroManager::action_label (holder);
	ArdourMessageDialog msg (*this,
	                         string_compose (_("%1 is the shortcut for \"%2\" (%3).\n\nUse it here instead? \"%2\" will have no shortcut there. You can give it one again later, or use Reset to Default."),
	                                         key.display_label (), holder_label, MacroManager::scope_label (scope)),
	                         false, MESSAGE_QUESTION, BUTTONS_NONE, true);
	msg.add_button (Stock::CANCEL, RESPONSE_CANCEL);
	msg.add_button (_("Use It Here"), RESPONSE_ACCEPT);
	msg.set_default_response (RESPONSE_ACCEPT);

	if (msg.run () != RESPONSE_ACCEPT) {
		_status.set_text (string_compose (_("%1 still runs \"%2\"."), key.display_label (), holder_label));
		return false;
	}
	mm.take_key (key, scope, self);
	return true;
}

void
MacroEditor::start_key_capture ()
{
	if (_kind == Heading) {
		return;
	}
	_capturing_key = true;
	_shortcut_label.set_text (_("Press the key now (Esc cancels)"));
	_shortcut_button.set_sensitive (false);
	_status.set_text (_("Any key works, with or without Ctrl, Alt or Shift: Tab, a letter, F5..."));
}

void
MacroEditor::stop_key_capture ()
{
	if (!_capturing_key) {
		return;
	}
	_capturing_key = false;
	show_selection ();
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
		_status.set_text (string ());
		return true;
	}

	GdkModifierType const mod = (GdkModifierType) (ev->state & Keyboard::RelevantModifierKeyMask);
	KeyboardKey const key (mod, gdk_keyval_to_lower (ev->keyval));

	stop_key_capture ();
	use_key (key);
	return true;
}

bool
MacroEditor::take_key_for (KeyboardKey const& key, string const& scope, string const& self, string const& what)
{
	MacroManager& mm (MacroManager::instance ());
	vector<MacroManager::KeyUse> const uses = mm.key_uses (key, self);
	if (uses.empty ()) {
		return true;
	}

	string here_label;
	string list;
	for (vector<MacroManager::KeyUse>::const_iterator u = uses.begin (); u != uses.end (); ++u) {
		if (u->set == scope) {
			here_label = u->label;
		}
		list += string_compose (_("\n    \"%1\" (%2)"), u->label, MacroManager::set_label (u->set));
	}

	bool const only_here = uses.size () == 1 && !here_label.empty ();
	string text = string_compose (_("%1 already does:%2\n\n"), key.display_label (), list);
	if (only_here) {
		text += string_compose (_("Use it for %1 instead? \"%2\" loses it; Reset to Default gives it back."), what, here_label);
	} else {
		text += string_compose (_("A page's or an editor's own key wins over a key that works everywhere. \"Take It From All\" makes %1 run %2 everywhere; Reset to Default gives each key back."), key.display_label (), what);
	}

	ArdourMessageDialog msg (*this, text, false, MESSAGE_QUESTION, BUTTONS_NONE, true);
	msg.add_button (Stock::CANCEL, RESPONSE_CANCEL);
	if (only_here) {
		msg.add_button (_("Use It Here"), RESPONSE_YES);
	} else {
		msg.add_button (here_label.empty () ? _("Keep Theirs") : string_compose (_("Take It From \"%1\" Only"), here_label), RESPONSE_NO);
		msg.add_button (_("Take It From All"), RESPONSE_YES);
	}
	msg.set_default_response (RESPONSE_YES);

	int const r = msg.run ();
	if (r == RESPONSE_YES) {
		for (vector<MacroManager::KeyUse>::const_iterator u = uses.begin (); u != uses.end (); ++u) {
			mm.take_key (key, u->set, self);
		}
		return true;
	}
	if (r == RESPONSE_NO) {
		mm.take_key (key, scope, self);
		return true;
	}
	_status.set_text (string_compose (_("%1 is unchanged."), key.display_label ()));
	return false;
}

void
MacroEditor::use_key (KeyboardKey const& key)
{
	MacroManager& mm (MacroManager::instance ());
	string scope;
	string self;
	string what;

	if (_kind == MacroRow && _slot >= 0) {
		scope = selected_scope ();
		self  = MacroManager::action_path (_slot);
		what  = _("this macro");
	} else if (_kind == ShortcutRow && !_action.empty ()) {
		scope = _set;
		self  = _action;
		what  = string_compose (_("\"%1\""), _label);
	} else {
		return;
	}

	if (!take_key_for (key, scope, self, what)) {
		return;
	}

	if (_kind == MacroRow) {
		mm.set_shortcut (_slot, key, scope);
	} else {
		change_builtin_key (key);
	}

	string done = string_compose (_("%1 now runs %2 (%3)."), key.display_label (), what, MacroManager::set_label (scope));
	vector<MacroManager::KeyUse> const left = mm.key_uses (key, self);
	if (!left.empty ()) {
		done += " " + string_compose (_("It still does %1 other things where their own key wins."), left.size ());
	}
	_status.set_text (done + " " + _("Menus show new shortcuts after a restart."));
}

void
MacroEditor::scope_changed ()
{
	if (_updating || _kind != MacroRow || _slot < 0) {
		return;
	}
	MacroManager& mm (MacroManager::instance ());
	string const scope = selected_scope ();
	KeyboardKey const key = mm.shortcut (_slot);

	if (!(key == KeyboardKey::null_key ()) && !take_key_from_holder (key, scope, MacroManager::action_path (_slot))) {
		show_selection ();
		return;
	}
	mm.set_shortcut_scope (_slot, scope);
	_status.set_text (scope == X_("Global") ? string (_("The shortcut works on every page.")) : string_compose (_("The shortcut works only on the %1 page, where it wins over a key that works everywhere."), MacroManager::scope_label (scope)));
}

void
MacroEditor::clear_shortcut ()
{
	MacroManager& mm (MacroManager::instance ());
	if (_kind == MacroRow && _slot >= 0) {
		mm.clear_shortcut (_slot);
	} else if (_kind == ShortcutRow && !_action.empty ()) {
		change_builtin_key (KeyboardKey::null_key ());
		_status.set_text (string_compose (_("\"%1\" has no shortcut now. It stays in the list, and Reset to Default gives its key back."), _label));
	}
}

void
MacroEditor::reset_shortcut ()
{
	if (_kind != ShortcutRow || _action.empty ()) {
		return;
	}
	string const def = _default;
	string const key = _key;
	_key     = def;
	_default.clear ();
	string const problem = MacroManager::instance ().reset_builtin_key (_set, _action, def);
	if (!problem.empty ()) {
		_key     = key;
		_default = def;
		redisplay ();
	}
	_status.set_text (problem.empty () ? string (_("Back to its default key.")) : problem);
}

int
MacroEditor::make_macro_from_shortcut ()
{
	MacroManager& mm (MacroManager::instance ());
	string const set = _set;
	string const action = _action;
	string const def = _default;
	string const label = _label;

	KeyboardKey key;
	if (!KeyboardKey::make_key (_key, key)) {
		key = KeyboardKey::null_key ();
	}

	vector<MacroManager::Step> steps;
	steps.push_back (MacroManager::Step (action, MacroManager::Run));
	int const slot = mm.new_macro (label, steps);
	if (slot < 0) {
		return -1;
	}

	_kind = MacroRow;
	_slot = slot;
	_set.clear ();
	_action.clear ();
	_key.clear ();
	_default.clear ();

	/* the key moves to the macro, on the same page */
	if (!(key == KeyboardKey::null_key ())) {
		mm.set_builtin_key (set, action, key, KeyboardKey::null_key (), def);
		mm.set_shortcut (slot, key, set);
	}

	redisplay ();

	_status.set_text (key == KeyboardKey::null_key ()
	                  ? string_compose (_("\"%1\" is now macro %2."), label, slot + 1)
	                  : string_compose (_("\"%1\" is now macro %2, still on %3 (%4)."), label, slot + 1, key.display_label (), MacroManager::scope_label (set)));
	return slot;
}

void
MacroEditor::change_builtin_key (KeyboardKey const& key)
{
	KeyboardKey old;
	if (!KeyboardKey::make_key (_key, old)) {
		old = KeyboardKey::null_key ();
	}
	string const def = _default;

	/* the entry's new identity, so the list keeps it selected */
	string const new_name = (key == KeyboardKey::null_key ()) ? string () : key.name ();
	string new_default = def.empty () ? _key : def;
	if (new_name == new_default) {
		new_default.clear ();
	}
	_key     = new_name;
	_default = new_default;

	MacroManager::instance ().set_builtin_key (_set, _action, old, key, def);
}

void
MacroEditor::new_macro ()
{
	int const slot = MacroManager::instance ().new_macro (string ());
	if (slot < 0) {
		return;
	}
	_kind = MacroRow;
	_slot = slot;
	_search.set_text (string ());
	redisplay ();
	_name_entry.grab_focus ();
	_status.set_text (_("Name it, give it a shortcut, then add steps or record them."));
}

void
MacroEditor::delete_macro ()
{
	if (_kind != MacroRow || _slot < 0) {
		return;
	}
	ArdourMessageDialog msg (*this, string_compose (_("Delete macro %1, \"%2\"?"), _slot + 1, MacroManager::instance ().macro (_slot).name), false, MESSAGE_QUESTION, BUTTONS_YES_NO, true);
	if (msg.run () != RESPONSE_YES) {
		return;
	}
	int const slot = _slot;
	_kind = Heading;
	_slot = -1;
	MacroManager::instance ().delete_macro (slot);
}

void
MacroEditor::add_step ()
{
	if (_kind == Heading) {
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

	if (_kind == ShortcutRow && make_macro_from_shortcut () < 0) {
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
	if (_kind != MacroRow || _slot < 0 || step < 0) {
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
	if (_kind != MacroRow || _slot < 0 || step < 0) {
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

	if (_kind == ShortcutRow) {
		if (make_macro_from_shortcut () < 0) {
			PBD::Unwinder<bool> uw (_updating, true);
			_record_button.set_active (false);
			return;
		}
	}

	if (_kind != MacroRow || _slot < 0) {
		PBD::Unwinder<bool> uw (_updating, true);
		_record_button.set_active (false);
		return;
	}

	mm.start_recording (_slot);
	_status.set_text (string_compose (_("Recording macro %1: every menu item, shortcut or button action you use is added as a step. Click Stop Recording to finish."), _slot + 1));
}

void
MacroEditor::run_selected ()
{
	if (_kind == MacroRow && _slot >= 0) {
		MacroManager::instance ().run (_slot);
	} else if (_kind == ShortcutRow && !_action.empty ()) {
		Glib::RefPtr<Action> act = ActionManager::get_action (_action, false);
		if (act) {
			act->activate ();
		}
	}
}

bool
MacroEditor::on_delete_event (GdkEventAny* ev)
{
	stop_key_capture ();
	MacroManager::instance ().stop_recording ();
	return ArdourWindow::on_delete_event (ev);
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
