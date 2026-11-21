/*
 * Copyright (C) 2010 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2017-2021 Robin Gareus <robin@gareus.org>
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

#include <pangomm/layout.h>

#include "pbd/compose.h"

#include "gtkmm2ext/rgb_macros.h"
#include "gtkmm2ext/utils.h"

#include "widgets/ardour_knob.h"
#include "gtkmm2ext/ui_config.h"

#include "pbd/i18n.h"

using namespace ArdourWidgets;
using namespace Gtkmm2ext;
using namespace PBD;

ArdourKnob::Element ArdourKnob::default_elements = ArdourKnob::Element (ArdourKnob::Arc);

ArdourKnob::ArdourKnob (Element e, Flags flags)
	: ArdourCtrlBase (flags)
	, _elements (e)
	, _extra_height (0)
{
}

void
ArdourKnob::gen_faceplate (Pango::FontDescription const& font,
                           std::string const&            lbl_left,
                           std::string const&            lbl_right,
                           std::string const&            caption)
{
	_bg.clear ();
	if (lbl_left.empty () && lbl_right.empty () && caption.empty ()) {
		_extra_height = 0;
		queue_resize ();
		return;
	}

	Gtk::Requisition req;
	on_size_request (&req);

	Glib::RefPtr<Pango::Layout> layout = Pango::Layout::create (get_pango_context ());
	layout->set_font_description (font);

	int extra_width  = 0;
	int extra_height = 0;
	int lbl_pos      = req.width * .2; // ~65 deg
	int lbl_max      = req.width * .4;

	int lbl_left_x  = req.width * .2;
	int lbl_right_x = req.width * .8;
	int lbl_lr_y    = req.height * .9;

	if (!lbl_left.empty ()) {
		int w, h;
		layout->set_text (lbl_left);
		layout->get_pixel_size (w, h);
		extra_height = std::max (extra_height, h - req.height / 4);
		if (2 * w > lbl_max) {
			/* label is too long to center */
			extra_width = std::max (extra_width, w - lbl_max);
			// TODO set lbl_left_x, right-align text
		} else {
			extra_width = std::max (extra_width, w / 2 - lbl_pos);
		}
	}

	if (!lbl_right.empty ()) {
		int w, h;
		layout->set_text (lbl_right);
		layout->get_pixel_size (w, h);
		extra_height = std::max (extra_height, h - 1);
		if (2 * w > lbl_max) {
			/* label is too long to center */
			extra_width = std::max (extra_width, w - lbl_max);
			//TODO set lbl_right_x, left-align text
		} else {
			extra_width = std::max (extra_width, w / 2 - lbl_pos);
		}
	}

	if (!caption.empty ()) {
		int w, h;
		layout->set_text (caption);
		layout->get_pixel_size (w, h);
		extra_height += h;
		extra_width = std::max (extra_width, w - req.width);
	}

	req.width += extra_width;
	req.height += extra_height;

	_bg = Cairo::ImageSurface::create (Cairo::FORMAT_ARGB32, req.width, req.height);
	Cairo::RefPtr<Cairo::Context> cr = Cairo::Context::create (_bg);

	Gtkmm2ext::Color txt_color = UIConfigurationBase::instance ().color ("gtk_foreground");
	Gtkmm2ext::set_source_rgba (cr, txt_color);

	if (!lbl_left.empty ()) {
		int w, h;
		layout->set_text (lbl_left);
		layout->get_pixel_size (w, h);
		cr->move_to (lbl_left_x - w * .5, lbl_lr_y);
		layout->show_in_cairo_context (cr);
	}

	if (!lbl_right.empty ()) {
		int w, h;
		layout->set_text (lbl_right);
		layout->get_pixel_size (w, h);
		cr->move_to (lbl_right_x - w * .5, lbl_lr_y);
		layout->show_in_cairo_context (cr);
	}

	if (!caption.empty ()) {
		int w, h;
		layout->set_text (caption);
		layout->get_pixel_size (w, h);
		cr->move_to ((req.width - w) * .5, req.height - h - 2);
		layout->show_in_cairo_context (cr);
	}
	_extra_height = extra_height;
	_bg->flush ();
	queue_resize ();
}

void
ArdourKnob::render (Cairo::RefPtr<Cairo::Context> const& ctx, cairo_rectangle_t*)
{
	cairo_t* cr = ctx->cobj ();

	float width  = get_width ();
	float height = get_height ();

	if (_bg) {
		cairo_set_source_surface (cr, _bg->cobj (), 0, 0);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		cairo_paint (cr);
		height -= _extra_height;
	}

	const float scale = std::min (width, height);

	/* Spite's look is plugdata's knob (KnobObject::render): no disc, no bevel, no shading, three flat
	 * strokes: a dim track over 270 degrees with the gap at the bottom, the value arc from the "zero"
	 * point to the value, and a pointer from the centre. The colours still come from the theme:
	 * "<name>" is the track, "<name>: arc start/end" the value (and the pointer). */
	const float start_angle = (135 * G_PI) / 180;
	const float end_angle   = (405 * G_PI) / 180;

	float zero = 0;
	if (_flags & ArcToZero) {
		zero = _normal;
	}

	const float value_angle = start_angle + (_val * (end_angle - start_angle));
	const float zero_angle  = start_angle + (zero * (end_angle - start_angle));

	const float value_x = cos (value_angle);
	const float value_y = sin (value_angle);

	const float xc = 0.5 + width / 2.0;
	const float yc = 0.5 + height / 2.0;

	cairo_translate (cr, xc, yc);

	Gtkmm2ext::Color knob_color = UIConfigurationBase::instance ().color (string_compose ("%1", get_name ()));

	const bool arc = (_elements & Arc) == Arc;

	const float line   = std::max (2.f, scale * 0.1f);
	const float radius = scale * 0.5f - line * 0.5f - 0.5f;

	const bool  highlight = _tooltip.dragging () || (_hovering && UIConfigurationBase::instance ().get_widget_prelight ());
	const float alpha     = get_sensitive () ? 1.f : 0.4f;

	/* track */
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);
	cairo_set_line_width (cr, line);
	/* a notch lighter than the knob's fill, so the track shows on the dark panels */
	{
		double tr, tg, tb, ta;
		Gtkmm2ext::color_to_rgba (knob_color, tr, tg, tb, ta);
		cairo_set_source_rgba (cr, tr + (1.0 - tr) * 0.12, tg + (1.0 - tg) * 0.12, tb + (1.0 - tb) * 0.12, alpha);
	}
	cairo_arc (cr, 0, 0, radius, start_angle, end_angle);
	cairo_stroke (cr);

	/* the value's colour: the arc's, varied over the travel as before; the theme's text colour if the knob has no arc */
	double r = 0.9, g = 0.9, b = 0.9, unused_alpha;
	if (arc) {
		double red_start, green_start, blue_start, unused;
		double red_end, green_end, blue_end;

		Gtkmm2ext::Color arc_start_color = UIConfigurationBase::instance ().color (string_compose ("%1: arc start", get_name ()));
		Gtkmm2ext::color_to_rgba (arc_start_color, red_start, green_start, blue_start, unused);
		Gtkmm2ext::Color arc_end_color = UIConfigurationBase::instance ().color (string_compose ("%1: arc end", get_name ()));
		Gtkmm2ext::color_to_rgba (arc_end_color, red_end, green_end, blue_end, unused);

		float       intensity     = fabsf (_val - zero) / std::max (zero, (1.f - zero));
		const float intensity_inv = 1.0 - intensity;

		r = intensity_inv * red_end   + intensity * red_start;
		g = intensity_inv * green_end + intensity * green_start;
		b = intensity_inv * blue_end  + intensity * blue_start;
	} else {
		Gtkmm2ext::color_to_rgba (UIConfigurationBase::instance ().color ("gtk_foreground"), r, g, b, unused_alpha);
	}

	if (highlight) {
		r += (1.0 - r) * 0.25;
		g += (1.0 - g) * 0.25;
		b += (1.0 - b) * 0.25;
	}

	/* value arc */
	if (arc && value_angle != zero_angle) {
		cairo_set_source_rgba (cr, r, g, b, alpha);
		cairo_set_line_width (cr, line);
		if (zero_angle > value_angle) {
			cairo_arc (cr, 0, 0, radius, value_angle, zero_angle);
		} else {
			cairo_arc (cr, 0, 0, radius, zero_angle, value_angle);
		}
		cairo_stroke (cr);
	}

	/* pointer: from the centre to 0.8 of the arc's radius, as plugdata's wiper */
	cairo_set_source_rgba (cr, r, g, b, alpha);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_width (cr, line);
	cairo_move_to (cr, 0, 0);
	cairo_line_to (cr, (radius * 0.8f) * value_x, (radius * 0.8f) * value_y);
	cairo_stroke (cr);

	cairo_identity_matrix (cr);
}

void
ArdourKnob::on_size_request (Gtk::Requisition* req)
{
	req->width  = _req_width;
	req->height = _req_height;
	if (req->width < 1) {
		req->width = 13;
	}
	if (req->height < 1) {
		req->height = 13;
	}

	/* knob is square */
	req->width  = std::max (req->width, req->height);
	req->height = std::max (req->width, req->height);

	if (_bg) {
		req->width  = std::max (req->width, _bg->get_width ());
		req->height = std::max (req->height, _bg->get_height ());
	}
}

void
ArdourKnob::set_elements (Element e)
{
	_elements = e;
}

void
ArdourKnob::add_elements (Element e)
{
	_elements = (ArdourKnob::Element) (_elements | e);
}
