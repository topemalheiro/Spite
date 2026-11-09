/*
 * Copyright (C) 2026 Spite contributors
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

#include "pbd/compose.h"

#include "ardour/scene_command.h"
#include "ardour/session.h"
#include "ardour/triggerbox.h"

#include "ardour_message.h"
#include "ardour_ui.h"
#include "public_editor.h"
#include "scene_actions.h"
#include "scene_scroll.h"
#include "selection.h"
#include "triggerbox_ui.h"
#include "utils.h"

#include "pbd/i18n.h"

using namespace ARDOUR;

static int32_t _selected_scene = -1;

void
SceneActions::set_selected_scene (int32_t n)
{
	_selected_scene = n;
}

void
SceneActions::scenes_shifted (uint32_t at, int32_t delta)
{
	if (delta == 0) {
		return;
	}
	Selection& sel (PublicEditor::instance ().get_selection ());
	if (sel.triggers.scenes_shifted (at, delta)) {
		sel.TriggersChanged ();
	}
	if (_selected_scene >= (int32_t) at) {
		if (delta > 0) {
			_selected_scene += delta;
		} else if (_selected_scene >= (int32_t) at - delta) {
			_selected_scene += delta;
		} else {
			_selected_scene = at; /* its scene went: the one after it */
		}
	}
}

void
SceneActions::go_to_scene ()
{
	Session* s = ARDOUR_UI::instance ()->the_session ();
	if (!s || s->scene_count () == 0) {
		return;
	}
	uint32_t const cur = _selected_scene >= 0 ? (uint32_t) _selected_scene : SceneScroll::instance ().first_row ();
	int const      n   = ARDOUR_UI_UTILS::ask_scene_number (_("Go to Scene"), s->scene_count (), cur);
	if (n < 0) {
		return;
	}
	_selected_scene = n;
	SceneScroll::instance ().show_row (n);
}

void
SceneActions::insert_scene ()
{
	Session* s = ARDOUR_UI::instance ()->the_session ();
	if (!s) {
		return;
	}

	uint32_t at = s->scene_count ();

	TriggerSelection const& ts (PublicEditor::instance ().get_selection ().triggers);
	if (!ts.empty ()) {
		uint32_t row = 0;
		for (auto const& tr : ts) {
			row = std::max (row, tr.slot ());
		}
		at = row + 1;
	} else if (_selected_scene >= 0 && _selected_scene < (int32_t) s->scene_count ()) {
		at = _selected_scene + 1;
	}

	insert_scene_at (at);
}

void
SceneActions::insert_scene_at (uint32_t at)
{
	Session* s = ARDOUR_UI::instance ()->the_session ();
	if (!s) {
		return;
	}

	if (s->scene_count () >= (uint32_t) TriggerBox::max_triggers_per_box) {
		ArdourMessageDialog msg (string_compose (_("A session can have at most %1 scenes."), TriggerBox::max_triggers_per_box));
		msg.run ();
		return;
	}

	at = std::min (at, s->scene_count ());

	s->begin_reversible_command (_("insert scene"));
	if (s->insert_scene (at)) {
		s->add_command (new SceneCommand (*s, SceneCommand::Insert, at));
		s->commit_reversible_command ();
		/* like Live, the new scene is the selected one: Ctrl+I again adds below it */
		_selected_scene = at;
	} else {
		s->abort_reversible_command ();
	}
}

bool
SceneActions::can_delete_scene (uint32_t at)
{
	Session* s = ARDOUR_UI::instance ()->the_session ();
	return s && s->scene_removable (at);
}

void
SceneActions::delete_scene (uint32_t at)
{
	Session* s = ARDOUR_UI::instance ()->the_session ();
	if (!s || !s->scene_removable (at)) {
		return;
	}

	s->begin_reversible_command (_("delete scene"));
	if (s->remove_scene (at)) {
		s->add_command (new SceneCommand (*s, SceneCommand::Remove, at));
		s->commit_reversible_command ();
		if (_selected_scene >= (int32_t) s->scene_count ()) {
			_selected_scene = (int32_t) s->scene_count () - 1;
		}
	} else {
		s->abort_reversible_command ();
	}
}
