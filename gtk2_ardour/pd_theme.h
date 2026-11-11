/*
 * Copyright (C) 2021-2025 Timothy Schoen (plugdata)
 * Copyright (C) 2026 Spite
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
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

/* plugdata's canvas colours (its "dark" and "light" themes, LookAndFeel.cpp)
 * and the colour maths its cords, jacks and grid derive their colours with.
 * Cairo only: no GTK, so the MCR and a plain test program can both use it.
 */

#pragma once

#include <cstdint>

#include <cairo.h>

namespace Gtkmm2ext {
/* the same type as gtkmm2ext/colors.h (RGBA, red in the top byte), declared
 * again so that this file does not pull in that header's ydkmm and cairomm */
typedef uint32_t Color;
}

/* Colour maths with the results of plugdata's (JUCE's Colour), so derived
 * colours come out as in plugdata, to the unit. */
namespace PdColor {

typedef Gtkmm2ext::Color Color;

inline int red (Color c) { return (c >> 24) & 0xff; }
inline int green (Color c) { return (c >> 16) & 0xff; }
inline int blue (Color c) { return (c >> 8) & 0xff; }
inline int alpha (Color c) { return c & 0xff; }

Color rgba (int r, int g, int b, int a = 255);
/* plugdata's theme files write colours as ARGB, "ff232323" */
Color from_argb (uint32_t argb);

Color with_alpha (Color, float a);            /* a in 0..1, rounded to the nearest unit */
Color brighter (Color, float amount = 0.4f);  /* 255 - (255 - c) / (1 + amount) */
Color darker (Color, float amount = 0.4f);    /* c / (1 + amount) */
float perceived_brightness (Color);           /* 0..1 */
Color overlaid (Color base, Color over);      /* "over" painted on "base" */
Color contrasting (Color, float amount = 1.0f); /* black or white at amount, over the colour */
Color interpolated (Color, Color other, float proportion);
Color scaled_rgb (Color, float k);            /* r, g, b times k, cut to whole units; alpha kept */

void set_source (cairo_t*, Color);
void set_source (cairo_t*, Color, double alpha_scale);

}

struct PdTheme
{
	Gtkmm2ext::Color canvas_background;
	Gtkmm2ext::Color canvas_text;
	Gtkmm2ext::Color canvas_dots;
	Gtkmm2ext::Color object_outline;          /* jack rings, badge and arrow outlines */
	Gtkmm2ext::Color selected_object_outline;
	Gtkmm2ext::Color outline;                 /* Detective bubble border */
	Gtkmm2ext::Color data;                    /* data jacks, selected data cords, the Detective dot */
	Gtkmm2ext::Color signal;                  /* audio jacks, selected audio cords */
	Gtkmm2ext::Color connection;              /* every cord at rest */
	Gtkmm2ext::Color toolbar_text;
	Gtkmm2ext::Color toolbar_active;
	Gtkmm2ext::Color panel_background;
	Gtkmm2ext::Color panel_text;
	Gtkmm2ext::Color dialog_background;
	int  connection_style;                    /* PdCord::Style: 1 default, 2 vanilla, 3 thin */
	bool straight_connections;
	bool square_iolets;

	static PdTheme const& dark ();
	static PdTheme const& light ();

	/* derived colours, worked out as plugdata does */
	Gtkmm2ext::Color band () const;        /* the cord's outer band (Connection.cpp:235) */
	Gtkmm2ext::Color audio_dash () const;  /* the dashes on audio cords (Connection.cpp:271-277) */
	Gtkmm2ext::Color locked_jack () const; /* jacks in Play mode (Iolet.cpp:81) */
	Gtkmm2ext::Color grid_dots () const;   /* the grid's dots (Canvas.cpp:510) */
	Gtkmm2ext::Color bubble_fill () const; /* Detective bubbles (Connection.cpp:1723) */
	Gtkmm2ext::Color badge_text () const;  /* the number in an order badge (Connection.cpp:238) */
};
