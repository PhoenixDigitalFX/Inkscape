// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Toolbar for Tools.
 */
/* Authors:
 *   Mike Kowalski (Popovers)
 *   Matrin Owens (Tool button catagories)
 *   Jonathon Neuhauser (Open tool preferences)
 *   Tavmjong Bah
 *
 * Copyright (C) 2023 Tavmjong Bah
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <iostream>
#include <utility>
#include <glibmm/i18n.h>
#include <giomm/menu.h>
#include <giomm/simpleactiongroup.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/popover.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>

#include "tool-toolbar.h"
#include "actions/actions-tools.h" // Function to open tool preferences.
#include "inkscape-window.h"
#include "ui/builder-utils.h"
#include "ui/widget/popover-menu.h"
#include "ui/widget/popover-menu-item.h"
#include "widgets/spw-utilities.h" // Find action target

namespace Inkscape::UI::Toolbar {

ToolToolbar::ToolToolbar(InkscapeWindow *window)
    : Gtk::Box()
    , _context_menu{makeContextMenu(window)}
{
    set_name("ToolToolbar");
    set_homogeneous(false);

    Gtk::ScrolledWindow* tool_toolbar = nullptr;

    auto builder = Inkscape::UI::create_builder("toolbar-tool.ui");
    builder->get_widget("tool-toolbar", tool_toolbar);
    if (!tool_toolbar) {
        std::cerr << "ToolToolbar: Failed to load tool toolbar!" << std::endl;
        return;
    }

    attachHandlers(builder, window);

    pack_start(*tool_toolbar, true, true);

    // Hide/show buttons based on preferences.
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    buttons_pref_observer = prefs->createObserver(tools_button_path, [=]() { set_visible_buttons(); });
    set_visible_buttons(); // Must come after pack_start()
}

ToolToolbar::~ToolToolbar() = default;

void ToolToolbar::set_visible_buttons()
{
    int buttons_before_separator = 0;
    Gtk::Widget* last_sep = nullptr;
    Gtk::FlowBox* last_box = nullptr;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    sp_traverse_widget_tree(this, [&](Gtk::Widget* widget) {
        if (auto flowbox = dynamic_cast<Gtk::FlowBox*>(widget)) {
            flowbox->set_visible(true);
            flowbox->set_no_show_all();
            flowbox->set_max_children_per_line(1);
            last_box = flowbox;
        }
        else if (auto btn = dynamic_cast<Gtk::Button*>(widget)) {
            auto name = sp_get_action_target(widget);
            auto show = prefs->getBool(get_tool_visible_button_path(name), true);
            auto parent = btn->get_parent();
            if (show) {
                parent->set_visible(true);
                ++buttons_before_separator;
                // keep the max_children up to date improves display.
                last_box->set_max_children_per_line(buttons_before_separator);
                last_sep = nullptr;
            }
            else {
                parent->set_visible(false);
            }
        }
        else if (auto sep = dynamic_cast<Gtk::Separator*>(widget)) {
            if (buttons_before_separator <= 0) {
                sep->set_visible(false);
            }
            else {
                sep->set_visible(true);
                buttons_before_separator = 0;
                last_sep = sep;
            }
        }
        return false;
    });
    if (last_sep) {
        // hide trailing separator
        last_sep->set_visible(false);
    }
}

// We should avoid passing in the window in Gtk4 by turning "tool_preferences()" into an action.
std::unique_ptr<UI::Widget::PopoverMenu> ToolToolbar::makeContextMenu(InkscapeWindow * const window)
{
    Glib::ustring icon_name;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs->getInt("/theme/menuIcons", true)) {
        icon_name = "preferences-system";
    }

    auto &item = *Gtk::make_managed<UI::Widget::PopoverMenuItem>(_("Open tool preferences"),
                                                                 icon_name);
    item.signal_activate().connect([=, this]
    {
        tool_preferences(_context_menu_tool_name, window);
        _context_menu_tool_name.clear();
    });

    auto menu = std::make_unique<UI::Widget::PopoverMenu>();
    menu->append(item);
    return menu;
}

void ToolToolbar::showContextMenu(InkscapeWindow * const window,
                                  Gtk::Button &button, Glib::ustring const &tool_name)
{
    _context_menu_tool_name = tool_name;
    // Point to the Image inside Button, not the entire Button including padding
    _context_menu->popup_at(button,
                            button.get_width () *  1./4,
                            button.get_height() * -1./2);
}

/**
 * @brief Attach handlers to all tool buttons, so that double-clicking on a tool
 *        in the toolbar opens up that tool's preferences, and a right click opens a
 *        context menu with the same functionality.
 * @param builder The builder that contains a loaded UI structure containing RadioButton's.
 * @param window The Inkscape window which will display the preferences dialog.
 */
void ToolToolbar::attachHandlers(Glib::RefPtr<Gtk::Builder> builder, InkscapeWindow *window)
{
    for (auto &object : builder->get_objects()) {
        auto const radio = dynamic_cast<Gtk::RadioButton *>(object.get());
        if (!radio) {
            continue;
        }

        Glib::VariantBase action_target;
        radio->get_property("action-target", action_target);
        if (!action_target.is_of_type(Glib::VARIANT_TYPE_STRING)) {
            continue;
        }

        auto tool_name = Glib::ustring((gchar const *)action_target.get_data());
        auto on_click_pressed = [=, tool_name = std::move(tool_name)]
                                (GdkEventButton const * const ev)
        {
            // Open tool preferences upon double click
            if (ev->type == GDK_2BUTTON_PRESS && ev->button == 1) {
                tool_preferences(tool_name, window);
                return true;
            }
            if (ev->button == 3) {
                showContextMenu(window, *radio, tool_name);
                return true;
            }
            return false;
        };
        radio->signal_button_press_event().connect(std::move(on_click_pressed));
    }
}

Glib::ustring ToolToolbar::get_tool_visible_button_path(const Glib::ustring& button_action_name) {
    return Glib::ustring(tools_button_path) + "/show" + button_action_name;
}

} // namespace Inkscape::UI::Toolbar

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
