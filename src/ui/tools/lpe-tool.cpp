// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LPEToolContext: a context for a generic tool composed of subtools that are given by LPEs
 *
 * Authors:
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 1998 The Free Software Foundation
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 * Copyright (C) 2008 Maximilian Albert
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <iomanip>

#include <glibmm/i18n.h>
#include <gtk/gtk.h>

#include <2geom/sbasis-geometric.h>

#include "desktop.h"
#include "document.h"
#include "message-context.h"
#include "message-stack.h"
#include "selection.h"

#include "display/curve.h"
#include "display/control/canvas-item-rect.h"
#include "display/control/canvas-item-text.h"

#include "object/sp-path.h"

#include "util/units.h"

#include "ui/toolbar/lpe-toolbar.h"
#include "ui/tools/lpe-tool.h"
#include "ui/shape-editor.h"
#include "ui/widget/events/canvas-event.h"

using Inkscape::Util::unit_table;
using Inkscape::UI::Tools::PenTool;

const int num_subtools = 8;

SubtoolEntry lpesubtools[] = {
    // this must be here to account for the "all inactive" action
    {Inkscape::LivePathEffect::INVALID_LPE, "draw-geometry-inactive"},
    {Inkscape::LivePathEffect::LINE_SEGMENT, "draw-geometry-line-segment"},
    {Inkscape::LivePathEffect::CIRCLE_3PTS, "draw-geometry-circle-from-three-points"},
    {Inkscape::LivePathEffect::CIRCLE_WITH_RADIUS, "draw-geometry-circle-from-radius"},
    {Inkscape::LivePathEffect::PARALLEL, "draw-geometry-line-parallel"},
    {Inkscape::LivePathEffect::PERP_BISECTOR, "draw-geometry-line-perpendicular"},
    {Inkscape::LivePathEffect::ANGLE_BISECTOR, "draw-geometry-angle-bisector"},
    {Inkscape::LivePathEffect::MIRROR_SYMMETRY, "draw-geometry-mirror"}
};

namespace Inkscape::UI::Tools {

void sp_lpetool_context_selection_changed(Inkscape::Selection *selection, gpointer data);

LpeTool::LpeTool(SPDesktop *desktop)
    : PenTool(desktop, "/tools/lpetool", "geometric.svg")
    , mode(Inkscape::LivePathEffect::BEND_PATH)
{
    Inkscape::Selection *selection = desktop->getSelection();
    SPItem *item = selection->singleItem();

    this->sel_changed_connection.disconnect();
    this->sel_changed_connection =
        selection->connectChanged(sigc::bind(sigc::ptr_fun(&sp_lpetool_context_selection_changed), (gpointer)this));

    shape_editor = std::make_unique<ShapeEditor>(desktop);

    lpetool_context_switch_mode(this, Inkscape::LivePathEffect::INVALID_LPE);
    lpetool_context_reset_limiting_bbox(this);
    lpetool_create_measuring_items(this);

// TODO temp force:
    this->enableSelectionCue();
    
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    if (item) {
        this->shape_editor->set_item(item);
    }

    if (prefs->getBool("/tools/lpetool/selcue")) {
        this->enableSelectionCue();
    }
}

LpeTool::~LpeTool()
{
    shape_editor.reset();
    canvas_bbox.reset();
    measuring_items.clear();

    sel_changed_connection.disconnect();
}

/**
 * Callback that processes the "changed" signal on the selection;
 * destroys old and creates new nodepath and reassigns listeners to the new selected item's repr.
 */
void sp_lpetool_context_selection_changed(Inkscape::Selection *selection, gpointer data)
{
    LpeTool *lc = SP_LPETOOL_CONTEXT(data);

    lc->shape_editor->unset_item();
    SPItem *item = selection->singleItem();
    lc->shape_editor->set_item(item);
}

void LpeTool::set(const Inkscape::Preferences::Entry& val) {
    if (val.getEntryName() == "mode") {
        Inkscape::Preferences::get()->setString("/tools/geometric/mode", "drag");
        SP_PEN_CONTEXT(this)->mode = PenTool::MODE_DRAG;
    }
}

bool LpeTool::item_handler(SPItem *item, CanvasEvent const &canvas_event)
{
    auto event = canvas_event.original();
    gint ret = FALSE;

    switch (event->type) {
        case GDK_BUTTON_PRESS:
        {
            // select the clicked item but do nothing else
            Inkscape::Selection *const selection = _desktop->getSelection();
            selection->clear();
            selection->add(item);
            ret = TRUE;
            break;
        }
        case GDK_BUTTON_RELEASE:
            // TODO: do we need to catch this or can we pass it on to the parent handler?
            ret = TRUE;
            break;
        default:
            break;
    }

    if (!ret) {
        ret = PenTool::item_handler(item, canvas_event);
    }

    return ret;
}

bool LpeTool::root_handler(CanvasEvent const &canvas_event)
{
    auto event = canvas_event.original();
    Inkscape::Selection *selection = _desktop->getSelection();

    bool ret = false;

    if (this->hasWaitingLPE()) {
        // quit when we are waiting for a LPE to be applied
        return PenTool::root_handler(canvas_event);
    }

    switch (event->type) {
        case GDK_BUTTON_PRESS:
            if (event->button.button == 1) {
                if (this->mode == Inkscape::LivePathEffect::INVALID_LPE) {
                    // don't do anything for now if we are inactive (except clearing the selection
                    // since this was a click into empty space)
                    selection->clear();
                    _desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("Choose a construction tool from the toolbar."));
                    ret = true;
                    break;
                }

                // save drag origin
                xyp = { (gint) event->button.x, (gint) event->button.y };
                within_tolerance = true;

                using namespace Inkscape::LivePathEffect;

                Inkscape::Preferences *prefs = Inkscape::Preferences::get();
                int mode = prefs->getInt("/tools/lpetool/mode");
                EffectType type = lpesubtools[mode].type;

                //bool over_stroke = lc->shape_editor->is_over_stroke(Geom::Point(event->button.x, event->button.y), true);

                this->waitForLPEMouseClicks(type, Inkscape::LivePathEffect::Effect::acceptsNumClicks(type));

                // we pass the mouse click on to pen tool as the first click which it should collect
                ret = PenTool::root_handler(canvas_event);
            }
            break;


    case GDK_BUTTON_RELEASE:
    {
        /**
        break;
        **/
    }

    case GDK_KEY_PRESS:
        /**
        switch (get_latin_keyval (&event->key)) {
        }
        break;
        **/

    case GDK_KEY_RELEASE:
        /**
        switch (get_latin_keyval(&event->key)) {
            case GDK_Control_L:
            case GDK_Control_R:
                dc->_message_context->clear();
                break;
            default:
                break;
        }
        **/

    default:
        break;
    }

    if (!ret) {
        ret = PenTool::root_handler(canvas_event);
    }

    return ret;
}

/*
 * Finds the index in the list of geometric subtools corresponding to the given LPE type.
 * Returns -1 if no subtool is found.
 */
int
lpetool_mode_to_index(Inkscape::LivePathEffect::EffectType const type) {
    for (int i = 0; i < num_subtools; ++i) {
        if (lpesubtools[i].type == type) {
            return i;
        }
    }
    return -1;
}

/*
 * Checks whether an item has a construction applied as LPE and if so returns the index in
 * lpesubtools of this construction
 */
int lpetool_item_has_construction(LpeTool */*lc*/, SPItem *item)
{
    if (!is<SPLPEItem>(item)) {
        return -1;
    }

    Inkscape::LivePathEffect::Effect* lpe = cast<SPLPEItem>(item)->getCurrentLPE();
    if (!lpe) {
        return -1;
    }
    return lpetool_mode_to_index(lpe->effectType());
}

/*
 * Attempts to perform the construction of the given type (i.e., to apply the corresponding LPE) to
 * a single selected item. Returns whether we succeeded.
 */
bool
lpetool_try_construction(LpeTool *lc, Inkscape::LivePathEffect::EffectType const type)
{
    Inkscape::Selection *selection = lc->getDesktop()->getSelection();
    SPItem *item = selection->singleItem();

    // TODO: should we check whether type represents a valid geometric construction?
    if (item && is<SPLPEItem>(item) && Inkscape::LivePathEffect::Effect::acceptsNumClicks(type) == 0) {
        Inkscape::LivePathEffect::Effect::createAndApply(type, lc->getDesktop()->getDocument(), item);
        return true;
    }
    return false;
}

void
lpetool_context_switch_mode(LpeTool *lc, Inkscape::LivePathEffect::EffectType const type)
{
    int index = lpetool_mode_to_index(type);
    if (index != -1) {
        lc->mode = type;
        auto tb = dynamic_cast<UI::Toolbar::LPEToolbar*>(lc->getDesktop()->get_toolbar_by_name("LPEToolToolbar"));

        if(tb) {
            tb->set_mode(index);
        } else {
            std::cerr << "Could not access LPE toolbar" << std::endl;
        }
    } else {
        g_warning ("Invalid mode selected: %d", type);
        return;
    }
}

void
lpetool_get_limiting_bbox_corners(SPDocument *document, Geom::Point &A, Geom::Point &B) {
    Geom::Coord w = document->getWidth().value("px");
    Geom::Coord h = document->getHeight().value("px");
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    double ulx = prefs->getDouble("/tools/lpetool/bbox_upperleftx", 0);
    double uly = prefs->getDouble("/tools/lpetool/bbox_upperlefty", 0);
    double lrx = prefs->getDouble("/tools/lpetool/bbox_lowerrightx", w);
    double lry = prefs->getDouble("/tools/lpetool/bbox_lowerrighty", h);

    A = Geom::Point(ulx, uly);
    B = Geom::Point(lrx, lry);
}

/*
 * Reads the limiting bounding box from preferences and draws it on the screen
 */
// TODO: Note that currently the bbox is not user-settable; we simply use the page borders
void
lpetool_context_reset_limiting_bbox(LpeTool *lc)
{
    lc->canvas_bbox.reset();

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (!prefs->getBool("/tools/lpetool/show_bbox", true))
        return;

    SPDocument *document = lc->getDesktop()->getDocument();

    Geom::Point A, B;
    lpetool_get_limiting_bbox_corners(document, A, B);
    Geom::Affine doc2dt(lc->getDesktop()->doc2dt());
    A *= doc2dt;
    B *= doc2dt;

    Geom::Rect rect(A, B);
    lc->canvas_bbox = make_canvasitem<CanvasItemRect>(lc->getDesktop()->getCanvasControls(), rect);
    lc->canvas_bbox->set_stroke(0x0000ffff);
    lc->canvas_bbox->set_dashed(true);
}

static void
set_pos_and_anchor(Inkscape::CanvasItemText *canvas_text, const Geom::Piecewise<Geom::D2<Geom::SBasis> > &pwd2,
                   const double t, const double length, bool /*use_curvature*/ = false)
{
    using namespace Geom;

    Piecewise<D2<SBasis> > pwd2_reparam = arc_length_parametrization(pwd2, 2 , 0.1);
    double t_reparam = pwd2_reparam.cuts.back() * t;
    Point pos = pwd2_reparam.valueAt(t_reparam);
    Point dir = unit_vector(derivative(pwd2_reparam).valueAt(t_reparam));
    Point n = -rot90(dir);
    double angle = Geom::angle_between(dir, Point(1,0));

    canvas_text->set_coord(pos + n * length);
    canvas_text->set_anchor(Geom::Point(std::sin(angle), -std::cos(angle)));
}

void
lpetool_create_measuring_items(LpeTool *lc, Inkscape::Selection *selection)
{
    if (!selection) {
        selection = lc->getDesktop()->getSelection();
    }
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool show = prefs->getBool("/tools/lpetool/show_measuring_info",  true);

    Inkscape::CanvasItemGroup *tmpgrp = lc->getDesktop()->getCanvasTemp();

    Inkscape::Util::Unit const * unit = nullptr;
    if (prefs->getString("/tools/lpetool/unit").compare("")) {
        unit = unit_table.getUnit(prefs->getString("/tools/lpetool/unit"));
    } else {
        unit = unit_table.getUnit("px");
    }

    auto items= selection->items();
    for (auto i : items) {
        auto path = cast<SPPath>(i);
        if (path) {
            SPCurve const *curve = path->curve();
            Geom::Piecewise<Geom::D2<Geom::SBasis> > pwd2 = paths_to_pw(curve->get_pathvector());

            double lengthval = Geom::length(pwd2);
            lengthval = Inkscape::Util::Quantity::convert(lengthval, "px", unit);

            Glib::ustring arc_length = Glib::ustring::format(std::setprecision(2), std::fixed, lengthval);
            arc_length += " ";
            arc_length += unit->abbr;

            auto canvas_text = make_canvasitem<CanvasItemText>(tmpgrp, Geom::Point(0,0), arc_length);
            set_pos_and_anchor(canvas_text.get(), pwd2, 0.5, 10);
            if (!show) {
                canvas_text->set_visible(false);
            }

            lc->measuring_items[path] = std::move(canvas_text);
        }
    }
}

void lpetool_delete_measuring_items(LpeTool *lc)
{
    lc->measuring_items.clear();
}

void
lpetool_update_measuring_items(LpeTool *lc)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Inkscape::Util::Unit const * unit = nullptr;
    if (prefs->getString("/tools/lpetool/unit").compare("")) {
        unit = unit_table.getUnit(prefs->getString("/tools/lpetool/unit"));
    } else {
        unit = unit_table.getUnit("px");
    }

    for (auto& i : lc->measuring_items) {

        SPPath *path = i.first;
        SPCurve const *curve = path->curve();
        Geom::Piecewise<Geom::D2<Geom::SBasis> > pwd2 = Geom::paths_to_pw(curve->get_pathvector());
        double lengthval = Geom::length(pwd2);
        lengthval = Inkscape::Util::Quantity::convert(lengthval, "px", unit);

        Glib::ustring arc_length = Glib::ustring::format(std::setprecision(2), std::fixed, lengthval);
        arc_length += " ";
        arc_length += unit->abbr;

        i.second->set_text(std::move(arc_length));
        set_pos_and_anchor(i.second.get(), pwd2, 0.5, 10);
    }
}

void
lpetool_show_measuring_info(LpeTool *lc, bool show)
{
    for (auto& i : lc->measuring_items) {
        if (show) {
            i.second->set_visible(true);
        } else {
            i.second->set_visible(false);
        }
    }
}

} // namespace Inkscape::UI::Tools

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
