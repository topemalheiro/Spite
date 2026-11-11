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

/* The engine side of the MCR's cables (Modular Channels Rack).
 *
 * A cable from device A (track T1) to device B (track T2) is made of things the
 * session already has and saves:
 *
 *  - one Send per source device, kept right after it on T1 (it is moved back
 *    there when the device or the send is moved), sized from the device's
 *    outputs, its panner bypassed (a 1:1 copy), removed when its last
 *    connection goes, tagged with the extra XML node "MCRCable" (source="A's id");
 *  - B's sidechain input, with one port per pin that cables land on (several
 *    cables into one pin share the port, and the port sums them);
 *  - the connection from the Send's port to that sidechain port;
 *  - B's pin map, reading that sidechain buffer on B's aux or sidechain pin
 *    (or on a MIDI input its track feeds: the cable then replaces the track's
 *    MIDI on that pin, until MIDI merges).
 *
 * B keeps a record of its cable pins (extra XML "MCRSidechain"), naming each
 * sidechain port by its own name and the sidechain by its id, so the pins come
 * back whenever B's map is reset (PluginMapChanged, PluginIoReConfigure) or its
 * ports are renumbered. A record whose sidechain was removed or replaced
 * elsewhere is dropped, never acted on. The MCR only removes the highest
 * numbered of the ports it added (a lower one waits, unused, as a spare), since
 * a session reload numbers a sidechain's ports from 1 again.
 *
 * Everything here runs in the GUI thread, without GTK, so it can be tested
 * headless. Only the Detective taps have a real-time side, which only writes
 * into preallocated rings. What the session's signals ask for (re-wiring a
 * target, dropping the cables of a deleted device or track, or of ports
 * disconnected elsewhere, putting a send back after its device, telling the
 * GUI) is done from the GUI's idle (the default Glib main context): some of
 * them come with the process and processor locks held. Dropping and moving
 * waits while the session records, while the engine is stopped, and for good
 * in Safe Mode (plugins not loaded).
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <sigc++/connection.h>
#include <sigc++/trackable.h>

#include "pbd/event_loop.h"
#include "pbd/id.h"
#include "pbd/ringbuffer.h"
#include "pbd/signals.h"

#include "ardour/data_type.h"
#include "ardour/types.h"

namespace ARDOUR {
	class IO;
	class MidiPort;
	class PluginInsert;
	class Port;
	class Processor;
	class Route;
	class Send;
	class Session;
}

/** One pin of a device as the cable engine sees it: inputs on the device's top
 * edge, outputs on its bottom edge (until a cable flips them). Pins are numbered
 * per type and per plugin instance, as McrPin numbers them.
 */
struct McrCableJack
{
	ARDOUR::DataType type;
	bool             input;     ///< an input pin, else an output
	uint32_t         instance;  ///< plugin instance (replicated plugins), usually 0
	uint32_t         pin;       ///< the plugin's own pin index, counted per type
	std::string      name;      ///< e.g. "Audio In 3"
	std::string      group;     ///< e.g. "Aux Input 1"
	bool             sidechain; ///< the plugin marks it as a sidechain (lv2:isSideChain)
	bool             aux;       ///< an input cables can land on: a sidechain pin, PgD4S's pins 3-32, any MIDI input
	bool             main;      ///< an input the track's own signal feeds (an audio one takes no cables)
	bool             replaces;  ///< a MIDI input its track feeds (or fed, before its cable): its cable replaces the track's MIDI there
	bool             wired;     ///< the pin map connects it (to the track's streams or a sidechain buffer)

	McrCableJack ()
		: type (ARDOUR::DataType::NIL)
		, input (false)
		, instance (0)
		, pin (0)
		, sidechain (false)
		, aux (false)
		, main (false)
		, replaces (false)
		, wired (false)
	{}
};

/** One end of a cable: a device's pin */
struct McrPin
{
	std::shared_ptr<ARDOUR::PluginInsert> device;
	ARDOUR::DataType                      type;
	uint32_t                              pin;      ///< counted per type
	uint32_t                              instance; ///< plugin instance, usually 0

	McrPin ()
		: type (ARDOUR::DataType::NIL)
		, pin (0)
		, instance (0)
	{}

	McrPin (std::shared_ptr<ARDOUR::PluginInsert> d, ARDOUR::DataType t, uint32_t p, uint32_t n = 0)
		: device (d)
		, type (t)
		, pin (p)
		, instance (n)
	{}
};

/** A cable as read back from the session, for drawing */
struct McrCable
{
	static const uint32_t no_pin = UINT32_MAX;

	ARDOUR::DataType                    type;
	std::weak_ptr<ARDOUR::Route>        source_route;
	std::weak_ptr<ARDOUR::PluginInsert> source;          ///< empty: for an MCR cable, its device didn't load (missing plugin); else no device before the send (the track itself sends)
	uint32_t                            source_instance;
	uint32_t                            source_pin;      ///< no_pin: a track channel that no pin of the source writes
	std::weak_ptr<ARDOUR::Route>        target_route;
	std::weak_ptr<ARDOUR::PluginInsert> target;
	uint32_t                            target_instance;
	uint32_t                            target_pin;      ///< no_pin: the sidechain port reaches no pin
	std::weak_ptr<ARDOUR::Send>         send;
	uint32_t                            send_port;       ///< the send's output port, counted per type
	uint32_t                            sidechain_port;  ///< the target's sidechain port, counted per type (a snapshot: ports renumber)
	std::weak_ptr<ARDOUR::Port>         out_port;        ///< the send's port itself: disconnect () goes by the ports, never by their numbers
	std::weak_ptr<ARDOUR::Port>         in_port;         ///< the target's sidechain port itself
	bool                                mcr;             ///< made by the MCR; else a sidechain send made elsewhere (e.g. the pin dialog)

	McrCable ()
		: type (ARDOUR::DataType::NIL)
		, source_instance (0)
		, source_pin (no_pin)
		, target_instance (0)
		, target_pin (no_pin)
		, send_port (0)
		, sidechain_port (0)
		, mcr (false)
	{}
};

/** Detective Mode: a cable's level, read from its send's meter.
 * A tap switches the send's meter on, and on again while it reads (Ardour's send
 * window switches it off when it closes); the meter stays on after the tap goes,
 * so that it never stops under an open send window.
 */
class McrLevelTap
{
public:
	~McrLevelTap ();

	/** audio: the peak in dBFS (with the meter's falloff); MIDI: the meter's
	 * note activity (0 when quiet); -inf once the send is gone. GUI thread.
	 */
	float level () const;

	class Hold;

private:
	friend class McrCables;
	McrLevelTap (std::shared_ptr<Hold>, ARDOUR::DataType, uint32_t port);

	std::shared_ptr<Hold> _hold;
	ARDOUR::DataType      _type;
	uint32_t              _port;
};

/** Detective Mode: the MIDI events going through a cable, traced on its send's
 * MIDI port. The real-time side only writes into a preallocated ring.
 *
 * A port holds a single trace parser, and the MIDI Tracer window takes it over
 * when it traces that port: the tap then waits, and poll () attaches it again
 * once the port's trace is free. The taps on one send port share its parser,
 * yet each tap reads every event (cords fanning out of one jack all see them).
 */
class McrMidiTap
{
public:
	struct Event {
		ARDOUR::samplepos_t time;    ///< engine sample time
		uint32_t            size;    ///< the message's size; data holds its first 3 bytes
		uint8_t             data[3];
	};

	~McrMidiTap ();

	/** GUI thread: move up to @a max events that came since this tap's last read
	 * (or since it was made) into @a dst.
	 * @return the number of events read
	 */
	size_t read (Event* dst, size_t max);

	/** GUI thread, on each tick: attach to the port when its trace is free.
	 * @return true while the taps trace the port
	 */
	bool poll ();

	bool attached () const;

	/** events this tap lost: the ring was full, or this tap read too seldom */
	uint32_t dropped () const;

	class Parser;
	class Hub;

private:
	friend class McrCables;
	McrMidiTap (std::shared_ptr<Hub>);

	std::shared_ptr<Hub> _hub;
	uint64_t             _next;   ///< the number of the next event this tap reads, counted by the hub
	uint32_t             _missed; ///< events gone from the hub before this tap read them
};

/** The MCR's cables in one session. Create it in the GUI thread after the
 * session is loaded, and destroy it before the session goes.
 */
class McrCables : public sigc::trackable
{
public:
	/** @param event_loop the GUI's event loop: signals from other threads are handled there */
	McrCables (ARDOUR::Session&, PBD::EventLoop* event_loop);
	~McrCables ();

	/** every pin of a device (all instances, inputs then outputs, audio then MIDI) */
	static std::vector<McrCableJack> jacks (std::shared_ptr<ARDOUR::PluginInsert>);

	/** @return an empty string when a cable can go from output @a from to input @a to;
	 * otherwise why not, short and for the user
	 */
	std::string can_connect (McrPin const& from, McrPin const& to) const;

	/** make a cable from output @a from to input @a to.
	 * @return an empty string on success; otherwise why not (nothing is changed then)
	 */
	std::string connect (McrPin const& from, McrPin const& to);

	/** remove a cable, restoring the target's pin map; the send goes with its last
	 * connection, and a sidechain the MCR added goes with its last cable.
	 * @return an empty string on success; otherwise why not (nothing is changed then)
	 */
	std::string disconnect (McrCable const&);

	/** every cable in the session: the MCR's sends, and any other send whose ports feed a sidechain */
	std::vector<McrCable> cables () const;

	/** @return true when @a from feeds @a to, through anything (ports, sends, aux sends,
	 * sidechains, busses), going by the connections as they are now
	 */
	bool feeds (std::shared_ptr<ARDOUR::Route> from, std::shared_ptr<ARDOUR::Route> to) const;

	/** apply every target's cable pins to its pin map again (done by itself, from
	 * idle, when a target's map or I/O changes, and when this object is created).
	 * The cables own their pins: a cable pin re-wired elsewhere (the pin dialog)
	 * comes back to its cable; remove the cable instead.
	 */
	void reapply_maps ();

	/** a level reader for Detective Mode (it switches the cable's send meter on) */
	std::shared_ptr<McrLevelTap> level_tap (McrCable const&);

	/** a MIDI event reader for Detective Mode, one per caller; empty for an audio
	 * cable. The taps on one send port share one ring, of the first one's @a ring_size.
	 */
	std::shared_ptr<McrMidiTap> midi_tap (McrCable const&, size_t ring_size = 1024);

	/** emitted in the GUI thread when cables may have changed: at the end of connect ()
	 * and disconnect (), and from idle after the session changed (never with the
	 * session's locks held)
	 */
	PBD::Signal<void()> Changed;

	struct PinRecord;
	struct TargetRecord;

private:
	ARDOUR::Session&                     _session;
	PBD::EventLoop*                      _event_loop;
	PBD::EventLoop::InvalidationRecord*  _invalidation;
	PBD::ScopedConnectionList            _session_connections;
	PBD::ScopedConnectionList            _watch_connections;

	std::map<PBD::ID, std::weak_ptr<McrLevelTap::Hold> > _meter_holds;
	std::vector<std::weak_ptr<McrMidiTap::Hub> >          _midi_hubs;

	/** the ids of every device seen in a route: only a send whose device was seen and
	 * then went is dropped, never one whose device didn't load (or was never seen) */
	std::set<PBD::ID> _seen;

	/** devices that didn't load (UnknownProcessor, which gets a new id): the id each one was saved with */
	mutable std::map<ARDOUR::Processor const*, std::pair<std::weak_ptr<ARDOUR::Processor>, PBD::ID> > _unknown_ids;

	PBD::ID                        saved_id (std::shared_ptr<ARDOUR::Processor> const&) const;
	std::shared_ptr<ARDOUR::Route> route_of (std::shared_ptr<ARDOUR::PluginInsert>) const;
	std::shared_ptr<ARDOUR::Send>  send_for (std::shared_ptr<ARDOUR::Route>, std::shared_ptr<ARDOUR::PluginInsert>) const;
	std::shared_ptr<ARDOUR::Port>  target_port (std::shared_ptr<ARDOUR::PluginInsert>, McrPin const&) const;

	void release_target_port (std::shared_ptr<ARDOUR::Route>, std::shared_ptr<ARDOUR::PluginInsert>, std::shared_ptr<ARDOUR::Port>);
	bool apply_record (std::shared_ptr<ARDOUR::PluginInsert>);
	bool apply_records ();

	bool cleanup_allowed () const;
	void restore_connections ();
	void prune ();
	void drop_send (std::shared_ptr<ARDOUR::Route>, std::shared_ptr<ARDOUR::Send>);
	void check_records ();
	bool place_sends (std::shared_ptr<ARDOUR::Route>);

	void watch ();
	void queue_update (bool rewatch);
	bool idle_update ();

	sigc::connection _idle;
	bool             _idle_queued;
	bool             _rewatch_pending; ///< routes or devices changed: clean up and follow them again (the clean-up waits while the session records)
};
