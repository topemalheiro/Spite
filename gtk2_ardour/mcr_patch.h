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

/* The MCR's patching layer (Modular Channels Rack): jacks, cords, the slivers
 * between rows and Detective Mode, drawn in plugdata's look (pd_cord, pd_detective)
 * over whatever hosts the faces.
 *
 * The canvas fills a McrPatchModel (its rows and devices, in its own pixels) and
 * lays its rows out with the sliver heights this layer asks for; it calls
 * draw_under_faces () before it paints the live faces and draw_over_faces () after,
 * and passes its pointer and key events on. The layer reaches the session only
 * through a McrPatchEngine: McrPatchCables in Spite (McrCables), a fake one in tests.
 *
 * Everything is for the GUI thread.
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <cairo.h>

#include <sigc++/connection.h>
#include <sigc++/signal.h>
#include <sigc++/slot.h>
#include <sigc++/trackable.h>

#include "pbd/signals.h"

#include "pd_cord.h"
#include "pd_detective.h"
#include "pd_theme.h"

namespace ARDOUR {
	class PluginInsert;
}

namespace PBD {
	class ID;
}

class McrCables;

/* ---- what the canvas gives the layer (its pixels) ---- */

struct McrPatchRow
{
	uint64_t     id;     /* the track's (McrPatchCables::key): a sliver keeps its state by the id of the row above it */
	int          number; /* the track's number in lettering ("↓4"); 0: its place among the rows, from 1 */
	std::string  name;
	PdCord::Rect rect;   /* the whole row, across the canvas; the sliver under it runs to the next row's top */
	PdCord::Rect header; /* the track's name and colour */

	McrPatchRow () : id (0), number (0) {}
};

struct McrPatchDevice
{
	uint64_t                              id;     /* the processor's (McrPatchCables::key), never 0 */
	int                                   row;    /* in McrPatchModel::rows */
	std::shared_ptr<ARDOUR::PluginInsert> insert; /* for the engine; empty in tests */
	std::string                           name;
	std::string                           tag;    /* a short name for lettering ("VERB"); empty: made from the name */
	PdCord::Rect                          rect;   /* its title strip and face: jacks sit on its top and bottom edges */

	McrPatchDevice () : id (0), row (0) {}
};

struct McrPatchModel
{
	std::vector<McrPatchRow>    rows;    /* top to bottom */
	std::vector<McrPatchDevice> devices; /* in each row, left to right */
	double                      scale;   /* pixels per plugdata unit: the UI scale */

	McrPatchModel () : scale (1) {}
};

/* ---- what the layer asks of the engine ---- */

/* one pin of a device */
struct McrPatchJack
{
	enum Type {
		Audio,
		Midi
	};

	static const uint32_t no_pin = UINT32_MAX;

	Type        type;
	bool        input;     /* an input pin (top edge), else an output (bottom edge) */
	uint32_t    instance;  /* plugin instance, usually 0 */
	uint32_t    pin;       /* counted per type; no_pin: a cable end that reaches no pin */
	std::string name;      /* "Audio In 3" */
	bool        sidechain; /* the plugin marks it as a sidechain */
	bool        cables;    /* it can take a cable at all: any output, an aux or MIDI input (not a main audio input) */
	bool        wired;     /* the pin map uses it: the track feeds it, or it feeds the track */

	McrPatchJack ()
		: type (Audio)
		, input (false)
		, instance (0)
		, pin (0)
		, sidechain (false)
		, cables (false)
		, wired (false)
	{}
};

/* one end of a cable to make */
struct McrPatchEnd
{
	uint64_t                              device;
	std::shared_ptr<ARDOUR::PluginInsert> insert;
	McrPatchJack::Type                    type;
	uint32_t                              instance;
	uint32_t                              pin;

	McrPatchEnd () : device (0), type (McrPatchJack::Audio), instance (0), pin (0) {}
};

/* a cable, as the engine reports it */
struct McrPatchCable
{
	McrPatchJack::Type    type;
	uint64_t              source_row; /* the tracks' ids */
	uint64_t              target_row;
	uint64_t              source;     /* the devices' ids; source 0: the track itself sends (no device before the send) */
	uint64_t              target;
	uint32_t              source_instance;
	uint32_t              source_pin; /* McrPatchJack::no_pin: no pin of the source writes it */
	uint32_t              target_instance;
	uint32_t              target_pin; /* no_pin: it reaches no pin */
	bool                  mcr;        /* made by the MCR; else a sidechain send made elsewhere */
	std::shared_ptr<void> record;     /* the engine's own record of it */

	McrPatchCable ()
		: type (McrPatchJack::Audio)
		, source_row (0)
		, target_row (0)
		, source (0)
		, target (0)
		, source_instance (0)
		, source_pin (0)
		, target_instance (0)
		, target_pin (0)
		, mcr (false)
	{}
};

/* Detective Mode's view of one cable */
class McrPatchTap
{
public:
	virtual ~McrPatchTap () {}

	/* hand what the cable carried since the last poll to the Detective, as cord's events
	 * (trace_midi (), trace_audio ()); each Detective tick, about every 16 ms */
	virtual void poll (PdCord::Detective&, uint32_t cord) = 0;

	/* the cable's level now: linear peak, 1 at 0 dBFS; 0 for quiet, unknown or MIDI */
	virtual float level () = 0;
};

class McrPatchEngine
{
public:
	virtual ~McrPatchEngine () {}

	/* every pin of a device: inputs, then outputs */
	virtual std::vector<McrPatchJack> jacks (McrPatchDevice const&) = 0;
	/* every cable in the session, made by the MCR or not */
	virtual std::vector<McrPatchCable> cables () = 0;

	/* an empty string when output from can feed input to; otherwise why not, short, for the user */
	virtual std::string can_connect (McrPatchEnd const& from, McrPatchEnd const& to) = 0;
	virtual std::string connect (McrPatchEnd const& from, McrPatchEnd const& to) = 0;
	virtual std::string disconnect (McrPatchCable const&) = 0;

	/* the cable's own gain, as a factor; false when it has none that it alone uses */
	virtual bool gain (McrPatchCable const&, double& factor) = 0;
	virtual bool set_gain (McrPatchCable const&, double factor) = 0;

	/* Detective Mode's view of a cable, or none */
	virtual std::shared_ptr<McrPatchTap> tap (McrPatchCable const&) = 0;

	/* the cables may have changed: emitted in the GUI thread, also from inside
	 * connect () and disconnect () */
	sigc::signal<void> Changed;
};

/* ---- the layer ---- */

class McrPatch : public sigc::trackable
{
public:
	typedef PdCord::Rect Rect;
	typedef PdCord::Pt   Pt;

	McrPatch ();
	~McrPatch ();

	/* ---- input ---- */

	/* not owned; 0 while there is no session. Reads the cables and follows Changed. */
	void set_engine (McrPatchEngine*);
	/* the rows and devices after each layout (and whenever they change) */
	void set_model (McrPatchModel const&);
	/* the visible part of the canvas (pixels): bubbles stay in it */
	void set_visible (Rect const&);
	/* Play mode: cords behind the faces, the pointer only on slivers; slivers close
	 * unless pinned open. Patch mode: cords over the faces, the slivers with cords open. */
	void set_play_mode (bool);
	bool play_mode () const { return _play; }
	/* a device's pins may have changed (the engine's Changed covers the cables) */
	void jacks_changed ();

	/* ---- output: layout ---- */

	/* the height the sliver under row i (between it and row i + 1) wants, in pixels */
	double sliver_height (size_t i) const;
	double sliver_height_below (uint64_t row) const;

	struct JackPlace
	{
		McrPatchJack jack;
		bool         top;     /* on the device's top edge, else its bottom one */
		Rect         box;     /* pixels: plugdata's 13 unit box, its centre on the edge */
		bool         greyed;  /* a drag is on, and this jack can't take it */
		bool         flipped; /* on the other edge than its own, facing a cord */
	};

	/* the jacks shown on a device, and how many more each edge has ("+N") */
	std::vector<JackPlace> jacks (uint64_t device) const;
	int hidden_jacks (uint64_t device, bool top) const;

	/* ---- drawing: cr in the canvas's pixels, area the part to paint ---- */

	/* Play mode: the slivers, the cords and the jacks; Patch mode: the slivers */
	void draw_under_faces (cairo_t*, Rect const& area);
	/* Patch mode: the cords, the jacks and the cord being dragged; both modes: the
	 * Detective's dots and bubbles, and the layer's own bubbles */
	void draw_over_faces (cairo_t*, Rect const& area);

	/* ---- events, in the canvas's pixels; true: handled, the canvas does nothing more ---- */

	/* GDK's state bits, so that the canvas can pass ev->state as it is */
	static const unsigned shift_mask = 1 << 0;
	static const unsigned control_mask = 1 << 2;
	static const unsigned button1_mask = 1 << 8;

	bool button_press (double x, double y, unsigned button, int clicks, unsigned state); /* clicks: 2 for GDK_2BUTTON_PRESS */
	bool button_release (double x, double y, unsigned button, unsigned state);
	bool motion (double x, double y, unsigned state);
	/* the wheel: the selected cable's own gain, when it is over it (else the canvas scrolls) */
	bool scroll (double x, double y, int dy, unsigned state); /* dy: -1 up, 1 down */
	bool key_press (unsigned keyval, unsigned state);         /* GDK's keyvals */
	void leave ();
	/* Escape: the cord being dragged goes, else the selected one is deselected; false if
	 * there was neither (the canvas passes Escape on then) */
	bool escape ();
	/* the pointer's gesture ends without its release (the canvas hid, or lost the
	 * pointer): a cord being dragged goes, a sliver's edge stays where it was dragged */
	void cancel ();

	enum Cursor {
		Arrow,
		Hand,   /* on a jack, or dragging a cord */
		Resize  /* on an open sliver's bottom edge */
	};
	Cursor cursor () const { return _cursor; }
	bool dragging () const { return _drag.on; }

	/* ---- slivers ---- */

	size_t n_slivers () const { return _slivers.size (); }
	bool sliver_open (size_t i) const;
	/* pinned: it stays open in Play mode */
	void set_sliver_open (size_t i, bool open, bool pinned = true);
	void open_all_slivers ();
	void close_all_slivers ();
	void open_slivers_with_cords ();
	void open_slivers_around (uint64_t row); /* the one above the track and the one below */

	static constexpr double closed_sliver = 10; /* plugdata units */
	static constexpr double open_sliver = 80;

	/* what the session keeps of the slivers: each by the id of the row above it */
	struct SliverSave
	{
		uint64_t row;
		bool     open;
		bool     pinned; /* opened by hand */
		double   height; /* open, in units */

		SliverSave () : row (0), open (false), pinned (false), height (open_sliver) {}
	};
	std::vector<SliverSave> sliver_states () const;
	void set_sliver_states (std::vector<SliverSave> const&);

	/* ---- cords ---- */

	size_t n_cords () const { return _cords.size (); }
	bool has_selection () const { return _selected >= 0; }
	/* the rows the selected cord runs between (its source's, then its target's), none without one */
	std::vector<uint64_t> selected_rows () const;
	void select_cord (int); /* -1: none */
	bool remove_selected (); /* Delete and BackSpace */

	/* ---- Detective Mode ---- */

	void set_detective (bool);
	bool detective () const;
	void set_detective_speed (float); /* 0.1 .. 5 */
	float detective_speed () const;
	void set_detective_paused (bool);
	bool detective_paused () const;
	void detective_step ();

	/* the selected cord's messages, newest first ("#12  note 60 96"); its title
	 * ("1 KICK OUT1 → 2 COMP SC"); empty with no selection */
	std::string history_title () const;
	std::vector<std::string> history () const;

	/* the Detective's ~16 ms timer (a Glib timeout while it is needed): public for tests */
	void tick (int64_t now_us);
	/* microseconds: g_get_monotonic_time (), or a test's own clock */
	void set_clock (sigc::slot<int64_t> const&);

	PdTheme const& theme () const { return *_theme; }
	void set_theme (PdTheme const&);

	/* ---- signals ---- */

	sigc::signal<void, Rect> Redraw;   /* pixels */
	sigc::signal<void>       Relayout; /* the sliver heights changed: lay the rows out again, then set_model () */
	sigc::signal<void, Rect> Reveal;   /* scroll this into view (pixels): a folded cord's far end was clicked */
	sigc::signal<void>       HistoryChanged; /* history () or history_title () changed */

private:
	/* a jack of a device, and where it shows */
	struct Jack
	{
		McrPatchJack pin;
		std::string  tag;       /* "SC", "IN3", "OUT1", "MIDI" */
		int          cords[2];  /* its cords at each edge: [0] the bottom one, [1] the top one */
		bool         shown[2];
		Rect         box[2];    /* units */
		int          drag_edge; /* the edge a drag shows it on: -1 none */
		bool         candidate; /* a drag is on, and the cord is of its kind and way */
		std::string  refusal;   /* why it can't take the cord; empty: it can */

		Jack () : drag_edge (-1), candidate (false)
		{
			cords[0] = cords[1] = 0;
			shown[0] = shown[1] = false;
		}
	};

	struct Device
	{
		uint64_t                              id; /* 0: a track itself, as a cable's source (its header) */
		int                                   row;
		std::shared_ptr<ARDOUR::PluginInsert> insert;
		std::string                           name;
		std::string                           tag;
		Rect                                  rect; /* units */
		std::vector<Jack>                     jacks;
		int                                   hidden[2]; /* "+N" on each edge */
		Rect                                  more[2];   /* where "+N" is */

		Device () : id (0), row (0)
		{
			hidden[0] = hidden[1] = 0;
		}
	};

	struct Row
	{
		uint64_t    id;
		int         number;
		std::string name;
		Rect        rect;  /* units */
		Rect        header;
		int         track; /* its header's device: the track as a cable's source */

		Row () : id (0), number (0), track (-1) {}
	};

	/* a jack's identity across layouts */
	struct JackId
	{
		uint64_t device;
		int      type;
		bool     input;
		uint32_t instance;
		uint32_t pin;

		JackId () : device (0), type (0), input (false), instance (0), pin (0) {}
		bool operator< (JackId const&) const;
		bool operator== (JackId const&) const;
	};

	/* a cord's identity across refreshes */
	struct Key
	{
		uint64_t source_row;
		uint64_t source;
		uint64_t target_row;
		uint64_t target;
		int      type;
		uint32_t source_instance;
		uint32_t source_pin;
		uint32_t target_instance;
		uint32_t target_pin;

		Key ()
			: source_row (0), source (0), target_row (0), target (0), type (0)
			, source_instance (0), source_pin (0), target_instance (0), target_pin (0)
		{}
		bool operator< (Key const&) const;
		bool operator== (Key const&) const;
	};

	/* what lasts as long as the cable does: its Detective Mode */
	struct Watch
	{
		uint32_t                     handle; /* the Detective's, 0 none */
		std::shared_ptr<McrPatchTap> tap;    /* only while Detective Mode is on */
		uint64_t                     seen;   /* the newest message's number */
		std::string                  last;   /* and its text: a closed sliver shows it */
		int64_t                      blink;  /* the cord's glyphs blink until then (us) */
		double                       dashes; /* its audio dashes' phase */
		float                        level;
		Rect                         drawn;  /* where its bubbles were (units) */

		Watch () : handle (0), seen (0), blink (0), dashes (0), level (0) {}
	};

	/* a folded cord's mark in a sliver */
	struct Part
	{
		enum Kind {
			Flat, /* both ends face this closed sliver: a flat line between two drops */
			Stub  /* one end: a drop, a glyph and the far end's name */
		};

		Kind        kind;
		int         cord;
		int         end;      /* Stub: 0 the source end, 1 the target end */
		Pt          jack[2];  /* where the drops start: the jacks' centres (Stub: jack[0]) */
		double      lane;     /* y of its line */
		double      x0;       /* its extent along the sliver */
		double      x1;
		std::string text;     /* Stub: "↓4 VERB SC" */
		double      text_w;
		bool        shown;    /* there was room for it */
		bool        named;    /* Stub: and for its name */
		bool        near_top; /* an open sliver's stub hangs from the row above */

		Part () : kind (Flat), cord (-1), end (0), lane (0), x0 (0), x1 (0), text_w (0), shown (false), named (false), near_top (true) {}
	};

	struct Sliver
	{
		uint64_t          row;      /* the id of the row above */
		Rect              band;     /* units */
		bool              open;
		std::vector<Part> parts;
		int               overflow; /* folded cords it has no room for ("+N") */
		Rect              more;
		Rect              chevron;  /* the open and close toggle at its left end */

		Sliver () : row (0), open (false), overflow (0) {}
	};

	struct SliverState
	{
		bool   open;
		bool   pinned; /* opened by hand: stays open in Play mode */
		bool   sprung; /* opened by a dragged cord resting on it */
		double height; /* open, in units */

		SliverState () : open (false), pinned (false), sprung (false), height (open_sliver) {}
	};

	struct Cord
	{
		Key           key;
		McrPatchCable cable;
		PdCord::Kind  kind;
		int           device[2]; /* [0] the source, [1] the target */
		int           jack[2];
		bool          top[2];    /* the edge each end sits on */
		int           sliver[2]; /* the sliver each end faces; -1 none */
		Pt            at[2];     /* the jacks' centres */
		PdCord::Path  path;      /* the whole cord */
		bool          folded;    /* an end faces a closed sliver: drawn there, not whole */
		Rect          bounds;    /* of what is drawn, units */

		Cord () : kind (PdCord::Signal), folded (false)
		{
			device[0] = device[1] = jack[0] = jack[1] = sliver[0] = sliver[1] = -1;
			top[0] = top[1] = false;
		}
	};

	/* a cord being dragged out of a jack */
	struct Drag
	{
		bool    on;
		bool    moved;
		JackId  source;
		int     device;      /* the jack it started from */
		int     jack;
		bool    from_input;  /* out of an input: it looks for an output */
		Pt      pointer;     /* units */
		Pt      press;
		int     edge;        /* the source jack's edge now: 0 bottom, 1 top */
		int     snap_device; /* the jack it would land on; -1 none */
		int     snap_jack;
		int     sliver;      /* the closed sliver it rests on; -1 none */
		int64_t since;       /* since when (us) */

		Drag () : on (false), moved (false), device (-1), jack (-1), from_input (false), edge (0), snap_device (-1), snap_jack (-1), sliver (-1), since (0) {}
	};

	/* a sliver's bottom edge being dragged */
	struct Stretch
	{
		bool   on;
		int    sliver;
		double y;      /* the press, units */
		double height; /* the sliver's height then */

		Stretch () : on (false), sliver (-1), y (0), height (0) {}
	};

	/* a word near the pointer in a Detective bubble's look: a refusal, a gain */
	struct Note
	{
		std::string text;
		Pt          at;
		int64_t     shown;
		int64_t     until;
		Rect        drawn;

		Note () : shown (0), until (0) {}
	};

	McrPatchEngine*     _engine;
	PdTheme const*      _theme;
	PdCord::Detective   _detective;
	sigc::slot<int64_t> _clock;
	sigc::connection    _engine_changed;
	sigc::connection    _timer;

	double                     _scale;
	Rect                       _view_px;
	Rect                       _view; /* units */
	bool                       _play;
	std::vector<Row>           _rows;
	std::vector<Device>        _devices;
	std::map<uint64_t, int>    _device_index;
	std::vector<Sliver>        _slivers;
	std::vector<Cord>          _cords;
	std::vector<McrPatchCable> _cables;

	std::map<uint64_t, std::vector<McrPatchJack> > _pins; /* each device's, as the engine gave them */
	std::map<uint64_t, SliverState>                _sliver_state;
	std::map<Key, Watch>                           _watches;
	std::set<Key>                                  _known;    /* cords whose slivers Patch mode opened */
	std::map<JackId, std::string>                  _refusals; /* a drag's candidates, and why each can't take it */

	int     _selected; /* a cord; -1 none */
	Key     _selected_key;
	int     _hover_cord;
	int     _hover_device;
	int     _hover_jack;
	int     _hover_edge;
	int     _hover_chevron;
	Pt      _pointer;  /* units */
	bool    _pointer_in;
	Cursor  _cursor;
	Drag    _drag;
	Stretch _stretch;
	Note    _note;
	int     _press_sliver;  /* a press on a closed sliver: it opens on release */
	int     _opened_sliver; /* the last one a click opened, and when: */
	int64_t _opened_at;     /* the double click that follows doesn't close it */
	int64_t _wheel_scrolled; /* when the wheel last scrolled the canvas: cords passing under
	                          * the pointer then don't take it */
	int64_t _last_tick;
	bool    _relayout;      /* the slivers changed: Relayout once the call is done */

	/* layout */
	void rebuild ();
	void resolve_cords ();
	void place_jacks ();
	void place_ends ();
	void place_slivers ();
	void place_lanes (Sliver&);
	void auto_open ();
	void sync_watches ();
	void restore_selection ();
	int fit (Rect const& face) const;
	int sliver_facing (int row, bool top) const;
	bool faces_cord (int sliver) const;
	int find_jack (int device, bool input, McrPatchJack::Type, uint32_t instance, uint32_t pin);
	std::string far_end (Cord const&, int end) const;
	JackId jack_id (Device const&, Jack const&) const;
	McrPatchEnd end_of (Device const&, Jack const&) const;
	static Key key_of (McrPatchCable const&);

	/* hit testing (units) */
	int cord_at (Pt) const;
	int part_at (Pt, int& sliver) const;
	bool jack_at (Pt, int& device, int& jack, int& edge) const;
	int device_at (Pt) const;
	int sliver_at (Pt, double reach = 0) const;
	bool sliver_edge_at (Pt, int& sliver) const;

	/* the drag */
	void start_drag (int device, int jack, int edge);
	void update_drag (Pt);
	void end_drag (bool drop);
	void end_stretch ();
	void snap (Pt);
	void spring ();
	PdCord::Path drag_path () const;
	Rect drag_bounds () const;

	void hover (Pt);
	void set_hover_cord (int);
	void show_note (std::string const&, Pt, double seconds = 2);
	Rect note_bounds () const;
	Rect bubble_view () const; /* where bubbles must stay: the visible area (units) */

	/* drawing (units) */
	void draw_slivers (cairo_t*, Rect const&);
	void draw_part (cairo_t*, Sliver const&, Part const&);
	void draw_glyph (cairo_t*, Pt, PdCord::Kind, Gtkmm2ext::Color, bool blink);
	void draw_cords (cairo_t*, Rect const&);
	void draw_jacks (cairo_t*, Rect const&);
	void draw_detective (cairo_t*, Rect const&);
	void draw_drag (cairo_t*);
	void draw_label (cairo_t*);
	void draw_note (cairo_t*);
	Rect jack_label (int device, int jack, int edge, std::string* text = 0) const;

	PdCord::State cord_state (int) const;
	Watch const* watch (int cord) const;
	Rect cord_bounds (int) const;

	/* units and pixels */
	Rect to_px (Rect const&) const;
	Rect to_units (Rect const&) const;
	void redraw (Rect const& units);
	void redraw_all ();

	int64_t now () const;
	bool needs_timer () const;
	void update_timer ();
	bool timer ();
	void flush ();
	void engine_changed ();

	McrPatch (McrPatch const&);
	McrPatch& operator= (McrPatch const&);
};

/* McrPatchEngine on the session's cables */
class McrPatchCables : public McrPatchEngine
{
public:
	McrPatchCables (McrCables&);
	~McrPatchCables ();

	/* the ids the patch layer knows rows and devices by */
	static uint64_t key (PBD::ID const&);

	std::vector<McrPatchJack> jacks (McrPatchDevice const&);
	std::vector<McrPatchCable> cables ();
	std::string can_connect (McrPatchEnd const& from, McrPatchEnd const& to);
	std::string connect (McrPatchEnd const& from, McrPatchEnd const& to);
	std::string disconnect (McrPatchCable const&);
	bool gain (McrPatchCable const&, double& factor);
	bool set_gain (McrPatchCable const&, double factor);
	std::shared_ptr<McrPatchTap> tap (McrPatchCable const&);

private:
	McrCables&            _cables;
	PBD::ScopedConnection _changed;
};
