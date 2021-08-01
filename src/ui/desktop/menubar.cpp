// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Desktop main menu bar code.
 */
/*
 * Authors:
 *   Tavmjong Bah       <tavmjong@free.fr>
 *   Alex Valavanis     <valavanisalex@gmail.com>
 *   Patrick Storz      <eduard.braun2@gmx.de>
 *   Krzysztof Kosiński <tweenk.pl@gmail.com>
 *   Kris De Gussem     <Kris.DeGussem@gmail.com>
 *
 * Copyright (C) 2018 Authors
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 * Read the file 'COPYING' for more information.
 *
 */

#include <gtkmm.h>
#include <glibmm/i18n.h>

#include <iostream>

#include "inkscape.h"
#include "inkscape-application.h" // Open recent

#include "message-context.h"

#include "menu-icon-shift.h"

#include "helper/action.h"
#include "helper/action-context.h"

#include "io/resource.h"    // UI File location

#include "object/sp-namedview.h"

#include "ui/icon-loader.h"
#include "inkscape-window.h"
#include "ui/shortcuts.h"
#include "ui/view/view.h"
#include "ui/uxmanager.h"   // To Do: Convert to actions

#ifdef GDK_WINDOWING_QUARTZ
#include <gtkosxapplication.h>
#endif

const std::vector< std::pair<std::string, std::string> > menu_data = {
    { "File"  ,        "file-menu"   },
    { "Edit"  ,        "edit-menu"   },
    { "View"  ,        "view-menu"   },
    { "Layer" ,        "layer-menu"  },
    { "Object",        "object-menu" },
    { "Path"  ,        "path-menu"   },
    { "Text"  ,        "text-menu"   },
    { "Filters",       "filter-menu" },
    { "Extensions",    "effect-menu" },
    { "Help"  ,        "help-menu"   }
};

// =================== Main Menu ================
void
build_menu(Gtk::MenuShell* menu, Inkscape::XML::Node* xml, Inkscape::UI::View::View* view, bool show_icons = true)
{
    if (menu == nullptr) {
        std::cerr << "build_menu: menu is nullptr" << std::endl;
        return;
    }

    if (xml == nullptr) {
        std::cerr << "build_menu: xml is nullptr" << std::endl;
        return;
    }

    for (auto [menu_title, menu_name] : menu_data)
    {
        Gtk::MenuItem* menuitem = Gtk::manage(new Gtk::MenuItem(_(menu_title.c_str()), true));
        menuitem->set_name(menu_name);

        std::string filename = Inkscape::IO::Resource::get_filename(Inkscape::IO::Resource::UIS, "menus.ui");
        auto refBuilder = Gtk::Builder::create();

        try
        {
            refBuilder->add_from_file(filename);
        }
        catch (const Glib::Error& err)
        {
            std::cerr << "build_menu: failed to load View menu from: "
                        << filename <<": "
                        << err.what() << std::endl;
        }
        auto object = refBuilder->get_object(menu_name);
        auto gmenu = Glib::RefPtr<Gio::Menu>::cast_dynamic(object);
        if (!gmenu) {
            std::cerr << "build_menu: failed to build View menu!" << std::endl;
        } else {

            auto submenu = Gtk::manage(new Gtk::Menu(gmenu));
            menuitem->set_submenu(*submenu);
            menu->append(*menuitem);

            if (menu_name == "filter-menu") {

                static auto app = InkscapeApplication::instance();
                
                for (auto [ filter_id, submenu_name ] : app->get_action_effect_data().give_all_data()) {
                    auto [ filter_submenu,filter_name ] = submenu_name;
                    if ( app->get_action_effect_data().is_filter(filter_submenu) ) {
                        auto sub_object = refBuilder->get_object(filter_submenu);
                        auto sub_gmenu = Glib::RefPtr<Gio::Menu>::cast_dynamic(sub_object);
                        sub_gmenu->append( filter_name, "app."+filter_id );        
                    }
                }

            } // Filters end

            if (menu_name == "effect-menu") {

                static auto app = InkscapeApplication::instance();
                
                for (auto [ extension_id, submenu_name ] : app->get_action_effect_data().give_all_data()) {
                    auto [ extension_submenu,extension_name ] = submenu_name;
                    if ( app->get_action_effect_data().is_extensions(extension_submenu) ) {
                        auto sub_object = refBuilder->get_object(extension_submenu);
                        auto sub_gmenu = Glib::RefPtr<Gio::Menu>::cast_dynamic(sub_object);
                        sub_gmenu->append( extension_name,"app."+extension_id );        
                    }
                }

            } // Extensions end

            if(menu_name== "file-menu") {
                auto recent_manager = Gtk::RecentManager::get_default();
                auto recent_files = recent_manager->get_items(); // all recent files not necessarily inkscape only

                int max_files = Inkscape::Preferences::get()->getInt("/options/maxrecentdocuments/value");

                auto sub_object = refBuilder->get_object("recent-files");
                auto sub_gmenu = Glib::RefPtr<Gio::Menu>::cast_dynamic(sub_object);

                for (auto const &recent_file : recent_files) {
                    // check if given was generated by inkscape
                    bool valid_file = recent_file->has_application(g_get_prgname()) or
                                    recent_file->has_application("org.inkscape.Inkscape") or
                                    recent_file->has_application("inkscape") or
                                    recent_file->has_application("inkscape.exe");

                    valid_file = valid_file and recent_file->exists();

                    if (not valid_file) {
                        continue;
                    }

                    if (max_files-- <= 0) {
                        break;
                    }
                    
                    std::string action_name = "app.file-open-window('"+recent_file->get_uri_display()+"')";
                    sub_gmenu->append(recent_file->get_short_name(),action_name);
                
                }
            } // Recent file end
        }
    }
}

void
reload_menu(Inkscape::UI::View::View* view, Gtk::MenuBar* menubar)
{   
    menubar->hide();
    for (auto *widg : menubar->get_children()) {
        menubar->remove(*widg);
    }
    build_menu(menubar, INKSCAPE.get_menus()->parent(), view);

    shift_icons_recursive(menubar); // Find all submenus and add callback to each one.

    menubar->show_all();
#ifdef GDK_WINDOWING_QUARTZ
    sync_menubar();
    menubar->hide();
#endif
}

Gtk::MenuBar*
build_menubar(Inkscape::UI::View::View* view)
{
    Gtk::MenuBar* menubar = Gtk::manage(new Gtk::MenuBar());
    build_menu(menubar, INKSCAPE.get_menus()->parent(), view);

    shift_icons_recursive(menubar); // Find all submenus and add callback to each one.

    return menubar;
}


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