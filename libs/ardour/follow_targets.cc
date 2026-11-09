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

#include <algorithm>
#include <cstdlib>
#include <map>
#include <mutex>
#include <sstream>

#include "ardour/follow_targets.h"

using namespace ARDOUR;

FollowTargets::FollowTargets (Runs const& r)
	: _runs (r)
	, _count (0)
{
	for (auto const& x : _runs) {
		_count += (size_t) x.hi - x.lo + 1;
	}
}

FollowTargets const*
FollowTargets::intern (Runs r)
{
	/* sort and merge overlapping or adjacent runs */
	std::sort (r.begin (), r.end ());
	Runs m;
	for (auto const& x : r) {
		if (x.hi < x.lo) {
			continue;
		}
		if (!m.empty () && (uint64_t) x.lo <= (uint64_t) m.back ().hi + 1) {
			m.back ().hi = std::max (m.back ().hi, x.hi);
		} else {
			m.push_back (x);
		}
	}
	if (m.empty ()) {
		return nullptr;
	}

	/* never freed: any copy anywhere stays valid */
	static std::mutex                                lock;
	static std::map<Runs, FollowTargets const*>* table = new std::map<Runs, FollowTargets const*>;

	std::lock_guard<std::mutex> lm (lock);
	auto i = table->find (m);
	if (i != table->end ()) {
		return i->second;
	}
	FollowTargets const* t = new FollowTargets (m);
	table->insert (std::make_pair (m, t));
	return t;
}

FollowTargets const*
FollowTargets::single (uint32_t scene)
{
	return intern (Runs (1, Run { scene, scene }));
}

FollowTargets const*
FollowTargets::from_mask (uint64_t mask)
{
	Runs r;
	for (uint32_t n = 0; n < 64; ++n) {
		if (mask & (uint64_t (1) << n)) {
			r.push_back (Run { n, n });
		}
	}
	return intern (r);
}

uint64_t
FollowTargets::mask () const
{
	uint64_t m = 0;
	for (auto const& x : _runs) {
		for (uint32_t n = x.lo; n <= x.hi && n < 64; ++n) {
			m |= uint64_t (1) << n;
		}
	}
	return m;
}

FollowTargets const*
FollowTargets::parse (std::string const& str, bool* ok)
{
	Runs r;
	bool good = true;
	std::stringstream ss (str);
	std::string tok;
	while (std::getline (ss, tok, ',')) {
		if (tok.empty ()) {
			continue;
		}
		char* end = 0;
		unsigned long const lo = strtoul (tok.c_str (), &end, 10);
		if (end == tok.c_str ()) {
			good = false;
			continue;
		}
		unsigned long hi = lo;
		if (*end == '-') {
			char const* s = end + 1;
			hi = strtoul (s, &end, 10);
			if (end == s || hi < lo) {
				good = false;
				continue;
			}
		}
		if (hi > UINT32_MAX) {
			good = false;
			continue;
		}
		r.push_back (Run { (uint32_t) lo, (uint32_t) hi });
	}
	if (ok) {
		*ok = good;
	}
	return intern (r);
}

std::string
FollowTargets::to_string (FollowTargets const* t)
{
	if (!t) {
		return std::string ();
	}
	std::stringstream ss;
	bool first = true;
	for (auto const& x : t->_runs) {
		if (x.hi - x.lo >= 2) {
			ss << (first ? "" : ",") << x.lo << '-' << x.hi;
		} else {
			for (uint32_t n = x.lo; n <= x.hi; ++n) {
				ss << (first ? "" : ",") << n;
				first = false;
			}
		}
		first = false;
	}
	return ss.str ();
}

bool
FollowTargets::contains (FollowTargets const* t, uint32_t scene)
{
	if (!t) {
		return false;
	}
	/* the last run starting at or before scene */
	Runs const& r (t->_runs);
	auto i = std::upper_bound (r.begin (), r.end (), scene, [] (uint32_t s, Run const& x) { return s < x.lo; });
	if (i == r.begin ()) {
		return false;
	}
	--i;
	return scene <= i->hi;
}

size_t
FollowTargets::count (FollowTargets const* t)
{
	return t ? t->_count : 0;
}

FollowTargets const*
FollowTargets::toggled (FollowTargets const* t, uint32_t scene)
{
	Runs r;
	if (t) {
		r = t->_runs;
	}
	if (!contains (t, scene)) {
		r.push_back (Run { scene, scene });
		return intern (r);
	}
	Runs out;
	for (auto const& x : r) {
		if (scene < x.lo || scene > x.hi) {
			out.push_back (x);
			continue;
		}
		if (scene > x.lo) {
			out.push_back (Run { x.lo, scene - 1 });
		}
		if (scene < x.hi) {
			out.push_back (Run { scene + 1, x.hi });
		}
	}
	return intern (out);
}

FollowTargets const*
FollowTargets::shifted (FollowTargets const* t, uint32_t at, int delta)
{
	if (!t || delta == 0) {
		return t;
	}
	Runs out;
	for (auto const& x : t->_runs) {
		if (delta > 0) {
			/* scenes from `at` move up one; `at` itself is the new scene */
			if (x.hi < at) {
				out.push_back (x);
			} else if (x.lo >= at) {
				out.push_back (Run { x.lo + 1, x.hi + 1 });
			} else {
				out.push_back (Run { x.lo, at - 1 });
				out.push_back (Run { at + 1, x.hi + 1 });
			}
		} else {
			/* scene `at` goes, the ones after it move down one */
			uint32_t const lo = x.lo > at ? x.lo - 1 : x.lo;
			uint32_t const hi = x.hi >= at ? x.hi - 1 : x.hi;
			if (x.hi >= at && x.hi == 0) {
				continue; /* only scene 0, removed */
			}
			if (hi >= lo) {
				out.push_back (Run { lo, hi });
			}
		}
	}
	FollowTargets const* n = intern (out);
	return n;
}
