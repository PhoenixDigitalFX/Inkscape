#define __SP_NODE_CONTEXT_C__

/*
 * Node editing context
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *
 * This code is in public domain, except stamping code,
 * which is Copyright (C) Masatake Yamato 2002
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <gdk/gdkkeysyms.h>
#include <gtk/gtksignal.h>
#include <string.h>
#include "macros.h"
#include "xml/repr.h"
#include "svg/svg.h"
#include <glibmm/i18n.h>
#include "display/sp-canvas-util.h"
#include "object-edit.h"
#include "sp-path.h"
#include "path-chemistry.h"
#include "rubberband.h"
#include "desktop.h"
#include "desktop-affine.h"
#include "desktop-handles.h"
#include "selection.h"
#include "nodepath.h"
#include "knotholder.h"
#include "pixmaps/cursor-node.xpm"
#include "node-context.h"
#include "sp-cursor.h"
#include "pixmaps/cursor-node-m.xpm"
#include "pixmaps/cursor-node-d.xpm"
#include "document.h"
#include "prefs-utils.h"
#include "xml/repr.h"
#include "xml/node-event-vector.h"

static void sp_node_context_class_init(SPNodeContextClass *klass);
static void sp_node_context_init(SPNodeContext *node_context);
static void sp_node_context_dispose(GObject *object);

static void sp_node_context_setup(SPEventContext *ec);
static gint sp_node_context_root_handler(SPEventContext *event_context, GdkEvent *event);
static gint sp_node_context_item_handler(SPEventContext *event_context,
                                         SPItem *item, GdkEvent *event);

static void nodepath_event_attr_changed(Inkscape::XML::Node *repr, gchar const *name,
                                        gchar const *old_value, gchar const *new_value,
                                        bool is_interactive, gpointer data);

static Inkscape::XML::NodeEventVector nodepath_repr_events = {
    NULL, /* child_added */
    NULL, /* child_removed */
    nodepath_event_attr_changed,
    NULL, /* content_changed */
    NULL  /* order_changed */
};

static SPEventContextClass *parent_class;
static GdkCursor *CursorNodeMouseover = NULL, *CursorNodeDragging = NULL;

/// If non-zero, rubberband was cancelled by esc, so the next button release should not deselect.
static gint nodeedit_rb_escaped = 0;

static gint xp = 0, yp = 0; ///< Where drag started.
static gint tolerance = 0;
static bool within_tolerance = false;

GType
sp_node_context_get_type()
{
    static GType type = 0;
    if (!type) {
        GTypeInfo info = {
            sizeof(SPNodeContextClass),
            NULL, NULL,
            (GClassInitFunc) sp_node_context_class_init,
            NULL, NULL,
            sizeof(SPNodeContext),
            4,
            (GInstanceInitFunc) sp_node_context_init,
            NULL,    /* value_table */
        };
        type = g_type_register_static(SP_TYPE_EVENT_CONTEXT, "SPNodeContext", &info, (GTypeFlags)0);
    }
    return type;
}

static void
sp_node_context_class_init(SPNodeContextClass *klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;
    SPEventContextClass *event_context_class = (SPEventContextClass *) klass;

    parent_class = (SPEventContextClass*)g_type_class_peek_parent(klass);

    object_class->dispose = sp_node_context_dispose;

    event_context_class->setup = sp_node_context_setup;
    event_context_class->root_handler = sp_node_context_root_handler;
    event_context_class->item_handler = sp_node_context_item_handler;

    // cursors in node context
    CursorNodeMouseover = sp_cursor_new_from_xpm(cursor_node_m_xpm, 1, 1);
    CursorNodeDragging = sp_cursor_new_from_xpm(cursor_node_d_xpm, 1, 1);
}

static void
sp_node_context_init(SPNodeContext *node_context)
{
    SPEventContext *event_context = SP_EVENT_CONTEXT(node_context);

    event_context->cursor_shape = cursor_node_xpm;
    event_context->hot_x = 1;
    event_context->hot_y = 1;

    node_context->leftalt = FALSE;
    node_context->rightalt = FALSE;
    node_context->leftctrl = FALSE;
    node_context->rightctrl = FALSE;

    new (&node_context->sel_changed_connection) sigc::connection();
}

static void
sp_node_context_dispose(GObject *object)
{
    SPNodeContext *nc = SP_NODE_CONTEXT(object);
    SPEventContext *ec = SP_EVENT_CONTEXT(object);

    ec->enableGrDrag(false);

    nc->sel_changed_connection.disconnect();
    nc->sel_changed_connection.~connection();

    Inkscape::XML::Node *repr = NULL;
    if (nc->nodepath) {
        repr = nc->nodepath->repr;
    }
    if (!repr && ec->shape_knot_holder) {
        repr = ec->shape_knot_holder->repr;
    }

    if (repr) {
        sp_repr_remove_listener_by_data(repr, ec);
        sp_repr_unref(repr);
    }

    if (nc->nodepath) {
        sp_nodepath_destroy(nc->nodepath);
        nc->nodepath = NULL;
    }

    if (ec->shape_knot_holder) {
        sp_knot_holder_destroy(ec->shape_knot_holder);
        ec->shape_knot_holder = NULL;
    }

    if (nc->_node_message_context) {
        delete nc->_node_message_context;
    }

    if (CursorNodeDragging) {
        gdk_cursor_unref (CursorNodeDragging);
        CursorNodeDragging = NULL;
    }
    if (CursorNodeMouseover) {
        gdk_cursor_unref (CursorNodeMouseover);
        CursorNodeMouseover = NULL;
    }

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
sp_node_context_setup(SPEventContext *ec)
{
    SPNodeContext *nc = SP_NODE_CONTEXT(ec);

    if (((SPEventContextClass *) parent_class)->setup)
        ((SPEventContextClass *) parent_class)->setup(ec);

    nc->sel_changed_connection.disconnect();
    nc->sel_changed_connection = SP_DT_SELECTION(ec->desktop)->connectChanged(sigc::bind(sigc::ptr_fun(&sp_node_context_selection_changed), (gpointer)nc));

    Inkscape::Selection *selection = SP_DT_SELECTION(ec->desktop);
    SPItem *item = selection->singleItem();

    nc->nodepath = NULL;
    ec->shape_knot_holder = NULL;

    if (item) {
        nc->nodepath = sp_nodepath_new(ec->desktop, item);
        if ( nc->nodepath) {
            //point pack to parent in case nodepath is deleted
            nc->nodepath->nodeContext = nc;
        }
        ec->shape_knot_holder = sp_item_knot_holder(item, ec->desktop);

        if (nc->nodepath || ec->shape_knot_holder) {
            // setting listener
            Inkscape::XML::Node *repr;
            if (ec->shape_knot_holder)
                repr = ec->shape_knot_holder->repr;
            else
                repr = SP_OBJECT_REPR(item); 
            if (repr) {
                sp_repr_ref(repr);
                sp_repr_add_listener(repr, &nodepath_repr_events, ec);
                sp_repr_synthesize_events(repr, &nodepath_repr_events, ec);
            }
        }
    }

    if (prefs_get_int_attribute("tools.nodes", "selcue", 0) != 0) {
        ec->enableSelectionCue();
    }

    if (prefs_get_int_attribute("tools.nodes", "gradientdrag", 0) != 0) {
        ec->enableGrDrag();
    }

    nc->_node_message_context = new Inkscape::MessageContext(SP_VIEW(ec->desktop)->messageStack());
    sp_nodepath_update_statusbar(nc->nodepath);
}

/**
\brief  Callback that processes the "changed" signal on the selection;
destroys old and creates new nodepath and reassigns listeners to the new selected item's repr
*/
void
sp_node_context_selection_changed(Inkscape::Selection *selection, gpointer data)
{
    SPNodeContext *nc = SP_NODE_CONTEXT(data);
    SPEventContext *ec = SP_EVENT_CONTEXT(nc);

    Inkscape::XML::Node *old_repr = NULL;

    if (nc->nodepath) {
        old_repr = nc->nodepath->repr;
        sp_nodepath_destroy(nc->nodepath);
    }
    if (ec->shape_knot_holder) {
        old_repr = ec->shape_knot_holder->repr;
        sp_knot_holder_destroy(ec->shape_knot_holder);
    }

    if (old_repr) { // remove old listener
        sp_repr_remove_listener_by_data(old_repr, ec);
        sp_repr_unref(old_repr);
    }

    SPItem *item = selection->singleItem();

    SPDesktop *desktop = selection->desktop();
    nc->nodepath = NULL;
    ec->shape_knot_holder = NULL;
    if (item) {
        nc->nodepath = sp_nodepath_new(desktop, item);
        if (nc->nodepath) {
            nc->nodepath->nodeContext = nc;
        }
        ec->shape_knot_holder = sp_item_knot_holder(item, desktop);

        if (nc->nodepath || ec->shape_knot_holder) {
            // setting new listener
            Inkscape::XML::Node *repr;
            if (ec->shape_knot_holder)
                repr = ec->shape_knot_holder->repr;
            else
                repr = SP_OBJECT_REPR(item); 
            if (repr) {
                sp_repr_ref(repr);
                sp_repr_add_listener(repr, &nodepath_repr_events, ec);
                sp_repr_synthesize_events(repr, &nodepath_repr_events, ec);
            }
        }
    }
    sp_nodepath_update_statusbar(nc->nodepath);
}

/**
\brief  Regenerates nodepath when the item's repr was change outside of node edit
(e.g. by undo, or xml editor, or edited in another view). The item is assumed to be the same
(otherwise sp_node_context_selection_changed() would have been called), so repr and listeners
are not changed.
*/
void
sp_nodepath_update_from_item(SPNodeContext *nc, SPItem *item)
{
    g_assert(nc);
    SPEventContext *ec = ((SPEventContext *) nc);

    SPDesktop *desktop = SP_EVENT_CONTEXT_DESKTOP(SP_EVENT_CONTEXT(nc));
    g_assert(desktop);

    if (nc->nodepath) {
        sp_nodepath_destroy(nc->nodepath);
    }

    if (ec->shape_knot_holder) {
        sp_knot_holder_destroy(ec->shape_knot_holder);
    }

    Inkscape::Selection *selection = SP_DT_SELECTION(desktop);
    item = selection->singleItem();

    nc->nodepath = NULL;
    ec->shape_knot_holder = NULL;
    if (item) {
        nc->nodepath = sp_nodepath_new(desktop, item);
        ec->shape_knot_holder = sp_item_knot_holder(item, desktop);
    }
    sp_nodepath_update_statusbar(nc->nodepath);
}

/**
\brief  Callback that is fired whenever an attribute of the selected item (which we have in the nodepath) changes
*/
static void
nodepath_event_attr_changed(Inkscape::XML::Node *repr, gchar const *name,
                            gchar const *old_value, gchar const *new_value,
                            bool is_interactive, gpointer data)
{
    SPItem *item = NULL;
    char const *newd = NULL, *newtypestr = NULL;
    gboolean changed = FALSE;

    g_assert(data);
    SPNodeContext *nc = ((SPNodeContext *) data);
    SPEventContext *ec = ((SPEventContext *) data);
    g_assert(nc);
    Inkscape::NodePath::Path *np = nc->nodepath;
    SPKnotHolder *kh = ec->shape_knot_holder;

    if (np) {
        item = SP_ITEM(np->path);
        if (!strcmp(name, "d")) {
            newd = new_value;
            changed = nodepath_repr_d_changed(np, new_value);
        } else if (!strcmp(name, "sodipodi:nodetypes")) {
            newtypestr = new_value;
            changed = nodepath_repr_typestr_changed(np, new_value);
        } else {
            return;
            // With paths, we only need to act if one of the path-affecting attributes has changed.
        }
    } else if (kh) {
        item = SP_ITEM(kh->item);
        changed = !(kh->local_change);
        kh->local_change = FALSE;
    }
    if (np && changed) {
        GList *saved = NULL;
        SPDesktop *desktop = np->desktop;
        g_assert(desktop);
        Inkscape::Selection *selection = desktop->selection;
        g_assert(selection);

        saved = save_nodepath_selection(nc->nodepath);
        sp_nodepath_update_from_item(nc, item);
        if (nc->nodepath && saved) restore_nodepath_selection(nc->nodepath, saved);

    } else if (kh && changed) {
        sp_nodepath_update_from_item(nc, item);
    }

    sp_nodepath_update_statusbar(nc->nodepath);
}

void
sp_node_context_show_modifier_tip(SPEventContext *event_context, GdkEvent *event)
{
    sp_event_show_modifier_tip
        (event_context->defaultMessageContext(), event,
         _("<b>Ctrl</b>: toggle node type, snap handle angle, move hor/vert; <b>Ctrl+Alt</b>: move along handles"),
         _("<b>Shift</b>: toggle node selection, disable snapping, rotate both handles"),
         _("<b>Alt</b>: lock handle length; <b>Ctrl+Alt</b>: move along handles"));
}

static gint
sp_node_context_item_handler(SPEventContext *event_context, SPItem *item, GdkEvent *event)
{
    gint ret = FALSE;

    SPDesktop *desktop = event_context->desktop;
    Inkscape::Selection *selection = SP_DT_SELECTION (desktop);

    SPNodeContext *nc = SP_NODE_CONTEXT(event_context);

    switch (event->type) {
        case GDK_BUTTON_RELEASE:
            if (event->button.button == 1) {
                if (!nc->drag) {
                    // find out clicked item, disregarding groups, honoring Alt
                    SPItem *item_ungrouped = sp_event_context_find_item (desktop, NR::Point(event->button.x, event->button.y), event->button.state, TRUE);

                    if (event->button.state & GDK_SHIFT_MASK) {
                        selection->toggle(item_ungrouped);
                    } else {
                        selection->set(item_ungrouped);
                    }

                    ret = TRUE;
                }
                break;
            }
            break;
        default:
            break;
    }

    if (!ret) {
        if (((SPEventContextClass *) parent_class)->item_handler)
            ret = ((SPEventContextClass *) parent_class)->item_handler(event_context, item, event);
    }

    return ret;
}

static gint
sp_node_context_root_handler(SPEventContext *event_context, GdkEvent *event)
{
    SPDesktop *desktop = event_context->desktop;
    SPNodeContext *nc = SP_NODE_CONTEXT(event_context);
    double const nudge = prefs_get_double_attribute_limited("options.nudgedistance", "value", 2, 0, 1000); // in px
    tolerance = prefs_get_int_attribute_limited("options.dragtolerance", "value", 0, 0, 100);
    int const snaps = prefs_get_int_attribute("options.rotationsnapsperpi", "value", 12);
    double const offset = prefs_get_double_attribute_limited("options.defaultscale", "value", 2, 0, 1000);

    gint ret = FALSE;

    switch (event->type) {
        case GDK_BUTTON_PRESS:
            if (event->button.button == 1) {
                // save drag origin
                xp = (gint) event->button.x;
                yp = (gint) event->button.y;
                within_tolerance = true;

                NR::Point const button_w(event->button.x,
                                         event->button.y);
                NR::Point const button_dt(sp_desktop_w2d_xy_point(desktop, button_w));
                sp_rubberband_start(desktop, button_dt);
                ret = TRUE;
            }
            break;
        case GDK_MOTION_NOTIFY:
            if (event->motion.state & GDK_BUTTON1_MASK) {

                if ( within_tolerance
                     && ( abs( (gint) event->motion.x - xp ) < tolerance )
                     && ( abs( (gint) event->motion.y - yp ) < tolerance ) ) {
                    break; // do not drag if we're within tolerance from origin
                }
                // Once the user has moved farther than tolerance from the original location
                // (indicating they intend to move the object, not click), then always process the
                // motion notify coordinates as given (no snapping back to origin)
                within_tolerance = false;

                NR::Point const motion_w(event->motion.x,
                                         event->motion.y);
                NR::Point const motion_dt(sp_desktop_w2d_xy_point(desktop, motion_w));
                sp_rubberband_move(motion_dt);
                nc->drag = TRUE;
                ret = TRUE;
            }
            break;
        case GDK_BUTTON_RELEASE:
            xp = yp = 0;
            if (event->button.button == 1) {
                NRRect b;
                if (sp_rubberband_rect(&b) && !within_tolerance) { // drag
                    if (nc->nodepath) {
                        sp_nodepath_select_rect(nc->nodepath, &b, event->button.state & GDK_SHIFT_MASK);
                    }
                } else {
                    if (!(nodeedit_rb_escaped)) { // unless something was cancelled
                        if (nc->nodepath && nc->nodepath->selected)
                            sp_nodepath_deselect(nc->nodepath);
                        else
                            SP_DT_SELECTION(desktop)->clear();
                    }
                }
                ret = TRUE;
                sp_rubberband_stop();
                nodeedit_rb_escaped = 0;
                nc->drag = FALSE;
                break;
            }
            break;
        case GDK_KEY_PRESS:
            switch (get_group0_keyval(&event->key)) {
                case GDK_Insert:
                case GDK_KP_Insert:
                    // with any modifiers
                    sp_node_selected_add_node();
                    ret = TRUE;
                    break;
                case GDK_Delete:
                case GDK_KP_Delete:
                case GDK_BackSpace:
                    // with any modifiers
                    sp_node_selected_delete();
                    ret = TRUE;
                    break;
                case GDK_C:
                case GDK_c:
                    if (MOD__SHIFT_ONLY) {
                        sp_node_selected_set_type(Inkscape::NodePath::NODE_CUSP);
                        ret = TRUE;
                    }
                    break;
                case GDK_S:
                case GDK_s:
                    if (MOD__SHIFT_ONLY) {
                        sp_node_selected_set_type(Inkscape::NodePath::NODE_SMOOTH);
                        ret = TRUE;
                    }
                    break;
                case GDK_Y:
                case GDK_y:
                    if (MOD__SHIFT_ONLY) {
                        sp_node_selected_set_type(Inkscape::NodePath::NODE_SYMM);
                        ret = TRUE;
                    }
                    break;
                case GDK_B:
                case GDK_b:
                    if (MOD__SHIFT_ONLY) {
                        sp_node_selected_break();
                        ret = TRUE;
                    }
                    break;
                case GDK_J:
                case GDK_j:
                    if (MOD__SHIFT_ONLY) {
                        sp_node_selected_join();
                        ret = TRUE;
                    }
                    break;
                case GDK_D:
                case GDK_d:
                    if (MOD__SHIFT_ONLY) {
                        sp_node_selected_duplicate();
                        ret = TRUE;
                    }
                    break;
                case GDK_L:
                case GDK_l:
                    if (MOD__SHIFT_ONLY) {
                        sp_node_selected_set_line_type(NR_LINETO);
                        ret = TRUE;
                    }
                    break;
                case GDK_K:
                case GDK_k:
                    if (MOD__SHIFT_ONLY) {
                        sp_node_selected_set_line_type(NR_CURVETO);
                        ret = TRUE;
                    }
                    break;
                case GDK_R:
                case GDK_r:
                    if (!MOD__CTRL && !MOD__ALT) {
                        // FIXME: add top panel button
                        sp_selected_path_reverse();
                        ret = TRUE;
                    }
                    break;
                case GDK_Left: // move selection left
                case GDK_KP_Left:
                case GDK_KP_4:
                    if (!MOD__CTRL) { // not ctrl
                        if (MOD__ALT) { // alt
                            if (MOD__SHIFT) sp_node_selected_move_screen(-10, 0); // shift
                            else sp_node_selected_move_screen(-1, 0); // no shift
                        }
                        else { // no alt
                            if (MOD__SHIFT) sp_node_selected_move(-10*nudge, 0); // shift
                            else sp_node_selected_move(-nudge, 0); // no shift
                        }
                        ret = TRUE;
                    }
                    break;
                case GDK_Up: // move selection up
                case GDK_KP_Up:
                case GDK_KP_8:
                    if (!MOD__CTRL) { // not ctrl
                        if (MOD__ALT) { // alt
                            if (MOD__SHIFT) sp_node_selected_move_screen(0, 10); // shift
                            else sp_node_selected_move_screen(0, 1); // no shift
                        }
                        else { // no alt
                            if (MOD__SHIFT) sp_node_selected_move(0, 10*nudge); // shift
                            else sp_node_selected_move(0, nudge); // no shift
                        }
                        ret = TRUE;
                    }
                    break;
                case GDK_Right: // move selection right
                case GDK_KP_Right:
                case GDK_KP_6:
                    if (!MOD__CTRL) { // not ctrl
                        if (MOD__ALT) { // alt
                            if (MOD__SHIFT) sp_node_selected_move_screen(10, 0); // shift
                            else sp_node_selected_move_screen(1, 0); // no shift
                        }
                        else { // no alt
                            if (MOD__SHIFT) sp_node_selected_move(10*nudge, 0); // shift
                            else sp_node_selected_move(nudge, 0); // no shift
                        }
                        ret = TRUE;
                    }
                    break;
                case GDK_Down: // move selection down
                case GDK_KP_Down:
                case GDK_KP_2:
                    if (!MOD__CTRL) { // not ctrl
                        if (MOD__ALT) { // alt
                            if (MOD__SHIFT) sp_node_selected_move_screen(0, -10); // shift
                            else sp_node_selected_move_screen(0, -1); // no shift
                        }
                        else { // no alt
                            if (MOD__SHIFT) sp_node_selected_move(0, -10*nudge); // shift
                            else sp_node_selected_move(0, -nudge); // no shift
                        }
                        ret = TRUE;
                    }
                    break;
                case GDK_Tab: // Tab - cycle selection forward
                    if (!(MOD__CTRL_ONLY || (MOD__CTRL && MOD__SHIFT))) {
                        sp_nodepath_select_next(nc->nodepath);
                        ret = TRUE;
                    }
                    break;
                case GDK_ISO_Left_Tab:  // Shift Tab - cycle selection backward
                    if (!(MOD__CTRL_ONLY || (MOD__CTRL && MOD__SHIFT))) {
                        sp_nodepath_select_prev(nc->nodepath);
                        ret = TRUE;
                    }
                    break;
                case GDK_Escape:
                {
                    NRRect b;
                    if (sp_rubberband_rect(&b)) { // cancel rubberband
                        sp_rubberband_stop();
                        nodeedit_rb_escaped = 1;
                    } else {
                        if (nc->nodepath && nc->nodepath->selected) {
                            sp_nodepath_deselect(nc->nodepath);
                        } else {
                            SP_DT_SELECTION(desktop)->clear();
                        }
                    }
                    ret = TRUE;
                    break;
                }

                case GDK_bracketleft:
                    if ( MOD__CTRL && !MOD__ALT && ( snaps != 0 ) ) {
                        if (nc->leftctrl)
                            sp_nodepath_selected_nodes_rotate (nc->nodepath, M_PI/snaps, -1, false);
                        if (nc->rightctrl)
                            sp_nodepath_selected_nodes_rotate (nc->nodepath, M_PI/snaps, 1, false);
                    } else if ( MOD__ALT && !MOD__CTRL ) {
                        if (nc->leftalt && nc->rightalt)
                            sp_nodepath_selected_nodes_rotate (nc->nodepath, 1, 0, true);
                        else {
                            if (nc->leftalt)
                                sp_nodepath_selected_nodes_rotate (nc->nodepath, 1, -1, true);
                            if (nc->rightalt)
                                sp_nodepath_selected_nodes_rotate (nc->nodepath, 1, 1, true);
                        }
                    } else if ( snaps != 0 ) {
                        sp_nodepath_selected_nodes_rotate (nc->nodepath, M_PI/snaps, 0, false);
                    }
                    ret = TRUE;
                    break;
                case GDK_bracketright:
                    if ( MOD__CTRL && !MOD__ALT && ( snaps != 0 ) ) {
                        if (nc->leftctrl)
                            sp_nodepath_selected_nodes_rotate (nc->nodepath, -M_PI/snaps, -1, false);
                        if (nc->rightctrl)
                            sp_nodepath_selected_nodes_rotate (nc->nodepath, -M_PI/snaps, 1, false);
                    } else if ( MOD__ALT && !MOD__CTRL ) {
                        if (nc->leftalt && nc->rightalt)
                            sp_nodepath_selected_nodes_rotate (nc->nodepath, -1, 0, true);
                        else {
                            if (nc->leftalt)
                                sp_nodepath_selected_nodes_rotate (nc->nodepath, -1, -1, true);
                            if (nc->rightalt)
                                sp_nodepath_selected_nodes_rotate (nc->nodepath, -1, 1, true);
                        }
                    } else if ( snaps != 0 ) {
                        sp_nodepath_selected_nodes_rotate (nc->nodepath, -M_PI/snaps, 0, false);
                    }
                    ret = TRUE;
                    break;
                case GDK_less:
                case GDK_comma:
                    if (MOD__CTRL) {
                        if (nc->leftctrl)
                            sp_nodepath_selected_nodes_scale(nc->nodepath, -offset, -1);
                        if (nc->rightctrl)
                            sp_nodepath_selected_nodes_scale(nc->nodepath, -offset, 1);
                    } else if (MOD__ALT) {
                        if (nc->leftalt && nc->rightalt)
                            sp_nodepath_selected_nodes_scale_screen(nc->nodepath, -1, 0);
                        else {
                            if (nc->leftalt)
                                sp_nodepath_selected_nodes_scale_screen(nc->nodepath, -1, -1);
                            if (nc->rightalt)
                                sp_nodepath_selected_nodes_scale_screen(nc->nodepath, -1, 1);
                        }
                    } else {
                        sp_nodepath_selected_nodes_scale(nc->nodepath, -offset, 0);
                    }
                    ret = TRUE;
                    break;
                case GDK_greater:
                case GDK_period:
                    if (MOD__CTRL) {
                        if (nc->leftctrl)
                            sp_nodepath_selected_nodes_scale(nc->nodepath, offset, -1);
                        if (nc->rightctrl)
                            sp_nodepath_selected_nodes_scale(nc->nodepath, offset, 1);
                    } else if (MOD__ALT) {
                        if (nc->leftalt && nc->rightalt)
                            sp_nodepath_selected_nodes_scale_screen(nc->nodepath, 1, 0);
                        else {
                            if (nc->leftalt)
                                sp_nodepath_selected_nodes_scale_screen(nc->nodepath, 1, -1);
                            if (nc->rightalt)
                                sp_nodepath_selected_nodes_scale_screen(nc->nodepath, 1, 1);
                        }
                    } else {
                        sp_nodepath_selected_nodes_scale(nc->nodepath, offset, 0);
                    }
                    ret = TRUE;
                    break;

                case GDK_Alt_L:
                    nc->leftalt = TRUE;
                    sp_node_context_show_modifier_tip(event_context, event);
                    break;
                case GDK_Alt_R:
                    nc->rightalt = TRUE;
                    sp_node_context_show_modifier_tip(event_context, event);
                    break;
                case GDK_Control_L:
                    nc->leftctrl = TRUE;
                    sp_node_context_show_modifier_tip(event_context, event);
                    break;
                case GDK_Control_R:
                    nc->rightctrl = TRUE;
                    sp_node_context_show_modifier_tip(event_context, event);
                    break;
                case GDK_Shift_L:
                case GDK_Shift_R:
                case GDK_Meta_L:
                case GDK_Meta_R:
                    sp_node_context_show_modifier_tip(event_context, event);
                    break;
                default:
                    ret = node_key(event);
                    break;
            }
            break;
        case GDK_KEY_RELEASE:
            switch (get_group0_keyval(&event->key)) {
                case GDK_Alt_L:
                    nc->leftalt = FALSE;
                    event_context->defaultMessageContext()->clear();
                    break;
                case GDK_Alt_R:
                    nc->rightalt = FALSE;
                    event_context->defaultMessageContext()->clear();
                    break;
                case GDK_Control_L:
                    nc->leftctrl = FALSE;
                    event_context->defaultMessageContext()->clear();
                    break;
                case GDK_Control_R:
                    nc->rightctrl = FALSE;
                    event_context->defaultMessageContext()->clear();
                    break;
                case GDK_Shift_L:
                case GDK_Shift_R:
                case GDK_Meta_L:
                case GDK_Meta_R:
                    event_context->defaultMessageContext()->clear();
                    break;
            }
            break;
        default:
            break;
    }

    if (!ret) {
        if (((SPEventContextClass *) parent_class)->root_handler)
            ret = ((SPEventContextClass *) parent_class)->root_handler(event_context, event);
    }

    return ret;
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
