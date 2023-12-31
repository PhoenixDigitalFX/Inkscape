// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SPKnot implementation
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Abhishek Sharma
 *
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifdef HAVE_CONFIG_H
#endif
#include <gdk/gdkkeysyms.h>
#include <glibmm/i18n.h>

#include "desktop.h"

#include "knot.h"
#include "knot-ptr.h"
#include "document.h"
#include "document-undo.h"
#include "message-stack.h"
#include "message-context.h"

#include "display/control/canvas-item-ctrl.h"
#include "ui/tools/tool-base.h"
#include "ui/tools/node-tool.h"
#include "ui/widget/canvas.h" // autoscroll
#include "ui/widget/events/canvas-event.h"

using Inkscape::DocumentUndo;
using Inkscape::EventType;

static constexpr auto KNOT_EVENT_MASK =
    EventType::BUTTON_PRESS   |
    EventType::BUTTON_RELEASE |
    EventType::MOTION |
    EventType::KEY_PRESS      |
    EventType::KEY_RELEASE;

const gchar *nograbenv = getenv("INKSCAPE_NO_GRAB");
static bool nograb = (nograbenv && *nograbenv && (*nograbenv != '0'));


void knot_ref(SPKnot* knot) {
    knot->ref_count++;
}

void knot_unref(SPKnot* knot) {
    if (--knot->ref_count < 1) {
        delete knot;
    }
}

SPKnot::SPKnot(SPDesktop *desktop, gchar const *tip, Inkscape::CanvasItemCtrlType type, Glib::ustring const & name)
    : desktop(desktop)
    , ref_count(1)
{
    if (tip) {
        this->tip = g_strdup (tip);
    }

    fill[SP_KNOT_STATE_NORMAL]    = 0xffffff00;
    fill[SP_KNOT_STATE_MOUSEOVER] = 0xff0000ff;
    fill[SP_KNOT_STATE_DRAGGING]  = 0xff0000ff;
    fill[SP_KNOT_STATE_SELECTED]  = 0x0000ffff;

    stroke[SP_KNOT_STATE_NORMAL]    = 0x01000000;
    stroke[SP_KNOT_STATE_MOUSEOVER] = 0x01000000;
    stroke[SP_KNOT_STATE_DRAGGING]  = 0x01000000;
    stroke[SP_KNOT_STATE_SELECTED]  = 0x01000000;

    image[SP_KNOT_STATE_NORMAL] = nullptr;
    image[SP_KNOT_STATE_MOUSEOVER] = nullptr;
    image[SP_KNOT_STATE_DRAGGING] = nullptr;
    image[SP_KNOT_STATE_SELECTED] = nullptr;

    ctrl = make_canvasitem<Inkscape::CanvasItemCtrl>(desktop->getCanvasControls(), type); // Shape, mode set
    ctrl->set_name("CanvasItemCtrl:Knot:" + name);

    // Are these needed?
    ctrl->set_fill(0xffffff00);
    ctrl->set_stroke(0x01000000);

    _event_connection = ctrl->connect_event(sigc::mem_fun(*this, &SPKnot::eventHandler));

    knot_created_callback(this);
}

SPKnot::~SPKnot() {

    // Make sure the knot is not grabbed, as it's destructing can be deferred causing
    // issues like https://gitlab.com/inkscape/inkscape/-/issues/4239
    ctrl->ungrab();
    ctrl.reset();

    if (this->tip) {
        g_free(this->tip);
        this->tip = nullptr;
    }

    // FIXME: cannot snap to destroyed knot (lp:1309050)
    // this->desktop->event_context->discard_delayed_snap_event();
    knot_deleted_callback(this);
}

void SPKnot::startDragging(Geom::Point const &p, gint x, gint y, guint32 etime) {
    // save drag origin
    xp = x;
    yp = y;
    within_tolerance = true;

    this->grabbed_rel_pos = p - this->pos;
    this->drag_origin = this->pos;

    if (!nograb && ctrl) {
        ctrl->grab(KNOT_EVENT_MASK, _cursors[SP_KNOT_STATE_DRAGGING]);
    }
    this->setFlag(SP_KNOT_GRABBED, true);

    grabbed = true;
}

void SPKnot::selectKnot(bool select)
{
    setFlag(SP_KNOT_SELECTED, select);
}

bool SPKnot::eventHandler(Inkscape::CanvasEvent const &canvas_event)
{
    auto event = canvas_event.original();

    /* Run client universal event handler, if present */
    bool consumed = event_signal.emit(this, event);
    if (consumed) {
        return true;
    }

    bool key_press_event_unconsumed = false;

    ref_count++;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    tolerance = prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);

    switch (event->type) {
    case GDK_2BUTTON_PRESS:
        if (event->button.button == 1) {
            doubleclicked_signal.emit(this, event->button.state);

            grabbed = false;
            moved = false;
            consumed = true;
        }
        break;
    case GDK_BUTTON_PRESS:
        if ((event->button.button == 1) && desktop && desktop->event_context && !desktop->event_context->is_space_panning()) {
            Geom::Point const p = desktop->w2d(Geom::Point(event->button.x, event->button.y));
            startDragging(p, (gint) event->button.x, (gint) event->button.y, event->button.time);
            mousedown_signal.emit(this, event->button.state);
            consumed = true;
        }
        break;
    case GDK_BUTTON_RELEASE:
        if (event->button.button == 1    &&
            desktop                &&
            desktop->event_context &&
            !desktop->event_context->is_space_panning()) {

            // If we have any pending snap event, then invoke it now
            desktop->event_context->process_delayed_snap_event();
            pressure = 0;

            if (transform_escaped) {
                transform_escaped = false;
                consumed = true;
            } else {
                setFlag(SP_KNOT_GRABBED, false);

                if (!nograb && ctrl) {
                    ctrl->ungrab();
                }

                if (moved) {
                    setFlag(SP_KNOT_DRAGGING, false);
                    ungrabbed_signal.emit(this, event->button.state);
                } else {
                    click_signal.emit(this, event->button.state);
                }

                grabbed = false;
                moved = false;
                consumed = true;
            }
        }
        Inkscape::UI::Tools::sp_update_helperpath(desktop);
        break;

    case GDK_MOTION_NOTIFY:

        if (!(event->motion.state & GDK_BUTTON1_MASK) && flags & SP_KNOT_DRAGGING) {
            pressure = 0;

            if (transform_escaped) {
                transform_escaped = false;
                consumed = true;
            } else {
                setFlag(SP_KNOT_GRABBED, false);

                if (!nograb && ctrl) {
                    ctrl->ungrab();
                }

                if (moved) {
                    setFlag(SP_KNOT_DRAGGING, false);
                    ungrabbed_signal.emit(this, event->motion.state);
                } else {
                    click_signal.emit(this, event->motion.state);
                }

                grabbed = false;
                moved = false;
                consumed = true;
                Inkscape::UI::Tools::sp_update_helperpath(desktop);
            }
        } else if (grabbed && desktop && desktop->event_context &&
                   !desktop->event_context->is_space_panning()) {
            consumed = true;

            if ( within_tolerance
                 && ( abs( (gint) event->motion.x - xp ) < tolerance )
                 && ( abs( (gint) event->motion.y - yp ) < tolerance ) ) {
                break; // do not drag if we're within tolerance from origin
            }

            // Once the user has moved farther than tolerance from the original location
            // (indicating they intend to move the object, not click), then always process the
            // motion notify coordinates as given (no snapping back to origin)
            within_tolerance = false;

            // Note: Synthesized events don't have a device.
            if (event->motion.device && gdk_event_get_axis(event, GDK_AXIS_PRESSURE, &pressure)) {
                pressure = CLAMP (pressure, 0, 1);
            } else {
                pressure = 0.5;
            }

            if (!moved) {
                setFlag(SP_KNOT_DRAGGING, true);
                grabbed_signal.emit(this, event->button.state);
            }

            desktop->event_context->snap_delay_handler(nullptr, this, static_cast<Inkscape::MotionEvent const &>(canvas_event),
                                                       Inkscape::UI::Tools::DelayedSnapEvent::KNOT_HANDLER);
            sp_knot_handler_request_position(event, this);
            moved = true;
        }
        break;
    case GDK_ENTER_NOTIFY:
        setFlag(SP_KNOT_MOUSEOVER, true);
        setFlag(SP_KNOT_GRABBED, false);

        if (tip && desktop && desktop->event_context) {
            desktop->event_context->defaultMessageContext()->set(Inkscape::NORMAL_MESSAGE, tip);
        }
        desktop->event_context->use_cursor(_cursors[SP_KNOT_STATE_MOUSEOVER]);

        grabbed = false;
        moved = false;
        consumed = true;
        break;
    case GDK_LEAVE_NOTIFY:
        setFlag(SP_KNOT_MOUSEOVER, false);
        setFlag(SP_KNOT_GRABBED, false);

        if (tip && desktop && desktop->event_context) {
            desktop->event_context->defaultMessageContext()->clear();
        }
        desktop->event_context->use_cursor(_cursors[SP_KNOT_STATE_NORMAL]);

        grabbed = false;
        moved = false;
        consumed = true;
        break;
    case GDK_KEY_PRESS: // keybindings for knot
        switch (Inkscape::UI::Tools::get_latin_keyval(&event->key)) {
            case GDK_KEY_Escape:
                setFlag(SP_KNOT_GRABBED, false);

                if (!nograb && ctrl) {
                    ctrl->ungrab();
                }

                if (moved) {
                    setFlag(SP_KNOT_DRAGGING, false);

                    ungrabbed_signal.emit(this, event->button.state);

                    DocumentUndo::undo(desktop->getDocument());
                    desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Node or handle drag canceled."));
                    transform_escaped = true;
                    consumed = true;
                }

                grabbed = false;
                moved = false;

                desktop->event_context->discard_delayed_snap_event();
                break;
            default:
                consumed = false;
                key_press_event_unconsumed = true;
                break;
        }
        break;
    default:
        break;
    }

    knot_unref(this);

    if (key_press_event_unconsumed) {
        return false; // e.g. in case "%" was pressed to toggle snapping, or Q for quick zoom (while dragging a handle)
    } else {
        return  consumed || grabbed;
    }
}

void sp_knot_handler_request_position(GdkEvent *event, SPKnot *knot) {
    Geom::Point const motion_w(event->motion.x, event->motion.y);
    Geom::Point const motion_dt = knot->desktop->w2d(motion_w);
    Geom::Point p = motion_dt - knot->grabbed_rel_pos;

    knot->requestPosition(p, event->motion.state);
    knot->desktop->getCanvas()->enable_autoscroll();
    knot->desktop->set_coordinate_status(knot->pos); // display the coordinate of knot, not cursor - they may be different!

    if (event->motion.state & GDK_BUTTON1_MASK) {
        Inkscape::UI::Tools::gobble_motion_events(GDK_BUTTON1_MASK);
    }
}

void SPKnot::show() {
    this->setFlag(SP_KNOT_VISIBLE, true);
}

void SPKnot::hide() {
    this->setFlag(SP_KNOT_VISIBLE, false);
}

void SPKnot::requestPosition(Geom::Point const &p, guint state) {
    bool done = this->request_signal.emit(this, &const_cast<Geom::Point&>(p), state);

    /* If user did not complete, we simply move knot to new position */
    if (!done) {
        this->setPosition(p, state);
    }
}

void SPKnot::setPosition(Geom::Point const &p, guint state) {
    this->pos = p;

    if (ctrl) {
        ctrl->set_position(p);
    }

    this->moved_signal.emit(this, p, state);
}

void SPKnot::moveto(Geom::Point const &p) {
    this->pos = p;

    if (ctrl) {
        ctrl->set_position(p);
    }
}

Geom::Point SPKnot::position() const {
    return this->pos;
}

void SPKnot::setFlag(guint flag, bool set) {
    if (set) {
        this->flags |= flag;
    } else {
        this->flags &= ~flag;
    }

    switch (flag) {
    case SP_KNOT_VISIBLE:
            if (set) {
                if (ctrl) {
                    ctrl->show();
                }
            } else {
                if (ctrl) {
                    ctrl->hide();
                }
            }
            break;
    case SP_KNOT_MOUSEOVER:
    case SP_KNOT_DRAGGING:
    case SP_KNOT_SELECTED:
            this->_setCtrlState();
            break;
    case SP_KNOT_GRABBED:
            break;
    default:
            g_assert_not_reached();
            break;
    }
}

// TODO: Look at removing this and setting ctrl parameters directly.
void SPKnot::updateCtrl() {

    if (ctrl) {
        if (shape_set) {
            ctrl->set_shape(shape);
        }
        ctrl->set_mode(mode);
        if (size_set) {
            ctrl->set_size(size);
        }
        ctrl->set_angle(angle);
        ctrl->set_anchor(anchor);
    }

    _setCtrlState();
}

void SPKnot::_setCtrlState() {
    int state = SP_KNOT_STATE_NORMAL;

    if (this->flags & SP_KNOT_DRAGGING) {
        state = SP_KNOT_STATE_DRAGGING;
    } else if (this->flags & SP_KNOT_MOUSEOVER) {
        state = SP_KNOT_STATE_MOUSEOVER;
    } else if (this->flags & SP_KNOT_SELECTED) {
        state = SP_KNOT_STATE_SELECTED;
    }
    if (ctrl) {
        ctrl->set_fill(fill[state]);
        ctrl->set_stroke(stroke[state]);
    }
}


void SPKnot::setSize(guint i) {
    size = i;
    size_set = true;
}

void SPKnot::setShape(Inkscape::CanvasItemCtrlShape s) {
    shape = s;
    shape_set = true;
}

void SPKnot::setAnchor(guint i) {
    anchor = (SPAnchorType) i;
}

void SPKnot::setMode(Inkscape::CanvasItemCtrlMode m) {
    mode = m;
}

void SPKnot::setAngle(double i) {
    angle = i;
}

void SPKnot::setFill(guint32 normal, guint32 mouseover, guint32 dragging, guint32 selected) {
    fill[SP_KNOT_STATE_NORMAL] = normal;
    fill[SP_KNOT_STATE_MOUSEOVER] = mouseover;
    fill[SP_KNOT_STATE_DRAGGING] = dragging;
    fill[SP_KNOT_STATE_SELECTED] = selected;
}

void SPKnot::setStroke(guint32 normal, guint32 mouseover, guint32 dragging, guint32 selected) {
    stroke[SP_KNOT_STATE_NORMAL] = normal;
    stroke[SP_KNOT_STATE_MOUSEOVER] = mouseover;
    stroke[SP_KNOT_STATE_DRAGGING] = dragging;
    stroke[SP_KNOT_STATE_SELECTED] = selected;
}

void SPKnot::setImage(guchar* normal, guchar* mouseover, guchar* dragging, guchar* selected) {
    image[SP_KNOT_STATE_NORMAL] = normal;
    image[SP_KNOT_STATE_MOUSEOVER] = mouseover;
    image[SP_KNOT_STATE_DRAGGING] = dragging;
    image[SP_KNOT_STATE_SELECTED] = selected;
}

void SPKnot::setCursor(SPKnotStateType type, Glib::RefPtr<Gdk::Cursor> cursor)
{
    _cursors[type] = cursor;
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
