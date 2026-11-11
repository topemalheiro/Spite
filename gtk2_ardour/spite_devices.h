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

#pragma once

#include <memory>
#include <string>

namespace ARDOUR {
	class LV2Plugin;
	class PluginInsert;
	class Processor;
}

namespace Spite {

/* Which devices show a live face of their own: PgD4S (plugdata for Spite) does,
 * stock plugdata, which shares its plugin URIs, does not.
 *
 * The version of the face protocol the plugin's loaded binary speaks: what its
 * exported pgd4s_spite_face_version () returns, or 1 for a PgD4S build older
 * than that symbol (its doap:name starts with "PgD4S"); 0 for anything else.
 * The binary is asked, not the URI: lilv loads one bundle per URI, so a newer
 * stock plugdata in LV2_PATH silently takes PgD4S's place. Cached per library. */
int pgd4s_face_version (std::shared_ptr<ARDOUR::LV2Plugin>);

/* the device shows its own GUI as its face (the Device view, the MCR) */
bool has_device_face (std::shared_ptr<ARDOUR::PluginInsert>);

/* the devices a track shows (the Device view, the MCR): its plugins, but no
 * processor that is hidden or internal */
bool is_device (std::shared_ptr<ARDOUR::Processor>);

/* the device's name, as Live shows it: a converted Max for Live device's own
 * name (its preset is "M4L: <name>"), else the processor's name */
std::string device_name (std::shared_ptr<ARDOUR::Processor>);

/* open the device's own window: its custom GUI (for PgD4S, the patch editor)
 * or its generic controls. An open window is raised, never closed. */
void open_device_window (std::shared_ptr<ARDOUR::Processor>, bool custom);

} /* namespace Spite */
