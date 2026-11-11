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
#include <cstring>
#include <map>
#include <string>

#include <dlfcn.h>

#include <lilv/lilv.h>

#include <ytkmm/window.h>

#include "ardour/lv2_plugin.h"
#include "ardour/plugin_insert.h"

#include "processor_box.h"
#include "spite_devices.h"
#include "utils.h"

/* X last: its macros (None...) clash with names in the headers above */
#ifdef GDK_WINDOWING_X11
#include <X11/Xatom.h>
#include <ydk/gdkx.h>
#endif

using namespace ARDOUR;

/* PgD4S keeps plugdata's URIs (old sessions keep loading), so they only say
 * "plugdata or PgD4S" */
static char const* const plugdata_uri = "https://github.com/timothyschoen/plugdata";

int
Spite::pgd4s_face_version (std::shared_ptr<LV2Plugin> lv2)
{
	if (!lv2 || std::string (lv2->uri ()).compare (0, strlen (plugdata_uri), plugdata_uri) != 0) {
		return 0;
	}

	/* by the binary lilv loaded, which never changes while it is loaded */
	static std::map<std::string, int> versions;

	std::string path;
	if (LilvNode const* lib = lilv_plugin_get_library_uri ((LilvPlugin const*) lv2->c_plugin ())) {
		if (char* p = lilv_file_uri_parse (lilv_node_as_uri (lib), NULL)) {
			path = p;
			lilv_free (p);
		}
	}

	if (!path.empty ()) {
		std::map<std::string, int>::const_iterator i = versions.find (path);
		if (i != versions.end ()) {
			return i->second;
		}
		/* lilv loads plugins RTLD_LOCAL, so RTLD_DEFAULT can't see their
		 * symbols: ask the library itself, which an instance keeps loaded */
		if (void* h = dlopen (path.c_str (), RTLD_NOW | RTLD_NOLOAD)) {
			int v = 0;
			if (void* f = dlsym (h, "pgd4s_spite_face_version")) {
				v = ((int (*) ()) f) ();
			}
			dlclose (h);
			if (v <= 0 && std::string (lv2->name ()).compare (0, 5, "PgD4S") == 0) {
				v = 1; /* PgD4S from before the symbol */
			}
			versions[path] = std::max (0, v);
			return versions[path];
		}
	}

	/* the library could not be asked (not loaded): its name alone, not kept */
	return std::string (lv2->name ()).compare (0, 5, "PgD4S") == 0 ? 1 : 0;
}

bool
Spite::has_device_face (std::shared_ptr<PluginInsert> pi)
{
	std::shared_ptr<LV2Plugin> lv2 = pi ? std::dynamic_pointer_cast<LV2Plugin> (pi->plugin ()) : std::shared_ptr<LV2Plugin> ();
	return lv2 && lv2->has_editor () && !lv2->is_external_ui () && pgd4s_face_version (lv2) > 0;
}

bool
Spite::is_device (std::shared_ptr<Processor> p)
{
	std::shared_ptr<PluginInsert> pi = std::dynamic_pointer_cast<PluginInsert> (p);
	if (!pi || !pi->display_to_user ()) {
		return false;
	}
#ifdef MIXBUS
	if (pi->channelstrip () != Processor::None) {
		return false;
	}
#endif
	return true;
}

std::string
Spite::device_name (std::shared_ptr<Processor> p)
{
	if (!p) {
		return std::string ();
	}
	std::shared_ptr<PluginInsert> pi  = std::dynamic_pointer_cast<PluginInsert> (p);
	std::shared_ptr<LV2Plugin>    lv2 = pi ? std::dynamic_pointer_cast<LV2Plugin> (pi->plugin ()) : std::shared_ptr<LV2Plugin> ();
	if (lv2) {
		std::string const label = lv2->last_preset ().label;
		if (label.compare (0, 5, "M4L: ") == 0 && label.size () > 5) {
			return label.substr (5);
		}
	}
	return p->display_name ();
}

void
Spite::open_device_window (std::shared_ptr<Processor> p, bool custom)
{
	/* as ProcessorBox::edit_processor () does, but showing, never toggling (and, as
	 * the Device view always did, not kept over the window it was opened from) */
	if (!p || !ARDOUR_UI_UTILS::engine_is_running ()) {
		return;
	}
	ProcessorWindowProxy* wp = p->window_proxy (); /* every Mixer strip's ProcessorBox makes them */
	if (!wp) {
		return;
	}
	wp->set_custom_ui_mode (custom);
	wp->show_the_right_window (true);

	/* show_the_right_window () leaves a window it takes for fully visible as it is,
	 * and under a compositor (Xwayland always is one) every mapped window says it
	 * is unobscured, even behind Spite's: raise it. (An external UI's window is
	 * never mapped: it has none to raise.) */
	if (Gtk::Window* w = wp->get (false)) {
		if (w->get_mapped ()) {
			w->present ();
		}
	}
}

void
Spite::reclaim_keyboard (Gtk::Widget& w)
{
#ifdef GDK_WINDOWING_X11
	Gtk::Widget* const top = w.get_toplevel ();
	if (!top || !top->get_realized () || !top->get_window ()) {
		return;
	}
	GdkWindow* const  gtop = top->get_window ()->gobj ();
	GdkDisplay* const gdpy = gdk_drawable_get_display (gtop);
	::Display* const  dpy  = GDK_WINDOW_XDISPLAY (gtop);
	::Window const    txid = GDK_WINDOW_XID (gtop);

	::Window focus  = None;
	int      revert = 0;
	XGetInputFocus (dpy, &focus, &revert);
	if (focus == None || focus == PointerRoot || gdk_xid_table_lookup_for_display (gdpy, focus)) {
		return; /* nowhere, or a window GTK knows (its own focus window among them) */
	}

	/* a window GTK doesn't know: a plugin's, in this toplevel? */
	bool inside = false;
	gdk_error_trap_push ();
	for (::Window x = focus; x;) {
		::Window     root     = None;
		::Window     parent   = None;
		::Window*    children = 0;
		unsigned int n        = 0;
		if (!XQueryTree (dpy, x, &root, &parent, &children, &n)) {
			break;
		}
		if (children) {
			XFree (children);
		}
		if (parent == txid) {
			inside = true;
			break;
		}
		x = (parent == root) ? None : parent;
	}
	gdk_error_trap_pop ();
	if (!inside) {
		return;
	}

	/* back to where the window manager gives it to Spite: GDK's focus window, which GDK
	 * names as the toplevel's _NET_WM_USER_TIME_WINDOW (else the toplevel itself) */
	::Window       to     = txid;
	Atom           type   = None;
	int            format = 0;
	unsigned long  items  = 0;
	unsigned long  after  = 0;
	unsigned char* data   = 0;
	if (XGetWindowProperty (dpy, txid, gdk_x11_get_xatom_by_name_for_display (gdpy, "_NET_WM_USER_TIME_WINDOW"), 0, 1, False, XA_WINDOW,
	                        &type, &format, &items, &after, &data) == Success && data) {
		if (type == XA_WINDOW && format == 32 && items == 1) {
			to = *(::Window*) data;
		}
		XFree (data);
	}

	/* The window manager may unmap the window just now (minimize it): X then fails, and
	 * its error comes back only once X has done the request. It is waited for (a round
	 * trip, only when a plugin had the keyboard): an error after the trap ends Spite. */
	gdk_error_trap_push ();
	XSetInputFocus (dpy, to, RevertToParent, CurrentTime);
	gdk_flush ();
	gdk_error_trap_pop ();
#endif
}
