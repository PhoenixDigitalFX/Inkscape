// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_ARC_CONTEXT_H
#define SEEN_ARC_CONTEXT_H

/*
 * Ellipse drawing context
 *
 * Authors:
 *   Mitsuru Oka
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 2000-2002 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2002 Mitsuru Oka
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstddef>

#include <2geom/point.h>
#include <sigc++/connection.h>

#include "ui/tools/tool-base.h"
#include "object/weakptr.h"

class SPItem;
class SPGenericEllipse;

namespace Inkscape {
    class Selection;
}

#define SP_ARC_CONTEXT(obj) (dynamic_cast<Inkscape::UI::Tools::ArcTool*>((Inkscape::UI::Tools::ToolBase*)obj))
#define SP_IS_ARC_CONTEXT(obj) (dynamic_cast<const Inkscape::UI::Tools::ArcTool*>(obj) != NULL)

namespace Inkscape {
namespace UI {
namespace Tools {

class ArcTool : public ToolBase
{
public:
    ArcTool(SPDesktop *desktop);
    ~ArcTool() override;

    bool root_handler(CanvasEvent const &event) override;
    bool item_handler(SPItem *item, CanvasEvent const &event) override;

private:
    SPWeakPtr<SPGenericEllipse> arc;

    Geom::Point center;

    sigc::connection sel_changed_connection;

    void selection_changed(Selection *selection);

    void drag(Geom::Point pt, unsigned state);
	void finishItem();
	void cancel();
};

}
}
}

#endif /* !SEEN_ARC_CONTEXT_H */

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
