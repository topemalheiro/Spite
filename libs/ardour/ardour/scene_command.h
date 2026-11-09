/*
 * Copyright (C) 2026 Spite contributors
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

#pragma once

#include <cstdint>

#include "pbd/command.h"

#include "ardour/libardour_visibility.h"

namespace ARDOUR {

class Session;

/* Spite: undo for inserting or removing a scene (Session::insert_scene (),
 * Session::remove_scene ()). Only empty scenes are ever removed, so undoing
 * an insert does nothing once the scene has a clip.
 */
class LIBARDOUR_API SceneCommand : public PBD::Command
{
public:
	enum Op {
		Insert,
		Remove
	};

	SceneCommand (Session&, Op, uint32_t scene);
	SceneCommand (Session&, XMLNode const&);
	~SceneCommand ();

	void operator() ();
	void undo ();

	XMLNode& get_state () const;

private:
	Session& _session;
	Op       _op;
	uint32_t _scene;

	void apply (Op);
};

} // namespace ARDOUR
