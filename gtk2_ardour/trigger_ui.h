/*
 * Copyright (C) 2021 Paul Davis <paul@linuxaudiosystems.com>
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

#include "ytkmm/colorselection.h"

#include "gtkmm2ext/actions.h"
#include "gtkmm2ext/bindings.h"

#include "ardour/triggerbox.h"
#include "widgets/ardour_button.h"

namespace Gtk
{
	class FileChooserDialog;
	class Menu;
}

namespace ArdourWidgets {
	class PopUp;
}

class TriggerJumpDialog;
class ArdourColorDialog;

class TriggerUI : virtual public sigc::trackable
{
public:
	/* Spite: box_signals false when the owner relays the box's signals,
	 * as TriggerBoxUI does for its rows: one connection per box instead of
	 * one per scene, see TriggerBoxUI.
	 */
	TriggerUI (bool box_signals = true);
	virtual ~TriggerUI ();

	void set_trigger (ARDOUR::TriggerReference);

	virtual void on_trigger_set () {}
	virtual void on_trigger_changed (PBD::PropertyChange const& ) = 0;

	/* Spite: the box put another trigger (a newly loaded clip) in our slot */
	void trigger_swapped ();

	static std::string follow_action_to_string (ARDOUR::FollowAction const &, bool with_targets=false);
	static std::string quantize_length_to_string (Temporal::BBT_Offset const &);
	static std::string launch_style_to_string (ARDOUR::Trigger::LaunchStyle);
	static std::string stretch_mode_to_string (ARDOUR::Trigger::StretchMode);

	static std::vector<std::string> follow_strings;
	static std::string              longest_follow;
	static std::vector<std::string> quantize_strings;
	static std::string              longest_quantize;
	static std::vector<std::string> launch_strings;
	static std::string              longest_launch;
	static std::vector<std::string> stretch_mode_strings;
	static std::string              longest_stretch_mode;

	static void                     setup_actions_and_bindings ();

	ARDOUR::TriggerReference trigger_reference() const { return tref; }

	/* Spite: unlimited scenes (plan step 2). An empty slot may have no
	 * Trigger: trigger_if_any () is null then. What a slot shows comes from
	 * view () (its Trigger, or the box's defaults for an empty slot), and
	 * what changes a slot goes through writable () (which gives an empty
	 * slot a Trigger first). Until the engine's defaults () and
	 * materialize () exist, both are trigger_if_any ().
	 */
	ARDOUR::TriggerPtr                     trigger_if_any () const;
	std::shared_ptr<ARDOUR::Trigger const> view () const;
	ARDOUR::TriggerPtr                     writable () const;
	/* the slot got a Trigger or lost it (had: whether it had one before):
	 * tell the rows and panels showing it, as a newly loaded clip does */
	void slot_changed (bool had) const;
	ARDOUR::TriggerBox&                    triggerbox () const { return *tref.box (); }

	void choose_color (Gtk::Window*);

	/* Spite: a clip without a color of its own shows its track's color, like Live */
	static const uint32_t follow_track_color = 0xBEBEBEFF; /* ARDOUR::Trigger's default color */
	uint32_t clip_color () const;
	void use_track_color ();
	void choose_sample (bool allow_multiple_select);
	void sample_chosen (int r);

	void launch_context_menu ();
	void follow_context_menu (GdkEventButton*);
	void context_menu ();

	void edit_jump_done (int r, TriggerJumpDialog* d);
	void edit_jump(bool right_fa);

	void set_follow_action (ARDOUR::FollowAction const &);
	void set_launch_style (ARDOUR::Trigger::LaunchStyle);
	void set_quantization (Temporal::BBT_Offset const&);
	void set_from_selection ();

	void toggle_trigger_isolated ();
	void clear_trigger ();
	void edit_trigger ();

	void trigger_midi_learn ();
	void trigger_stop_midi_learn ();
	void trigger_midi_unlearn ();
	void trigger_learning_finished (ArdourWidgets::PopUp*);

private:
	void trigger_changed (PBD::PropertyChange const& );  //calls on_trigger_changed to subclasses

protected:
	/* Spite: the current trigger's own signals, into trigger_connections;
	 * when the slot gets another trigger they are made again.
	 */
	virtual void connect_to_trigger ();

	/* Spite: the name editing widgets are SlotPropertyTable's: a row in the
	 * clip grid (TriggerEntry) is a TriggerUI too, one per scene and track.
	 */

	Gtk::FileChooserDialog* _file_chooser;
	sigc::connection        _file_chooser_connection;

	Gtk::Menu*              _launch_context_menu;
	Gtk::Menu*              _follow_context_menu;
	Gtk::Menu*              _context_menu;
	bool                    _ignore_menu_action;

	ArdourColorDialog*   _color_dialog;

	bool                  _box_signals; /* Spite: false when the owner relays them */
	void                  trigger_swap (uint32_t);
	PBD::ScopedConnection trigger_swap_connection;

	ARDOUR::TriggerReference tref;
	sigc::connection          color_connection;
	PBD::ScopedConnectionList trigger_connections;

	PBD::ScopedConnection learning_connection;
};



