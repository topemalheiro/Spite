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

#include "pbd/xml++.h"

#include "ardour/scene_command.h"
#include "ardour/session.h"

#include "pbd/i18n.h"

using namespace ARDOUR;

SceneCommand::SceneCommand (Session& s, Op op, uint32_t scene)
	: Command (op == Insert ? _("insert scene") : _("delete scene"))
	, _session (s)
	, _op (op)
	, _scene (scene)
{
}

SceneCommand::SceneCommand (Session& s, XMLNode const& node)
	: _session (s)
	, _op (Insert)
	, _scene (0)
{
	std::string op;
	if (node.get_property (X_("op"), op) && op == X_("remove")) {
		_op = Remove;
	}
	node.get_property (X_("scene"), _scene);
	set_name (_op == Insert ? _("insert scene") : _("delete scene"));
}

SceneCommand::~SceneCommand ()
{
	drop_references ();
}

void
SceneCommand::apply (Op op)
{
	if (op == Insert) {
		_session.insert_scene (_scene);
	} else {
		_session.remove_scene (_scene);
	}
}

void
SceneCommand::operator() ()
{
	apply (_op);
}

void
SceneCommand::undo ()
{
	apply (_op == Insert ? Remove : Insert);
}

XMLNode&
SceneCommand::get_state () const
{
	XMLNode* node = new XMLNode (X_("SceneCommand"));
	node->set_property (X_("op"), _op == Insert ? X_("insert") : X_("remove"));
	node->set_property (X_("scene"), _scene);
	return *node;
}
