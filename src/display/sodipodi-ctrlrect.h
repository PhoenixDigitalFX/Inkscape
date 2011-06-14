#ifndef SEEN_INKSCAPE_CTRLRECT_H
#define SEEN_INKSCAPE_CTRLRECT_H

/**
 * \file sodipodi-ctrlrect.h
 * \brief Simple non-transformed rectangle, usable for rubberband
 *
 * Authors:
 *   Lauris Kaplinski <lauris@ximian.com>
 *   Carl Hetherington <inkscape@carlh.net>
 *
 * Copyright (C) 1999-2001 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 *
 * Released under GNU GPL
 *
 */

#include <glib/gtypes.h>
#include "sp-canvas-item.h"
#include "libnr/nr-rect-l.h"

struct SPCanvasBuf;

#define SP_TYPE_CTRLRECT (sp_ctrlrect_get_type ())
#define SP_CTRLRECT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), SP_TYPE_CTRLRECT, CtrlRect))
#define SP_CTRLRECT_CLASS(c) (G_TYPE_CHECK_CLASS_CAST((c), SP_TYPE_CTRLRECT, SPCtrlRectClass))
#define SP_IS_CTRLRECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SP_TYPE_CTRLRECT))
#define SP_IS_CTRLRECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SP_TYPE_CTRLRECT))

class CtrlRect : public SPCanvasItem
{
public:

    void init();
    void setColor(guint32 b, bool h, guint f);
    void setShadow(int s, guint c);
    void setRectangle(Geom::Rect const &r);
    void setDashed(bool d);

    void render(SPCanvasBuf *buf);
    void update(Geom::Affine const &affine, unsigned int flags);
    
private:
    void _requestUpdate();
    
    Geom::Rect _rect;
    bool _has_fill;
    bool _dashed;
    NRRectL _area;
    gint _shadow_size;
    guint32 _border_color;
    guint32 _fill_color;
    guint32 _shadow_color;    
    int _shadow;
};

struct SPCtrlRectClass : public SPCanvasItemClass {};

GtkType sp_ctrlrect_get_type();

#endif // SEEN_RUBBERBAND_H

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
