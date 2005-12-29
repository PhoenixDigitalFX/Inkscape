/** \file
 * \brief 
 *
 * Authors:
 *   Ralf Stephan <ralf@ark.in-berlin.de>
 *
 * Copyright (C) 2005 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_REGISTERED_WIDGET__H_
#define INKSCAPE_UI_WIDGET_REGISTERED_WIDGET__H_

#include <gtkmm/box.h>
#include <gtkmm/adjustment.h>

class SPUnit;

namespace Gtk {
    class HScale;
    class RadioButton;
}

namespace Inkscape {
namespace UI {
namespace Widget {

class ColorPicker;
class Registry;
class ScalarUnit;
class UnitMenu;

class RegisteredCheckButton {
public:
    RegisteredCheckButton();
    ~RegisteredCheckButton();
    void init (const Glib::ustring& label, const Glib::ustring& tip, const Glib::ustring& key, Registry& wr, bool right=true);
    void setActive (bool);

    Gtk::ToggleButton *_button;

protected:
    Gtk::Tooltips     _tt;
    sigc::connection  _toggled_connection;
    Registry    *_wr;
    Glib::ustring      _key;
    void on_toggled();
};

class RegisteredUnitMenu {
public:
    RegisteredUnitMenu();
    ~RegisteredUnitMenu();
    void init (const Glib::ustring& label, const Glib::ustring& key, Registry& wr);
    void setUnit (const SPUnit*);
    Gtk::Label   *_label;
    UnitMenu     *_sel;
    sigc::connection _changed_connection;

protected:
    void on_changed();
    Registry     *_wr;
    Glib::ustring _key;
};

class RegisteredScalarUnit {
public:
    RegisteredScalarUnit();
    ~RegisteredScalarUnit();
    void init (const Glib::ustring& label, 
            const Glib::ustring& tip, 
            const Glib::ustring& key, 
            const RegisteredUnitMenu &rum,
            Registry& wr);
    ScalarUnit* getSU();
    void setValue (double);

protected:
    ScalarUnit   *_widget;
    sigc::connection  _value_changed_connection;
    UnitMenu         *_um;
    Registry         *_wr;
    Glib::ustring    _key;
    void on_value_changed();
};

class RegisteredSlider {
public:
    RegisteredSlider();
    ~RegisteredSlider();
    void init (const Glib::ustring& label1, 
            const Glib::ustring& label2, 
            const Glib::ustring& tip, 
            const Glib::ustring& key, 
            Registry& wr);
    void setValue (double, const SPUnit*);
    void setLimits (double, double);
    Gtk::HBox* _hbox;

protected:
    void on_scale_changed();
    void update();
    sigc::connection  _scale_changed_connection;
    Gtk::HScale      *_hscale;
    Registry         *_wr;
    Glib::ustring     _key;
};

class RegisteredColorPicker {
public:
    RegisteredColorPicker();
    ~RegisteredColorPicker();
    void init (const Glib::ustring& label, 
            const Glib::ustring& title, 
            const Glib::ustring& tip, 
            const Glib::ustring& ckey, 
            const Glib::ustring& akey,
            Registry& wr);
    void setRgba32 (guint32);
    void closeWindow();

    Gtk::Label *_label;
    ColorPicker *_cp;

protected:
    Glib::ustring _ckey, _akey;
    Registry      *_wr;
    void on_changed (guint32);
    sigc::connection _changed_connection;
};

class RegisteredSuffixedInteger {
public:
    RegisteredSuffixedInteger();
    ~RegisteredSuffixedInteger();
    void init (const Glib::ustring& label1, 
               const Glib::ustring& label2, 
               const Glib::ustring& key,
               Registry& wr);
    void setValue (int);
    Gtk::Label *_label;
    Gtk::HBox _hbox;

protected:
    Gtk::SpinButton *_sb;
    Gtk::Adjustment _adj;
    Gtk::Label      *_suffix;
    Glib::ustring   _key;
    Registry        *_wr;
    sigc::connection _changed_connection;
    void on_value_changed();
};

class RegisteredRadioButtonPair {
public:
    RegisteredRadioButtonPair();
    ~RegisteredRadioButtonPair();
    void init (const Glib::ustring& label, 
               const Glib::ustring& label1, 
               const Glib::ustring& label2, 
               const Glib::ustring& key,
               const char* val1,
               const char* val2,
               Registry& wr);
    void setValue (bool first);
    Gtk::HBox *_hbox;

protected:
    Gtk::RadioButton *_rb1, *_rb2;
    Glib::ustring   _key, _val1, _val2;
    Registry        *_wr;
    sigc::connection _changed_connection;
    void on_value_changed();
};



} // namespace Widget
} // namespace UI
} // namespace Inkscape

#endif // INKSCAPE_UI_WIDGET_REGISTERED_WIDGET__H_

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
