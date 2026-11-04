/*
 * Copyright (C) 2005-2018 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2013-2019 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2014-2018 Ben Loftis <ben@harrisonconsoles.com>
 *
 * Moved out of mixer_ui.cc for Spite.
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

#ifdef WAF_BUILD
#include "gtk2ardour-config.h"
#endif

#include <ytkmm/menu.h>

#include "pbd/unwind.h"

#include "ardour/amp.h"
#include "ardour/plugin_insert.h"
#include "ardour/plugin_manager.h"
#include "ardour/route.h"
#include "ardour/rc_configuration.h"
#include "ardour/selection.h"
#include "ardour/session.h"

#include "widgets/tooltips.h"

#include "ardour_ui.h"
#include "gui_thread.h"
#include "plugin_browser.h"
#include "plugin_utils.h"
#include "ui_config.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ARDOUR_PLUGIN_UTILS;
using namespace ArdourWidgets;
using namespace PBD;
using namespace Gtk;
using namespace std;

std::list<std::string>                   PluginBrowser::_favorite_order;
std::map<std::string, bool>              PluginBrowser::_favorite_expanded;
bool                                     PluginBrowser::_favorite_order_loaded = false;
sigc::signal<void, PluginBrowser const*> PluginBrowser::FavoriteOrderChanged;

PluginBrowser::PluginBrowser (bool mode_bar)
	: _mode (Favorite)
	, _with_mode_bar (mode_bar)
	, _ignore_refill (false)
	, _ignore_reorder (false)
	, _search_clear_button (X_("Clear"))
{
	load_favorite_order ();

	std::list<TargetEntry> target_list;
	target_list.push_back (TargetEntry ("x-ardour/plugin.preset", Gtk::TARGET_SAME_APP));

	_model = PluginTreeStore::create (_columns);
	_display.set_model (_model);
	_display.append_column (_("Favorite Plugins"), _columns.name);
	_display.set_name ("EditGroupList");
	_display.get_selection ()->set_mode (Gtk::SELECTION_SINGLE);
	_display.set_reorderable (false);
	_display.set_headers_visible (false);
	_display.set_rules_hint (true);
	_display.set_can_focus (false);
	_display.add_object_drag (_columns.plugin.index (), "x-ardour/plugin.favorite", Gtk::TARGET_SAME_APP);
	_display.set_drag_column (_columns.name.index ());
	_display.add_drop_targets (target_list);
	_display.signal_row_activated ().connect (sigc::mem_fun (*this, &PluginBrowser::row_activated));
	_display.signal_button_press_event ().connect (sigc::mem_fun (*this, &PluginBrowser::row_button_press), false);
	_display.signal_drop.connect (sigc::mem_fun (*this, &PluginBrowser::plugin_drop));
	_display.signal_motion.connect (sigc::mem_fun (*this, &PluginBrowser::plugin_drag_motion));
	_display.signal_row_expanded ().connect (sigc::mem_fun (*this, &PluginBrowser::save_row_expanded));
	_display.signal_row_collapsed ().connect (sigc::mem_fun (*this, &PluginBrowser::save_row_expanded));
	if (UIConfiguration::instance ().get_use_tooltips ()) {
		_display.set_tooltip_column (0);
	}
	_model->signal_row_has_child_toggled ().connect (sigc::mem_fun (*this, &PluginBrowser::sync_row_expanded));
	_model->signal_row_deleted ().connect (sigc::mem_fun (*this, &PluginBrowser::rows_deleted));

	_search_entry.signal_changed ().connect (sigc::mem_fun (*this, &PluginBrowser::search_entry_changed));
	_search_clear_button.signal_clicked.connect ([this] () { _search_entry.set_text (""); });

	_scroller.add (_display);
	_scroller.set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

	_search_box.set_spacing (2);
	_search_box.pack_start (_search_entry, true, true);
	_search_box.pack_start (_search_clear_button, false, false);

	set_spacing (3);

	if (_with_mode_bar) {
		/* short labels, the sidebar is narrow */
		const char* labels[] = { _("Favorites"), _("Recent"), _("Top"), _("Search") };
		_mode_box.set_spacing (2);
		_mode_box.set_homogeneous (true);
		for (int i = 0; i < 4; ++i) {
			_mode_button[i].set_text (labels[i]);
			_mode_button[i].set_name ("generic button");
			_mode_button[i].signal_clicked.connect (sigc::bind (sigc::mem_fun (*this, &PluginBrowser::set_mode), (Mode) i));
			set_tooltip (_mode_button[i], mode_name ((Mode) i));
			_mode_box.pack_start (_mode_button[i], true, true);
		}
		pack_start (_mode_box, false, false);
		pack_start (_search_box, false, false);
		pack_start (_scroller, true, true);
		set_tooltip (_display, _("Double-click a plugin to add it to the selected tracks, or drag it onto a track's devices. Drag an instrument to an empty area to make a MIDI track with it."));
	} else {
		pack_start (_scroller, true, true);
		pack_start (_search_box, false, false);
	}

	_mode_box.show_all ();
	_search_box.show_all ();
	_scroller.show_all ();
	set_mode_buttons ();

	PluginManager::instance ().PluginListChanged.connect (*this, invalidator (*this), std::bind (&PluginBrowser::refill, this), gui_context ());
	ARDOUR::Plugin::PresetsChanged.connect (*this, invalidator (*this), std::bind (&PluginBrowser::refill, this), gui_context ());
	PluginManager::instance ().PluginStatusChanged.connect (*this, invalidator (*this), std::bind (&PluginBrowser::maybe_refill, this, Favorite), gui_context ());
	PluginManager::instance ().PluginStatsChanged.connect (*this, invalidator (*this), std::bind (&PluginBrowser::maybe_refill, this, Recent), gui_context ());
	FavoriteOrderChanged.connect (sigc::mem_fun (*this, &PluginBrowser::favorite_order_changed));
}

PluginBrowser::~PluginBrowser ()
{
}

void
PluginBrowser::set_session (Session* s)
{
	SessionHandlePtr::set_session (s);
	if (!_session) {
		PBD::Unwinder<bool> uw (_ignore_reorder, true);
		_model->clear ();
		return;
	}
	refill ();
}

std::string
PluginBrowser::mode_name (Mode m)
{
	switch (m) {
		case Favorite:
			return _("Favorite Plugins");
		case Recent:
			return _("Recent Plugins");
		case TopHits:
			return _("Top-10 Plugins");
		case SearchAll:
			return _("Search All Plugins");
	}
	return "";
}

void
PluginBrowser::set_mode_buttons ()
{
	if (!_with_mode_bar) {
		return;
	}
	for (int i = 0; i < 4; ++i) {
		_mode_button[i].set_active (i == (int)_mode);
	}
}

void
PluginBrowser::set_mode (Mode m)
{
	_mode = m;

	if (_mode == Favorite || _mode == SearchAll) {
		PBD::Unwinder<bool> uw (_ignore_refill, true);
		_search_box.show ();
		_search_entry.set_text ("");
	} else {
		_search_box.hide ();
	}

	set_mode_buttons ();
	refill ();

	if (_mode == SearchAll && _with_mode_bar && get_mapped ()) {
		_search_entry.grab_focus ();
	}

	ModeChanged (); /* EMIT SIGNAL */
}

void
PluginBrowser::search_entry_changed ()
{
	if (_mode == Favorite || _mode == SearchAll) {
		refill ();
	}
}

void
PluginBrowser::refiller (PluginInfoList& result, const PluginInfoList& plugs)
{
	PluginManager& manager (PluginManager::instance ());

	std::string searchstr = _search_entry.get_text ();
	setup_search_string (searchstr);

	for (PluginInfoList::const_iterator i = plugs.begin (); i != plugs.end (); ++i) {
		bool maybe_show = true;

		if (_mode == Favorite) {
			if (manager.get_status (*i) != PluginManager::Favorite) {
				maybe_show = false;
			}

			if (maybe_show && !searchstr.empty ()) {
				maybe_show = false;
				/* check name */
				std::string compstr = (*i)->name;
				setup_search_string (compstr);
				maybe_show |= match_search_strings (compstr, searchstr);
				/* check tags */
				std::string tags = manager.get_tags_as_string (*i);
				setup_search_string (tags);
				maybe_show |= match_search_strings (tags, searchstr);
			}
		} else if (_mode == SearchAll) {
			maybe_show = false;
			if (searchstr.length () > 2) {
				/* check name */
				std::string compstr = (*i)->name;
				setup_search_string (compstr);
				maybe_show |= match_search_strings (compstr, searchstr);
				/* check tags */
				std::string tags = manager.get_tags_as_string (*i);
				setup_search_string (tags);
				maybe_show |= match_search_strings (tags, searchstr);
			}
		} else {
			int64_t  lru;
			uint64_t use_count;
			if (!manager.stats (*i, lru, use_count)) {
				maybe_show = false;
			}
			if (_mode == Recent && lru == 0) {
				maybe_show = false;
			}
		}

		if (!maybe_show) {
			continue;
		}
		result.push_back (*i);
	}
}

void
PluginBrowser::refill ()
{
	if (_ignore_refill) {
		return;
	}

	PluginInfoList plugs;
	PluginManager& mgr (PluginManager::instance ());

#ifdef WINDOWS_VST_SUPPORT
	refiller (plugs, mgr.windows_vst_plugin_info ());
#endif
#ifdef LXVST_SUPPORT
	refiller (plugs, mgr.lxvst_plugin_info ());
#endif
#ifdef MACVST_SUPPORT
	refiller (plugs, mgr.mac_vst_plugin_info ());
#endif
#ifdef VST3_SUPPORT
	refiller (plugs, mgr.vst3_plugin_info ());
#endif
#ifdef AUDIOUNIT_SUPPORT
	refiller (plugs, mgr.au_plugin_info ());
#endif
	refiller (plugs, mgr.ladspa_plugin_info ());
	refiller (plugs, mgr.lv2_plugin_info ());
	refiller (plugs, mgr.lua_plugin_info ());

	switch (_mode) {
		default:
			/* use favorites & search all as-is */
			break;
		case TopHits: {
			PluginChartsSorter cmp;
			plugs.sort (cmp);
			plugs.resize (std::min (plugs.size (), size_t (UIConfiguration::instance ().get_max_plugin_chart ())));
		} break;
		case Recent: {
			PluginRecentSorter cmp;
			plugs.sort (cmp);
			plugs.resize (std::min (plugs.size (), size_t (10)));
			plugs.resize (std::min (plugs.size (), size_t (UIConfiguration::instance ().get_max_plugin_recent ())));
		} break;
	}
	_plugin_list = plugs;

	sync_treeview_from_favorite_order ();
}

void
PluginBrowser::maybe_refill (Mode m)
{
	switch (m) {
		case Favorite:
			if (_mode == Favorite) {
				refill ();
			}
			break;
		default:
			if (_mode != Favorite) {
				refill ();
			}
			break;
	}
}

void
PluginBrowser::sync_treeview_from_favorite_order ()
{
	PBD::Unwinder<bool> uw (_ignore_reorder, true);
	switch (_mode) {
		case Favorite: {
			PluginUIOrderSorter cmp (_favorite_order);
			_plugin_list.sort (cmp);
		} break;
		case TopHits:
		case SearchAll: {
			PluginABCSorter cmp;
			_plugin_list.sort (cmp);
		}
		case Recent:
			break;
	}

	_model->clear ();
	for (PluginInfoList::const_iterator i = _plugin_list.begin (); i != _plugin_list.end (); ++i) {
		PluginInfoPtr pip = (*i);

		TreeModel::Row newrow             = *(_model->append ());
		newrow[_columns.name]   = (*i)->name;
		newrow[_columns.plugin] = PluginPresetPtr (new PluginPreset (pip));
		if (!_session) {
			continue;
		}

		vector<ARDOUR::Plugin::PresetRecord> presets = (*i)->get_presets (true);
		for (vector<ARDOUR::Plugin::PresetRecord>::const_iterator j = presets.begin (); j != presets.end (); ++j) {
			if (!(*j).user) {
				continue;
			}
			Gtk::TreeModel::Row child_row     = *(_model->append (newrow.children ()));
			child_row[_columns.name]   = (*j).label;
			child_row[_columns.plugin] = PluginPresetPtr (new PluginPreset (pip, &(*j)));
		}
		if (_favorite_expanded.find (pip->unique_id) != _favorite_expanded.end ()) {
			if (_favorite_expanded[pip->unique_id]) {
				_display.expand_row (_model->get_path (newrow), true);
			}
		}
	}
}

void
PluginBrowser::store_current_favorite_order ()
{
	if (_mode != Favorite || !_search_entry.get_text ().empty ()) {
		return;
	}

	typedef Gtk::TreeModel::Children type_children;
	type_children children = _model->children ();
	_favorite_order.clear ();
	for (type_children::iterator iter = children.begin (); iter != children.end (); ++iter) {
		Gtk::TreeModel::Row     row = *iter;
		ARDOUR::PluginPresetPtr ppp = row[_columns.plugin];
		_favorite_order.push_back ((*ppp->_pip).unique_id);
		_favorite_expanded[(*ppp->_pip).unique_id] = _display.row_expanded (_model->get_path (iter));
	}
}

void
PluginBrowser::rows_deleted (const TreeModel::Path&)
{
	if (_ignore_reorder) {
		return;
	}
	/* re-order is implemented by insert; delete */
	store_current_favorite_order ();
	save_favorite_order ();
	FavoriteOrderChanged (this); /* EMIT SIGNAL */
}

void
PluginBrowser::favorite_order_changed (PluginBrowser const* src)
{
	if (src == this || _mode != Favorite) {
		return;
	}
	sync_treeview_from_favorite_order ();
}

void
PluginBrowser::save_row_expanded (const TreeModel::iterator& iter, const TreeModel::Path& path)
{
	Gtk::TreeModel::Row     row = *iter;
	ARDOUR::PluginPresetPtr ppp = row[_columns.plugin];
	assert (ppp);
	_favorite_expanded[(*ppp->_pip).unique_id] = _display.row_expanded (_model->get_path (iter));
}

void
PluginBrowser::sync_row_expanded (const TreeModel::Path& path, const TreeModel::iterator&)
{
	TreeIter iter;
	if (!(iter = _model->get_iter (path))) {
		return;
	}
	ARDOUR::PluginPresetPtr ppp = (*iter)[_columns.plugin];
	if (!ppp) {
		return;
	}
	PluginInfoPtr pip = ppp->_pip;
	if (_favorite_expanded.find (pip->unique_id) != _favorite_expanded.end ()) {
		if (_favorite_expanded[pip->unique_id]) {
			_display.expand_row (path, true);
		}
	}
}

void
PluginBrowser::load_favorite_order ()
{
	if (_favorite_order_loaded) {
		return;
	}
	_favorite_order_loaded = true;

	XMLNode plugin_order (X_("PO"));
	if (!PluginManager::instance ().load_plugin_order_file (plugin_order)) {
		return;
	}

	_favorite_order.clear ();
	const XMLNodeList& kids = plugin_order.children ("PluginInfo");
	for (XMLNodeConstIterator i = kids.begin (); i != kids.end (); ++i) {
		std::string unique_id;
		if ((*i)->get_property ("unique-id", unique_id)) {
			_favorite_order.push_back (unique_id);
			bool yn;
			if ((*i)->get_property ("expanded", yn)) {
				_favorite_expanded[unique_id] = yn;
			}
		}
	}
}

void
PluginBrowser::save_favorite_order ()
{
	XMLNode  plugin_order ("PluginOrder");
	uint32_t cnt = 0;
	for (std::list<std::string>::const_iterator i = _favorite_order.begin (); i != _favorite_order.end (); ++i, ++cnt) {
		XMLNode* p = new XMLNode ("PluginInfo");
		p->set_property ("sort", cnt);
		p->set_property ("unique-id", *i);
		if (_favorite_expanded.find (*i) != _favorite_expanded.end ()) {
			p->set_property ("expanded", _favorite_expanded[*i]);
		}
		plugin_order.add_child_nocopy (*p);
	}
	PluginManager::instance ().save_plugin_order_file (plugin_order);
}

void
PluginBrowser::popup_context_menu (GdkEventButton* ev)
{
	using namespace Gtk::Menu_Helpers;

	Gtk::Menu* m     = ARDOUR_UI::instance ()->shared_popup_menu ();
	MenuList&  items = m->items ();

	bool have_routes = false;
	if (_session) {
		CoreSelection::StripableAutomationControls sel;
		_session->selection ().get_stripables (sel);
		for (auto const& s : sel) {
			if (std::dynamic_pointer_cast<Route> (s.stripable)) {
				have_routes = true;
				break;
			}
		}
	}

	if (!have_routes) {
		items.push_back (MenuElem (_("No Track/Bus is selected.")));
	} else {
		items.push_back (MenuElem (_("Add at the top"), sigc::bind (sigc::mem_fun (*this, &PluginBrowser::add_selected), AddTop)));
		items.push_back (MenuElem (_("Add Pre-Fader"), sigc::bind (sigc::mem_fun (*this, &PluginBrowser::add_selected), AddPreFader)));
		items.push_back (MenuElem (_("Add Post-Fader"), sigc::bind (sigc::mem_fun (*this, &PluginBrowser::add_selected), AddPostFader)));
		items.push_back (MenuElem (_("Add at the end"), sigc::bind (sigc::mem_fun (*this, &PluginBrowser::add_selected), AddBottom)));
	}

	items.push_back (SeparatorElem ());

	items.push_back (MenuElem (_("Remove from favorites"), sigc::mem_fun (*this, &PluginBrowser::remove_selected_from_favorites)));

	ARDOUR::PluginPresetPtr ppp = selected_plugin ();
	if (ppp && ppp->_preset.valid && ppp->_preset.user) {
		// we cannot currently delete AU presets
		if (!ppp->_pip || ppp->_pip->type != AudioUnit) {
			items.push_back (MenuElem (_("Delete Preset"), sigc::mem_fun (*this, &PluginBrowser::delete_selected_preset)));
		}
	}

	m->popup (ev->button, ev->time);
}

bool
PluginBrowser::row_button_press (GdkEventButton* ev)
{
	if ((ev->type == GDK_BUTTON_PRESS) && (ev->button == 3)) {
		TreeModel::Path path;
		TreeViewColumn* column;
		int             cellx, celly;
		if (_display.get_path_at_pos ((int)ev->x, (int)ev->y, path, column, cellx, celly)) {
			Glib::RefPtr<Gtk::TreeView::Selection> selection = _display.get_selection ();
			if (selection) {
				selection->unselect_all ();
				selection->select (path);
			}
			ARDOUR::PluginPresetPtr ppp = selected_plugin ();
			if (ppp) {
				popup_context_menu (ev);
			}
		}
	}
	return false;
}

PluginPresetPtr
PluginBrowser::selected_plugin ()
{
	Glib::RefPtr<Gtk::TreeView::Selection> selection = _display.get_selection ();
	if (!selection) {
		return PluginPresetPtr ();
	}
	Gtk::TreeModel::iterator iter = selection->get_selected ();
	if (!iter) {
		return PluginPresetPtr ();
	}
	return (*iter)[_columns.plugin];
}

void
PluginBrowser::add_selected (Position pos)
{
	ARDOUR::PluginPresetPtr ppp = selected_plugin ();
	if (ppp) {
		add_to_selected_routes (ppp, pos);
	}
}

void
PluginBrowser::delete_selected_preset ()
{
	if (!_session) {
		return;
	}
	ARDOUR::PluginPresetPtr ppp = selected_plugin ();
	if (!ppp || !ppp->_preset.valid || !ppp->_preset.user) {
		return;
	}
	PluginPtr plugin = ppp->_pip->load (*_session);
	plugin->get_presets ();
	plugin->remove_preset (ppp->_preset.label);
}

void
PluginBrowser::remove_selected_from_favorites ()
{
	ARDOUR::PluginPresetPtr ppp = selected_plugin ();
	if (!ppp) {
		return;
	}
	PluginManager::PluginStatusType status = PluginManager::Normal;
	PluginManager&                  manager (PluginManager::instance ());

	manager.set_status (ppp->_pip->type, ppp->_pip->unique_id, status);
	manager.save_statuses ();
}

void
PluginBrowser::row_activated (const TreeModel::Path& path, TreeViewColumn* column)
{
	TreeIter iter;
	if (!(iter = _model->get_iter (path))) {
		return;
	}
	ARDOUR::PluginPresetPtr ppp = (*iter)[_columns.plugin];
	add_to_selected_routes (ppp, AddPreFader); // TODO: preference?!
}

void
PluginBrowser::add_to_selected_routes (ARDOUR::PluginPresetPtr ppp, Position pos)
{
	if (!_session || !ppp) {
		return;
	}

	/* the stripable selection is shared by the Editor, Mixer and Cue pages */
	CoreSelection::StripableAutomationControls sel;
	_session->selection ().get_stripables (sel);

	std::list<std::shared_ptr<Route>> routes;
	for (auto const& s : sel) {
		std::shared_ptr<Route> rt = std::dynamic_pointer_cast<Route> (s.stripable);
		if (rt && std::find (routes.begin (), routes.end (), rt) == routes.end ()) {
			routes.push_back (rt);
		}
	}

	PluginInfoPtr pip = ppp->_pip;
	for (auto const& rt : routes) {
		PluginPtr p = pip->load (*_session);

		if (!p) {
			continue;
		}

		if (ppp->_preset.valid) {
			p->load_preset (ppp->_preset);
		}

		Route::ProcessorStreams    err;
		std::shared_ptr<Processor> processor (new PluginInsert (*_session, *rt, p));

		switch (pos) {
			case AddTop:
				rt->add_processor_by_index (processor, 0, &err, Config->get_new_plugins_active ());
				break;
			case AddPreFader:
				rt->add_processor (processor, PreFader, &err, Config->get_new_plugins_active ());
				break;
			case AddPostFader: {
				int idx = 0;
				int pos = 0;
				for (;; ++idx) {
					std::shared_ptr<Processor> np = rt->nth_processor (idx);
					if (!np) {
						break;
					}
					if (!np->display_to_user ()) {
						continue;
					}
					if (std::dynamic_pointer_cast<Amp> (np) && // Fader, not Trim
					    std::dynamic_pointer_cast<Amp> (np)->gain_control ()->parameter ().type () == GainAutomation) {
						break;
					}
					++pos;
				}
				rt->add_processor_by_index (processor, ++pos, &err, Config->get_new_plugins_active ());
			} break;
			case AddBottom:
				rt->add_processor_by_index (processor, -1, &err, Config->get_new_plugins_active ());
				break;
		}
	}
}

bool
PluginTreeStore::row_drop_possible_vfunc (const Gtk::TreeModel::Path& dest, const Gtk::SelectionData& data) const
{
	if (data.get_target () != "GTK_TREE_MODEL_ROW") {
		return false;
	}

	// only allow to re-order top-level items
	TreePath src;
	if (TreePath::get_from_selection_data (data, src)) {
		if (src.up () && src.up ()) {
			return false;
		}
	}

	// don't allow to drop as child-rows.
	Gtk::TreeModel::Path _dest    = dest;        // un const
	const bool           is_child = _dest.up (); // explicit bool for clang
	if (!is_child || _dest.empty ()) {
		return true;
	}
	return false;
}

bool
PluginBrowser::plugin_drag_motion (const Glib::RefPtr<Gdk::DragContext>& ctx, int x, int y, guint time)
{
	std::string target = _display.drag_dest_find_target (ctx, _display.drag_dest_get_target_list ());

	if (target.empty ()) {
		ctx->drag_status (Gdk::DragAction (0), time);
		return false;
	}

	if (target == "GTK_TREE_MODEL_ROW") {
		if (_mode == Favorite && _search_entry.get_text ().empty ()) {
			/* re-order rows */
			ctx->drag_status (Gdk::ACTION_MOVE, time);
			return true;
		}
	} else if (target == "x-ardour/plugin.preset") {
		ctx->drag_status (Gdk::ACTION_COPY, time);
		return true;
	}

	ctx->drag_status (Gdk::DragAction (0), time);
	return false;
}

void
PluginBrowser::plugin_drop (const Glib::RefPtr<Gdk::DragContext>&, const Gtk::SelectionData& data)
{
	if (data.get_target () != "x-ardour/plugin.preset") {
		return;
	}
	if (data.get_length () != sizeof (PluginPresetPtr)) {
		return;
	}

	const void*           d   = data.get_data ();
	const PluginPresetPtr ppp = *(static_cast<const PluginPresetPtr*> (d));

	PluginManager::PluginStatusType status = PluginManager::Favorite;
	PluginManager&                  manager (PluginManager::instance ());

	manager.set_status (ppp->_pip->type, ppp->_pip->unique_id, status);
	manager.save_statuses ();

	/* switch to favorite tab */
	if (_mode != Favorite) {
		set_mode (Favorite);
	}
}
