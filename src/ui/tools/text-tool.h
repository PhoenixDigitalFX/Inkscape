// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __SP_TEXT_CONTEXT_H__
#define __SP_TEXT_CONTEXT_H__

/*
 * TextTool
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <sigc++/connection.h>

#include "ui/tools/tool-base.h"
#include <2geom/point.h>
#include "libnrtype/Layout-TNG.h"
#include "display/control/canvas-item-ptr.h"

#define SP_TEXT_CONTEXT(obj) (dynamic_cast<Inkscape::UI::Tools::TextTool*>((Inkscape::UI::Tools::ToolBase*)obj))
#define SP_IS_TEXT_CONTEXT(obj) (dynamic_cast<const Inkscape::UI::Tools::TextTool*>((const Inkscape::UI::Tools::ToolBase*)obj) != NULL)

typedef struct _GtkIMContext GtkIMContext;

namespace Inkscape {

class CanvasItemCurve; // Cursor
class CanvasItemQuad;  // Highlighted text
class CanvasItemRect;  // Indicator, Frame
class CanvasItemBpath;
class Selection;

namespace UI {
namespace Tools {

class TextTool : public ToolBase
{
public:
    TextTool(SPDesktop *desktop);
    ~TextTool() override;

    sigc::connection sel_changed_connection;
    sigc::connection sel_modified_connection;
    sigc::connection style_set_connection;
    sigc::connection style_query_connection;

    GtkIMContext *imc = nullptr;

    SPItem *text = nullptr; // the text we're editing, or NULL if none selected

    /* Text item position in root coordinates */
    Geom::Point pdoc;
    /* Insertion point position */
    Text::Layout::iterator text_sel_start;
    Text::Layout::iterator text_sel_end;

    char uni[9];
    bool unimode = false;
    unsigned unipos = 0;

    // ---- On canvas editing ---
    CanvasItemPtr<CanvasItemCurve> cursor;
    CanvasItemPtr<CanvasItemRect> indicator;
    CanvasItemPtr<CanvasItemBpath> frame; // Highlighting flowtext shapes or textpath path
    CanvasItemPtr<CanvasItemBpath> padding_frame; // Highlighting flowtext padding
    std::vector<CanvasItemPtr<CanvasItemQuad>> text_selection_quads;

    int timeout = 0;
    bool show = false;
    bool phase = false;
    bool nascent_object = false; // true if we're clicked on canvas to put cursor,
                                 // but no text typed yet so ->text is still NULL

    bool over_text = false; // true if cursor is over a text object

    unsigned dragging = 0;     // dragging selection over text
    bool creating = false;  // dragging rubberband to create flowtext
    Geom::Point p0;         // initial point if the flowtext rect

    /* Preedit String */
    char* preedit_string = nullptr;

    bool root_handler(CanvasEvent const &event) override;
    bool item_handler(SPItem *item, CanvasEvent const &event) override;
    void deleteSelected();

private:
    void _selectionChanged(Selection *selection);
    void _selectionModified(Selection *selection, unsigned flags);
    bool _styleSet(SPCSSAttr const *css);
    int _styleQueried(SPStyle *style, int property);
};

bool sp_text_paste_inline(ToolBase *ec);
Glib::ustring sp_text_get_selected_text(ToolBase const *ec);
SPCSSAttr *sp_text_get_style_at_cursor(ToolBase const *ec);
// std::vector<SPCSSAttr*> sp_text_get_selected_style(ToolBase const *ec, unsigned *k, int *b, std::vector<unsigned>
// *positions);
bool sp_text_delete_selection(ToolBase *ec);
void sp_text_context_place_cursor (TextTool *tc, SPObject *text, Inkscape::Text::Layout::iterator where);
void sp_text_context_place_cursor_at (TextTool *tc, SPObject *text, Geom::Point const p);
Inkscape::Text::Layout::iterator *sp_text_context_get_cursor_position(TextTool *tc, SPObject *text);

}
}
}

#endif

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
