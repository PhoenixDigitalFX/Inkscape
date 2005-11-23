/**
 * \brief Selected style indicator (fill, stroke, opacity)
 *
 * Author:
 *   buliabyak@gmail.com
 *
 * Copyright (C) 2005 author
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gtkmm/separatormenuitem.h>
#include <gtkmm/separator.h>

#include "selected-style.h"

#include "widgets/spw-utilities.h"
#include "ui/widget/color-preview.h"

#include "selection.h"
#include "desktop-handles.h"
#include "style.h"
#include "desktop-style.h"
#include "color.h"
#include "sp-linear-gradient-fns.h"
#include "sp-radial-gradient-fns.h"
#include "sp-pattern.h"
#include "dialogs/object-properties.h"
#include "xml/repr.h"
#include "document.h"
#include "widgets/widget-sizes.h"
#include "widgets/spinbutton-events.h"
#include "svg/svg.h"
#include "svg/css-ostringstream.h"

static void 
ss_selection_changed (Inkscape::Selection *, gpointer data)
{
    Inkscape::UI::Widget::SelectedStyle *ss = (Inkscape::UI::Widget::SelectedStyle *) data;
    ss->update();
}

static void
ss_selection_modified (Inkscape::Selection *selection, guint flags, gpointer data)
{
    ss_selection_changed (selection, data);
}

static void
ss_subselection_changed (gpointer dragger, gpointer data)
{
    ss_selection_changed (NULL, data);
}

namespace Inkscape {
namespace UI {
namespace Widget {

SelectedStyle::SelectedStyle(bool layout)
    : _desktop (NULL),

      _table(2, 6),
      _fill_label (_("F:")),
      _stroke_label (_("S:")),
      _opacity_label (_("O:")),
      _fill_place (),
      _stroke_place (),

      _fill_flag_place (),
      _stroke_flag_place (),

      _opacity_place (),
      _opacity_adjustment (1.0, 0.0, 1.0, 0.01, 0.1),
      _opacity_sb (0.02, 2),
      _opacity_blocked(false),

      _tooltips ()
{
    _fill_label.set_alignment(0.0, 0.5);
    _fill_label.set_padding(0, 0);
    _stroke_label.set_alignment(0.0, 0.5);
    _stroke_label.set_padding(0, 0);
    _opacity_label.set_alignment(0.0, 0.5);
    _opacity_label.set_padding(0, 0);

    _table.set_col_spacings (2);
    _table.set_row_spacings (0);

    for (int i = SS_FILL; i <= SS_STROKE; i++) {

        _na[i].set_markup (_("N/A"));
        sp_set_font_size_smaller_smaller (GTK_WIDGET(_na[i].gobj()));
        _na[i].show_all();
        __na[i] = (_("Nothing selected"));

        _none[i].set_markup (_("None"));
        sp_set_font_size_smaller_smaller (GTK_WIDGET(_none[i].gobj()));
        _none[i].show_all();
        __none[i] = (i == SS_FILL)? (_("No fill")) : (_("No stroke"));

        _pattern[i].set_markup (_("Pattern"));
        sp_set_font_size_smaller_smaller (GTK_WIDGET(_pattern[i].gobj()));
        _pattern[i].show_all();
        __pattern[i] = (i == SS_FILL)? (_("Pattern fill")) : (_("Pattern stroke"));

        _lgradient[i].set_markup (_("L Gradient"));
        sp_set_font_size_smaller_smaller (GTK_WIDGET(_lgradient[i].gobj()));
        _lgradient[i].show_all();
        __lgradient[i] = (i == SS_FILL)? (_("Linear gradient fill")) : (_("Linear gradient stroke"));

        _rgradient[i].set_markup (_("R Gradient"));
        sp_set_font_size_smaller_smaller (GTK_WIDGET(_rgradient[i].gobj()));
        _rgradient[i].show_all();
        __rgradient[i] = (i == SS_FILL)? (_("Radial gradient fill")) : (_("Radial gradient stroke"));

        _many[i].set_markup (_("Different"));
        sp_set_font_size_smaller_smaller (GTK_WIDGET(_many[i].gobj()));
        _many[i].show_all();
        __many[i] = (i == SS_FILL)? (_("Different fills")) : (_("Different strokes"));

        _unset[i].set_markup (_("Unset"));
        sp_set_font_size_smaller_smaller (GTK_WIDGET(_unset[i].gobj()));
        _unset[i].show_all();
        __unset[i] = (i == SS_FILL)? (_("Unset fill")) : (_("Unset stroke"));

        _color_preview[i] = new Inkscape::UI::Widget::ColorPreview (0);
        __color[i] = (i == SS_FILL)? (_("Flat color fill")) : (_("Flat color stroke"));

        // TRANSLATOR COMMENT: A means "Averaged"
        _averaged[i].set_markup (_("A"));
        sp_set_font_size_smaller_smaller (GTK_WIDGET(_averaged[i].gobj()));
        _averaged[i].show_all();
        __averaged[i] = (i == SS_FILL)? (_("Fill is averaged over selected objects")) : (_("Stroke is averaged over selected objects"));

        // TRANSLATOR COMMENT: M means "Multiple"
        _multiple[i].set_markup (_("M"));
        sp_set_font_size_smaller_smaller (GTK_WIDGET(_multiple[i].gobj()));
        _multiple[i].show_all();
        __multiple[i] = (i == SS_FILL)? (_("Multiple selected objects have the same fill")) : (_("Multiple selected objects have the same stroke"));

        _popup_edit[i].add(*(new Gtk::Label((i == SS_FILL)? _("Edit fill...") : _("Edit stroke..."), 0.0, 0.5)));
        _popup_edit[i].signal_activate().connect(sigc::mem_fun(*this, 
                               (i == SS_FILL)? &SelectedStyle::on_fill_edit : &SelectedStyle::on_stroke_edit ));

        _popup_lastused[i].add(*(new Gtk::Label(_("Last set color"), 0.0, 0.5)));
        _popup_lastused[i].signal_activate().connect(sigc::mem_fun(*this, 
                               (i == SS_FILL)? &SelectedStyle::on_fill_lastused : &SelectedStyle::on_stroke_lastused ));

        _popup_lastselected[i].add(*(new Gtk::Label(_("Last selected color"), 0.0, 0.5)));
        _popup_lastselected[i].signal_activate().connect(sigc::mem_fun(*this, 
                               (i == SS_FILL)? &SelectedStyle::on_fill_lastselected : &SelectedStyle::on_stroke_lastselected ));

        _popup_white[i].add(*(new Gtk::Label(_("White"), 0.0, 0.5)));
        _popup_white[i].signal_activate().connect(sigc::mem_fun(*this, 
                               (i == SS_FILL)? &SelectedStyle::on_fill_white : &SelectedStyle::on_stroke_white ));

        _popup_black[i].add(*(new Gtk::Label(_("Black"), 0.0, 0.5)));
        _popup_black[i].signal_activate().connect(sigc::mem_fun(*this, 
                               (i == SS_FILL)? &SelectedStyle::on_fill_black : &SelectedStyle::on_stroke_black ));

        _popup_copy[i].add(*(new Gtk::Label(_("Copy color"), 0.0, 0.5)));
        _popup_copy[i].signal_activate().connect(sigc::mem_fun(*this, 
                               (i == SS_FILL)? &SelectedStyle::on_fill_copy : &SelectedStyle::on_stroke_copy ));

        _popup_paste[i].add(*(new Gtk::Label(_("Paste color"), 0.0, 0.5)));
        _popup_paste[i].signal_activate().connect(sigc::mem_fun(*this, 
                               (i == SS_FILL)? &SelectedStyle::on_fill_paste : &SelectedStyle::on_stroke_paste ));

        _popup_swap[i].add(*(new Gtk::Label(_("Swap fill and stroke"), 0.0, 0.5)));
        _popup_swap[i].signal_activate().connect(sigc::mem_fun(*this, 
                               &SelectedStyle::on_fillstroke_swap));

        _popup_opaque[i].add(*(new Gtk::Label((i == SS_FILL)? _("Make fill opaque") : _("Make stroke opaque"), 0.0, 0.5)));
        _popup_opaque[i].signal_activate().connect(sigc::mem_fun(*this, 
                               (i == SS_FILL)? &SelectedStyle::on_fill_opaque : &SelectedStyle::on_stroke_opaque ));

        //TRANSLATORS COMMENT: unset is a verb here
        _popup_unset[i].add(*(new Gtk::Label((i == SS_FILL)? _("Unset fill") : _("Unset stroke"), 0.0, 0.5)));
        _popup_unset[i].signal_activate().connect(sigc::mem_fun(*this, 
                               (i == SS_FILL)? &SelectedStyle::on_fill_unset : &SelectedStyle::on_stroke_unset ));

        _popup_remove[i].add(*(new Gtk::Label((i == SS_FILL)? _("Remove fill") : _("Remove stroke"), 0.0, 0.5)));
        _popup_remove[i].signal_activate().connect(sigc::mem_fun(*this, 
                               (i == SS_FILL)? &SelectedStyle::on_fill_remove : &SelectedStyle::on_stroke_remove ));

        _popup[i].attach(_popup_edit[i], 0,1, 0,1);
          _popup[i].attach(*(new Gtk::SeparatorMenuItem()), 0,1, 1,2);
        _popup[i].attach(_popup_lastused[i], 0,1, 2,3);
        _popup[i].attach(_popup_lastselected[i], 0,1, 3,4);
          _popup[i].attach(*(new Gtk::SeparatorMenuItem()), 0,1, 4,5);
        _popup[i].attach(_popup_white[i], 0,1, 5,6);
        _popup[i].attach(_popup_black[i], 0,1, 6,7);
          _popup[i].attach(*(new Gtk::SeparatorMenuItem()), 0,1, 7,8);
        _popup[i].attach(_popup_copy[i], 0,1, 8,9);
        _popup_copy[i].set_sensitive(false);
        _popup[i].attach(_popup_paste[i], 0,1, 9,10);
        _popup[i].attach(_popup_swap[i], 0,1, 10,11);
          _popup[i].attach(*(new Gtk::SeparatorMenuItem()), 0,1, 11,12); 
        _popup[i].attach(_popup_opaque[i], 0,1, 12,13);
        _popup[i].attach(_popup_unset[i], 0,1, 13,14);
        _popup[i].attach(_popup_remove[i], 0,1, 14,15);
        _popup[i].show_all();

        _mode[i] = SS_NA;
    }

    _fill_place.signal_button_press_event().connect(sigc::mem_fun(*this, &SelectedStyle::on_fill_click));
    _stroke_place.signal_button_press_event().connect(sigc::mem_fun(*this, &SelectedStyle::on_stroke_click));
    _opacity_place.signal_button_press_event().connect(sigc::mem_fun(*this, &SelectedStyle::on_opacity_click));

    _opacity_sb.signal_populate_popup().connect(sigc::mem_fun(*this, &SelectedStyle::on_opacity_menu));
    _opacity_sb.signal_value_changed().connect(sigc::mem_fun(*this, &SelectedStyle::on_opacity_changed));

    _fill_place.add(_na[SS_FILL]);
    _tooltips.set_tip(_fill_place, __na[SS_FILL]);
    _stroke_place.add(_na[SS_STROKE]);
    _tooltips.set_tip(_stroke_place, __na[SS_STROKE]);

    _opacity_sb.set_adjustment(_opacity_adjustment);
    sp_set_font_size_smaller (GTK_WIDGET(_opacity_sb.gobj()));
    _opacity_sb.set_size_request (SELECTED_STYLE_SB_WIDTH, -1);
    _opacity_sb.set_sensitive (false);

    _table.attach(_fill_label, 0,1, 0,1, Gtk::SHRINK, Gtk::SHRINK);
    _table.attach(_stroke_label, 0,1, 1,2, Gtk::SHRINK, Gtk::SHRINK);

    _table.attach(_fill_place, 1,2, 0,1);
    _table.attach(_stroke_place, 1,2, 1,2);

    _table.attach(_fill_flag_place, 2,3, 0,1, Gtk::SHRINK, Gtk::SHRINK);
    _table.attach(_stroke_flag_place, 2,3, 1,2, Gtk::SHRINK, Gtk::SHRINK);

    _table.attach(*(new Gtk::VSeparator), 3,4, 0,2, Gtk::SHRINK);

    _opacity_place.add(_opacity_label);
    _table.attach(_opacity_place, 4,5, 0,1, Gtk::SHRINK, Gtk::SHRINK);
    _table.attach(_opacity_sb, 5,6, 0,2, Gtk::SHRINK, Gtk::SHRINK);

    pack_start(_table, true, true, 2);

    set_size_request (SELECTED_STYLE_WIDTH, -1);

    sp_set_font_size_smaller_smaller (GTK_WIDGET(_opacity_label.gobj()));
    sp_set_font_size_smaller_smaller (GTK_WIDGET(_fill_place.gobj()));
    sp_set_font_size_smaller_smaller (GTK_WIDGET(_fill_flag_place.gobj()));
    sp_set_font_size_smaller_smaller (GTK_WIDGET(_stroke_place.gobj()));
    sp_set_font_size_smaller_smaller (GTK_WIDGET(_stroke_flag_place.gobj()));
    sp_set_font_size_smaller_smaller (GTK_WIDGET(_fill_label.gobj()));
    sp_set_font_size_smaller_smaller (GTK_WIDGET(_stroke_label.gobj()));
}

SelectedStyle::~SelectedStyle()
{
    selection_changed_connection->disconnect();
    delete selection_changed_connection;
    selection_modified_connection->disconnect();
    delete selection_modified_connection;
    subselection_changed_connection->disconnect();
    delete subselection_changed_connection;

    for (int i = SS_FILL; i <= SS_STROKE; i++) {
        delete _color_preview[i];
    }
}

void
SelectedStyle::setDesktop(SPDesktop *desktop)
{
    _desktop = desktop;
    gtk_object_set_data (GTK_OBJECT(_opacity_sb.gobj()), "dtw", _desktop->canvas);

    Inkscape::Selection *selection = SP_DT_SELECTION (desktop);

    selection_changed_connection = new sigc::connection (selection->connectChanged(
        sigc::bind (
            sigc::ptr_fun(&ss_selection_changed),
            this )
    ));
    selection_modified_connection = new sigc::connection (selection->connectModified(
        sigc::bind (
            sigc::ptr_fun(&ss_selection_modified),
            this )
    ));
    subselection_changed_connection = new sigc::connection (desktop->connectToolSubselectionChanged(
        sigc::bind (
            sigc::ptr_fun(&ss_subselection_changed),
            this )
    ));
}

void SelectedStyle::on_fill_remove() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    sp_repr_css_set_property (css, "fill", "none");
    sp_desktop_set_style (_desktop, css, true, false); // do not write to current, to preserve current color
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_stroke_remove() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    sp_repr_css_set_property (css, "stroke", "none");
    sp_desktop_set_style (_desktop, css, true, false); // do not write to current, to preserve current color
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_fill_unset() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    sp_repr_css_unset_property (css, "fill");
    sp_desktop_set_style (_desktop, css, true, false); // do not write to current, to preserve current color
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_stroke_unset() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    sp_repr_css_unset_property (css, "stroke");
    sp_desktop_set_style (_desktop, css, true, false); // do not write to current, to preserve current color
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_fill_opaque() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    sp_repr_css_set_property (css, "fill-opacity", "1");
    sp_desktop_set_style (_desktop, css, true);
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_stroke_opaque() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    sp_repr_css_set_property (css, "stroke-opacity", "1");
    sp_desktop_set_style (_desktop, css, true);
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_fill_lastused() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    guint32 color = sp_desktop_get_color(_desktop, true);
    gchar c[64];
    sp_svg_write_color (c, 64, color);
    sp_repr_css_set_property (css, "fill", c);
    sp_desktop_set_style (_desktop, css);
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_stroke_lastused() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    guint32 color = sp_desktop_get_color(_desktop, false);
    gchar c[64];
    sp_svg_write_color (c, 64, color);
    sp_repr_css_set_property (css, "stroke", c);
    sp_desktop_set_style (_desktop, css);
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_fill_lastselected() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    gchar c[64];
    sp_svg_write_color (c, 64, _lastselected[SS_FILL]);
    sp_repr_css_set_property (css, "fill", c);
    sp_desktop_set_style (_desktop, css);
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_stroke_lastselected() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    gchar c[64];
    sp_svg_write_color (c, 64, _lastselected[SS_STROKE]);
    sp_repr_css_set_property (css, "stroke", c);
    sp_desktop_set_style (_desktop, css);
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_fill_white() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    gchar c[64];
    sp_svg_write_color (c, 64, 0xffffffff);
    sp_repr_css_set_property (css, "fill", c);
    sp_repr_css_set_property (css, "fill-opacity", "1");
    sp_desktop_set_style (_desktop, css);
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_stroke_white() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    gchar c[64];
    sp_svg_write_color (c, 64, 0xffffffff);
    sp_repr_css_set_property (css, "stroke", c);
    sp_repr_css_set_property (css, "stroke-opacity", "1");
    sp_desktop_set_style (_desktop, css);
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_fill_black() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    gchar c[64];
    sp_svg_write_color (c, 64, 0x000000ff);
    sp_repr_css_set_property (css, "fill", c);
    sp_repr_css_set_property (css, "fill-opacity", "1.0");
    sp_desktop_set_style (_desktop, css);
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_stroke_black() {
    SPCSSAttr *css = sp_repr_css_attr_new ();
    gchar c[64];
    sp_svg_write_color (c, 64, 0x000000ff);
    sp_repr_css_set_property (css, "stroke", c);
    sp_repr_css_set_property (css, "stroke-opacity", "1.0");
    sp_desktop_set_style (_desktop, css);
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_fill_copy() {
    if (_mode[SS_FILL] == SS_COLOR) {
        gchar c[64];
        sp_svg_write_color (c, 64, _thisselected[SS_FILL]);
        Glib::ustring text;
        text += c;
        if (!text.empty()) {
            Glib::RefPtr<Gtk::Clipboard> refClipboard = Gtk::Clipboard::get();
            refClipboard->set_text(text);
        }
    }
}

void SelectedStyle::on_stroke_copy() {
    if (_mode[SS_STROKE] == SS_COLOR) {
        gchar c[64];
        sp_svg_write_color (c, 64, _thisselected[SS_STROKE]);
        Glib::ustring text;
        text += c;
        if (!text.empty()) {
            Glib::RefPtr<Gtk::Clipboard> refClipboard = Gtk::Clipboard::get();
            refClipboard->set_text(text);
        }
    }
}

void SelectedStyle::on_fill_paste() {
    Glib::RefPtr<Gtk::Clipboard> refClipboard = Gtk::Clipboard::get();
    Glib::ustring const text = refClipboard->wait_for_text();

    if (!text.empty()) {
        guint32 color = sp_svg_read_color(text.c_str(), 0x000000ff); // impossible value, as SVG color cannot have opacity
        if (color == 0x000000ff) // failed to parse color string
            return;

        SPCSSAttr *css = sp_repr_css_attr_new ();
        sp_repr_css_set_property (css, "fill", text.c_str());
        sp_desktop_set_style (_desktop, css);
        sp_repr_css_attr_unref (css);
        sp_document_done (SP_DT_DOCUMENT(_desktop));
    }
}

void SelectedStyle::on_stroke_paste() {
    Glib::RefPtr<Gtk::Clipboard> refClipboard = Gtk::Clipboard::get();
    Glib::ustring const text = refClipboard->wait_for_text();

    if (!text.empty()) {
        guint32 color = sp_svg_read_color(text.c_str(), 0x000000ff); // impossible value, as SVG color cannot have opacity
        if (color == 0x000000ff) // failed to parse color string
            return;

        SPCSSAttr *css = sp_repr_css_attr_new ();
        sp_repr_css_set_property (css, "stroke", text.c_str());
        sp_desktop_set_style (_desktop, css);
        sp_repr_css_attr_unref (css);
        sp_document_done (SP_DT_DOCUMENT(_desktop));
    }
}

void SelectedStyle::on_fillstroke_swap() {
    SPCSSAttr *css = sp_repr_css_attr_new ();

    switch (_mode[SS_FILL]) {
    case SS_NA:
    case SS_MANY:
        break;
    case SS_NONE:
        sp_repr_css_set_property (css, "stroke", "none");
        break;
    case SS_UNSET:
        sp_repr_css_unset_property (css, "stroke");
        break;
    case SS_COLOR:
        gchar c[64];
        sp_svg_write_color (c, 64, _thisselected[SS_FILL]);
        sp_repr_css_set_property (css, "stroke", c);
        break;
    case SS_LGRADIENT:
    case SS_RGRADIENT:
    case SS_PATTERN:
        sp_repr_css_set_property (css, "stroke", _paintserver_id[SS_FILL].c_str());
        break;
    }

    switch (_mode[SS_STROKE]) {
    case SS_NA:
    case SS_MANY:
        break;
    case SS_NONE:
        sp_repr_css_set_property (css, "fill", "none");
        break;
    case SS_UNSET:
        sp_repr_css_unset_property (css, "fill");
        break;
    case SS_COLOR:
        gchar c[64];
        sp_svg_write_color (c, 64, _thisselected[SS_STROKE]);
        sp_repr_css_set_property (css, "fill", c);
        break;
    case SS_LGRADIENT:
    case SS_RGRADIENT:
    case SS_PATTERN:
        sp_repr_css_set_property (css, "fill", _paintserver_id[SS_STROKE].c_str());
        break;
    }

    sp_desktop_set_style (_desktop, css);
    sp_repr_css_attr_unref (css);
    sp_document_done (SP_DT_DOCUMENT(_desktop));
}

void SelectedStyle::on_fill_edit() {
    sp_object_properties_fill();
}

void SelectedStyle::on_stroke_edit() {
    sp_object_properties_stroke();
}

bool 
SelectedStyle::on_fill_click(GdkEventButton *event)
{
    if (event->button == 1) { // click, open fill&stroke
        sp_object_properties_fill();
    } else if (event->button == 3) { // right-click, popup menu
        _popup[SS_FILL].popup(event->button, event->time);
    } else if (event->button == 2) { // middle click, toggle none/lastcolor
        if (_mode[SS_FILL] == SS_NONE) {
            on_fill_lastused();
        } else {
            on_fill_remove();
        }
    }
    return true;
}

bool 
SelectedStyle::on_stroke_click(GdkEventButton *event)
{
    if (event->button == 1) { // click, open fill&stroke
        sp_object_properties_stroke();
    } else if (event->button == 3) { // right-click, popup menu
        _popup[SS_STROKE].popup(event->button, event->time);
    } else if (event->button == 2) { // middle click, toggle none/lastcolor
        if (_mode[SS_STROKE] == SS_NONE) {
            on_stroke_lastused();
        } else {
            on_stroke_remove();
        }
    }
    return true;
}

bool 
SelectedStyle::on_opacity_click(GdkEventButton *event)
{
    if (event->button == 2) { // middle click
        const char* opacity = _opacity_sb.get_value() < 0.5? "0.5" : (_opacity_sb.get_value() == 1? "0" : "1");
        SPCSSAttr *css = sp_repr_css_attr_new ();
        sp_repr_css_set_property (css, "opacity", opacity);
        sp_desktop_set_style (_desktop, css);
        sp_repr_css_attr_unref (css);
        sp_document_done (SP_DT_DOCUMENT (_desktop));
        return true;
    }

    return false;
}

void
SelectedStyle::update()
{
    if (_desktop == NULL)
        return;

    // create temporary style
    SPStyle *query = sp_style_new ();

    for (int i = SS_FILL; i <= SS_STROKE; i++) {
        Gtk::EventBox *place = (i == SS_FILL)? &_fill_place : &_stroke_place;
        Gtk::EventBox *flag_place = (i == SS_FILL)? &_fill_flag_place : &_stroke_flag_place;

        place->remove();
        flag_place->remove();

        _tooltips.unset_tip(*place);
        _tooltips.unset_tip(*flag_place);

        _mode[i] = SS_NA;
        _paintserver_id[i].clear();

        _popup_copy[i].set_sensitive(false);

        // query style from desktop. This returns a result flag and fills query with the style of subselection, if any, or selection
        int result = sp_desktop_query_style (_desktop, query, 
                                             (i == SS_FILL)? QUERY_STYLE_PROPERTY_FILL : QUERY_STYLE_PROPERTY_STROKE);

        switch (result) {
        case QUERY_STYLE_NOTHING:
            place->add(_na[i]);
            _tooltips.set_tip(*place, __na[i]);
            _mode[i] = SS_NA;
            break;
        case QUERY_STYLE_SINGLE:
        case QUERY_STYLE_MULTIPLE_AVERAGED:
        case QUERY_STYLE_MULTIPLE_SAME: 
            SPIPaint *paint;
            if (i == SS_FILL) {
                paint = &(query->fill);
            } else {
                paint = &(query->stroke);
            }
            if (paint->set && paint->type == SP_PAINT_TYPE_COLOR) {
                guint32 color = sp_color_get_rgba32_falpha (&(paint->value.color), 
                                     SP_SCALE24_TO_FLOAT ((i == SS_FILL)? query->fill_opacity.value : query->stroke_opacity.value));
                _lastselected[i] = _thisselected[i];
                _thisselected[i] = color | 0xff; // only color, opacity === 1
                ((Inkscape::UI::Widget::ColorPreview*)_color_preview[i])->setRgba32 (color);
                _color_preview[i]->show_all();
                place->add(*_color_preview[i]);
                gchar c_string[64];
                g_snprintf (c_string, 64, "%06x/%.3g", color >> 8, SP_RGBA32_A_F(color));
                _tooltips.set_tip(*place, __color[i] + ": " + c_string);
                _mode[i] = SS_COLOR;
                _popup_copy[i].set_sensitive(true);

            } else if (paint->set && paint->type == SP_PAINT_TYPE_PAINTSERVER) {
                SPPaintServer *server = (i == SS_FILL)? SP_STYLE_FILL_SERVER (query) : SP_STYLE_STROKE_SERVER (query);

                Inkscape::XML::Node *srepr = SP_OBJECT_REPR(server);
                _paintserver_id[i] += "url(#";
                _paintserver_id[i] += srepr->attribute("id");
                _paintserver_id[i] += ")";

                if (SP_IS_LINEARGRADIENT (server)) {
                    place->add(_lgradient[i]);
                    _tooltips.set_tip(*place, __lgradient[i]);
                    _mode[i] = SS_LGRADIENT;
                } else if (SP_IS_RADIALGRADIENT (server)) {
                    place->add(_rgradient[i]);
                    _tooltips.set_tip(*place, __rgradient[i]);
                    _mode[i] = SS_RGRADIENT;
                } else if (SP_IS_PATTERN (server)) {
                    place->add(_pattern[i]);
                    _tooltips.set_tip(*place, __pattern[i]);
                    _mode[i] = SS_PATTERN;
                }

            } else if (paint->set && paint->type == SP_PAINT_TYPE_NONE) {
                place->add(_none[i]);
                _tooltips.set_tip(*place, __none[i]);
                _mode[i] = SS_NONE;
            } else if (!paint->set) {
                place->add(_unset[i]);
                _tooltips.set_tip(*place, __unset[i]);
                _mode[i] = SS_UNSET;
            }
            if (result == QUERY_STYLE_MULTIPLE_AVERAGED) {
                flag_place->add(_averaged[i]);
                _tooltips.set_tip(*flag_place, __averaged[i]);
            } else if (result == QUERY_STYLE_MULTIPLE_SAME) {
                flag_place->add(_multiple[i]);
                _tooltips.set_tip(*flag_place, __multiple[i]);
            }
            break;
        case QUERY_STYLE_MULTIPLE_DIFFERENT:
            place->add(_many[i]);
            _tooltips.set_tip(*place, __many[i]);
            _mode[i] = SS_MANY;
            break;
        default:
            break;
        }
    }

// Now query opacity
    _tooltips.unset_tip(_opacity_place);

    int result = sp_desktop_query_style (_desktop, query, QUERY_STYLE_PROPERTY_MASTEROPACITY);

    switch (result) {
    case QUERY_STYLE_NOTHING:
        _tooltips.set_tip(_opacity_place, _("Nothing selected"));
        _opacity_sb.set_sensitive(false);
        break;
    case QUERY_STYLE_SINGLE:
    case QUERY_STYLE_MULTIPLE_AVERAGED:
    case QUERY_STYLE_MULTIPLE_SAME:
        _tooltips.set_tip(_opacity_place, _("Master opacity"));
        _opacity_blocked = true;
        _opacity_sb.set_sensitive(true);
        _opacity_adjustment.set_value(SP_SCALE24_TO_FLOAT(query->opacity.value));
        _opacity_blocked = false;
        break;
    }

    g_free (query);
}

void SelectedStyle::opacity_0(void) {_opacity_sb.set_value(0);}
void SelectedStyle::opacity_025(void) {_opacity_sb.set_value(0.25);}
void SelectedStyle::opacity_05(void) {_opacity_sb.set_value(0.5);}
void SelectedStyle::opacity_075(void) {_opacity_sb.set_value(0.75);}
void SelectedStyle::opacity_1(void) {_opacity_sb.set_value(1.0);}

void SelectedStyle::on_opacity_menu (Gtk::Menu *menu) {

    Glib::ListHandle<Gtk::Widget *> children = menu->get_children();
    for (Glib::ListHandle<Gtk::Widget *>::iterator iter = children.begin(); iter != children.end(); iter++) {
        menu->remove(*(*iter));
    }

    {
        Gtk::MenuItem *item = new Gtk::MenuItem;
        item->add(*(new Gtk::Label(_("0 (transparent)"), 0, 0)));
        item->signal_activate().connect(sigc::mem_fun(*this, &SelectedStyle::opacity_0 ));
        menu->add(*item);
    }
    {
        Gtk::MenuItem *item = new Gtk::MenuItem;
        item->add(*(new Gtk::Label("0.25", 0, 0)));
        item->signal_activate().connect(sigc::mem_fun(*this, &SelectedStyle::opacity_025 ));
        menu->add(*item);
    }
    {
        Gtk::MenuItem *item = new Gtk::MenuItem;
        item->add(*(new Gtk::Label("0.5", 0, 0)));
        item->signal_activate().connect(sigc::mem_fun(*this, &SelectedStyle::opacity_05 ));
        menu->add(*item);
    }
    {
        Gtk::MenuItem *item = new Gtk::MenuItem;
        item->add(*(new Gtk::Label("0.75", 0, 0)));
        item->signal_activate().connect(sigc::mem_fun(*this, &SelectedStyle::opacity_075 ));
        menu->add(*item);
    }
    {
        Gtk::MenuItem *item = new Gtk::MenuItem;
        item->add(*(new Gtk::Label(_("1.0 (opaque)"), 0, 0)));
        item->signal_activate().connect(sigc::mem_fun(*this, &SelectedStyle::opacity_1 ));
        menu->add(*item);
    }

    menu->show_all();
}

void SelectedStyle::on_opacity_changed () {
    if (_opacity_blocked)
        return;
    _opacity_blocked = true;
    SPCSSAttr *css = sp_repr_css_attr_new ();
    Inkscape::CSSOStringStream os;
    os << CLAMP (_opacity_adjustment.get_value(), 0.0, 1.0);
    sp_repr_css_set_property (css, "opacity", os.str().c_str());
    sp_desktop_set_style (_desktop, css);
    sp_repr_css_attr_unref (css);
    sp_document_maybe_done (SP_DT_DOCUMENT (_desktop), "fillstroke:opacity");
    spinbutton_defocus(GTK_OBJECT(_opacity_sb.gobj()));
    _opacity_blocked = false;
}

} // namespace Widget
} // namespace UI
} // namespace Inkscape

/* 
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=c++:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
