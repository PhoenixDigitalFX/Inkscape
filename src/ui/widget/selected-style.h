// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 *   buliabyak@gmail.com
 *   scislac@users.sf.net
 *
 * Copyright (C) 2005 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_CURRENT_STYLE_H
#define INKSCAPE_UI_CURRENT_STYLE_H

#include <memory>
#include <vector>
#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/enums.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/menu.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/radiobuttongroup.h>
#include "helper/auto-connection.h"
#include "rotateable.h"
#include "ui/widget/spinbutton.h"

namespace Gtk {
class Adjustment;
class RadioMenuItem;
} // namespace Gtk

class SPDesktop;

namespace Inkscape {

namespace Util {
class Unit;
} // namespace Util

namespace UI::Widget {

enum {
    SS_NA,
    SS_NONE,
    SS_UNSET,
    SS_PATTERN,
    SS_LGRADIENT,
    SS_RGRADIENT,
#ifdef WITH_MESH
    SS_MGRADIENT,
#endif
    SS_MANY,
    SS_COLOR,
    SS_HATCH
};

enum {
    SS_FILL,
    SS_STROKE
};

class ColorPreview;
class GradientImage;
class SelectedStyle;
class SelectedStyleDropTracker;

class RotateableSwatch : public Rotateable {
  public:
    RotateableSwatch(SelectedStyle *parent, guint mode);
    ~RotateableSwatch() override;

    double color_adjust (float *hsl, double by, guint32 cc, guint state);

    void do_motion (double by, guint state) override;
    void do_release (double by, guint state) override;
    void do_scroll (double by, guint state) override;

private:
    guint fillstroke;

    SelectedStyle *parent;

    guint32 startcolor = 0;
    bool startcolor_set = false;

    gchar const *undokey = "ssrot1";

    bool cr_set = false;
};

class RotateableStrokeWidth : public Rotateable {
  public:
    RotateableStrokeWidth(SelectedStyle *parent);
    ~RotateableStrokeWidth() override;

    double value_adjust(double current, double by, guint modifier, bool final);
    void do_motion (double by, guint state) override;
    void do_release (double by, guint state) override;
    void do_scroll (double by, guint state) override;

private:
    SelectedStyle *parent;

    double startvalue;
    bool startvalue_set;

    gchar const *undokey;
};

/**
 * Selected style indicator (fill, stroke, opacity).
 */
class SelectedStyle : public Gtk::Box
{
public:
    SelectedStyle(bool layout = true);

    ~SelectedStyle() override;

    void setDesktop(SPDesktop *desktop);
    SPDesktop *getDesktop() {return _desktop;}
    void update();

    guint32 _lastselected[2];
    guint32 _thisselected[2];

    guint _mode[2];

    double current_stroke_width;
    Inkscape::Util::Unit const *_sw_unit; // points to object in UnitTable, do not delete

protected:
    SPDesktop *_desktop;

    Gtk::Grid _table;

    Gtk::Label _fill_label;
    Gtk::Label _stroke_label;
    Gtk::Label _opacity_label;

    RotateableSwatch _fill_place;
    RotateableSwatch _stroke_place;

    Gtk::EventBox _fill_flag_place;
    Gtk::EventBox _stroke_flag_place;

    Gtk::EventBox _opacity_place;
    Glib::RefPtr<Gtk::Adjustment> _opacity_adjustment;
    Inkscape::UI::Widget::SpinButton _opacity_sb;

    Gtk::Label _na[2];
    Glib::ustring __na[2];

    Gtk::Label _none[2];
    Glib::ustring __none[2];

    Gtk::Label _pattern[2];
    Glib::ustring __pattern[2];

    Gtk::Label _hatch[2];
    Glib::ustring __hatch[2];

    Gtk::Label _lgradient[2];
    Glib::ustring __lgradient[2];

    GradientImage *_gradient_preview_l[2];
    Gtk::Box _gradient_box_l[2];

    Gtk::Label _rgradient[2];
    Glib::ustring __rgradient[2];

    GradientImage *_gradient_preview_r[2];
    Gtk::Box _gradient_box_r[2];

#ifdef WITH_MESH
    Gtk::Label _mgradient[2];
    Glib::ustring __mgradient[2];

    GradientImage *_gradient_preview_m[2];
    Gtk::Box _gradient_box_m[2];
#endif

    Gtk::Label _many[2];
    Glib::ustring __many[2];

    Gtk::Label _unset[2];
    Glib::ustring __unset[2];

    std::unique_ptr<ColorPreview> _color_preview[2];
    Glib::ustring __color[2];

    Gtk::Label _averaged[2];
    Glib::ustring __averaged[2];
    Gtk::Label _multiple[2];
    Glib::ustring __multiple[2];

    Gtk::Box _fill;
    Gtk::Box _stroke;
    RotateableStrokeWidth _stroke_width_place;
    Gtk::Label _stroke_width;
    Gtk::Label _fill_empty_space;

    Glib::ustring _paintserver_id[2];

    auto_connection selection_changed_connection;
    auto_connection selection_modified_connection;
    auto_connection subselection_changed_connection;

    static void dragDataReceived( GtkWidget *widget,
                                  GdkDragContext *drag_context,
                                  gint x, gint y,
                                  GtkSelectionData *data,
                                  guint info,
                                  guint event_time,
                                  gpointer user_data );

    bool on_fill_click(GdkEventButton *event);
    bool on_stroke_click(GdkEventButton *event);
    bool on_opacity_click(GdkEventButton *event);
    bool on_sw_click(GdkEventButton *event);

    bool _opacity_blocked;
    void on_opacity_changed();
    void on_opacity_menu(Gtk::Menu *menu);
    void opacity_0();
    void opacity_025();
    void opacity_05();
    void opacity_075();
    void opacity_1();

    void on_fill_remove();
    void on_stroke_remove();
    void on_fill_lastused();
    void on_stroke_lastused();
    void on_fill_lastselected();
    void on_stroke_lastselected();
    void on_fill_unset();
    void on_stroke_unset();
    void on_fill_edit();
    void on_stroke_edit();
    void on_fillstroke_swap();
    void on_fill_invert();
    void on_stroke_invert();
    void on_fill_white();
    void on_stroke_white();
    void on_fill_black();
    void on_stroke_black();
    void on_fill_copy();
    void on_stroke_copy();
    void on_fill_paste();
    void on_stroke_paste();
    void on_fill_opaque();
    void on_stroke_opaque();

    Gtk::Menu _popup[2];
    Gtk::MenuItem _popup_edit[2];
    Gtk::MenuItem _popup_lastused[2];
    Gtk::MenuItem _popup_lastselected[2];
    Gtk::MenuItem _popup_invert[2];
    Gtk::MenuItem _popup_white[2];
    Gtk::MenuItem _popup_black[2];
    Gtk::MenuItem _popup_copy[2];
    Gtk::MenuItem _popup_paste[2];
    Gtk::MenuItem _popup_swap[2];
    Gtk::MenuItem _popup_opaque[2];
    Gtk::MenuItem _popup_unset[2];
    Gtk::MenuItem _popup_remove[2];

    Gtk::Menu _popup_sw;
    Gtk::RadioButtonGroup _sw_group;
    std::vector<Gtk::RadioMenuItem*> _unit_mis;
    void on_popup_units(Inkscape::Util::Unit const *u);
    void on_popup_preset(int i);
    Gtk::MenuItem _popup_sw_remove;

    std::unique_ptr<SelectedStyleDropTracker> _drop[2];
    bool _dropEnabled[2];
};


} // namespace UI::Widget

} // namespace Inkscape

#endif // INKSCAPE_UI_WIDGET_BUTTON_H

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
