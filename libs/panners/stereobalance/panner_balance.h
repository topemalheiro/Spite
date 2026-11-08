/*
 * Copyright (C) 2016-2017 Paul Davis <paul@linuxaudiosystems.com>
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

#ifndef __ardour_panner_balance_h__
#define __ardour_panner_balance_h__

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "pbd/cartesian.h"
#include "pbd/controllable.h"
#include "pbd/stateful.h"

#include "ardour/automatable.h"
#include "ardour/automation_control.h"
#include "ardour/panner.h"
#include "ardour/types.h"

namespace ARDOUR
{
class Pannerbalance : public Panner
{
public:
	Pannerbalance (std::shared_ptr<Pannable>);
	~Pannerbalance ();

	/* Spite: 2 in and 2 out, or more channels as stereo pairs, see PannerManager::stereo_pairs () */
	ChanCount in () const
	{
		return ChanCount (DataType::AUDIO, _n_in);
	}
	ChanCount out () const
	{
		return ChanCount (DataType::AUDIO, _n_out);
	}
	void configure_io (ChanCount in, ChanCount out);

	void                      set_position (double);
	bool                      clamp_position (double&);
	std::pair<double, double> position_range () const;
	double                    position () const;

	static Panner* factory (std::shared_ptr<Pannable>, std::shared_ptr<Speakers>);

	std::string value_as_string (std::shared_ptr<const AutomationControl>) const;

	XMLNode& get_state () const;

	void reset ();
	void thaw ();

protected:
	float              desired_pos[2]; /* left, right */
	std::vector<float> pos;            /* per input, gliding to its side's desired_pos */
	std::vector<float> pos_interp;
	uint32_t           _n_in;
	uint32_t           _n_out;

	void reset_glide ();

	void update ();

private:
	void distribute_one (AudioBuffer& srcbuf, BufferSet& obufs, gain_t gain_coeff, pframes_t nframes, uint32_t which);
	void distribute_one_automated (AudioBuffer& srcbuf, BufferSet& obufs,
	                               samplepos_t start, samplepos_t end, pframes_t nframes,
	                               pan_t** buffers, uint32_t which);
};

} // namespace ARDOUR

#endif /* __ardour_panner_balance_h__ */
