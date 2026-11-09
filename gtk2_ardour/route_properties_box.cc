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

#include <algorithm>
#include <atomic>
#include <cassert>
#include <ytkmm/widget.h>

#include "pbd/compose.h"

#include "ardour/audio_track.h"
#include "ardour/audioengine.h"
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
 * then shows the patch in plugin mode: a converted Max for Live device's presentation.
 *
 * A face keeps its last picture (see FaceMemory): it shows that at the face's size until its
 * GUI is ready, so a face never shows up collapsed, and washed out while the device's own
 * window is open, as Live does (one PgD4S editor per plugin, so the face's GUI has to go). */
class DeviceFace : public Gtk::VBox
{
public:
	DeviceFace (std::shared_ptr<Route>, std::shared_ptr<Processor>, std::shared_ptr<PlugInsertBase>, std::shared_ptr<LV2Plugin>, int face_h, int title_h);
	~DeviceFace ();

	/* free the GUI: before the face leaves its container (a live X11 GUI must never be re-parented) */
	void release ();
	void set_face_height (int);
	/* keep a picture of the face as it is on screen now, if it is */
	void snapshot ();

	PBD::ID const&             route_id () const { return _route_id; }
	std::shared_ptr<Processor> processor () const { return _proc; }
	std::shared_ptr<LV2Plugin> plugin () const { return _lv2; }

	/* the processor went away (RoutePropertiesBox deletes the face) */
	PBD::ScopedConnection gone_connection;

protected:
	void on_map ();
	void on_unmap ();
	void on_unrealize ();

private:
	bool idle_show_face ();
	void gui_showing (PlugInsertBase*);
	void gui_hidden (PlugInsertBase*);
	bool title_press (GdkEventButton*);
	void show_picture (bool washed);
	void size_body ();

	Gtk::EventBox                   _title;
	Gtk::HBox                       _title_box;
	ArdourButton                    _enable_btn;
	Gtk::Label                      _name;
	Gtk::VBox                       _body;
	Gtk::Image                      _picture;
	Gtk::Label                      _placeholder;
	LV2PluginUI*                    _ui;
	std::shared_ptr<Processor>      _proc;
	std::shared_ptr<PlugInsertBase> _pib;
	std::shared_ptr<LV2Plugin>      _lv2;
	PBD::ID                         _route_id;
	int                             _face_h;
	float                           _scale;
	bool                            _picture_washed;
	sigc::connection                _idle;
	sigc::connection                _snap_timer;
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

/* Spite: with SPITE_DSP_PROBE set in the environment, the log also gets the audio cycles that
 * took more than half their period, next to the faces' events. One past its period is an
 * xrun with JACK, and with PipeWire it glitches every app's sound. */
static std::atomic<int> spite_engine_xruns (0);

static bool
spite_dsp_probe ()
{
	if (int const x = spite_engine_xruns.exchange (0)) {
		/* the engine's own: a cycle skipped (the process lock was busy) or one the backend lost */
		spite_cue_log (string_compose ("audio: %1 engine xrun(s) -- XRUN", x));
	}
	AudioEngine*      e  = AudioEngine::instance ();
	PBD::TimingStats& st = e->dsp_stats[AudioEngine::ProcessCallback];
	PBD::microseconds_t mn, mx;
	double              avg, dev;
	if (!e->running () || e->sample_rate () == 0 || !st.get_stats (mn, mx, avg, dev)) {
		return true;
	}
	double const period = 1e6 * e->samples_per_cycle () / (double) e->sample_rate ();
	if (mx > period / 2) {
		spite_cue_log (string_compose ("audio: a cycle took %1 ms of its %2 ms%3", mx / 1000., period / 1000., mx > period ? " -- XRUN" : ""));
	}
	st.queue_reset ();
	return true;
}

static void
spite_dsp_probe_start ()
{
	static bool started = false;
	if (!started && g_getenv ("SPITE_DSP_PROBE")) {
		started = true;
		spite_cue_log ("audio: logging long cycles");
		static PBD::ScopedConnection xrun_connection;
		AudioEngine::instance ()->Xrun.connect_same_thread (xrun_connection, [] () { ++spite_engine_xruns; });
		Glib::signal_timeout ().connect (sigc::ptr_fun (&spite_dsp_probe), 50);
	}
}

/* Spite: what the Device view remembers of each face, by processor: its last picture, which
 * also gives its width for its height. Kept for the run, and as a PNG in Spite's cache folder
 * (one folder per session) so a face has its size and picture from the start next time. */
struct FaceMemory {
	Glib::RefPtr<Gdk::Pixbuf> picture;
	bool                      loaded = false; /* looked for the PNG */
};

static std::map<PBD::ID, FaceMemory> face_memory;

static std::string
face_picture_path (PBD::ID const& id)
{
	Session* s = ARDOUR_UI::instance ()->the_session ();
	if (!s) {
		return std::string ();
	}
	std::string const dir = Glib::build_filename (ARDOUR::user_cache_directory (), X_("faces"), string_compose ("%1", g_str_hash (s->path ().c_str ())));
	return Glib::build_filename (dir, id.to_s () + X_(".png"));
}

static FaceMemory&
remembered_face (PBD::ID const& id)
{
	FaceMemory& m = face_memory[id];
	if (!m.loaded) {
		m.loaded = true;
		std::string const path = face_picture_path (id);
		if (!path.empty () && Glib::file_test (path, Glib::FILE_TEST_EXISTS)) {
			try {
				m.picture = Gdk::Pixbuf::create_from_file (path);
			} catch (...) {
			}
		}
	}
	return m;
}

/* a face whose device's window is open: Live's washed-out look */
static Glib::RefPtr<Gdk::Pixbuf>
washed_out (Glib::RefPtr<Gdk::Pixbuf> const& src)
{
	Glib::RefPtr<Gdk::Pixbuf> p  = src->copy ();
	int const                 nc = p->get_n_channels ();
	int const                 rs = p->get_rowstride ();
	guint8*                   px = p->get_pixels ();
	for (int y = 0; y < p->get_height (); ++y) {
		guint8* c = px + y * rs;
		for (int x = 0; x < p->get_width (); ++x, c += nc) {
			int const grey = (c[0] * 30 + c[1] * 59 + c[2] * 11) / 100;
			for (int k = 0; k < 3; ++k) {
				int const v = (c[k] + grey) / 2;         /* half the colour */
				c[k]        = v + (255 - v) * 62 / 100; /* most of the way to white */
			}
		}
	}
	return p;
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

DeviceFace::DeviceFace (std::shared_ptr<Route> r, std::shared_ptr<Processor> p, std::shared_ptr<PlugInsertBase> pib, std::shared_ptr<LV2Plugin> lv2, int face_h, int title_h)
	: _enable_btn (ArdourButton::just_led_default_elements)
	, _name (device_name (p, lv2))
	, _ui (new LV2PluginUI (pib, lv2))
	, _proc (p)
	, _pib (pib)
	, _lv2 (lv2)
	, _route_id (r->id ())
	, _face_h (face_h)
	, _scale (face_h / 169.f) /* a Max for Live device's face is 169 px tall */
	, _picture_washed (false)
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
	_picture.set_no_show_all ();
	_picture.set_alignment (0, 0);
	_body.pack_start (*_ui, true, true);
	_body.pack_start (_picture, true, true);
	_body.pack_start (_placeholder, true, true);

	pack_start (_title, false, false);
	pack_start (_body, false, false); /* the face's own height: no band around it */

	_showing_connection = PluginUIWindow::CustomGUIShowing.connect (sigc::mem_fun (*this, &DeviceFace::gui_showing));
	_hidden_connection  = PluginUIWindow::CustomGUIHidden.connect (sigc::mem_fun (*this, &DeviceFace::gui_hidden));
	show_all ();

	/* at its size and with its picture from the start, until its GUI is made */
	_ui->hide ();
	show_picture (false);
	size_body ();
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
	_snap_timer.disconnect ();
	if (_ui->embed_active ()) {
		spite_cue_log (string_compose ("face %1: GUI freed", _name.get_text ()));
		_ui->embed_hide ();
	}
}

/* the body's size: the GUI's own once it is there, else the picture's (or, with neither, the
 * face's height only) */
void
DeviceFace::size_body ()
{
	FaceMemory& m = remembered_face (_proc->id ());
	if (_ui->embed_active () || !m.picture || m.picture->get_height () < 1) {
		_body.set_size_request (-1, _face_h);
	} else {
		_body.set_size_request (rintf (_face_h * m.picture->get_width () / (float) m.picture->get_height ()), _face_h);
	}
}

void
DeviceFace::show_picture (bool washed)
{
	FaceMemory& m = remembered_face (_proc->id ());
	_picture_washed = washed;
	if (!m.picture || m.picture->get_height () < 1) {
		_picture.hide ();
		if (washed) {
			_placeholder.show (); /* never seen yet: say where it is */
		} else {
			_placeholder.hide ();
		}
		return;
	}
	Glib::RefPtr<Gdk::Pixbuf> pb = m.picture;
	if (pb->get_height () != _face_h) {
		int const w = std::max (1, (int) rintf (_face_h * pb->get_width () / (float) pb->get_height ()));
		pb          = pb->scale_simple (w, _face_h, Gdk::INTERP_BILINEAR);
	}
	_picture.set (washed ? washed_out (pb) : pb);
	_placeholder.hide ();
	_picture.show ();
}

/* the face's pixels, the plugin's own X window and all, read back from the screen: only when
 * the GUI is up and the whole face can be seen */
void
DeviceFace::snapshot ()
{
	if (!_ui->embed_active () || !_ui->get_visible () || !_ui->is_mapped ()) {
		return;
	}
	Glib::RefPtr<Gdk::Window> win = _ui->get_window ();
	if (!win || !win->is_viewable ()) {
		return;
	}
	Gtk::Allocation const a = _ui->get_allocation ();
	if (a.get_width () < 8 || a.get_height () < 8) {
		return;
	}
	if (GtkWidget* vp = gtk_widget_get_ancestor (GTK_WIDGET (gobj ()), GTK_TYPE_VIEWPORT)) {
		GtkAdjustment* h = gtk_viewport_get_hadjustment (GTK_VIEWPORT (vp));
		GtkAdjustment* v = gtk_viewport_get_vadjustment (GTK_VIEWPORT (vp));
		if ((h && (a.get_x () < h->value || a.get_x () + a.get_width () > h->value + h->page_size))
		    || (v && (a.get_y () < v->value || a.get_y () + a.get_height () > v->value + v->page_size))) {
			return; /* scrolled partly out of view */
		}
	}

	gdk_error_trap_push ();
	GdkPixbuf* pb = gdk_pixbuf_get_from_drawable (NULL, win->gobj (), NULL, a.get_x (), a.get_y (), 0, 0, a.get_width (), a.get_height ());
	gdk_flush ();
	bool const failed = gdk_error_trap_pop () != 0;
	if (!pb) {
		return;
	}
	Glib::RefPtr<Gdk::Pixbuf> picture = Glib::wrap (pb);
	if (failed) {
		return;
	}
	remembered_face (_proc->id ()).picture = picture;

	std::string const path = face_picture_path (_proc->id ());
	if (!path.empty ()) {
		/* written when Spite is idle: it's for the next time the session is opened */
		Glib::signal_idle ().connect ([picture, path] () {
			g_mkdir_with_parents (Glib::path_get_dirname (path).c_str (), 0755);
			try {
				picture->save (path, "png");
			} catch (...) {
			}
			return false;
		}, Glib::PRIORITY_LOW);
	}
}

/* The GUI is made once the face is mapped (OpenGL needs a shown window) and then kept, since
 * making a PgD4S GUI takes a moment. A face that's hidden (the Device view shows another
 * track, see RoutePropertiesBox::refill_processors) keeps its GUI: PgD4S stops drawing while
 * its window can't be seen, and showing it again is instant. The GUI is freed when the face
 * goes (or on unrealize, before GTK could re-parent the live X11 GUI) and while the device's
 * own window is open. */
void
DeviceFace::on_map ()
{
	Gtk::VBox::on_map ();
	_idle.disconnect ();
	_idle = Glib::signal_idle ().connect (sigc::mem_fun (*this, &DeviceFace::idle_show_face));
}

void
DeviceFace::on_unmap ()
{
	_idle.disconnect ();
	_snap_timer.disconnect ();
	if (_ui->embed_active ()) {
		_ui->stop_updating (0); /* nothing shows it */
	}
	Gtk::VBox::on_unmap ();
}

void
DeviceFace::on_unrealize ()
{
	release (); /* while the GUI is still in place */
	Gtk::VBox::on_unrealize ();
}

bool
DeviceFace::idle_show_face ()
{
	if (!get_mapped ()) {
		return false;
	}
	if (PluginUIWindow::custom_gui_shown (_pib.get ())) {
		_ui->hide ();
		show_picture (true);
		size_body ();
		return false;
	}
	_ui->show ();
	if (_ui->embed_active ()) {
		_picture.hide ();
		_placeholder.hide ();
		_ui->embed_show (); /* its GUI was kept: updates resume */
		spite_cue_log (string_compose ("face %1: shown again, GUI kept", _name.get_text ()));
		return false;
	}
	gint64 const t0 = g_get_monotonic_time ();
	_ui->embed_show ();
	spite_cue_log (string_compose ("face %1: GUI made in %2 ms", _name.get_text (), (g_get_monotonic_time () - t0) / 1000));
	if (_ui->embed_active ()) {
		_picture.hide ();
		_placeholder.hide ();
		size_body ();
		/* a fresh picture once it has drawn itself */
		_snap_timer.disconnect ();
		_snap_timer = Glib::signal_timeout ().connect ([this] () { snapshot (); return false; }, 1500);
	} else {
		_ui->hide ();
	}
	return false;
}

void
DeviceFace::set_face_height (int h)
{
	if (h == _face_h || h < 20) {
		return;
	}
	_face_h = h;
	size_body (); /* PgD4S zooms to spite_lv2_ui_face_height () */
	if (_picture.get_visible ()) {
		show_picture (_picture_washed);
	}
}

void
DeviceFace::gui_showing (PlugInsertBase* pib)
{
	if (pib == _pib.get ()) {
		/* its window is opening: keep its picture, then let its GUI go (one editor per plugin) */
		snapshot ();
		release ();
		_ui->hide ();
		show_picture (true);
		size_body ();
	}
}

void
DeviceFace::gui_hidden (PlugInsertBase* pib)
{
	if (pib == _pib.get ()) {
		show_picture (false); /* until its GUI is back */
		if (get_mapped ()) {
			_idle.disconnect ();
			_idle = Glib::signal_idle ().connect (sigc::mem_fun (*this, &DeviceFace::idle_show_face));
		}
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
	, _display_pos (0)
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
	spite_dsp_probe_start ();
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
	face_memory.clear (); /* processor IDs are only unique within a session */
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
	/* Spite: a track going away takes its faces along, kept ones too */
	PBD::ID const route_id = _route->id ();
	_route->DropReferences.connect (_route_connections, invalidator (*this), [this, route_id] () {
		for (auto const& face : std::vector<DeviceFace*> (_all_faces)) {
			if (face->route_id () == route_id) {
				delete_face (face);
			}
		}
		_recent_routes.remove (route_id);
		drop_route ();
	}, gui_context());

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
	/* Spite: the track's faces stay, hidden, for when it is shown again */
	drop_generic_uis ();
	for (auto const& face : _faces) {
		face->snapshot ();
		face->hide ();
	}
	_faces.clear ();
	_insert_frame.hide ();
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
	drop_generic_uis ();
	for (auto const& face : std::vector<DeviceFace*> (_all_faces)) {
		delete_face (face);
	}
	_recent_routes.clear ();
	_insert_frame.hide ();
}

void
RoutePropertiesBox::drop_generic_uis ()
{
	for (auto const& frame : _generic_frames) {
		frame->hide ();
		_box.remove (*frame);
		delete frame;
	}
	_generic_frames.clear ();

	for (auto const& ui : _proc_uis) {
		ui->stop_updating (0);
		delete ui;
	}

	_processor_connections.drop_connections ();
	_proc_uis.clear ();
}

/* Spite: a face leaves _box and goes */
void
RoutePropertiesBox::delete_face (DeviceFace* face)
{
	_all_faces.erase (std::remove (_all_faces.begin (), _all_faces.end (), face), _all_faces.end ());
	_faces.erase (std::remove (_faces.begin (), _faces.end (), face), _faces.end ());
	face->release (); /* before the face leaves _box */
	face->hide ();
	_box.remove (*face);
	delete face;
}

/* Spite: its processor went away (taken out of its track, or the track deleted) */
void
RoutePropertiesBox::face_gone (DeviceFace* face)
{
	if (std::find (_all_faces.begin (), _all_faces.end (), face) == _all_faces.end ()) {
		return;
	}
	bool const shown = std::find (_faces.begin (), _faces.end (), face) != _faces.end ();
	delete_face (face);
	if (shown) {
		update_processor_box_visibility ();
	}
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
			/* the face kept from before, GUI and all, or a new one */
			DeviceFace* face = 0;
			for (auto const& f : _all_faces) {
				if (f->processor () == p && f->plugin () == lv2 && f->route_id () == _route->id ()) {
					face = f;
					break;
				}
			}
			if (!face) {
				face = new DeviceFace (_route, p, pib, lv2, face_height (), title_height ());
				_all_faces.push_back (face);
				_box.pack_start (*face, false, false);
				p->DropReferences.connect (face->gone_connection, invalidator (*face), std::bind (&RoutePropertiesBox::face_gone, this, face), gui_context ());
			} else {
				face->set_face_height (face_height ());
				face->show ();
			}
			_box.reorder_child (*face, _display_pos++);
			_faces.push_back (face);
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
	ProcessorUIFrame* frame = new ProcessorUIFrame (_route, p, plugin_ui);
	_box.pack_start (*frame, false, false);
	_box.reorder_child (*frame, _display_pos++);
	_generic_frames.push_back (frame);
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
	assert (_route);

	if (!_route) {
		drop_route ();
		_idle_refill_processors_id = -1;
		return;
	}

	/* Spite: faces aren't made again. The track's own are kept when it is refilled (pins,
	 * outputs, a device added), and those of the tracks shown last are kept hidden, GUIs
	 * and all, so going back to one is instant. Each face keeps a picture of itself first. */
	for (auto const& face : _faces) {
		face->snapshot ();
	}
	drop_generic_uis ();
	_faces.clear ();
	_display_pos = 0;

	std::vector<DeviceFace*> const had = _all_faces;
	_route->foreach_processor (sigc::mem_fun (*this, &RoutePropertiesBox::add_processor_to_display));

	for (auto const& face : std::vector<DeviceFace*> (_all_faces)) {
		if (std::find (_faces.begin (), _faces.end (), face) != _faces.end ()) {
			continue;
		}
		if (face->route_id () == _route->id ()) {
			delete_face (face); /* no longer on the track, or a new plugin */
		} else if (face->get_visible ()) {
			face->hide ();
		}
	}

	/* the last four tracks with faces keep theirs */
	_recent_routes.remove (_route->id ());
	if (!_faces.empty ()) {
		_recent_routes.push_front (_route->id ());
	}
	while (_recent_routes.size () > 4) {
		PBD::ID const old = _recent_routes.back ();
		_recent_routes.pop_back ();
		for (auto const& face : std::vector<DeviceFace*> (_all_faces)) {
			if (face->route_id () == old) {
				delete_face (face);
			}
		}
	}

	if (_device_faces) {
		size_t made = 0;
		for (auto const& face : _faces) {
			made += std::find (had.begin (), had.end (), face) == had.end ();
		}
		spite_cue_log (string_compose ("devices of %1: %2 faces (%3 new), %4 other devices", _route->name (), _faces.size (), made, _proc_uis.size ()));
	}
	if (_proc_uis.empty () && _faces.empty ()) {
		_scroller.hide ();
	} else if (_device_faces) {
		/* Spite: Live's proportions; generic devices fit the same height */
		_box.set_size_request (-1, title_height () + face_height () + 4);
		_box.show ();
		_scroller.show (); /* not show_all (): the kept faces stay hidden */
	} else {
		float ui_scale = std::max<float> (1.f, UIConfiguration::instance().get_ui_scale());
		int h = 100 * ui_scale;
		for (auto const& ui : _proc_uis) {
			h = std::max<int> (h, ui->get_preferred_height () + /* frame label */ 34 * ui_scale);
		}
		h = std::min<int> (h, 300 * ui_scale);
		_box.set_size_request (-1, h);
		_box.show ();
		_scroller.show ();
	}
	update_processor_box_visibility ();
	_idle_refill_processors_id = -1;
}
