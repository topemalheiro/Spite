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

/* Spite: inserting and deleting scenes from the GUI, with undo. A scene is a
 * row of clip slots across every track (Session::scene_count ()).
 */
namespace SceneActions {

/* Insert Scene (Common/insert-scene, Ctrl+I), like Live's: an empty scene
 * below the selected one, which is the row of the last selected clip slot,
 * or the scene last clicked in the scene column; at the end if neither.
 */
void insert_scene ();
void insert_scene_at (uint32_t at);

/* only an empty scene can be deleted */
bool can_delete_scene (uint32_t at);
void delete_scene (uint32_t at);

/* the scene column reports clicks on its scenes here; -1 for none */
void set_selected_scene (int32_t);

}
