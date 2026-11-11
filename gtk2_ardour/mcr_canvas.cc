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
#include <cstring>

#include <pango/pangocairo.h>
#include <pangomm/layout.h>

#include <ytkmm/adjustment.h>
#include <ytkmm/menu.h>
#include <ytkmm/menu_elems.h>
#include <ytkmm/window.h>

#include "pbd/compose.h"
#include "pbd/error.h"
#include "pbd/file_utils.h"

#include "ardour/filesystem_paths.h"
#include "ardour/lv2_plugin.h"
#include "ardour/plugin.h"
#include "ardour/plugin_insert.h"
#include "ardour/presentation_info.h"
#include "ardour/route.h"
#include "ardour/selection.h"
#include "ardour/session.h"
#include "ardour/stripable.h"
#include "ardour/track.h"

#include "gtkmm2ext/colors.h"
#include "gtkmm2ext/keyboard.h"
#include "gtkmm2ext/utils.h"

#include "context_menu_helper.h"
#include "gui_thread.h"
#include "lv2_plugin_ui.h"
#include "mcr_cables.h"
#include "mcr_canvas.h"
#include "pd_cord.h"
#include "pd_detective.h"
#include "plugin_ui.h"
#include "processor_box.h"
#include "route_properties_box.h"
#include "spite_devices.h"
#include "ui_config.h"

/* X last: its macros (None...) clash with names in the headers above */
#ifdef GDK_WINDOWING_X11
#include <ydk/gdkx.h>
#if defined(HAVE_XCOMPOSITE) && defined(HAVE_CAIRO_XLIB)
/* live faces: their composited windows' pixmaps, painted with cairo */
#include <X11/extensions/Xcomposite.h>
#include <cairo-xlib.h>
#define MCR_LIVE_FACES
#endif
#endif

#include "pbd/i18n.h"

using namespace ARDOUR;
using ArdourCanvas::Rect;

/* plugdata's dark theme (plugdata/Source/LookAndFeel.cpp) */
static Gtkmm2ext::Color const canvas_bg      = 0x232323ff; /* canvas_background */
static Gtkmm2ext::Color const grid_dots      = 0x6c6c6cff; /* canvas_dots (7f7f7f), a fifth of the way to the background */
static Gtkmm2ext::Color const canvas_text    = 0xe1e1e1ff; /* canvas_text */
static Gtkmm2ext::Color const object_outline = 0x555555ff; /* object_outline_colour: a track without a colour */
static Gtkmm2ext::Color const header_bg      = 0x2c2c2cff; /* panel_background */
static Gtkmm2ext::Color const card_bg        = 0x191919ff; /* default_object_background */

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

/* plugdata's font, for the cables' lettering and Detective Mode's bubbles: loaded for
 * them alone, once (the rest of Spite keeps its fonts) */
static void
load_cable_font ()
{
	static bool loaded = false;
	if (loaded) {
		return;
	}
	loaded = true;
	std::string path;
	if (PBD::find_file (ARDOUR::ardour_data_search_path (), X_("InterRegular.ttf"), path) && PdCord::load_font_file (path)) {
		return;
	}
	/* "Inter, Sans": the system's sans then */
	PBD::warning << _("MCR: plugdata's font (InterRegular.ttf) isn't on the data path") << endmsg;
}

McrCanvas::RowConnections::~RowConnections ()
{
	for (auto& c : windows) {
		c.disconnect ();
	}
}

McrCanvas::McrCanvas ()
	: _cables (0)
	, _engine (0)
	, _in_layout (false)
	, _relayout (false)
	, _cursor (-1)
	, _drag_x (0)
	, _drag_y (0)
	, _drag_state (0)
	, _play (false)
	, _hover_row (-1)
	, _hover_device (-1)
	, _width (0)
	, _height (0)
	, _grid (0)
	, _shield (0)
	, _composite (false)
	, _debug (g_getenv ("SPITE_MCR_DEBUG") != 0)
	, _shown (false)
	, _turned_to (false)
{
	set_name (X_("MCRCanvas"));

	/* what X clears the canvas to before each expose: no flash of the theme's colour */
	Gdk::Color bg;
	Gtkmm2ext::set_color_from_rgba (bg, canvas_bg);
	modify_bg (Gtk::STATE_NORMAL, bg);

	/* the bin window gets these at realize (and the shield, see on_realize) */
	add_events (Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK | Gdk::ENTER_NOTIFY_MASK | Gdk::LEAVE_NOTIFY_MASK);

	/* a device's custom GUI in its window, or back */
	PluginUIWindow::CustomGUIShowing.connect (sigc::mem_fun (*this, &McrCanvas::custom_gui_showing));
	PluginUIWindow::CustomGUIHidden.connect (sigc::mem_fun (*this, &McrCanvas::custom_gui_hidden));
	/* another view let faces go: the MCR takes (or makes) the ones it shows, if it shows */
	SpiteFaceHolder::FacesFree.connect (sigc::mem_fun (*this, &McrCanvas::queue_faces));

	if (_debug) {
		/* a test of the layer over the faces: a red cross over each live face */
		PaintOverFaces.connect (sigc::mem_fun (*this, &McrCanvas::paint_debug));
	}

	/* the keys go to the patching layer (Delete takes the selected cable) once the canvas
	 * is clicked */
	set_can_focus (true);
	add_events (Gdk::SCROLL_MASK | Gdk::KEY_PRESS_MASK);

	/* a press's own grab of the pointer is broken (its window hid, another one took the
	 * pointer), or a dialog or a menu takes GTK's grab: the release won't come here, so a
	 * cable being dragged goes */
	signal_grab_broken_event ().connect (sigc::mem_fun (*this, &McrCanvas::grab_broken), false);
	signal_grab_notify ().connect (sigc::mem_fun (*this, &McrCanvas::grab_notify));

	/* Detective Mode's switches are made here, on the GUI thread, before an engine can
	 * trace; the cables' text is in plugdata's font */
	PdCord::Detective::instance ();
	load_cable_font ();

	_patch.set_play_mode (_play);
	_patch.Redraw.connect (sigc::mem_fun (*this, &McrCanvas::patch_redraw));
	_patch.Relayout.connect (sigc::mem_fun (*this, &McrCanvas::patch_relayout));
	_patch.Reveal.connect (sigc::mem_fun (*this, &McrCanvas::patch_reveal));
}

McrCanvas::~McrCanvas ()
{
	_autoscroll.disconnect ();
	drop_cables ();
	_hscroll.disconnect ();
	_vscroll.disconnect ();
	_shown_idle.disconnect ();
	_rebuild_idle.disconnect ();
	_refill_idle.disconnect ();
	_face_idle.disconnect ();
	_reveal_timer.disconnect ();
	_layout_idle.disconnect ();
	free_faces ();
	drop_grid ();
}

/* the face height PgD4S faces fill (libardour's, which PgD4S reads too) */
extern "C" int spite_lv2_ui_face_height (void);

int
McrCanvas::face_height ()
{
	/* One for all faces, the Device view's (it zooms them): PgD4S reads it from Spite,
	 * and a face resized while it is hidden (as a view's own height would do on each
	 * page switch) draws itself wrongly when it shows again. Until the Device view sets
	 * it, RoutePropertiesBox::face_height () at zoom 1: Live's 9.49 clip rows of 16 px */
	int const h = spite_lv2_ui_face_height ();
	if (h > 0) {
		return h;
	}
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
	drop_cables ();

	SessionHandlePtr::set_session (s);

	if (_session) {
		_session->RouteAdded.connect (_session_connections, invalidator (*this), std::bind (&McrCanvas::queue_rebuild, this), gui_context ());
		/* tracks reordered or hidden */
		PresentationInfo::Change.connect (_session_connections, invalidator (*this), std::bind (&McrCanvas::pi_change, this, _1), gui_context ());
		/* the session's cables, from the moment it is loaded: McrCables tidies up only after
		 * devices it has seen */
		_cables = new McrCables (*_session, gui_context ());
		_engine = new McrPatchCables (*_cables);
	}
	rebuild ();
	_patch.set_engine (_engine);
}

/* the patching layer lets go of the cables first, then they go (before the session does) */
void
McrCanvas::drop_cables ()
{
	_patch.set_engine (0);
	delete _engine;
	_engine = 0;
	delete _cables;
	_cables = 0;
}

void
McrCanvas::session_going_away ()
{
	ENSURE_GUI_THREAD (*this, &McrCanvas::session_going_away);

	drop_cables ();
	_rebuild_idle.disconnect ();
	_refill_idle.disconnect ();
	_face_idle.disconnect ();
	_to_refill.clear ();
	_route_connections.drop_connections ();
	_row_connections.clear ();
	_rows.clear ();
	_hover_row    = -1;
	_hover_device = -1;
	free_faces ();
	_face_failed.clear ();

	SessionHandlePtr::session_going_away ();

	layout ();
	queue_draw ();
}

void
McrCanvas::set_play_mode (bool yn)
{
	_play = yn;
	show_shield ();
	_patch.set_play_mode (yn); /* cables behind the faces or over them; slivers open or close */
	queue_draw (); /* faces are dimmed in Patch mode */
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
			row.number  = std::max<int64_t> (0, r->track_number ());
			row.active  = r->active ();
			fill_row (row, r);
		}
	}

	prune_faces ();
	layout ();
	queue_draw ();
	queue_faces ();
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
	bool      changed = false;
	int const fh      = face_height (); /* the Device view's zoom may have changed it */

	for (auto& row : _rows) {
		for (auto& d : row.devices) {
			if (d.card.live && (spite_face_picture (d.id) != d.source || (d.picture && d.picture->get_height () != fh))) {
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
McrCanvas::on_realize ()
{
	Gtk::Layout::on_realize ();

	/* the part of the canvas that shows: the patching layer keeps its bubbles in it */
	_cursor = -1;
	_hscroll.disconnect ();
	_vscroll.disconnect ();
	if (get_hadjustment ()) {
		_hscroll = get_hadjustment ()->signal_value_changed ().connect (sigc::mem_fun (*this, &McrCanvas::view_changed));
	}
	if (get_vadjustment ()) {
		_vscroll = get_vadjustment ()->signal_value_changed ().connect (sigc::mem_fun (*this, &McrCanvas::view_changed));
	}

	/* Its window: once the MCR is detached, its window hides and shows again without the
	 * canvas being unmapped or mapped (GTK only withdraws the window), and no window is
	 * unmapped by GTK when it is minimized */
	if (Gtk::Window* top = dynamic_cast<Gtk::Window*> (get_toplevel ())) {
		_top_map   = top->signal_map_event ().connect (sigc::mem_fun (*this, &McrCanvas::toplevel_mapped));
		_top_unmap = top->signal_unmap_event ().connect (sigc::mem_fun (*this, &McrCanvas::toplevel_unmapped));
		_top_state = top->signal_window_state_event ().connect (sigc::mem_fun (*this, &McrCanvas::toplevel_state));
	}

#ifdef MCR_LIVE_FACES
	GdkWindow* const bin = get_bin_window ()->gobj ();
	_composite = gdk_display_supports_composite (gdk_drawable_get_display (bin));
	if (!_composite) {
		return; /* no live faces: their pictures stand in */
	}

	/* The faces' windows go in the bin window. A composited window isn't clipped to its
	 * parent (it is drawn offscreen, all of it), so the bin window has to be a native one
	 * for X to clip their input to it (GDK shapes it to the part that shows). */
	gdk_window_ensure_native (bin);

	/* The shield: an InputOnly window over the faces' windows, so in Patch mode the
	 * canvas gets every pointer event (as from its bin window: it is as big, at 0,0).
	 * Native, so X stacks it over them. */
	GdkWindowAttr a;
	memset (&a, 0, sizeof (a));
	a.window_type = GDK_WINDOW_CHILD;
	a.wclass      = GDK_INPUT_ONLY;
	a.x           = 0;
	a.y           = 0;
	gdk_drawable_get_size (bin, &a.width, &a.height);
	a.event_mask = gtk_widget_get_events (GTK_WIDGET (gobj ())) | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK
	               | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_SCROLL_MASK;
	_shield = gdk_window_new (bin, &a, GDK_WA_X | GDK_WA_Y);
	gdk_window_set_user_data (_shield, gobj ());
	gdk_window_ensure_native (_shield);
	show_shield ();
#endif
}

void
McrCanvas::on_unrealize ()
{
	/* The faces' GUIs go first: a live X11 GUI must never be unrealized under them. They
	 * are made again once the canvas shows again (the MCR is detached, attached or shown) */
	cancel_drag ();
	_top_map.disconnect ();
	_top_unmap.disconnect ();
	_top_state.disconnect ();
	_hscroll.disconnect ();
	_vscroll.disconnect ();
	_shown_idle.disconnect ();
	_face_idle.disconnect ();
	_reveal_timer.disconnect ();
	size_t const n = _faces.size ();
	free_faces ();
	_shown     = false;
	_turned_to = false;
	if (n) {
		spite_cue_log (string_compose ("mcr: canvas unrealized, %1 face(s) freed", n));
		faces_free (); /* a view on screen may make the ones it shows */
	}
	if (_shield) {
		gdk_window_set_user_data (_shield, 0);
		gdk_window_destroy (_shield);
		_shield = 0;
	}
	_composite = false;
	Gtk::Layout::on_unrealize ();
}

void
McrCanvas::on_map ()
{
	Gtk::Layout::on_map ();
	/* On screen once the windows around it show too, which a container with a window of
	 * its own (Tabbable's EventBox) does after mapping its children: looked at once the
	 * page is mapped */
	_shown_idle.disconnect ();
	_shown_idle = Glib::signal_idle ().connect (sigc::mem_fun (*this, &McrCanvas::idle_shown), Glib::PRIORITY_HIGH_IDLE);
}

bool
McrCanvas::idle_shown ()
{
	if (showing ()) {
		shown ();
	}
	return false;
}

bool
McrCanvas::showing () const
{
	return get_mapped () && spite_window_on_screen (get_bin_window ());
}

/* Shown, and still on screen. The faces' windows are only named (face_surface), and the
 * faces only made, taken and revealed, while it is: a minimized window is unmapped by
 * the window manager, but GDK still calls the windows in it viewable, and naming a
 * window X doesn't show fails. */
bool
McrCanvas::on_screen () const
{
	return _shown && showing ();
}

/* the device's face is live here, or will be (see idle_faces) */
bool
McrCanvas::face_shown (PBD::ID const& id) const
{
	if (!_composite || !on_screen ()) {
		return false;
	}
	if (face_of (id)) {
		return true;
	}
	if (_face_failed.count (id)) {
		return false; /* not made again */
	}
	for (auto const& row : _rows) {
		for (auto const& d : row.devices) {
			if (d.id == id) {
				return d.card.live;
			}
		}
	}
	return false;
}

bool
McrCanvas::toplevel_mapped (GdkEventAny*)
{
	if (showing ()) {
		shown ();
	}
	return false;
}

bool
McrCanvas::toplevel_unmapped (GdkEventAny*)
{
	hidden ();
	return false;
}

/* minimized, or back: a window manager that keeps a minimized window mapped says it is
 * hidden (one that unmaps it says so too) */
bool
McrCanvas::toplevel_state (GdkEventWindowState* ev)
{
	if (ev->changed_mask & GDK_WINDOW_STATE_ICONIFIED) {
		if (ev->new_window_state & GDK_WINDOW_STATE_ICONIFIED) {
			hidden ();
		} else if (showing ()) {
			shown ();
		}
	}
	return false;
}

void
McrCanvas::shown ()
{
	if (_shown) {
		return;
	}
	_shown = true;
	spite_cue_log (string_compose ("mcr: shown, %1 live face(s)", _faces.size ()));

	/* A face that was resized while the canvas was hidden (the Device view zoomed them
	 * all) would draw itself wrongly (PgD4S only applies a new size when it draws, and
	 * it doesn't draw what can't be seen): it is made again. */
	std::vector<PBD::ID> resized;
	for (auto const& i : _faces) {
		Gtk::Requisition const r = i.second->ui->size_request ();
		if (i.second->hidden_w > 0 && (r.width != i.second->hidden_w || r.height != i.second->hidden_h)) {
			resized.push_back (i.first);
		}
	}
	for (auto const& id : resized) {
		spite_cue_log ("mcr: a face resized while hidden is made again");
		free_face (id);
	}
	for (auto const& i : _faces) {
		if (i.second->ui->embed_active () && i.second->ui->get_visible ()) {
			i.second->ui->embed_show (); /* updates resume */
		}
	}
	/* faces moved here that didn't show yet */
	_reveal_timer.disconnect ();
	_reveal_timer = Glib::signal_timeout ().connect (sigc::mem_fun (*this, &McrCanvas::reveal_faces), SpiteFaceHolder::settle_ms);
	show_shield ();
	refresh_pictures ();
	layout (); /* the faces' height may have changed meanwhile */
	queue_draw ();
	/* the faces the Device view has come over (the user turned to the MCR: from a view on
	 * screen too), then the rest are made */
	_turned_to = true;
	queue_faces ();
}

void
McrCanvas::on_unmap ()
{
	_shown_idle.disconnect ();
	hidden (); /* a cable being dragged too */
	Gtk::Layout::on_unmap ();
}

void
McrCanvas::hidden ()
{
	/* a drag's release would go elsewhere */
	cancel_drag ();
	if (!_shown) {
		return;
	}
	_shown     = false;
	_turned_to = false;
	_face_idle.disconnect ();
	_reveal_timer.disconnect (); /* shown () starts it again */
	spite_cue_log (string_compose ("mcr: hidden, %1 live face(s) kept", _faces.size ()));
	for (auto const& i : _faces) {
		/* its size now, to tell whether it changes while hidden (see shown) */
		Gtk::Requisition const r = i.second->ui->size_request ();
		i.second->hidden_w       = r.width;
		i.second->hidden_h       = r.height;
		/* a window gets new storage each time it is mapped: named again then */
		drop_face_surface (*i.second);
		if (i.second->ui->embed_active ()) {
			i.second->ui->stop_updating (0); /* nothing shows it */
		}
	}
	if (!_faces.empty ()) {
		faces_free (); /* a view on screen may take the ones it shows */
	}
}

void
McrCanvas::on_size_allocate (Gtk::Allocation& a)
{
	Gtk::Layout::on_size_allocate (a);
	size_shield (); /* the bin window may have grown */
	view_changed ();
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

	prune_faces ();
	layout ();
	_patch.jacks_changed (); /* their pins may have changed */

	if (_width != width) {
		queue_draw ();
	} else {
		/* rows keep their height: only theirs is redrawn */
		for (auto const& rect : changed) {
			invalidate (rect);
		}
	}
	queue_faces ();
	return false;
}

/* where everything goes: rows stacked down the canvas, each its header, then its
 * devices left to right, all the Device view's height; between two rows a sliver, as
 * tall as the patching layer wants it (closed, or open for its cables). The layer gets
 * the rows and devices then; when that changes the slivers, all of it is done again. */
void
McrCanvas::layout ()
{
	if (_in_layout) {
		_relayout = true; /* once this one is done */
		return;
	}
	_in_layout = true;
	for (int n = 0; n < 4; ++n) {
		_relayout = false;
		place ();
		update_model (); /* may want other slivers (a new cable opens them in Patch mode) */
		if (!_relayout) {
			break;
		}
	}
	_relayout  = false;
	_in_layout = false;
}

void
McrCanvas::place ()
{
	double const pad  = margin ();
	double const hw   = header_width ();
	double const gap  = device_gap ();
	double const rpad = row_padding ();
	double const th   = title_height ();
	double const fh   = face_height ();
	double const cw   = DefaultFace::width (fh, th);

	double y     = pad;
	double right = pad + hw;

	for (size_t i = 0; i < _rows.size (); ++i) {
		McrRow&      row  = _rows[i];
		double const top  = y + rpad;
		double       body = fh; /* the row's faces' height: the tallest's */

		double x = pad + hw + gap;
		for (auto& d : row.devices) {
			/* a live face is as big as its GUI (painted 1:1, as it gets its input where
			 * it is), and until it has one, as its picture, as in the Device view */
			McrFace* const f = face_of (d.id);
			double         w = cw;
			double         h = fh;
			if (f) {
				Gtk::Requisition const r = f->ui->size_request ();
				w = std::max<double> (r.width, 2 * th);
				h = std::max<double> (r.height, th);
			} else if (d.picture) {
				w = std::max<double> (d.picture->get_width (), 2 * th);
			}
			body   = std::max (body, h);
			d.rect = Rect (x, top, x + w, top + th + h);
			if (f && (f->x != (int) x || f->y != (int) (top + th))) {
				/* under its title strip (moved only when it moves: each move reallocates it) */
				f->x = (int) x;
				f->y = (int) (top + th);
				move (*f->ui, f->x, f->y);
			}
			x += w + gap;
		}
		right = std::max (right, x - gap);

		row.header = Rect (pad, top, pad + hw, top + th + body);
		row.rect   = Rect (pad, y, pad, top + th + body + rpad);
		y          = row.rect.y1;

		if (i + 1 < _rows.size ()) {
			double const sl = std::max (1., _patch.sliver_height_below (McrPatchCables::key (row.id)));
			row.sliver      = Rect (pad, y, pad, y + sl);
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
	size_shield ();
}

/* the rows and devices, as the patching layer knows them */
void
McrCanvas::update_model ()
{
	McrPatchModel m;
	m.scale = ui_scale ();

	for (size_t i = 0; i < _rows.size (); ++i) {
		McrRow const& row = _rows[i];
		McrPatchRow   r;
		r.id     = McrPatchCables::key (row.id);
		r.number = row.number;
		r.name   = row.name;
		r.rect   = PdCord::Rect (row.rect.x0, row.rect.y0, row.rect.x1, row.rect.y1);
		r.header = PdCord::Rect (row.header.x0, row.header.y0, row.header.x1, row.header.y1);
		m.rows.push_back (r);

		for (auto const& d : row.devices) {
			McrPatchDevice pd;
			pd.id     = McrPatchCables::key (d.id);
			pd.row    = i;
			pd.insert = d.insert.lock ();
			pd.name   = d.card.name;
			pd.rect   = PdCord::Rect (d.rect.x0, d.rect.y0, d.rect.x1, d.rect.y1);
			m.devices.push_back (pd);
		}
	}

	view_changed ();
	_patch.set_model (m);
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
				if (McrFace* f = face_of (d.id)) {
					/* its window is opening: one GUI per plugin, so the face's goes, and
					 * its picture (as it was just now) stands in, washed out */
					keep_picture (d.id, *f);
					free_face (d.id);
					take_picture (d);
					queue_layout ();
				}
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
	queue_faces (); /* its face again, if the MCR shows */
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
McrCanvas::is_canvas_window (GdkWindow* w) const
{
	if (!w) {
		return false;
	}
	if (w == _shield) {
		return true;
	}
	Glib::RefPtr<Gdk::Window const> bin = get_bin_window ();
	return bin && w == bin->gobj ();
}

bool
McrCanvas::on_motion_notify_event (GdkEventMotion* ev)
{
	if (is_canvas_window (ev->window)) {
		bool const taken = _patch.motion (ev->x, ev->y, ev->state);
		set_patch_cursor ();
		if (taken) {
			autoscroll_check (ev->x, ev->y, ev->state);
			return true; /* a cable or a sliver's edge is being dragged */
		}
		Hit const h = hit (ev->x, ev->y);
		set_hover (h.row, h.device);
	}
	return Gtk::Layout::on_motion_notify_event (ev);
}

bool
McrCanvas::on_button_release_event (GdkEventButton* ev)
{
	if (is_canvas_window (ev->window) && _patch.button_release (ev->x, ev->y, ev->button, ev->state)) {
		set_patch_cursor ();
		return true;
	}
	return Gtk::Layout::on_button_release_event (ev);
}

/* a cable's own gain, in Patch mode; else the canvas scrolls */
bool
McrCanvas::on_scroll_event (GdkEventScroll* ev)
{
	if (is_canvas_window (ev->window) && (ev->direction == GDK_SCROLL_UP || ev->direction == GDK_SCROLL_DOWN)) {
		if (_patch.scroll (ev->x, ev->y, ev->direction == GDK_SCROLL_UP ? -1 : 1, ev->state)) {
			return true;
		}
	}
	return Gtk::Layout::on_scroll_event (ev);
}

/* Delete and BackSpace take the selected cable (Escape comes from MCRPage, see escape ()) */
bool
McrCanvas::on_key_press_event (GdkEventKey* ev)
{
	if (_patch.key_press (ev->keyval, ev->state)) {
		set_patch_cursor ();
		return true;
	}
	return Gtk::Layout::on_key_press_event (ev);
}

/* Escape, from the MCR's own binding (MCRPage): ARDOUR_UI's global one comes before the
 * canvas gets the key */
bool
McrCanvas::escape ()
{
	bool const taken = _patch.escape ();
	if (taken) {
		_autoscroll.disconnect ();
		set_patch_cursor ();
	}
	return taken;
}

/* a cable (or a sliver's edge) being dragged stops where it is: its release goes
 * elsewhere (the canvas went, or another window took the pointer) */
void
McrCanvas::cancel_drag ()
{
	_autoscroll.disconnect ();
	_patch.cancel ();
	set_patch_cursor ();
}

bool
McrCanvas::grab_broken (GdkEventGrabBroken*)
{
	cancel_drag ();
	return false;
}

void
McrCanvas::grab_notify (bool was_grabbed)
{
	if (!was_grabbed) {
		cancel_drag (); /* shadowed by another widget's grab */
	}
}

/* the pointer comes in (from a face, too) or goes out: a face that took the keyboard
 * when it was played gives it back */
bool
McrCanvas::on_enter_notify_event (GdkEventCrossing* ev)
{
	Spite::reclaim_keyboard (*this);
	return Gtk::Layout::on_enter_notify_event (ev);
}

bool
McrCanvas::on_leave_notify_event (GdkEventCrossing* ev)
{
	if (is_canvas_window (ev->window)) {
		/* out of the canvas, or into a live face (Play mode) */
		_patch.leave ();
		set_patch_cursor ();
	}
	set_hover (-1, -1);
	Spite::reclaim_keyboard (*this);
	return Gtk::Layout::on_leave_notify_event (ev);
}

bool
McrCanvas::on_button_press_event (GdkEventButton* ev)
{
	if (!is_canvas_window (ev->window)) {
		return Gtk::Layout::on_button_press_event (ev);
	}
	Spite::reclaim_keyboard (*this);
	if (!has_focus ()) {
		grab_focus (); /* for Delete */
	}

	/* the patching layer first: jacks, cables, slivers */
	if (ev->type == GDK_BUTTON_PRESS || ev->type == GDK_2BUTTON_PRESS) {
		bool const taken = _patch.button_press (ev->x, ev->y, ev->button, ev->type == GDK_2BUTTON_PRESS ? 2 : 1, ev->state);
		set_patch_cursor ();
		if (taken) {
			return true;
		}
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

	/* everything, the live faces too (their windows are composited: GDK leaves them out
	 * of the bin window's clip, and a face's damage exposes the bin window there) */
	cairo_t* cr = gdk_cairo_create (ev->window);
	gdk_cairo_region (cr, ev->region);
	cairo_clip (cr);
	paint (cr, Rect (ev->area.x, ev->area.y, ev->area.x + ev->area.width, ev->area.y + ev->area.height));
	cairo_destroy (cr);

	/* then its other children, if it had any */
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

	/* what goes behind the devices and their faces */
	cairo_save (cr);
	PaintBehindFaces (cr, area);
	cairo_restore (cr);

	/* the tracks' names */
	for (auto const& row : _rows) {
		if (!row.header.intersection (area).empty ()) {
			paint_header (cr, row);
		}
	}

	PdCord::Rect const pa (area.x0, area.y0, area.x1, area.y1);

	/* the slivers, and in Play mode the cables and the jacks, behind the devices (as in
	 * plugdata's run mode) */
	cairo_save (cr);
	_patch.draw_under_faces (cr, pa);
	cairo_restore (cr);

	for (size_t i = 0; i < _rows.size (); ++i) {
		McrRow const& row  = _rows[i];
		Rect const    band = row.sliver.empty () ? row.rect : row.rect.extend (row.sliver);
		if (band.expand (row_padding ()).intersection (area).empty ()) {
			continue;
		}
		paint_row (cr, row, i, area);
	}

	/* over everything, live faces too: Patch mode's cables and jacks, the cable being
	 * dragged, Detective Mode */
	cairo_save (cr);
	_patch.draw_over_faces (cr, pa);
	cairo_restore (cr);

	cairo_save (cr);
	PaintOverFaces (cr, area);
	cairo_restore (cr);
}

void
McrCanvas::paint_row (cairo_t* cr, McrRow const& row, int r, Rect const& area)
{
	Glib::RefPtr<Pango::Context> ctx = get_pango_context ();
	double const                 th  = title_height ();

	for (size_t i = 0; i < row.devices.size (); ++i) {
		McrDevice const& d = row.devices[i];
		if (device_area (d).intersection (area).empty ()) {
			continue;
		}
		bool const hovered = (r == _hover_row && (int) i == _hover_device);
		d.card.paint (cr, ctx, d.rect.x0, d.rect.y0, d.rect.width (), d.rect.height (), th, hovered);
		if (!paint_face (cr, d)) {
			paint_picture (cr, d); /* until its face is live */
		}
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

/* ****************************************************************************
 * The patching layer
 */

/* the slivers changed height: the rows move */
void
McrCanvas::patch_relayout ()
{
	layout (); /* once the current one is done, if one is going on */
	queue_draw ();
}

void
McrCanvas::patch_redraw (PdCord::Rect r)
{
	invalidate (Rect (r.x0, r.y0, r.x1, r.y1));
}

/* a folded cable's far end was clicked: it comes into view */
void
McrCanvas::patch_reveal (PdCord::Rect r)
{
	Gtk::Adjustment* const h = get_hadjustment ();
	Gtk::Adjustment* const v = get_vadjustment ();
	if (h) {
		h->clamp_page (std::max (0., r.x0), std::min (_width, r.x1));
	}
	if (v) {
		v->clamp_page (std::max (0., r.y0), std::min (_height, r.y1));
	}
}

/* what shows of the canvas: the layer keeps its bubbles in it */
void
McrCanvas::view_changed ()
{
	Gtk::Adjustment* const h = get_hadjustment ();
	Gtk::Adjustment* const v = get_vadjustment ();
	if (!h || !v) {
		return;
	}
	_patch.set_visible (PdCord::Rect (h->get_value (), v->get_value (), h->get_value () + h->get_page_size (), v->get_value () + v->get_page_size ()));
}

/* a jack: a hand; an open sliver's bottom edge: up and down */
void
McrCanvas::set_patch_cursor ()
{
	int const c = (int) _patch.cursor ();
	if (c == _cursor || !get_realized ()) {
		return;
	}
	_cursor = c;

	GdkCursor* cursor = 0;
	if (c == McrPatch::Hand) {
		cursor = gdk_cursor_new (GDK_HAND2);
	} else if (c == McrPatch::Resize) {
		cursor = gdk_cursor_new (GDK_SB_V_DOUBLE_ARROW);
	}
	gdk_window_set_cursor (get_bin_window ()->gobj (), cursor);
	if (_shield) {
		gdk_window_set_cursor (_shield, cursor);
	}
	if (cursor) {
		gdk_cursor_unref (cursor);
	}
}

/* how near an edge of the view (pixels) a dragged cable scrolls the canvas */
static double
autoscroll_margin ()
{
	return rint (40 * ui_scale ());
}

void
McrCanvas::autoscroll_check (double x, double y, unsigned state)
{
	_drag_x     = x;
	_drag_y     = y;
	_drag_state = state;
	if (!_patch.dragging () || _autoscroll.connected ()) {
		return;
	}
	if (autoscroll ()) {
		_autoscroll = Glib::signal_timeout ().connect (sigc::mem_fun (*this, &McrCanvas::autoscroll), 30);
	}
}

/* while the pointer stays near an edge, the canvas scrolls under it (faster the further
 * out it is) and the dragged cable follows: rows out of view can take it */
bool
McrCanvas::autoscroll ()
{
	Gtk::Adjustment* const h = get_hadjustment ();
	Gtk::Adjustment* const v = get_vadjustment ();
	if (!_patch.dragging () || !h || !v) {
		return false;
	}

	double const m     = autoscroll_margin ();
	auto         speed = [m] (double out) {
		return out <= 0 ? 0. : std::min (40., 4 + 24 * out / m);
	};

	double dx = 0;
	double dy = 0;
	dx -= speed (h->get_value () + m - _drag_x);
	dx += speed (_drag_x - (h->get_value () + h->get_page_size () - m));
	dy -= speed (v->get_value () + m - _drag_y);
	dy += speed (_drag_y - (v->get_value () + v->get_page_size () - m));

	double const nx = std::max (h->get_lower (), std::min (h->get_upper () - h->get_page_size (), h->get_value () + dx));
	double const ny = std::max (v->get_lower (), std::min (v->get_upper () - v->get_page_size (), v->get_value () + dy));
	double const mx = nx - h->get_value ();
	double const my = ny - v->get_value ();
	if (fabs (mx) < .5 && fabs (my) < .5) {
		return false; /* not near an edge, or at the end */
	}

	h->set_value (nx);
	v->set_value (ny);
	/* the pointer stays where it is on screen: over another part of the canvas */
	_drag_x += mx;
	_drag_y += my;
	_patch.motion (_drag_x, _drag_y, _drag_state);
	return true;
}

std::vector<uint64_t>
McrCanvas::selected_rows () const
{
	std::vector<uint64_t> rv;
	if (!_session) {
		return rv;
	}
	for (auto const& row : _rows) {
		std::shared_ptr<Route> r = row.route.lock ();
		if (r && _session->selection ().selected (r)) {
			rv.push_back (McrPatchCables::key (row.id));
		}
	}
	return rv;
}

/* ****************************************************************************
 * Live faces
 */

McrFace*
McrCanvas::face_of (PBD::ID const& id) const
{
	auto const i = _faces.find (id);
	return i == _faces.end () ? 0 : i->second;
}

bool
McrCanvas::face_is_live (McrDevice const& d) const
{
	McrFace const* f = face_of (d.id);
	return f && f->ui->embed_active ();
}

/* a live face, from its window's pixmap, exactly where its window is (input goes
 * there): false if it can't be painted yet */
bool
McrCanvas::paint_face (cairo_t* cr, McrDevice const& d)
{
#ifdef MCR_LIVE_FACES
	McrFace* const f = face_of (d.id);
	if (!f || !f->window) {
		return false;
	}
	cairo_surface_t* const s = face_surface (*f);
	if (!s) {
		return false;
	}
	int x = 0;
	int y = 0;
	gdk_window_get_position (f->window, &x, &y);

	cairo_save (cr);
	cairo_rectangle (cr, x, y, f->width, f->height);
	cairo_clip (cr);
	cairo_set_source_surface (cr, s, x, y);
	cairo_paint (cr);
	if (!d.card.on) {
		/* an off device's face, as its picture: at half strength */
		Gtkmm2ext::set_source_rgb_a (cr, card_bg, .5);
		cairo_paint (cr);
	}
	if (!_play) {
		/* Patch mode: it can't be played (the shield is over it), the cables come first */
		Gtkmm2ext::set_source_rgb_a (cr, canvas_bg, .22);
		cairo_paint (cr);
	}
	cairo_restore (cr);
	return true;
#else
	return false;
#endif
}

/* SPITE_MCR_DEBUG: a red cross over each live face, through PaintOverFaces */
void
McrCanvas::paint_debug (cairo_t* cr, Rect const&)
{
	double const th = title_height ();
	cairo_set_line_width (cr, 3 * ui_scale ());
	cairo_set_source_rgba (cr, 1, .15, .1, .9);
	for (auto const& row : _rows) {
		for (auto const& d : row.devices) {
			if (!face_is_live (d)) {
				continue;
			}
			cairo_move_to (cr, d.rect.x0, d.rect.y0 + th);
			cairo_line_to (cr, d.rect.x1, d.rect.y1);
			cairo_move_to (cr, d.rect.x1, d.rect.y0 + th);
			cairo_line_to (cr, d.rect.x0, d.rect.y1);
		}
	}
	cairo_stroke (cr);
}

/* the faces still to come in: soon, but after what's pending (an idle callback each) */
void
McrCanvas::queue_faces ()
{
	if (!_composite || !_session || !on_screen ()) {
		return;
	}
	if (!_face_idle.connected ()) {
		_face_idle = Glib::signal_idle ().connect (sigc::mem_fun (*this, &McrCanvas::idle_faces));
	}
}

static double
rect_distance (Rect const& a, Rect const& b)
{
	double const dx = std::max (0., std::max (b.x0 - a.x1, a.x0 - b.x1));
	double const dy = std::max (0., std::max (b.y0 - a.y1, a.y0 - b.y1));
	return hypot (dx, dy);
}

/* The faces the Device view has are taken at once (moved here); then one face is made
 * per call, nearest to what the canvas shows first, as making one takes a moment
 * (0.3-0.5 s). Its picture shows until it is there. */
bool
McrCanvas::idle_faces ()
{
	if (!_composite || !_session || !on_screen ()) {
		return false;
	}

	take_faces ();

	Gtk::Adjustment* const h = get_hadjustment ();
	Gtk::Adjustment* const v = get_vadjustment ();
	Rect const view (h->get_value (), v->get_value (), h->get_value () + h->get_page_size (), v->get_value () + v->get_page_size ());

	McrDevice* next    = 0;
	double     nearest = 0;
	for (auto& row : _rows) {
		for (auto& d : row.devices) {
			if (!d.card.live || face_of (d.id) || _face_failed.count (d.id) || SpiteFaceHolder::holder (d.id) || leave_to_last (d.id)) {
				continue; /* made, failed, or another view on screen has it or makes it */
			}
			std::shared_ptr<PluginInsert> pi = d.insert.lock ();
			if (!pi || PluginUIWindow::custom_gui_shown (pi.get ())) {
				continue; /* in its window: its picture, washed out */
			}
			double const dist = rect_distance (d.rect, view);
			if (!next || dist < nearest) {
				next    = &d;
				nearest = dist;
			}
		}
	}
	if (!next) {
		return false;
	}
	make_face (*next);
	return true;
}

/* the Device view's live GUIs of the devices here, moved over (no making): the lease */
bool
McrCanvas::take_faces ()
{
	if (!on_screen ()) {
		return false;
	}
	gint64 const t0    = g_get_monotonic_time ();
	size_t       taken = 0;

	for (auto& row : _rows) {
		for (auto& d : row.devices) {
			if (!d.card.live || face_of (d.id)) {
				continue;
			}
			SpiteFaceHolder* const h = SpiteFaceHolder::holder (d.id);
			if (!h || h == this) {
				continue;
			}
			std::shared_ptr<PluginInsert> pi = d.insert.lock ();
			if (!pi || PluginUIWindow::custom_gui_shown (pi.get ())) {
				continue;
			}
			if (LV2PluginUI* ui = take_face (d.id, *this, _turned_to)) {
				adopt_face (d.id, ui, false);
				++taken;
			}
		}
	}
	_turned_to = false; /* from now on, only faces no view on screen shows */

	if (taken) {
		layout ();
		queue_draw ();
		spite_cue_log (string_compose ("mcr: took %1 face(s) from the Device view in %2 ms", taken, (g_get_monotonic_time () - t0) / 1000));
		/* they come hidden: they stay so a moment longer, their pictures showing, until
		 * PgD4S has seen them hidden and draws all of them again */
		_reveal_timer.disconnect ();
		_reveal_timer = Glib::signal_timeout ().connect (sigc::mem_fun (*this, &McrCanvas::reveal_faces), SpiteFaceHolder::settle_ms);
	}
	return taken > 0;
}

/* the faces taken from the Device view show (see take_faces) */
bool
McrCanvas::reveal_faces ()
{
	if (!on_screen ()) {
		return false; /* when the canvas shows again (shown () starts this again) */
	}
	gint64 const t0    = g_get_monotonic_time ();
	size_t       shown = 0;
	for (auto const& i : _faces) {
		LV2PluginUI* const ui = i.second->ui;
		if (!ui->get_visible () && ui->embed_active ()) {
			ui->show ();
			ui->embed_show (); /* updates resume */
			++shown;
		}
	}
	if (shown) {
		if (_shield && gdk_window_is_visible (_shield)) {
			gdk_window_raise (_shield);
		}
		spite_cue_log (string_compose ("mcr: %1 moved face(s) shown in %2 ms", shown, (g_get_monotonic_time () - t0) / 1000));
	}
	return false;
}

void
McrCanvas::make_face (McrDevice& d)
{
	std::shared_ptr<PluginInsert> pi  = d.insert.lock ();
	std::shared_ptr<LV2Plugin>    lv2 = pi ? std::dynamic_pointer_cast<LV2Plugin> (pi->plugin ()) : std::shared_ptr<LV2Plugin> ();
	if (!lv2) {
		_face_failed.insert (d.id);
		faces_free (); /* see below */
		return;
	}

	/* made as the Device view makes its faces: embedded (no preset bar, no keyboard, its
	 * own options), shown before the GUI is made (PgD4S's OpenGL wants a shown window) */
	LV2PluginUI* const ui = new LV2PluginUI (pi, lv2);
	ui->set_embedded (face_height () / 169.f); /* a Max for Live device's face is 169 px tall */
	put (*ui, (int) d.rect.x0, (int) (d.rect.y0 + title_height ()));
	ui->show ();

	if (spite_lv2_ui_face_height () <= 0) {
		ARDOUR::set_lv2_ui_face_height (face_height ()); /* the Device view didn't yet */
	}
	gint64 const t0 = g_get_monotonic_time ();
	ui->embed_show ();
	spite_cue_log (string_compose ("mcr: face %1: GUI made in %2 ms", d.card.name, (g_get_monotonic_time () - t0) / 1000));

	if (!ui->embed_active ()) {
		_face_failed.insert (d.id);
		remove (*ui);
		delete ui;
		/* a view that left the face to the MCR (which held it last) makes it now */
		faces_free ();
		return;
	}
	hold_face (d.id);
	adopt_face (d.id, ui, true);
	layout (); /* as big as its GUI */
	queue_draw ();
}

/* a GUI, made here or moved here, becomes a live face: its window composited, the
 * shield over it (a moved one shows later, see take_faces) */
void
McrCanvas::adopt_face (PBD::ID const& id, LV2PluginUI* ui, bool show)
{
	McrFace* const f = new McrFace;
	f->ui            = ui;
	_faces[id]       = f;
	composite_face (*f);
	f->resized = ui->signal_size_allocate ().connect (sigc::hide (sigc::bind (sigc::mem_fun (*this, &McrCanvas::face_resized), id)));
	if (show) {
		ui->show ();
		if (_shield && gdk_window_is_visible (_shield)) {
			gdk_window_raise (_shield);
		}
	}
}

/* the Device view takes a face (it shows the device now): its picture stands in here */
LV2PluginUI*
McrCanvas::lend_face (PBD::ID const& id)
{
	auto const i = _faces.find (id);
	if (i == _faces.end ()) {
		return 0;
	}
	McrFace* const     f  = i->second;
	LV2PluginUI* const ui = f->ui;
	/* its look now stands in for it (the Device view keeps a picture of the faces it
	 * lends the same way), if the canvas shows it */
	keep_picture (id, *f);
	_faces.erase (i);
	uncomposite_face (*f);
	delete f;
	ui->hide ();
	for (auto& row : _rows) {
		for (auto& d : row.devices) {
			if (d.id == id) {
				take_picture (d);
			}
		}
	}
	queue_layout (); /* as wide as its picture */
	return ui;
}

/* its GUI goes (its device went, its window opened, the canvas goes) */
void
McrCanvas::free_face (PBD::ID const& id)
{
	auto const i = _faces.find (id);
	if (i == _faces.end ()) {
		return;
	}
	McrFace* const     f  = i->second;
	LV2PluginUI* const ui = f->ui;
	_faces.erase (i);
	uncomposite_face (*f);
	delete f;
	if (ui->embed_active ()) {
		ui->embed_hide (); /* before it leaves its container */
	}
	remove (*ui);
	delete ui;
	drop_face (id);
}

void
McrCanvas::free_faces ()
{
	while (!_faces.empty ()) {
		free_face (_faces.begin ()->first);
	}
}

/* the faces whose devices are no longer here (or no longer have a face) */
void
McrCanvas::prune_faces ()
{
	std::set<PBD::ID> keep;
	for (auto const& row : _rows) {
		for (auto const& d : row.devices) {
			if (d.card.live) {
				keep.insert (d.id);
			}
		}
	}
	std::vector<PBD::ID> gone;
	for (auto const& i : _faces) {
		if (!keep.count (i.first)) {
			gone.push_back (i.first);
		}
	}
	for (auto const& id : gone) {
		free_face (id);
	}
}

void
McrCanvas::composite_face (McrFace& f)
{
#ifdef MCR_LIVE_FACES
	GtkWidget* const s = f.ui->embed_socket ();
	f.window           = s ? gtk_widget_get_window (s) : 0;
	if (!f.window) {
		return;
	}
	/* Drawn offscreen now (X redirects it, its children too), and painted by the canvas;
	 * each change damages it, which exposes the canvas there. With ydk's composite
	 * support it isn't shaped to the part that shows, so all of it stays current. */
	gdk_window_set_composited (f.window, TRUE);
	gdk_window_add_filter (f.window, &McrCanvas::face_filter, this);
#endif
}

void
McrCanvas::uncomposite_face (McrFace& f)
{
	f.resized.disconnect ();
	drop_face_surface (f);
#ifdef MCR_LIVE_FACES
	if (f.window) {
		gdk_window_remove_filter (f.window, &McrCanvas::face_filter, this);
		gdk_window_set_composited (f.window, FALSE);
		f.window = 0;
	}
#endif
}

/* a face's window got new storage (it is mapped again): its pixmap is named again */
GdkFilterReturn
McrCanvas::face_filter (GdkXEvent* gxev, GdkEvent*, gpointer self)
{
#ifdef MCR_LIVE_FACES
	XEvent const* xev = (XEvent const*) gxev;
	if (xev->type == MapNotify) {
		McrCanvas* const c = static_cast<McrCanvas*> (self);
		for (auto const& i : c->_faces) {
			if (i.second->window && GDK_WINDOW_XID (i.second->window) == xev->xmap.window) {
				c->drop_face_surface (*i.second);
			}
		}
	}
#endif
	return GDK_FILTER_CONTINUE;
}

/* what the face's window shows, whole (scrolled out of view or not): a cairo surface
 * on its pixmap, named again after the window is mapped or resized. Only while the
 * canvas is shown (see on_screen ()): X fails to name a window it doesn't show. */
cairo_surface_t*
McrCanvas::face_surface (McrFace& f)
{
#ifdef MCR_LIVE_FACES
	if (!_shown || !f.window || !gdk_window_is_viewable (f.window)) {
		drop_face_surface (f);
		return 0;
	}
	int w = 0;
	int h = 0;
	gdk_drawable_get_size (f.window, &w, &h);
	if (f.surface && w == f.width && h == f.height) {
		return f.surface;
	}
	drop_face_surface (f);

	/* The window manager may have unmapped its window just now (minimized it), before
	 * Spite heard of it: X then fails, and the error comes back only once X has done the
	 * request, so it is waited for (a round trip, only when a face is named again). An
	 * error that came after the trap would end Spite. */
	::Display* const dpy = GDK_WINDOW_XDISPLAY (f.window);
	gdk_error_trap_push ();
	Pixmap const pm = XCompositeNameWindowPixmap (dpy, GDK_WINDOW_XID (f.window));
	gdk_flush ();
	if (gdk_error_trap_pop ()) {
		return 0; /* not viewable after all: its picture shows */
	}
	f.pixmap  = pm;
	f.surface = cairo_xlib_surface_create (dpy, pm, GDK_VISUAL_XVISUAL (gdk_drawable_get_visual (f.window)), w, h);
	f.width   = w;
	f.height  = h;
	return f.surface;
#else
	return 0;
#endif
}

void
McrCanvas::drop_face_surface (McrFace& f)
{
	if (f.surface) {
		cairo_surface_destroy (f.surface);
		f.surface = 0;
	}
#ifdef MCR_LIVE_FACES
	if (f.pixmap) {
		gdk_error_trap_push ();
		XFreePixmap (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), f.pixmap);
		gdk_error_trap_pop ();
		f.pixmap = 0;
	}
#endif
	f.width  = 0;
	f.height = 0;
}

/* the face's latest look (all of it, from its pixmap), kept as its picture */
void
McrCanvas::keep_picture (PBD::ID const& id, McrFace& f)
{
#ifdef MCR_LIVE_FACES
	if (!face_surface (f)) {
		return;
	}
	GdkPixmap* const pm = gdk_pixmap_foreign_new_for_display (gdk_drawable_get_display (f.window), f.pixmap);
	if (!pm) {
		return;
	}
	gdk_error_trap_push ();
	GdkPixbuf* const pb = gdk_pixbuf_get_from_drawable (NULL, pm, gdk_drawable_get_colormap (f.window), 0, 0, 0, 0, f.width, f.height);
	gdk_error_trap_pop ();
	g_object_unref (pm);
	if (pb) {
		spite_keep_face_picture (id, Glib::wrap (pb));
	}
#endif
}

/* PgD4S resized its face (it follows the face height): the devices after it move */
void
McrCanvas::face_resized (PBD::ID id)
{
	McrFace* const f = face_of (id);
	if (!f) {
		return;
	}
	Gtk::Allocation const a  = f->ui->get_allocation ();
	double const          th = title_height ();
	for (auto const& row : _rows) {
		for (auto const& d : row.devices) {
			if (d.id == id && (fabs (std::max<double> (a.get_width (), 2 * th) - d.rect.width ()) > .5
			                   || fabs (std::max<double> (a.get_height (), th) + th - d.rect.height ()) > .5)) {
				queue_layout ();
				return;
			}
		}
	}
}

void
McrCanvas::queue_layout ()
{
	if (!_layout_idle.connected ()) {
		_layout_idle = Glib::signal_idle ().connect (sigc::mem_fun (*this, &McrCanvas::idle_layout));
	}
}

bool
McrCanvas::idle_layout ()
{
	layout ();
	queue_draw ();
	return false;
}

/* the shield is as big as the bin window, which grows with the canvas */
void
McrCanvas::size_shield ()
{
	Glib::RefPtr<Gdk::Window> bin = get_bin_window ();
	if (!_shield || !bin) {
		return;
	}
	int w = 0;
	int h = 0;
	gdk_drawable_get_size (bin->gobj (), &w, &h);
	gdk_window_resize (_shield, std::max (1, w), std::max (1, h));
}

/* over the faces in Patch mode, out of the way in Play mode */
void
McrCanvas::show_shield ()
{
	if (!_shield) {
		return;
	}
	if (_play) {
		gdk_window_hide (_shield);
	} else {
		gdk_window_show (_shield); /* raised over the faces too */
	}
}
