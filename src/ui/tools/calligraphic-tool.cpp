// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Handwriting-like drawing mode
 *
 * Authors:
 *   Mitsuru Oka <oka326@parkcity.ne.jp>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   MenTaLguY <mental@rydia.net>
 *   Abhishek Sharma
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * The original dynadraw code:
 *   Paul Haeberli <paul@sgi.com>
 *
 * Copyright (C) 1998 The Free Software Foundation
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 * Copyright (C) 2005-2007 bulia byak
 * Copyright (C) 2006 MenTaLguY
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#define noDYNA_DRAW_VERBOSE

#include "ui/tools/calligraphic-tool.h"

#include <cstring>
#include <numeric>
#include <string>

#include <gdk/gdkkeysyms.h>
#include <glibmm/i18n.h>
#include <gtk/gtk.h>

#include <2geom/bezier-utils.h>
#include <2geom/circle.h>
#include <2geom/pathvector.h>

#include "context-fns.h"
#include "desktop-events.h"
#include "desktop-style.h"
#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "inkscape.h"
#include "message-context.h"
#include "selection.h"

#include "display/control/canvas-item-bpath.h"
#include "display/control/canvas-item-drawing.h" // ctx
#include "display/curve.h"
#include "display/drawing.h"

#include "include/macros.h"

#include "livarot/Path.h"
#include "livarot/Shape.h"

#include "object/sp-shape.h"
#include "object/sp-text.h"

#include "path/path-util.h"

#include "svg/svg.h"

#include "ui/icon-names.h"
#include "ui/tools/freehand-base.h"
#include "ui/widget/canvas.h"
#include "ui/widget/events/canvas-event.h"

#include "util/units.h"

using Inkscape::DocumentUndo;
using Inkscape::Util::Quantity;
using Inkscape::Util::Unit;
using Inkscape::Util::unit_table;

#define DDC_RED_RGBA 0xff0000ff

#define TOLERANCE_CALLIGRAPHIC 0.1

#define DYNA_EPSILON 0.5e-6
#define DYNA_EPSILON_START 0.5e-2
#define DYNA_VEL_START 1e-5

#define DYNA_MIN_WIDTH 1.0e-6

namespace Inkscape {
namespace UI {
namespace Tools {

CalligraphicTool::CalligraphicTool(SPDesktop *desktop)
    : DynamicBase(desktop, "/tools/calligraphic", "calligraphy.svg")
    , keep_selected(true)
    , hatch_spacing(0)
    , hatch_spacing_step(0)
    , hatch_item(nullptr)
    , hatch_last_nearest(Geom::Point(0, 0))
    , hatch_last_pointer(Geom::Point(0, 0))
    , hatch_escaped(false)
    , just_started_drawing(false)
    , trace_bg(false)
{
    this->vel_thin = 0.1;
    this->flatness = -0.9;
    this->cap_rounding = 0.0;
    this->abs_width = false;

    currentshape = make_canvasitem<CanvasItemBpath>(desktop->getCanvasSketch());
    currentshape->set_stroke(0x0);
    currentshape->set_fill(DDC_RED_RGBA, SP_WIND_RULE_EVENODD);

    /* fixme: Cannot we cascade it to root more clearly? */
    currentshape->connect_event(sigc::bind(sigc::ptr_fun(sp_desktop_root_handler), desktop));

    hatch_area = make_canvasitem<CanvasItemBpath>(desktop->getCanvasControls());
    hatch_area->set_fill(0x0, SP_WIND_RULE_EVENODD);
    hatch_area->set_stroke(0x0000007f);
    hatch_area->set_pickable(false);
    hatch_area->set_visible(false);

    sp_event_context_read(this, "mass");
    sp_event_context_read(this, "wiggle");
    sp_event_context_read(this, "angle");
    sp_event_context_read(this, "width");
    sp_event_context_read(this, "thinning");
    sp_event_context_read(this, "tremor");
    sp_event_context_read(this, "flatness");
    sp_event_context_read(this, "tracebackground");
    sp_event_context_read(this, "usepressure");
    sp_event_context_read(this, "usetilt");
    sp_event_context_read(this, "abs_width");
    sp_event_context_read(this, "keep_selected");
    sp_event_context_read(this, "cap_rounding");

    this->is_drawing = false;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs->getBool("/tools/calligraphic/selcue")) {
        this->enableSelectionCue();
    }
}

CalligraphicTool::~CalligraphicTool() = default;

void CalligraphicTool::set(const Inkscape::Preferences::Entry& val) {
    Glib::ustring path = val.getEntryName();

    if (path == "tracebackground") {
        this->trace_bg = val.getBool();
    } else if (path == "keep_selected") {
        this->keep_selected = val.getBool();
    } else {
        //pass on up to parent class to handle common attributes.
    	DynamicBase::set(val);
    }

    //g_print("DDC: %g %g %g %g\n", ddc->mass, ddc->drag, ddc->angle, ddc->width);
}

static double
flerp(double f0, double f1, double p)
{
    return f0 + ( f1 - f0 ) * p;
}

void CalligraphicTool::reset(Geom::Point p) {
    this->last = this->cur = this->getNormalizedPoint(p);

    this->vel = Geom::Point(0,0);
    this->vel_max = 0;
    this->acc = Geom::Point(0,0);
    this->ang = Geom::Point(0,0);
    this->del = Geom::Point(0,0);
}

void CalligraphicTool::extinput(GdkEvent *event) {
    if (gdk_event_get_axis (event, GDK_AXIS_PRESSURE, &this->pressure)) {
        this->pressure = CLAMP (this->pressure, DDC_MIN_PRESSURE, DDC_MAX_PRESSURE);
    } else {
        this->pressure = DDC_DEFAULT_PRESSURE;
    }

    if (gdk_event_get_axis (event, GDK_AXIS_XTILT, &this->xtilt)) {
        this->xtilt = CLAMP (this->xtilt, DDC_MIN_TILT, DDC_MAX_TILT);
    } else {
        this->xtilt = DDC_DEFAULT_TILT;
    }

    if (gdk_event_get_axis (event, GDK_AXIS_YTILT, &this->ytilt)) {
        this->ytilt = CLAMP (this->ytilt, DDC_MIN_TILT, DDC_MAX_TILT);
    } else {
        this->ytilt = DDC_DEFAULT_TILT;
    }
}


bool CalligraphicTool::apply(Geom::Point p) {
    Geom::Point n = this->getNormalizedPoint(p);

    /* Calculate mass and drag */
    double const mass = flerp(1.0, 160.0, this->mass);
    double const drag = flerp(0.0, 0.5, this->drag * this->drag);

    /* Calculate force and acceleration */
    Geom::Point force = n - this->cur;

    // If force is below the absolute threshold DYNA_EPSILON,
    // or we haven't yet reached DYNA_VEL_START (i.e. at the beginning of stroke)
    // _and_ the force is below the (higher) DYNA_EPSILON_START threshold,
    // discard this move.
    // This prevents flips, blobs, and jerks caused by microscopic tremor of the tablet pen,
    // especially bothersome at the start of the stroke where we don't yet have the inertia to
    // smooth them out.
    if ( Geom::L2(force) < DYNA_EPSILON || (this->vel_max < DYNA_VEL_START && Geom::L2(force) < DYNA_EPSILON_START)) {
        return FALSE;
    }

    this->acc = force / mass;

    /* Calculate new velocity */
    this->vel += this->acc;

    if (Geom::L2(this->vel) > this->vel_max)
        this->vel_max = Geom::L2(this->vel);

    /* Calculate angle of drawing tool */

    double a1;
    if (this->usetilt) {
        // 1a. calculate nib angle from input device tilt:
        if (this->xtilt == 0 && this->ytilt == 0) {
            // to be sure that atan2 in the computation below
            // would not crash or return NaN.
            a1 = 0;
        } else {
            Geom::Point dir(-this->xtilt, this->ytilt);
            a1 = atan2(dir);
        }
    }
    else {
        // 1b. fixed dc->angle (absolutely flat nib):
        a1 = ( this->angle / 180.0 ) * M_PI;
    }
    a1 *= -_desktop->yaxisdir();
    if (this->flatness < 0.0) {
        // flips direction. Useful when this->usetilt
        // allows simulating both pen and calligraphic brush
        a1 *= -1;
    }
    a1 = fmod(a1, M_PI);
    if (a1 > 0.5*M_PI) {
        a1 -= M_PI;
    } else if (a1 <= -0.5*M_PI) {
        a1 += M_PI;
    }

    // 2. perpendicular to dc->vel (absolutely non-flat nib):
    gdouble const mag_vel = Geom::L2(this->vel);
    if ( mag_vel < DYNA_EPSILON ) {
        return FALSE;
    }
    Geom::Point ang2 = Geom::rot90(this->vel) / mag_vel;

    // 3. Average them using flatness parameter:
    // calculate angles
    double a2 = atan2(ang2);
    // flip a2 to force it to be in the same half-circle as a1
    bool flipped = false;
    if (fabs (a2-a1) > 0.5*M_PI) {
        a2 += M_PI;
        flipped = true;
    }
    // normalize a2
    if (a2 > M_PI)
        a2 -= 2*M_PI;
    if (a2 < -M_PI)
        a2 += 2*M_PI;
    // find the flatness-weighted bisector angle, unflip if a2 was flipped
    // FIXME: when dc->vel is oscillating around the fixed angle, the new_ang flips back and forth. How to avoid this?
    double new_ang = a1 + (1 - fabs(this->flatness)) * (a2 - a1) - (flipped? M_PI : 0);

    // Try to detect a sudden flip when the new angle differs too much from the previous for the
    // current velocity; in that case discard this move
    double angle_delta = Geom::L2(Geom::Point (cos (new_ang), sin (new_ang)) - this->ang);
    if ( angle_delta / Geom::L2(this->vel) > 4000 ) {
        return FALSE;
    }

    // convert to point
    this->ang = Geom::Point (cos (new_ang), sin (new_ang));

//    g_print ("force %g  acc %g  vel_max %g  vel %g  a1 %g  a2 %g  new_ang %g\n", Geom::L2(force), Geom::L2(dc->acc), dc->vel_max, Geom::L2(dc->vel), a1, a2, new_ang);

    /* Apply drag */
    this->vel *= 1.0 - drag;

    /* Update position */
    this->last = this->cur;
    this->cur += this->vel;

    return TRUE;
}

void CalligraphicTool::brush() {
    g_assert( this->npoints >= 0 && this->npoints < SAMPLING_SIZE );

    // How much velocity thins strokestyle
    double vel_thin = flerp (0, 160, this->vel_thin);

    // Influence of pressure on thickness
    double pressure_thick = (this->usepressure ? this->pressure : 1.0);

    // get the real brush point, not the same as pointer (affected by hatch tracking and/or mass
    // drag)
    Geom::Point brush = getViewPoint(this->cur);
    Geom::Point brush_w = _desktop->d2w(brush);

    double trace_thick = 1;
    if (this->trace_bg) {
        // Trace background, use single pixel under brush.
        Geom::IntRect area = Geom::IntRect::from_xywh(brush_w.floor(), Geom::IntPoint(1, 1));

        Inkscape::CanvasItemDrawing *canvas_item_drawing = _desktop->getCanvasDrawing();
        Inkscape::Drawing *drawing = canvas_item_drawing->get_drawing();

        // Get average color.
        double R, G, B, A;
        drawing->averageColor(area, R, G, B, A);

        // Convert to thickness.
        double max = MAX (MAX (R, G), B);
        double min = MIN (MIN (R, G), B);
        double L = A * (max + min)/2 + (1 - A); // blend with white bg
        trace_thick = 1 - L;
        //g_print ("L %g thick %g\n", L, trace_thick);
    }

    double width = (pressure_thick * trace_thick - vel_thin * Geom::L2(this->vel)) * this->width;

    double tremble_left = 0, tremble_right = 0;
    if (this->tremor > 0) {
        // obtain two normally distributed random variables, using polar Box-Muller transform
        double x1, x2, w, y1, y2;
        do {
            x1 = 2.0 * g_random_double_range(0,1) - 1.0;
            x2 = 2.0 * g_random_double_range(0,1) - 1.0;
            w = x1 * x1 + x2 * x2;
        } while ( w >= 1.0 );
        w = sqrt( (-2.0 * log( w ) ) / w );
        y1 = x1 * w;
        y2 = x2 * w;

        // deflect both left and right edges randomly and independently, so that:
        // (1) dc->tremor=1 corresponds to sigma=1, decreasing dc->tremor narrows the bell curve;
        // (2) deflection depends on width, but is upped for small widths for better visual uniformity across widths;
        // (3) deflection somewhat depends on speed, to prevent fast strokes looking
        // comparatively smooth and slow ones excessively jittery
        tremble_left  = (y1)*this->tremor * (0.15 + 0.8*width) * (0.35 + 14*Geom::L2(this->vel));
        tremble_right = (y2)*this->tremor * (0.15 + 0.8*width) * (0.35 + 14*Geom::L2(this->vel));
    }

    if ( width < 0.02 * this->width ) {
        width = 0.02 * this->width;
    }

    double dezoomify_factor = 0.05 * 1000;
    if (!this->abs_width) {
        dezoomify_factor /= _desktop->current_zoom();
    }

    Geom::Point del_left = dezoomify_factor * (width + tremble_left) * this->ang;
    Geom::Point del_right = dezoomify_factor * (width + tremble_right) * this->ang;

    this->point1[this->npoints] = brush + del_left;
    this->point2[this->npoints] = brush - del_right;

    this->del = 0.5*(del_left + del_right);

    this->npoints++;
}

static void
sp_ddc_update_toolbox (SPDesktop *desktop, const gchar *id, double value)
{
    desktop->setToolboxAdjustmentValue (id, value);
}

void CalligraphicTool::cancel() {
    this->dragging = false;
    this->is_drawing = false;

    ungrabCanvasEvents();

    /* Remove all temporary line segments */
    segments.clear();

    /* reset accumulated curve */
    accumulated.reset();
    clear_current();

    repr = nullptr;
}

bool CalligraphicTool::root_handler(CanvasEvent const &canvas_event)
{
    auto event = canvas_event.original();
    gint ret = FALSE;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Unit const *unit = unit_table.getUnit(prefs->getString("/tools/calligraphic/unit"));

    switch (event->type) {
        case GDK_BUTTON_PRESS:
            if (event->button.button == 1) {
                if (Inkscape::have_viable_layer(_desktop, defaultMessageContext()) == false) {
                    return TRUE;
                }

                accumulated.reset();

                repr = nullptr;

                /* initialize first point */
                npoints = 0;

                grabCanvasEvents();

                ret = TRUE;

                set_high_motion_precision();
                this->is_drawing = true;
                this->just_started_drawing = true;
            }
            break;
        case GDK_MOTION_NOTIFY:
        {
            Geom::Point const motion_w(event->motion.x,
                                     event->motion.y);
            Geom::Point motion_dt(_desktop->w2d(motion_w));
            this->extinput(event);

            this->message_context->clear();

            // for hatching:
            double hatch_dist = 0;
            Geom::Point hatch_unit_vector(0,0);
            Geom::Point nearest(0,0);
            Geom::Point pointer(0,0);
            Geom::Affine motion_to_curve(Geom::identity());

            if (event->motion.state & GDK_CONTROL_MASK) { // hatching - sense the item

                SPItem *selected = _desktop->getSelection()->singleItem();
                if (selected && (is<SPShape>(selected) || is<SPText>(selected))) {
                    // One item selected, and it's a path;
                    // let's try to track it as a guide

                    if (selected != hatch_item) {
                        hatch_item = selected;
                        hatch_livarot_path = Path_for_item(hatch_item, true, true);
                        if (hatch_livarot_path) {
                            hatch_livarot_path->ConvertWithBackData(0.01);
                        }
                    }

                    // calculate pointer point in the guide item's coords
                    motion_to_curve = selected->dt2i_affine() * selected->i2doc_affine();
                    pointer = motion_dt * motion_to_curve;

                    // calculate the nearest point on the guide path
                    std::optional<Path::cut_position> position = get_nearest_position_on_Path(hatch_livarot_path.get(), pointer);
                    if (position) {
                        nearest = get_point_on_Path(hatch_livarot_path.get(), position->piece, position->t);

                        // distance from pointer to nearest
                        hatch_dist = Geom::L2(pointer - nearest);
                        // unit-length vector
                        hatch_unit_vector = (pointer - nearest) / hatch_dist;

                        this->message_context->set(Inkscape::NORMAL_MESSAGE, _("<b>Guide path selected</b>; start drawing along the guide with <b>Ctrl</b>"));
                    }
                } else {
                    this->message_context->set(Inkscape::NORMAL_MESSAGE, _("<b>Select a guide path</b> to track with <b>Ctrl</b>"));
                }
            }

            if ( this->is_drawing && (event->motion.state & GDK_BUTTON1_MASK)) {
                this->dragging = TRUE;

                if (event->motion.state & GDK_CONTROL_MASK && this->hatch_item) { // hatching

#define HATCH_VECTOR_ELEMENTS 12
#define INERTIA_ELEMENTS 24
#define SPEED_ELEMENTS 12
#define SPEED_MIN 0.3
#define SPEED_NORMAL 0.35
#define INERTIA_FORCE 0.5

                    // speed is the movement of the nearest point along the guide path, divided by
                    // the movement of the pointer at the same period; it is averaged for the last
                    // SPEED_ELEMENTS motion events.  Normally, as you track the guide path, speed
                    // is about 1, i.e. the nearest point on the path is moved by about the same
                    // distance as the pointer. If the speed starts to decrease, we are losing
                    // contact with the guide; if it drops below SPEED_MIN, we are on our own and
                    // not attracted to guide anymore. Most often this happens when you have
                    // tracked to the end of a guide calligraphic stroke and keep moving
                    // further. We try to handle this situation gracefully: not stick with the
                    // guide forever but let go of it smoothly and without sharp jerks (non-zero
                    // mass recommended; with zero mass, jerks are still quite noticeable).

                    double speed = 1;
                    if (Geom::L2(this->hatch_last_nearest) != 0) {
                        // the distance nearest moved since the last motion event
                        double nearest_moved = Geom::L2(nearest - this->hatch_last_nearest);
                        // the distance pointer moved since the last motion event
                        double pointer_moved = Geom::L2(pointer - this->hatch_last_pointer);
                        // store them in stacks limited to SPEED_ELEMENTS
                        this->hatch_nearest_past.push_front(nearest_moved);
                        if (this->hatch_nearest_past.size() > SPEED_ELEMENTS)
                            this->hatch_nearest_past.pop_back();
                        this->hatch_pointer_past.push_front(pointer_moved);
                        if (this->hatch_pointer_past.size() > SPEED_ELEMENTS)
                            this->hatch_pointer_past.pop_back();

                        // If the stacks are full,
                        if (this->hatch_nearest_past.size() == SPEED_ELEMENTS) {
                            // calculate the sums of all stored movements
                            double nearest_sum = std::accumulate (this->hatch_nearest_past.begin(), this->hatch_nearest_past.end(), 0.0);
                            double pointer_sum = std::accumulate (this->hatch_pointer_past.begin(), this->hatch_pointer_past.end(), 0.0);
                            // and divide to get the speed
                            speed = nearest_sum/pointer_sum;
                            //g_print ("nearest sum %g  pointer_sum %g  speed %g\n", nearest_sum, pointer_sum, speed);
                        }
                    }

                    if (   this->hatch_escaped  // already escaped, do not reattach
                        || (speed < SPEED_MIN) // stuck; most likely reached end of traced stroke
                        || (this->hatch_spacing > 0 && hatch_dist > 50 * this->hatch_spacing) // went too far from the guide
                        ) {
                        // We are NOT attracted to the guide!

                        //g_print ("\nlast_nearest %g %g   nearest %g %g  pointer %g %g  pos %d %g\n", dc->last_nearest[Geom::X], dc->last_nearest[Geom::Y], nearest[Geom::X], nearest[Geom::Y], pointer[Geom::X], pointer[Geom::Y], position->piece, position->t);

                        // Remember hatch_escaped so we don't get
                        // attracted again until the end of this stroke
                        this->hatch_escaped = true;

                        if (this->inertia_vectors.size() >= INERTIA_ELEMENTS/2) { // move by inertia
                            Geom::Point moved_past_escape = motion_dt - this->inertia_vectors.front();
                            Geom::Point inertia = 
                                this->inertia_vectors.front() - this->inertia_vectors.back();

                            double dot = Geom::dot (moved_past_escape, inertia);
                            dot /= Geom::L2(moved_past_escape) * Geom::L2(inertia);

                            if (dot > 0) { // mouse is still moving in approx the same direction
                                Geom::Point should_have_moved = 
                                    (inertia) * (1/Geom::L2(inertia)) * Geom::L2(moved_past_escape);
                                motion_dt = this->inertia_vectors.front() + 
                                    (INERTIA_FORCE * should_have_moved + (1 - INERTIA_FORCE) * moved_past_escape);
                            }
                        }

                    } else {

                        // Calculate angle cosine of this vector-to-guide and all past vectors
                        // summed, to detect if we accidentally flipped to the other side of the
                        // guide
                        Geom::Point hatch_vector_accumulated = std::accumulate 
                            (this->hatch_vectors.begin(), this->hatch_vectors.end(), Geom::Point(0,0));
                        double dot = Geom::dot (pointer - nearest, hatch_vector_accumulated);
                        dot /= Geom::L2(pointer - nearest) * Geom::L2(hatch_vector_accumulated);

                        if (this->hatch_spacing != 0) { // spacing was already set
                            double target;
                            if (speed > SPEED_NORMAL) {
                                // all ok, strictly obey the spacing
                                target = this->hatch_spacing;
                            } else {
                                // looks like we're starting to lose speed,
                                // so _gradually_ let go attraction to prevent jerks
                                target = (this->hatch_spacing * speed + hatch_dist * (SPEED_NORMAL - speed))/SPEED_NORMAL;
                            }
                            if (!std::isnan(dot) && dot < -0.5) {// flip
                                target = -target;
                            }

                            // This is the track pointer that we will use instead of the real one
                            Geom::Point new_pointer = nearest + target * hatch_unit_vector;

                            // some limited feedback: allow persistent pulling to slightly change
                            // the spacing
                            this->hatch_spacing += (hatch_dist - this->hatch_spacing)/3500;

                            // return it to the desktop coords
                            motion_dt = new_pointer * motion_to_curve.inverse();

                            if (speed >= SPEED_NORMAL) {
                                this->inertia_vectors.push_front(motion_dt);
                                if (this->inertia_vectors.size() > INERTIA_ELEMENTS)
                                    this->inertia_vectors.pop_back();
                            }

                        } else {
                            // this is the first motion event, set the dist
                            this->hatch_spacing = hatch_dist;
                        }

                        // remember last points
                        this->hatch_last_pointer = pointer;
                        this->hatch_last_nearest = nearest;

                        this->hatch_vectors.push_front(pointer - nearest);
                        if (this->hatch_vectors.size() > HATCH_VECTOR_ELEMENTS)
                            this->hatch_vectors.pop_back();
                    }

                    this->message_context->set(Inkscape::NORMAL_MESSAGE, this->hatch_escaped? _("Tracking: <b>connection to guide path lost!</b>") : _("<b>Tracking</b> a guide path"));

                } else {
                    this->message_context->set(Inkscape::NORMAL_MESSAGE, _("<b>Drawing</b> a calligraphic stroke"));
                }

                if (this->just_started_drawing) {
                    this->just_started_drawing = false;
                    this->reset(motion_dt);
                }

                if (!this->apply(motion_dt)) {
                    ret = TRUE;
                    break;
                }

                if ( this->cur != this->last ) {
                    this->brush();
                    g_assert( this->npoints > 0 );
                    this->fit_and_split(false);
                }
                ret = TRUE;
            }

            Geom::PathVector path = Geom::Path(Geom::Circle(0,0,1)); // Unit circle centered at origin.

            // Draw the hatching circle if necessary
            if (event->motion.state & GDK_CONTROL_MASK) {
                if (this->hatch_spacing == 0 && hatch_dist != 0) {
                    // Haven't set spacing yet: gray, center free, update radius live

                    Geom::Point c = _desktop->w2d(motion_w);
                    Geom::Affine const sm (Geom::Scale(hatch_dist, hatch_dist) * Geom::Translate(c));
                    path *= sm;

                    hatch_area->set_bpath(std::move(path), true);
                    hatch_area->set_stroke(0x7f7f7fff);
                    hatch_area->set_visible(true);

                } else if (this->dragging && !this->hatch_escaped && hatch_dist != 0) {
                    // Tracking: green, center snapped, fixed radius

                    Geom::Point c = motion_dt;
                    Geom::Affine const sm (Geom::Scale(this->hatch_spacing, this->hatch_spacing) * Geom::Translate(c));
                    path *= sm;

                    hatch_area->set_bpath(std::move(path), true);
                    hatch_area->set_stroke(0x00FF00ff);
                    hatch_area->set_visible(true);

                } else if (this->dragging && this->hatch_escaped && hatch_dist != 0) {
                    // Tracking escaped: red, center free, fixed radius

                    Geom::Point c = motion_dt;
                    Geom::Affine const sm (Geom::Scale(this->hatch_spacing, this->hatch_spacing) * Geom::Translate(c));
                    path *= sm;

                    hatch_area->set_bpath(std::move(path), true);
                    hatch_area->set_stroke(0xff0000ff);
                    hatch_area->set_visible(true);

                } else {
                    // Not drawing but spacing set: gray, center snapped, fixed radius

                    Geom::Point c = (nearest + this->hatch_spacing * hatch_unit_vector) * motion_to_curve.inverse();
                    if (!std::isnan(c[Geom::X]) && !std::isnan(c[Geom::Y]) && this->hatch_spacing!=0) {
                        Geom::Affine const sm (Geom::Scale(this->hatch_spacing, this->hatch_spacing) * Geom::Translate(c));
                        path *= sm;

                        hatch_area->set_bpath(std::move(path), true);
                        hatch_area->set_stroke(0x7f7f7fff);
                        hatch_area->set_visible(true);
                    }
                }
            } else {
                hatch_area->set_visible(false);
            }
        }
        break;


    case GDK_BUTTON_RELEASE:
    {
        Geom::Point const motion_w(event->button.x, event->button.y);
        Geom::Point const motion_dt(_desktop->w2d(motion_w));

        ungrabCanvasEvents();

        set_high_motion_precision(false);
        this->is_drawing = false;

        if (this->dragging && event->button.button == 1) {
            this->dragging = FALSE;

            this->apply(motion_dt);

            /* Remove all temporary line segments */
            segments.clear();

            /* Create object */
            this->fit_and_split(true);
            if (this->accumulate())
                this->set_to_accumulated(event->button.state & GDK_SHIFT_MASK, event->button.state & GDK_MOD1_MASK); // performs document_done
            else
                g_warning ("Failed to create path: invalid data in dc->cal1 or dc->cal2");

            /* reset accumulated curve */
            accumulated.reset();

            clear_current();
            repr = nullptr;

            if (!this->hatch_pointer_past.empty()) this->hatch_pointer_past.clear();
            if (!this->hatch_nearest_past.empty()) this->hatch_nearest_past.clear();
            if (!this->inertia_vectors.empty()) this->inertia_vectors.clear();
            if (!this->hatch_vectors.empty()) this->hatch_vectors.clear();
            this->hatch_last_nearest = Geom::Point(0,0);
            this->hatch_last_pointer = Geom::Point(0,0);
            this->hatch_escaped = false;
            this->hatch_item = nullptr;
            this->hatch_livarot_path.reset();
            this->just_started_drawing = false;

            if (this->hatch_spacing != 0 && !this->keep_selected) {
                // we do not select the newly drawn path, so increase spacing by step
                if (this->hatch_spacing_step == 0) {
                    this->hatch_spacing_step = this->hatch_spacing;
                }
                this->hatch_spacing += this->hatch_spacing_step;
            }

            this->message_context->clear();
            ret = TRUE;
        } else if (!this->dragging
                   && event->button.button == 1
                   && Inkscape::have_viable_layer(_desktop, defaultMessageContext()))
        {
            spdc_create_single_dot(this, _desktop->w2d(motion_w), "/tools/calligraphic", event->button.state);
            ret = TRUE;
        }
        break;
    }

    case GDK_KEY_PRESS:
        switch (get_latin_keyval (&event->key)) {
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            if (!MOD__CTRL_ONLY(event)) {
                this->angle += 5.0;
                if (this->angle > 90.0)
                    this->angle = 90.0;
                sp_ddc_update_toolbox(_desktop, "calligraphy-angle", this->angle);
                ret = TRUE;
            }
            break;
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            if (!MOD__CTRL_ONLY(event)) {
                this->angle -= 5.0;
                if (this->angle < -90.0)
                    this->angle = -90.0;
                sp_ddc_update_toolbox(_desktop, "calligraphy-angle", this->angle);
                ret = TRUE;
            }
            break;
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            if (!MOD__CTRL_ONLY(event)) {
                this->width = Quantity::convert(this->width, "px", unit) + 0.01;
                if (this->width > 1.0)
                    this->width = 1.0;
                sp_ddc_update_toolbox (_desktop, "calligraphy-width", this->width * 100); // the same spinbutton is for alt+x
                ret = TRUE;
            }
            break;
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            if (!MOD__CTRL_ONLY(event)) {
                this->width = Quantity::convert(this->width, "px", unit) - 0.01;
                if (this->width < 0.00001)
                    this->width = 0.00001;
                sp_ddc_update_toolbox(_desktop, "calligraphy-width", this->width * 100);
                ret = TRUE;
            }
            break;
        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
            this->width = 0.00001;
            sp_ddc_update_toolbox(_desktop, "calligraphy-width", this->width * 100);
            ret = TRUE;
            break;
        case GDK_KEY_End:
        case GDK_KEY_KP_End:
            this->width = 1.0;
            sp_ddc_update_toolbox(_desktop, "calligraphy-width", this->width * 100);
            ret = TRUE;
            break;
        case GDK_KEY_x:
        case GDK_KEY_X:
            if (MOD__ALT_ONLY(event)) {
                _desktop->setToolboxFocusTo("calligraphy-width");
                ret = TRUE;
            }
            break;
        case GDK_KEY_Escape:
            if (this->is_drawing) {
                // if drawing, cancel, otherwise pass it up for deselecting
                this->cancel();
                ret = TRUE;
            }
            break;
        case GDK_KEY_z:
        case GDK_KEY_Z:
            if (MOD__CTRL_ONLY(event) && this->is_drawing) {
                // if drawing, cancel, otherwise pass it up for undo
                this->cancel();
                ret = TRUE;
            }
            break;
        default:
            break;
        }
        break;

    case GDK_KEY_RELEASE:
        switch (get_latin_keyval(&event->key)) {
            case GDK_KEY_Control_L:
            case GDK_KEY_Control_R:
                this->message_context->clear();
                this->hatch_spacing = 0;
                this->hatch_spacing_step = 0;
                break;
            default:
                break;
        }
        break;

    default:
        break;
    }

    if (!ret) {
        ret = DynamicBase::root_handler(canvas_event);
    }

    return ret;
}

void CalligraphicTool::clear_current() {
    /* reset bpath */
    currentshape->set_bpath(nullptr);

    /* reset curve */
    currentcurve.reset();
    cal1.reset();
    cal2.reset();

    /* reset points */
    npoints = 0;
}

void CalligraphicTool::set_to_accumulated(bool unionize, bool subtract) {
    if (!accumulated.is_empty()) {
        if (!repr) {
            /* Create object */
            Inkscape::XML::Document *xml_doc = _desktop->doc()->getReprDoc();
            Inkscape::XML::Node *repr = xml_doc->createElement("svg:path");

            /* Set style */
            sp_desktop_apply_style_tool(_desktop, repr, "/tools/calligraphic", false);

            this->repr = repr;

            auto layer = currentLayer();
            auto item = cast<SPItem>(layer->appendChildRepr(this->repr));
            Inkscape::GC::release(this->repr);
            item->transform = layer->i2doc_affine().inverse();
            item->updateRepr();
        }

        Geom::PathVector pathv = accumulated.get_pathvector() * _desktop->dt2doc();
        repr->setAttribute("d", sp_svg_write_path(pathv));

        if (unionize) {
            _desktop->getSelection()->add(this->repr);
            _desktop->getSelection()->pathUnion(true);
        } else if (subtract) {
            _desktop->getSelection()->add(this->repr);
            _desktop->getSelection()->pathDiff(true);
        } else {
            if (this->keep_selected) {
                _desktop->getSelection()->set(this->repr);
            }
        }

        // Now we need to write the transform information.
        // First, find out whether our repr is still linked to a valid object. In this case,
        // we need to write the transform data only for this element.
        // Either there was no boolean op or it failed.
        auto result = cast<SPItem>(_desktop->doc()->getObjectByRepr(this->repr));

        if (result == nullptr) {
            // The boolean operation succeeded.
            // Now we fetch the single item, that has been set as selected by the boolean op.
            // This is its result.
            result = _desktop->getSelection()->singleItem();
        }
        result->doWriteTransform(result->transform, nullptr, true);
    } else {
        if (this->repr) {
            sp_repr_unparent(this->repr);
        }

        this->repr = nullptr;
    }

    DocumentUndo::done(_desktop->getDocument(), _("Draw calligraphic stroke"), INKSCAPE_ICON("draw-calligraphic"));
}

static void
add_cap(SPCurve &curve,
        Geom::Point const &from,
        Geom::Point const &to,
        double rounding)
{
    if (Geom::L2( to - from ) > DYNA_EPSILON) {
        Geom::Point vel = rounding * Geom::rot90( to - from ) / sqrt(2.0);
        double mag = Geom::L2(vel);

        Geom::Point v = mag * Geom::rot90( to - from ) / Geom::L2( to - from );
        curve.curveto(from + v, to + v, to);
    }
}

bool CalligraphicTool::accumulate() {
	if (
        cal1.is_empty() ||
        cal2.is_empty() ||
        (cal1.get_segment_count() <= 0) ||
        cal1.first_path()->closed()
		) {

        cal1.reset();
        cal2.reset();

		return false; // failure
	}

        auto rev_cal2 = cal2.reversed();

    if ((rev_cal2.get_segment_count() <= 0) || rev_cal2.first_path()->closed()) {
        cal1.reset();
        cal2.reset();

		return false; // failure
	}

    Geom::Curve const * dc_cal1_firstseg  = cal1.first_segment();
    Geom::Curve const * rev_cal2_firstseg = rev_cal2.first_segment();
    Geom::Curve const * dc_cal1_lastseg   = cal1.last_segment();
    Geom::Curve const * rev_cal2_lastseg  = rev_cal2.last_segment();

    accumulated.reset(); /*  Is this required ?? */

    accumulated.append(cal1);

    add_cap(accumulated, dc_cal1_lastseg->finalPoint(), rev_cal2_firstseg->initialPoint(), cap_rounding);

    accumulated.append(rev_cal2, true);

    add_cap(accumulated, rev_cal2_lastseg->finalPoint(), dc_cal1_firstseg->initialPoint(), cap_rounding);

    accumulated.closepath();

    cal1.reset();
    cal2.reset();

	return true; // success
}

static double square(double const x)
{
    return x * x;
}

void CalligraphicTool::fit_and_split(bool release) {
    double const tolerance_sq = square(_desktop->w2d().descrim() * TOLERANCE_CALLIGRAPHIC);

#ifdef DYNA_DRAW_VERBOSE
    g_print("[F&S:R=%c]", release?'T':'F');
#endif

    if (!( this->npoints > 0 && this->npoints < SAMPLING_SIZE )) {
        return; // just clicked
    }

    if ( this->npoints == SAMPLING_SIZE - 1 || release ) {
#define BEZIER_SIZE       4
#define BEZIER_MAX_BEZIERS  8
#define BEZIER_MAX_LENGTH ( BEZIER_SIZE * BEZIER_MAX_BEZIERS )

#ifdef DYNA_DRAW_VERBOSE
        g_print("[F&S:#] dc->npoints:%d, release:%s\n",
                this->npoints, release ? "TRUE" : "FALSE");
#endif

        /* Current calligraphic */
        if ( cal1.is_empty() || cal2.is_empty() ) {
            /* dc->npoints > 0 */
            /* g_print("calligraphics(1|2) reset\n"); */
            cal1.reset();
            cal2.reset();

            cal1.moveto(this->point1[0]);
            cal2.moveto(this->point2[0]);
        }

        Geom::Point b1[BEZIER_MAX_LENGTH];
        gint const nb1 = Geom::bezier_fit_cubic_r(b1, this->point1, this->npoints,
                                               tolerance_sq, BEZIER_MAX_BEZIERS);
        g_assert( nb1 * BEZIER_SIZE <= gint(G_N_ELEMENTS(b1)) );

        Geom::Point b2[BEZIER_MAX_LENGTH];
        gint const nb2 = Geom::bezier_fit_cubic_r(b2, this->point2, this->npoints,
                                               tolerance_sq, BEZIER_MAX_BEZIERS);
        g_assert( nb2 * BEZIER_SIZE <= gint(G_N_ELEMENTS(b2)) );

        if ( nb1 != -1 && nb2 != -1 ) {
            /* Fit and draw and reset state */
#ifdef DYNA_DRAW_VERBOSE
            g_print("nb1:%d nb2:%d\n", nb1, nb2);
#endif
            /* CanvasShape */
            if (! release) {
                currentcurve.reset();
                currentcurve.moveto(b1[0]);
                for (Geom::Point *bp1 = b1; bp1 < b1 + BEZIER_SIZE * nb1; bp1 += BEZIER_SIZE) {
                    currentcurve.curveto(bp1[1], bp1[2], bp1[3]);
                }
                currentcurve.lineto(b2[BEZIER_SIZE*(nb2-1) + 3]);
                for (Geom::Point *bp2 = b2 + BEZIER_SIZE * ( nb2 - 1 ); bp2 >= b2; bp2 -= BEZIER_SIZE) {
                    currentcurve.curveto(bp2[2], bp2[1], bp2[0]);
                }
                // FIXME: dc->segments is always NULL at this point??
                if (this->segments.empty()) { // first segment
                    add_cap(currentcurve, b2[0], b1[0], cap_rounding);
                }
                currentcurve.closepath();
                currentshape->set_bpath(&currentcurve, true);
            }

            /* Current calligraphic */
            for (Geom::Point *bp1 = b1; bp1 < b1 + BEZIER_SIZE * nb1; bp1 += BEZIER_SIZE) {
                cal1.curveto(bp1[1], bp1[2], bp1[3]);
            }
            for (Geom::Point *bp2 = b2; bp2 < b2 + BEZIER_SIZE * nb2; bp2 += BEZIER_SIZE) {
                cal2.curveto(bp2[1], bp2[2], bp2[3]);
            }
        } else {
            /* fixme: ??? */
#ifdef DYNA_DRAW_VERBOSE
            g_print("[fit_and_split] failed to fit-cubic.\n");
#endif
            this->draw_temporary_box();

            for (gint i = 1; i < this->npoints; i++) {
                cal1.lineto(this->point1[i]);
            }
            for (gint i = 1; i < this->npoints; i++) {
                cal2.lineto(this->point2[i]);
            }
        }

        /* Fit and draw and copy last point */
#ifdef DYNA_DRAW_VERBOSE
        g_print("[%d]Yup\n", this->npoints);
#endif
        if (!release) {
            g_assert(!currentcurve.is_empty());

            guint32 fillColor = sp_desktop_get_color_tool(_desktop, "/tools/calligraphic", true);
            double opacity = sp_desktop_get_master_opacity_tool(_desktop, "/tools/calligraphic");
            double fillOpacity = sp_desktop_get_opacity_tool(_desktop, "/tools/calligraphic", true);
            guint fill = (fillColor & 0xffffff00) | SP_COLOR_F_TO_U(opacity*fillOpacity);

            auto cbp = new Inkscape::CanvasItemBpath(_desktop->getCanvasSketch(), currentcurve.get_pathvector(), true);
            cbp->set_fill(fill, SP_WIND_RULE_EVENODD);
            cbp->set_stroke(0x0);

            /* fixme: Cannot we cascade it to root more clearly? */
            cbp->connect_event(sigc::bind(sigc::ptr_fun(sp_desktop_root_handler), _desktop));

            segments.emplace_back(cbp);
        }

        this->point1[0] = this->point1[this->npoints - 1];
        this->point2[0] = this->point2[this->npoints - 1];
        this->npoints = 1;
    } else {
        this->draw_temporary_box();
    }
}

void CalligraphicTool::draw_temporary_box() {
    currentcurve.reset();

    currentcurve.moveto(this->point2[this->npoints-1]);

    for (gint i = this->npoints-2; i >= 0; i--) {
        currentcurve.lineto(this->point2[i]);
    }

    for (gint i = 0; i < this->npoints; i++) {
        currentcurve.lineto(this->point1[i]);
    }

    if (this->npoints >= 2) {
        add_cap(currentcurve, point1[npoints - 1], point2[npoints - 1], cap_rounding);
    }

    currentcurve.closepath();
    currentshape->set_bpath(&currentcurve, true);
}

}
}
}


/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
