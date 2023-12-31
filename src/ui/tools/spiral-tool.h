// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __SP_SPIRAL_CONTEXT_H__
#define __SP_SPIRAL_CONTEXT_H__

/** \file
 * Spiral drawing context
 */
/*
 * Authors:
 *   Mitsuru Oka
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 1999-2001 Lauris Kaplinski
 * Copyright (C) 2001-2002 Mitsuru Oka
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <sigc++/connection.h>
#include <2geom/point.h>
#include "ui/tools/tool-base.h"
#include "object/weakptr.h"

#define SP_SPIRAL_CONTEXT(obj) (dynamic_cast<Inkscape::UI::Tools::SpiralTool*>((Inkscape::UI::Tools::ToolBase*)obj))
#define SP_IS_SPIRAL_CONTEXT(obj) (dynamic_cast<const Inkscape::UI::Tools::SpiralTool*>((const Inkscape::UI::Tools::ToolBase*)obj) != NULL)

class SPSpiral;

namespace Inkscape {

class Selection;

namespace UI {
namespace Tools {

class SpiralTool : public ToolBase
{
public:
    SpiralTool(SPDesktop *desktop);
    ~SpiralTool() override;

    void set(Preferences::Entry const &val) override;
    bool root_handler(CanvasEvent const &event) override;

private:
    SPWeakPtr<SPSpiral> spiral;
    Geom::Point center;
    double revo;
    double exp;
    double t0;

    sigc::connection sel_changed_connection;

    void drag(Geom::Point const &p, unsigned state);
	void finishItem();
	void cancel();
    void selection_changed(Selection *selection);
};

}
}
}

#endif
