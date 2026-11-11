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

#ifdef WAF_BUILD
#include "gtk2ardour-config.h"
#endif

#include <algorithm>
#include <cmath>

#include <pango/pangocairo.h>
#include <pangomm/layout.h>

#include <ytkmm/menu.h>
#include <ytkmm/menu_elems.h>
#include <ytkmm/window.h>

#include "pbd/compose.h"

#include "ardour/chan_count.h"
#include "ardour/chan_mapping.h"
#include "ardour/plugin.h"
#include "ardour/plugin_insert.h"
#include "ardour/presentation_info.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/stripable.h"

#include "gtkmm2ext/colors.h"
#include "gtkmm2ext/keyboard.h"
#include "gtkmm2ext/utils.h"

#include "context_menu_helper.h"
#include "gui_thread.h"
#include "mcr_canvas.h"
#include "plugin_ui.h"
#include "processor_box.h"
#include "route_properties_box.h"
#include "spite_devices.h"
#include "ui_config.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using ArdourCanvas::Rect;

/* plugdata's dark theme (plugdata/Source/LookAndFeel.cpp) */
static Gtkmm2ext::Color const canvas_bg      = 0x232323ff; /* canvas_background */
static Gtkmm2ext::Color const grid_dots      = 0x6c6c6cff; /* canvas_dots (7f7f7f), a fifth of the way to the background */
static Gtkmm2ext::Color const canvas_text    = 0xe1e1e1ff; /* canvas_text */
static Gtkmm2ext::Color const row_line       = 0x393939ff; /* outline_colour: the lines between tracks */
static Gtkmm2ext::Color const object_outline = 0x555555ff; /* object_outline_colour: the jacks' rings */
static Gtkmm2ext::Color const header_bg      = 0x2c2c2cff; /* panel_background */
static Gtkmm2ext::Color const card_bg        = 0x191919ff; /* default_object_background */
static Gtkmm2ext::Color const signal_colour  = 0xff8500ff; /* signal_colour: audio jacks */
static Gtkmm2ext::Color const data_colour    = 0x42a2c8ff; /* data_colour: MIDI jacks */

static double
ui_scale ()
{
	return std::max (1.f, UIConfiguration::instance ().get_ui_scale ());
}

/* the canvas's own measures, in pixels at the UI's scale */

/* around everything */
static double
margin ()
{
	return rint (16 * ui_scale ());
}

static double
header_width ()
{
	return rint (132 * ui_scale ());
}

/* between devices */
static double
device_gap ()
{
	return rint (16 * ui_scale ());
}

/* above and below the devices: room for their jacks */
static double
row_padding ()
{
	return rint (12 * ui_scale ());
}

/* a closed sliver */
static double
sliver_height ()
{
	return rint (10 * ui_scale ());
}

/* plugdata's iolets: 13 px boxes, drawn 2 px in, the first and last 12.5 px in from
 * the sides */
static double
jack_size ()
{
	return rint (13 * ui_scale ());
}

static double
jack_radius ()
{
	return (jack_size () - 4 * ui_scale ()) * .5;
}

static double
jack_inset ()
{
	return 12.5 * ui_scale ();
}

McrCanvas::RowConnections::~RowConnections ()
{
	for (auto& c : windows) {
		c.disconnect ();
	}
}

McrCanvas::McrCanvas ()
	: _play (false)
	, _hover_row (-1)
	, _hover_device (-1)
	, _width (0)
	, _height (0)
	, _grid (0)
{
	set_name (X_("MCRCanvas"));

	/* what X clears the canvas to before each expose: no flash of the theme's colour */
	Gdk::Color bg;
	Gtkmm2ext::set_color_from_rgba (bg, canvas_bg);
	modify_bg (Gtk::STATE_NORMAL, bg);

	/* the bin window gets these at realize */
	add_events (Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK | Gdk::LEAVE_NOTIFY_MASK);

	/* a device's custom GUI in its window, or back */
	PluginUIWindow::CustomGUIShowing.connect (sigc::mem_fun (*this, &McrCanvas::custom_gui_showing));
	PluginUIWindow::CustomGUIHidden.connect (sigc::mem_fun (*this, &McrCanvas::custom_gui_hidden));
}

McrCanvas::~McrCanvas ()
{
	_rebuild_idle.disconnect ();
	_refill_idle.disconnect ();
	drop_grid ();
}

int
McrCanvas::face_height ()
{
	/* RoutePropertiesBox::face_height () at zoom 1: Live's 9.49 clip rows of 16 px */
	return rintf (9.49f * 16.f * UIConfiguration::instance ().get_ui_scale ());
}

int
McrCanvas::title_height ()
{
	return rintf (1.07f * 16.f * UIConfiguration::instance ().get_ui_scale ());
}

void
McrCanvas::set_session (Session* s)
{
	SessionHandlePtr::set_session (s);

	if (_session) {
		_session->RouteAdded.connect (_session_connections, invalidator (*this), std::bind (&McrCanvas::queue_rebuild, this), gui_context ());
		/* tracks reordered or hidden */
		PresentationInfo::Change.connect (_session_connections, invalidator (*this), std::bind (&McrCanvas::pi_change, this, _1), gui_context ());
	}
	rebuild ();
}

void
McrCanvas::session_going_away ()
{
	ENSURE_GUI_THREAD (*this, &McrCanvas::session_going_away);

	_rebuild_idle.disconnect ();
	_refill_idle.disconnect ();
	_to_refill.clear ();
	_route_connections.drop_connections ();
	_row_connections.clear ();
	_rows.clear ();
	_hover_row    = -1;
	_hover_device = -1;

	SessionHandlePtr::session_going_away ();

	layout ();
	queue_draw ();
}

void
McrCanvas::set_play_mode (bool yn)
{
	_play = yn;
}

void
McrCanvas::queue_rebuild ()
{
	if (!_rebuild_idle.connected ()) {
		_rebuild_idle = Glib::signal_idle ().connect (sigc::bind_return (sigc::mem_fun (*this, &McrCanvas::rebuild), false));
	}
}

void
McrCanvas::pi_change (PBD::PropertyChange const& what)
{
	if (what.contains (Properties::order) || what.contains (Properties::hidden)) {
		queue_rebuild ();
	}
}

/* one row per track, in the editor's order, but none for hidden tracks */
void
McrCanvas::rebuild ()
{
	_rebuild_idle.disconnect ();
	_refill_idle.disconnect ();
	_to_refill.clear ();
	_route_connections.drop_connections ();
	_row_connections.clear ();
	_rows.clear ();
	_hover_row    = -1;
	_hover_device = -1;

	if (_session && !_session->deletion_in_progress ()) {
		RouteList rl (*_session->get_tracks ());
		rl.sort (Stripable::Sorter ());

		for (auto const& r : rl) {
			PBD::ID const id = r->id ();

			/* a hidden track shown again, or a track going away, rebuilds */
			r->presentation_info ().PropertyChanged.connect (_route_connections, invalidator (*this), std::bind (&McrCanvas::route_pi_changed, this, _1, id), gui_context ());
			r->DropReferences.connect (_route_connections, invalidator (*this), std::bind (&McrCanvas::queue_rebuild, this), gui_context ());

			if (r->is_hidden ()) {
				continue;
			}

			r->PropertyChanged.connect (_route_connections, invalidator (*this), std::bind (&McrCanvas::route_property_changed, this, _1, id), gui_context ());
			r->active_changed.connect (_route_connections, invalidator (*this), std::bind (&McrCanvas::route_active_changed, this, id), gui_context ());
			r->processors_changed.connect (_route_connections, invalidator (*this), std::bind (&McrCanvas::queue_refill, this, id), gui_context ());

			_rows.push_back (McrRow ());
			McrRow& row = _rows.back ();
			row.route   = r;
			row.id      = id;
			row.name    = r->name ();
			row.colour  = r->color ();
			row.active  = r->active ();
			fill_row (row, r);
		}
	}

	layout ();
	queue_draw ();
}

/* a device's pins, from its plugin's ports (one instance's pins, for each instance) */
static std::vector<McrJack>
device_jacks (std::shared_ptr<PluginInsert> pi)
{
	std::vector<McrJack> jacks;

	for (int dir = 0; dir < 2; ++dir) {
		bool const      input = (dir == 0);
		ChanCount const c     = input ? pi->natural_input_streams () : pi->natural_output_streams ();

		for (DataType::iterator t = DataType::begin (); t != DataType::end (); ++t) {
			uint32_t const per = c.get (*t);
			for (uint32_t n = 0; n < pi->get_count (); ++n) {
				ChanMapping const       m   = input ? pi->input_map (n) : pi->output_map (n);
				std::shared_ptr<Plugin> plg = pi->plugin (n);
				for (uint32_t i = 0; i < per; ++i) {
					bool wired = false;
					m.get (*t, i, &wired);
					std::string const name = plg ? plg->describe_io_port (*t, input, i).name : std::string ();
					jacks.push_back (McrJack (*t, input, n * per + i, name, wired));
				}
			}
		}
	}
	return jacks;
}

void
McrCanvas::fill_row (McrRow& row, std::shared_ptr<Route> r)
{
	row.devices.clear ();

	/* the row's old devices' connections go */
	std::shared_ptr<RowConnections> rc (new RowConnections);
	_row_connections[row.id] = rc;

	/* the devices the Device view shows, in processor order */
	std::vector<std::shared_ptr<PluginInsert> > inserts;
	r->foreach_processor ([&inserts] (std::weak_ptr<Processor> w) {
		std::shared_ptr<Processor> p = w.lock ();
		if (Spite::is_device (p)) {
			inserts.push_back (std::dynamic_pointer_cast<PluginInsert> (p));
		}
	});

	PBD::ID const rid = row.id;

	for (auto const& pi : inserts) {
		PBD::ID const         did = pi->id ();
		ProcessorWindowProxy* wp  = pi->window_proxy ();

		row.devices.push_back (McrDevice ());
		McrDevice& d = row.devices.back ();
		d.insert     = pi;
		d.id         = did;
		d.card.read (pi);
		/* the window itself, not wp->visible (): until a window has been mapped,
		 * that says what the session saved, though a session loaded after the
		 * first one opens no windows */
		Gtk::Window* const w = wp ? wp->get (false) : 0;
		d.custom_open        = PluginUIWindow::custom_gui_shown (pi.get ());
		d.card.in_window     = d.custom_open || (w && w->get_mapped ());
		d.jacks              = device_jacks (pi);
		take_picture (d);

		pi->ActiveChanged.connect (rc->devices, invalidator (*this), std::bind (&McrCanvas::device_changed, this, rid, did), gui_context ());
		pi->PropertyChanged.connect (rc->devices, invalidator (*this), std::bind (&McrCanvas::device_changed, this, rid, did), gui_context ());
		if (std::shared_ptr<Plugin> plg = pi->plugin ()) {
			/* a converted Max for Live device takes its name from its preset */
			plg->PresetLoaded.connect (rc->devices, invalidator (*this), std::bind (&McrCanvas::device_changed, this, rid, did), gui_context ());
		}
		/* its pins */
		pi->PluginIoReConfigure.connect (rc->devices, invalidator (*this), std::bind (&McrCanvas::queue_refill, this, rid), gui_context ());
		pi->PluginMapChanged.connect (rc->devices, invalidator (*this), std::bind (&McrCanvas::queue_refill, this, rid), gui_context ());
		pi->PluginConfigChanged.connect (rc->devices, invalidator (*this), std::bind (&McrCanvas::queue_refill, this, rid), gui_context ());

		/* its window: a generic one says nothing else when it opens or closes */
		if (wp) {
			rc->windows.push_back (wp->signal_map.connect (sigc::bind (sigc::mem_fun (*this, &McrCanvas::device_window_changed), rid, did, true)));
			rc->windows.push_back (wp->signal_unmap.connect (sigc::bind (sigc::mem_fun (*this, &McrCanvas::device_window_changed), rid, did, false)));
		}
	}
}

/* a live face stands in as the picture the Device view kept of it, until the face
 * itself is here */
void
McrCanvas::take_picture (McrDevice& d) const
{
	Glib::RefPtr<Gdk::Pixbuf> pb = d.card.live ? spite_face_picture (d.id) : Glib::RefPtr<Gdk::Pixbuf> ();

	d.source  = pb;
	d.picture = Glib::RefPtr<Gdk::Pixbuf> ();

	if (pb && pb->get_width () > 0 && pb->get_height () > 0) {
		int const fh = face_height ();
		if (pb->get_height () != fh) {
			int const w = std::max (1, (int) rintf (fh * pb->get_width () / (float) pb->get_height ()));
			pb          = pb->scale_simple (w, fh, Gdk::INTERP_BILINEAR);
		}
		d.picture = pb;
	}
}

/* The Device view takes a new picture of a face once it has drawn itself (see
 * DeviceFace::snapshot), which it only does while the Cue page shows: each time the
 * canvas shows again, it takes the pictures that changed meanwhile. */
void
McrCanvas::refresh_pictures ()
{
	bool changed = false;

	for (auto& row : _rows) {
		for (auto& d : row.devices) {
			if (d.card.live && spite_face_picture (d.id) != d.source) {
				take_picture (d);
				changed = true;
			}
		}
	}

	if (changed) {
		layout (); /* a picture's width is its device's */
		queue_draw ();
	}
}

void
McrCanvas::on_map ()
{
	Gtk::Layout::on_map ();
	refresh_pictures ();
}

void
McrCanvas::queue_refill (PBD::ID const& id)
{
	_to_refill.insert (id);
	if (!_refill_idle.connected ()) {
		_refill_idle = Glib::signal_idle ().connect (sigc::mem_fun (*this, &McrCanvas::idle_refill));
	}
}

bool
McrCanvas::idle_refill ()
{
	std::set<PBD::ID> ids;
	ids.swap (_to_refill);

	double const width = _width;
	std::vector<Rect> changed;

	for (auto const& id : ids) {
		McrRow*                row = row_by_id (id);
		std::shared_ptr<Route> r   = row ? row->route.lock () : std::shared_ptr<Route> ();
		if (!r) {
			continue;
		}
		if (_hover_row >= 0 && &_rows[_hover_row] == row) {
			_hover_device = -1;
		}
		changed.push_back (row->rect);
		fill_row (*row, r);
	}

	layout ();

	if (_width != width) {
		queue_draw ();
	} else {
		/* rows keep their height: only theirs is redrawn */
		for (auto const& rect : changed) {
			invalidate (rect);
		}
	}
	return false;
}

/* where everything goes: rows stacked down the canvas, each its header, then its
 * devices left to right, all the Device view's height; a closed sliver between rows */
void
McrCanvas::layout ()
{
	double const pad  = margin ();
	double const hw   = header_width ();
	double const gap  = device_gap ();
	double const rpad = row_padding ();
	double const sl   = sliver_height ();
	double const th   = title_height ();
	double const fh   = face_height ();
	double const cw   = DefaultFace::width (fh, th);

	double y     = pad;
	double right = pad + hw;

	for (size_t i = 0; i < _rows.size (); ++i) {
		McrRow&      row = _rows[i];
		double const top = y + rpad;

		row.header = Rect (pad, top, pad + hw, top + th + fh);

		double x = pad + hw + gap;
		for (auto& d : row.devices) {
			/* a live face is as wide as its picture, as in the Device view */
			double const w = d.picture ? std::max<double> (d.picture->get_width (), 2 * th) : cw;
			d.rect         = Rect (x, top, x + w, top + th + fh);
			layout_jacks (d);
			x += w + gap;
		}
		right = std::max (right, x - gap);

		row.rect = Rect (pad, y, pad, top + th + fh + rpad);
		y        = row.rect.y1;

		if (i + 1 < _rows.size ()) {
			row.sliver = Rect (pad, y, pad, y + sl);
			y += sl;
		} else {
			row.sliver = Rect ();
		}
	}

	/* rows and slivers go across the canvas */
	for (auto& row : _rows) {
		row.rect.x1 = right;
		if (!row.sliver.empty ()) {
			row.sliver.x1 = right;
		}
	}

	_width  = right + pad;
	_height = y + pad;
	set_size (ceil (_width), ceil (_height));
}

/* plugdata's iolets: inputs on the top edge, outputs on the bottom one, a lone one
 * on the left, more spread from side to side. A device with more pins than fit an
 * edge shows its wired ones and a free one of each type (for a cable), and says how
 * many more it has. */
void
McrCanvas::layout_jacks (McrDevice& d) const
{
	double const span = std::max (0., d.rect.width () - 2 * jack_inset ());
	size_t const fit  = 1 + (size_t) floor (span / (jack_size () + 2 * ui_scale ()));

	d.more_inputs  = 0;
	d.more_outputs = 0;

	for (int dir = 0; dir < 2; ++dir) {
		bool const            input = (dir == 0);
		std::vector<McrJack*> edge;

		for (auto& j : d.jacks) {
			if (j.input == input) {
				j.top   = input;
				j.shown = false;
				edge.push_back (&j);
			}
		}

		size_t shown = 0;
		if (edge.size () <= fit) {
			for (auto const& j : edge) {
				j->shown = true;
			}
			shown = edge.size ();
		} else {
			std::set<uint32_t> spare; /* the types that have their free one */
			for (auto const& j : edge) {
				if (shown < fit && (j->wired || spare.insert ((uint32_t) j->type).second)) {
					j->shown = true;
					++shown;
				}
			}
		}

		if (input) {
			d.more_inputs = edge.size () - shown;
		} else {
			d.more_outputs = edge.size () - shown;
		}

		double const y = input ? d.rect.y0 + 1 : d.rect.y1;
		size_t       n = 0;
		for (auto const& j : edge) {
			if (!j->shown) {
				continue;
			}
			j->x = d.rect.x0 + jack_inset () + (shown > 1 ? n * span / (shown - 1) : 0);
			j->y = y;
			++n;
		}
	}
}

McrRow*
McrCanvas::row_by_id (PBD::ID const& id)
{
	for (auto& row : _rows) {
		if (row.id == id) {
			return &row;
		}
	}
	return 0;
}

McrDevice*
McrCanvas::device_by_id (PBD::ID const& route, PBD::ID const& device)
{
	McrRow* row = row_by_id (route);
	if (!row) {
		return 0;
	}
	for (auto& d : row->devices) {
		if (d.id == device) {
			return &d;
		}
	}
	return 0;
}

void
McrCanvas::route_property_changed (PBD::PropertyChange const& what, PBD::ID id)
{
	if (!what.contains (Properties::name)) {
		return;
	}
	McrRow*                row = row_by_id (id);
	std::shared_ptr<Route> r   = row ? row->route.lock () : std::shared_ptr<Route> ();
	if (r) {
		row->name = r->name ();
		invalidate (row->header);
	}
}

void
McrCanvas::route_pi_changed (PBD::PropertyChange const& what, PBD::ID id)
{
	if (what.contains (Properties::hidden) || what.contains (Properties::order)) {
		queue_rebuild ();
		return;
	}
	if (!what.contains (Properties::color)) {
		return;
	}
	McrRow*                row = row_by_id (id);
	std::shared_ptr<Route> r   = row ? row->route.lock () : std::shared_ptr<Route> ();
	if (r) {
		row->colour = r->color ();
		invalidate (row->header);
	}
}

void
McrCanvas::route_active_changed (PBD::ID id)
{
	McrRow*                row = row_by_id (id);
	std::shared_ptr<Route> r   = row ? row->route.lock () : std::shared_ptr<Route> ();
	if (r) {
		row->active = r->active ();
		invalidate (row->rect);
	}
}

/* turned on or off, renamed */
void
McrCanvas::device_changed (PBD::ID route, PBD::ID device)
{
	McrDevice*                    d  = device_by_id (route, device);
	std::shared_ptr<PluginInsert> pi = d ? d->insert.lock () : std::shared_ptr<PluginInsert> ();
	if (!pi) {
		return;
	}
	bool const in_window = d->card.in_window;
	d->card.read (pi);
	d->card.in_window = in_window;
	invalidate_device (*d);
}

void
McrCanvas::device_window_changed (PBD::ID route, PBD::ID device, bool open)
{
	McrDevice*                    d  = device_by_id (route, device);
	std::shared_ptr<PluginInsert> pi = d ? d->insert.lock () : std::shared_ptr<PluginInsert> ();
	if (!pi) {
		return;
	}
	/* a custom GUI's window unmaps before it says it is gone */
	d->card.in_window = open || PluginUIWindow::custom_gui_shown (pi.get ());
	invalidate_device (*d);
}

void
McrCanvas::custom_gui_showing (PlugInsertBase* pib)
{
	for (auto& row : _rows) {
		for (auto& d : row.devices) {
			std::shared_ptr<PluginInsert> pi = d.insert.lock ();
			if (pi && static_cast<PlugInsertBase*> (pi.get ()) == pib) {
				d.custom_open    = true;
				d.card.in_window = true;
				invalidate_device (d);
			}
		}
	}
}

void
McrCanvas::custom_gui_hidden (PlugInsertBase* pib)
{
	for (auto& row : _rows) {
		for (auto& d : row.devices) {
			std::shared_ptr<PluginInsert> pi = d.insert.lock ();
			if (pi && static_cast<PlugInsertBase*> (pi.get ()) == pib) {
				d.custom_open    = false;
				d.card.in_window = false;
				invalidate_device (d);
			}
		}
	}
}

McrCanvas::Hit
McrCanvas::hit (double x, double y) const
{
	Hit h;
	for (size_t r = 0; r < _rows.size (); ++r) {
		McrRow const& row = _rows[r];
		if (y < row.rect.y0 || y >= row.rect.y1) {
			continue;
		}
		h.row = r;
		for (size_t i = 0; i < row.devices.size (); ++i) {
			Rect const&             dr = row.devices[i].rect;
			DefaultFace::Part const p  = DefaultFace::part_at (x - dr.x0, y - dr.y0, dr.width (), dr.height (), title_height ());
			if (p != DefaultFace::Outside) {
				h.device = i;
				h.part   = p;
				break;
			}
		}
		break;
	}
	return h;
}

void
McrCanvas::set_hover (int row, int device)
{
	if (row == _hover_row && device == _hover_device) {
		return;
	}
	if (_hover_row >= 0 && _hover_row < (int) _rows.size () && _hover_device >= 0 && _hover_device < (int) _rows[_hover_row].devices.size ()) {
		invalidate_device (_rows[_hover_row].devices[_hover_device]);
	}
	_hover_row    = row;
	_hover_device = device;
	if (_hover_row >= 0 && _hover_row < (int) _rows.size () && _hover_device >= 0 && _hover_device < (int) _rows[_hover_row].devices.size ()) {
		invalidate_device (_rows[_hover_row].devices[_hover_device]);
	}
}

bool
McrCanvas::on_motion_notify_event (GdkEventMotion* ev)
{
	Glib::RefPtr<Gdk::Window> bin = get_bin_window ();
	if (bin && ev->window == bin->gobj ()) {
		Hit const h = hit (ev->x, ev->y);
		set_hover (h.row, h.device);
	}
	return Gtk::Layout::on_motion_notify_event (ev);
}

bool
McrCanvas::on_leave_notify_event (GdkEventCrossing* ev)
{
	set_hover (-1, -1);
	return Gtk::Layout::on_leave_notify_event (ev);
}

bool
McrCanvas::on_button_press_event (GdkEventButton* ev)
{
	Glib::RefPtr<Gdk::Window> bin = get_bin_window ();
	if (!bin || ev->window != bin->gobj ()) {
		return Gtk::Layout::on_button_press_event (ev);
	}
	Hit const h = hit (ev->x, ev->y);
	if (h.device < 0) {
		return Gtk::Layout::on_button_press_event (ev);
	}
	McrDevice const&              d    = _rows[h.row].devices[h.device];
	std::shared_ptr<PluginInsert> pi   = d.insert.lock ();
	bool const                    live = d.card.live;
	if (!pi) {
		return true;
	}

	if (Gtkmm2ext::Keyboard::is_context_menu_event (ev)) {
		device_menu (pi, live, ev);
		return true;
	}
	if (ev->button != 1) {
		return false;
	}
	if (ev->type == GDK_BUTTON_PRESS && h.part == DefaultFace::Led) {
		pi->enable (!pi->enabled ());
		return true;
	}
	if (ev->type == GDK_2BUTTON_PRESS && h.part != DefaultFace::Led) {
		/* its own window, as the Device view's faces do: an open one is raised */
		Spite::open_device_window (pi, true);
		return true;
	}
	return true;
}

void
McrCanvas::device_menu (std::shared_ptr<PluginInsert> pi, bool live, GdkEventButton* ev)
{
	using namespace Gtk::Menu_Helpers;

	std::weak_ptr<Processor> wp (pi);
	Gtk::Menu*               m     = ARDOUR_UI_UTILS::shared_popup_menu ();
	MenuList&                items = m->items ();

	/* PgD4S's window is its patch editor */
	items.push_back (MenuElem (live ? _("Edit in plugdata for Spite") : _("Open Its Window"), [wp] () { Spite::open_device_window (wp.lock (), true); }));
	items.push_back (MenuElem (_("Generic Controls..."), [wp] () { Spite::open_device_window (wp.lock (), false); }));
	items.push_back (SeparatorElem ());
	items.push_back (MenuElem (pi->enabled () ? _("Turn Off") : _("Turn On"), [wp] () {
		if (std::shared_ptr<Processor> p = wp.lock ()) {
			p->enable (!p->enabled ());
		}
	}));

	m->popup (ev->button, ev->time);
}

void
McrCanvas::invalidate (Rect const& r)
{
	Glib::RefPtr<Gdk::Window> bin = get_bin_window ();
	if (!bin || r.width () <= 0 || r.height () <= 0) {
		return;
	}
	int const x0 = floor (r.x0) - 1;
	int const y0 = floor (r.y0) - 1;
	int const x1 = ceil (r.x1) + 1;
	int const y1 = ceil (r.y1) + 1;
	bin->invalidate_rect (Gdk::Rectangle (x0, y0, x1 - x0, y1 - y0), false);
}

/* a device and what it draws around itself: its jacks, and how many more it has */
Rect
McrCanvas::device_area (McrDevice const& d) const
{
	return d.rect.expand (row_padding ());
}

void
McrCanvas::invalidate_device (McrDevice const& d)
{
	invalidate (device_area (d));
}

bool
McrCanvas::on_expose_event (GdkEventExpose* ev)
{
	Glib::RefPtr<Gdk::Window> bin = get_bin_window ();
	if (!bin || ev->window != bin->gobj ()) {
		return Gtk::Layout::on_expose_event (ev);
	}

	cairo_t* cr = gdk_cairo_create (ev->window);
	gdk_cairo_region (cr, ev->region);
	cairo_clip (cr);
	paint (cr, Rect (ev->area.x, ev->area.y, ev->area.x + ev->area.width, ev->area.y + ev->area.height));
	cairo_destroy (cr);

	/* then its children (later: the live faces), then (later) the cables over everything */
	Gtk::Layout::on_expose_event (ev);
	return true;
}

void
McrCanvas::drop_grid ()
{
	if (_grid) {
		cairo_surface_destroy (_grid);
		_grid = 0;
	}
}

/* plugdata's dot grid: a dot every 25 px */
void
McrCanvas::paint_grid (cairo_t* cr, Rect const& area)
{
	if (!_grid) {
		double const s  = ui_scale ();
		int const    sp = std::max (8, (int) rint (25 * s));
		_grid           = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, sp, sp);
		cairo_t* g      = cairo_create (_grid);
		cairo_arc (g, sp * .5, sp * .5, s, 0, 2 * M_PI);
		Gtkmm2ext::set_source_rgb_a (g, grid_dots, .8);
		cairo_fill (g);
		cairo_destroy (g);
	}
	cairo_pattern_t* pat = cairo_pattern_create_for_surface (_grid);
	cairo_pattern_set_extend (pat, CAIRO_EXTEND_REPEAT);
	cairo_set_source (cr, pat);
	cairo_rectangle (cr, area.x0, area.y0, area.width (), area.height ());
	cairo_fill (cr);
	cairo_pattern_destroy (pat);
}

void
McrCanvas::paint (cairo_t* cr, Rect const& area)
{
	Gtkmm2ext::set_source_rgba (cr, canvas_bg);
	cairo_rectangle (cr, area.x0, area.y0, area.width (), area.height ());
	cairo_fill (cr);
	paint_grid (cr, area);

	if (_rows.empty ()) {
		if (_session) {
			Glib::RefPtr<Pango::Layout> l = Pango::Layout::create (get_pango_context ());
			l->set_font_description (UIConfiguration::instance ().get_NormalFont ());
			l->set_text (_("No tracks yet: each track's devices show here, one row per track"));
			Gtkmm2ext::set_source_rgb_a (cr, canvas_text, .5);
			cairo_move_to (cr, margin (), margin ());
			pango_cairo_show_layout (cr, l->gobj ());
			cairo_new_path (cr);
		}
		return;
	}

	for (size_t i = 0; i < _rows.size (); ++i) {
		McrRow const& row  = _rows[i];
		Rect const    band = row.sliver.empty () ? row.rect : row.rect.extend (row.sliver);
		if (band.expand (row_padding ()).intersection (area).empty ()) {
			continue;
		}
		paint_row (cr, row, i, area);
	}
}

void
McrCanvas::paint_row (cairo_t* cr, McrRow const& row, int r, Rect const& area)
{
	/* the line between this track and the next: a closed sliver */
	if (!row.sliver.empty ()) {
		double const y = floor ((row.sliver.y0 + row.sliver.y1) * .5) + .5;
		cairo_move_to (cr, row.sliver.x0, y);
		cairo_line_to (cr, row.sliver.x1, y);
		cairo_set_line_width (cr, 1);
		Gtkmm2ext::set_source_rgba (cr, row_line);
		cairo_stroke (cr);
	}

	paint_header (cr, row);

	Glib::RefPtr<Pango::Context> ctx = get_pango_context ();
	double const                 th  = title_height ();

	for (size_t i = 0; i < row.devices.size (); ++i) {
		McrDevice const& d = row.devices[i];
		if (device_area (d).intersection (area).empty ()) {
			continue;
		}
		bool const hovered = (r == _hover_row && (int) i == _hover_device);
		d.card.paint (cr, ctx, d.rect.x0, d.rect.y0, d.rect.width (), d.rect.height (), th, hovered);
		paint_picture (cr, d);
		paint_jacks (cr, d, hovered);
	}

	if (!row.active) {
		/* an inactive track's devices do nothing */
		Rect const dim (row.header.x1, row.rect.y0, row.rect.x1, row.rect.y1);
		cairo_rectangle (cr, dim.x0, dim.y0, dim.width (), dim.height ());
		Gtkmm2ext::set_source_rgb_a (cr, canvas_bg, .55);
		cairo_fill (cr);
	}
}

void
McrCanvas::paint_header (cairo_t* cr, McrRow const& row)
{
	double const s = ui_scale ();
	Rect const&  h = row.header;
	double const r = 2.75 * s; /* plugdata's object corners */
	double const band = rint (4 * s);
	double const pad  = rint (8 * s);

	cairo_save (cr);
	Gtkmm2ext::rounded_rectangle (cr, h.x0 + .5, h.y0 + .5, h.width () - 1, h.height () - 1, r);
	Gtkmm2ext::set_source_rgba (cr, header_bg);
	cairo_fill_preserve (cr);
	cairo_clip (cr);

	/* the track's colour, down its side */
	cairo_rectangle (cr, h.x0, h.y0, band, h.height ());
	Gtkmm2ext::set_source_rgb_a (cr, row.colour ? row.colour : object_outline, 1.);
	cairo_fill (cr);

	/* its name, on up to three lines */
	Glib::RefPtr<Pango::Layout> l = Pango::Layout::create (get_pango_context ());
	l->set_font_description (UIConfiguration::instance ().get_NormalFont ());
	l->set_text (row.name);
	l->set_width ((int) ((h.width () - band - 2 * pad) * PANGO_SCALE));
	l->set_wrap (Pango::WRAP_WORD_CHAR);
	l->set_ellipsize (Pango::ELLIPSIZE_END);
	l->set_height (-3);
	Gtkmm2ext::set_source_rgb_a (cr, canvas_text, row.active ? 1. : .5);
	cairo_move_to (cr, h.x0 + band + pad, h.y0 + rint (6 * s));
	pango_cairo_show_layout (cr, l->gobj ());
	cairo_new_path (cr);
	cairo_restore (cr);
}

/* a live face's last picture, under its title strip; washed out while the device's
 * own GUI is in its window, as the Device view's face is (its generic controls'
 * window leaves the face live there) */
void
McrCanvas::paint_picture (cairo_t* cr, McrDevice const& d)
{
	if (!d.picture) {
		return;
	}
	double const th = title_height ();
	Rect const   body (d.rect.x0 + 1, d.rect.y0 + th, d.rect.x1 - 1, d.rect.y1 - 1);

	cairo_save (cr);
	cairo_rectangle (cr, body.x0, body.y0, body.width (), body.height ());
	cairo_clip (cr);
	Gtkmm2ext::set_source_rgba (cr, card_bg); /* the card's own lines don't show through */
	cairo_paint (cr);
	gdk_cairo_set_source_pixbuf (cr, d.picture->gobj (), d.rect.x0, d.rect.y0 + th);
	cairo_paint_with_alpha (cr, d.card.on ? 1. : .5);
	if (d.custom_open) {
		cairo_set_source_rgba (cr, 1, 1, 1, .62);
		cairo_paint (cr);
	}
	cairo_restore (cr);
}

void
McrCanvas::paint_jacks (cairo_t* cr, McrDevice const& d, bool hovered)
{
	double const radius = jack_radius ();

	cairo_save (cr);
	if (!hovered) {
		/* half-moons inside the box, as plugdata draws the iolets of an object the
		 * mouse isn't on */
		cairo_rectangle (cr, d.rect.x0, d.rect.y0, d.rect.width (), d.rect.height ());
		cairo_clip (cr);
	}
	cairo_set_line_width (cr, 1);
	for (auto const& j : d.jacks) {
		if (!j.shown) {
			continue;
		}
		cairo_arc (cr, j.x, j.y, radius, 0, 2 * M_PI);
		Gtkmm2ext::set_source_rgba (cr, (uint32_t) j.type == (uint32_t) DataType::MIDI ? data_colour : signal_colour);
		cairo_fill_preserve (cr);
		Gtkmm2ext::set_source_rgba (cr, object_outline);
		cairo_stroke (cr);
	}
	cairo_restore (cr);

	/* how many more pins the device has, in small lettering past its last jack */
	for (int dir = 0; dir < 2; ++dir) {
		uint32_t const more = dir == 0 ? d.more_inputs : d.more_outputs;
		if (!more) {
			continue;
		}
		Glib::RefPtr<Pango::Layout> l = Pango::Layout::create (get_pango_context ());
		l->set_font_description (UIConfiguration::instance ().get_SmallerFont ());
		l->set_text (string_compose (X_("+%1"), more));
		int tw, th;
		l->get_pixel_size (tw, th);
		double const y = dir == 0 ? d.rect.y0 - th - 1 : d.rect.y1 + 1;
		Gtkmm2ext::set_source_rgb_a (cr, canvas_text, .45);
		cairo_move_to (cr, d.rect.x1 - tw, y);
		pango_cairo_show_layout (cr, l->gobj ());
		cairo_new_path (cr);
	}
}
