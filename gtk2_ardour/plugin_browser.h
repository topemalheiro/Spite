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

#pragma once

#include <list>
#include <map>
#include <string>

#include <ytkmm/box.h>
#include <ytkmm/entry.h>
#include <ytkmm/scrolledwindow.h>
#include <ytkmm/treestore.h>

#include "pbd/signals.h"

#include "ardour/plugin.h"
#include "ardour/session_handle.h"

#include "gtkmm2ext/dndtreeview.h"

#include "widgets/ardour_button.h"

class PluginTreeStore : public Gtk::TreeStore
{
public:
	static Glib::RefPtr<PluginTreeStore> create(const Gtk::TreeModelColumnRecord& columns) {
		return Glib::RefPtr<PluginTreeStore> (new PluginTreeStore (columns));
	}

protected:
	PluginTreeStore (const Gtk::TreeModelColumnRecord& columns) : Gtk::TreeStore (columns) {}
	virtual bool row_draggable_vfunc (const Gtk::TreeModel::Path&) const { return true; }
	virtual bool row_drop_possible_vfunc (const Gtk::TreeModel::Path&, const Gtk::SelectionData&) const;
};

/** The plugin list of a page's browser (sidebar): favorite, recently used,
 * most used, or searched plugins with their user presets. Rows are dragged
 * as "x-ardour/plugin.favorite" onto strips' device lists, and
 * double-clicked to add the plugin to the selected tracks.
 *
 * The Mixer, Cue and Editor pages each have one. The favorite order and
 * the expanded rows are shared by all of them and kept in the plugin order
 * file.
 */
class PluginBrowser : public Gtk::VBox, public ARDOUR::SessionHandlePtr, public PBD::ScopedConnectionList
{
public:
	/* the values are stored in the Mixer's state, keep them */
	enum Mode {
		Favorite = 0,
		Recent,
		TopHits,
		SearchAll
	};

	enum Position {
		AddTop,
		AddPreFader,
		AddPostFader,
		AddBottom
	};

	/** @param mode_bar show buttons to switch the mode, with the search
	 * entry on top. The Mixer switches modes from its own sidebar pager
	 * instead, with the search entry at the bottom.
	 */
	PluginBrowser (bool mode_bar);
	~PluginBrowser ();

	void set_session (ARDOUR::Session*);

	Mode mode () const { return _mode; }
	void set_mode (Mode);
	static std::string mode_name (Mode);

	/** add a plugin (and preset) to the selected tracks and busses */
	void add_to_selected_routes (ARDOUR::PluginPresetPtr, Position);

	/** read the shared favorite order from the plugin order file */
	static void load_favorite_order ();
	/** write the shared favorite order to the plugin order file */
	static void save_favorite_order ();

	sigc::signal<void> ModeChanged;

private:
	struct Columns : public Gtk::TreeModel::ColumnRecord {
		Columns () {
			add (name);
			add (plugin);
		}
		Gtk::TreeModelColumn<std::string>             name;
		Gtk::TreeModelColumn<ARDOUR::PluginPresetPtr> plugin;
	};

	void set_mode_buttons ();
	void refiller (ARDOUR::PluginInfoList& result, const ARDOUR::PluginInfoList& plugs);
	void refill ();
	void maybe_refill (Mode);
	void sync_treeview_from_favorite_order ();
	void store_current_favorite_order ();
	void favorite_order_changed (PluginBrowser const*);

	void search_entry_changed ();
	void row_activated (const Gtk::TreeModel::Path&, Gtk::TreeViewColumn*);
	bool row_button_press (GdkEventButton*);
	void popup_context_menu (GdkEventButton*);
	void plugin_drop (const Glib::RefPtr<Gdk::DragContext>&, const Gtk::SelectionData&);
	bool plugin_drag_motion (Glib::RefPtr<Gdk::DragContext> const&, int, int, guint);
	void rows_deleted (const Gtk::TreeModel::Path&);
	void sync_row_expanded (const Gtk::TreeModel::Path&, const Gtk::TreeModel::iterator&);
	void save_row_expanded (const Gtk::TreeModel::iterator&, const Gtk::TreeModel::Path&);

	ARDOUR::PluginPresetPtr selected_plugin ();
	void add_selected (Position);
	void remove_selected_from_favorites ();
	void delete_selected_preset ();

	Mode                          _mode;
	bool                          _with_mode_bar;
	bool                          _ignore_refill;
	bool                          _ignore_reorder;
	ARDOUR::PluginInfoList        _plugin_list;

	Columns                                         _columns;
	Glib::RefPtr<PluginTreeStore>                   _model;
	Gtkmm2ext::DnDTreeView<ARDOUR::PluginPresetPtr> _display;
	Gtk::ScrolledWindow                             _scroller;
	Gtk::HBox                                       _mode_box;
	ArdourWidgets::ArdourButton                     _mode_button[4];
	Gtk::HBox                                       _search_box;
	Gtk::Entry                                      _search_entry;
	ArdourWidgets::ArdourButton                     _search_clear_button;

	/* shared by all browsers */
	static std::list<std::string>                     _favorite_order;
	static std::map<std::string, bool>                _favorite_expanded;
	static bool                                       _favorite_order_loaded;
	static sigc::signal<void, PluginBrowser const*>   FavoriteOrderChanged;
};
