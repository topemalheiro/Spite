/*
 * Copyright (C) 2011-2017 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2024 Ben Loftis <ben@harrisonconsoles.com>
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

#include <cassert>
#include <ytkmm/widget.h>

#include "pbd/compose.h"

#include "ardour/audio_track.h"
#include "ardour/plugin_insert.h"
#include "ardour/port_insert.h"
#include "ardour/route.h"
#include "ardour/session.h"

#include "gtkmm2ext/gui_thread.h"
#include "gtkmm2ext/utils.h"

#include "widgets/tooltips.h"

#include "ardour_ui.h"
#include "mixer_ui.h"
#include "plugin_selector.h"
#include "plugin_ui.h"
#include "port_insert_ui.h"
#include "lv2_plugin_ui.h"
#include "route_properties_box.h"
#include "timers.h"
#include "ui_config.h"

#include "ardour/filesystem_paths.h"
#include "ardour/lv2_plugin.h"
#include "ardour/plugin_insert.h"

#include <fstream>
#include <glibmm/miscutils.h>

#ifdef GDK_WINDOWING_X11
#include <ydk/gdkx.h>
#endif

#include "pbd/i18n.h"

using namespace Gtk;
using namespace ARDOUR;
using namespace ArdourWidgets;

class ProcessorUIFrame : public Gtk::Frame
{
public:
	ProcessorUIFrame (std::shared_ptr<Route>, std::shared_ptr<Processor>, GenericPluginUI*);

private:
	void save_state () const;

	Gtk::HBox    _top;
	Gtk::VBox    _ctrl_box;
	Gtk::Label   _label;
	ArdourButton _collapse_btn;
	ArdourButton _enable_btn;

	std::string                _strip_id;
	std::string                _state_id;
	GenericPluginUI*           _ui;
	std::shared_ptr<Processor> _proc;
	PBD::ScopedConnectionList  _connections;
};

ProcessorUIFrame::ProcessorUIFrame (std::shared_ptr<Route> r, std::shared_ptr<Processor> p, GenericPluginUI* ui)
	: _label (p->display_name ())
	, _collapse_btn (ArdourButton::default_elements, true)
	, _enable_btn (ArdourButton::just_led_default_elements)
	, _strip_id (string_compose ("strip %1", r->id ().to_s ()))
	, _state_id (string_compose ("procuiframe %1", p->id ().to_s ()))
	, _ui (ui)
	, _proc (p)
{
	_label.property_angle () = 90;
	_collapse_btn.set_icon (ArdourIcon::ArrowRight);
	_collapse_btn.set_name ("processor collapse button");
	_collapse_btn.set_tweaks (ArdourButton::Square);
	_enable_btn.set_tweaks (ArdourButton::ExpandtoSquare);

	ui->set_no_show_all ();

	_ctrl_box.pack_start (_collapse_btn, false, false, 4);
	_ctrl_box.pack_start (_label, true, true);
	_ctrl_box.pack_start (_enable_btn, false, false, 7);
	_top.pack_start (_ctrl_box, false, false);
	_top.pack_start (*ui, true, true);
	add (_top);
	show_all ();

	set_tooltip (&_collapse_btn, _("Collapse"));
	set_tooltip (&_enable_btn, _("Bypass"));

	_enable_btn.set_active (_proc->enabled ());
	_collapse_btn.signal_clicked.connect ([&] () {
			bool a = !_collapse_btn.get_active ();
			_ui->set_visible (a);
			save_state ();
			_collapse_btn.set_icon (a ? ArdourIcon::ArrowLeft : ArdourIcon::ArrowRight);
			});
	_enable_btn.signal_clicked.connect ([&] () { _proc->enable (!_proc->enabled ()); });

	p->ActiveChanged.connect (_connections, invalidator (*this), [&] () { _enable_btn.set_active (_proc->enabled ()); }, gui_context ());

	bool            visible = true;
	GUIObjectState& st      = *ARDOUR_UI::instance ()->gui_object_state;
	XMLNode*        strip   = st.get_or_add_node (_strip_id);
	XMLNode*        n       = GUIObjectState::get_node (strip, _state_id);
	if (n) {
		n->get_property (X_("visible"), visible);
	}
	_collapse_btn.set_active (!visible);
	_collapse_btn.set_icon (visible ? ArdourIcon::ArrowLeft : ArdourIcon::ArrowRight);
	_ui->set_visible (visible);
}

void
ProcessorUIFrame::save_state () const
{
	GUIObjectState& st    = *ARDOUR_UI::instance ()->gui_object_state;
	XMLNode*        strip = st.get_or_add_node (_strip_id);
	XMLNode*        state = st.get_or_add_node (strip, _state_id);
	state->set_property (X_("visible"), !_collapse_btn.get_active ());
}

/* Spite: a device in the Cue page's Device view that shows the plugin's own GUI as its face,
 * under a title bar, like Live's device chain. Used for PgD4S (plugdata for Spite), whose GUI
 * then shows the patch in plugin mode: a converted Max for Live device's presentation. */
class DeviceFace : public Gtk::VBox
{
public:
	DeviceFace (std::shared_ptr<Processor>, std::shared_ptr<PlugInsertBase>, std::shared_ptr<LV2Plugin>, int face_h, int title_h);
	~DeviceFace ();

	/* free the GUI: before the face leaves its container (a live X11 GUI must never be re-parented) */
	void release ();
	void set_face_height (int);

protected:
	void on_map ();
	void on_unmap ();
	void on_unrealize ();

private:
	bool idle_show_face ();
	bool idle_hide_face ();
	void gui_showing (PlugInsertBase*);
	void gui_hidden (PlugInsertBase*);
	bool title_press (GdkEventButton*);

	Gtk::EventBox                   _title;
	Gtk::HBox                       _title_box;
	ArdourButton                    _enable_btn;
	Gtk::Label                      _name;
	Gtk::VBox                       _body;
	Gtk::Label                      _placeholder;
	LV2PluginUI*                    _ui;
	std::shared_ptr<Processor>      _proc;
	std::shared_ptr<PlugInsertBase> _pib;
	int                             _face_h;
	float                           _scale;
	sigc::connection                _idle;
	sigc::connection                _unmap_idle;
	sigc::connection                _showing_connection;
	sigc::connection                _hidden_connection;
	PBD::ScopedConnectionList       _connections;
};

void
spite_cue_log (std::string const& msg)
{
	static std::ofstream* out   = 0;
	static bool           tried = false;
	if (!tried) {
		tried = true;
		std::string const path = Glib::build_filename (ARDOUR::user_cache_directory (), X_("cue-layout.log"));
		out = new std::ofstream (path.c_str (), std::ios::out | std::ios::trunc);
		if (!out->is_open ()) {
			delete out;
			out = 0;
		}
	}
	if (!out) {
		return;
	}
	GDateTime* now = g_date_time_new_now_local ();
	gchar*     hms = g_date_time_format (now, "%H:%M:%S");
	char       ms[16];
	snprintf (ms, sizeof (ms), ".%03d ", g_date_time_get_microsecond (now) / 1000);
	*out << hms << ms << msg << std::endl;
	g_free (hms);
	g_date_time_unref (now);
}

/* the device's name, as Live shows it: a converted Max for Live device's own name (its preset
 * is "M4L: <name>"), else the processor's name */
static std::string
device_name (std::shared_ptr<Processor> p, std::shared_ptr<LV2Plugin> lv2)
{
	std::string const label = lv2->last_preset ().label;
	if (label.compare (0, 5, "M4L: ") == 0 && label.size () > 5) {
		return label.substr (5);
	}
	return p->display_name ();
}

DeviceFace::DeviceFace (std::shared_ptr<Processor> p, std::shared_ptr<PlugInsertBase> pib, std::shared_ptr<LV2Plugin> lv2, int face_h, int title_h)
	: _enable_btn (ArdourButton::just_led_default_elements)
	, _name (device_name (p, lv2))
	, _ui (new LV2PluginUI (pib, lv2))
	, _proc (p)
	, _pib (pib)
	, _face_h (face_h)
	, _scale (face_h / 169.f) /* a Max for Live device's face is 169 px tall */
{
	_ui->set_embedded (_scale);

	_title.modify_bg (Gtk::STATE_NORMAL, Gdk::Color ("#464646"));
	_name.set_alignment (0, 0.5);
	_name.modify_fg (Gtk::STATE_NORMAL, Gdk::Color ("#e6e6e6"));
	_name.set_ellipsize (Pango::ELLIPSIZE_END);
	_enable_btn.set_tweaks (ArdourButton::ExpandtoSquare);
	_enable_btn.set_active (_proc->enabled ());
	_enable_btn.signal_clicked.connect ([this] () { _proc->enable (!_proc->enabled ()); });
	p->ActiveChanged.connect (_connections, invalidator (*this), [this] () { _enable_btn.set_active (_proc->enabled ()); }, gui_context ());

	_title_box.pack_start (_enable_btn, false, false, 3);
	_title_box.pack_start (_name, true, true, 2);
	_title.add (_title_box);
	_title.set_size_request (-1, title_h);
	_title.add_events (Gdk::BUTTON_PRESS_MASK);
	_title.signal_button_press_event ().connect (sigc::mem_fun (*this, &DeviceFace::title_press), false);
	set_tooltip (&_enable_btn, _("Turn the device on or off"));
	set_tooltip (&_title, _("Double-click to open its window"));

	_placeholder.set_text (_("In its window"));
	_placeholder.set_no_show_all ();
	_body.pack_start (*_ui, true, true);
	_body.pack_start (_placeholder, true, true);
	_body.set_size_request (-1, face_h);

	pack_start (_title, false, false);
	pack_start (_body, false, false); /* the face's own height: no band around it */

	_showing_connection = PluginUIWindow::CustomGUIShowing.connect (sigc::mem_fun (*this, &DeviceFace::gui_showing));
	_hidden_connection  = PluginUIWindow::CustomGUIHidden.connect (sigc::mem_fun (*this, &DeviceFace::gui_hidden));
	show_all ();
}

DeviceFace::~DeviceFace ()
{
	_showing_connection.disconnect ();
	_hidden_connection.disconnect ();
	release ();
	delete _ui;
}

void
DeviceFace::release ()
{
	_idle.disconnect ();
	_unmap_idle.disconnect ();
	if (_ui->embed_active ()) {
		spite_cue_log (string_compose ("face %1: GUI freed", _name.get_text ()));
		_ui->embed_hide ();
	}
}

/* The GUI lives while the face is on screen: created once it is mapped (OpenGL needs a
 * shown window), freed soon after it is unmapped (another page, the Clip view), and at
 * once on unrealize, before GTK could re-parent the live X11 GUI. */
void
DeviceFace::on_map ()
{
	Gtk::VBox::on_map ();
	_unmap_idle.disconnect ();
	_idle.disconnect ();
	_idle = Glib::signal_idle ().connect (sigc::mem_fun (*this, &DeviceFace::idle_show_face));
}

void
DeviceFace::on_unmap ()
{
	_idle.disconnect ();
	_unmap_idle.disconnect ();
	_unmap_idle = Glib::signal_idle ().connect (sigc::mem_fun (*this, &DeviceFace::idle_hide_face));
	Gtk::VBox::on_unmap ();
}

void
DeviceFace::on_unrealize ()
{
	release (); /* while the GUI is still in place */
	Gtk::VBox::on_unrealize ();
}

bool
DeviceFace::idle_hide_face ()
{
	if (!get_mapped ()) {
		release ();
	}
	return false;
}

bool
DeviceFace::idle_show_face ()
{
	if (!get_mapped ()) {
		return false;
	}
	if (PluginUIWindow::custom_gui_shown (_pib.get ())) {
		_ui->hide ();
		_placeholder.show ();
		return false;
	}
	_placeholder.hide ();
	_ui->show ();
	gint64 const t0 = g_get_monotonic_time ();
	_ui->embed_show ();
	spite_cue_log (string_compose ("face %1: GUI made in %2 ms", _name.get_text (), (g_get_monotonic_time () - t0) / 1000));
	return false;
}

void
DeviceFace::set_face_height (int h)
{
	if (h == _face_h || h < 20) {
		return;
	}
	_face_h = h;
	_body.set_size_request (-1, h); /* PgD4S zooms to spite_lv2_ui_face_height () */
}

void
DeviceFace::gui_showing (PlugInsertBase* pib)
{
	if (pib == _pib.get ()) {
		release ();
		_ui->hide ();
		_placeholder.show ();
	}
}

void
DeviceFace::gui_hidden (PlugInsertBase* pib)
{
	if (pib == _pib.get () && get_mapped ()) {
		_idle.disconnect ();
		_idle = Glib::signal_idle ().connect (sigc::mem_fun (*this, &DeviceFace::idle_show_face));
	}
}

bool
DeviceFace::title_press (GdkEventButton* ev)
{
	if (ev->type == GDK_2BUTTON_PRESS && ev->button == 1) {
		_proc->ShowUI (); /* the plugin's window: PgD4S shows its patch editor there */
		return true;
	}
	return false;
}

/* ****************************************************************************/

RoutePropertiesBox::RoutePropertiesBox ()
	: _insert_box (nullptr)
	, _show_insert (false)
	, _always_show_insert (false)
	, _device_faces (false)
	, _face_zoom (1.f)
	, _idle_refill_processors_id (-1)
{
	_scroller.set_policy (Gtk::POLICY_AUTOMATIC, Gtk::POLICY_NEVER);
	_scroller.add (_box);

	/* the return of the ScrolledWindowViewport mess:
	 * remove shadow from scrollWindow's viewport
	 * see http://www.mail-archive.com/gtkmm-list@gnome.org/msg03509.html
	 */
	Gtk::Viewport* viewport = (Gtk::Viewport*) _scroller.get_child();
	viewport->set_shadow_type(Gtk::SHADOW_NONE);
	viewport->set_border_width(0);
	/* Spite: Ctrl+wheel in the Device view, see devices_viewport_realized () */
	viewport->signal_realize ().connect (sigc::mem_fun (*this, &RoutePropertiesBox::devices_viewport_realized));
	viewport->signal_unrealize ().connect (sigc::mem_fun (*this, &RoutePropertiesBox::devices_viewport_unrealized), false);

	_box.set_spacing (4);
	_insert_frame.set_no_show_all ();

	pack_start (_insert_frame, false, false, 4);
	pack_start (_scroller, true, true);
	pack_start (_empty_label, true, true);
	_empty_label.set_no_show_all ();
	_empty_label.set_alignment (0.5, 0.5);
	_empty_label.set_size_request (-1, 160 * std::max<float> (1.f, UIConfiguration::instance ().get_ui_scale ()));
	show_all();

	ARDOUR_UI::instance()->ActionsReady.connect_same_thread (_forever_connections, std::bind (&RoutePropertiesBox::ui_actions_ready, this));
}

RoutePropertiesBox::~RoutePropertiesBox ()
{
}

void
RoutePropertiesBox::ui_actions_ready ()
{
	Glib::RefPtr<ToggleAction> tact = ActionManager::get_toggle_action (X_("Editor"), X_("show-editor-mixer"));
	tact->signal_toggled().connect (sigc::mem_fun (*this, &RoutePropertiesBox::update_processor_box_visibility));
}

void
RoutePropertiesBox::session_going_away ()
{
	ENSURE_GUI_THREAD (*this, &RoutePropertiesBox::session_going_away);
	SessionHandlePtr::session_going_away ();

	_insert_frame.remove ();
	drop_plugin_uis ();
	drop_route ();
	delete _insert_box;
	_insert_box = nullptr;
}

void
RoutePropertiesBox::set_session (ARDOUR::Session* s) {
	SessionHandlePtr::set_session (s);
	if (!s) {
		return;
	}
	delete _insert_box;
	_insert_box = new ProcessorBox (_session, std::bind (&Mixer_UI::plugin_selector, Mixer_UI::instance()), Mixer_UI::instance()->selection(), 0);
	_insert_box->show_all ();

	float ui_scale = std::max<float> (1.f, UIConfiguration::instance().get_ui_scale());
	_insert_frame.remove ();
	_insert_frame.add (*_insert_box);
	_insert_frame.set_padding (4);
	/* Spite: in the Cue page's Device view the list follows the faces' height */
	_insert_frame.set_size_request (144 * ui_scale, _device_faces ? -1 : 236 * ui_scale);

	_session->SurroundMasterAddedOrRemoved.connect (_session_connections, invalidator (*this), std::bind (&RoutePropertiesBox::surround_master_added_or_removed, this), gui_context());
}

void
RoutePropertiesBox::surround_master_added_or_removed ()
{
	set_route (_route, true);
}

void
RoutePropertiesBox::set_route (std::shared_ptr<Route> r, bool force_update)
{
	if (r == _route && !force_update) {
		return;
	}

	if (!r || !_insert_box) {
		/* Spite: also with no session yet, or one going away */
		drop_route ();
		return;
	}

	_route = r;
	_route_connections.drop_connections ();

	_route->processors_changed.connect (_route_connections, invalidator (*this), std::bind (&RoutePropertiesBox::idle_refill_processors, this), gui_context());
	_route->PropertyChanged.connect (_route_connections, invalidator (*this), std::bind (&RoutePropertiesBox::property_changed, this, _1), gui_context ());
	_route->DropReferences.connect (_route_connections, invalidator (*this), std::bind (&RoutePropertiesBox::drop_route, this), gui_context());

	std::shared_ptr<AudioTrack> at = std::dynamic_pointer_cast<AudioTrack>(_route);
	if (at) {
		at->FreezeChange.connect (_route_connections, invalidator (*this), std::bind (&RoutePropertiesBox::map_frozen, this), gui_context());
	}

	_insert_box->set_route (r);
	refill_processors ();
}

void
RoutePropertiesBox::map_frozen ()
{
	ENSURE_GUI_THREAD (*this, &RoutePropertiesBox::map_frozen)
		std::shared_ptr<AudioTrack> at = std::dynamic_pointer_cast<AudioTrack>(_route);
	if (at && _insert_box) {
		switch (at->freeze_state()) {
			case AudioTrack::Frozen:
				_insert_box->set_sensitive (false);
				break;
			default:
				_insert_box->set_sensitive (true);
				break;
		}
	}
}

void
RoutePropertiesBox::update_processor_box_visibility ()
{
	_show_insert = _always_show_insert || !ActionManager::get_toggle_action (X_("Editor"), X_("show-editor-mixer"))->get_active ();
	if (!_show_insert || (_proc_uis.empty () && _faces.empty () && !_always_show_insert)) {
		_insert_frame.hide ();
	} else {
		_insert_frame.show ();
	}

#ifndef MIXBUS
	if (_always_show_insert) {
		/* Spite: as tall as the device list, like Live's device view */
		set_size_request (-1, -1);
	} else if (_show_insert || !_proc_uis.empty ()) {
		float ui_scale = std::max<float> (1.f, UIConfiguration::instance().get_ui_scale());
		set_size_request (-1, 365 * ui_scale); // match with SelectionPropertiesBox
	} else {
		set_size_request (-1, -1);
	}
#endif

	update_empty_label ();
}

void
RoutePropertiesBox::update_empty_label ()
{
	/* nothing to show: say so, rather than collapsing to an empty strip */
	if (_proc_uis.empty () && _faces.empty () && !_insert_frame.get_visible () && !_empty_label.get_text ().empty ()) {
		_empty_label.show ();
	} else {
		_empty_label.hide ();
	}
}

void
RoutePropertiesBox::set_always_show_insert_list (bool yn)
{
	_always_show_insert = yn;
	if (_route) {
		update_processor_box_visibility ();
	}
}

void
RoutePropertiesBox::set_device_faces (bool yn)
{
	_device_faces = yn;
	if (_route) {
		set_route (_route, true);
	}
}

/* Live's device view: the face is 9.49 clip rows tall and the title bar 1.07, and a
 * Cue page clip row is 16 px times the UI scale (trigger_strip.cc, fitted_canvas_widget.cc) */
int
RoutePropertiesBox::face_height () const
{
	return rintf (9.49f * 16.f * UIConfiguration::instance ().get_ui_scale () * _face_zoom);
}

int
RoutePropertiesBox::title_height () const
{
	return rintf (1.07f * 16.f * UIConfiguration::instance ().get_ui_scale ());
}

/* what the Device view needs besides the faces: their title bars, the box's
 * 4 px, and the room the scroller always asks for its scrollbar. Leaving any
 * of it out makes the faces ask for more than they get, and the pane grows
 * the view to give it to them, again and again. */
int
RoutePropertiesBox::face_overhead () const
{
	int const scroller = _scroller.size_request ().height - _box.size_request ().height;
	return title_height () + 4 + std::max (0, scroller);
}

int
RoutePropertiesBox::height_for_zoom (float zoom) const
{
	float const base = 9.49f * 16.f * UIConfiguration::instance ().get_ui_scale ();
	return face_overhead () + std::max (48, (int) rintf (base * std::min (3.f, zoom)));
}

int
RoutePropertiesBox::min_height () const
{
	return face_overhead () + 48;
}

float
RoutePropertiesBox::zoom_for_height (int h) const
{
	float const base = 9.49f * 16.f * UIConfiguration::instance ().get_ui_scale ();
	int const   fh   = std::max (48, h - face_overhead ());
	return std::min (3.f, fh / base);
}

/* Spite: the Device view got taller or shorter (its pane divider): zoom the faces to fit */
void
RoutePropertiesBox::set_available_height (int h)
{
	float const zoom = zoom_for_height (h);
	if (fabsf (zoom - _face_zoom) < 0.01f) {
		return;
	}
	spite_cue_log (string_compose ("faces: %1 px -> zoom %2 (was %3)", h, zoom, _face_zoom));
	_face_zoom = zoom;
	ARDOUR::set_lv2_ui_face_height (face_height ());
	for (auto const& face : _faces) {
		face->set_face_height (face_height ());
	}
	if (_device_faces && !(_proc_uis.empty () && _faces.empty ())) {
		_box.set_size_request (-1, title_height () + face_height () + 4);
	}
}

#ifdef GDK_WINDOWING_X11
/* Spite: in the Device view, Ctrl+wheel anywhere over the devices scrolls them
 * sideways, down to the right and up to the left. A PgD4S face is an X window
 * of its own that would take the wheel itself, so the row grabs Ctrl+wheel at
 * the X level, and takes it before GDK passes it to the widget under the
 * pointer (a knob would turn). */
void
RoutePropertiesBox::devices_viewport_realized ()
{
	Gtk::Viewport* viewport = dynamic_cast<Gtk::Viewport*> (_scroller.get_child ());
	if (!_device_faces || !viewport || !viewport->get_bin_window ()) {
		return;
	}
	GdkWindow*     win  = viewport->get_bin_window ()->gobj ();
	::Display*     dpy  = GDK_WINDOW_XDISPLAY (win);
	::Window       xwin = GDK_WINDOW_XID (win);
	unsigned const mods[] = { ControlMask, ControlMask | LockMask, ControlMask | Mod2Mask, ControlMask | LockMask | Mod2Mask }; /* Caps and Num Lock on or off */

	gdk_error_trap_push ();
	for (unsigned int button = 4; button <= 7; ++button) { /* up, down, left, right */
		for (auto const m : mods) {
			XGrabButton (dpy, button, m, xwin, False, ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
		}
	}
	gdk_flush ();
	gdk_error_trap_pop ();

	gdk_window_add_filter (win, devices_wheel_filter, this);
}

void
RoutePropertiesBox::devices_viewport_unrealized ()
{
	/* the grabs go with the window */
	Gtk::Viewport* viewport = dynamic_cast<Gtk::Viewport*> (_scroller.get_child ());
	if (viewport && viewport->get_bin_window ()) {
		gdk_window_remove_filter (viewport->get_bin_window ()->gobj (), devices_wheel_filter, this);
	}
}

GdkFilterReturn
RoutePropertiesBox::devices_wheel_filter (GdkXEvent* gxev, GdkEvent*, gpointer self)
{
	XEvent const* xev = (XEvent const*) gxev;
	if ((xev->type != ButtonPress && xev->type != ButtonRelease) || xev->xbutton.button < 4 || xev->xbutton.button > 7 || !(xev->xbutton.state & ControlMask)) {
		return GDK_FILTER_CONTINUE;
	}
	if (xev->type == ButtonPress) {
		bool const back = (xev->xbutton.button == 4 || xev->xbutton.button == 6);
		static_cast<RoutePropertiesBox*> (self)->scroll_devices (back ? -1 : 1);
	}
	return GDK_FILTER_REMOVE;
}
#else
void RoutePropertiesBox::devices_viewport_realized () {}
void RoutePropertiesBox::devices_viewport_unrealized () {}
#endif

void
RoutePropertiesBox::scroll_devices (int dir)
{
	Gtk::Adjustment* adj  = _scroller.get_hadjustment ();
	double const     page = adj->get_page_size ();
	double const     step = pow (page, 2.0 / 3.0); /* the step GTK's own wheel scrolling takes */
	adj->set_value (std::max (adj->get_lower (), std::min (adj->get_upper () - page, adj->get_value () + dir * step)));
}

void
RoutePropertiesBox::set_empty_text (std::string const& text)
{
	_empty_label.set_text (text);
	update_empty_label ();
}

void
RoutePropertiesBox::property_changed (const PBD::PropertyChange& what_changed)
{
}

void
RoutePropertiesBox::drop_route ()
{
	drop_plugin_uis ();
	_route.reset ();
	_route_connections.drop_connections ();
	if (_idle_refill_processors_id >= 0) {
		g_source_destroy (g_main_context_find_source_by_id (NULL, _idle_refill_processors_id));
		_idle_refill_processors_id = -1;
	}
}

void
RoutePropertiesBox::drop_plugin_uis ()
{
	for (auto const& face : _faces) {
		face->release (); /* before the face leaves _box */
	}
	_faces.clear ();

	std::list<Gtk::Widget*> children = _box.get_children ();
	for (auto const& child : children) {
		child->hide ();
		_box.remove (*child);
		delete child;
	}

	for (auto const& ui : _proc_uis) {
		ui->stop_updating (0);
		delete ui;
	}

	_processor_connections.drop_connections ();
	_proc_uis.clear ();
	_insert_frame.hide ();
}

void
RoutePropertiesBox::add_processor_to_display (std::weak_ptr<Processor> w)
{
	std::shared_ptr<Processor> p = w.lock ();
	std::shared_ptr<PlugInsertBase> pib = std::dynamic_pointer_cast<PlugInsertBase> (p);
	if (!pib) {
		return;
	}
#ifdef MIXBUS
	if (std::dynamic_pointer_cast<PluginInsert> (pib)->channelstrip () != Processor::None) {
		return;
	}
#endif
	if (_device_faces) {
		/* Spite: PgD4S (plugdata for Spite) shows its own face */
		std::shared_ptr<PluginInsert> pi  = std::dynamic_pointer_cast<PluginInsert> (pib);
		std::shared_ptr<LV2Plugin>    lv2 = pi ? std::dynamic_pointer_cast<LV2Plugin> (pi->plugin ()) : std::shared_ptr<LV2Plugin> ();
		if (lv2 && lv2->has_editor () && !lv2->is_external_ui () && std::string (lv2->uri ()).find ("https://github.com/timothyschoen/plugdata") == 0) {
			ARDOUR::set_lv2_ui_face_height (face_height ());
			DeviceFace* face = new DeviceFace (p, pib, lv2, face_height (), title_height ());
			_faces.push_back (face);
			_box.pack_start (*face, false, false);
			return;
		}
	}
	GenericPluginUI* plugin_ui = new GenericPluginUI (pib, true, true);
	if (plugin_ui->empty ()) {
		delete plugin_ui;
		return;
	}
	//pib->DropReferences.connect (_processor_connections, invalidator (*this), std::bind (&RoutePropertiesBox::refill_processors, this), gui_context());
	_proc_uis.push_back (plugin_ui);
#if 0
	ArdourWidgets::Frame* frame = new ArdourWidgets::Frame ();
	frame->set_label (p->display_name ());
	frame->add (*plugin_ui);
	frame->set_padding (0);
	_box.pack_start (*frame, false, false);
	plugin_ui->show ();
#else
	ProcessorUIFrame* frame = new ProcessorUIFrame (_route, p, plugin_ui);
	_box.pack_start (*frame, false, false);
#endif
}

int
RoutePropertiesBox::_idle_refill_processors (gpointer arg)
{
	static_cast<RoutePropertiesBox*>(arg)->refill_processors ();
	return 0;
}

void
RoutePropertiesBox::idle_refill_processors ()
{
	if (_idle_refill_processors_id < 0) {
		_idle_refill_processors_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE + 10, _idle_refill_processors, this, NULL);
	}
}

void
RoutePropertiesBox::refill_processors ()
{
	if (!_session || _session->deletion_in_progress()) {
		return;
	}
	drop_plugin_uis ();

	assert (_route);

	if (!_route) {
		_idle_refill_processors_id = -1;
		return;
	}

	_route->foreach_processor (sigc::mem_fun (*this, &RoutePropertiesBox::add_processor_to_display));
	if (_device_faces) {
		spite_cue_log (string_compose ("devices of %1: %2 faces, %3 other devices", _route->name (), _faces.size (), _proc_uis.size ()));
	}
	if (_proc_uis.empty () && _faces.empty ()) {
		_scroller.hide ();
	} else if (_device_faces) {
		/* Spite: Live's proportions; generic devices fit the same height */
		_box.set_size_request (-1, title_height () + face_height () + 4);
		_scroller.show_all ();
	} else {
		float ui_scale = std::max<float> (1.f, UIConfiguration::instance().get_ui_scale());
		int h = 100 * ui_scale;
		for (auto const& ui : _proc_uis) {
			h = std::max<int> (h, ui->get_preferred_height () + /* frame label */ 34 * ui_scale);
		}
		h = std::min<int> (h, 300 * ui_scale);
		_box.set_size_request (-1, h);
		_scroller.show_all ();
	}
	update_processor_box_visibility ();
	_idle_refill_processors_id = -1;
}
