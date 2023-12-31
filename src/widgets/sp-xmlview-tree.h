// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * Authors:
 *   MenTaLguY <mental@rydia.net>
 *
 * Copyright (C) 2002 MenTaLguY
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_SP_XMLVIEW_TREE_H
#define SEEN_SP_XMLVIEW_TREE_H

#include <gtk/gtk.h>
#include <glib.h>
#include <gtkmm/cellrenderertext.h>

namespace Inkscape::XML {
class Node;
}

/**
 * Specialization of GtkTreeView for the XML editor
 */

#define SP_TYPE_XMLVIEW_TREE (sp_xmlview_tree_get_type ())
#define SP_XMLVIEW_TREE(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SP_TYPE_XMLVIEW_TREE, SPXMLViewTree))
#define SP_IS_XMLVIEW_TREE(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SP_TYPE_XMLVIEW_TREE))
#define SP_XMLVIEW_TREE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), SP_TYPE_XMLVIEW_TREE))

struct SPXMLViewTree;
struct SPXMLViewTreeClass;
namespace Inkscape::UI::Syntax { class XMLFormatter; }

class SPXMLViewTree
{
public:
    GtkTreeView tree;
    GtkTreeStore *store;
    Inkscape::XML::Node * repr;
    gint blocked;
    Gtk::CellRendererText* renderer;
    Inkscape::UI::Syntax::XMLFormatter* formatter;

    sigc::connection connectTreeMove(const sigc::slot<void ()> &slot)
    {
        return _tree_move->connect(slot);
    }
// private: Make private and not-pointer when refactoring to C++
    sigc::signal<void ()> *_tree_move;
};

struct SPXMLViewTreeClass
{
	GtkTreeViewClass parent_class;
};

GType sp_xmlview_tree_get_type ();
GtkWidget * sp_xmlview_tree_new (Inkscape::XML::Node * repr, void * factory, void * data);

#define SP_XMLVIEW_TREE_REPR(tree) (SP_XMLVIEW_TREE (tree)->repr)

void sp_xmlview_tree_set_repr (SPXMLViewTree * tree, Inkscape::XML::Node * repr);

Inkscape::XML::Node * sp_xmlview_tree_node_get_repr (GtkTreeModel *model, GtkTreeIter * node);
gboolean sp_xmlview_tree_get_repr_node (SPXMLViewTree * tree, Inkscape::XML::Node * repr, GtkTreeIter *node);


#endif // !SEEN_SP_XMLVIEW_TREE_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8 :
