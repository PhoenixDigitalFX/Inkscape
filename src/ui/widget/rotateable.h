// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   buliabyak@gmail.com
 *
 * Copyright (C) 2007 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_ROTATEABLE_H
#define INKSCAPE_UI_ROTATEABLE_H

#include <gtkmm/eventbox.h>

namespace Inkscape::UI::Widget {

/**
 * Widget adjustable by dragging it to rotate away from a zero-change axis.
 */
class Rotateable: public Gtk::EventBox
{
public:
    Rotateable();
    ~Rotateable() override;

    double axis;
    double current_axis;
    double maxdecl;
    bool scrolling;

private:
    double drag_started_x;
    double drag_started_y;
    unsigned modifier;
    bool dragging;
    bool working;

    unsigned get_single_modifier(unsigned old, unsigned state);

    bool on_click  (GdkEventButton *event);
    bool on_motion (GdkEventMotion *event);
    bool on_release(GdkEventButton *event);
    bool on_scroll (GdkEventScroll* event);

    virtual void do_motion (double /*by*/, unsigned /*state*/) {}
    virtual void do_release(double /*by*/, unsigned /*state*/) {}
    virtual void do_scroll (double /*by*/, unsigned /*state*/) {}
};

} // namespace Inkscape::UI::Widget

#endif // INKSCAPE_UI_ROTATEABLE_H

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
