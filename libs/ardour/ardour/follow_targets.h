/*
 * Copyright (C) 2026 Spite
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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ardour/libardour_visibility.h"

namespace ARDOUR {

/* Spite: the scenes a Jump follow action can go to, as sorted, disjoint
 * runs of scene numbers [lo, hi], so that their number is unlimited (this
 * used to be a bitset, which set the most scenes a session could have).
 *
 * Sets are interned: equal sets are the same object, which is immutable and
 * never freed, so a FollowAction (which holds a pointer) stays trivially
 * copyable and the audio thread can read any copy of one. The empty set is
 * nullptr; the static calls accept it.
 */
class LIBARDOUR_API FollowTargets
{
public:
	struct Run {
		uint32_t lo;
		uint32_t hi;
		bool operator< (Run const& o) const { return lo < o.lo || (lo == o.lo && hi < o.hi); }
		bool operator== (Run const& o) const { return lo == o.lo && hi == o.hi; }
	};
	typedef std::vector<Run> Runs;

	/* not the audio thread: these may allocate (once per distinct set) */
	static FollowTargets const* intern (Runs);   /* any runs: sorted and merged here */
	static FollowTargets const* single (uint32_t scene);
	static FollowTargets const* from_mask (uint64_t);
	static FollowTargets const* parse (std::string const&, bool* ok = nullptr); /* "3,10-40,512" */
	static FollowTargets const* toggled (FollowTargets const*, uint32_t scene);
	/* a scene inserted (delta 1) or removed (-1) at `at`: the targets keep
	 * their scenes, and a removed scene's target goes
	 */
	static FollowTargets const* shifted (FollowTargets const*, uint32_t at, int delta);
	static std::string to_string (FollowTargets const*); /* "3,10-40,512": a-b for runs of 3 or more */

	/* any thread */
	static bool contains (FollowTargets const*, uint32_t scene);
	static size_t count (FollowTargets const*);

	Runs const& runs () const { return _runs; }
	uint32_t first () const { return _runs.front ().lo; }
	uint32_t last () const { return _runs.back ().hi; }
	/* all within the first 64 scenes: the old saved form (a 64-bit mask), which older builds read */
	bool fits_mask () const { return _runs.back ().hi < 64; }
	uint64_t mask () const;

private:
	FollowTargets (Runs const&);
	Runs   _runs;
	size_t _count;
};

} // namespace ARDOUR
