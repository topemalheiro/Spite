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

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <set>
#include <utility>

#include <pthread.h>

#include <glibmm/main.h>

#include "pbd/compose.h"
#include "pbd/error.h"
#include "pbd/types_convert.h"
#include "pbd/xml++.h"

#include "midi++/parser.h"

#include "ardour/audioengine.h"
#include "ardour/chan_count.h"
#include "ardour/chan_mapping.h"
#include "ardour/internal_send.h"
#include "ardour/io.h"
#include "ardour/meter.h"
#include "ardour/midi_port.h"
#include "ardour/panner_shell.h"
#include "ardour/plugin.h"
#include "ardour/plugin_insert.h"
#include "ardour/port.h"
#include "ardour/port_set.h"
#include "ardour/route.h"
#include "ardour/send.h"
#include "ardour/session.h"
#include "ardour/unknown_processor.h"

#include "mcr_cables.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace PBD;

/** a pin of a target device that cables land on */
struct McrCables::PinRecord
{
	DataType    type;
	uint32_t    instance;
	uint32_t    pin;
	std::string port;  ///< the sidechain port it reads, by its own name ("audio_in 2")
	std::string prev;  ///< the sidechain port it read before its first cable, if any
	bool        added; ///< the MCR added that port to the sidechain
	int         main;  ///< the track's buffer it read before its first cable (a MIDI input its track fed), else -1

	PinRecord ()
		: type (DataType::NIL)
		, instance (0)
		, pin (0)
		, added (false)
		, main (-1)
	{}
};

/** what the MCR did to a target device, saved with it as extra XML */
struct McrCables::TargetRecord
{
	bool                                          created;   ///< the MCR added the device's sidechain
	std::string                                   sidechain; ///< the id of the sidechain (its IO) this is about; empty: not known yet
	std::vector<PinRecord>                        pins;
	std::vector<std::pair<DataType, std::string> > spares;   ///< ports the MCR added that no cable uses now, lower numbered than one in use

	TargetRecord ()
		: created (false)
	{}

	bool empty () const { return !created && pins.empty () && spares.empty (); }
};

namespace {

const char* const send_tag   = X_("MCRCable");
const char* const target_tag = X_("MCRSidechain");

typedef std::vector<std::shared_ptr<Processor> > Processors;

/** a pin that reads a sidechain buffer, and the port behind that buffer */
struct ScPin
{
	uint32_t              instance = 0;
	DataType              type     = DataType::NIL;
	uint32_t              pin      = 0;
	std::shared_ptr<Port> port;
};

/** a sidechain port, with the device and route it belongs to */
struct Sink
{
	std::shared_ptr<Route>        route;
	std::shared_ptr<PluginInsert> device;
	DataType                      type  = DataType::NIL;
	uint32_t                      index = 0;
	std::shared_ptr<Port>         port;
};

/** a copy of a route's processor list, so that nothing is called with the route's processor lock held */
Processors
processors_of (std::shared_ptr<Route> const& r)
{
	Processors rv;
	r->foreach_processor ([&rv] (std::weak_ptr<Processor> w) {
		std::shared_ptr<Processor> p (w.lock ());
		if (p) {
			rv.push_back (p);
		}
	});
	return rv;
}

std::shared_ptr<Processor>
processor_after (Processors const& pl, std::shared_ptr<Processor> const& p)
{
	Processors::const_iterator i = std::find (pl.begin (), pl.end (), p);
	if (i == pl.end () || ++i == pl.end ()) {
		return std::shared_ptr<Processor> ();
	}
	return *i;
}

/** @return true when @a s is one of the MCR's sends, with its source device's id in @a id */
bool
mcr_source (std::shared_ptr<Send> const& s, PBD::ID& id)
{
	XMLNode* x = s ? s->extra_xml (send_tag) : 0;
	return x && x->get_property (X_("source"), id);
}

bool
is_pgd4s (std::shared_ptr<PluginInsert> const& pi)
{
	std::shared_ptr<Plugin> p (pi->plugin ());
	return p && p->unique_id ().rfind ("https://github.com/timothyschoen/plugdata", 0) == 0;
}

/** a port's own name within its IO ("audio_in 2"); it survives renaming the IO and renumbering its ports */
std::string
short_name (std::shared_ptr<Port> const& p)
{
	std::string const& n (p->name ());
	std::string::size_type const s = n.rfind ('/');
	return s == std::string::npos ? n : n.substr (s + 1);
}

std::shared_ptr<Port>
port_by_short_name (std::shared_ptr<IO> const& io, DataType dt, std::string const& name)
{
	if (!io || name.empty ()) {
		return std::shared_ptr<Port> ();
	}
	std::shared_ptr<PortSet> ps (io->ports ());
	for (uint32_t j = 0; j < ps->num_ports (dt); ++j) {
		std::shared_ptr<Port> p (ps->port (dt, j));
		if (short_name (p) == name) {
			return p;
		}
	}
	return std::shared_ptr<Port> ();
}

/** @return the index of @a p among the ports of its type in @a io (they are sorted by number), or -1 */
int
port_index (std::shared_ptr<IO> const& io, std::shared_ptr<Port> const& p)
{
	if (!io || !p) {
		return -1;
	}
	std::shared_ptr<PortSet> ps (io->ports ());
	for (uint32_t j = 0; j < ps->num_ports (p->type ()); ++j) {
		if (ps->port (p->type (), j) == p) {
			return j;
		}
	}
	return -1;
}

uint32_t
n_connections (std::shared_ptr<IO> const& io)
{
	uint32_t n = 0;
	std::shared_ptr<PortSet> ps (io->ports ());
	for (uint32_t i = 0; i < ps->num_ports (); ++i) {
		std::vector<std::string> c;
		ps->port (i)->get_connections (c);
		n += c.size ();
	}
	return n;
}

/** the record as it was saved, whatever sidechain it was made for */
McrCables::TargetRecord
read_raw_record (std::shared_ptr<PluginInsert> const& pi)
{
	McrCables::TargetRecord rec;
	XMLNode* node = pi->extra_xml (target_tag);
	if (!node) {
		return rec;
	}
	node->get_property (X_("created"), rec.created);
	node->get_property (X_("sidechain"), rec.sidechain);
	for (XMLNode const* child : node->children ()) {
		std::string type;
		if (child->name () == X_("Spare")) {
			std::string port;
			if (child->get_property (X_("type"), type) && child->get_property (X_("port"), port) && DataType (type) != DataType::NIL) {
				rec.spares.push_back (std::make_pair (DataType (type), port));
			}
			continue;
		}
		McrCables::PinRecord p;
		if (child->name () != X_("Pin") || !child->get_property (X_("type"), type) || !child->get_property (X_("pin"), p.pin) || !child->get_property (X_("port"), p.port)) {
			continue;
		}
		p.type = DataType (type);
		if (p.type != DataType::AUDIO && p.type != DataType::MIDI) {
			continue;
		}
		child->get_property (X_("instance"), p.instance);
		child->get_property (X_("prev"), p.prev);
		child->get_property (X_("added"), p.added);
		child->get_property (X_("main"), p.main);
		rec.pins.push_back (p);
	}
	return rec;
}

/** a record made for another sidechain than @a pi has now (removed, or removed and added
 * again, elsewhere: the pin dialog) is stale: its port names can match ports the MCR didn't make
 */
bool
record_stale (std::shared_ptr<PluginInsert> const& pi, McrCables::TargetRecord const& rec)
{
	std::shared_ptr<IO> sc (pi->sidechain_input ());
	return !sc || (!rec.sidechain.empty () && rec.sidechain != sc->id ().to_s ());
}

/** the record about @a pi's sidechain as it is now: empty when it has none, or the record is stale */
McrCables::TargetRecord
read_record (std::shared_ptr<PluginInsert> const& pi)
{
	McrCables::TargetRecord const rec (read_raw_record (pi));
	if (record_stale (pi, rec)) {
		return McrCables::TargetRecord ();
	}
	return rec;
}

void
write_record (std::shared_ptr<PluginInsert> const& pi, McrCables::TargetRecord const& rec)
{
	XMLNode* node = new XMLNode (target_tag);
	if (!rec.empty ()) {
		node->set_property (X_("created"), rec.created);
		if (std::shared_ptr<IO> sc = pi->sidechain_input ()) {
			node->set_property (X_("sidechain"), sc->id ().to_s ());
		}
	}
	for (auto const& p : rec.pins) {
		XMLNode* child = node->add_child (X_("Pin"));
		child->set_property (X_("type"), std::string (p.type.to_string ()));
		child->set_property (X_("instance"), p.instance);
		child->set_property (X_("pin"), p.pin);
		child->set_property (X_("port"), p.port);
		if (!p.prev.empty ()) {
			child->set_property (X_("prev"), p.prev);
		}
		child->set_property (X_("added"), p.added);
		if (p.main >= 0) {
			child->set_property (X_("main"), p.main);
		}
	}
	for (auto const& s : rec.spares) {
		XMLNode* child = node->add_child (X_("Spare"));
		child->set_property (X_("type"), std::string (s.first.to_string ()));
		child->set_property (X_("port"), s.second);
	}
	pi->add_extra_xml (*node); /* replaces the previous record (an empty one: none) */
}

/** the pins of @a pi that read a sidechain buffer, with the ports behind them */
std::vector<ScPin>
sidechain_pins (std::shared_ptr<PluginInsert> const& pi)
{
	std::vector<ScPin> rv;
	std::shared_ptr<IO> sc (pi->sidechain_input ());
	if (!sc) {
		return rv;
	}
	ChanCount in, out;
	pi->configured_io (in, out);
	std::shared_ptr<PortSet> ps (sc->ports ());
	for (uint32_t n = 0; n < pi->get_count (); ++n) {
		ChanMapping const m (pi->input_map (n));
		for (DataType::iterator t = DataType::begin (); t != DataType::end (); ++t) {
			for (uint32_t i = 0; i < pi->natural_input_streams ().get (*t); ++i) {
				bool valid;
				uint32_t const idx = m.get (*t, i, &valid);
				if (!valid || idx < in.get (*t) || idx - in.get (*t) >= ps->num_ports (*t)) {
					continue;
				}
				ScPin p;
				p.instance = n;
				p.type     = *t;
				p.pin      = i;
				p.port     = ps->port (*t, idx - in.get (*t));
				rv.push_back (p);
			}
		}
	}
	return rv;
}

/** map @a pins to their ports' buffers again (after sidechain ports were added or
 * removed, which renumbers them); any other sidechain pin is unwired
 */
void
restore_sidechain_pins (std::shared_ptr<PluginInsert> const& pi, std::vector<ScPin> const& pins)
{
	std::shared_ptr<IO> sc (pi->sidechain_input ());
	ChanCount in, out;
	pi->configured_io (in, out);
	for (uint32_t n = 0; n < pi->get_count (); ++n) {
		ChanMapping const old (pi->input_map (n));
		ChanMapping m (old);
		for (DataType::iterator t = DataType::begin (); t != DataType::end (); ++t) {
			for (uint32_t i = 0; i < pi->natural_input_streams ().get (*t); ++i) {
				bool valid;
				uint32_t const idx = m.get (*t, i, &valid);
				if (valid && idx >= in.get (*t)) {
					m.unset (*t, i);
				}
			}
		}
		for (auto const& p : pins) {
			int const j = p.instance == n ? port_index (sc, p.port) : -1;
			if (j >= 0) {
				m.unset (p.type, p.pin); /* ChanMapping::set () never replaces a pin's buffer */
				m.set (p.type, p.pin, in.get (p.type) + j);
			}
		}
		if (m != old) {
			pi->set_input_map (n, m);
		}
	}
}

/** a cable that replaced its track's MIDI on a pin went: the pin reads the track's buffer again */
void
restore_main_pin (std::shared_ptr<PluginInsert> const& pi, McrCables::PinRecord const& r)
{
	ChanCount in, out;
	pi->configured_io (in, out);
	if (r.main < 0 || (uint32_t) r.main >= in.get (r.type) || r.instance >= pi->get_count ()) {
		return;
	}
	ChanMapping m (pi->input_map (r.instance));
	bool        valid;
	m.get (r.type, r.pin, &valid);
	if (!valid) {
		m.set (r.type, r.pin, r.main);
		pi->set_input_map (r.instance, m);
	}
}

bool
is_spare (McrCables::TargetRecord const& rec, DataType dt, std::string const& name)
{
	return std::find (rec.spares.begin (), rec.spares.end (), std::make_pair (dt, name)) != rec.spares.end ();
}

/** a sidechain port no cable or pin uses (a spare first, as it has the lowest number) */
std::shared_ptr<Port>
free_port (std::shared_ptr<PluginInsert> const& pi, McrCables::TargetRecord const& rec, DataType dt)
{
	std::shared_ptr<IO> sc (pi->sidechain_input ());
	std::vector<ScPin> const used (sidechain_pins (pi));
	std::shared_ptr<PortSet> ps (sc->ports ());
	for (uint32_t j = 0; j < ps->num_ports (dt); ++j) {
		std::shared_ptr<Port> p (ps->port (dt, j));
		std::string const name (short_name (p));
		if (p->connected ()
		    || std::find_if (used.begin (), used.end (), [&p] (ScPin const& s) { return s.port == p; }) != used.end ()
		    || std::find_if (rec.pins.begin (), rec.pins.end (), [&] (McrCables::PinRecord const& r) { return r.type == dt && r.port == name; }) != rec.pins.end ()) {
			continue;
		}
		return p;
	}
	return std::shared_ptr<Port> ();
}

/** remove the highest numbered ports of @a dt while they are spares, so that the
 * sidechain's ports stay numbered without gaps (a session reload numbers them from 1
 * again, and finds their connections by name). Writes the record when it changes it.
 */
void
trim_spares (std::shared_ptr<PluginInsert> const& pi, McrCables::TargetRecord& rec, DataType dt)
{
	std::shared_ptr<IO> sc (pi->sidechain_input ());
	if (!sc) {
		return;
	}
	std::vector<ScPin> const keep (sidechain_pins (pi));
	bool                     removed = false;

	while (sc->n_ports ().get (dt) > 0 && sc->n_ports ().n_total () > 1) {
		std::shared_ptr<Port> const last (sc->ports ()->port (dt, sc->n_ports ().get (dt) - 1));
		std::string const           name (short_name (last));
		if (!is_spare (rec, dt, name) || last->connected () || std::find_if (keep.begin (), keep.end (), [&last] (ScPin const& s) { return s.port == last; }) != keep.end ()) {
			break;
		}
		rec.spares.erase (std::find (rec.spares.begin (), rec.spares.end (), std::make_pair (dt, name)));
		write_record (pi, rec);
		sc->remove_port (last); /* locks by itself; the highest number: the others keep theirs */
		removed = true;
	}
	if (removed) {
		restore_sidechain_pins (pi, keep);
	}
}

/** the pins of @a pi that the sidechain port @a port reaches: its cable pins, else whatever the map says */
std::vector<std::pair<uint32_t, uint32_t> >
pins_reading (std::shared_ptr<PluginInsert> const& pi, std::shared_ptr<Port> const& port)
{
	std::vector<std::pair<uint32_t, uint32_t> > rv;
	std::string const name (short_name (port));
	for (auto const& p : read_record (pi).pins) {
		if (p.type == port->type () && p.port == name) {
			rv.push_back (std::make_pair (p.instance, p.pin));
		}
	}
	if (!rv.empty ()) {
		return rv;
	}
	for (auto const& p : sidechain_pins (pi)) {
		if (p.port == port) {
			rv.push_back (std::make_pair (p.instance, p.pin));
		}
	}
	return rv;
}

/** every sidechain port in the session, by its full name; @a procs gets each route's processors */
std::map<std::string, Sink>
sidechain_sinks (RouteList const& routes, std::map<Route const*, Processors>& procs)
{
	std::map<std::string, Sink> sinks;
	AudioEngine*                engine (AudioEngine::instance ());

	for (auto const& r : routes) {
		Processors const& pl (procs[r.get ()] = processors_of (r));
		for (auto const& p : pl) {
			std::shared_ptr<PluginInsert> pi (std::dynamic_pointer_cast<PluginInsert> (p));
			std::shared_ptr<IO>           sc (pi ? pi->sidechain_input () : std::shared_ptr<IO> ());
			if (!sc) {
				continue;
			}
			std::shared_ptr<PortSet> ps (sc->ports ());
			for (DataType::iterator t = DataType::begin (); t != DataType::end (); ++t) {
				for (uint32_t j = 0; j < ps->num_ports (*t); ++j) {
					Sink s;
					s.route  = r;
					s.device = pi;
					s.type   = *t;
					s.index  = j;
					s.port   = ps->port (*t, j);
					sinks[engine->make_port_name_non_relative (s.port->name ())] = s;
				}
			}
		}
	}
	return sinks;
}

McrCableJack
make_jack (std::shared_ptr<PluginInsert> const& pi, uint32_t n, bool input, DataType dt, uint32_t i, McrCables::TargetRecord const& rec)
{
	ChanCount in, out;
	pi->configured_io (in, out);
	Plugin::IOPortDescription const d (pi->plugin (n)->describe_io_port (dt, input, i));

	McrCableJack j;
	j.type      = dt;
	j.input     = input;
	j.instance  = n;
	j.pin       = i;
	j.name      = d.name;
	j.group     = d.group_name;
	j.sidechain = d.is_sidechain;

	bool valid;
	uint32_t const idx = (input ? pi->input_map (n) : pi->output_map (n)).get (dt, i, &valid);
	j.wired = valid;

	if (input) {
		/* a pin reads one buffer: a cable into a pin the track feeds replaces the track's
		 * signal there. For MIDI that is allowed (until MIDI merges); for audio it isn't. */
		j.main = valid && idx < in.get (dt);
		bool const fed_before = std::find_if (rec.pins.begin (), rec.pins.end (), [&] (McrCables::PinRecord const& p) {
			                        return p.type == dt && p.instance == n && p.pin == i && p.main >= 0;
		                        }) != rec.pins.end ();
		j.replaces = dt == DataType::MIDI && (j.main || fed_before);
		j.aux      = dt == DataType::MIDI || (!j.main && (d.is_sidechain || (dt == DataType::AUDIO && i >= 2 && is_pgd4s (pi))));
	}
	return j;
}

/** catches Session::FeedbackDetected from a graph sort in this thread (not one in the engine's thread) */
class FeedbackWatch
{
public:
	FeedbackWatch ()
		: _thread (pthread_self ())
		, _fired (false)
	{
		Session::FeedbackDetected.connect_same_thread (_connection, std::bind (&FeedbackWatch::detected, this));
	}

	bool fired () const { return _fired.load (); }

private:
	void detected ()
	{
		if (pthread_equal (pthread_self (), _thread)) {
			_fired.store (true);
		}
	}

	pthread_t             _thread;
	std::atomic<bool>     _fired;
	PBD::ScopedConnection _connection;
};

} /* anonymous namespace */

/* ****************************************************************************
 * Detective taps
 */

/** switches a send's meter on for the level taps on it. It doesn't switch it off when
 * the last tap goes: Ardour's send window and mixer strip switch it on and off without
 * counting, so switching it off could stop the meter of an open send window.
 */
class McrLevelTap::Hold
{
public:
	Hold (std::shared_ptr<Send> s)
		: _send (s)
	{
		s->set_metering (true);
	}

	std::shared_ptr<Send> send () const { return _send.lock (); }

private:
	std::weak_ptr<Send> _send;
};

McrLevelTap::McrLevelTap (std::shared_ptr<Hold> h, DataType dt, uint32_t port)
	: _hold (h)
	, _type (dt)
	, _port (port)
{
}

McrLevelTap::~McrLevelTap ()
{
}

float
McrLevelTap::level () const
{
	std::shared_ptr<Send> s (_hold->send ());
	if (!s) {
		return -std::numeric_limits<float>::infinity ();
	}
	if (!s->metering ()) {
		/* a send window closed (send_ui.cc), or a mixer strip stopped showing this send */
		s->set_metering (true);
	}
	std::shared_ptr<PeakMeter> m (s->meter ());
	ChanCount const c (m->input_streams ());
	/* the meter's MIDI channels come first */
	if (_type == DataType::MIDI) {
		return _port < c.n_midi () ? m->meter_level (_port, MeterPeak) : 0.f;
	}
	if (_port >= c.n_audio ()) {
		return -std::numeric_limits<float>::infinity ();
	}
	return m->meter_level (c.n_midi () + _port, MeterPeak);
}

/** a trace parser that owns the ring its real-time side writes into. The port holds it
 * through a weak pointer it locks while tracing, so the ring outlives any trace in progress.
 */
class McrMidiTap::Parser : public MIDI::Parser
{
public:
	Parser (size_t ring_size)
		: _fifo (std::max<size_t> (ring_size, 16))
		, _dropped (0)
	{}

	/* real-time thread: no allocation, no locks of ours */
	void push (MIDI::byte const* msg, size_t len, samplecnt_t when)
	{
		McrMidiTap::Event e;
		e.time = when;
		e.size = len;
		memset (e.data, 0, sizeof (e.data));
		memcpy (e.data, msg, std::min<size_t> (len, sizeof (e.data)));
		if (_fifo.write (&e, 1) != 1) {
			_dropped.fetch_add (1);
		}
	}

	size_t read (McrMidiTap::Event* dst, size_t max) { return _fifo.read (dst, max); }
	uint32_t dropped () const { return _dropped.load (); }

private:
	PBD::RingBuffer<McrMidiTap::Event> _fifo;
	std::atomic<uint32_t>              _dropped;
};

/** one send port's trace, shared by the taps on it: the GUI side moves what the real-time
 * side wrote into a history that each tap reads at its own pace (GUI thread only)
 */
class McrMidiTap::Hub
{
public:
	Hub (std::shared_ptr<MidiPort> port, size_t ring_size)
		: _port (port)
		, _parser (new Parser (ring_size))
		, _attached (false)
		, _keep (std::max<size_t> (ring_size, 16))
		, _first (0)
	{
		/* no captures: PBD::Signal copies the slot in the emitting (real-time) thread,
		 * and a capture-less lambda copies without allocating */
		_parser->any.connect_same_thread (_connection, [] (MIDI::Parser& p, MIDI::byte* msg, size_t len, samplecnt_t when) {
			static_cast<McrMidiTap::Parser&> (p).push (msg, len, when);
		});
		poll ();
	}

	~Hub ()
	{
		std::shared_ptr<MidiPort> mp (_port.lock ());
		if (mp && mp->trace_parser ().get () == _parser.get ()) {
			/* only our own trace: never the MIDI Tracer's */
			mp->set_trace (std::weak_ptr<MIDI::Parser> ());
		}
		_connection.disconnect ();
	}

	bool poll ()
	{
		std::shared_ptr<MidiPort> mp (_port.lock ());
		if (!mp) {
			_attached = false;
			return false;
		}
		std::shared_ptr<MIDI::Parser> const current (mp->trace_parser ());
		if (current.get () == _parser.get ()) {
			_attached = true;
		} else if (current) {
			/* the MIDI Tracer traces this port: wait until it lets go */
			_attached = false;
		} else {
			mp->set_trace (_parser);
			_attached = true;
		}
		return _attached;
	}

	/** move what came from the real-time side into the history, which keeps the latest events */
	void drain ()
	{
		McrMidiTap::Event buf[64];
		size_t            n;
		while ((n = _parser->read (buf, 64)) > 0) {
			_history.insert (_history.end (), buf, buf + n);
		}
		while (_history.size () > _keep) {
			_history.pop_front ();
			++_first;
		}
	}

	std::shared_ptr<MidiPort> port () const { return _port.lock (); }
	bool attached () const { return _attached; }
	uint32_t dropped () const { return _parser->dropped (); }

	uint64_t first () const { return _first; }
	uint64_t end () const { return _first + _history.size (); }
	McrMidiTap::Event const& at (uint64_t n) const { return _history[n - _first]; }

private:
	std::weak_ptr<MidiPort>       _port;
	std::shared_ptr<Parser>       _parser;
	PBD::ScopedConnection         _connection;
	bool                          _attached;
	size_t                        _keep;
	std::deque<McrMidiTap::Event> _history;
	uint64_t                      _first; ///< the number of the history's first event
};

McrMidiTap::McrMidiTap (std::shared_ptr<Hub> hub)
	: _hub (hub)
	, _next (0)
	, _missed (0)
{
	/* a new tap reads what comes from now on */
	_hub->drain ();
	_next = _hub->end ();
}

McrMidiTap::~McrMidiTap ()
{
}

bool
McrMidiTap::poll ()
{
	return _hub->poll ();
}

bool
McrMidiTap::attached () const
{
	return _hub->attached ();
}

size_t
McrMidiTap::read (Event* dst, size_t max)
{
	_hub->drain ();
	if (_next < _hub->first ()) {
		_missed += _hub->first () - _next;
		_next = _hub->first ();
	}
	size_t n = 0;
	while (n < max && _next < _hub->end ()) {
		dst[n++] = _hub->at (_next++);
	}
	return n;
}

uint32_t
McrMidiTap::dropped () const
{
	return _hub->dropped () + _missed;
}

/* ****************************************************************************
 * Cables
 */

McrCables::McrCables (Session& s, PBD::EventLoop* event_loop)
	: _session (s)
	, _event_loop (event_loop)
	, _invalidation (PBD::EventLoop::__invalidator (*this, __FILE__, __LINE__))
	, _idle_queued (false)
	, _rewatch_pending (false)
{
	_session.RouteAdded.connect (_session_connections, _invalidation, std::bind (&McrCables::queue_update, this, true), _event_loop);
	/* a connection made or lost elsewhere (a routing grid, a deleted track) can make or take a cable */
	AudioEngine::instance ()->PortConnectedOrDisconnected.connect (_session_connections, _invalidation, std::bind (&McrCables::queue_update, this, true), _event_loop);
	/* clean-ups wait while recording and while the engine is stopped: do them once that ends */
	_session.RecordStateChanged.connect (_session_connections, _invalidation, std::bind (&McrCables::queue_update, this, true), _event_loop);
	AudioEngine::instance ()->Running.connect (_session_connections, _invalidation, std::bind (&McrCables::queue_update, this, true), _event_loop);
	watch ();
	apply_records ();
	queue_update (true);
}

McrCables::~McrCables ()
{
	_idle.disconnect ();
	_session_connections.drop_connections ();
	_watch_connections.drop_connections ();
}

std::vector<McrCableJack>
McrCables::jacks (std::shared_ptr<PluginInsert> pi)
{
	std::vector<McrCableJack> rv;
	if (!pi || !pi->plugin ()) {
		return rv;
	}
	TargetRecord const rec (read_record (pi));
	for (uint32_t n = 0; n < pi->get_count (); ++n) {
		for (int dir = 0; dir < 2; ++dir) {
			bool const      input = dir == 0;
			ChanCount const c (input ? pi->natural_input_streams () : pi->natural_output_streams ());
			for (DataType::iterator t = DataType::begin (); t != DataType::end (); ++t) {
				for (uint32_t i = 0; i < c.get (*t); ++i) {
					rv.push_back (make_jack (pi, n, input, *t, i, rec));
				}
			}
		}
	}
	return rv;
}

/** the id @a p was saved with: its own, or for a device that didn't load (an
 * UnknownProcessor, which gets a new id) the one in the state it keeps
 */
PBD::ID
McrCables::saved_id (std::shared_ptr<Processor> const& p) const
{
	if (!std::dynamic_pointer_cast<UnknownProcessor> (p)) {
		return p->id ();
	}
	auto const i = _unknown_ids.find (p.get ());
	if (i != _unknown_ids.end () && i->second.first.lock () == p) {
		return i->second.second;
	}
	PBD::ID  id (p->id ());
	XMLNode& node (p->get_state ()); /* a copy of the state it was loaded from */
	node.get_property (X_("id"), id);
	delete &node;
	_unknown_ids[p.get ()] = std::make_pair (std::weak_ptr<Processor> (p), id);
	return id;
}

std::shared_ptr<Route>
McrCables::route_of (std::shared_ptr<PluginInsert> pi) const
{
	if (!pi) {
		return std::shared_ptr<Route> ();
	}
	/* never pi->owner (): it isn't cleared when the route goes, and an McrPin can keep
	 * the device alive after that */
	std::shared_ptr<RouteList const> routes (_session.get_routes ());
	for (auto const& r : *routes) {
		if (r->processor_by_id (pi->id ()) == pi) {
			return r;
		}
	}
	return std::shared_ptr<Route> ();
}

std::shared_ptr<Send>
McrCables::send_for (std::shared_ptr<Route> r, std::shared_ptr<PluginInsert> pi) const
{
	for (auto const& p : processors_of (r)) {
		std::shared_ptr<Send> s (std::dynamic_pointer_cast<Send> (p));
		PBD::ID               id;
		if (mcr_source (s, id) && id == pi->id ()) {
			return s;
		}
	}
	return std::shared_ptr<Send> ();
}

/** the sidechain port that the target pin @a to reads, if any: its cable record's, else the map's */
std::shared_ptr<Port>
McrCables::target_port (std::shared_ptr<PluginInsert> pi, McrPin const& to) const
{
	std::shared_ptr<IO> sc (pi->sidechain_input ());
	if (!sc) {
		return std::shared_ptr<Port> ();
	}
	for (auto const& p : read_record (pi).pins) {
		if (p.type == to.type && p.instance == to.instance && p.pin == to.pin) {
			std::shared_ptr<Port> port (port_by_short_name (sc, p.type, p.port));
			if (port) {
				return port;
			}
		}
	}
	ChanCount in, out;
	pi->configured_io (in, out);
	bool           valid;
	uint32_t const idx = pi->input_map (to.instance).get (to.type, to.pin, &valid);
	if (valid && idx >= in.get (to.type) && idx - in.get (to.type) < sc->n_ports ().get (to.type)) {
		return sc->ports ()->port (to.type, idx - in.get (to.type));
	}
	return std::shared_ptr<Port> ();
}

bool
McrCables::feeds (std::shared_ptr<Route> from, std::shared_ptr<Route> to) const
{
	if (!from || !to) {
		return false;
	}
	/* walk the connections as they are, rather than the graph of the last sort,
	 * which can lag a cycle behind a port connection */
	std::shared_ptr<RouteList const> routes (_session.get_routes ());
	std::vector<std::shared_ptr<Route> > todo (1, from);
	std::set<Route const*> seen;
	while (!todo.empty ()) {
		std::shared_ptr<Route> r (todo.back ());
		todo.pop_back ();
		for (auto const& o : *routes) {
			if (seen.find (o.get ()) != seen.end () || !r->direct_feeds_according_to_reality (o)) {
				continue;
			}
			if (o == to) {
				return true;
			}
			seen.insert (o.get ());
			todo.push_back (o);
		}
	}
	return false;
}

std::string
McrCables::can_connect (McrPin const& from, McrPin const& to) const
{
	if (!from.device || !to.device || !from.device->plugin () || !to.device->plugin ()) {
		return _("That device is gone.");
	}

	std::shared_ptr<Route> const r1 (route_of (from.device));
	std::shared_ptr<Route> const r2 (route_of (to.device));

	if (!r1 || !r2) {
		return _("Cables connect devices on tracks and busses.");
	}
	if (r1 == r2) {
		return _("Cables go between tracks for now.");
	}
	if (_session.actively_recording ()) {
		return _("Not while recording: make cables once recording stops.");
	}
	if (!AudioEngine::instance ()->running ()) {
		return _("Start the audio engine to make cables.");
	}
	if (from.type != to.type) {
		return _("Audio goes to audio inputs, and MIDI to MIDI inputs.");
	}
	if (from.type != DataType::AUDIO && from.type != DataType::MIDI) {
		return _("Spite can't make this kind of cable yet.");
	}
	if (from.instance >= from.device->get_count () || from.pin >= from.device->natural_output_streams ().get (from.type)) {
		return _("That output doesn't exist.");
	}
	if (to.instance >= to.device->get_count () || to.pin >= to.device->natural_input_streams ().get (to.type)) {
		return _("That input doesn't exist.");
	}

	bool           valid;
	uint32_t const k = from.device->output_map (from.instance).get (from.type, from.pin, &valid);
	if (!valid) {
		return _("That output isn't wired to its track, so it has nothing to send.");
	}

	McrCableJack const j (make_jack (to.device, to.instance, true, to.type, to.pin, read_record (to.device)));
	if (j.main && !j.replaces) {
		return _("That's a main input: a cable would replace its track's signal.");
	}
	if (!j.aux) {
		return _("Cables land on sidechain and aux inputs.");
	}

	std::shared_ptr<Port> const dport (target_port (to.device, to));

	if (to.type == DataType::MIDI && !dport) {
		/* a sidechain holds a single MIDI port, which reaches a single MIDI input */
		std::shared_ptr<IO> sc (to.device->sidechain_input ());
		if (sc && sc->n_ports ().n_midi () > 0) {
			std::shared_ptr<Port>    mp (sc->ports ()->port (DataType::MIDI, 0));
			std::vector<ScPin> const used (sidechain_pins (to.device));
			if (mp->connected () || std::find_if (used.begin (), used.end (), [&mp] (ScPin const& s) { return s.port == mp; }) != used.end ()) {
				return _("This device takes MIDI cables on one input only.");
			}
		}
	}

	std::shared_ptr<Send> const send (send_for (r1, from.device));
	if (send && dport && k < send->output ()->n_ports ().get (from.type)) {
		std::shared_ptr<Port> const sport (send->output ()->ports ()->port (from.type, k));
		if (sport->connected_to (dport->name ())) {
			return _("Those jacks are already connected.");
		}
	}

	if (feeds (r2, r1)) {
		return string_compose (_("%1 already feeds %2: this cable would make a loop."), r2->name (), r1->name ());
	}

	return std::string ();
}

std::string
McrCables::connect (McrPin const& from, McrPin const& to)
{
	std::string const why (can_connect (from, to));
	if (!why.empty ()) {
		return why;
	}

	std::shared_ptr<PluginInsert> const a (from.device);
	std::shared_ptr<PluginInsert> const b (to.device);
	std::shared_ptr<Route> const        t1 (route_of (a));
	std::shared_ptr<Route> const        t2 (route_of (b));
	DataType const                      dt (from.type);

	bool           valid;
	uint32_t const k = a->output_map (from.instance).get (dt, from.pin, &valid);

	/* 1. the source device's send: one per device, right after it */
	std::shared_ptr<Send> send (send_for (t1, a));
	bool const            new_send = !send;

	if (new_send) {
		send.reset (new Send (_session, t1->pannable (), t1->mute_master ()));
		/* Ardour frees a send's number when an aux send goes too (Send::~Send), so a live
		 * send can have this new one's name already, and their ports would clash */
		if (!_session.io_name_is_legal (send->name ())) {
			send->set_name (send->name ()); /* Send::set_name makes it unique ("send 1-1") */
		}
		if (!_session.io_name_is_legal (send->name ())) {
			return _("Spite couldn't make the ports for this cable.");
		}
		try {
			PBD::Mutex::Lock lm (AudioEngine::instance ()->process_lock ());
			send->output ()->ensure_io (a->output_streams (), false);
		} catch (AudioEngine::PortRegistrationFailure& e) {
			error << string_compose (_("MCR: cannot set up a cable's send: %1"), e.what ()) << endmsg;
			return _("Spite couldn't make the ports for this cable.");
		}
		send->panner_shell ()->set_bypassed (true); /* a 1:1 copy of the device's outputs */
		send->set_remove_on_disconnect (true);
		XMLNode* node = new XMLNode (send_tag);
		node->set_property (X_("source"), a->id ());
		send->add_extra_xml (*node);
		send->output ()->set_pretty_name (string_compose (_("MCR %1/%2"), t1->name (), a->name ()));
	} else {
		/* a reorder or an insert can have moved it away from its device */
		place_sends (t1);
	}

	while (send->output ()->n_ports ().get (dt) <= k) {
		/* the device has more outputs than when its send was made */
		try {
			if (send->output ()->add_port ("", dt)) {
				return _("Spite couldn't make the ports for this cable.");
			}
		} catch (AudioEngine::PortRegistrationFailure& e) {
			error << string_compose (_("MCR: cannot add a port to a cable's send: %1"), e.what ()) << endmsg;
			return _("Spite couldn't make the ports for this cable.");
		}
	}
	std::shared_ptr<Port> const sport (send->output ()->ports ()->port (dt, k));

	/* 2. the target's sidechain */
	TargetRecord rec (read_record (b)); /* empty for a stale record */
	bool const   new_sidechain = !b->has_sidechain ();

	if (new_sidechain) {
		bool added = false;
		try {
			added = t2->add_sidechain (b); /* locks by itself */
		} catch (AudioEngine::PortRegistrationFailure& e) {
			error << string_compose (_("MCR: cannot add a sidechain: %1"), e.what ()) << endmsg;
		}
		if (!added || !b->sidechain_input ()) {
			return _("The target device can't take a sidechain right now.");
		}
		rec         = TargetRecord ();
		rec.created = true;
	}
	std::shared_ptr<IO> const sc (b->sidechain_input ());

	/* 3. the sidechain port for the target pin: the one it reads (another cable's, or
	 * e.g. ACE Compressor's sidechain pin, wired when the sidechain was added), else a
	 * free one (a spare first), else a new one */
	std::shared_ptr<Port> dport (target_port (b, to));
	bool const            new_pin = std::find_if (rec.pins.begin (), rec.pins.end (), [&] (PinRecord const& p) {
                           return p.type == dt && p.instance == to.instance && p.pin == to.pin && port_by_short_name (sc, dt, p.port);
                         }) == rec.pins.end ();
	PinRecord pr;
	pr.type     = dt;
	pr.instance = to.instance;
	pr.pin      = to.pin;

	{
		/* a MIDI input its track feeds: the cable replaces the track's MIDI there */
		ChanCount in, out;
		b->configured_io (in, out);
		bool           fed;
		uint32_t const idx = b->input_map (to.instance).get (dt, to.pin, &fed);
		if (fed && idx < in.get (dt)) {
			pr.main = idx;
		}
	}

	if (dport) {
		pr.prev = short_name (dport);
	} else if ((dport = free_port (b, rec, dt))) {
		std::string const name (short_name (dport));
		if (is_spare (rec, dt, name)) {
			pr.added = true;
			rec.spares.erase (std::find (rec.spares.begin (), rec.spares.end (), std::make_pair (dt, name)));
		}
	}

	if (!dport) {
		std::vector<ScPin> const keep (sidechain_pins (b));
		std::shared_ptr<PortSet> before (new PortSet (*sc->ports ()));
		bool                     failed = false;

		try {
			failed = sc->add_port ("", dt) != 0; /* locks by itself */
		} catch (AudioEngine::PortRegistrationFailure& e) {
			error << string_compose (_("MCR: cannot add a port to a sidechain: %1"), e.what ()) << endmsg;
			failed = true;
		}
		if (failed) {
			if (new_sidechain) {
				t2->remove_sidechain (b);
			}
			return _("Spite couldn't add a port to the target's sidechain.");
		}
		/* a new port takes the lowest free number */
		restore_sidechain_pins (b, keep);
		std::shared_ptr<PortSet> ps (sc->ports ());
		for (uint32_t j = 0; j < ps->num_ports (dt); ++j) {
			if (!before->contains (ps->port (dt, j))) {
				dport = ps->port (dt, j);
			}
		}
		pr.added = true;
	}

	if (!dport) {
		if (new_sidechain) {
			t2->remove_sidechain (b);
		}
		return _("Spite couldn't add a port to the target's sidechain.");
	}

	if (new_pin) {
		pr.port = short_name (dport);
		rec.pins.erase (std::remove_if (rec.pins.begin (), rec.pins.end (), [&] (PinRecord const& p) {
			                return p.type == dt && p.instance == to.instance && p.pin == to.pin;
		                }),
		                rec.pins.end ());
		rec.pins.push_back (pr);
	}
	write_record (b, rec);

	/* 4. the port connection, then the pin */
	if (send->output ()->connect (sport, dport->name ())) {
		release_target_port (t2, b, dport);
		return _("Spite couldn't connect the cable's ports.");
	}
	apply_record (b);

	/* 5. a new send goes in after its device, now that its ports are connected, so the
	 * graph sort that follows sees the cable; an existing send has the routes sorted
	 * now rather than after the next cycle. A loop that slipped past can_connect () is
	 * undone here. */
	FeedbackWatch fw;
	int           err = 0;

	if (new_send) {
		err = t1->add_processor (send, processor_after (processors_of (t1), a)); /* locks by itself */
	} else {
		_session.resort_routes ();
	}

	if (err || fw.fired ()) {
		bool const last = n_connections (send->output ()) <= 1;
		if (last) {
			send->set_remove_on_disconnect (false);
		}
		send->output ()->disconnect (sport, dport->name ());
		release_target_port (t2, b, dport);
		if (last && new_send && !err) {
			t1->remove_processor (send);
		} else {
			_session.resort_routes ();
		}
		if (fw.fired ()) {
			return string_compose (_("%1 already feeds %2: this cable would make a loop."), t2->name (), t1->name ());
		}
		return _("Spite couldn't add the cable's send to its track.");
	}

	watch ();
	Changed (); /* EMIT SIGNAL */
	return std::string ();
}

/** after a cable into @a dport went: when nothing feeds that port any more, the pins it
 * reached go back to what they read before their first cable, a port the MCR added
 * goes (or waits as a spare, when a higher numbered one is in use), and a sidechain the
 * MCR added goes with its last cable
 */
void
McrCables::release_target_port (std::shared_ptr<Route> t2, std::shared_ptr<PluginInsert> b, std::shared_ptr<Port> dport)
{
	std::shared_ptr<IO> sc (b->sidechain_input ());
	if (!sc || dport->connected ()) {
		/* other cables still feed it (several cables into one pin sum on one port) */
		return;
	}

	TargetRecord           rec (read_record (b)); /* empty for a stale record */
	std::string const      name (short_name (dport));
	DataType const         dt (dport->type ());
	std::vector<ScPin>     pins (sidechain_pins (b));
	std::vector<PinRecord> released;
	bool                   remove_port = false;

	for (std::vector<PinRecord>::iterator i = rec.pins.begin (); i != rec.pins.end ();) {
		if (i->type != dt || i->port != name) {
			++i;
			continue;
		}
		remove_port |= i->added;

		std::shared_ptr<Port> const prev (i->prev == name ? dport : port_by_short_name (sc, dt, i->prev));
		bool                        found = false;
		for (auto& p : pins) {
			if (p.instance == i->instance && p.type == dt && p.pin == i->pin) {
				p.port = prev;
				found  = true;
			}
		}
		if (!found && prev) {
			ScPin p;
			p.instance = i->instance;
			p.type     = dt;
			p.pin      = i->pin;
			p.port     = prev;
			pins.push_back (p);
		}
		released.push_back (*i);
		i = rec.pins.erase (i);
	}

	if (released.empty ()) {
		/* a sidechain made elsewhere: leave the device as it is */
		return;
	}

	pins.erase (std::remove_if (pins.begin (), pins.end (), [] (ScPin const& p) { return !p.port; }), pins.end ());
	if (std::find_if (pins.begin (), pins.end (), [&dport] (ScPin const& p) { return p.port == dport; }) != pins.end ()) {
		remove_port = false;
	}

	uint32_t const n_dt = sc->n_ports ().get (dt);
	bool const     last = n_dt > 0 && sc->ports ()->port (dt, n_dt - 1) == dport;
	if (remove_port && !last) {
		/* removing it would leave a gap in the numbers, which a reload closes, losing
		 * the connections of the ports above it: it waits, unused, for the next cable */
		rec.spares.push_back (std::make_pair (dt, name));
		remove_port = false;
	}

	write_record (b, rec);
	if (remove_port) {
		sc->remove_port (dport); /* locks by itself; the highest number: the others keep theirs */
	}
	restore_sidechain_pins (b, pins);
	for (auto const& r : released) {
		restore_main_pin (b, r);
	}
	if (remove_port) {
		trim_spares (b, rec, dt);
	}

	if (rec.pins.empty () && rec.created && !sc->connected ()) {
		if (t2->remove_sidechain (b)) { /* locks by itself */
			write_record (b, TargetRecord ());
		}
	}
}

std::string
McrCables::disconnect (McrCable const& c)
{
	std::shared_ptr<Send> const         send (c.send.lock ());
	std::shared_ptr<PluginInsert> const b (c.target.lock ());
	std::shared_ptr<Route> const        t1 (c.source_route.lock ());
	std::shared_ptr<Route> const        t2 (c.target_route.lock ());

	if (!send || !b || !t1 || !t2 || !b->sidechain_input () || !send->output ()) {
		return _("That cable is gone.");
	}
	if (_session.actively_recording ()) {
		return _("Not while recording: remove cables once recording stops.");
	}
	if (!AudioEngine::instance ()->running ()) {
		return _("Start the audio engine to remove cables.");
	}

	/* the ports themselves, never their numbers: removing a cable can renumber the
	 * target's sidechain ports, and an older McrCable would name another cable's */
	std::shared_ptr<IO> const   sc (b->sidechain_input ());
	std::shared_ptr<Port> const sport (c.out_port.lock ());
	std::shared_ptr<Port> const dport (c.in_port.lock ());

	if (!sport || !dport || !send->output ()->ports ()->contains (sport) || !sc->ports ()->contains (dport) || !sport->connected_to (dport->name ())) {
		return _("That cable is gone.");
	}

	/* the send's last connection: remove the send here and now, rather than have it
	 * remove itself later in the session's signal thread */
	bool const last        = n_connections (send->output ()) <= 1;
	bool const remove_send = last && send->remove_on_disconnect ();

	if (remove_send) {
		send->set_remove_on_disconnect (false);
	}

	send->output ()->disconnect (sport, dport->name ());
	release_target_port (t2, b, dport);

	if (remove_send) {
		t1->remove_processor (send); /* locks by itself */
	} else {
		_session.resort_routes ();
	}

	watch ();
	Changed (); /* EMIT SIGNAL */
	return std::string ();
}

std::vector<McrCable>
McrCables::cables () const
{
	std::vector<McrCable>              rv;
	std::shared_ptr<RouteList const>   routes (_session.get_routes ());
	AudioEngine*                       engine (AudioEngine::instance ());
	std::map<Route const*, Processors> procs;
	std::map<std::string, Sink> const  sinks (sidechain_sinks (*routes, procs));

	if (sinks.empty ()) {
		return rv;
	}

	for (auto const& r : *routes) {
		Processors const&             pl (procs[r.get ()]);
		std::shared_ptr<PluginInsert> before; /* the last device before each send */

		for (auto const& p : pl) {
			if (std::shared_ptr<PluginInsert> pi = std::dynamic_pointer_cast<PluginInsert> (p)) {
				before = pi;
				continue;
			}

			std::shared_ptr<Send> s (std::dynamic_pointer_cast<Send> (p));
			if (!s || std::dynamic_pointer_cast<InternalSend> (s) || s->role () != Delivery::Send || !s->output ()) {
				continue;
			}

			PBD::ID                       id;
			bool const                    mcr = mcr_source (s, id);
			std::shared_ptr<PluginInsert> src;

			if (!mcr) {
				src = before;
			} else {
				for (auto const& q : pl) {
					if (q->id () == id) {
						src = std::dynamic_pointer_cast<PluginInsert> (q);
					}
				}
			}

			std::shared_ptr<PortSet> ps (s->output ()->ports ());
			for (DataType::iterator t = DataType::begin (); t != DataType::end (); ++t) {
				for (uint32_t k = 0; k < ps->num_ports (*t); ++k) {
					std::vector<std::string> conns;
					ps->port (*t, k)->get_connections (conns);

					for (auto const& cn : conns) {
						std::map<std::string, Sink>::const_iterator const d (sinks.find (engine->make_port_name_non_relative (cn)));
						if (d == sinks.end () || d->second.type != *t) {
							continue;
						}

						McrCable c;
						c.type           = *t;
						c.source_route   = r;
						c.source         = src;
						c.target_route   = d->second.route;
						c.target         = d->second.device;
						c.send           = s;
						c.send_port      = k;
						c.sidechain_port = d->second.index;
						c.out_port       = ps->port (*t, k);
						c.in_port        = d->second.port;
						c.mcr            = mcr;

						if (src) {
							/* the source pin writing the send's channel */
							for (uint32_t n = 0; n < src->get_count () && c.source_pin == McrCable::no_pin; ++n) {
								bool           valid;
								uint32_t const pin = src->output_map (n).get_src (*t, k, &valid);
								if (valid) {
									c.source_instance = n;
									c.source_pin      = pin;
								}
							}
						}

						std::vector<std::pair<uint32_t, uint32_t> > const tp (pins_reading (d->second.device, d->second.port));
						if (tp.empty ()) {
							rv.push_back (c);
						}
						for (auto const& i : tp) {
							c.target_instance = i.first;
							c.target_pin      = i.second;
							rv.push_back (c);
						}
					}
				}
			}
		}
	}
	return rv;
}

/** re-wire a target's cable pins; @return true when its map changed */
bool
McrCables::apply_record (std::shared_ptr<PluginInsert> pi)
{
	std::shared_ptr<IO> sc (pi->sidechain_input ());
	if (!sc || !pi->configured ()) {
		return false;
	}
	TargetRecord const rec (read_record (pi)); /* never a stale one */
	if (rec.pins.empty ()) {
		return false;
	}

	ChanCount in, out;
	pi->configured_io (in, out);

	bool rv = false;
	for (uint32_t n = 0; n < pi->get_count (); ++n) {
		ChanMapping m (pi->input_map (n));
		bool        changed = false;
		for (auto const& p : rec.pins) {
			if (p.instance != n || p.pin >= pi->natural_input_streams ().get (p.type)) {
				continue;
			}
			int const j = port_index (sc, port_by_short_name (sc, p.type, p.port));
			if (j < 0) {
				continue;
			}
			uint32_t const idx = in.get (p.type) + j;
			bool           valid;
			if (m.get (p.type, p.pin, &valid) == idx && valid) {
				continue;
			}
			m.unset (p.type, p.pin); /* ChanMapping::set () never replaces a pin's buffer */
			m.set (p.type, p.pin, idx);
			changed = true;
		}
		if (changed) {
			pi->set_input_map (n, m);
			rv = true;
		}
	}
	return rv;
}

/** re-wire every target's cable pins; @return true when a map changed */
bool
McrCables::apply_records ()
{
	std::vector<std::shared_ptr<PluginInsert> > targets;
	std::shared_ptr<RouteList const>             routes (_session.get_routes ());
	for (auto const& r : *routes) {
		for (auto const& p : processors_of (r)) {
			std::shared_ptr<PluginInsert> pi (std::dynamic_pointer_cast<PluginInsert> (p));
			if (pi && pi->extra_xml (target_tag)) {
				targets.push_back (pi);
			}
		}
	}
	bool any = false;
	for (auto const& pi : targets) {
		any |= apply_record (pi);
	}
	return any;
}

void
McrCables::reapply_maps ()
{
	if (apply_records ()) {
		Changed (); /* EMIT SIGNAL */
	}
}

std::shared_ptr<McrLevelTap>
McrCables::level_tap (McrCable const& c)
{
	std::shared_ptr<Send> s (c.send.lock ());
	if (!s) {
		return std::shared_ptr<McrLevelTap> ();
	}
	for (auto i = _meter_holds.begin (); i != _meter_holds.end ();) {
		if (i->second.expired ()) {
			i = _meter_holds.erase (i);
		} else {
			++i;
		}
	}
	std::shared_ptr<McrLevelTap::Hold> h (_meter_holds[s->id ()].lock ());
	if (!h || h->send () != s) {
		h.reset (new McrLevelTap::Hold (s));
		_meter_holds[s->id ()] = h;
	}
	return std::shared_ptr<McrLevelTap> (new McrLevelTap (h, c.type, c.send_port));
}

std::shared_ptr<McrMidiTap>
McrCables::midi_tap (McrCable const& c, size_t ring_size)
{
	std::shared_ptr<Send> s (c.send.lock ());
	if (!s || c.type != DataType::MIDI || !s->output ()) {
		return std::shared_ptr<McrMidiTap> ();
	}
	std::shared_ptr<MidiPort> mp (std::dynamic_pointer_cast<MidiPort> (c.out_port.lock ()));
	if (!mp || !s->output ()->ports ()->contains (mp)) {
		mp = s->output ()->midi (c.send_port);
	}
	if (!mp) {
		return std::shared_ptr<McrMidiTap> ();
	}

	/* by the port itself: a port of a deleted send can have had the same name */
	std::shared_ptr<McrMidiTap::Hub> hub;
	for (auto i = _midi_hubs.begin (); i != _midi_hubs.end ();) {
		std::shared_ptr<McrMidiTap::Hub> h (i->lock ());
		if (!h || !h->port ()) {
			i = _midi_hubs.erase (i);
			continue;
		}
		if (h->port () == mp) {
			hub = h;
		}
		++i;
	}
	if (!hub) {
		hub.reset (new McrMidiTap::Hub (mp, ring_size));
		_midi_hubs.push_back (hub);
	}
	return std::shared_ptr<McrMidiTap> (new McrMidiTap (hub));
}

/** follow every route, device, sidechain and send, to redraw and to re-wire targets */
void
McrCables::watch ()
{
	_watch_connections.drop_connections ();

	for (auto i = _unknown_ids.begin (); i != _unknown_ids.end ();) {
		if (i->second.first.expired ()) {
			i = _unknown_ids.erase (i);
		} else {
			++i;
		}
	}

	std::shared_ptr<RouteList const> routes (_session.get_routes ());
	for (auto const& r : *routes) {
		r->processors_changed.connect (_watch_connections, _invalidation, std::bind (&McrCables::queue_update, this, true), _event_loop);
		r->DropReferences.connect (_watch_connections, _invalidation, std::bind (&McrCables::queue_update, this, true), _event_loop);

		for (auto const& p : processors_of (r)) {
			std::shared_ptr<PluginInsert> pi (std::dynamic_pointer_cast<PluginInsert> (p));
			std::shared_ptr<Send>         s (std::dynamic_pointer_cast<Send> (p));

			if (pi || std::dynamic_pointer_cast<UnknownProcessor> (p)) {
				_seen.insert (saved_id (p));
			}
			if (pi && pi->sidechain_input ()) {
				/* a port removed or added elsewhere (the pin dialog) can make a cable record stale */
				pi->sidechain_input ()->changed.connect (_watch_connections, _invalidation, std::bind (&McrCables::queue_update, this, true), _event_loop);
			}
			if (pi && pi->extra_xml (target_tag)) {
				/* a reconfiguration can reset the map (reset_map () without PluginMapChanged,
				 * or reset_sidechain_map () doing nothing for PgD4S): wire the cable pins again */
				pi->PluginMapChanged.connect (_watch_connections, _invalidation, std::bind (&McrCables::queue_update, this, false), _event_loop);
				pi->PluginIoReConfigure.connect (_watch_connections, _invalidation, std::bind (&McrCables::queue_update, this, false), _event_loop);
			}
			if (s && s->output ()) {
				s->output ()->changed.connect (_watch_connections, _invalidation, std::bind (&McrCables::queue_update, this, false), _event_loop);
			}
		}
	}
}

/** clean-ups change routes, sidechains and ports: Ardour refuses some of that while
 * recording (Route::remove_processor on an armed track, Route::add_remove_sidechain),
 * connections only tell while the engine runs, and in Safe Mode no device loaded (each
 * is an UnknownProcessor), so nothing would be seen right
 */
bool
McrCables::cleanup_allowed () const
{
	return !_session.actively_recording () && AudioEngine::instance ()->running () && !Session::get_disable_all_loaded_plugins ();
}

/** a send whose source device is gone takes its cables with it, as an object's cords go
 * with it in plugdata; a target pin whose port lost its last connection elsewhere (its
 * send's track was deleted, or the ports were disconnected in a routing grid) goes back
 * to what it read before its first cable. Only while cleanup_allowed ().
 */
void
McrCables::prune ()
{
	std::shared_ptr<RouteList const> routes (_session.get_routes ());

	for (auto const& r : *routes) {
		Processors const pl (processors_of (r));
		for (auto const& p : pl) {
			std::shared_ptr<Send> s (std::dynamic_pointer_cast<Send> (p));
			PBD::ID               id;
			if (!mcr_source (s, id)) {
				continue;
			}
			if (_seen.find (id) == _seen.end ()) {
				/* its device was never seen (it didn't load, or this session was saved
				 * without it): keep its cables for when it comes back */
				continue;
			}
			if (std::find_if (pl.begin (), pl.end (), [this, &id] (std::shared_ptr<Processor> const& q) { return saved_id (q) == id; }) != pl.end ()) {
				continue;
			}
			drop_send (r, s);
		}
	}

	/* target pins whose port lost its last connection elsewhere: each port once (releasing
	 * one never takes another one's record, and ports keep their objects when renumbered) */
	std::vector<std::pair<std::shared_ptr<PluginInsert>, std::shared_ptr<Port> > > lost;
	std::map<PluginInsert const*, std::shared_ptr<Route> >                         route;

	routes = _session.get_routes ();
	for (auto const& r : *routes) {
		for (auto const& p : processors_of (r)) {
			std::shared_ptr<PluginInsert> pi (std::dynamic_pointer_cast<PluginInsert> (p));
			std::shared_ptr<IO>           sc (pi ? pi->sidechain_input () : std::shared_ptr<IO> ());
			if (!sc || !pi->extra_xml (target_tag)) {
				continue;
			}
			std::set<Port const*> listed;
			for (auto const& pr : read_record (pi).pins) {
				std::shared_ptr<Port> port (port_by_short_name (sc, pr.type, pr.port));
				if (port && !port->connected () && listed.insert (port.get ()).second) {
					lost.push_back (std::make_pair (pi, port));
					route[pi.get ()] = r;
				}
			}
		}
	}
	for (auto const& l : lost) {
		release_target_port (route[l.first.get ()], l.first, l.second);
	}

	/* spares that became the highest numbered ports (a port above them was removed elsewhere) */
	routes = _session.get_routes ();
	for (auto const& r : *routes) {
		for (auto const& p : processors_of (r)) {
			std::shared_ptr<PluginInsert> pi (std::dynamic_pointer_cast<PluginInsert> (p));
			if (!pi || !pi->extra_xml (target_tag)) {
				continue;
			}
			TargetRecord rec (read_record (pi));
			if (!rec.spares.empty ()) {
				trim_spares (pi, rec, DataType::AUDIO);
				trim_spares (pi, rec, DataType::MIDI);
			}
		}
	}
}

/** remove a send whose source device went, and its cables (each target pin goes back) */
void
McrCables::drop_send (std::shared_ptr<Route> r, std::shared_ptr<Send> s)
{
	std::shared_ptr<IO> const out (s->output ());
	s->set_remove_on_disconnect (false);

	if (out) {
		std::shared_ptr<RouteList const>   routes (_session.get_routes ());
		std::map<Route const*, Processors> procs;
		std::map<std::string, Sink> const  sinks (sidechain_sinks (*routes, procs));
		AudioEngine*                       engine (AudioEngine::instance ());
		std::shared_ptr<PortSet>           ps (out->ports ());

		for (uint32_t i = 0; i < ps->num_ports (); ++i) {
			std::shared_ptr<Port>    sport (ps->port (i));
			std::vector<std::string> conns;
			sport->get_connections (conns);
			for (auto const& cn : conns) {
				out->disconnect (sport, cn);
				std::map<std::string, Sink>::const_iterator const d (sinks.find (engine->make_port_name_non_relative (cn)));
				if (d != sinks.end ()) {
					release_target_port (d->second.route, d->second.device, d->second.port);
				}
			}
		}
	}

	if (r->remove_processor (s)) { /* locks by itself */
		warning << string_compose (_("MCR: cannot remove the send of a deleted device from %1"), r->name ()) << endmsg;
	}
}

/** forget what a target's record says about things that changed elsewhere (records only,
 * and the pins they name, so this is fine while recording): a record about a sidechain
 * that was removed or replaced, cable pins whose port was removed (the pin dialog), and
 * spares that went or that something else uses now
 */
void
McrCables::check_records ()
{
	std::shared_ptr<RouteList const> routes (_session.get_routes ());
	for (auto const& r : *routes) {
		for (auto const& p : processors_of (r)) {
			std::shared_ptr<PluginInsert> pi (std::dynamic_pointer_cast<PluginInsert> (p));
			if (!pi || !pi->extra_xml (target_tag)) {
				continue;
			}
			TargetRecord rec (read_raw_record (pi));
			if (rec.empty ()) {
				continue;
			}
			if (record_stale (pi, rec)) {
				write_record (pi, TargetRecord ());
				continue;
			}

			std::shared_ptr<IO> const sc (pi->sidechain_input ());
			std::vector<ScPin> const  used (sidechain_pins (pi));
			bool                      changed = rec.sidechain.empty (); /* note the sidechain's id */

			for (std::vector<PinRecord>::iterator i = rec.pins.begin (); i != rec.pins.end ();) {
				if (port_by_short_name (sc, i->type, i->port)) {
					++i;
					continue;
				}
				/* its cable went with its port */
				restore_main_pin (pi, *i);
				i       = rec.pins.erase (i);
				changed = true;
			}
			for (auto i = rec.spares.begin (); i != rec.spares.end ();) {
				std::shared_ptr<Port> const port (port_by_short_name (sc, i->first, i->second));
				bool const                  named = std::find_if (rec.pins.begin (), rec.pins.end (), [&] (PinRecord const& q) { return q.type == i->first && q.port == i->second; }) != rec.pins.end ();
				if (port && !named && !port->connected () && std::find_if (used.begin (), used.end (), [&port] (ScPin const& s) { return s.port == port; }) == used.end ()) {
					++i;
					continue;
				}
				i       = rec.spares.erase (i);
				changed = true;
			}
			if (changed) {
				write_record (pi, rec);
			}
		}
	}
}

/** a device's MCR send sits right after it, so that its cables carry the device's
 * outputs: put back the sends that a reorder or an insert moved away from their devices.
 * @return true when the route was asked to reorder
 */
bool
McrCables::place_sends (std::shared_ptr<Route> r)
{
	Processors const    pl (processors_of (r));
	Route::ProcessorList shown;
	std::set<PBD::ID>    devices;

	for (auto const& p : pl) {
		if (!p->display_to_user ()) {
			continue;
		}
		shown.push_back (p);
		if (std::dynamic_pointer_cast<PluginInsert> (p)) {
			devices.insert (p->id ());
		}
	}

	/* each shown device's sends, in their order */
	std::map<PBD::ID, Processors> sends;
	for (auto const& p : shown) {
		PBD::ID id;
		if (mcr_source (std::dynamic_pointer_cast<Send> (p), id) && devices.find (id) != devices.end ()) {
			sends[id].push_back (p);
		}
	}
	if (sends.empty ()) {
		return false;
	}

	Route::ProcessorList want;
	for (auto const& p : shown) {
		PBD::ID id;
		if (mcr_source (std::dynamic_pointer_cast<Send> (p), id) && sends.find (id) != sends.end ()) {
			continue;
		}
		want.push_back (p);
		std::map<PBD::ID, Processors>::const_iterator const i (sends.find (p->id ()));
		if (i != sends.end ()) {
			want.insert (want.end (), i->second.begin (), i->second.end ());
		}
	}

	if (want == shown) {
		return false;
	}
	if (r->reorder_processors (want)) {
		warning << string_compose (_("MCR: cannot put a cable's send back after its device on %1"), r->name ()) << endmsg;
		return false;
	}
	return true;
}

/** session signals ask for work here. Some come with the process and processor locks
 * held (PluginIoReConfigure in a route's reconfiguration, IO::changed while a sidechain
 * is set up), and a slot connected through the GUI's event loop then runs right away
 * in the GUI thread (see PluginPinWidget::queue_idle_update): do the work from idle
 */
void
McrCables::queue_update (bool rewatch)
{
	_rewatch_pending |= rewatch;
	if (!_idle_queued) {
		_idle_queued = true;
		_idle        = Glib::signal_idle ().connect (sigc::mem_fun (*this, &McrCables::idle_update));
	}
}

bool
McrCables::idle_update ()
{
	/* what comes while this runs queues another idle */
	_idle_queued = false;

	if (_session.deletion_in_progress () || _session.loading ()) {
		return false;
	}
	if (_rewatch_pending) {
		check_records ();
		if (cleanup_allowed ()) {
			/* else it waits: RecordStateChanged and Running queue it again */
			_rewatch_pending = false;
			prune ();
			std::shared_ptr<RouteList const> routes (_session.get_routes ());
			for (auto const& r : *routes) {
				place_sends (r);
			}
		}
		watch ();
	}
	apply_records ();
	Changed (); /* EMIT SIGNAL */
	return false;
}
