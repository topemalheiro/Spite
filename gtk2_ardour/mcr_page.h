/*
 * Copyright (C) 2026 Spite
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#include <ytkmm/box.h>
#include <ytkmm/scrolledwindow.h>
#include <ytkmm/toggleaction.h>

#include "ardour/session_handle.h"

#include "gtkmm2ext/bindings.h"

#include "widgets/ardour_button.h"
#include "widgets/tabbable.h"

#include "application_bar.h"
#include "mcr_canvas.h"

/* Spite: the Modular Channels Rack (MCR), a window of Spite's own beside Rec, Edit,
 * Cue and Mix: one row per track, each track's devices side by side as in the Cue
 * page's Device view, on a plugdata-like canvas where cables will go between them.
 * Its Patch and Play modes are plugdata's edit and run modes.
 *
 * Like the other pages it can be hidden, or detached into a window of its own and
 * attached again; each of those unrealizes the page, so its live faces are made
 * again afterwards. */
class MCRPage : public ArdourWidgets::Tabbable, public ARDOUR::SessionHandlePtr
{
public:
	MCRPage ();
	~MCRPage ();

	void set_session (ARDOUR::Session*);

	XMLNode& get_state () const;
	int      set_state (const XMLNode&, int /* version */);

	void focus_on_clock ();

	McrCanvas& canvas () { return _canvas; }

private:
	void load_bindings ();
	void register_actions ();
	void play_mode_toggled ();
	void set_play_mode (bool);

	Gtkmm2ext::Bindings* bindings;

	ApplicationBar              _application_bar;
	Gtk::HBox                   _toolbar;
	ArdourWidgets::ArdourButton _patch_button;
	ArdourWidgets::ArdourButton _play_button;
	Gtk::ScrolledWindow         _scroller;
	McrCanvas                   _canvas;

	Glib::RefPtr<Gtk::ToggleAction> _play_action; /* MCR/play-mode */
};
