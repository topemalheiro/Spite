/*
 * Copyright (C) 2021-2025 Timothy Schoen (plugdata)
 * Copyright (C) 2026 Timothy Schoen (plugdata)
 * Copyright (C) 2026 Spite
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
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

/* plugdata's Detective Mode for the MCR's cords: each message a cord carries travels
 * along it as a dot, with a bubble saying what it is.
 *
 * Ported from plugdata's Connection.cpp (the debug messages: receiveMessage, the timer,
 * formatDebugMessage, the bubbles, 31-129 and 1439-1768), Connection.h (its limits and
 * timings, 120-155 and 214-238) and DebugExecutionScheduler.cpp (one message at a
 * time). Where plugdata's messages come from Pd's outlets, the MCR's come from the
 * engine: MIDI, audio levels and parameter values, through one wait-free ring per cord.
 *
 * Detective::trace () and its trace_* helpers are real-time safe; everything else is
 * for the GUI thread.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <cairo.h>

#include "pd_cord.h"
#include "pd_theme.h"

namespace PdCord {

/* what the engine saw on a cord; plain data, copied on the process thread */
struct TraceEvent
{
	enum Type {
		Midi = 0,  /* one MIDI message */
		Audio = 1, /* the peak level of a cycle */
		Value = 2  /* a parameter value */
	};

	uint32_t cord;   /* from Detective::add_cord () */
	uint8_t type;
	uint8_t size;    /* Midi: bytes in midi; Audio: channels in level */
	bool truncated;  /* there was more than fits */
	int64_t when;    /* engine time, in samples: orders the events of different cords */
	uint64_t seq;    /* set by Detective::trace () */
	uint8_t midi[8];
	float level[4];  /* linear peak of each channel */
	float value;
};

/* a single-producer, single-consumer ring of events, its memory made up front; one per
 * cord, so the engine's process threads never share one. A cord's events must come from
 * one thread at a time: trace it where the route that feeds it runs (Ardour processes a
 * route on one thread in a cycle, and its cycles one after another). */
class TraceRing
{
public:
	explicit TraceRing (size_t capacity); /* rounded up to a power of two */

	bool push (TraceEvent const&); /* the producer: wait free; false when full */
	bool pop (TraceEvent&);        /* the consumer */
	size_t capacity () const { return _mask + 1; }

private:
	std::vector<TraceEvent> _buf;
	size_t _mask;
	alignas (64) std::atomic<size_t> _write;
	alignas (64) std::atomic<size_t> _read;
};

/* ---- the bubbles ---- */

/* the width a bubble's text wraps at, so that the widest bubble fits in the view
 * (getDebugWrapWidth, Connection.cpp:47-51) */
int bubble_wrap_width (Rect const& view);

/* a bubble of w x h next to the dot at p: to its right and above it, else to the left or
 * below, kept in the view (placeDebugBubble, Connection.cpp:67-92) */
Rect place_bubble (Rect const& view, Pt p, double w, double h, double offset);

/* the bubble round text of that size: its padding and outline added (getDebugBubbleBounds,
 * Connection.cpp:95-103) */
Rect bubble_bounds (Rect const& view, Pt p, double text_w, double text_h, double offset);

/* the message's dot: a white halo, white, and the data colour (Connection.cpp:1740-1755) */
void render_dot (cairo_t*, Pt, double cord_width, PdTheme const&);

/* the status bar's Detective toggle (DetectiveModeButton, Statusbar.cpp:60-79): 64 px wide in
 * plugdata, its 11 px label grey when it can't be used, in the active colour when on */
void render_detective_button (cairo_t*, Rect const&, bool on, bool hovered, bool sensitive, PdTheme const&);

/* ---- what a message says ---- */

/* plugdata's bubble text (Connection::formatDebugMessage, Connection.cpp:1592-1612):
 * "selector atom atom ...", cut at max characters and then marked " …" */
std::string format_message (std::string const& selector, std::vector<std::string> const& atoms, bool truncated = false, int max = 300);

/* a number as Pd and plugdata print one, with a point in every locale: "60", "0.5", "1e+06" */
std::string pd_float (double);

/* a MIDI message as the Pd message an instrument in plugdata takes (ELSE's sfz~ and the
 * like, sfz~.c:365-370), in Pd's units: "note 60 96", "note 60 0" for a note off (as Pd's
 * notein), "ctl <value> <controller>", "pgm <1..128>" (Pd counts programs from 1),
 * "bend <0..16383>" (as Pd's bendin), "touch <value>", "polytouch <value> <key>",
 * "sysex 240 ... 247", and "midi <bytes>" for the rest. with_channel adds the channel,
 * 1..16, last, in the order Pd's MIDI objects give it. */
std::string midi_text (uint8_t const* bytes, size_t size, bool truncated = false, bool with_channel = false);

/* audio peaks as plugdata's signal display labels them, 3 decimals a channel, with a point
 * in every locale: "0.523 0.498" */
std::string level_text (float const* peaks, size_t nchans);

/* a parameter value as a Pd float message: "float 0.5" */
std::string value_text (float);

/* what an event's bubble says; a truncated one (MIDI longer than 8 bytes, audio with more
 * than 4 channels) ends in " …" */
std::string event_text (TraceEvent const&);

/* ---- one cord ---- */

/* a cord's Detective state (plugdata keeps it in Connection) */
class CordDetective
{
public:
	/* plugdata's limits (Connection.h:229-233) */
	static constexpr size_t max_in_flight = 8;
	static constexpr size_t max_history = 64;
	static constexpr int max_text = 300;
	static constexpr double duration_ms = 400; /* a message takes this long along the cord, at speed 1 */
	static constexpr double step_ms = 200;     /* how far Step moves a paused message: half a cord */

	struct Bubble;

	struct Message
	{
		std::string text;
		uint64_t number;  /* counts this cord's messages, from 1: the history's "#n" */
		double progress;  /* 0..1 along the cord */
		bool audio;
		mutable std::shared_ptr<Bubble> bubble; /* the text laid out for the bubble */

		Message (std::string const& t, uint64_t n, bool a = false) : text (t), number (n), progress (0), audio (a) {}
	};

	CordDetective ();

	/* a message that sets off along the cord, at most 8 at a time (Connection::receiveMessage,
	 * Connection.cpp:1439-1479); it goes into the history too */
	void push (std::string const& text, bool audio = false);
	/* only into the history: in execution order, the scheduler plays it later */
	void record (std::string const& text);

	/* the scheduler's one message (Connection::setScheduledMessage etc, Connection.cpp:1614-1634) */
	void schedule (std::string const& text);
	void clear_scheduled ();
	bool scheduled () const { return _scheduled.get () != 0; }
	bool scheduled_done () const { return _scheduled_done; }

	/* moves the messages on by ms (already times the speed) and drops those that arrived
	 * (Connection::timerCallback, Connection.cpp:1522-1589); false when none moves any more */
	bool advance (double ms);

	bool active () const { return !_flying.empty () || _scheduled; }
	void clear (); /* drops the messages in flight, not the history */

	/* what render () draws over: the path and the bubbles (Connection::getDebugMessageBounds,
	 * Connection.cpp:1639-1672) */
	Rect bounds (Path const&, double cord_width, Rect const& view) const;

	/* the bubbles and dots (Connection::renderDebugMessages, Connection.cpp:1688-1756): draw
	 * them after all faces and cords. view is the visible part of the canvas, in canvas units. */
	void render (cairo_t*, Path const&, double cord_width, Rect const& view, PdTheme const&) const;

	std::deque<Message> const& in_flight () const { return _flying; }
	std::deque<Message> const& history () const { return _history; } /* oldest first */
	Message const* scheduled_message () const { return _scheduled.get (); }

	/* a row of the history panel, which lists the newest first: "#12  note 60 96"
	 * (ConnectionDebugPanel.cpp:81-128) */
	static std::string history_row (Message const&);
	static char const* const no_history; /* what the panel shows with no history */

private:
	std::deque<Message> _flying;
	std::deque<Message> _history;
	std::unique_ptr<Message> _scheduled;
	bool _scheduled_done;
	uint64_t _count;

	void add_history (Message const&);
	void prepare (Message const&, int wrap) const;
};

/* ---- all cords ---- */

/* the Detective's switches (plugdata keeps them in Connection's statics) and the engine's
 * way in; the scheduler for execution order (DebugExecutionScheduler) */
class Detective
{
public:
	/* make it on the GUI thread before the engine can trace */
	static Detective& instance ();

	explicit Detective (size_t ring_size = 256);
	~Detective ();

	/* ---- GUI thread ---- */

	/* a cord to follow: its handle, never 0, for the engine's events and cord () */
	uint32_t add_cord ();
	void remove_cord (uint32_t);
	CordDetective* cord (uint32_t);
	CordDetective const* cord (uint32_t) const;

	/* plugdata's defaults: on, speed 1 (SettingsFile.h:145-146) */
	void set_enabled (bool);
	bool enabled () const { return _enabled.load (std::memory_order_relaxed); }
	void set_speed (float); /* 0.1..5 (Connection.cpp:1497-1500) */
	float speed () const { return _speed; }
	void set_paused (bool);
	bool paused () const { return _paused; }
	void step (); /* moves paused messages on by step_ms (times the speed) */

	/* plugdata's Detective plays one message at a time, in the order they happened; off, each
	 * cord runs its own (up to 8 in flight), which keeps up with busy MIDI */
	void set_execution_order (bool);
	bool execution_order () const { return _execution_order; }

	/* from a GUI timer, about every 16 ms whenever enabled (), as plugdata's 60 Hz timer
	 * (Connection.cpp:1468): each tick sets off together, at the start of their cords, all
	 * the events that came in since the last one, so a slower timer bunches messages up and
	 * starts them late (and a busy cord's ring, ring_size events, can fill up between two
	 * ticks). A tick with nothing to do takes a few microseconds. While !enabled (), one tick
	 * after switching off clears what is drawn. now: a monotonic clock in microseconds
	 * (g_get_monotonic_time ()). */
	void tick (int64_t now_us);
	bool animating () const { return _animating; }
	std::vector<uint32_t> const& changed () const { return _changed; } /* the cords to redraw */

	/* ---- any thread: real-time safe, no locks, no allocation ---- */

	bool tracing () const { return enabled (); }
	bool trace (TraceEvent const&);
	bool trace_midi (uint32_t cord, int64_t when, uint8_t const* bytes, size_t size);
	bool trace_audio (uint32_t cord, int64_t when, float const* peaks, uint32_t nchans);
	bool trace_value (uint32_t cord, int64_t when, float value);

	static constexpr uint32_t max_cords = 4096;
	static constexpr size_t max_queue = 4096; /* execution order's queue, as plugdata's */

private:
	struct Slot {
		uint32_t handle; /* 0: free */
		std::unique_ptr<TraceRing> ring;
		std::unique_ptr<CordDetective> state;
		std::string audio;   /* the newest audio level, until the cord's last one arrived */
		bool audio_pending;
		bool audio_queued;   /* execution order: the level waits in the queue */
		bool dirty;

		Slot () : handle (0), audio_pending (false), audio_queued (false), dirty (false) {}
	};

	struct Queued {
		uint32_t cord;
		std::string text;
		bool audio; /* take the cord's newest level when it plays */
	};

	size_t _ring_size;
	std::vector<Slot> _slots;
	std::unique_ptr<std::atomic<TraceRing*>[]> _table;  /* what trace () reads: each index's ring, */
	std::unique_ptr<std::atomic<uint32_t>[]> _handles;  /* and the handle it now belongs to */
	std::vector<uint32_t> _free;
	uint32_t _next;
	std::vector<uint32_t> _generation;

	std::atomic<bool> _enabled;
	std::atomic<uint64_t> _seq;

	float _speed;
	bool _paused;
	bool _step;
	bool _execution_order;
	bool _animating;
	int64_t _last_us;

	std::vector<TraceEvent> _events;
	std::deque<Queued> _queue;
	uint32_t _current;
	std::vector<uint32_t> _changed;

	Slot* slot (uint32_t handle);
	Slot const* slot (uint32_t handle) const;
	void dirty (Slot&);
	void drain (Slot&, bool keep);

	Detective (Detective const&);
	Detective& operator= (Detective const&);
};

}
