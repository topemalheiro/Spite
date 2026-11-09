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

#include <algorithm>
#include <vector>

#include "ytkmm/sizegroup.h"
#include <ytkmm/filechooserdialog.h>
#include <ytkmm/menu.h>
#include <ytkmm/menuitem.h>
#include <ytkmm/stock.h>

#include "pbd/compose.h"
#include "pbd/convert.h"
#include "pbd/unwind.h"

#include "pbd/basename.h"
#include "pbd/file_utils.h"
#include "pbd/pathexpand.h"
#include "pbd/search_path.h"

#include "ardour/directory_names.h"
#include "ardour/filesystem_paths.h"
#include "ardour/region.h"
#include "ardour/region_factory.h"
#include "ardour/triggerbox.h"

#include "canvas/polygon.h"
#include "canvas/text.h"

#include "gtkmm2ext/utils.h"

#include "ardour_ui.h"
#include "gui_thread.h"
#include "keyboard.h"
#include "public_editor.h"
#include "region_view.h"
#include "selection.h"
#include "slot_properties_box.h"
#include "timers.h"
#include "trigger_ui.h"
#include "scene_scroll.h"
#include "triggerbox_ui.h"
#include "ui_config.h"
#include "utils.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ArdourCanvas;
using namespace Gtkmm2ext;
using namespace PBD;

TriggerEntry::TriggerEntry (Item* item, TriggerStrip& s, TriggerReference tr)
	: ArdourCanvas::Rectangle (item)
	, TriggerUI (false) /* Spite: TriggerBoxUI relays the box's signals */
	, _strip (s)
	, _grabbed (false)
	, _drag_active (false)
	, rec_blink_on (false)
	, _drawn_selected (false)
{
	set_layout_sensitive (true); // why???

	name = string_compose ("trigger %1", tr.slot());

	set_outline (false);

	play_button = new ArdourCanvas::Rectangle (this);
	play_button->set_outline (true);
	play_button->set_fill (true);
	play_button->name = string_compose ("playbutton %1", tr.slot());
	play_button->show ();

	follow_button = new ArdourCanvas::Rectangle (this);
	follow_button->set_outline (false);
	follow_button->set_fill (true);
	follow_button->name = ("slot_selector_button");
	follow_button->set_tooltip (_("Click to select Follow-Actions for this clip"));
	follow_button->show ();

	name_button = new ArdourCanvas::Rectangle (this);
	name_button->set_outline (true);
	name_button->set_fill (true);
	name_button->name = ("slot_selector_button");
	name_button->show ();

	name_text = new Text (name_button);
	name_text->set_ignore_events (false);
	name_text->set_tooltip (_("Click to select this clip and edit its properties\nRight-Click for context menu"));
	name_text->show ();

	/* this will trigger a call to on_trigger_changed() */
	set_trigger (tr);

	/* event handling */
	play_button->Event.connect (sigc::mem_fun (*this, &TriggerEntry::play_button_event));
	name_button->Event.connect (sigc::mem_fun (*this, &TriggerEntry::name_button_event));
	follow_button->Event.connect (sigc::mem_fun (*this, &TriggerEntry::follow_button_event));

	Event.connect (sigc::mem_fun (*this, &TriggerEntry::event));

	/* Spite: the canvas's drag signals, theme changes, the track's color and
	 * the box's signals come through TriggerBoxUI
	 */
	set_widget_colors ();

	rec_enable_change ();
}

TriggerEntry::~TriggerEntry ()
{
}

void
TriggerEntry::connect_to_trigger ()
{
	TriggerUI::connect_to_trigger ();

	TriggerPtr t (trigger_if_any ());
	if (t) {
		t->ArmChanged.connect (trigger_connections, invalidator (*this), std::bind (&TriggerEntry::rec_enable_change, this), gui_context ());
	}
}

void
TriggerEntry::repoint (TriggerReference tr)
{
	/* Spite: scenes were inserted or removed, and this row now shows slot
	 * tr.slot (): connect to that trigger, as the constructor does
	 */
	name = string_compose ("trigger %1", tr.slot());
	play_button->name = string_compose ("playbutton %1", tr.slot());

	set_trigger (tr);

	selection_change ();
	rec_enable_change ();
}

void
TriggerEntry::rec_enable_change ()
{
	switch (tref.box()->record_enabled()) {
	case Recording:
		break;
	case Enabled:
		if (!UIConfiguration::instance().get_no_strobe() && view () && view ()->armed()) {
			rec_blink_connection = Timers::blink_connect (sigc::mem_fun (*this, &TriggerEntry::blink_rec_enable));
		}
		break;
	case Disabled:
		rec_blink_connection.disconnect ();
		break;
	}

	set_play_button_tooltip ();
	redraw ();
}

void
TriggerEntry::blink_rec_enable (bool onoff)
{
	rec_blink_on = onoff;
	redraw ();
}

void
TriggerEntry::set_play_button_tooltip ()
{
	switch (tref.box()->record_enabled()) {
	case Recording:
	case Enabled:
		play_button->set_tooltip (string_compose (_("Click to enable recording for the follow length\n%1-click to enable recording until stopped"), Keyboard::primary_modifier_name()));
		break;
	default:
		play_button->set_tooltip (_("Stop other clips on this track.\nRight-click to select Launch Options for this clip"));
	}
}

void
TriggerEntry::owner_color_changed ()
{
	/* Spite: clips without a color of their own follow the track's */
	set_widget_colors ();
	redraw ();
}

void
TriggerEntry::selection_change ()
{
	set_widget_colors ();
}

void
TriggerEntry::_size_allocate (ArdourCanvas::Rect const& alloc)
{
	Rectangle::_size_allocate (alloc);

	const Distance width  = _rect.width ();
	const Distance height = _rect.height ();

	play_button->set (ArdourCanvas::Rect (0, 0, height, height));
	name_button->set (ArdourCanvas::Rect (height, 0, width - height, height));
	follow_button->set (ArdourCanvas::Rect (width - height, 0, width, height));

	const double scale = UIConfiguration::instance ().get_ui_scale ();
	double _poly_margin = 2. * scale;
	double font_margin = 2. * scale;

	name_text->size_allocate (ArdourCanvas::Rect (0, 0, width, height - font_margin * 2));
	float tleft = height;                                                 // make room for the play button
	name_text->set_position (Duple (tleft + _poly_margin, font_margin)); // @paul why do we need tleft here? isn't name_text a child of name_button?
	name_text->clamp_width (width - height * 2 - _poly_margin * 3);

	/* font scale may have changed. uiconfig 'embeds' the ui-scale in the font */
	name_text->set_font_description (UIConfiguration::instance ().get_NormalFont ());
}

void
TriggerEntry::draw_follow_icon (Cairo::RefPtr<Cairo::Context> context, FollowAction const & icon, float size, float scale) const
{
	uint32_t fg_color = UIConfiguration::instance ().color ("neutral:midground");

	/* in the case where there is a random follow-action, just put a "?" */
	if (view ()->follow_action_probability () > 0) {
		Glib::RefPtr<Pango::Layout> layout = Pango::Layout::create (context);
		layout->set_font_description (UIConfiguration::instance ().get_SmallMonospaceFont ());
		layout->set_text ("?");
		int tw, th;
		layout->get_pixel_size (tw, th);
		context->move_to (size / 2, size / 2);
		context->rel_move_to (-tw / 2, -th / 2);
		layout->show_in_cairo_context (context);
		return;
	}

	set_source_rgba (context, fg_color);
	context->set_line_width (1 * scale);

	switch (icon.type) {
		case FollowAction::Stop:
			context->rectangle (6 * scale, 6 * scale, size - 12 * scale, size - 12 * scale);
			context->stroke ();
			break;
		case FollowAction::Again:
			context->arc (size / 2, size / 2, size * 0.20, 60. * (M_PI / 180.0), 2 * M_PI);
			context->stroke ();
			context->arc (size / 2 + size * 0.2, size / 2, 1.5 * scale, 0, 2 * M_PI); // arrow head
			context->fill ();
			break;
		case FollowAction::ForwardTrigger:
			context->move_to (size / 2, 3 * scale);
			context->line_to (size / 2, size - 5 * scale);
			context->stroke ();
			context->arc (size / 2, size - 5 * scale, 2 * scale, 0, 2 * M_PI); // arrow head
			context->fill ();
			break;
		case FollowAction::ReverseTrigger:
			context->move_to (size / 2, 5 * scale);
			context->line_to (size / 2, size - 3 * scale);
			context->stroke ();
			context->arc (size / 2, 5 * scale, 2 * scale, 0, 2 * M_PI); // arrow head
			context->fill ();
			break;
		case FollowAction::JumpTrigger: {
			if ( icon.targets.count() == 1 ) {  //Jump to a specific row; just draw the letter of the row we are jumping to
				int cue_idx = -1;
				for (int i = 0; i < (int) icon.targets.size (); i++) {
					if (icon.targets.test(i)) {
						cue_idx = i;
						break;
					}
				}
				Glib::RefPtr<Pango::Layout> layout = Pango::Layout::create (context);
				layout->set_font_description (UIConfiguration::instance ().get_SmallMonospaceFont ());
				layout->set_text (cue_marker_name (cue_idx));
				int tw, th;
				layout->get_pixel_size (tw, th);
				context->move_to (size / 2, size / 2);
				context->rel_move_to (-tw / 2, -th / 2);
				layout->show_in_cairo_context (context);
			} else { // Multi-Jump: draw a star-like icon
				context->set_line_width (1.5 * scale);
				set_source_rgba (context, HSV (UIConfiguration::instance ().color ("neutral:midground")).lighter (0.25).color ()); // needs to be brighter to maintain balance
				for (int i = 0; i < 6; i++) {
					Cairo::Matrix m = context->get_matrix ();
					context->translate (size / 2, size / 2);
					context->rotate (i * M_PI / 3);
					context->move_to (0, 2 * scale);
					context->line_to (0, (size / 2) - 4 * scale);
					context->stroke ();
					context->set_matrix (m);
				}
			}
		} break;
		case FollowAction::None:
		default:
			break;
	}
}

void
TriggerEntry::draw_launch_icon (Cairo::RefPtr<Cairo::Context> context, float sz, float scale) const
{
	context->set_line_width (1 * scale);

	float margin = 4 * scale;
	float size   = sz - 2 * margin;

	/* Spite: an empty slot shows the box's defaults (view ()) */
	std::shared_ptr<Trigger const> const v (view ());
	if (!v) {
		return;
	}

	bool active = v->active ();

	if (!v->playable ()) {

		bool solid = false;

		switch (tref.box()->record_enabled()) {
		case Enabled:
			context->arc (margin + (size * 0.5), margin + (size * 0.5), (size * 0.5), 0., 360.0 * (M_PI/180.0));
			if (v->armed()) {
				solid = rec_blink_on;
			} else {
				solid = false;
			}
			break;

		case Recording:
			context->arc (margin + (size * 0.5), margin + (size * 0.5), (size * 0.5), 0., 360.0 * (M_PI/180.0));
			if (v->armed()) {
				solid = true;
			} else {
				solid = false;
			}
			break;

		case Disabled:
			/* not recording and no content in this slot, it is only a Stop button */
			context->move_to (margin, margin);
			context->rel_line_to (size, 0);
			context->rel_line_to (0, size);
			context->rel_line_to (-size, 0);
			context->rel_line_to (0, -size);
			set_source_rgba (context, UIConfiguration::instance ().color ("neutral:midground"));
			context->stroke ();
			return;
		}

		if (solid) {
			set_source_rgba (context, UIConfiguration::instance ().color ("record enable button: fill active"));
			context->fill ();
		} else {
			set_source_rgba (context, bg_color());
			context->fill_preserve ();
			set_source_rgba (context, UIConfiguration::instance ().color ("record enable button: fill active"));
			context->stroke ();
		}
		return;
	}

	set_source_rgba (context, UIConfiguration::instance ().color ("neutral:midground"));

	switch (v->launch_style ()) {
		case Trigger::Toggle:
			if (active) {
				/* special case: now it's a square Stop button */
				context->move_to (margin, margin);
				context->rel_line_to (size, 0);	
				context->rel_line_to (-size, 0);
				context->line_to (margin, margin);
				set_source_rgba (context, UIConfiguration::instance ().color ("neutral:foreground"));
				context->fill ();
				context->stroke ();
			} else {				/* boxy arrow */
				context->move_to (margin, margin);
				context->rel_line_to (0, size);
				context->rel_line_to (size * 1 / 3, 0);
				context->rel_line_to (size * 2 / 3, -size / 2);
				context->rel_line_to (-size * 2 / 3, -size / 2);
				context->line_to (margin, margin);
				set_source_rgba (context, UIConfiguration::instance ().color ("neutral:midground"));
				context->stroke ();
			}
			break;
		case Trigger::OneShot:
			context->move_to (margin, margin);
			context->rel_line_to (0, size);
			context->rel_line_to (size, -size / 2);
			context->line_to (margin, margin);
			if (active) {
				set_source_rgba (context, UIConfiguration::instance ().color ("neutral:foreground"));
				context->fill ();
			} else {
				set_source_rgba (context, UIConfiguration::instance ().color ("neutral:midground"));
			}
			context->stroke ();
			break;
		case Trigger::ReTrigger:
			/* line + boxy arrow + line */
			if (active) {
				set_source_rgba (context, UIConfiguration::instance ().color ("neutral:foreground"));
			} else {
				set_source_rgba (context, UIConfiguration::instance ().color ("neutral:midground"));
			}

			/* vertical line at left */
			context->set_line_width (2 * scale);
			context->move_to (margin + 1 * scale, margin);
			context->line_to (margin + 1 * scale, margin + size);
			context->stroke ();

			/* small triangle */
			context->set_line_width (1 * scale);
			context->move_to (margin + scale * 4, margin + 2 * scale);
			context->line_to (margin + size, margin + size / 2);
			context->line_to (margin + scale * 4, margin + size - 2 * scale);
			context->line_to (margin + scale * 4, margin + 2 * scale);
			if (active) {
				context->fill ();
			} else {
				context->stroke ();
			}
			break;
		case Trigger::Gate:
			/* diamond shape */
			context->move_to (margin + size / 2, margin);
			context->rel_line_to (size / 2, size / 2);
			context->rel_line_to (-size / 2, size / 2);
			context->rel_line_to (-size / 2, -size / 2);
			context->rel_line_to (size / 2, -size / 2);
			if (active) {
				set_source_rgba (context, UIConfiguration::instance ().color ("neutral:foreground"));
				context->fill ();
				context->stroke ();
			} else {
				set_source_rgba (context, UIConfiguration::instance ().color ("neutral:midground"));
				context->stroke ();
			}
			break;
		case Trigger::Repeat:
			/* 'stutter' shape */
			context->set_line_width (1 * scale);
			context->move_to (margin, margin);
			context->rel_line_to (0, size);

			context->move_to (margin + scale * 3, margin + scale * 2);
			context->rel_line_to (0, size - scale * 4);

			context->move_to (margin + scale * 6, margin + scale * 3);
			context->rel_line_to (0, size - scale * 6);

			if (active) {
				set_source_rgba (context, UIConfiguration::instance ().color ("neutral:foregroundest"));
			} else {
				/* stutter shape needs to be brighter to maintain balance */
				set_source_rgba (context, HSV (UIConfiguration::instance ().color ("neutral:midground")).lighter (0.25).color ());
			}
			context->stroke ();
			break;
		default:
			std::cerr << "other\n";
			break;
	}

	context->set_line_width (1);
}

void
TriggerEntry::render (ArdourCanvas::Rect const& area, Cairo::RefPtr<Cairo::Context> context) const
{
	Rectangle::render (area, context);

	/* Note that item_to_window() already takes _position into account (as
	 * part of item_to_canvas()
	 */
	ArdourCanvas::Rect       self (item_to_window (_rect));
	const ArdourCanvas::Rect draw = self.intersection (area);

	if (!draw) {
		return;
	}

	float width  = _rect.width ();
	float height = _rect.height ();

	const double scale = UIConfiguration::instance ().get_ui_scale ();

	if (_fill && !_transparent) {
		setup_fill_context (context);
		context->rectangle (draw.x0, draw.y0, draw.width (), draw.height ());
		context->fill ();
	}

	render_children (area, context);

	std::shared_ptr<Trigger const> const v (view ());
	if (!v) {
		return;
	}

	if (v->cue_isolated ()) {
		/* left shadow */
		context->save ();
		context->translate (self.x0, self.y0 - 0.5);
		Cairo::RefPtr<Cairo::LinearGradient> l_shadow = Cairo::LinearGradient::create (0, 0, scale * 12, 0);
		l_shadow->add_color_stop_rgba (0.0, 0.0, 0.0, 0.0, 0.8);
		l_shadow->add_color_stop_rgba (1.0, 0.0, 0.0, 0.0, 0.0);
		context->set_source (l_shadow);
		context->rectangle (0, 0, scale * 12, height);
		context->fill ();
		context->restore ();
	}

	if (false /*tref.slot == 1*/) {
		/* drop-shadow at top */
		Cairo::RefPtr<Cairo::LinearGradient> drop_shadow_pattern = Cairo::LinearGradient::create (0.0, 0.0, 0.0, 6 * scale);
		drop_shadow_pattern->add_color_stop_rgba (0, 0, 0, 0, 0.7);
		drop_shadow_pattern->add_color_stop_rgba (1, 0, 0, 0, 0.0);
		context->set_source (drop_shadow_pattern);
		context->rectangle (0, 0, width, 6 * scale);
		context->fill ();
	}

	/* launch icon */
	{
		context->save ();
		context->translate (self.x0, self.y0 - 0.5);
		draw_launch_icon (context, height, scale);
		context->restore ();
	}

	/* follow-action icon */
	if (v->playable () && v->will_follow ()) {
		context->save ();
		context->translate (self.x0, self.y0 - 0.5);
		context->translate (width - height, 0); // right side of the widget
		draw_follow_icon (context, v->follow_action0 (), height, scale);
		context->restore ();
	}
}

void
TriggerEntry::on_trigger_changed (PropertyChange const& change)
{
	if (change.contains (ARDOUR::Properties::name)) {
		std::shared_ptr<Trigger const> const v (view ());
		if (v && v->playable ()) {
			name_text->set (short_version (v->name (), 16));
		} else {
			name_text->set ("");
		}
		set_play_button_tooltip ();
	}

	set_widget_colors (); //depending on the state, this might change a color and queue a redraw

	PropertyChange interesting_stuff;
	interesting_stuff.add (ARDOUR::Properties::name);
	interesting_stuff.add (ARDOUR::Properties::color);
	interesting_stuff.add (ARDOUR::Properties::launch_style);
	interesting_stuff.add (ARDOUR::Properties::follow_action0);
	interesting_stuff.add (ARDOUR::Properties::follow_action1);
	interesting_stuff.add (ARDOUR::Properties::follow_action_probability);
	interesting_stuff.add (ARDOUR::Properties::follow_count);
	interesting_stuff.add (ARDOUR::Properties::cue_isolated);
	interesting_stuff.add (ARDOUR::Properties::running);

	if (change.contains (interesting_stuff)) {
		redraw ();
	}
}

Color
TriggerEntry::bg_color() const
{
	Color bg_col = UIConfiguration::instance ().color ("theme:bg");

	//alternating darker bands
	if ((tref.slot() / 2) % 2 == 0) {
		bg_col = HSV (bg_col).darker (0.25).color ();
	}

	return bg_col;
}

void
TriggerEntry::set_widget_colors (TriggerEntry::EnteredState es)
{
	color_t bg_col = bg_color ();
	set_fill_color (bg_col);

	//child widgets highlight when entered
	color_t hilite = HSV (bg_col).lighter (0.15).color ();

	play_button->set_fill_color ((es == PlayEntered) ? hilite : bg_col);
	play_button->set_outline_color ((es == PlayEntered) ? hilite : bg_col);

	name_button->set_fill_color ((es == NameEntered) ? hilite : bg_col);
	name_button->set_outline_color ((es == NameEntered) ? hilite : bg_col);

	follow_button->set_fill_color ((es == FollowEntered) ? hilite : bg_col);

	std::shared_ptr<Trigger const> const v (view ());
	if (!v) {
		return;
	}

	name_text->set_color (v->color ());
	name_text->set_fill_color (UIConfiguration::instance ().color ("neutral:midground"));

	/* Spite: a loaded clip is a block of its color (its track's unless the
	 * user picked one), with readable text, like Live's clip slots */
	if (v->playable ()) {
		const color_t clip = clip_color ();
		const color_t fill = (es == NameEntered) ? HSV (clip).lighter (0.15).color () : clip;
		name_button->set_fill_color (fill);
		name_button->set_outline_color (fill);
		name_text->set_color (contrasting_text_color (fill));
	}

	/*preserve selection border*/
	_drawn_selected = PublicEditor::instance ().get_selection ().selected (trigger_reference ());
	if (_drawn_selected) {
		name_button->set_outline_color (UIConfiguration::instance ().color ("alert:red"));
	}

	/*draw a box around 'queued' trigger*/
	TriggerPtr const t (trigger_if_any ());
	if (t && !t->active () && tref.box ()->currently_playing () == t) {
		play_button->set_outline_color (UIConfiguration::instance ().color ("neutral:foreground"));
	}
}

bool
TriggerEntry::name_button_event (GdkEvent* ev)
{
	switch (ev->type) {
	case GDK_ENTER_NOTIFY:
		if (ev->crossing.detail != GDK_NOTIFY_INFERIOR) {
			set_widget_colors (NameEntered);
		}
		break;
	case GDK_LEAVE_NOTIFY:
		if (ev->crossing.detail != GDK_NOTIFY_INFERIOR) {
			set_widget_colors (NoneEntered);
		}
		break;
	case GDK_BUTTON_PRESS:
		if (Gtkmm2ext::Keyboard::is_context_menu_event (&ev->button)) {
			PublicEditor::instance ().get_selection ().set (trigger_reference ());
			context_menu ();
			return true;
		}
		break;
	case GDK_2BUTTON_PRESS:
#if SELECTION_PROPERTIES_BOX_TODO
		edit_trigger ();
#endif
		return true;
	case GDK_BUTTON_RELEASE:
		if (Gtkmm2ext::Keyboard::is_delete_event (&ev->button)) {
			clear_trigger ();
			return true;
		}
		switch (ev->button.button) {
		case 1:
			PublicEditor::instance ().get_selection ().set (trigger_reference ());
			return true;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return false;
}

bool
TriggerEntry::play_button_event (GdkEvent* ev)
{
	std::shared_ptr<Trigger const> const v (view ());

	if (!v || !v->playable ()) {
		/* empty slot; this is just a stop button */
		switch (ev->type) {
			case GDK_BUTTON_PRESS:
				if (ev->button.button == 1) {
					TriggerBox& box (triggerbox ());
					if (box.record_enabled()) {
						/* this is a record button */
						TriggerPtr const t (trigger_if_any ());
						if (t && t->armed()) {
							t->disarm ();
						} else if (TriggerPtr w = writable ()) {
							/* Record till done (with Primary), or for the
							 * trigger's capture duration: both arm () */
							w->arm ();
						}
					} else if (Keyboard::modifier_state_equals (ev->button.state, Keyboard::PrimaryModifier)) {
						box.stop_all_immediately ();
					} else {
						box.stop_all_quantized ();
					}
					return true; /* Spite: there is nothing to launch */
				}
				break;
			default:
				break;
		}
	}

	switch (ev->type) {
		case GDK_2BUTTON_PRESS:  //need to un-grab in a double-click action
		case GDK_BUTTON_PRESS:
			switch (ev->button.button) {
				case 1:
					if (TriggerPtr t = trigger_if_any ()) {
						if (t->launch_style () == Trigger::Gate ||
						    t->launch_style () == Trigger::Repeat) {
							t->bang ();
							_grabbed = true;
							play_button->grab ();
						} else {
							t->bang ();
						}
					}
				default:
					break;
			}
			break;
		case GDK_BUTTON_RELEASE:
			switch (ev->button.button) {
			case 1:
				if (_grabbed) {
					if (TriggerPtr t = trigger_if_any ()) {
						t->unbang ();
					}
					play_button->ungrab ();
					_grabbed = false;
				}
				break;
			case 2:
				if (Keyboard::modifier_state_equals (ev->button.state, Keyboard::PrimaryModifier)) {
					/* begin/end MIDI Learn */
					if (tref.box ()) {
						if (triggerbox ().learning()) {
							triggerbox ().stop_midi_learn ();
						} else {
							trigger_midi_learn ();
						}
					}
				}
				break;
			case 3:
				launch_context_menu ();
				return true;
			default:
				break;
			}
			break;
		case GDK_ENTER_NOTIFY:
			if (ev->crossing.detail != GDK_NOTIFY_INFERIOR) {
				set_widget_colors (PlayEntered);
			}
			break;
		case GDK_LEAVE_NOTIFY:
			if (ev->crossing.detail != GDK_NOTIFY_INFERIOR) {
				set_widget_colors (NoneEntered);
			}
			break;
		default:
			break;
	}
	return true;
}

bool
TriggerEntry::follow_button_event (GdkEvent* ev)
{
	switch (ev->type) {
		case GDK_BUTTON_PRESS:
			return true;  //wait for release to show the menu
			break;
		case GDK_BUTTON_RELEASE:
			switch (ev->button.button) {
				case 1:
					follow_context_menu (&ev->button);
				default:
					break;
			}
			break;
		case GDK_ENTER_NOTIFY:
			if (ev->crossing.detail != GDK_NOTIFY_INFERIOR) {
				set_widget_colors (FollowEntered);
			}
			break;
		case GDK_LEAVE_NOTIFY:
			if (ev->crossing.detail != GDK_NOTIFY_INFERIOR) {
				set_widget_colors (NoneEntered);
			}
			break;
		default:
			break;
	}
	return true;
}

bool
TriggerEntry::event (GdkEvent* ev)
{
	if (!view () || !view ()->playable ()) {
		return false;
	}

	switch (ev->type) {
		case GDK_2BUTTON_PRESS:  //need to un-grab in a double-click action
		case GDK_BUTTON_RELEASE:
			if(_grabbed) {
				ungrab();
				_grabbed = false;
				if (ev->type == GDK_BUTTON_RELEASE) {
					/* Pass event down to child item, as if this item was not grabbed.
					 * This is needed to select item on release.
					 */
					name_button->Event (ev);
					return true;
				}
			}
			break;

		case GDK_BUTTON_PRESS:
			if (!_drag_active) {
				GdkEventButton* bev = (GdkEventButton*)ev;
				if (bev->button == 1) {
					_drag_start_x = bev->x;
					_drag_start_y = bev->y;
					_grabbed = true;
					grab();
					return true;
				} else {
					_drag_start_x = -1;
					_drag_start_y = -1;
				}
			}
			break;

		case GDK_MOTION_NOTIFY:
			if (!_drag_active) {
				int               x, y;
				Gdk::ModifierType mask;

				GtkCanvas* gtkcanvas = static_cast<GtkCanvas*> (canvas ());
				gtkcanvas->get_window ()->get_pointer (x, y, mask);

				if (mask & GDK_BUTTON1_MASK) {
					if (gtkcanvas->drag_check_threshold (_drag_start_x, _drag_start_y, x, y)) {
						_drag_active = true;
						gtkcanvas->drag_begin (TriggerBoxUI::dnd_src (), Gdk::ACTION_COPY, 1, ev);
						// -> save a reference to the dragged slot, for use in ::drag_begin ::drag_data_get()
						return true;
					}
				}
			}
			break;
		default:
			break;
	}
	return false;
}

void
TriggerEntry::drag_begin (Glib::RefPtr<Gdk::DragContext> const& context)
{
	if (!_drag_active) {
		/* Since the canvas is shared, all TriggerEntry inside
		 * the TriggerBox canvas receive this signal
		 */
		return;
	}
	//const ArdourCanvas::Rect rect = allocation ();
	int                       width     = _rect.width ();
	int                       height    = _rect.height ();
	GtkCanvas*                gtkcanvas = static_cast<GtkCanvas*> (canvas ());
	Glib::RefPtr<Gdk::Pixmap> pixmap    = Gdk::Pixmap::create (gtkcanvas->get_root_window (), width, height);

	{
		cairo_t*                      cr  = gdk_cairo_create (Glib::unwrap (pixmap));
		Cairo::RefPtr<Cairo::Context> ctx = Cairo::RefPtr<Cairo::Context> (new Cairo::Context (cr, true /* has_reference */));

		/* inverse offset, because ::render() translates coordinates itself */
		ArdourCanvas::Rect self (item_to_window (_rect));
		ctx->translate (-self.x0, -self.y0);
		ctx->save ();
		render (self, ctx);
		ctx->restore ();

		/* draw an outline around the drag object, replace red selection border */
		ctx->set_identity_matrix ();
		ctx->rectangle (0, 0, width, height);
		set_source_rgba (ctx, UIConfiguration::instance ().color ("neutral:foreground"));
		ctx->set_line_width (1.5);
		ctx->stroke ();
		/* ctx leaves scope, cr is destroyed, and pixmap surface is flush()ed */
	}

	TriggerPtr const        t (trigger_if_any ());
	std::shared_ptr<Region> region = t ? t->the_region () : std::shared_ptr<Region> ();
	if (region) {
		PublicEditor::instance ().pbdid_dragged_dt = region->data_type ();
	} else {
		PublicEditor::instance ().pbdid_dragged_dt = DataType::NIL;
	}
	context->set_icon (pixmap->get_colormap (), pixmap, Glib::RefPtr<Gdk::Bitmap> (NULL), width / 2, height / 2);
}

void
TriggerEntry::drag_end (Glib::RefPtr<Gdk::DragContext> const&)
{
	if (_drag_active) {
		PublicEditor::instance ().pbdid_dragged_dt = DataType::NIL;
	}
	_drag_active = false;
}

void
TriggerEntry::drag_data_get (Glib::RefPtr<Gdk::DragContext> const&, Gtk::SelectionData& data, guint, guint)
{
	if (!_drag_active) {
		/* Since the canvas is shared, all TriggerEntry instances
		 * inside a TriggerBox canvas receive this signal.
		 */
		return;
	}
	TriggerPtr const t (trigger_if_any ());
	if (!t) {
		return; /* an empty slot has nothing to drag */
	}
	if (data.get_target () == "x-ardour/region.pbdid") {
		std::shared_ptr<Region> region = t->the_region ();
		if (region) {
			data.set (data.get_target (), region->id ().to_s ());
		}
	}
	if (data.get_target () == "x-ardour/trigger.pbdid") {
		data.set (data.get_target (), t->id ().to_s ());
	}
}

/* ***************************************************** */

Glib::RefPtr<Gtk::TargetList> TriggerBoxUI::_dnd_src;

TriggerBoxUI::TriggerBoxUI (ArdourCanvas::Item* parent, TriggerStrip& s, TriggerBox& tb)
	: Rectangle (parent)
	, _triggerbox (tb)
	, _pool_n (0)
	, _strip (s)
	, _width (0)
	, _height (0)
	, _rows_w (-1)
	, _rows_h (-1)
	, _cue_row (-1)
	, _cue_active (false)
{
	set_layout_sensitive (true); // why???

	set_fill_color (UIConfiguration::instance ().color (X_("theme:bg")));
	set_fill (true);

	build ();

	/* Spite: the page's scenes scroll as one */
	_scroll_connection = SceneScroll::instance ().adjustment ().signal_value_changed ().connect (sigc::mem_fun (*this, &TriggerBoxUI::layout));

	_selection_connection = PublicEditor::instance ().get_selection ().TriggersChanged.connect (sigc::mem_fun (*this, &TriggerBoxUI::selection_changed));

	/* DnD */

	if (!_dnd_src) {
		std::vector<Gtk::TargetEntry> source_table;
		source_table.push_back (Gtk::TargetEntry ("x-ardour/trigger.pbdid", Gtk::TARGET_SAME_APP));
		source_table.push_back (Gtk::TargetEntry ("x-ardour/region.pbdid", Gtk::TARGET_SAME_APP));
		_dnd_src = Gtk::TargetList::create (source_table);
	}

	std::vector<Gtk::TargetEntry> target_table;
	target_table.push_back (Gtk::TargetEntry ("x-ardour/trigger.pbdid", Gtk::TARGET_SAME_APP));
	target_table.push_back (Gtk::TargetEntry ("x-ardour/region.pbdid", Gtk::TARGET_SAME_APP));
	target_table.push_back (Gtk::TargetEntry ("text/uri-list"));
	target_table.push_back (Gtk::TargetEntry ("text/plain"));
	target_table.push_back (Gtk::TargetEntry ("application/x-rootwin-drop"));

	GtkCanvas* gtkcanvas = static_cast<GtkCanvas*> (canvas ());
	assert (gtkcanvas);
	gtkcanvas->drag_dest_set (target_table);
	gtkcanvas->signal_drag_motion ().connect (sigc::mem_fun (*this, &TriggerBoxUI::drag_motion));
	gtkcanvas->signal_drag_leave ().connect (sigc::mem_fun (*this, &TriggerBoxUI::drag_leave));
	gtkcanvas->signal_drag_data_received ().connect (sigc::mem_fun (*this, &TriggerBoxUI::drag_data_received));

	/* Spite: what every row needs, connected once here instead of once per
	 * row (see TriggerEntry). The box emits these from the audio thread.
	 */
	gtkcanvas->signal_drag_begin ().connect (sigc::mem_fun (*this, &TriggerBoxUI::drag_begin));
	gtkcanvas->signal_drag_end ().connect (sigc::mem_fun (*this, &TriggerBoxUI::drag_end));
	gtkcanvas->signal_drag_data_get ().connect (sigc::mem_fun (*this, &TriggerBoxUI::drag_data_get));

	_triggerbox.PropertyChanged.connect (_box_connections, invalidator (*this), std::bind (&TriggerBoxUI::box_property_changed, this, _1), gui_context ());
	_triggerbox.TriggerSwapped.connect (_box_connections, invalidator (*this), std::bind (&TriggerBoxUI::trigger_swapped, this, _1), gui_context ());
	_triggerbox.RecEnableChanged.connect (_box_connections, invalidator (*this), std::bind (&TriggerBoxUI::rec_enable_changed, this), gui_context ());

	if (Stripable* owner = dynamic_cast<Stripable*> (_triggerbox.owner ())) {
		owner->presentation_info ().Change.connect (_box_connections, invalidator (*this), std::bind (&TriggerBoxUI::owner_prop_change, this, _1), gui_context ());
	}

	UIConfiguration::instance ().ParameterChanged.connect (sigc::mem_fun (*this, &TriggerBoxUI::ui_parameter_changed));

	/* the audio thread's requests to the GUI go through a queue of fixed
	 * size, and when a cue starts or stops clips on many tracks in the same
	 * cycle some may be dropped: look again ten times a second, as
	 * TriggerMaster does (this costs nothing while nothing changes)
	 */
	_cue_check_connection = Timers::rapid_connect (sigc::mem_fun (*this, &TriggerBoxUI::update_cue_rows));
}

TriggerBoxUI::~TriggerBoxUI ()
{
	/* sigc connection's are not scoped (i.e. they do not disconnect the
	   functor from the signal when they are destroyed).
	*/
	_selection_connection.disconnect ();
	_cue_check_connection.disconnect ();
	_scroll_connection.disconnect ();
}

void
TriggerBoxUI::selection_changed ()
{
	/* Spite: only the rows whose selection changed are redrawn; the
	 * selection holds slots (box and scene) of every track.
	 */
	Selection const& sel (PublicEditor::instance ().get_selection ());
	for (auto& slot : _slots) {
		if (sel.selected (slot->trigger_reference ()) != slot->drawn_selected ()) {
			slot->selection_change ();
		}
	}
}

int32_t
TriggerBoxUI::cue_row () const
{
	/* the row of the box's current (playing or queued) trigger */
	TriggerPtr const cp (_triggerbox.currently_playing ());
	if (!cp) {
		return -1;
	}
	uint32_t const n = cp->index ();
	return n < _triggerbox.n_triggers () ? (int32_t) n : -1;
}

void
TriggerBoxUI::update_cue_rows ()
{
	/* Spite: at most one trigger per box plays or waits to start, the
	 * box's current one. A row shows it with its play button (filled while
	 * it plays, outlined while it is queued, see TriggerEntry), so when the
	 * current trigger or its state changes, only the row that showed it and
	 * the one that shows it now are redone.
	 */
	TriggerPtr const cp (_triggerbox.currently_playing ());
	int32_t const    now    = cue_row ();
	bool const       active = cp && cp->active ();

	if (now == _cue_row && active == _cue_active) {
		return;
	}

	PropertyChange pc;
	pc.add (Properties::running);
	pc.add (Properties::currently_playing);

	/* rows off screen have none: they show the state when scrolled in */
	if (_cue_row >= 0) {
		if (TriggerEntry* e = entry_for_slot (_cue_row)) {
			e->on_trigger_changed (pc);
		}
	}
	if (now >= 0 && now != _cue_row) {
		if (TriggerEntry* e = entry_for_slot (now)) {
			e->on_trigger_changed (pc);
		}
	}
	_cue_row    = now;
	_cue_active = active;
}

void
TriggerBoxUI::box_property_changed (PropertyChange const& what)
{
	/* Spite: the audio thread sends these whenever a clip starts, stops or
	 * gets queued, see update_cue_rows ()
	 */
	PropertyChange rest (what);

	if (what.contains (Properties::currently_playing) || what.contains (Properties::queued)) {
		update_cue_rows ();
		rest.erase (Properties::currently_playing.property_id);
		rest.erase (Properties::queued.property_id);
	}

	if (!rest.empty ()) {
		for (auto& slot : _slots) {
			slot->on_trigger_changed (rest);
		}
	}
}

void
TriggerBoxUI::trigger_swapped (uint32_t n)
{
	/* a newly loaded clip took the place of slot n's trigger */
	TriggerEntry* e = entry_for_slot (n);
	if (!e) {
		return; /* off screen: its row is pointed at it when scrolled in */
	}
	e->trigger_swapped ();
	TriggerPtr const t (_triggerbox.trigger (n));
	_shown[n % _pool_n] = t ? t.get () : 0;
}

void
TriggerBoxUI::rec_enable_changed ()
{
	/* empty slots turn from stop buttons into record buttons, and back */
	for (auto& slot : _slots) {
		slot->rec_enable_change ();
	}
}

void
TriggerBoxUI::owner_prop_change (PropertyChange const& what)
{
	if (what.contains (Properties::color)) {
		/* clips without a color of their own follow the track's */
		for (auto& slot : _slots) {
			slot->owner_color_changed ();
		}
	}
}

void
TriggerBoxUI::ui_parameter_changed (std::string const& p)
{
	if (p == "color-file") {
		for (auto& slot : _slots) {
			slot->set_widget_colors ();
		}
	}
}

TriggerEntry*
TriggerBoxUI::dragging_row () const
{
	/* a drag started on this canvas: the row it started from */
	for (auto& slot : _slots) {
		if (slot->drag_active ()) {
			return slot;
		}
	}
	return 0;
}

void
TriggerBoxUI::drag_begin (Glib::RefPtr<Gdk::DragContext> const& context)
{
	if (TriggerEntry* row = dragging_row ()) {
		row->drag_begin (context);
	}
}

void
TriggerBoxUI::drag_end (Glib::RefPtr<Gdk::DragContext> const& context)
{
	if (TriggerEntry* row = dragging_row ()) {
		row->drag_end (context);
	}
}

void
TriggerBoxUI::drag_data_get (Glib::RefPtr<Gdk::DragContext> const& context, Gtk::SelectionData& data, guint info, guint time)
{
	if (TriggerEntry* row = dragging_row ()) {
		row->drag_data_get (context, data, info, time);
	}
}

void
TriggerBoxUI::build ()
{
	/* Spite: scenes were inserted or removed: the rows on screen show what
	 * their slots hold now (the selection is by slot and moves with the
	 * scenes: TriggerSelection::scenes_shifted ())
	 */
	layout ();

	/* the rows were redone for their triggers, the current one included */
	TriggerPtr const cp (_triggerbox.currently_playing ());
	_cue_row    = cue_row ();
	_cue_active = cp && cp->active ();
}

/* Spite: the rows for the scenes on screen (see Slots), pointed at their
 * slots and placed. Called when the view's size, the scroll offset or the
 * scene count changes.
 */
void
TriggerBoxUI::layout ()
{
	SceneScroll&   ss (SceneScroll::instance ());
	double const   rh = ss.row_height ();
	uint32_t const n  = _triggerbox.n_triggers ();
	size_t const   P  = std::min<size_t> (n, SceneScroll::pool_size (_height));

	std::shared_ptr<TriggerBox> box (_triggerbox.shared_from_this ());

	if (P != _pool_n) {
		/* the view got taller or shorter: rows are assigned to scenes again */
		while (_slots.size () < P) {
			_slots.push_back (new TriggerEntry (this, _strip, TriggerReference (box, std::min<uint32_t> (_slots.size (), n - 1))));
		}
		for (size_t k = P; k < _slots.size (); ++k) {
			_slots[k]->hide ();
		}
		_pool_n = P;
		_slot_of.assign (_slots.size (), -1);
		_shown.assign (_slots.size (), 0);
		_rows_w = -1;
	}
	if (P == 0) {
		return;
	}

	bool const resize = (_rows_w != _width || _rows_h != rh);
	_rows_w = _width;
	_rows_h = rh;

	uint32_t const first  = ss.first_row ();
	double const   offset = ss.offset ();

	for (uint32_t s = first; s < first + P; ++s) {
		size_t const  k = s % P;
		TriggerEntry* e = _slots[k];
		if (s >= n) {
			e->hide ();
			_slot_of[k] = -1;
			continue;
		}
		TriggerPtr const t (_triggerbox.trigger (s));
		if (_slot_of[k] != (int64_t) s || _shown[k] != t.get ()) {
			e->repoint (TriggerReference (box, s));
			_slot_of[k] = s;
			_shown[k]   = t.get ();
		}
		if (resize) {
			e->size_allocate (ArdourCanvas::Rect (0, 0, _width, rh));
		}
		e->set_position (Duple (0, s * rh - offset));
		e->show ();
	}
}

TriggerEntry*
TriggerBoxUI::entry_for_slot (uint32_t s) const
{
	if (_pool_n == 0) {
		return 0;
	}
	size_t const k = s % _pool_n;
	return _slot_of[k] == (int64_t) s ? _slots[k] : 0;
}

void
TriggerBoxUI::_size_allocate (ArdourCanvas::Rect const& alloc)
{
	Rectangle::_size_allocate (alloc);

	/* Spite: the view; the rows are a scene's height each, see layout () */
	_width  = alloc.width ();
	_height = alloc.height ();
	layout ();
}

uint64_t
TriggerBoxUI::slot_at_y (int y) const
{
	/* Spite: the scene under y, on screen or not */
	return (uint64_t) std::max (0., floor ((y + SceneScroll::instance ().offset ()) / SceneScroll::row_height ()));
}

bool
TriggerBoxUI::drag_motion (Glib::RefPtr<Gdk::DragContext> const& context, int, int y, guint time)
{
	bool can_drop = true;
	GtkCanvas* gtkcanvas = static_cast<GtkCanvas*> (canvas ());
	std::string target = gtkcanvas->drag_dest_find_target (context, gtkcanvas->drag_dest_get_target_list ());

	if ((target == "x-ardour/region.pbdid") || (target == "x-ardour/trigger.pbdid")) {
		can_drop = PublicEditor::instance ().pbdid_dragged_dt == _triggerbox.data_type ();
	}

	uint64_t n = slot_at_y (y);
	if (n >= _triggerbox.n_triggers ()) {
		can_drop = false; /* below the last scene */
	}

	if (can_drop) {
		context->drag_status (Gdk::ACTION_COPY, time);
		/* prelight (only rows on screen exist) */
		GdkEventCrossing ev;
		ev.detail = GDK_NOTIFY_ANCESTOR;
		TriggerEntry* over = entry_for_slot (n);
		for (auto& slot : _slots) {
			ev.type = (slot == over) ? GDK_ENTER_NOTIFY : GDK_LEAVE_NOTIFY;
			slot->name_button_event ((GdkEvent*)&ev);
		}
		return true;
	} else {
		context->drag_status (Gdk::DragAction (0), time);
		return false;
	}
}

void
TriggerBoxUI::drag_leave (Glib::RefPtr<Gdk::DragContext> const&, guint)
{
	GdkEventCrossing ev;
	ev.type   = GDK_LEAVE_NOTIFY;
	ev.detail = GDK_NOTIFY_ANCESTOR;
	for (size_t i = 0; i < _slots.size (); ++i) {
		_slots[i]->name_button_event ((GdkEvent*)&ev);
	}
}

void
TriggerBoxUI::drag_data_received (Glib::RefPtr<Gdk::DragContext> const& context, int /*x*/, int y, Gtk::SelectionData const& data, guint /*info*/, guint time)
{
	uint64_t n = slot_at_y (y);
	if (n >= _triggerbox.n_triggers ()) {
		context->drag_finish (false, false, time);
		return;
	}

	if (data.get_target () == "x-ardour/region.pbdid") {
		PBD::ID                   rid (data.get_data_as_string ());
		std::shared_ptr<Region> region = RegionFactory::region_by_id (rid);
		if (region) {
			_triggerbox.set_from_selection (n, region);
			context->drag_finish (true, false, time);
		} else {
			context->drag_finish (false, false, time);
		}
		return;
	}

	if (data.get_target () == "x-ardour/trigger.pbdid") {
		PBD::ID tid (data.get_data_as_string ());
		std::shared_ptr<Trigger> source = _triggerbox.session().trigger_by_id (tid);
		if (source) {
			Trigger::UIState *state = new Trigger::UIState();
			source->get_ui_state(*state);
			std::shared_ptr<Trigger::UIState> state_p (state);
			_triggerbox.enqueue_trigger_state_for_region(source->the_region(), state_p);
			_triggerbox.set_from_selection (n, source->the_region());
			context->drag_finish (true, false, time);
		} else {
			context->drag_finish (false, false, time);
		}
		return;
	}

	std::vector<std::string> paths;
	if (ARDOUR_UI_UTILS::convert_drop_to_paths (paths, data)) {
		for (std::vector<std::string>::iterator s = paths.begin (); s != paths.end (); ++s) {
			/* this will do nothing if n is too large */
			_triggerbox.set_from_path (n, *s);
#if 1 /* assume drop from sidebar -- TODO use a special data.get_target() ? */
			ARDOUR_UI_UTILS::copy_patch_changes (_triggerbox.session().the_auditioner (), _triggerbox.trigger (n));
#endif
			++n;
		}
	}
	context->drag_finish (true, false, time);
}

TriggerEntry*
TriggerBoxUI::entry_by_trigger (Trigger const & trigger) const
{
	/* Spite: a trigger of another box is in none of our rows, and one of ours
	 * is in the row of its index, if that is on screen.
	 */
	if (&trigger.box () != &_triggerbox) {
		return nullptr;
	}
	TriggerEntry* e = entry_for_slot (trigger.index ());
	return (e && e->trigger_if_any ().get () == &trigger) ? e : nullptr;
}

/* ********************************************** */

TriggerBoxWidget::TriggerBoxWidget (TriggerStrip& s, float w, float h)
	: FittedCanvasWidget (w, h)
	, _ui (nullptr)
	, _strip (s)
{
	use_intermediate_surface (false);
	set_background_color (UIConfiguration::instance ().color (X_("theme:bg")));
}

void
TriggerBoxWidget::set_triggerbox (TriggerBox* tb)
{
	if (_ui) {
		root ()->remove (_ui);
		delete _ui;
		_ui = nullptr;
	}

	if (!tb) {
		return;
	}

	_ui = new TriggerBoxUI (root (), _strip, *tb);

	/* Spite: the grid fills its strip's height, whatever the scene count */
	tb->session ().SceneCountChanged.connect (_scene_connection, invalidator (*this), std::bind (&TriggerBoxWidget::scenes_changed, this), gui_context ());

	repeat_size_allocation ();
}

void
TriggerBoxWidget::scenes_changed ()
{
	if (!_ui) {
		return;
	}
	_ui->build ();
}

void
TriggerBoxWidget::on_size_allocate (Gtk::Allocation& alloc)
{
	FittedCanvasWidget::on_size_allocate (alloc);
	SceneScroll::instance ().set_view_height (alloc.get_height ());
}

bool
TriggerBoxWidget::on_scroll_event (GdkEventScroll* ev)
{
	if (FittedCanvasWidget::on_scroll_event (ev)) {
		return true;
	}
	return SceneScroll::instance ().scroll_event (ev);
}
