/*
 * Copyright (C) 2008-2017 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2008-2018 David Robillard <d@drobilla.net>
 * Copyright (C) 2009-2012 Carl Hetherington <carl@carlh.net>
 * Copyright (C) 2012-2018 Robin Gareus <robin@gareus.org>
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

#ifdef WAF_BUILD
#include "gtk2ardour-config.h"
#endif

#include <ytkmm/stock.h>

#include "ardour/ardour.h"
#include "ardour/lv2_plugin.h"
#include "ardour/uri_map.h"
#include "ardour/plugin_insert.h"
#include "ardour/session.h"
#include "pbd/error.h"

#include "gui_thread.h"
#include "lv2_plugin_ui.h"
#include "timers.h"

#include "gtkmm2ext/utils.h"

#ifdef HAVE_LV2_1_18_6
#include <lv2/ui/ui.h>
#else
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>
#endif

#include <lv2/atom/atom.h>
#include <lv2/options/options.h>
#include <lilv/lilv.h>
#include <suil/suil.h>

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace Gtk;
using namespace PBD;

#define NS_UI "http://lv2plug.in/ns/extensions/ui#"

static SuilHost* ui_host = NULL;

void
LV2PluginUI::write_from_ui(void*       controller,
                           uint32_t    port_index,
                           uint32_t    buffer_size,
                           uint32_t    format,
                           const void* buffer)
{
	LV2PluginUI* me = (LV2PluginUI*)controller;
	if (format == 0) {
		if (port_index >= me->_controllables.size()) {
			return;
		}

		std::shared_ptr<AutomationControl> ac = me->_controllables[port_index];

		if (ac) {
			me->_updates.insert (port_index);
			ac->set_value(*(const float*)buffer, Controllable::NoGroup);
		}
	} else if (format == URIMap::instance().urids.atom_eventTransfer) {

		const int cnt = me->_pib->get_count();
		for (int i=0; i < cnt; i++ ) {
			std::shared_ptr<LV2Plugin> lv2i = std::dynamic_pointer_cast<LV2Plugin> (me->_pib->plugin(i));
			lv2i->write_from_ui(port_index, format, buffer_size, (const uint8_t*)buffer);
		}
	}
}

void
LV2PluginUI::write_to_ui(void*       controller,
                         uint32_t    port_index,
                         uint32_t    buffer_size,
                         uint32_t    format,
                         const void* buffer)
{
	LV2PluginUI* me = (LV2PluginUI*)controller;
	if (me->_inst) {
		suil_instance_port_event((SuilInstance*)me->_inst,
					 port_index, buffer_size, format, buffer);
	}
}

uint32_t
LV2PluginUI::port_index(void* controller, const char* symbol)
{
	return ((LV2PluginUI*)controller)->_lv2->port_index(symbol);
}

void
LV2PluginUI::touch(void*    controller,
                   uint32_t port_index,
                   bool     grabbed)
{
	LV2PluginUI* me = (LV2PluginUI*)controller;
	if (port_index >= me->_controllables.size()) {
		return;
	}
	if (!me->_lv2->parameter_is_control(port_index) || !me->_lv2->parameter_is_input(port_index)) {
		return;
	}

	ControllableRef control = me->_controllables[port_index];
	if (grabbed) {
		control->start_touch (timepos_t (control->session().transport_sample()));
	} else {
		control->stop_touch (timepos_t (control->session().transport_sample()));
	}
}

void
LV2PluginUI::set_path_property (int response,
                                const ParameterDescriptor& desc,
                                Gtk::FileChooserDialog*    widget)
{
	if (response == Gtk::RESPONSE_ACCEPT) {
		plugin->set_property (desc.key, Variant (Variant::PATH, widget->get_filename()));
	}
#if 0
	widget->hide ();
	delete_when_idle (widget);
#else
	delete widget;
#endif
	active_parameter_requests.erase (desc.key);
}

#ifdef HAVE_LV2_1_17_2
LV2UI_Request_Value_Status
LV2PluginUI::request_value(void*                     handle,
                           LV2_URID                  key,
                           LV2_URID                  type,
                           const LV2_Feature* const* features)
{
	LV2PluginUI* me = (LV2PluginUI*)handle;

	const ParameterDescriptor& desc (me->_lv2->get_property_descriptor(key));
	if (desc.key == (uint32_t)-1) {
		return LV2UI_REQUEST_VALUE_ERR_UNKNOWN;
	} else if (desc.datatype != Variant::PATH) {
		return LV2UI_REQUEST_VALUE_ERR_UNSUPPORTED;
	} else if (me->active_parameter_requests.count (key)) {
		return LV2UI_REQUEST_VALUE_BUSY;
	}
	me->active_parameter_requests.insert (key);

	Gtk::FileChooserDialog* lv2ui_file_dialog = new Gtk::FileChooserDialog(desc.label, FILE_CHOOSER_ACTION_OPEN);
	Gtkmm2ext::add_volume_shortcuts (*lv2ui_file_dialog);
	lv2ui_file_dialog->add_button (Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
	lv2ui_file_dialog->add_button (Gtk::Stock::OPEN, Gtk::RESPONSE_ACCEPT);
	lv2ui_file_dialog->set_default_response(Gtk::RESPONSE_ACCEPT);

	/* this assumes  announce_property_values() was called, or
	 * the plugin has previously sent a patch:Set */
	const Variant& value = me->_lv2->get_property_value (desc.key);
	if (value.type() == Variant::PATH) {
		lv2ui_file_dialog->set_filename (value.get_path());
	}

#if 0 // TODO mime-type, file-extension filter, get from LV2 Parameter Property
	FileFilter file_ext_filter;
	file_ext_filter.add_pattern ("*.foo");
	file_ext_filter.set_name ("Foo File");
	lv2ui_file_dialog.add_filter (file_ext_filter);
#endif

	lv2ui_file_dialog->signal_response().connect (sigc::bind (sigc::mem_fun (*me, &LV2PluginUI::set_path_property), desc, lv2ui_file_dialog));
	lv2ui_file_dialog->present();
	return LV2UI_REQUEST_VALUE_SUCCESS;
}
#endif

void
LV2PluginUI::update_timeout()
{
	_lv2->emit_to_ui(this, &LV2PluginUI::write_to_ui);
}

void
LV2PluginUI::on_external_ui_closed(void* controller)
{
	//printf("LV2PluginUI::on_external_ui_closed\n");
	LV2PluginUI* me = (LV2PluginUI*)controller;
	me->_screen_update_connection.disconnect();
	me->_message_update_connection.disconnect();
	me->_external_ui_ptr = NULL;
}

void
LV2PluginUI::control_changed (uint32_t port_index)
{
	/* Must run in GUI thread because we modify _updates with no lock */
	if (_controllables[port_index]->get_value () != _values_last_sent_to_ui[port_index]) {
		/* current plugin parameter does not match last value received
		   from GUI, so queue an update to push it to the GUI during
		   our regular timeout.
		*/
		_updates.insert (port_index);
	}
}

bool
LV2PluginUI::start_updating(GdkEventAny*)
{
	_screen_update_connection.disconnect();
	_screen_update_connection = Timers::super_rapid_connect
		(sigc::mem_fun(*this, &LV2PluginUI::output_update));
	return false;
}

bool
LV2PluginUI::stop_updating(GdkEventAny*)
{
	//cout << "stop_updating" << endl;
	_screen_update_connection.disconnect();
	return false;
}

void
LV2PluginUI::queue_port_update()
{
	const uint32_t num_ports = _lv2->num_ports();
	for (uint32_t i = 0; i < num_ports; ++i) {
		if (_lv2->parameter_is_control (i) && _lv2->parameter_is_input(i)) {
			_updates.insert (i);
		}
	}
}

void
LV2PluginUI::output_update()
{
	//std::cout << "output_update" << std::endl;
	if (_external_ui_ptr) {
		LV2_EXTERNAL_UI_RUN(_external_ui_ptr);
		if (_lv2->is_external_kx() && !_external_ui_ptr) {
			// clean up external UI if it closes itself via
			// on_external_ui_closed() during run()
			//printf("LV2PluginUI::output_update -- UI was closed\n");
			//_screen_update_connection.disconnect();
			_message_update_connection.disconnect();
			if (_inst) {
				suil_instance_free((SuilInstance*)_inst);
			}
			_inst = NULL;
			_external_ui_ptr = NULL;
			return;
		}
	}

	if (!_inst) {
		return;
	}

	/* output ports (values set by DSP) need propagating to GUI */

	for (auto const& index: _output_ports) {
		float val = _pib->control_output (index)->get_parameter ();

		if (val != _values_last_sent_to_ui[index]) {
			/* Send to GUI */
			suil_instance_port_event ((SuilInstance*)_inst, index, 4, 0, &val);
			/* Cache current value */
			_values_last_sent_to_ui[index] = val;
		}
	}

	/* Input ports marked for update because the control value changed
	 * since the last redisplay.
	 */

	for (auto const& i : _updates) {
		float val = _controllables[i]->get_value ();
		/* push current value to the GUI */
		suil_instance_port_event ((SuilInstance*)_inst, i, 4, 0, &val);
		_values_last_sent_to_ui[i] = val;
	}

	_updates.clear ();
}

LV2PluginUI::LV2PluginUI(std::shared_ptr<PlugInsertBase> pib,
                         std::shared_ptr<LV2Plugin>    lv2p)
	: PlugUIBase(pib)
	, _pib(pib)
	, _lv2(lv2p)
	, _gui_widget(NULL)
	, _values_last_sent_to_ui(NULL)
	, _external_ui_ptr(NULL)
	, _inst(NULL)
	, _embedded (false)
	, _face_scale (1.f)
	, _face_update_rate (60.f)
	, _face_options (NULL)
{
	_ardour_buttons_box.set_spacing (6);
	_ardour_buttons_box.set_border_width (6);

	add_common_widgets (&_ardour_buttons_box);

	plugin->PresetLoaded.connect (*this, invalidator (*this), std::bind (&LV2PluginUI::queue_port_update, this), gui_context ());
}

void
LV2PluginUI::lv2ui_instantiate(const std::string& title)
{
	bool          is_external_ui = _lv2->is_external_ui();
	LV2_Feature** features_src   = const_cast<LV2_Feature**>(_lv2->features());
	LV2_Feature** features       = const_cast<LV2_Feature**>(_lv2->features());
	size_t        features_count = 0;
	while (*features++) {
		++features_count;
	}

	if (is_external_ui) {
		features = (LV2_Feature**)malloc(sizeof(LV2_Feature*) * (features_count + 4));
	} else {
		features = (LV2_Feature**)malloc(sizeof(LV2_Feature*) * (features_count + 3));
	}

	size_t fi = 0;
	for (; fi < features_count; ++fi) {
		features[fi] = features_src[fi];
	}

#ifdef HAVE_LV2_1_17_2
	_lv2ui_request_value.handle  = this;
	_lv2ui_request_value.request = LV2PluginUI::request_value;
	_lv2ui_request_feature.URI   = LV2_UI__requestValue;
	_lv2ui_request_feature.data  = &_lv2ui_request_value;

	features[fi++] = &_lv2ui_request_feature;
#endif

	Gtk::Alignment* container = NULL;
	if (is_external_ui) {
		_external_ui_host.ui_closed       = LV2PluginUI::on_external_ui_closed;
		_external_ui_host.plugin_human_id = strdup(title.c_str());

		_external_ui_feature.URI  = LV2_EXTERNAL_UI_URI;
		_external_ui_feature.data = &_external_ui_host;

		_external_kxui_feature.URI  = LV2_EXTERNAL_UI_KX__Host;
		_external_kxui_feature.data = &_external_ui_host;

		features[fi++] = &_external_kxui_feature;
		features[fi++] = &_external_ui_feature;
	} else {
		if (_ardour_buttons_box.get_parent()) {
			_ardour_buttons_box.get_parent()->remove(_ardour_buttons_box);
		}
		if (!_embedded) {
			pack_start(_ardour_buttons_box, false, false);
			_ardour_buttons_box.show_all();
		}

		_gui_widget = Gtk::manage((container = new Gtk::Alignment()));
		pack_start(*_gui_widget, true, true);
		_gui_widget->show();
		if (_embedded) {
			/* suil (vendored): leave the host's keyboard focus and drag and drop alone */
			g_object_set_data (G_OBJECT (_gui_widget->gobj ()), "suil-embedded", GINT_TO_POINTER (1));
		}

		_parent_feature.URI  = LV2_UI__parent;
		_parent_feature.data = _gui_widget->gobj();

		features[fi++] = &_parent_feature;
	}

	features[fi] = NULL;

	if (_embedded) {
		/* the face's own options: a 60 Hz idle rate, so animated faces stay smooth (suil
		 * reads ui:updateRate), and a ui:scaleFactor of 1. The zoom is PgD4S's own: it fills
		 * spite_lv2_ui_face_height (), which is in X pixels and already has the UI scale in
		 * it. Spite's UI scale as well would zoom the face twice (JUCE scales the window by
		 * ui:scaleFactor), drawing it bigger than its place. */
		static float const face_ui_scale = 1.f;
		for (size_t i = 0; i < fi; ++i) {
			if (!features[i] || strcmp (features[i]->URI, LV2_OPTIONS__options)) {
				continue;
			}
			const LV2_Options_Option* src = (const LV2_Options_Option*) features[i]->data;
			size_t n = 0;
			while (src[n].key || src[n].value) {
				++n;
			}
			URIMap& uri_map = URIMap::instance ();
			uint32_t const scale_key = uri_map.uri_to_id (LV2_UI__scaleFactor);
			free (_face_options);
			_face_options = (LV2_Options_Option*) calloc (n + 2, sizeof (LV2_Options_Option));
			for (size_t k = 0; k < n; ++k) {
				_face_options[k] = src[k];
				if (src[k].key == scale_key) {
					_face_options[k].value = &face_ui_scale;
				}
			}
			_face_options[n].context = LV2_OPTIONS_INSTANCE;
			_face_options[n].key     = uri_map.uri_to_id (LV2_UI__updateRate);
			_face_options[n].size    = sizeof (float);
			_face_options[n].type    = uri_map.uri_to_id (LV2_ATOM__Float);
			_face_options[n].value   = &_face_update_rate;
			_face_options_feature.URI  = LV2_OPTIONS__options;
			_face_options_feature.data = _face_options;
			features[i] = &_face_options_feature;
		}
	}
#ifdef HAVE_LV2_1_17_2
	assert (fi == features_count + (is_external_ui ? 3 : 2));
#else
	assert (fi == features_count + (is_external_ui ? 2 : 1));
#endif

	if (!ui_host) {
		ui_host = suil_host_new(LV2PluginUI::write_from_ui,
		                        LV2PluginUI::port_index,
		                        NULL, NULL);
		suil_host_set_touch_func(ui_host, LV2PluginUI::touch);
	}
	const char* container_type = (is_external_ui)
		? NS_UI "external"
		: NS_UI "GtkUI";

	if (_lv2->has_message_output()) {
		_lv2->enable_ui_emission();
	}

	const LilvUI*   ui     = (const LilvUI*)_lv2->c_ui();
	const LilvNode* bundle = lilv_ui_get_bundle_uri(ui);
	const LilvNode* binary = lilv_ui_get_binary_uri(ui);
	char* ui_bundle_path = lilv_file_uri_parse(lilv_node_as_uri(bundle), NULL);
	char* ui_binary_path = lilv_file_uri_parse(lilv_node_as_uri(binary), NULL);
	if (!ui_bundle_path || !ui_binary_path) {
		error << _("failed to get path for UI bindle or binary") << endmsg;
		free(ui_bundle_path);
		free(ui_binary_path);
		free(features);
		return;
	}

	if (_embedded) {
		ARDOUR::set_lv2_ui_wants_face (true); /* PgD4S shows plugin mode, as a face */
	}
	_inst = suil_instance_new(
		ui_host,
		this,
		container_type,
		_lv2->uri(),
		lilv_node_as_uri(lilv_ui_get_uri(ui)),
		lilv_node_as_uri((const LilvNode*)_lv2->c_ui_type()),
		ui_bundle_path,
		ui_binary_path,
		features);
	if (_embedded) {
		ARDOUR::set_lv2_ui_wants_face (false);
	}

	free(ui_bundle_path);
	free(ui_binary_path);
	free(features);

	if (!_inst) {
		error << _("failed to instantiate LV2 GUI") << endmsg;
		return;
	}

#define GET_WIDGET(inst) suil_instance_get_widget((SuilInstance*)inst);

	_external_ui_ptr = NULL;
	if (!is_external_ui) {
		GtkWidget* c_widget = (GtkWidget*)GET_WIDGET(_inst);
		if (!c_widget) {
			error << _("failed to get LV2 UI widget") << endmsg;
			suil_instance_free((SuilInstance*)_inst);
			_inst = NULL;
			return;
		}
		if (!container->get_child()) {
			// Suil didn't add the UI to the container for us, so do it now
			container->add(*Gtk::manage(Glib::wrap(c_widget)));
		}
		container->show_all();
		gtk_widget_set_can_focus(c_widget, true);
		if (!_embedded) {
			gtk_widget_grab_focus(c_widget);
		}
	} else {
		_external_ui_ptr = (struct lv2_external_ui*)GET_WIDGET(_inst);
	}

	const uint32_t num_ports = _lv2->num_ports();

	_values_last_sent_to_ui = new float[num_ports];
	_controllables.resize(num_ports);

	for (uint32_t i = 0; i < num_ports; ++i) {
		if (!_lv2->parameter_is_control (i)) {
			continue;
		}

		/* Cache initial value of the parameter, regardless of whether it is input or output */

		_values_last_sent_to_ui[i] = _lv2->get_parameter(i);
		_controllables[i] = std::dynamic_pointer_cast<ARDOUR::AutomationControl> (_pib->control(Evoral::Parameter(PluginAutomation, 0, i)));

		if (_lv2->parameter_is_input(i)) {
			assert (_controllables[i]);
			_controllables[i]->Changed.connect (control_connections, invalidator (*this), std::bind (&LV2PluginUI::control_changed, this, i), gui_context());
			/* queue for first update ("push") to GUI */
			_updates.insert (i);
		}

		if (_lv2->parameter_is_output(i) && is_update_wanted(i)) {
			_output_ports.push_back (i);
			_values_last_sent_to_ui[i] -= 1; // force update
		}
	}

	if (_lv2->has_message_output()) {
		_message_update_connection = Timers::super_rapid_connect (
			sigc::mem_fun(*this, &LV2PluginUI::update_timeout));
	}
}

void
LV2PluginUI::grab_focus()
{
	if (_inst && !_lv2->is_external_ui()) {
		GtkWidget* c_widget = (GtkWidget*)GET_WIDGET(_inst);
		gtk_widget_grab_focus(c_widget);
	}
}

void
LV2PluginUI::lv2ui_free()
{
	stop_updating (0);

	/* Spite: removing the container destroys the plugin's X window along with its
	 * parent, then the plugin destroys it again (JUCE does): trap the BadWindow that
	 * follows, or GDK's handler takes Spite down at the next XSync */
	gdk_error_trap_push ();

	if (_gui_widget) {
		remove (*_gui_widget);
		_gui_widget = NULL;
	}

	if (_inst) {
		suil_instance_free((SuilInstance*)_inst);
		_inst = NULL;
	}

	gdk_flush ();
	gdk_error_trap_pop ();
}

void
LV2PluginUI::set_embedded (float scale)
{
	_embedded   = true;
	_face_scale = scale;
}

void
LV2PluginUI::embed_show ()
{
	if (!_inst) {
		lv2ui_instantiate ("");
	}
	if (_inst) {
		start_updating (0);
	}
}

void
LV2PluginUI::embed_hide ()
{
	lv2ui_free ();
}

void
LV2PluginUI::set_face_scale (float scale)
{
	if (fabsf (scale - _face_scale) < 0.005f) {
		return;
	}
	_face_scale = scale;
	if (!_inst) {
		return;
	}
	/* the GUI rescales itself (LV2 options interface) and asks for its new size */
	const LV2_Options_Interface* iface = (const LV2_Options_Interface*) suil_instance_extension_data ((SuilInstance*)_inst, LV2_OPTIONS__interface);
	if (iface && iface->set) {
		URIMap&            uri_map = URIMap::instance ();
		LV2_Options_Option opts[2];
		memset (opts, 0, sizeof (opts));
		opts[0].context = LV2_OPTIONS_INSTANCE;
		opts[0].key     = uri_map.uri_to_id (LV2_UI__scaleFactor);
		opts[0].size    = sizeof (float);
		opts[0].type    = uri_map.uri_to_id (LV2_ATOM__Float);
		opts[0].value   = &_face_scale;
		iface->set (suil_instance_get_handle ((SuilInstance*)_inst), opts);
	}
}

LV2PluginUI::~LV2PluginUI ()
{
	delete [] _values_last_sent_to_ui;
	free (_face_options);

	_message_update_connection.disconnect();
	_screen_update_connection.disconnect();

	if (_external_ui_ptr && _lv2->is_external_kx()) {
		LV2_EXTERNAL_UI_HIDE(_external_ui_ptr);
	}
	lv2ui_free();
	_external_ui_ptr = NULL;
}

int
LV2PluginUI::get_preferred_height()
{
	Gtk::Requisition r = size_request();
	return r.height;
}

int
LV2PluginUI::get_preferred_width()
{
	Gtk::Requisition r = size_request();
	return r.width;
}

bool
LV2PluginUI::resizable()
{
	return _lv2->ui_is_resizable();
}

int
LV2PluginUI::package(Gtk::Window& win)
{
	if (!_external_ui_ptr) {
		/* forward configure events to plugin window */
		win.signal_configure_event().connect(
			sigc::mem_fun(*this, &LV2PluginUI::configure_handler));
		win.signal_map_event().connect(
			sigc::mem_fun(*this, &LV2PluginUI::start_updating));
		win.signal_unmap_event().connect(
			sigc::mem_fun(*this, &LV2PluginUI::stop_updating));
	}
	return 0;
}

bool
LV2PluginUI::configure_handler(GdkEventConfigure*)
{
	std::cout << "CONFIGURE" << std::endl;
	return false;
}

bool
LV2PluginUI::is_update_wanted(uint32_t /*index*/)
{
	/* FIXME: use port notification properties
	   and/or new UI extension subscription methods
	*/
	return true;
}

bool
LV2PluginUI::on_window_show(const std::string& title)
{
	//std::cout << "on_window_show - " << title << std::endl;// flush(cout);

	if (_lv2->is_external_ui()) {
		if (_external_ui_ptr) {
			_screen_update_connection.disconnect();
			_message_update_connection.disconnect();
			LV2_EXTERNAL_UI_SHOW(_external_ui_ptr);
			_screen_update_connection = Timers::super_rapid_connect
		        (sigc::mem_fun(*this, &LV2PluginUI::output_update));
			if (_lv2->has_message_output()) {
				_message_update_connection = Timers::super_rapid_connect (
					sigc::mem_fun(*this, &LV2PluginUI::update_timeout));
			}
			return false;
		}
		lv2ui_instantiate(title);
		if (!_external_ui_ptr) {
			return false;
		}

		_screen_update_connection.disconnect();
		_message_update_connection.disconnect();
		LV2_EXTERNAL_UI_SHOW(_external_ui_ptr);
		_screen_update_connection = Timers::super_rapid_connect
			(sigc::mem_fun(*this, &LV2PluginUI::output_update));
		if (_lv2->has_message_output()) {
			_message_update_connection = Timers::super_rapid_connect (
				sigc::mem_fun(*this, &LV2PluginUI::update_timeout));
		}
		return false;
	} else {
		lv2ui_instantiate("gtk2gui");
	}

	return _inst ? true : false;
}

void
LV2PluginUI::on_window_hide()
{
	//printf("LV2PluginUI::on_window_hide\n");

	if (_lv2->is_external_ui()) {
		if (!_external_ui_ptr) { return; }
		LV2_EXTERNAL_UI_HIDE(_external_ui_ptr);
		if (!_lv2->is_external_kx()) { return ; }
		_message_update_connection.disconnect();
		_screen_update_connection.disconnect();
		_external_ui_ptr = NULL;
		suil_instance_free((SuilInstance*)_inst);
		_inst = NULL;
	} else {
		lv2ui_free();
	}
}

bool
LV2PluginUI::is_external () const
{
	return _lv2->is_external_ui ();
}

bool
LV2PluginUI::is_external_visible () const
{
	return _external_ui_ptr != 0;
}
