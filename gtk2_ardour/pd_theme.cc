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

#include <algorithm>
#include <cmath>

#include "pd_theme.h"

using namespace PdColor;

namespace {

int
clamp_unit (int v)
{
	return std::min (255, std::max (0, v));
}

/* v / 256, rounded down also for negative v (an arithmetic shift) */
int
div256 (int v)
{
	return v >= 0 ? v / 256 : -((-v + 255) / 256);
}

/* as JUCE's PixelARGB::premultiply (juce_PixelFormats.h:240-258), which divides by 256 */
int
premultiplied (int v, int a)
{
	return a == 255 ? v : (v * a + 0x7f) >> 8;
}

}

Color
PdColor::rgba (int r, int g, int b, int a)
{
	return ((uint32_t) clamp_unit (r) << 24) | ((uint32_t) clamp_unit (g) << 16) | ((uint32_t) clamp_unit (b) << 8) | (uint32_t) clamp_unit (a);
}

Color
PdColor::from_argb (uint32_t argb)
{
	return (argb << 8) | (argb >> 24);
}

Color
PdColor::with_alpha (Color c, float a)
{
	/* to the nearest unit, halves to even, as plugdata rounds */
	return (c & 0xffffff00) | (uint32_t) clamp_unit ((int) lrintf (std::min (1.f, std::max (0.f, a)) * 255.f));
}

Color
PdColor::brighter (Color c, float amount)
{
	float const k = 1.f / (1.f + amount);
	return rgba ((int) (255.f - k * (255 - red (c))),
	             (int) (255.f - k * (255 - green (c))),
	             (int) (255.f - k * (255 - blue (c))),
	             alpha (c));
}

Color
PdColor::darker (Color c, float amount)
{
	float const k = 1.f / (1.f + amount);
	return rgba ((int) (k * red (c)), (int) (k * green (c)), (int) (k * blue (c)), alpha (c));
}

float
PdColor::perceived_brightness (Color c)
{
	float const r = red (c) / 255.f;
	float const g = green (c) / 255.f;
	float const b = blue (c) / 255.f;
	return sqrtf (0.241f * r * r + 0.691f * g * g + 0.068f * b * b);
}

Color
PdColor::overlaid (Color base, Color over)
{
	int const base_a = alpha (base);
	if (base_a <= 0) {
		return over;
	}

	int const keep = 255 - alpha (over);
	int const out_a = 255 - (((255 - base_a) * keep) >> 8);
	if (out_a <= 0) {
		return base;
	}

	/* how much of the base shows through, out of 256 */
	int const k = (keep * base_a) / out_a;

	return rgba (red (over) + div256 ((red (base) - red (over)) * k),
	             green (over) + div256 ((green (base) - green (over)) * k),
	             blue (over) + div256 ((blue (base) - blue (over)) * k),
	             out_a);
}

Color
PdColor::contrasting (Color c, float amount)
{
	Color const ink = perceived_brightness (c) >= 0.5f ? rgba (0, 0, 0) : rgba (255, 255, 255);
	return overlaid (c, with_alpha (ink, amount));
}

Color
PdColor::interpolated (Color c, Color other, float proportion)
{
	if (proportion <= 0.f) {
		return c;
	}
	if (proportion >= 1.f) {
		return other;
	}

	int const amount = (int) lrintf (proportion * 255.f);

	/* tween the premultiplied colours, then take the alpha out again */
	int const a0 = alpha (c);
	int const a1 = alpha (other);
	int const p0[4] = { premultiplied (red (c), a0), premultiplied (green (c), a0), premultiplied (blue (c), a0), a0 };
	int const p1[4] = { premultiplied (red (other), a1), premultiplied (green (other), a1), premultiplied (blue (other), a1), a1 };

	int q[4];
	for (int i = 0; i < 4; ++i) {
		q[i] = p0[i] + div256 ((p1[i] - p0[i]) * amount);
	}

	int const a = q[3];
	if (a <= 0) {
		return rgba (0, 0, 0, 0);
	}
	if (a < 255) {
		for (int i = 0; i < 3; ++i) {
			q[i] = std::min (255, q[i] * 255 / a);
		}
	}
	return rgba (q[0], q[1], q[2], a);
}

Color
PdColor::scaled_rgb (Color c, float k)
{
	return rgba ((int) (red (c) * k), (int) (green (c) * k), (int) (blue (c) * k), alpha (c));
}

void
PdColor::set_source (cairo_t* cr, Color c)
{
	cairo_set_source_rgba (cr, red (c) / 255., green (c) / 255., blue (c) / 255., alpha (c) / 255.);
}

void
PdColor::set_source (cairo_t* cr, Color c, double alpha_scale)
{
	cairo_set_source_rgba (cr, red (c) / 255., green (c) / 255., blue (c) / 255., alpha (c) / 255. * alpha_scale);
}

/* plugdata's two default themes (LookAndFeel.cpp, the "dark" and "light" entries of
 * defaultThemesJSON); plugdata starts in "light" */

PdTheme const&
PdTheme::dark ()
{
	static PdTheme const t = {
		from_argb (0xff232323), /* canvas_background */
		from_argb (0xffe1e1e1), /* canvas_text */
		from_argb (0xff7f7f7f), /* canvas_dots */
		from_argb (0xff555555), /* object_outline_colour */
		from_argb (0xff42a2c8), /* selected_object_outline_colour */
		from_argb (0xff393939), /* outline_colour */
		from_argb (0xff42a2c8), /* data_colour */
		from_argb (0xffff8500), /* signal_colour */
		from_argb (0xffe1e1e1), /* connection_colour */
		from_argb (0xffe1e1e1), /* toolbar_text */
		from_argb (0xff42a2c8), /* toolbar_active */
		from_argb (0xff2c2c2c), /* panel_background */
		from_argb (0xffe1e1e1), /* panel_text */
		from_argb (0xff191919), /* dialog_background */
		1,                      /* connection_style */
		false,                  /* straight_connections */
		false                   /* square_iolets */
	};
	return t;
}

PdTheme const&
PdTheme::light ()
{
	static PdTheme const t = {
		from_argb (0xfffafafa),
		from_argb (0xff4d4d4d),
		from_argb (0xff909090),
		from_argb (0xffb7b7b7),
		from_argb (0xff007aff),
		from_argb (0xffd0d0d0),
		from_argb (0xff007aff),
		from_argb (0xffff8500),
		from_argb (0xffb3b3b3),
		from_argb (0xff373737),
		from_argb (0xff007aff),
		from_argb (0xfff7f7f7),
		from_argb (0xff373737),
		from_argb (0xffebebeb),
		1,
		false,
		false
	};
	return t;
}

Gtkmm2ext::Color
PdTheme::band () const
{
	return with_alpha (contrasting (canvas_background, 0.06f), 0.24f);
}

Gtkmm2ext::Color
PdTheme::audio_dash () const
{
	/* plugdata scales the band's 8 bit channels and makes it opaque */
	return (scaled_rgb (band (), 0.4f) & 0xffffff00) | 0xff;
}

Gtkmm2ext::Color
PdTheme::locked_jack () const
{
	return contrasting (canvas_background, 0.5f);
}

Gtkmm2ext::Color
PdTheme::grid_dots () const
{
	return interpolated (canvas_dots, canvas_background, 0.2f);
}

Gtkmm2ext::Color
PdTheme::bubble_fill () const
{
	return with_alpha (canvas_background, 0.95f);
}

Gtkmm2ext::Color
PdTheme::badge_text () const
{
	return contrasting (selected_object_outline);
}
