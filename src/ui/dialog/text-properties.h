/**
 * \brief Text Properties dialog
 *
 * Authors:
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *
 * Copyright (C) 2004, 2005 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_DIALOG_TEXT_PROPERTIES_H
#define INKSCAPE_UI_DIALOG_TEXT_PROPERTIES_H

#include <gtkmm/notebook.h>
#include <glibmm/i18n.h>

#include "ui/widget/panel.h"
#include "ui/widget/notebook-page.h"

using namespace Inkscape::UI::Widget;

namespace Inkscape {
namespace UI {
namespace Dialog {

class TextProperties : public UI::Widget::Panel {
public:
    TextProperties();
    virtual ~TextProperties();

    static int get_verb();

    static TextProperties &getInstance() { return *new TextProperties(); }

protected:
    Gtk::Notebook  _notebook;

    NotebookPage   _page_font;
    NotebookPage   _page_text;

private:
    TextProperties(TextProperties const &d);
    TextProperties& operator=(TextProperties const &d);
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_DIALOG_TEXT_PROPERTIES_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
