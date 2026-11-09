/*
 * Copyright (C) 2007-2017 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2015-2016 Robin Gareus <robin@gareus.org>
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

#include "ardour/triggerbox.h"

/* Spite: the selected clip slots, by box and scene (TriggerReference), not by
 * the rows that show them: the Cue page only has rows for the scenes on
 * screen, so a selected slot may have none. Inserting or removing scenes
 * moves them along, see scenes_shifted ().
 */
class TriggerSelection : public std::list<ARDOUR::TriggerReference>
{
public:
	static bool same (ARDOUR::TriggerReference const& a, ARDOUR::TriggerReference const& b)
	{
		return a.slot () == b.slot () && a.box () == b.box ();
	}

	iterator find (ARDOUR::TriggerReference const& r)
	{
		for (iterator i = begin (); i != end (); ++i) {
			if (same (*i, r)) {
				return i;
			}
		}
		return end ();
	}

	bool contains (ARDOUR::TriggerReference const& r) const
	{
		for (auto const& t : *this) {
			if (same (t, r)) {
				return true;
			}
		}
		return false;
	}

	/* scenes were inserted before `at` (delta > 0), or `-delta` scenes were
	 * removed from `at` on (delta < 0): true if the selection changed */
	bool scenes_shifted (uint32_t at, int32_t delta)
	{
		bool changed = false;
		for (iterator i = begin (); i != end ();) {
			uint32_t const s = i->slot ();
			if (s < at) {
				++i;
				continue;
			}
			changed = true;
			if (delta < 0 && s < at + (uint32_t) -delta) {
				i = erase (i); /* its scene went */
				continue;
			}
			i->set (i->box (), s + delta);
			++i;
		}
		return changed;
	}
};
