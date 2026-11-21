/*
 * Spite's shared look: the sizes and named colours that every new view (the macro configurer, the
 * device button matrices, ...) uses, so they match the rest of the app without copying numbers.
 * The reasons for each value are in ~/.local/share/claude-spite/look/style-guide.md (and the
 * plugdata values they come from in reference.md). Header only.
 *
 * Colours are looked up by name at paint time, so a theme change repaints them correctly:
 *   Gtkmm2ext::Color c = SpiteLook::color (SpiteLook::Panel);
 */

#pragma once

#include "gtkmm2ext/colors.h"

#include "ui_config.h"

namespace SpiteLook {

/* sizes, in pixels at ui-scale 1 (multiply with UIConfiguration::get_ui_scale ()) */
static const float corner_radius = 4.5f; /* buttons, panels, entries: plugdata's 5 */
static const float outline_width = 1.0f; /* every border, drawn inside */
static const float knob_line     = 2.0f; /* minimum stroke of a knob; 10% of its size above 20 px */
static const float gap           = 4.0f; /* between controls in a row */
static const float pad           = 6.0f; /* inside a panel */

/* roles, each a named colour of the theme (so every theme can answer them) */
enum Role {
	Window,     /* the page background (plugdata canvas, #232323) */
	Panel,      /* a raised group or strip (#2c2c2c) */
	Bar,        /* toolbars, rulers, trough areas, menus (#191919) */
	Control,    /* a button's fill (#303030) */
	Outline,    /* borders (#393939) */
	Text,       /* #e1e1e1 */
	TextDim,    /* secondary text, scene numbers (#b0b0b0) */
	Accent,     /* value arcs, slider fills, focus (#42a2c8) */
	AccentFill, /* the accent as a fill under light text: active buttons, selection (#2b6a84) */
	Warning     /* #ff8500 */
};

inline char const*
role_name (Role r)
{
	switch (r) {
		case Window:     return "theme:bg";
		case Panel:      return "theme:bg1";
		case Bar:        return "theme:bg2";
		case Control:    return "widget:bg";
		case Outline:    return "neutral:backgroundest";
		case Text:       return "neutral:foreground";
		case TextDim:    return "neutral:foreground2";
		case Accent:     return "widget:blue";
		case AccentFill: return "widget:accent dim"; /* only the Spite theme defines it: falls back below */
		case Warning:    return "alert:orange";
	}
	return "theme:bg";
}

inline Gtkmm2ext::Color
color (Role r)
{
	UIConfiguration& cfg = UIConfiguration::instance ();
	bool failed = false;
	Gtkmm2ext::Color c = cfg.color (role_name (r), &failed);
	if (failed && r == AccentFill) {
		/* other themes have no "widget:accent dim": their selection colour is the nearest equivalent */
		return cfg.color ("theme:contrasting selection");
	}
	return c;
}

} /* namespace SpiteLook */
