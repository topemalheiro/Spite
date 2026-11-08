/*
 * Copyright (C) 2014-2017 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2014-2017 Robin Gareus <robin@gareus.org>
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

#include <inttypes.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <float.h>
#include <locale.h>
#include <string>

#include <glibmm.h>

#include "pbd/cartesian.h"
#include "pbd/convert.h"
#include "pbd/enumwriter.h"
#include "pbd/error.h"
#include "pbd/failed_constructor.h"
#include "pbd/xml++.h"

#include "evoral/Curve.h"

#include "ardour/audio_buffer.h"
#include "ardour/buffer_set.h"
#include "ardour/mix.h"
#include "ardour/pan_controllable.h"
#include "ardour/pannable.h"
#include "ardour/panner_manager.h"
#include "ardour/runtime_functions.h"
#include "ardour/session.h"
#include "ardour/utils.h"

#include "panner_balance.h"

#include "pbd/i18n.h"

#include "pbd/mathfix.h"

using namespace std;
using namespace ARDOUR;
using namespace PBD;

static PanPluginDescriptor _descriptor = {
	"Stereo Balance",
	"http://ardour.org/plugin/panner_balance",
	"http://ardour.org/plugin/panner_balance#ui",
	2, 2,
	25,
	Pannerbalance::factory
};

extern "C" ARDOURPANNER_API PanPluginDescriptor*
panner_descriptor ()
{
	return &_descriptor;
}

Pannerbalance::Pannerbalance (std::shared_ptr<Pannable> p)
	: Panner (p)
	, _n_in (2)
	, _n_out (2)
{
	if (!_pannable->has_state ()) {
		_pannable->pan_azimuth_control->set_value (0.5, Controllable::NoGroup);
	}

	_can_automate_list.insert (Evoral::Parameter (PanAzimuthAutomation));

	update ();
	reset_glide ();

	_pannable->pan_azimuth_control->Changed.connect_same_thread (*this, std::bind (&Pannerbalance::update, this));
}

Pannerbalance::~Pannerbalance ()
{
}

void
Pannerbalance::reset_glide ()
{
	/* input 0 is left, 1 right; with more, odd inputs are left and even right (counting from 1) */
	pos.assign (_n_in, 1.f);
	pos_interp.assign (_n_in, 1.f);
	for (uint32_t i = 0; i < _n_in; ++i) {
		pos_interp[i] = pos[i] = desired_pos[i % 2];
	}
}

void
Pannerbalance::configure_io (ChanCount in, ChanCount out)
{
	/* Spite: called on a new panner, with the process lock held */
	uint32_t const n_in  = in.n_audio ();
	uint32_t const n_out = out.n_audio ();
	if (PannerManager::stereo_pairs (n_in, n_out)) {
		_n_in  = n_in;
		_n_out = n_out;
	} else {
		_n_in  = 2;
		_n_out = 2;
	}
	reset_glide ();
}

double
Pannerbalance::position () const
{
	return _pannable->pan_azimuth_control->get_value ();
}

void
Pannerbalance::set_position (double p)
{
	if (clamp_position (p)) {
		_pannable->pan_azimuth_control->set_value (p, Controllable::NoGroup);
	}
}

void
Pannerbalance::thaw ()
{
	Panner::thaw ();
	if (_frozen == 0) {
		update ();
	}
}

void
Pannerbalance::update ()
{
	if (_frozen) {
		return;
	}

	float const pos = _pannable->pan_azimuth_control->get_value ();

	if (pos == .5) {
		desired_pos[0] = 1.0;
		desired_pos[1] = 1.0;
	} else if (pos > .5) {
		desired_pos[0] = 2 - 2. * pos;
		desired_pos[1] = 1.0;
	} else {
		desired_pos[0] = 1.0;
		desired_pos[1] = 2. * pos;
	}
}

bool
Pannerbalance::clamp_position (double& p)
{
	p = max (min (p, 1.0), 0.0);
	return true;
}

pair<double, double>
Pannerbalance::position_range () const
{
	return make_pair (0, 1);
}

void
Pannerbalance::distribute_one (AudioBuffer& srcbuf, BufferSet& obufs, gain_t gain_coeff, pframes_t nframes, uint32_t which)
{
	/* Spite: an input pans as its side (which % 2). With as many outputs as inputs it goes to its
	 * own output, with two outputs it's mixed into its side's. Stereo is unchanged. */
	uint32_t const side = which % 2;
	uint32_t const o    = (_n_out == _n_in) ? which : side;

	if (which >= pos.size () || o >= obufs.count ().n_audio ()) {
		return;
	}

	pan_t   delta;
	Sample* dst;
	pan_t   pan;

	Sample* const src = srcbuf.data ();

	dst = obufs.get_audio (o).data ();

	if (fabsf ((delta = (pos[which] - desired_pos[side]))) > 0.002) { // about 1 degree of arc

		/* we've moving the pan by an appreciable amount, so we must
		 * interpolate over 64 samples or nframes, whichever is smaller */

		pframes_t const limit = min ((pframes_t)64, nframes);
		pframes_t       n;

		delta = -(delta / (float)(limit));

		for (n = 0; n < limit; n++) {
			pos_interp[which] = pos_interp[which] + delta;
			pos[which]        = pos_interp[which] + 0.9 * (pos[which] - pos_interp[which]);
			dst[n] += src[n] * pos[which] * gain_coeff;
		}

		/* then pan the rest of the buffer; no need for interpolation for this bit */

		pan = pos[which] * gain_coeff;

		mix_buffers_with_gain (dst + n, src + n, nframes - n, pan);

	} else {
		pos[which]        = desired_pos[side];
		pos_interp[which] = pos[which];

		if ((pan = (pos[which] * gain_coeff)) != 1.0f) {
			if (pan != 0.0f) {
				/* pan is 1 but also not 0, so we must do it "properly" */

				mix_buffers_with_gain (dst, src, nframes, pan);

				/* XXX it would be nice to mark the buffer as written to */
			}

		} else {
			/* pan is 1 so we can just copy the input samples straight in */
			mix_buffers_no_gain (dst, src, nframes);
		}
	}
}

void
Pannerbalance::distribute_one_automated (AudioBuffer& srcbuf, BufferSet& obufs,
                                         samplepos_t start, samplepos_t end, pframes_t nframes,
                                         pan_t** buffers, uint32_t which)
{
	/* Spite: as in distribute_one (); the gains for a side go into buffers[side] */
	uint32_t const side = which % 2;
	uint32_t const o    = (_n_out == _n_in) ? which : side;

	if (o >= obufs.count ().n_audio ()) {
		return;
	}

	Sample*       dst;
	pan_t*        pbuf;
	Sample* const src      = srcbuf.data ();
	pan_t* const  position = buffers[0];

	/* fetch positional data */

	if (!_pannable->pan_azimuth_control->list ()->curve ().rt_safe_get_vector (timepos_t (start), timepos_t (end), position, nframes)) {
		/* fallback */
		distribute_one (srcbuf, obufs, 1.0, nframes, which);
		return;
	}

	for (pframes_t n = 0; n < nframes; ++n) {
		float const pos = position[n];

		if (side == 0) { // Left
			if (pos > .5) {
				buffers[side][n] = 2 - 2. * pos;
			} else {
				buffers[side][n] = 1.0;
			}
		} else { // Right
			if (pos < .5) {
				buffers[side][n] = 2. * pos;
			} else {
				buffers[side][n] = 1.0;
			}
		}
	}

	dst  = obufs.get_audio (o).data ();
	pbuf = buffers[side];

	for (pframes_t n = 0; n < nframes; ++n) {
		dst[n] += src[n] * pbuf[n];
	}

	/* XXX it would be nice to mark the buffer as written to */
}

Panner*
Pannerbalance::factory (std::shared_ptr<Pannable> p, std::shared_ptr<Speakers> /* ignored */)
{
	return new Pannerbalance (p);
}

XMLNode&
Pannerbalance::get_state () const
{
	XMLNode& root (Panner::get_state ());
	root.set_property (X_ ("uri"), _descriptor.panner_uri);
	/* this is needed to allow new sessions to load with old Ardour: */
	root.set_property (X_ ("type"), _descriptor.name);
	return root;
}

string
Pannerbalance::value_as_string (std::shared_ptr<const AutomationControl> ac) const
{
	double val = ac->get_value ();

	switch (ac->parameter ().type ()) {
		case PanAzimuthAutomation:
			/* We show the position of the center of the image relative to the left & right.
			 * This is expressed as a pair of percentage values that ranges from (100,0)
			 * (hard left) through (50,50) (hard center) to (0,100) (hard right).
			 *
			 * This is pretty weird, but its the way audio engineers expect it. Just remember that
			 * the center of the USA isn't Kansas, its (50LA, 50NY) and it will all make sense.
			 *
			 * This is designed to be as narrow as possible. Dedicated
			 * panner GUIs can do their own version of this if they need
			 * something less compact.
			 */

			return string_compose (_ ("L%1R%2"), (int)rint (100.0 * (1.0 - val)),
			                       (int)rint (100.0 * val));

		default:
			return _ ("unused");
	}
}

void
Pannerbalance::reset ()
{
	set_position (0.5);
	update ();
}
