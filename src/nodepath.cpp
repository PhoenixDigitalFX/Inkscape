#define __SP_NODEPATH_C__

/** \file
 * Path handler in node edit mode
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *
 * This code is in public domain
 */

#include "config.h"

#include <math.h>
//#include <string.h>
#include <gdk/gdkkeysyms.h>
#include "svg/svg.h"
#include "display/curve.h"
#include "display/sp-canvas-util.h"
#include "display/sp-ctrlline.h"
#include "display/sodipodi-ctrl.h"
#include <glibmm/i18n.h>
#include "libnr/n-art-bpath.h"
#include "knot.h"
#include "inkscape.h"
#include "document.h"
#include "desktop.h"
#include "desktop-handles.h"
#include "desktop-affine.h"
#include "snap.h"
#include "message-stack.h"
#include "message-context.h"
#include "node-context.h"
#include "nodepath.h"
#include "selection-chemistry.h"
#include "selection.h"
#include "xml/repr.h"
//#include "object-edit.h"
#include "prefs-utils.h"
#include "sp-metrics.h"
#include "sp-path.h"
#include "sp-shape.h"

#include "libnr/nr-point-ops.h"
#include <libnr/nr-rect.h>
//#include <libnr/nr-matrix.h>
#include <libnr/nr-matrix-fns.h>
#include <libnr/nr-matrix-ops.h>
#include <libnr/nr-point-matrix-ops.h>
#include "livarot/Path.h"
#include "splivarot.h"

class NR::Matrix;

/// \todo
/// evil evil evil. FIXME: conflict of two different Path classes!
/// There is a conflict in the namespace between two classes named Path.
/// #include "sp-flowtext.h"
/// #include "sp-flowregion.h" 

#define SP_TYPE_FLOWREGION            (sp_flowregion_get_type ())
#define SP_IS_FLOWREGION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SP_TYPE_FLOWREGION))
GType sp_flowregion_get_type (void);
#define SP_TYPE_FLOWTEXT            (sp_flowtext_get_type ())
#define SP_IS_FLOWTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SP_TYPE_FLOWTEXT))
GType sp_flowtext_get_type (void);
// end evil workaround

#include "helper/stlport.h"

#include <libnr/nr-point-matrix-ops.h>
#include <libnr/nr-point-fns.h>

/// \todo fixme: Implement these via preferences */

#define NODE_FILL          0xbfbfbf00
#define NODE_STROKE        0x000000ff
#define NODE_FILL_HI       0xff000000
#define NODE_STROKE_HI     0x000000ff
#define NODE_FILL_SEL      0x0000ffff
#define NODE_STROKE_SEL    0x000000ff
#define NODE_FILL_SEL_HI   0xff000000
#define NODE_STROKE_SEL_HI 0x000000ff
#define KNOT_FILL          0xffffffff
#define KNOT_STROKE        0x000000ff
#define KNOT_FILL_HI       0xff000000
#define KNOT_STROKE_HI     0x000000ff

static GMemChunk *nodechunk = NULL;

/* Creation from object */

static NArtBpath *subpath_from_bpath(Inkscape::NodePath::Path *np, NArtBpath *b, gchar const *t);
static gchar *parse_nodetypes(gchar const *types, gint length);

/* Object updating */

static void stamp_repr(Inkscape::NodePath::Path *np);
static SPCurve *create_curve(Inkscape::NodePath::Path *np);
static gchar *create_typestr(Inkscape::NodePath::Path *np);

static void sp_node_ensure_ctrls(Inkscape::NodePath::Node *node);

static void sp_nodepath_node_select(Inkscape::NodePath::Node *node, gboolean incremental, gboolean override);

static void sp_node_set_selected(Inkscape::NodePath::Node *node, gboolean selected);

/* Control knot placement, if node or other knot is moved */

static void sp_node_adjust_knot(Inkscape::NodePath::Node *node, gint which_adjust);
static void sp_node_adjust_knots(Inkscape::NodePath::Node *node);

/* Knot event handlers */

static void node_clicked(SPKnot *knot, guint state, gpointer data);
static void node_grabbed(SPKnot *knot, guint state, gpointer data);
static void node_ungrabbed(SPKnot *knot, guint state, gpointer data);
static gboolean node_request(SPKnot *knot, NR::Point *p, guint state, gpointer data);
static void node_ctrl_clicked(SPKnot *knot, guint state, gpointer data);
static void node_ctrl_grabbed(SPKnot *knot, guint state, gpointer data);
static void node_ctrl_ungrabbed(SPKnot *knot, guint state, gpointer data);
static gboolean node_ctrl_request(SPKnot *knot, NR::Point *p, guint state, gpointer data);
static void node_ctrl_moved(SPKnot *knot, NR::Point *p, guint state, gpointer data);

/* Constructors and destructors */

static Inkscape::NodePath::SubPath *sp_nodepath_subpath_new(Inkscape::NodePath::Path *nodepath);
static void sp_nodepath_subpath_destroy(Inkscape::NodePath::SubPath *subpath);
static void sp_nodepath_subpath_close(Inkscape::NodePath::SubPath *sp);
static void sp_nodepath_subpath_open(Inkscape::NodePath::SubPath *sp,Inkscape::NodePath::Node *n);
static Inkscape::NodePath::Node * sp_nodepath_node_new(Inkscape::NodePath::SubPath *sp,Inkscape::NodePath::Node *next,Inkscape::NodePath::NodeType type, NRPathcode code,
                                         NR::Point *ppos, NR::Point *pos, NR::Point *npos);
static void sp_nodepath_node_destroy(Inkscape::NodePath::Node *node);

/* Helpers */

static Inkscape::NodePath::NodeSide *sp_node_get_side(Inkscape::NodePath::Node *node, gint which);
static Inkscape::NodePath::NodeSide *sp_node_opposite_side(Inkscape::NodePath::Node *node,Inkscape::NodePath::NodeSide *me);
static NRPathcode sp_node_path_code_from_side(Inkscape::NodePath::Node *node,Inkscape::NodePath::NodeSide *me);
//from splivarot.cpp
Path::cut_position get_nearest_position_on_Path(SPItem * item, NR::Point p);

// active_node indicates mouseover node
static Inkscape::NodePath::Node *active_node = NULL;

/**
 * \brief Creates new nodepath from item 
 */
Inkscape::NodePath::Path *sp_nodepath_new(SPDesktop *desktop, SPItem *item)
{
    Inkscape::XML::Node *repr = SP_OBJECT(item)->repr;

    /** \todo
     * FIXME: remove this. We don't want to edit paths inside flowtext.
     * Instead we will build our flowtext with cloned paths, so that the
     * real paths are outside the flowtext and thus editable as usual.
     */
    if (SP_IS_FLOWTEXT(item)) {
        for (SPObject *child = sp_object_first_child(SP_OBJECT(item)) ; child != NULL; child = SP_OBJECT_NEXT(child) ) {
            if SP_IS_FLOWREGION(child) {
                SPObject *grandchild = sp_object_first_child(SP_OBJECT(child));
                if (grandchild && SP_IS_PATH(grandchild)) {
                    item = SP_ITEM(grandchild);
                    break;
                }
            }
        }
    }

    if (!SP_IS_PATH(item))
        return NULL;
    SPPath *path = SP_PATH(item);
    SPCurve *curve = sp_shape_get_curve(SP_SHAPE(path));
    if (curve == NULL)
        return NULL;

    NArtBpath *bpath = sp_curve_first_bpath(curve);
    gint length = curve->end;
    if (length == 0)
        return NULL; // prevent crash for one-node paths

    gchar const *nodetypes = repr->attribute("sodipodi:nodetypes");
    gchar *typestr = parse_nodetypes(nodetypes, length);

    //Create new nodepath
    Inkscape::NodePath::Path *np = g_new(Inkscape::NodePath::Path, 1);
    if (!np)
        return NULL;

    // Set defaults
    np->desktop     = desktop;
    np->path        = path;
    np->subpaths    = NULL;
    np->selected    = NULL;
    np->nodeContext = NULL; //Let the context that makes this set it

    // we need to update item's transform from the repr here,
    // because they may be out of sync when we respond 
    // to a change in repr by regenerating nodepath     --bb
    sp_object_read_attr(SP_OBJECT(item), "transform");

    np->i2d  = sp_item_i2d_affine(SP_ITEM(path));
    np->d2i  = np->i2d.inverse();
    np->repr = repr;

    /* Now the bitchy part (lauris) */

    NArtBpath *b = bpath;

    while (b->code != NR_END) {
        b = subpath_from_bpath(np, b, typestr + (b - bpath));
    }

    g_free(typestr);
    sp_curve_unref(curve);

    return np;
}

/**
 * Destroys nodepath's subpaths, then itself, also tell context about it.
 */
void sp_nodepath_destroy(Inkscape::NodePath::Path *np) {

    if (!np)  //soft fail, like delete
        return;

    while (np->subpaths) {
        sp_nodepath_subpath_destroy((Inkscape::NodePath::SubPath *) np->subpaths->data);
    }

    //Inform the context that made me, if any, that I am gone.
    if (np->nodeContext)
        np->nodeContext->nodepath = NULL;

    g_assert(!np->selected);

    np->desktop = NULL;

    g_free(np);
}


/**
 *  Return the node count of a given NodeSubPath.
 */
static gint sp_nodepath_subpath_get_node_count(Inkscape::NodePath::SubPath *subpath)
{
    if (!subpath)
        return 0;
    gint nodeCount = g_list_length(subpath->nodes);
    return nodeCount;
}

/**
 *  Return the node count of a given NodePath.
 */
static gint sp_nodepath_get_node_count(Inkscape::NodePath::Path *np)
{
    if (!np)
        return 0;
    gint nodeCount = 0;
    for (GList *item = np->subpaths ; item ; item=item->next) {
       Inkscape::NodePath::SubPath *subpath = (Inkscape::NodePath::SubPath *)item->data;
        nodeCount += g_list_length(subpath->nodes);
    }
    return nodeCount;
}


/**
 * Clean up a nodepath after editing.
 * 
 * Currently we are deleting trivial subpaths.
 */
static void sp_nodepath_cleanup(Inkscape::NodePath::Path *nodepath)
{
    GList *badSubPaths = NULL;

    //Check all subpaths to be >=2 nodes
    for (GList *l = nodepath->subpaths; l ; l=l->next) {
       Inkscape::NodePath::SubPath *sp = (Inkscape::NodePath::SubPath *)l->data;
        if (sp_nodepath_subpath_get_node_count(sp)<2)
            badSubPaths = g_list_append(badSubPaths, sp);
    }

    //Delete them.  This second step is because sp_nodepath_subpath_destroy()
    //also removes the subpath from nodepath->subpaths
    for (GList *l = badSubPaths; l ; l=l->next) {
       Inkscape::NodePath::SubPath *sp = (Inkscape::NodePath::SubPath *)l->data;
        sp_nodepath_subpath_destroy(sp);
    }

    g_list_free(badSubPaths);
}



/**
 * \brief Returns true if the argument nodepath and the d attribute in 
 * its repr do not match. 
 *
 * This may happen if repr was changed in, e.g., XML editor or by undo. 
 * 
 * \todo
 * UGLY HACK, think how we can eliminate it.
 */
gboolean nodepath_repr_d_changed(Inkscape::NodePath::Path *np, char const *newd)
{
    g_assert(np);

    SPCurve *curve = create_curve(np);

    gchar *svgpath = sp_svg_write_path(curve->bpath);

    char const *attr_d = ( newd
                           ? newd
                           : SP_OBJECT(np->path)->repr->attribute("d") );

    gboolean ret;
    if (attr_d && svgpath)
        ret = strcmp(attr_d, svgpath);
    else 
        ret = TRUE;

    g_free(svgpath);
    sp_curve_unref(curve);

    return ret;
}

/**
 * \brief Returns true if the argument nodepath and the sodipodi:nodetypes 
 * attribute in its repr do not match. 
 *
 * This may happen if repr was changed in, e.g., the XML editor or by undo.
 */
gboolean nodepath_repr_typestr_changed(Inkscape::NodePath::Path *np, char const *newtypestr)
{
    g_assert(np);
    gchar *typestr = create_typestr(np);
    char const *attr_typestr = ( newtypestr
                                 ? newtypestr
                                 : SP_OBJECT(np->path)->repr->attribute("sodipodi:nodetypes") );
    gboolean const ret = (attr_typestr && strcmp(attr_typestr, typestr));

    g_free(typestr);

    return ret;
}

/**
 * Create new nodepath from b, make it subpath of np.
 * \param t The node type.
 * \todo Fixme: t should be a proper type, rather than gchar
 */
static NArtBpath *subpath_from_bpath(Inkscape::NodePath::Path *np, NArtBpath *b, gchar const *t)
{
    NR::Point ppos, pos, npos;

    g_assert((b->code == NR_MOVETO) || (b->code == NR_MOVETO_OPEN));

    Inkscape::NodePath::SubPath *sp = sp_nodepath_subpath_new(np);
    bool const closed = (b->code == NR_MOVETO);

    pos = NR::Point(b->x3, b->y3) * np->i2d;
    if (b[1].code == NR_CURVETO) {
        npos = NR::Point(b[1].x1, b[1].y1) * np->i2d;
    } else {
        npos = pos;
    }
    Inkscape::NodePath::Node *n;
    n = sp_nodepath_node_new(sp, NULL, (Inkscape::NodePath::NodeType) *t, NR_MOVETO, &pos, &pos, &npos);
    g_assert(sp->first == n);
    g_assert(sp->last  == n);

    b++;
    t++;
    while ((b->code == NR_CURVETO) || (b->code == NR_LINETO)) {
        pos = NR::Point(b->x3, b->y3) * np->i2d;
        if (b->code == NR_CURVETO) {
            ppos = NR::Point(b->x2, b->y2) * np->i2d;
        } else {
            ppos = pos;
        }
        if (b[1].code == NR_CURVETO) {
            npos = NR::Point(b[1].x1, b[1].y1) * np->i2d;
        } else {
            npos = pos;
        }
        n = sp_nodepath_node_new(sp, NULL, (Inkscape::NodePath::NodeType)*t, b->code, &ppos, &pos, &npos);
        b++;
        t++;
    }

    if (closed) sp_nodepath_subpath_close(sp);

    return b;
}

/**
 * Convert from sodipodi:nodetypes to new style type string.
 */
static gchar *parse_nodetypes(gchar const *types, gint length)
{
    g_assert(length > 0);

    gchar *typestr = g_new(gchar, length + 1);

    gint pos = 0;

    if (types) {
        for (gint i = 0; types[i] && ( i < length ); i++) {
            while ((types[i] > '\0') && (types[i] <= ' ')) i++;
            if (types[i] != '\0') {
                switch (types[i]) {
                    case 's':
                        typestr[pos++] =Inkscape::NodePath::NODE_SMOOTH;
                        break;
                    case 'z':
                        typestr[pos++] =Inkscape::NodePath::NODE_SYMM;
                        break;
                    case 'c':
                        typestr[pos++] =Inkscape::NodePath::NODE_CUSP;
                        break;
                    default:
                        typestr[pos++] =Inkscape::NodePath::NODE_NONE;
                        break;
                }
            }
        }
    }

    while (pos < length) typestr[pos++] =Inkscape::NodePath::NODE_NONE;

    return typestr;
}

/**
 * Make curve out of path and associate it with it.
 */
static void update_object(Inkscape::NodePath::Path *np)
{
    g_assert(np);

    SPCurve *curve = create_curve(np);

    sp_shape_set_curve(SP_SHAPE(np->path), curve, TRUE);

    sp_curve_unref(curve);
}

/**
 * Update XML path node with data from path object.
 */
static void update_repr_internal(Inkscape::NodePath::Path *np)
{
    g_assert(np);

    Inkscape::XML::Node *repr = SP_OBJECT(np->path)->repr;

    SPCurve *curve = create_curve(np);
    gchar *typestr = create_typestr(np);
    gchar *svgpath = sp_svg_write_path(curve->bpath);

    sp_repr_set_attr(repr, "d", svgpath);
    sp_repr_set_attr(repr, "sodipodi:nodetypes", typestr);

    g_free(svgpath);
    g_free(typestr);
    sp_curve_unref(curve);
}

/**
 * Update XML path node with data from path object, commit changes forever.
 */
static void update_repr(Inkscape::NodePath::Path *np)
{
    update_repr_internal(np);
    sp_document_done(SP_DT_DOCUMENT(np->desktop));
}

/**
 * Update XML path node with data from path object, commit changes with undo.
 */
static void update_repr_keyed(Inkscape::NodePath::Path *np, gchar const *key)
{
    update_repr_internal(np);
    sp_document_maybe_done(SP_DT_DOCUMENT(np->desktop), key);
}

/**
 * Make duplicate of path, replace corresponding XML node in tree, commit.
 */
static void stamp_repr(Inkscape::NodePath::Path *np)
{
    g_assert(np);

    Inkscape::XML::Node *old_repr = SP_OBJECT(np->path)->repr;
    Inkscape::XML::Node *new_repr = old_repr->duplicate();

    // remember the position of the item
    gint pos = old_repr->position();
    // remember parent
    Inkscape::XML::Node *parent = sp_repr_parent(old_repr);

    SPCurve *curve = create_curve(np);
    gchar *typestr = create_typestr(np);

    gchar *svgpath = sp_svg_write_path(curve->bpath);

    sp_repr_set_attr(new_repr, "d", svgpath);
    sp_repr_set_attr(new_repr, "sodipodi:nodetypes", typestr);

    // add the new repr to the parent
    parent->appendChild(new_repr);
    // move to the saved position
    new_repr->setPosition(pos > 0 ? pos : 0);

    sp_document_done(SP_DT_DOCUMENT(np->desktop));

    sp_repr_unref(new_repr);
    g_free(svgpath);
    g_free(typestr);
    sp_curve_unref(curve);
}

/**
 * Create curve from path.
 */
static SPCurve *create_curve(Inkscape::NodePath::Path *np)
{
    SPCurve *curve = sp_curve_new();

    for (GList *spl = np->subpaths; spl != NULL; spl = spl->next) {
       Inkscape::NodePath::SubPath *sp = (Inkscape::NodePath::SubPath *) spl->data;
        sp_curve_moveto(curve,
                        sp->first->pos * np->d2i);
       Inkscape::NodePath::Node *n = sp->first->n.other;
        while (n) {
            NR::Point const end_pt = n->pos * np->d2i;
            switch (n->code) {
                case NR_LINETO:
                    sp_curve_lineto(curve, end_pt);
                    break;
                case NR_CURVETO:
                    sp_curve_curveto(curve,
                                     n->p.other->n.pos * np->d2i,
                                     n->p.pos * np->d2i,
                                     end_pt);
                    break;
                default:
                    g_assert_not_reached();
                    break;
            }
            if (n != sp->last) {
                n = n->n.other;
            } else {
                n = NULL;
            }
        }
        if (sp->closed) {
            sp_curve_closepath(curve);
        }
    }

    return curve;
}

/**
 * Convert path type string to sodipodi:nodetypes style.
 */
static gchar *create_typestr(Inkscape::NodePath::Path *np)
{
    gchar *typestr = g_new(gchar, 32);
    gint len = 32;
    gint pos = 0;

    for (GList *spl = np->subpaths; spl != NULL; spl = spl->next) {
       Inkscape::NodePath::SubPath *sp = (Inkscape::NodePath::SubPath *) spl->data;

        if (pos >= len) {
            typestr = g_renew(gchar, typestr, len + 32);
            len += 32;
        }

        typestr[pos++] = 'c';

       Inkscape::NodePath::Node *n;
        n = sp->first->n.other;
        while (n) {
            gchar code;

            switch (n->type) {
                case Inkscape::NodePath::NODE_CUSP:
                    code = 'c';
                    break;
                case Inkscape::NodePath::NODE_SMOOTH:
                    code = 's';
                    break;
                case Inkscape::NodePath::NODE_SYMM:
                    code = 'z';
                    break;
                default:
                    g_assert_not_reached();
                    code = '\0';
                    break;
            }

            if (pos >= len) {
                typestr = g_renew(gchar, typestr, len + 32);
                len += 32;
            }

            typestr[pos++] = code;

            if (n != sp->last) {
                n = n->n.other;
            } else {
                n = NULL;
            }
        }
    }

    if (pos >= len) {
        typestr = g_renew(gchar, typestr, len + 1);
        len += 1;
    }

    typestr[pos++] = '\0';

    return typestr;
}

/**
 * Returns current path in context.
 */
static Inkscape::NodePath::Path *sp_nodepath_current()
{
    if (!SP_ACTIVE_DESKTOP) {
        return NULL;
    }

    SPEventContext *event_context = (SP_ACTIVE_DESKTOP)->event_context;

    if (!SP_IS_NODE_CONTEXT(event_context)) {
        return NULL;
    }

    return SP_NODE_CONTEXT(event_context)->nodepath;
}



/**
 \brief Fills node and control positions for three nodes, splitting line
  marked by end at distance t.
 */
static void sp_nodepath_line_midpoint(Inkscape::NodePath::Node *new_path,Inkscape::NodePath::Node *end, gdouble t)
{
    g_assert(new_path != NULL);
    g_assert(end      != NULL);

    g_assert(end->p.other == new_path);
   Inkscape::NodePath::Node *start = new_path->p.other;
    g_assert(start);

    if (end->code == NR_LINETO) {
        new_path->type =Inkscape::NodePath::NODE_CUSP;
        new_path->code = NR_LINETO;
        new_path->pos  = (t * start->pos + (1 - t) * end->pos);
    } else {
        new_path->type =Inkscape::NodePath::NODE_SMOOTH;
        new_path->code = NR_CURVETO;
        gdouble s      = 1 - t;
        for (int dim = 0; dim < 2; dim++) {
            NR::Coord const f000 = start->pos[dim];
            NR::Coord const f001 = start->n.pos[dim];
            NR::Coord const f011 = end->p.pos[dim];
            NR::Coord const f111 = end->pos[dim];
            NR::Coord const f00t = s * f000 + t * f001;
            NR::Coord const f01t = s * f001 + t * f011;
            NR::Coord const f11t = s * f011 + t * f111;
            NR::Coord const f0tt = s * f00t + t * f01t;
            NR::Coord const f1tt = s * f01t + t * f11t;
            NR::Coord const fttt = s * f0tt + t * f1tt;
            start->n.pos[dim]    = f00t;
            new_path->p.pos[dim] = f0tt;
            new_path->pos[dim]   = fttt;
            new_path->n.pos[dim] = f1tt;
            end->p.pos[dim]      = f11t;
        }
    }
}

/**
 * Adds new node on direct line between two nodes, activates handles of all 
 * three nodes.
 */
static Inkscape::NodePath::Node *sp_nodepath_line_add_node(Inkscape::NodePath::Node *end, gdouble t)
{
    g_assert(end);
    g_assert(end->subpath);
    g_assert(g_list_find(end->subpath->nodes, end));

   Inkscape::NodePath::Node *start = end->p.other;
    g_assert( start->n.other == end );
   Inkscape::NodePath::Node *newnode = sp_nodepath_node_new(end->subpath,
                                               end,
                                              Inkscape::NodePath::NODE_SMOOTH,
                                               (NRPathcode)end->code,
                                               &start->pos, &start->pos, &start->n.pos);
    sp_nodepath_line_midpoint(newnode, end, t);

    sp_node_ensure_ctrls(start);
    sp_node_ensure_ctrls(newnode);
    sp_node_ensure_ctrls(end);

    return newnode;
}

/**
\brief Break the path at the node: duplicate the argument node, start a new subpath with the duplicate, and copy all nodes after the argument node to it
*/
static Inkscape::NodePath::Node *sp_nodepath_node_break(Inkscape::NodePath::Node *node)
{
    g_assert(node);
    g_assert(node->subpath);
    g_assert(g_list_find(node->subpath->nodes, node));

   Inkscape::NodePath::SubPath *sp = node->subpath;
    Inkscape::NodePath::Path *np    = sp->nodepath;

    if (sp->closed) {
        sp_nodepath_subpath_open(sp, node);
        return sp->first;
    } else {
        // no break for end nodes
        if (node == sp->first) return NULL;
        if (node == sp->last ) return NULL;

        // create a new subpath
       Inkscape::NodePath::SubPath *newsubpath = sp_nodepath_subpath_new(np);

        // duplicate the break node as start of the new subpath
       Inkscape::NodePath::Node *newnode = sp_nodepath_node_new(newsubpath, NULL, (Inkscape::NodePath::NodeType)node->type, NR_MOVETO, &node->pos, &node->pos, &node->n.pos);

        while (node->n.other) { // copy the remaining nodes into the new subpath
           Inkscape::NodePath::Node *n  = node->n.other;
           Inkscape::NodePath::Node *nn = sp_nodepath_node_new(newsubpath, NULL, (Inkscape::NodePath::NodeType)n->type, (NRPathcode)n->code, &n->p.pos, &n->pos, &n->n.pos);
            if (n->selected) {
                sp_nodepath_node_select(nn, TRUE, TRUE); //preserve selection
            }
            sp_nodepath_node_destroy(n); // remove the point on the original subpath
        }

        return newnode;
    }
}

/**
 * Duplicate node and connect to neighbours.
 */
static Inkscape::NodePath::Node *sp_nodepath_node_duplicate(Inkscape::NodePath::Node *node)
{
    g_assert(node);
    g_assert(node->subpath);
    g_assert(g_list_find(node->subpath->nodes, node));

   Inkscape::NodePath::SubPath *sp = node->subpath;

    NRPathcode code = (NRPathcode) node->code;
    if (code == NR_MOVETO) { // if node is the endnode,
        node->code = NR_LINETO; // new one is inserted before it, so change that to line
    }

    Inkscape::NodePath::Node *newnode = sp_nodepath_node_new(sp, node, (Inkscape::NodePath::NodeType)node->type, code, &node->p.pos, &node->pos, &node->n.pos);

    return newnode;
}

static void sp_node_control_mirror_n_to_p(Inkscape::NodePath::Node *node)
{
    node->p.pos = (node->pos + (node->pos - node->n.pos));
}

static void sp_node_control_mirror_p_to_n(Inkscape::NodePath::Node *node)
{
    node->n.pos = (node->pos + (node->pos - node->p.pos));
}

/**
 * Change line type at node, with side effects on neighbours.
 */
static void sp_nodepath_set_line_type(Inkscape::NodePath::Node *end, NRPathcode code)
{
    g_assert(end);
    g_assert(end->subpath);
    g_assert(end->p.other);

    if (end->code == static_cast< guint > ( code ) )
        return;

   Inkscape::NodePath::Node *start = end->p.other;

    end->code = code;

    if (code == NR_LINETO) {
        if (start->code == NR_LINETO) start->type =Inkscape::NodePath::NODE_CUSP;
        if (end->n.other) {
            if (end->n.other->code == NR_LINETO) end->type =Inkscape::NodePath::NODE_CUSP;
        }
        sp_node_adjust_knot(start, -1);
        sp_node_adjust_knot(end, 1);
    } else {
        NR::Point delta = end->pos - start->pos;
        start->n.pos = start->pos + delta / 3;
        end->p.pos = end->pos - delta / 3;
        sp_node_adjust_knot(start, 1);
        sp_node_adjust_knot(end, -1);
    }

    sp_node_ensure_ctrls(start);
    sp_node_ensure_ctrls(end);
}

/**
 * Change node type, and its handles accordingly.
 */
static Inkscape::NodePath::Node *sp_nodepath_set_node_type(Inkscape::NodePath::Node *node,Inkscape::NodePath::NodeType type)
{
    g_assert(node);
    g_assert(node->subpath);

    if (type == static_cast<Inkscape::NodePath::NodeType>(static_cast< guint >(node->type) ) )
        return node;

    if ((node->p.other != NULL) && (node->n.other != NULL)) {
        if ((node->code == NR_LINETO) && (node->n.other->code == NR_LINETO)) {
            type =Inkscape::NodePath::NODE_CUSP;
        }
    }

    node->type = type;

    if (node->type == Inkscape::NodePath::NODE_CUSP) {
        g_object_set(G_OBJECT(node->knot), "shape", SP_KNOT_SHAPE_DIAMOND, "size", 9, NULL);
    } else {
        g_object_set(G_OBJECT(node->knot), "shape", SP_KNOT_SHAPE_SQUARE, "size", 7, NULL);
    }

    sp_node_adjust_knots(node);

    sp_nodepath_update_statusbar(node->subpath->nodepath);

    return node;
}

/**
 * Same as sp_nodepath_set_node_type(), but also converts, if necessary, 
 * adjacent segments from lines to curves.
*/
void sp_nodepath_convert_node_type(Inkscape::NodePath::Node *node, Inkscape::NodePath::NodeType type)
{
    if (type == Inkscape::NodePath::NODE_SYMM || type == Inkscape::NodePath::NODE_SMOOTH) {
        if ((node->p.other != NULL) && (node->code == NR_LINETO || node->pos == node->p.pos)) {
            // convert adjacent segment BEFORE to curve
            node->code = NR_CURVETO;
            NR::Point delta;
            if (node->n.other != NULL)
                delta = node->n.other->pos - node->p.other->pos;
            else 
                delta = node->pos - node->p.other->pos;
            node->p.pos = node->pos - delta / 4;
            sp_node_ensure_ctrls(node);
        }

        if ((node->n.other != NULL) && (node->n.other->code == NR_LINETO || node->pos == node->n.pos)) {
            // convert adjacent segment AFTER to curve
            node->n.other->code = NR_CURVETO;
            NR::Point delta;
            if (node->p.other != NULL)
                delta = node->p.other->pos - node->n.other->pos;
            else 
                delta = node->pos - node->n.other->pos;
            node->n.pos = node->pos - delta / 4;
            sp_node_ensure_ctrls(node);
        }
    }

    sp_nodepath_set_node_type (node, type);
}

/**
 * Move node to point, and adjust its and neighbouring handles.
 */
void sp_node_moveto(Inkscape::NodePath::Node *node, NR::Point p)
{
    NR::Point delta = p - node->pos;
    node->pos = p;

    node->p.pos += delta;
    node->n.pos += delta;

    if (node->p.other) {
        if (node->code == NR_LINETO) {
            sp_node_adjust_knot(node, 1);
            sp_node_adjust_knot(node->p.other, -1);
        }
    }
    if (node->n.other) {
        if (node->n.other->code == NR_LINETO) {
            sp_node_adjust_knot(node, -1);
            sp_node_adjust_knot(node->n.other, 1);
        }
    }

    sp_node_ensure_ctrls(node);
}

/**
 * Call sp_node_moveto() for node selection and handle possible snapping.
 */
static void sp_nodepath_selected_nodes_move(Inkscape::NodePath::Path *nodepath, NR::Coord dx, NR::Coord dy,
                                            bool const snap = true)
{
    NR::Coord best[2] = { NR_HUGE, NR_HUGE };
    NR::Point delta(dx, dy);
    NR::Point best_pt = delta;

    if (snap) {
        for (GList *l = nodepath->selected; l != NULL; l = l->next) {
           Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
            NR::Point p = n->pos + delta;
            for (int dim = 0; dim < 2; dim++) {
                NR::Coord dist = namedview_dim_snap(nodepath->desktop->namedview,
                                                    Snapper::SNAP_POINT, p,
                                                    NR::Dim2(dim));
                if (dist < best[dim]) {
                    best[dim] = dist;
                    best_pt[dim] = p[dim] - n->pos[dim];
                }
            }
        }
    }

    for (GList *l = nodepath->selected; l != NULL; l = l->next) {
       Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
        sp_node_moveto(n, n->pos + best_pt);
    }

    update_object(nodepath);
}

/**
 * Move node selection to point, adjust its and neighbouring handles,
 * handle possible snapping, and commit the change with possible undo.
 */
void
sp_node_selected_move(gdouble dx, gdouble dy)
{
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) return;

    sp_nodepath_selected_nodes_move(nodepath, dx, dy, false);

    if (dx == 0) {
        update_repr_keyed(nodepath, "node:move:vertical");
    } else if (dy == 0) {
        update_repr_keyed(nodepath, "node:move:horizontal");
    } else {
        update_repr(nodepath);
    }
}

/**
 * Move node selection off screen and commit the change.
 */
void
sp_node_selected_move_screen(gdouble dx, gdouble dy)
{
    // borrowed from sp_selection_move_screen in selection-chemistry.c
    // we find out the current zoom factor and divide deltas by it
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    g_return_if_fail(SP_IS_DESKTOP(desktop));

    gdouble zoom = SP_DESKTOP_ZOOM(desktop);
    gdouble zdx = dx / zoom;
    gdouble zdy = dy / zoom;

    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) return;

    sp_nodepath_selected_nodes_move(nodepath, zdx, zdy, false);

    if (dx == 0) {
        update_repr_keyed(nodepath, "node:move:vertical");
    } else if (dy == 0) {
        update_repr_keyed(nodepath, "node:move:horizontal");
    } else {
        update_repr(nodepath);
    }
}

/**
 * Ensure knot on side of node is visible/invisible.
 */
static void sp_node_ensure_knot(Inkscape::NodePath::Node *node, gint which, gboolean show_knot)
{
    g_assert(node != NULL);

   Inkscape::NodePath::NodeSide *side = sp_node_get_side(node, which);
    NRPathcode code = sp_node_path_code_from_side(node, side);

    show_knot = show_knot && (code == NR_CURVETO) && (NR::L2(side->pos - node->pos) > 1e-6);

    if (show_knot) {
        if (!SP_KNOT_IS_VISIBLE(side->knot)) {
            sp_knot_show(side->knot);
        }

        sp_knot_set_position(side->knot, &side->pos, 0);
        sp_canvas_item_show(side->line);

    } else {
        if (SP_KNOT_IS_VISIBLE(side->knot)) {
            sp_knot_hide(side->knot);
        }
        sp_canvas_item_hide(side->line);
    }
}

/**
 * Ensure handles on node and neighbours of node are visible if selected.
 */
static void sp_node_ensure_ctrls(Inkscape::NodePath::Node *node)
{
    g_assert(node != NULL);

    if (!SP_KNOT_IS_VISIBLE(node->knot)) {
        sp_knot_show(node->knot);
    }

    sp_knot_set_position(node->knot, &node->pos, 0);

    gboolean show_knots = node->selected;
    if (node->p.other != NULL) {
        if (node->p.other->selected) show_knots = TRUE;
    }
    if (node->n.other != NULL) {
        if (node->n.other->selected) show_knots = TRUE;
    }

    sp_node_ensure_knot(node, -1, show_knots);
    sp_node_ensure_knot(node, 1, show_knots);
}

/**
 * Call sp_node_ensure_ctrls() for all nodes on subpath.
 */
static void sp_nodepath_subpath_ensure_ctrls(Inkscape::NodePath::SubPath *subpath)
{
    g_assert(subpath != NULL);

    for (GList *l = subpath->nodes; l != NULL; l = l->next) {
        sp_node_ensure_ctrls((Inkscape::NodePath::Node *) l->data);
    }
}

/**
 * Call sp_nodepath_subpath_ensure_ctrls() for all subpaths of nodepath.
 */
static void sp_nodepath_ensure_ctrls(Inkscape::NodePath::Path *nodepath)
{
    g_assert(nodepath != NULL);

    for (GList *l = nodepath->subpaths; l != NULL; l = l->next) {
        sp_nodepath_subpath_ensure_ctrls((Inkscape::NodePath::SubPath *) l->data);
    }
}

/**
 * Adds all selected nodes in nodepath to list.
 */
void Inkscape::NodePath::Path::selection(std::list<Node *> &l)
{
    StlConv<Node *>::list(l, selected);
/// \todo this adds a copying, rework when the selection becomes a stl list
}

/**
 * Align selected nodes on the specified axis.
 */
void sp_nodepath_selected_align(Inkscape::NodePath::Path *nodepath, NR::Dim2 axis)
{
    if ( !nodepath || !nodepath->selected ) { // no nodepath, or no nodes selected
        return;
    }

    if ( !nodepath->selected->next ) { // only one node selected
        return;
    }
   Inkscape::NodePath::Node *pNode = reinterpret_cast<Inkscape::NodePath::Node *>(nodepath->selected->data);
    NR::Point dest(pNode->pos);
    for (GList *l = nodepath->selected; l != NULL; l = l->next) {
        pNode = reinterpret_cast<Inkscape::NodePath::Node *>(l->data);
        if (pNode) {
            dest[axis] = pNode->pos[axis];
            sp_node_moveto(pNode, dest);
        }
    }
    if (axis == NR::X) {
        update_repr_keyed(nodepath, "node:move:vertical");
    } else {
        update_repr_keyed(nodepath, "node:move:horizontal");
    }
}

/// Helper struct.
struct NodeSort
{
   Inkscape::NodePath::Node *_node;
    NR::Coord _coord;
    /// \todo use vectorof pointers instead of calling copy ctor
    NodeSort(Inkscape::NodePath::Node *node, NR::Dim2 axis) :
        _node(node), _coord(node->pos[axis])
    {}

};

static bool operator<(NodeSort const &a, NodeSort const &b)
{
    return (a._coord < b._coord);
}

/**
 * Distribute selected nodes on the specified axis.
 */
void sp_nodepath_selected_distribute(Inkscape::NodePath::Path *nodepath, NR::Dim2 axis)
{
    if ( !nodepath || !nodepath->selected ) { // no nodepath, or no nodes selected
        return;
    }

    if ( ! (nodepath->selected->next && nodepath->selected->next->next) ) { // less than 3 nodes selected
        return;
    }

   Inkscape::NodePath::Node *pNode = reinterpret_cast<Inkscape::NodePath::Node *>(nodepath->selected->data);
    std::vector<NodeSort> sorted;
    for (GList *l = nodepath->selected; l != NULL; l = l->next) {
        pNode = reinterpret_cast<Inkscape::NodePath::Node *>(l->data);
        if (pNode) {
            NodeSort n(pNode, axis);
            sorted.push_back(n);
            //dest[axis] = pNode->pos[axis];
            //sp_node_moveto(pNode, dest);
        }
    }
    std::sort(sorted.begin(), sorted.end());
    unsigned int len = sorted.size();
    //overall bboxes span
    float dist = (sorted.back()._coord -
                  sorted.front()._coord);
    //new distance between each bbox
    float step = (dist) / (len - 1);
    float pos = sorted.front()._coord;
    for ( std::vector<NodeSort> ::iterator it(sorted.begin());
          it < sorted.end();
          it ++ )
    {
        NR::Point dest((*it)._node->pos);
        dest[axis] = pos;
        sp_node_moveto((*it)._node, dest);
        pos += step;
    }

    if (axis == NR::X) {
        update_repr_keyed(nodepath, "node:move:horizontal");
    } else {
        update_repr_keyed(nodepath, "node:move:vertical");
    }
}


/**
 * Call sp_nodepath_line_add_node() for all selected segments.
 */
void
sp_node_selected_add_node(void)
{
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) {
        return;
    }

    GList *nl = NULL;

    for (GList *l = nodepath->selected; l != NULL; l = l->next) {
       Inkscape::NodePath::Node *t = (Inkscape::NodePath::Node *) l->data;
        g_assert(t->selected);
        if (t->p.other && t->p.other->selected) {
            nl = g_list_prepend(nl, t);
        }
    }

    while (nl) {
       Inkscape::NodePath::Node *t = (Inkscape::NodePath::Node *) nl->data;
       Inkscape::NodePath::Node *n = sp_nodepath_line_add_node(t, 0.5);
        sp_nodepath_node_select(n, TRUE, FALSE);
        nl = g_list_remove(nl, t);
    }

    /** \todo fixme: adjust ? */
    sp_nodepath_ensure_ctrls(nodepath);

    update_repr(nodepath);

    sp_nodepath_update_statusbar(nodepath);
}

/**
 * Select segment nearest to point
 */
void
sp_nodepath_select_segment_near_point(SPItem * item, NR::Point p, bool toggle)
{
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) {
        return;
    }

    Path::cut_position position = get_nearest_position_on_Path(item, p);

    //find segment to segment
    Inkscape::NodePath::Node *e = sp_nodepath_get_node_by_index(position.piece);
   
    sp_nodepath_node_select(e, (gboolean) toggle, FALSE);
    sp_nodepath_node_select(e->p.other, TRUE, FALSE);

    sp_nodepath_ensure_ctrls(nodepath);

    sp_nodepath_update_statusbar(nodepath);
}

/**
 * Add a node nearest to point
 */
void
sp_nodepath_add_node_near_point(SPItem * item, NR::Point p)
{
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) {
        return;
    }

    Path::cut_position position = get_nearest_position_on_Path(item, p);

    //find segment to split
    Inkscape::NodePath::Node *e = sp_nodepath_get_node_by_index(position.piece);
   
    //don't know why but t seems to flip for lines
    if (sp_node_path_code_from_side(e, sp_node_get_side(e, -1)) == NR_LINETO) {
        position.t = 1.0 - position.t;
    }
    Inkscape::NodePath::Node *n = sp_nodepath_line_add_node(e, position.t);
    sp_nodepath_node_select(n, FALSE, FALSE);

    /* fixme: adjust ? */
    sp_nodepath_ensure_ctrls(nodepath);

    update_repr(nodepath);

    sp_nodepath_update_statusbar(nodepath);
}

/*
 * Adjusts a segment so that t moves by a certain delta for dragging
 * converts lines to curves
 *
 * method and idea borrowed from Simon Budig  <simon@gimp.org> and the GIMP
 * cf. app/vectors/gimpbezierstroke.c, gimp_bezier_stroke_point_move_relative()
 */
void
sp_nodepath_curve_drag(Inkscape::NodePath::Node * e, double t, NR::Point delta, char * key) 
{
    /* feel good is an arbitrary parameter that distributes the delta between handles
     * if t of the drag point is less than 1/6 distance form the endpoint only
     * the corresponding hadle is adjusted. This matches the behavior in GIMP
     */
    double feel_good;
    if (t <= 1.0 / 6.0)
        feel_good = 0;
    else if (t <= 0.5)
        feel_good = (pow((6 * t - 1) / 2.0, 3)) / 2;
    else if (t <= 5.0 / 6.0)
        feel_good = (1 - pow((6 * (1-t) - 1) / 2.0, 3)) / 2 + 0.5;
    else
        feel_good = 1;
    
    //if we're dragging a line convert it to a curve
    if (sp_node_path_code_from_side(e, sp_node_get_side(e, -1))==NR_LINETO) {
        sp_nodepath_set_line_type(e, NR_CURVETO);
    }

    NR::Point offsetcoord0 = ((1-feel_good)/(3*t*(1-t)*(1-t))) * delta;
    NR::Point offsetcoord1 = (feel_good/(3*t*t*(1-t))) * delta;
    e->p.other->n.pos += offsetcoord0;
    e->p.pos += offsetcoord1;

    // adjust controls of adjacent segments where necessary
    sp_node_adjust_knot(e,1);
    sp_node_adjust_knot(e->p.other,-1);

    sp_nodepath_ensure_ctrls(e->subpath->nodepath);

    update_repr_keyed(e->subpath->nodepath, key);

    sp_nodepath_update_statusbar(e->subpath->nodepath);
}


/**
 * Call sp_nodepath_break() for all selected segments.
 */
void sp_node_selected_break()
{
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) return;

    GList *temp = NULL;
    for (GList *l = nodepath->selected; l != NULL; l = l->next) {
       Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
       Inkscape::NodePath::Node *nn = sp_nodepath_node_break(n);
        if (nn == NULL) continue; // no break, no new node
        temp = g_list_prepend(temp, nn);
    }

    if (temp) {
        sp_nodepath_deselect(nodepath);
    }
    for (GList *l = temp; l != NULL; l = l->next) {
        sp_nodepath_node_select((Inkscape::NodePath::Node *) l->data, TRUE, TRUE);
    }

    sp_nodepath_ensure_ctrls(nodepath);

    update_repr(nodepath);
}

/**
 * Duplicate the selected node(s).
 */
void sp_node_selected_duplicate()
{
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) {
        return;
    }

    GList *temp = NULL;
    for (GList *l = nodepath->selected; l != NULL; l = l->next) {
       Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
       Inkscape::NodePath::Node *nn = sp_nodepath_node_duplicate(n);
        if (nn == NULL) continue; // could not duplicate
        temp = g_list_prepend(temp, nn);
    }

    if (temp) {
        sp_nodepath_deselect(nodepath);
    }
    for (GList *l = temp; l != NULL; l = l->next) {
        sp_nodepath_node_select((Inkscape::NodePath::Node *) l->data, TRUE, TRUE);
    }

    sp_nodepath_ensure_ctrls(nodepath);

    update_repr(nodepath);
}

/**
 *  Join two nodes by merging them into one.
 */
void sp_node_selected_join()
{
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) return; // there's no nodepath when editing rects, stars, spirals or ellipses

    if (g_list_length(nodepath->selected) != 2) {
        nodepath->desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("To join, you must have <b>two endnodes</b> selected."));
        return;
    }

   Inkscape::NodePath::Node *a = (Inkscape::NodePath::Node *) nodepath->selected->data;
   Inkscape::NodePath::Node *b = (Inkscape::NodePath::Node *) nodepath->selected->next->data;

    g_assert(a != b);
    g_assert(a->p.other || a->n.other);
    g_assert(b->p.other || b->n.other);

    if (((a->subpath->closed) || (b->subpath->closed)) || (a->p.other && a->n.other) || (b->p.other && b->n.other)) {
        nodepath->desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("To join, you must have <b>two endnodes</b> selected."));
        return;
    }

    /* a and b are endpoints */

    NR::Point c = (a->pos + b->pos) / 2;

    if (a->subpath == b->subpath) {
       Inkscape::NodePath::SubPath *sp = a->subpath;
        sp_nodepath_subpath_close(sp);

        sp_nodepath_ensure_ctrls(sp->nodepath);

        update_repr(nodepath);

        return;
    }

    /* a and b are separate subpaths */
   Inkscape::NodePath::SubPath *sa = a->subpath;
   Inkscape::NodePath::SubPath *sb = b->subpath;
    NR::Point p;
   Inkscape::NodePath::Node *n;
    NRPathcode code;
    if (a == sa->first) {
        p = sa->first->n.pos;
        code = (NRPathcode)sa->first->n.other->code;
       Inkscape::NodePath::SubPath *t = sp_nodepath_subpath_new(sa->nodepath);
        n = sa->last;
        sp_nodepath_node_new(t, NULL,Inkscape::NodePath::NODE_CUSP, NR_MOVETO, &n->n.pos, &n->pos, &n->p.pos);
        n = n->p.other;
        while (n) {
            sp_nodepath_node_new(t, NULL, (Inkscape::NodePath::NodeType)n->type, (NRPathcode)n->n.other->code, &n->n.pos, &n->pos, &n->p.pos);
            n = n->p.other;
            if (n == sa->first) n = NULL;
        }
        sp_nodepath_subpath_destroy(sa);
        sa = t;
    } else if (a == sa->last) {
        p = sa->last->p.pos;
        code = (NRPathcode)sa->last->code;
        sp_nodepath_node_destroy(sa->last);
    } else {
        code = NR_END;
        g_assert_not_reached();
    }

    if (b == sb->first) {
        sp_nodepath_node_new(sa, NULL,Inkscape::NodePath::NODE_CUSP, code, &p, &c, &sb->first->n.pos);
        for (n = sb->first->n.other; n != NULL; n = n->n.other) {
            sp_nodepath_node_new(sa, NULL, (Inkscape::NodePath::NodeType)n->type, (NRPathcode)n->code, &n->p.pos, &n->pos, &n->n.pos);
        }
    } else if (b == sb->last) {
        sp_nodepath_node_new(sa, NULL,Inkscape::NodePath::NODE_CUSP, code, &p, &c, &sb->last->p.pos);
        for (n = sb->last->p.other; n != NULL; n = n->p.other) {
            sp_nodepath_node_new(sa, NULL, (Inkscape::NodePath::NodeType)n->type, (NRPathcode)n->n.other->code, &n->n.pos, &n->pos, &n->p.pos);
        }
    } else {
        g_assert_not_reached();
    }
    /* and now destroy sb */

    sp_nodepath_subpath_destroy(sb);

    sp_nodepath_ensure_ctrls(sa->nodepath);

    update_repr(nodepath);

    sp_nodepath_update_statusbar(nodepath);
}

/**
 *  Join two nodes by adding a segment between them.
 */
void sp_node_selected_join_segment()
{
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) return; // there's no nodepath when editing rects, stars, spirals or ellipses

    if (g_list_length(nodepath->selected) != 2) {
        nodepath->desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("To join, you must have <b>two endnodes</b> selected."));
        return;
    }

   Inkscape::NodePath::Node *a = (Inkscape::NodePath::Node *) nodepath->selected->data;
   Inkscape::NodePath::Node *b = (Inkscape::NodePath::Node *) nodepath->selected->next->data;

    g_assert(a != b);
    g_assert(a->p.other || a->n.other);
    g_assert(b->p.other || b->n.other);

    if (((a->subpath->closed) || (b->subpath->closed)) || (a->p.other && a->n.other) || (b->p.other && b->n.other)) {
        nodepath->desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("To join, you must have <b>two endnodes</b> selected."));
        return;
    }

    if (a->subpath == b->subpath) {
       Inkscape::NodePath::SubPath *sp = a->subpath;

        /*similar to sp_nodepath_subpath_close(sp), without the node destruction*/
        sp->closed = TRUE;

        sp->first->p.other = sp->last;
        sp->last->n.other  = sp->first;

        sp_node_control_mirror_p_to_n(sp->last);
        sp_node_control_mirror_n_to_p(sp->first);

        sp->first->code = sp->last->code;
        sp->first       = sp->last;

        sp_nodepath_ensure_ctrls(sp->nodepath);

        update_repr(nodepath);

        return;
    }

    /* a and b are separate subpaths */
   Inkscape::NodePath::SubPath *sa = a->subpath;
   Inkscape::NodePath::SubPath *sb = b->subpath;

   Inkscape::NodePath::Node *n;
    NR::Point p;
    NRPathcode code;
    if (a == sa->first) {
        code = (NRPathcode) sa->first->n.other->code;
       Inkscape::NodePath::SubPath *t = sp_nodepath_subpath_new(sa->nodepath);
        n = sa->last;
        sp_nodepath_node_new(t, NULL,Inkscape::NodePath::NODE_CUSP, NR_MOVETO, &n->n.pos, &n->pos, &n->p.pos);
        for (n = n->p.other; n != NULL; n = n->p.other) {
            sp_nodepath_node_new(t, NULL, (Inkscape::NodePath::NodeType)n->type, (NRPathcode)n->n.other->code, &n->n.pos, &n->pos, &n->p.pos);
        }
        sp_nodepath_subpath_destroy(sa);
        sa = t;
    } else if (a == sa->last) {
        code = (NRPathcode)sa->last->code;
    } else {
        code = NR_END;
        g_assert_not_reached();
    }

    if (b == sb->first) {
        n = sb->first;
        sp_node_control_mirror_p_to_n(sa->last);
        sp_nodepath_node_new(sa, NULL,Inkscape::NodePath::NODE_CUSP, code, &n->p.pos, &n->pos, &n->n.pos);
        sp_node_control_mirror_n_to_p(sa->last);
        for (n = n->n.other; n != NULL; n = n->n.other) {
            sp_nodepath_node_new(sa, NULL, (Inkscape::NodePath::NodeType)n->type, (NRPathcode)n->code, &n->p.pos, &n->pos, &n->n.pos);
        }
    } else if (b == sb->last) {
        n = sb->last;
        sp_node_control_mirror_p_to_n(sa->last);
        sp_nodepath_node_new(sa, NULL,Inkscape::NodePath::NODE_CUSP, code, &p, &n->pos, &n->p.pos);
        sp_node_control_mirror_n_to_p(sa->last);
        for (n = n->p.other; n != NULL; n = n->p.other) {
            sp_nodepath_node_new(sa, NULL, (Inkscape::NodePath::NodeType)n->type, (NRPathcode)n->n.other->code, &n->n.pos, &n->pos, &n->p.pos);
        }
    } else {
        g_assert_not_reached();
    }
    /* and now destroy sb */

    sp_nodepath_subpath_destroy(sb);

    sp_nodepath_ensure_ctrls(sa->nodepath);

    update_repr(nodepath);
}

/**
 * Delete one or more selected nodes.
 */
void sp_node_selected_delete()
{
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) return;
    if (!nodepath->selected) return;

    /** \todo fixme: do it the right way */
    while (nodepath->selected) {
       Inkscape::NodePath::Node *node = (Inkscape::NodePath::Node *) nodepath->selected->data;
        sp_nodepath_node_destroy(node);
    }


    //clean up the nodepath (such as for trivial subpaths)
    sp_nodepath_cleanup(nodepath);

    sp_nodepath_ensure_ctrls(nodepath);

    // if the entire nodepath is removed, delete the selected object.
    if (nodepath->subpaths == NULL ||
        sp_nodepath_get_node_count(nodepath) < 2) {
        SPDocument *document = SP_DT_DOCUMENT (nodepath->desktop);
        sp_nodepath_destroy(nodepath);
        sp_selection_delete();
        sp_document_done (document);
        return;
    }

    update_repr(nodepath);

    sp_nodepath_update_statusbar(nodepath);
}

/**
 * Delete one or more segments between two selected nodes.
 * This is the code for 'split'.
 */
void
sp_node_selected_delete_segment(void)
{
   Inkscape::NodePath::Node *start, *end;     //Start , end nodes.  not inclusive
   Inkscape::NodePath::Node *curr, *next;     //Iterators

    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) return; // there's no nodepath when editing rects, stars, spirals or ellipses

    if (g_list_length(nodepath->selected) != 2) {
        nodepath->desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE,
                                                 _("Select <b>two non-endpoint nodes</b> on a path between which to delete segments."));
        return;
    }

    //Selected nodes, not inclusive
   Inkscape::NodePath::Node *a = (Inkscape::NodePath::Node *) nodepath->selected->data;
   Inkscape::NodePath::Node *b = (Inkscape::NodePath::Node *) nodepath->selected->next->data;

    if ( ( a==b)                       ||  //same node
         (a->subpath  != b->subpath )  ||  //not the same path
         (!a->p.other || !a->n.other)  ||  //one of a's sides does not have a segment
         (!b->p.other || !b->n.other) )    //one of b's sides does not have a segment
    {
        nodepath->desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE,
                                                 _("Select <b>two non-endpoint nodes</b> on a path between which to delete segments."));
        return;
    }

    //###########################################
    //# BEGIN EDITS
    //###########################################
    //##################################
    //# CLOSED PATH
    //##################################
    if (a->subpath->closed) {


        gboolean reversed = FALSE;

        //Since we can go in a circle, we need to find the shorter distance.
        //  a->b or b->a
        start = end = NULL;
        int distance    = 0;
        int minDistance = 0;
        for (curr = a->n.other ; curr && curr!=a ; curr=curr->n.other) {
            if (curr==b) {
                //printf("a to b:%d\n", distance);
                start = a;//go from a to b
                end   = b;
                minDistance = distance;
                //printf("A to B :\n");
                break;
            }
            distance++;
        }

        //try again, the other direction
        distance = 0;
        for (curr = b->n.other ; curr && curr!=b ; curr=curr->n.other) {
            if (curr==a) {
                //printf("b to a:%d\n", distance);
                if (distance < minDistance) {
                    start    = b;  //we go from b to a
                    end      = a;
                    reversed = TRUE;
                    //printf("B to A\n");
                }
                break;
            }
            distance++;
        }


        //Copy everything from 'end' to 'start' to a new subpath
       Inkscape::NodePath::SubPath *t = sp_nodepath_subpath_new(nodepath);
        for (curr=end ; curr ; curr=curr->n.other) {
            NRPathcode code = (NRPathcode) curr->code;
            if (curr == end)
                code = NR_MOVETO;
            sp_nodepath_node_new(t, NULL,
                                 (Inkscape::NodePath::NodeType)curr->type, code,
                                 &curr->p.pos, &curr->pos, &curr->n.pos);
            if (curr == start)
                break;
        }
        sp_nodepath_subpath_destroy(a->subpath);


    }



    //##################################
    //# OPEN PATH
    //##################################
    else {

        //We need to get the direction of the list between A and B
        //Can we walk from a to b?
        start = end = NULL;
        for (curr = a->n.other ; curr && curr!=a ; curr=curr->n.other) {
            if (curr==b) {
                start = a;  //did it!  we go from a to b
                end   = b;
                //printf("A to B\n");
                break;
            }
        }
        if (!start) {//didn't work?  let's try the other direction
            for (curr = b->n.other ; curr && curr!=b ; curr=curr->n.other) {
                if (curr==a) {
                    start = b;  //did it!  we go from b to a
                    end   = a;
                    //printf("B to A\n");
                    break;
                }
            }
        }
        if (!start) {
            nodepath->desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE,
                                                     _("Cannot find path between nodes."));
            return;
        }



        //Copy everything after 'end' to a new subpath
       Inkscape::NodePath::SubPath *t = sp_nodepath_subpath_new(nodepath);
        for (curr=end ; curr ; curr=curr->n.other) {
            sp_nodepath_node_new(t, NULL, (Inkscape::NodePath::NodeType)curr->type, (NRPathcode)curr->code,
                                 &curr->p.pos, &curr->pos, &curr->n.pos);
        }

        //Now let us do our deletion.  Since the tail has been saved, go all the way to the end of the list
        for (curr = start->n.other ; curr  ; curr=next) {
            next = curr->n.other;
            sp_nodepath_node_destroy(curr);
        }

    }
    //###########################################
    //# END EDITS
    //###########################################

    //clean up the nodepath (such as for trivial subpaths)
    sp_nodepath_cleanup(nodepath);

    sp_nodepath_ensure_ctrls(nodepath);

    update_repr(nodepath);

    // if the entire nodepath is removed, delete the selected object.
    if (nodepath->subpaths == NULL ||
        sp_nodepath_get_node_count(nodepath) < 2) {
        sp_nodepath_destroy(nodepath);
        sp_selection_delete();
        return;
    }

    sp_nodepath_update_statusbar(nodepath);
}

/**
 * Call sp_nodepath_set_line() for all selected segments.
 */
void
sp_node_selected_set_line_type(NRPathcode code)
{
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (nodepath == NULL) return;

    for (GList *l = nodepath->selected; l != NULL; l = l->next) {
       Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
        g_assert(n->selected);
        if (n->p.other && n->p.other->selected) {
            sp_nodepath_set_line_type(n, code);
        }
    }

    update_repr(nodepath);
}

/**
 * Call sp_nodepath_convert_node_type() for all selected nodes.
 */
void
sp_node_selected_set_type(Inkscape::NodePath::NodeType type)
{
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (nodepath == NULL) return;

    for (GList *l = nodepath->selected; l != NULL; l = l->next) {
        sp_nodepath_convert_node_type((Inkscape::NodePath::Node *) l->data, type);
    }

    update_repr(nodepath);
}

/**
 * Change select status of node, update its own and neighbour handles.
 */
static void sp_node_set_selected(Inkscape::NodePath::Node *node, gboolean selected)
{
    node->selected = selected;

    if (selected) {
        g_object_set(G_OBJECT(node->knot),
                     "fill", NODE_FILL_SEL,
                     "fill_mouseover", NODE_FILL_SEL_HI,
                     "stroke", NODE_STROKE_SEL,
                     "stroke_mouseover", NODE_STROKE_SEL_HI,
                     NULL);
    } else {
        g_object_set(G_OBJECT(node->knot),
                     "fill", NODE_FILL,
                     "fill_mouseover", NODE_FILL_HI,
                     "stroke", NODE_STROKE,
                     "stroke_mouseover", NODE_STROKE_HI,
                     NULL);
    }

    sp_node_ensure_ctrls(node);
    if (node->n.other) sp_node_ensure_ctrls(node->n.other);
    if (node->p.other) sp_node_ensure_ctrls(node->p.other);
}

/**
\brief Select a node
\param node     The node to select
\param incremental   If true, add to selection, otherwise deselect others
\param override   If true, always select this node, otherwise toggle selected status
*/
static void sp_nodepath_node_select(Inkscape::NodePath::Node *node, gboolean incremental, gboolean override)
{
    Inkscape::NodePath::Path *nodepath = node->subpath->nodepath;

    if (incremental) {
        if (override) {
            if (!g_list_find(nodepath->selected, node)) {
                nodepath->selected = g_list_append(nodepath->selected, node);
            }
            sp_node_set_selected(node, TRUE);
        } else { // toggle
            if (node->selected) {
                g_assert(g_list_find(nodepath->selected, node));
                nodepath->selected = g_list_remove(nodepath->selected, node);
            } else {
                g_assert(!g_list_find(nodepath->selected, node));
                nodepath->selected = g_list_append(nodepath->selected, node);
            }
            sp_node_set_selected(node, !node->selected);
        }
    } else {
        sp_nodepath_deselect(nodepath);
        nodepath->selected = g_list_append(nodepath->selected, node);
        sp_node_set_selected(node, TRUE);
    }

    sp_nodepath_update_statusbar(nodepath);
}


/**
\brief Deselect all nodes in the nodepath
*/
void
sp_nodepath_deselect(Inkscape::NodePath::Path *nodepath)
{
    if (!nodepath) return; // there's no nodepath when editing rects, stars, spirals or ellipses

    while (nodepath->selected) {
        sp_node_set_selected((Inkscape::NodePath::Node *) nodepath->selected->data, FALSE);
        nodepath->selected = g_list_remove(nodepath->selected, nodepath->selected->data);
    }
    sp_nodepath_update_statusbar(nodepath);
}

/**
\brief Select all nodes in the nodepath
*/
void
sp_nodepath_select_all(Inkscape::NodePath::Path *nodepath)
{
    if (!nodepath) return;

    for (GList *spl = nodepath->subpaths; spl != NULL; spl = spl->next) {
       Inkscape::NodePath::SubPath *subpath = (Inkscape::NodePath::SubPath *) spl->data;
        for (GList *nl = subpath->nodes; nl != NULL; nl = nl->next) {
           Inkscape::NodePath::Node *node = (Inkscape::NodePath::Node *) nl->data;
            sp_nodepath_node_select(node, TRUE, TRUE);
        }
    }
}

/** 
 * If nothing selected, does the same as sp_nodepath_select_all(); 
 * otherwise selects all nodes in all subpaths that have selected nodes 
 * (i.e., similar to "select all in layer", with the "selected" subpaths 
 * being treated as "layers" in the path).
 */
void
sp_nodepath_select_all_from_subpath(Inkscape::NodePath::Path *nodepath)
{
    if (!nodepath) return;

    if (g_list_length (nodepath->selected) == 0) {
        sp_nodepath_select_all (nodepath);
        return;
    }

    GList *copy = g_list_copy (nodepath->selected); // copy initial selection so that selecting in the loop does not affect us

    for (GList *l = copy; l != NULL; l = l->next) {
        Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
        Inkscape::NodePath::SubPath *subpath = n->subpath;
        for (GList *nl = subpath->nodes; nl != NULL; nl = nl->next) {
            Inkscape::NodePath::Node *node = (Inkscape::NodePath::Node *) nl->data;
            sp_nodepath_node_select(node, TRUE, TRUE);
        }
    }

    g_list_free (copy);
}

/**
 * \brief Select the node after the last selected; if none is selected, 
 * select the first within path.
 */
void sp_nodepath_select_next(Inkscape::NodePath::Path *nodepath)
{
    if (!nodepath) return; // there's no nodepath when editing rects, stars, spirals or ellipses

   Inkscape::NodePath::Node *last = NULL;
    if (nodepath->selected) {
        for (GList *spl = nodepath->subpaths; spl != NULL; spl = spl->next) {
           Inkscape::NodePath::SubPath *subpath, *subpath_next;
            subpath = (Inkscape::NodePath::SubPath *) spl->data;
            for (GList *nl = subpath->nodes; nl != NULL; nl = nl->next) {
               Inkscape::NodePath::Node *node = (Inkscape::NodePath::Node *) nl->data;
                if (node->selected) {
                    if (node->n.other == (Inkscape::NodePath::Node *) subpath->last) {
                        if (node->n.other == (Inkscape::NodePath::Node *) subpath->first) { // closed subpath
                            if (spl->next) { // there's a next subpath
                                subpath_next = (Inkscape::NodePath::SubPath *) spl->next->data;
                                last = subpath_next->first;
                            } else if (spl->prev) { // there's a previous subpath
                                last = NULL; // to be set later to the first node of first subpath
                            } else {
                                last = node->n.other;
                            }
                        } else {
                            last = node->n.other;
                        }
                    } else {
                        if (node->n.other) {
                            last = node->n.other;
                        } else {
                            if (spl->next) { // there's a next subpath
                                subpath_next = (Inkscape::NodePath::SubPath *) spl->next->data;
                                last = subpath_next->first;
                            } else if (spl->prev) { // there's a previous subpath
                                last = NULL; // to be set later to the first node of first subpath
                            } else {
                                last = (Inkscape::NodePath::Node *) subpath->first;
                            }
                        }
                    }
                }
            }
        }
        sp_nodepath_deselect(nodepath);
    }

    if (last) { // there's at least one more node after selected
        sp_nodepath_node_select((Inkscape::NodePath::Node *) last, TRUE, TRUE);
    } else { // no more nodes, select the first one in first subpath
       Inkscape::NodePath::SubPath *subpath = (Inkscape::NodePath::SubPath *) nodepath->subpaths->data;
        sp_nodepath_node_select((Inkscape::NodePath::Node *) subpath->first, TRUE, TRUE);
    }
}

/**
 * \brief Select the node before the first selected; if none is selected, 
 * select the last within path
 */
void sp_nodepath_select_prev(Inkscape::NodePath::Path *nodepath)
{
    if (!nodepath) return; // there's no nodepath when editing rects, stars, spirals or ellipses

   Inkscape::NodePath::Node *last = NULL;
    if (nodepath->selected) {
        for (GList *spl = g_list_last(nodepath->subpaths); spl != NULL; spl = spl->prev) {
           Inkscape::NodePath::SubPath *subpath = (Inkscape::NodePath::SubPath *) spl->data;
            for (GList *nl = g_list_last(subpath->nodes); nl != NULL; nl = nl->prev) {
               Inkscape::NodePath::Node *node = (Inkscape::NodePath::Node *) nl->data;
                if (node->selected) {
                    if (node->p.other == (Inkscape::NodePath::Node *) subpath->first) {
                        if (node->p.other == (Inkscape::NodePath::Node *) subpath->last) { // closed subpath
                            if (spl->prev) { // there's a prev subpath
                               Inkscape::NodePath::SubPath *subpath_prev = (Inkscape::NodePath::SubPath *) spl->prev->data;
                                last = subpath_prev->last;
                            } else if (spl->next) { // there's a next subpath
                                last = NULL; // to be set later to the last node of last subpath
                            } else {
                                last = node->p.other;
                            }
                        } else {
                            last = node->p.other;
                        }
                    } else {
                        if (node->p.other) {
                            last = node->p.other;
                        } else {
                            if (spl->prev) { // there's a prev subpath
                               Inkscape::NodePath::SubPath *subpath_prev = (Inkscape::NodePath::SubPath *) spl->prev->data;
                                last = subpath_prev->last;
                            } else if (spl->next) { // there's a next subpath
                                last = NULL; // to be set later to the last node of last subpath
                            } else {
                                last = (Inkscape::NodePath::Node *) subpath->last;
                            }
                        }
                    }
                }
            }
        }
        sp_nodepath_deselect(nodepath);
    }

    if (last) { // there's at least one more node before selected
        sp_nodepath_node_select((Inkscape::NodePath::Node *) last, TRUE, TRUE);
    } else { // no more nodes, select the last one in last subpath
        GList *spl = g_list_last(nodepath->subpaths);
       Inkscape::NodePath::SubPath *subpath = (Inkscape::NodePath::SubPath *) spl->data;
        sp_nodepath_node_select((Inkscape::NodePath::Node *) subpath->last, TRUE, TRUE);
    }
}

/**
 * \brief Select all nodes that are within the rectangle.
 */
void sp_nodepath_select_rect(Inkscape::NodePath::Path *nodepath, NRRect *b, gboolean incremental)
{
    if (!incremental) {
        sp_nodepath_deselect(nodepath);
    }

    for (GList *spl = nodepath->subpaths; spl != NULL; spl = spl->next) {
       Inkscape::NodePath::SubPath *subpath = (Inkscape::NodePath::SubPath *) spl->data;
        for (GList *nl = subpath->nodes; nl != NULL; nl = nl->next) {
           Inkscape::NodePath::Node *node = (Inkscape::NodePath::Node *) nl->data;

            NR::Point p = node->pos;

            if ((p[NR::X] > b->x0) && (p[NR::X] < b->x1) && (p[NR::Y] > b->y0) && (p[NR::Y] < b->y1)) {
                sp_nodepath_node_select(node, TRUE, FALSE);
            }
        }
    }
}

/**
\brief  Saves selected nodes in a nodepath into a list containing integer positions of all selected nodes
*/
GList *save_nodepath_selection(Inkscape::NodePath::Path *nodepath)
{
    if (!nodepath->selected) {
        return NULL;
    }

    GList *r = NULL;
    guint i = 0;
    for (GList *spl = nodepath->subpaths; spl != NULL; spl = spl->next) {
       Inkscape::NodePath::SubPath *subpath = (Inkscape::NodePath::SubPath *) spl->data;
        for (GList *nl = subpath->nodes; nl != NULL; nl = nl->next) {
           Inkscape::NodePath::Node *node = (Inkscape::NodePath::Node *) nl->data;
            i++;
            if (node->selected) {
                r = g_list_append(r, GINT_TO_POINTER(i));
            }
        }
    }
    return r;
}

/**
\brief  Restores selection by selecting nodes whose positions are in the list
*/
void restore_nodepath_selection(Inkscape::NodePath::Path *nodepath, GList *r)
{
    sp_nodepath_deselect(nodepath);

    guint i = 0;
    for (GList *spl = nodepath->subpaths; spl != NULL; spl = spl->next) {
       Inkscape::NodePath::SubPath *subpath = (Inkscape::NodePath::SubPath *) spl->data;
        for (GList *nl = subpath->nodes; nl != NULL; nl = nl->next) {
           Inkscape::NodePath::Node *node = (Inkscape::NodePath::Node *) nl->data;
            i++;
            if (g_list_find(r, GINT_TO_POINTER(i))) {
                sp_nodepath_node_select(node, TRUE, TRUE);
            }
        }
    }

}

/**
\brief Adjusts control point according to node type and line code.
*/
static void sp_node_adjust_knot(Inkscape::NodePath::Node *node, gint which_adjust)
{
    double len, otherlen, linelen;

    g_assert(node);

   Inkscape::NodePath::NodeSide *me = sp_node_get_side(node, which_adjust);
   Inkscape::NodePath::NodeSide *other = sp_node_opposite_side(node, me);

    /** \todo fixme: */
    if (me->other == NULL) return;
    if (other->other == NULL) return;

    /* I have line */

    NRPathcode mecode, ocode;
    if (which_adjust == 1) {
        mecode = (NRPathcode)me->other->code;
        ocode = (NRPathcode)node->code;
    } else {
        mecode = (NRPathcode)node->code;
        ocode = (NRPathcode)other->other->code;
    }

    if (mecode == NR_LINETO) return;

    /* I am curve */

    if (other->other == NULL) return;

    /* Other has line */

    if (node->type == Inkscape::NodePath::NODE_CUSP) return;

    NR::Point delta;
    if (ocode == NR_LINETO) {
        /* other is lineto, we are either smooth or symm */
       Inkscape::NodePath::Node *othernode = other->other;
        len = NR::L2(me->pos - node->pos);
        delta = node->pos - othernode->pos;
        linelen = NR::L2(delta);
        if (linelen < 1e-18) return;

        me->pos = node->pos + (len / linelen)*delta;
        sp_knot_set_position(me->knot, &me->pos, 0);

        sp_node_ensure_ctrls(node);
        return;
    }

    if (node->type == Inkscape::NodePath::NODE_SYMM) {

        me->pos = 2 * node->pos - other->pos;
        sp_knot_set_position(me->knot, &me->pos, 0);

        sp_node_ensure_ctrls(node);
        return;
    }

    /* We are smooth */

    len = NR::L2(me->pos - node->pos);
    delta = other->pos - node->pos;
    otherlen = NR::L2(delta);
    if (otherlen < 1e-18) return;

    me->pos = node->pos - (len / otherlen) * delta;
    sp_knot_set_position(me->knot, &me->pos, 0);

    sp_node_ensure_ctrls(node);
}

/**
 \brief Adjusts control point according to node type and line code
 */
static void sp_node_adjust_knots(Inkscape::NodePath::Node *node)
{
    g_assert(node);

    if (node->type == Inkscape::NodePath::NODE_CUSP) return;

    /* we are either smooth or symm */

    if (node->p.other == NULL) return;

    if (node->n.other == NULL) return;

    if (node->code == NR_LINETO) {
        if (node->n.other->code == NR_LINETO) return;
        sp_node_adjust_knot(node, 1);
        sp_node_ensure_ctrls(node);
        return;
    }

    if (node->n.other->code == NR_LINETO) {
        if (node->code == NR_LINETO) return;
        sp_node_adjust_knot(node, -1);
        sp_node_ensure_ctrls(node);
        return;
    }

    /* both are curves */

    NR::Point const delta( node->n.pos - node->p.pos );

    if (node->type == Inkscape::NodePath::NODE_SYMM) {
        node->p.pos = node->pos - delta / 2;
        node->n.pos = node->pos + delta / 2;
        sp_node_ensure_ctrls(node);
        return;
    }

    /* We are smooth */

    double plen = NR::L2(node->p.pos - node->pos);
    if (plen < 1e-18) return;
    double nlen = NR::L2(node->n.pos - node->pos);
    if (nlen < 1e-18) return;
    node->p.pos = node->pos - (plen / (plen + nlen)) * delta;
    node->n.pos = node->pos + (nlen / (plen + nlen)) * delta;
    sp_node_ensure_ctrls(node);
}

/**
 * Knot events handler callback.
 */
static gboolean node_event(SPKnot *knot, GdkEvent *event,Inkscape::NodePath::Node *n)
{
    gboolean ret = FALSE;
    switch (event->type) {
        case GDK_ENTER_NOTIFY:
            active_node = n;
            break;
        case GDK_LEAVE_NOTIFY:
            active_node = NULL;
            break;
        case GDK_KEY_PRESS:
            switch (get_group0_keyval (&event->key)) {
                case GDK_space:
                    if (event->key.state & GDK_BUTTON1_MASK) {
                        Inkscape::NodePath::Path *nodepath = n->subpath->nodepath;
                        stamp_repr(nodepath);
                        ret = TRUE;
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    return ret;
}

/**
 * Handle keypress on node; directly called.
 */
gboolean node_key(GdkEvent *event)
{
    Inkscape::NodePath::Path *np;

    // there is no way to verify nodes so set active_node to nil when deleting!!
    if (active_node == NULL) return FALSE;

    if ((event->type == GDK_KEY_PRESS) && !(event->key.state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK))) {
        gint ret = FALSE;
        switch (get_group0_keyval (&event->key)) {
            /// \todo FIXME: this does not seem to work, the keys are stolen by tool contexts!
            case GDK_BackSpace:
                np = active_node->subpath->nodepath;
                sp_nodepath_node_destroy(active_node);
                update_repr(np);
                active_node = NULL;
                ret = TRUE;
                break;
            case GDK_c:
                sp_nodepath_set_node_type(active_node,Inkscape::NodePath::NODE_CUSP);
                ret = TRUE;
                break;
            case GDK_s:
                sp_nodepath_set_node_type(active_node,Inkscape::NodePath::NODE_SMOOTH);
                ret = TRUE;
                break;
            case GDK_y:
                sp_nodepath_set_node_type(active_node,Inkscape::NodePath::NODE_SYMM);
                ret = TRUE;
                break;
            case GDK_b:
                sp_nodepath_node_break(active_node);
                ret = TRUE;
                break;
        }
        return ret;
    }
    return FALSE;
}

/**
 * Mouseclick on node callback.
 */
static void node_clicked(SPKnot *knot, guint state, gpointer data)
{
   Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) data;

    if (state & GDK_CONTROL_MASK) {
        Inkscape::NodePath::Path *nodepath = n->subpath->nodepath;

        if (!(state & GDK_MOD1_MASK)) { // ctrl+click: toggle node type
            if (n->type == Inkscape::NodePath::NODE_CUSP) {
                sp_nodepath_convert_node_type (n,Inkscape::NodePath::NODE_SMOOTH);
            } else if (n->type == Inkscape::NodePath::NODE_SMOOTH) {
                sp_nodepath_convert_node_type (n,Inkscape::NodePath::NODE_SYMM);
            } else {
                sp_nodepath_convert_node_type (n,Inkscape::NodePath::NODE_CUSP);
            }
            update_repr(nodepath);
            sp_nodepath_update_statusbar(nodepath);

        } else { //ctrl+alt+click: delete node
            sp_nodepath_node_destroy(n);
            //clean up the nodepath (such as for trivial subpaths)
            sp_nodepath_cleanup(nodepath);

            // if the entire nodepath is removed, delete the selected object.
            if (nodepath->subpaths == NULL ||
                sp_nodepath_get_node_count(nodepath) < 2) {
                SPDocument *document = SP_DT_DOCUMENT (nodepath->desktop);
                sp_nodepath_destroy(nodepath);
                sp_selection_delete();
                sp_document_done (document);

            } else {
                sp_nodepath_ensure_ctrls(nodepath);
                update_repr(nodepath);
                sp_nodepath_update_statusbar(nodepath);
            }
        }

    } else {
        sp_nodepath_node_select(n, (state & GDK_SHIFT_MASK), FALSE);
    }
}

/**
 * Mouse grabbed node callback.
 */
static void node_grabbed(SPKnot *knot, guint state, gpointer data)
{
   Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) data;

    n->origin = knot->pos;

    if (!n->selected) {
        sp_nodepath_node_select(n, (state & GDK_SHIFT_MASK), FALSE);
    }
}

/**
 * Mouse ungrabbed node callback.
 */
static void node_ungrabbed(SPKnot *knot, guint state, gpointer data)
{
   Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) data;

   n->dragging_out = NULL;

   update_repr(n->subpath->nodepath);
}

/**
 * The point on a line, given by its angle, closest to the given point.
 * \param p  A point.
 * \param a  Angle of the line; it is assumed to go through coordinate origin.
 * \param closest  Pointer to the point struct where the result is stored.
 * \todo FIXME: use dot product perhaps?
 */
static void point_line_closest(NR::Point *p, double a, NR::Point *closest)
{
    if (a == HUGE_VAL) { // vertical
        *closest = NR::Point(0, (*p)[NR::Y]);
    } else {
        (*closest)[NR::X] = ( a * (*p)[NR::Y] + (*p)[NR::X]) / (a*a + 1);
        (*closest)[NR::Y] = a * (*closest)[NR::X];
    }
}

/**
 * Distance from the point to a line given by its angle.
 * \param p  A point.
 * \param a  Angle of the line; it is assumed to go through coordinate origin.
 */
static double point_line_distance(NR::Point *p, double a)
{
    NR::Point c;
    point_line_closest(p, a, &c);
    return sqrt(((*p)[NR::X] - c[NR::X])*((*p)[NR::X] - c[NR::X]) + ((*p)[NR::Y] - c[NR::Y])*((*p)[NR::Y] - c[NR::Y]));
}

/**
 * Callback for node "request" signal.
 * \todo fixme: This goes to "moved" event? (lauris)
 */
static gboolean
node_request(SPKnot *knot, NR::Point *p, guint state, gpointer data)
{
    double yn, xn, yp, xp;
    double an, ap, na, pa;
    double d_an, d_ap, d_na, d_pa;
    gboolean collinear = FALSE;
    NR::Point c;
    NR::Point pr;

   Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) data;

   // If either (Shift and some handle retracted), or (we're already dragging out a handle)
   if (((state & GDK_SHIFT_MASK) && ((n->n.other && n->n.pos == n->pos) || (n->p.other && n->p.pos == n->pos))) || n->dragging_out) { 

       NR::Point mouse = (*p);

       if (!n->dragging_out) {
           // This is the first drag-out event; find out which handle to drag out
           double appr_n = (n->n.other ? NR::L2(n->n.other->pos - n->pos) - NR::L2(n->n.other->pos - (*p)) : -HUGE_VAL);
           double appr_p = (n->p.other ? NR::L2(n->p.other->pos - n->pos) - NR::L2(n->p.other->pos - (*p)) : -HUGE_VAL);

           if (appr_p == -HUGE_VAL && appr_n == -HUGE_VAL) // orphan node?
               return FALSE;

           Inkscape::NodePath::NodeSide *opposite;
           if (appr_p > appr_n) { // closer to p
               n->dragging_out = &n->p;
               opposite = &n->n;
               n->code = NR_CURVETO;
           } else if (appr_p < appr_n) { // closer to n
               n->dragging_out = &n->n;
               opposite = &n->p;
               n->n.other->code = NR_CURVETO;
           } else { // p and n nodes are the same
               if (n->n.pos != n->pos) { // n handle already dragged, drag p
                   n->dragging_out = &n->p;
                   opposite = &n->n;
                   n->code = NR_CURVETO;
               } else if (n->p.pos != n->pos) { // p handle already dragged, drag n
                   n->dragging_out = &n->n;
                   opposite = &n->p;
                   n->n.other->code = NR_CURVETO;
               } else { // find out to which handle of the adjacent node we're closer; note that n->n.other == n->p.other
                   double appr_other_n = (n->n.other ? NR::L2(n->n.other->n.pos - n->pos) - NR::L2(n->n.other->n.pos - (*p)) : -HUGE_VAL);
                   double appr_other_p = (n->n.other ? NR::L2(n->n.other->p.pos - n->pos) - NR::L2(n->n.other->p.pos - (*p)) : -HUGE_VAL);
                   if (appr_other_p > appr_other_n) { // closer to other's p handle
                       n->dragging_out = &n->n;
                       opposite = &n->p;
                       n->n.other->code = NR_CURVETO;
                   } else { // closer to other's n handle
                       n->dragging_out = &n->p;
                       opposite = &n->n;
                       n->code = NR_CURVETO;
                   }
               }
           }

           // if there's another handle, make sure the one we drag out starts parallel to it
           if (opposite->pos != n->pos) {
               mouse = n->pos - NR::L2(mouse - n->pos) * NR::unit_vector(opposite->pos - n->pos);
           }
       }

       // pass this on to the handle-moved callback
       node_ctrl_moved(n->dragging_out->knot, &mouse, state, (gpointer) n);
       sp_node_ensure_ctrls(n);
       return TRUE;
   }

    if (state & GDK_CONTROL_MASK) { // constrained motion

        // calculate relative distances of handles
        // n handle:
        yn = n->n.pos[NR::Y] - n->pos[NR::Y];
        xn = n->n.pos[NR::X] - n->pos[NR::X];
        // if there's no n handle (straight line), see if we can use the direction to the next point on path
        if ((n->n.other && n->n.other->code == NR_LINETO) || fabs(yn) + fabs(xn) < 1e-6) {
            if (n->n.other) { // if there is the next point
                if (L2(n->n.other->p.pos - n->n.other->pos) < 1e-6) // and the next point has no handle either
                    yn = n->n.other->pos[NR::Y] - n->origin[NR::Y]; // use origin because otherwise the direction will change as you drag
                    xn = n->n.other->pos[NR::X] - n->origin[NR::X];
            }
        }
        if (xn < 0) { xn = -xn; yn = -yn; } // limit the angle to between 0 and pi
        if (yn < 0) { xn = -xn; yn = -yn; }

        // p handle:
        yp = n->p.pos[NR::Y] - n->pos[NR::Y];
        xp = n->p.pos[NR::X] - n->pos[NR::X];
        // if there's no p handle (straight line), see if we can use the direction to the prev point on path
        if (n->code == NR_LINETO || fabs(yp) + fabs(xp) < 1e-6) {
            if (n->p.other) {
                if (L2(n->p.other->n.pos - n->p.other->pos) < 1e-6)
                    yp = n->p.other->pos[NR::Y] - n->origin[NR::Y];
                    xp = n->p.other->pos[NR::X] - n->origin[NR::X];
            }
        }
        if (xp < 0) { xp = -xp; yp = -yp; } // limit the angle to between 0 and pi
        if (yp < 0) { xp = -xp; yp = -yp; }

        if (state & GDK_MOD1_MASK && !(xn == 0 && xp == 0)) {
            // sliding on handles, only if at least one of the handles is non-vertical
            // (otherwise it's the same as ctrl+drag anyway)

            // calculate angles of the control handles
            if (xn == 0) {
                if (yn == 0) { // no handle, consider it the continuation of the other one
                    an = 0;
                    collinear = TRUE;
                }
                else an = 0; // vertical; set the angle to horizontal
            } else an = yn/xn;

            if (xp == 0) {
                if (yp == 0) { // no handle, consider it the continuation of the other one
                    ap = an;
                }
                else ap = 0; // vertical; set the angle to horizontal
            } else  ap = yp/xp;

            if (collinear) an = ap;

            // angles of the perpendiculars; HUGE_VAL means vertical
            if (an == 0) na = HUGE_VAL; else na = -1/an;
            if (ap == 0) pa = HUGE_VAL; else pa = -1/ap;

            //g_print("an %g    ap %g\n", an, ap);

            // mouse point relative to the node's original pos
            pr = (*p) - n->origin;

            // distances to the four lines (two handles and two perpendiculars)
            d_an = point_line_distance(&pr, an);
            d_na = point_line_distance(&pr, na);
            d_ap = point_line_distance(&pr, ap);
            d_pa = point_line_distance(&pr, pa);

            // find out which line is the closest, save its closest point in c
            if (d_an <= d_na && d_an <= d_ap && d_an <= d_pa) {
                point_line_closest(&pr, an, &c);
            } else if (d_ap <= d_an && d_ap <= d_na && d_ap <= d_pa) {
                point_line_closest(&pr, ap, &c);
            } else if (d_na <= d_an && d_na <= d_ap && d_na <= d_pa) {
                point_line_closest(&pr, na, &c);
            } else if (d_pa <= d_an && d_pa <= d_ap && d_pa <= d_na) {
                point_line_closest(&pr, pa, &c);
            }

            // move the node to the closest point
            sp_nodepath_selected_nodes_move(n->subpath->nodepath,
                                            n->origin[NR::X] + c[NR::X] - n->pos[NR::X],
                                            n->origin[NR::Y] + c[NR::Y] - n->pos[NR::Y]);

        } else {  // constraining to hor/vert

            if (fabs((*p)[NR::X] - n->origin[NR::X]) > fabs((*p)[NR::Y] - n->origin[NR::Y])) { // snap to hor
                sp_nodepath_selected_nodes_move(n->subpath->nodepath, (*p)[NR::X] - n->pos[NR::X], n->origin[NR::Y] - n->pos[NR::Y]);
            } else { // snap to vert
                sp_nodepath_selected_nodes_move(n->subpath->nodepath, n->origin[NR::X] - n->pos[NR::X], (*p)[NR::Y] - n->pos[NR::Y]);
            }
        }
    } else { // move freely
        sp_nodepath_selected_nodes_move(n->subpath->nodepath,
                                        (*p)[NR::X] - n->pos[NR::X],
                                        (*p)[NR::Y] - n->pos[NR::Y],
                                        (state & GDK_SHIFT_MASK) == 0);
    }

    sp_desktop_scroll_to_point(n->subpath->nodepath->desktop, p);

    return TRUE;
}

/**
 * Node handle clicked callback.
 */
static void node_ctrl_clicked(SPKnot *knot, guint state, gpointer data)
{
   Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) data;

    if (state & GDK_CONTROL_MASK) { // "delete" handle
        if (n->p.knot == knot) {
            n->p.pos = n->pos;
        } else if (n->n.knot == knot) {
            n->n.pos = n->pos;
        }
        sp_node_ensure_ctrls(n);
        Inkscape::NodePath::Path *nodepath = n->subpath->nodepath;
        update_repr(nodepath);
        sp_nodepath_update_statusbar(nodepath);

    } else { // just select or add to selection, depending in Shift
        sp_nodepath_node_select(n, (state & GDK_SHIFT_MASK), FALSE);
    }
}

/**
 * Node handle grabbed callback.
 */
static void node_ctrl_grabbed(SPKnot *knot, guint state, gpointer data)
{
   Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) data;

    if (!n->selected) {
        sp_nodepath_node_select(n, (state & GDK_SHIFT_MASK), FALSE);
    }

    // remember the origin of the control
    if (n->p.knot == knot) {
        n->p.origin = n->p.pos - n->pos;
    } else if (n->n.knot == knot) {
        n->n.origin = n->n.pos - n->pos;
    } else {
        g_assert_not_reached();
    }

}

/**
 * Node handle ungrabbed callback.
 */
static void node_ctrl_ungrabbed(SPKnot *knot, guint state, gpointer data)
{
   Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) data;

    // forget origin and set knot position once more (because it can be wrong now due to restrictions)
    if (n->p.knot == knot) {
        n->p.origin.a = 0;
        sp_knot_set_position(knot, &n->p.pos, state);
    } else if (n->n.knot == knot) {
        n->n.origin.a = 0;
        sp_knot_set_position(knot, &n->n.pos, state);
    } else {
        g_assert_not_reached();
    }

    update_repr(n->subpath->nodepath);
}

/**
 * Node handle "request" signal callback.
 */
static gboolean node_ctrl_request(SPKnot *knot, NR::Point *p, guint state, gpointer data)
{
   Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) data;

   Inkscape::NodePath::NodeSide *me, *opposite;
    gint which;
    if (n->p.knot == knot) {
        me = &n->p;
        opposite = &n->n;
        which = -1;
    } else if (n->n.knot == knot) {
        me = &n->n;
        opposite = &n->p;
        which = 1;
    } else {
        me = opposite = NULL;
        which = 0;
        g_assert_not_reached();
    }

    NRPathcode othercode = sp_node_path_code_from_side(n, opposite);

    if (opposite->other && (n->type !=Inkscape::NodePath::NODE_CUSP) && (othercode == NR_LINETO)) {
        gdouble len, linelen, scal;
        /* We are smooth node adjacent with line */
        NR::Point delta = *p - n->pos;
        len = NR::L2(delta);
       Inkscape::NodePath::Node *othernode = opposite->other;
        NR::Point ndelta = n->pos - othernode->pos;
        linelen = NR::L2(ndelta);
        if ((len > 1e-18) && (linelen > 1e-18)) {
            scal = dot(delta, ndelta) / linelen;
            (*p) = n->pos + (scal / linelen) * ndelta;
        }
        namedview_vector_snap(n->subpath->nodepath->desktop->namedview, Snapper::SNAP_POINT, *p, ndelta);
    } else {
        namedview_free_snap(n->subpath->nodepath->desktop->namedview, Snapper::SNAP_POINT, *p);
    }

    sp_node_adjust_knot(n, -which);

    return FALSE;
}

/**
 * Node handle moved callback.
 */
static void node_ctrl_moved(SPKnot *knot, NR::Point *p, guint state, gpointer data)
{
   Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) data;

   Inkscape::NodePath::NodeSide *me;
   Inkscape::NodePath::NodeSide *other;
    if (n->p.knot == knot) {
        me = &n->p;
        other = &n->n;
    } else if (n->n.knot == knot) {
        me = &n->n;
        other = &n->p;
    } else {
        me = NULL;
        other = NULL;
        g_assert_not_reached();
    }

    // calculate radial coordinates of the grabbed control, other control, and the mouse point
    Radial rme(me->pos - n->pos);
    Radial rother(other->pos - n->pos);
    Radial rnew(*p - n->pos);

    if (state & GDK_CONTROL_MASK && rnew.a != HUGE_VAL) {
        double a_snapped, a_ortho;

        int snaps = prefs_get_int_attribute("options.rotationsnapsperpi", "value", 12);
        /* 0 interpreted as "no snapping". */

        // the closest PI/snaps angle, starting from zero
        a_snapped = floor(rnew.a/(M_PI/snaps) + 0.5) * (M_PI/snaps);
        // the closest PI/2 angle, starting from original angle (i.e. snapping to original, its opposite and perpendiculars)
        a_ortho = me->origin.a + floor((rnew.a - me->origin.a)/(M_PI/2) + 0.5) * (M_PI/2);

        // snap to the closest, or to snapped if ortho does not exist because original control was zero length
        if (me->origin.a == HUGE_VAL || fabs(a_snapped - rnew.a) < fabs(a_ortho - rnew.a))
            rnew.a = a_snapped;
        else
            rnew.a = a_ortho;
    }

    if (state & GDK_MOD1_MASK) {
        // lock handle length
        rnew.r = me->origin.r;
    }

    if (( n->type !=Inkscape::NodePath::NODE_CUSP || (state & GDK_SHIFT_MASK))
        && rme.a != HUGE_VAL && rnew.a != HUGE_VAL && fabs(rme.a - rnew.a) > 0.001) {
        // rotate the other handle correspondingly, if both old and new angles exist and are not the same
        rother.a += rnew.a - rme.a;
        other->pos = NR::Point(rother) + n->pos;
        sp_ctrlline_set_coords(SP_CTRLLINE(other->line), n->pos, other->pos);
        sp_knot_set_position(other->knot, &other->pos, 0);
    }

    me->pos = NR::Point(rnew) + n->pos;
    sp_ctrlline_set_coords(SP_CTRLLINE(me->line), n->pos, me->pos);

    // this is what sp_knot_set_position does, but without emitting the signal:
    // we cannot emit a "moved" signal because we're now processing it
    if (me->knot->item) SP_CTRL(me->knot->item)->moveto(me->pos);

    sp_desktop_set_coordinate_status(knot->desktop, me->pos, 0);

    update_object(n->subpath->nodepath);

    /* status text */
    SPDesktop *desktop = n->subpath->nodepath->desktop;
    if (!desktop) return;
    SPEventContext *ec = desktop->event_context;
    if (!ec) return;
    Inkscape::MessageContext *mc = SP_NODE_CONTEXT (ec)->_node_message_context;
    if (!mc) return;

    double degrees = 180 / M_PI * rnew.a;
    if (degrees > 180) degrees -= 360;
    if (degrees < -180) degrees += 360;

    GString *length = SP_PX_TO_METRIC_STRING(rnew.r, sp_desktop_get_default_metric(desktop));

    mc->setF(Inkscape::NORMAL_MESSAGE,
         _("<b>Node handle</b>: at %0.2f&#176;, length %s; with <b>Ctrl</b> to snap angle; with <b>Alt</b> to lock length; with <b>Shift</b> to rotate both handles"), degrees, length->str);

    g_string_free(length, TRUE);
}

/**
 * Node handle event callback.
 */
static gboolean node_ctrl_event(SPKnot *knot, GdkEvent *event,Inkscape::NodePath::Node *n)
{
    gboolean ret = FALSE;
    switch (event->type) {
        case GDK_KEY_PRESS:
            switch (get_group0_keyval (&event->key)) {
                case GDK_space:
                    if (event->key.state & GDK_BUTTON1_MASK) {
                        Inkscape::NodePath::Path *nodepath = n->subpath->nodepath;
                        stamp_repr(nodepath);
                        ret = TRUE;
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    return ret;
}

static void node_rotate_one_internal(Inkscape::NodePath::Node const &n, gdouble const angle,
                                 Radial &rme, Radial &rother, gboolean const both)
{
    rme.a += angle;
    if ( both
         || ( n.type == Inkscape::NodePath::NODE_SMOOTH )
         || ( n.type == Inkscape::NodePath::NODE_SYMM )  )
    {
        rother.a += angle;
    }
}

static void node_rotate_one_internal_screen(Inkscape::NodePath::Node const &n, gdouble const angle,
                                        Radial &rme, Radial &rother, gboolean const both)
{
    gdouble const norm_angle = angle / SP_DESKTOP_ZOOM(n.subpath->nodepath->desktop);

    gdouble r;
    if ( both
         || ( n.type == Inkscape::NodePath::NODE_SMOOTH )
         || ( n.type == Inkscape::NodePath::NODE_SYMM )  )
    {
        r = MAX(rme.r, rother.r);
    } else {
        r = rme.r;
    }

    gdouble const weird_angle = atan2(norm_angle, r);
/* Bulia says norm_angle is just the visible distance that the
 * object's end must travel on the screen.  Left as 'angle' for want of
 * a better name.*/

    rme.a += weird_angle;
    if ( both
         || ( n.type == Inkscape::NodePath::NODE_SMOOTH )
         || ( n.type == Inkscape::NodePath::NODE_SYMM )  )
    {
        rother.a += weird_angle;
    }
}

/**
 * Rotate one node.
 */
static void node_rotate_one (Inkscape::NodePath::Node *n, gdouble angle, int which, gboolean screen)
{
    Inkscape::NodePath::NodeSide *me, *other;
    bool both = false;

    double xn = n->n.other? n->n.other->pos[NR::X] : n->pos[NR::X];
    double xp = n->p.other? n->p.other->pos[NR::X] : n->pos[NR::X];

    if (!n->n.other) { // if this is an endnode, select its single handle regardless of "which"
        me = &(n->p);
        other = &(n->n);
    } else if (!n->p.other) {
        me = &(n->n);
        other = &(n->p);
    } else {
        if (which > 0) { // right handle
            if (xn > xp) {
                me = &(n->n);
                other = &(n->p);
            } else {
                me = &(n->p);
                other = &(n->n);
            }
        } else if (which < 0){ // left handle
            if (xn <= xp) {
                me = &(n->n);
                other = &(n->p);
            } else {
                me = &(n->p);
                other = &(n->n);
            }
        } else { // both handles
            me = &(n->n);
            other = &(n->p);
            both = true;
        }
    }

    Radial rme(me->pos - n->pos);
    Radial rother(other->pos - n->pos);

    if (screen) {
        node_rotate_one_internal_screen (*n, angle, rme, rother, both);
    } else {
        node_rotate_one_internal (*n, angle, rme, rother, both);
    }

    me->pos = n->pos + NR::Point(rme);

    if (both || n->type == Inkscape::NodePath::NODE_SMOOTH || n->type == Inkscape::NodePath::NODE_SYMM) {
        other->pos =  n->pos + NR::Point(rother);
    }

    sp_node_ensure_ctrls(n);
}

/**
 * Rotate selected nodes.
 */
void sp_nodepath_selected_nodes_rotate(Inkscape::NodePath::Path *nodepath, gdouble angle, int which, bool screen)
{
    if (!nodepath || !nodepath->selected) return;

    if (g_list_length(nodepath->selected) == 1) {
       Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) nodepath->selected->data;
        node_rotate_one (n, angle, which, screen);
    } else {
       // rotate as an object:

        Inkscape::NodePath::Node *n0 = (Inkscape::NodePath::Node *) nodepath->selected->data;
        NR::Rect box (n0->pos, n0->pos); // originally includes the first selected node
        for (GList *l = nodepath->selected; l != NULL; l = l->next) { 
            Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
            box.expandTo (n->pos); // contain all selected nodes
        }

        gdouble rot;
        if (screen) {
            gdouble const zoom = SP_DESKTOP_ZOOM(nodepath->desktop);
            gdouble const zmove = angle / zoom;
            gdouble const r = NR::L2(box.max() - box.midpoint());
            rot = atan2(zmove, r);
        } else {
            rot = angle;
        }

        NR::Matrix t = 
            NR::Matrix (NR::translate(-box.midpoint())) * 
            NR::Matrix (NR::rotate(rot)) * 
            NR::Matrix (NR::translate(box.midpoint()));

        for (GList *l = nodepath->selected; l != NULL; l = l->next) { 
            Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
            n->pos *= t;
            n->n.pos *= t;
            n->p.pos *= t;
            sp_node_ensure_ctrls(n);
        }
    }

    update_object(nodepath);
    /// \todo fixme: use _keyed
    update_repr(nodepath);
}

/**
 * Scale one node.
 */
static void node_scale_one (Inkscape::NodePath::Node *n, gdouble grow, int which)
{
    bool both = false;
    Inkscape::NodePath::NodeSide *me, *other;

    double xn = n->n.other? n->n.other->pos[NR::X] : n->pos[NR::X];
    double xp = n->p.other? n->p.other->pos[NR::X] : n->pos[NR::X];

    if (!n->n.other) { // if this is an endnode, select its single handle regardless of "which"
        me = &(n->p);
        other = &(n->n);
        n->code = NR_CURVETO;
    } else if (!n->p.other) {
        me = &(n->n);
        other = &(n->p);
        if (n->n.other)    
            n->n.other->code = NR_CURVETO;
    } else {
        if (which > 0) { // right handle
            if (xn > xp) {
                me = &(n->n);
                other = &(n->p);
                if (n->n.other)    
                    n->n.other->code = NR_CURVETO;
            } else {
                me = &(n->p);
                other = &(n->n);
                n->code = NR_CURVETO;
            }
        } else if (which < 0){ // left handle
            if (xn <= xp) {
                me = &(n->n);
                other = &(n->p);
                if (n->n.other)    
                    n->n.other->code = NR_CURVETO;
            } else {
                me = &(n->p);
                other = &(n->n);
                n->code = NR_CURVETO;
            }
        } else { // both handles
            me = &(n->n);
            other = &(n->p);
            both = true;
            n->code = NR_CURVETO;
            if (n->n.other)    
                n->n.other->code = NR_CURVETO;
        }
    }

    Radial rme(me->pos - n->pos);
    Radial rother(other->pos - n->pos);

    rme.r += grow;
    if (rme.r < 0) rme.r = 0;
    if (rme.a == HUGE_VAL) {
        if (me->other) { // if direction is unknown, initialize it towards the next node
            Radial rme_next(me->other->pos - n->pos);
            rme.a = rme_next.a;
        } else { // if there's no next, initialize to 0
            rme.a = 0;
        }
    }
    if (both || n->type == Inkscape::NodePath::NODE_SYMM) {
        rother.r += grow;
        if (rother.r < 0) rother.r = 0;
        if (rother.a == HUGE_VAL) {
            rother.a = rme.a + M_PI;
        }
    }

    me->pos = n->pos + NR::Point(rme);

    if (both || n->type == Inkscape::NodePath::NODE_SYMM) {
        other->pos = n->pos + NR::Point(rother);
    }

    sp_node_ensure_ctrls(n);
}

/**
 * Scale selected nodes.
 */
void sp_nodepath_selected_nodes_scale(Inkscape::NodePath::Path *nodepath, gdouble const grow, int const which)
{
    if (!nodepath || !nodepath->selected) return;

    if (g_list_length(nodepath->selected) == 1) {
        // scale handles of the single selected node
        Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) nodepath->selected->data;
        node_scale_one (n, grow, which);
    } else {
        // scale nodes as an "object":

        Inkscape::NodePath::Node *n0 = (Inkscape::NodePath::Node *) nodepath->selected->data;
        NR::Rect box (n0->pos, n0->pos); // originally includes the first selected node
        for (GList *l = nodepath->selected; l != NULL; l = l->next) { 
            Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
            box.expandTo (n->pos); // contain all selected nodes
        }

        double scale = (box.maxExtent() + grow)/box.maxExtent();

        NR::Matrix t = 
            NR::Matrix (NR::translate(-box.midpoint())) * 
            NR::Matrix (NR::scale(scale, scale)) * 
            NR::Matrix (NR::translate(box.midpoint()));

        for (GList *l = nodepath->selected; l != NULL; l = l->next) { 
            Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
            n->pos *= t;
            n->n.pos *= t;
            n->p.pos *= t;
            sp_node_ensure_ctrls(n);
        }
    }

    update_object(nodepath);
    /// \todo fixme: use _keyed
    update_repr(nodepath);
}

void sp_nodepath_selected_nodes_scale_screen(Inkscape::NodePath::Path *nodepath, gdouble const grow, int const which)
{
    if (!nodepath) return;
    sp_nodepath_selected_nodes_scale(nodepath, grow / SP_DESKTOP_ZOOM(nodepath->desktop), which);
}

/**
 * Flip selected nodes horizontally/vertically.
 */
void sp_nodepath_flip (Inkscape::NodePath::Path *nodepath, NR::Dim2 axis)
{
    if (!nodepath || !nodepath->selected) return;

    if (g_list_length(nodepath->selected) == 1) {
        // flip handles of the single selected node
        Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) nodepath->selected->data;
        double temp = n->p.pos[axis];
        n->p.pos[axis] = n->n.pos[axis];
        n->n.pos[axis] = temp;
        sp_node_ensure_ctrls(n);
    } else {
        // scale nodes as an "object":

        Inkscape::NodePath::Node *n0 = (Inkscape::NodePath::Node *) nodepath->selected->data;
        NR::Rect box (n0->pos, n0->pos); // originally includes the first selected node
        for (GList *l = nodepath->selected; l != NULL; l = l->next) { 
            Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
            box.expandTo (n->pos); // contain all selected nodes
        }

        NR::Matrix t = 
            NR::Matrix (NR::translate(-box.midpoint())) * 
            NR::Matrix ((axis == NR::X)? NR::scale(-1, 1) : NR::scale(1, -1)) * 
            NR::Matrix (NR::translate(box.midpoint()));

        for (GList *l = nodepath->selected; l != NULL; l = l->next) { 
            Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node *) l->data;
            n->pos *= t;
            n->n.pos *= t;
            n->p.pos *= t;
            sp_node_ensure_ctrls(n);
        }
    }

    update_object(nodepath);
    /// \todo fixme: use _keyed
    update_repr(nodepath);
}

//-----------------------------------------------
/**
 * Return new subpath under given nodepath.
 */
static Inkscape::NodePath::SubPath *sp_nodepath_subpath_new(Inkscape::NodePath::Path *nodepath)
{
    g_assert(nodepath);
    g_assert(nodepath->desktop);

   Inkscape::NodePath::SubPath *s = g_new(Inkscape::NodePath::SubPath, 1);

    s->nodepath = nodepath;
    s->closed = FALSE;
    s->nodes = NULL;
    s->first = NULL;
    s->last = NULL;

    // do not use prepend here because:
    // if you have a path like "subpath_1 subpath_2 ... subpath_k" in the svg, you end up with
    // subpath_k -> ... ->subpath_1 in the nodepath structure. thus the i-th node of the svg is not
    // the i-th node in the nodepath (only if there are multiple subpaths)
    // note that the problem only arise when called from subpath_from_bpath(), since for all the other
    // cases, the repr is updated after the call to sp_nodepath_subpath_new()
    nodepath->subpaths = g_list_append /*g_list_prepend*/ (nodepath->subpaths, s);

    return s;
}

/**
 * Destroy nodes in subpath, then subpath itself.
 */
static void sp_nodepath_subpath_destroy(Inkscape::NodePath::SubPath *subpath)
{
    g_assert(subpath);
    g_assert(subpath->nodepath);
    g_assert(g_list_find(subpath->nodepath->subpaths, subpath));

    while (subpath->nodes) {
        sp_nodepath_node_destroy((Inkscape::NodePath::Node *) subpath->nodes->data);
    }

    subpath->nodepath->subpaths = g_list_remove(subpath->nodepath->subpaths, subpath);

    g_free(subpath);
}

/**
 * Link head to tail in subpath.
 */
static void sp_nodepath_subpath_close(Inkscape::NodePath::SubPath *sp)
{
    g_assert(!sp->closed);
    g_assert(sp->last != sp->first);
    g_assert(sp->first->code == NR_MOVETO);

    sp->closed = TRUE;

    //Link the head to the tail
    sp->first->p.other = sp->last;
    sp->last->n.other  = sp->first;
    sp->last->n.pos    = sp->first->n.pos;
    sp->first          = sp->last;

    //Remove the extra end node
    sp_nodepath_node_destroy(sp->last->n.other);
}

/**
 * Open closed (loopy) subpath at node.
 */
static void sp_nodepath_subpath_open(Inkscape::NodePath::SubPath *sp,Inkscape::NodePath::Node *n)
{
    g_assert(sp->closed);
    g_assert(n->subpath == sp);
    g_assert(sp->first == sp->last);

    /* We create new startpoint, current node will become last one */

   Inkscape::NodePath::Node *new_path = sp_nodepath_node_new(sp, n->n.other,Inkscape::NodePath::NODE_CUSP, NR_MOVETO,
                                                &n->pos, &n->pos, &n->n.pos);


    sp->closed        = FALSE;

    //Unlink to make a head and tail
    sp->first         = new_path;
    sp->last          = n;
    n->n.other        = NULL;
    new_path->p.other = NULL;
}

/**
 * Returns area in triangle given by points; may be negative.
 */
inline double
triangle_area (NR::Point p1, NR::Point p2, NR::Point p3) 
{
    return (p1[NR::X]*p2[NR::Y] + p1[NR::Y]*p3[NR::X] + p2[NR::X]*p3[NR::Y] - p2[NR::Y]*p3[NR::X] - p1[NR::Y]*p2[NR::X] - p1[NR::X]*p3[NR::Y]);
}

/**
 * Return new node in subpath with given properties.
 * \param pos Position of node.
 * \param ppos Handle position in previous direction
 * \param npos Handle position in previous direction
 */
Inkscape::NodePath::Node *
sp_nodepath_node_new(Inkscape::NodePath::SubPath *sp, Inkscape::NodePath::Node *next, Inkscape::NodePath::NodeType type, NRPathcode code, NR::Point *ppos, NR::Point *pos, NR::Point *npos)
{
    g_assert(sp);
    g_assert(sp->nodepath);
    g_assert(sp->nodepath->desktop);

    if (nodechunk == NULL)
        nodechunk = g_mem_chunk_create(Inkscape::NodePath::Node, 32, G_ALLOC_AND_FREE);

    Inkscape::NodePath::Node *n = (Inkscape::NodePath::Node*)g_mem_chunk_alloc(nodechunk);

    n->subpath  = sp;

    if (type != Inkscape::NodePath::NODE_NONE) {
        // use the type from sodipodi:nodetypes
        n->type = type;
    } else {
        if (fabs (triangle_area (*pos, *ppos, *npos)) < 1e-2) {
            // points are (almost) collinear
            if (NR::L2(*pos - *ppos) < 1e-6 || NR::L2(*pos - *npos) < 1e-6) {
                // endnode, or a node with a retracted handle
                n->type = Inkscape::NodePath::NODE_CUSP;
            } else {
                n->type = Inkscape::NodePath::NODE_SMOOTH;
            }
        } else {
            n->type = Inkscape::NodePath::NODE_CUSP;
        }
    }

    n->code     = code;
    n->selected = FALSE;
    n->pos      = *pos;
    n->p.pos    = *ppos;
    n->n.pos    = *npos;

    n->dragging_out = NULL;

    Inkscape::NodePath::Node *prev;
    if (next) {
        g_assert(g_list_find(sp->nodes, next));
        prev = next->p.other;
    } else {
        prev = sp->last;
    }

    if (prev)
        prev->n.other = n;
    else
        sp->first = n;

    if (next)
        next->p.other = n;
    else
        sp->last = n;

    n->p.other = prev;
    n->n.other = next;

    n->knot = sp_knot_new(sp->nodepath->desktop);
    sp_knot_set_position(n->knot, pos, 0);
    g_object_set(G_OBJECT(n->knot),
                 "anchor", GTK_ANCHOR_CENTER,
                 "fill", NODE_FILL,
                 "fill_mouseover", NODE_FILL_HI,
                 "stroke", NODE_STROKE,
                 "stroke_mouseover", NODE_STROKE_HI,
                 "tip", _("<b>Node</b>: drag to edit the path; with <b>Ctrl</b> to snap to horizontal/vertical; with <b>Ctrl+Alt</b> to snap to handles' directions"),
                 NULL);
    if (n->type == Inkscape::NodePath::NODE_CUSP)
        g_object_set(G_OBJECT(n->knot), "shape", SP_KNOT_SHAPE_DIAMOND, "size", 9, NULL);
    else
        g_object_set(G_OBJECT(n->knot), "shape", SP_KNOT_SHAPE_SQUARE, "size", 7, NULL);

    g_signal_connect(G_OBJECT(n->knot), "event", G_CALLBACK(node_event), n);
    g_signal_connect(G_OBJECT(n->knot), "clicked", G_CALLBACK(node_clicked), n);
    g_signal_connect(G_OBJECT(n->knot), "grabbed", G_CALLBACK(node_grabbed), n);
    g_signal_connect(G_OBJECT(n->knot), "ungrabbed", G_CALLBACK(node_ungrabbed), n);
    g_signal_connect(G_OBJECT(n->knot), "request", G_CALLBACK(node_request), n);
    sp_knot_show(n->knot);

    n->p.knot = sp_knot_new(sp->nodepath->desktop);
    sp_knot_set_position(n->p.knot, ppos, 0);
    g_object_set(G_OBJECT(n->p.knot),
                 "shape", SP_KNOT_SHAPE_CIRCLE,
                 "size", 7,
                 "anchor", GTK_ANCHOR_CENTER,
                 "fill", KNOT_FILL,
                 "fill_mouseover", KNOT_FILL_HI,
                 "stroke", KNOT_STROKE,
                 "stroke_mouseover", KNOT_STROKE_HI,
                 "tip", _("<b>Node handle</b>: drag to shape the curve; with <b>Ctrl</b> to snap angle; with <b>Alt</b> to lock length; with <b>Shift</b> to rotate both handles"),
                 NULL);
    g_signal_connect(G_OBJECT(n->p.knot), "clicked", G_CALLBACK(node_ctrl_clicked), n);
    g_signal_connect(G_OBJECT(n->p.knot), "grabbed", G_CALLBACK(node_ctrl_grabbed), n);
    g_signal_connect(G_OBJECT(n->p.knot), "ungrabbed", G_CALLBACK(node_ctrl_ungrabbed), n);
    g_signal_connect(G_OBJECT(n->p.knot), "request", G_CALLBACK(node_ctrl_request), n);
    g_signal_connect(G_OBJECT(n->p.knot), "moved", G_CALLBACK(node_ctrl_moved), n);
    g_signal_connect(G_OBJECT(n->p.knot), "event", G_CALLBACK(node_ctrl_event), n);

    sp_knot_hide(n->p.knot);
    n->p.line = sp_canvas_item_new(SP_DT_CONTROLS(n->subpath->nodepath->desktop),
                                   SP_TYPE_CTRLLINE, NULL);
    sp_canvas_item_hide(n->p.line);

    n->n.knot = sp_knot_new(sp->nodepath->desktop);
    sp_knot_set_position(n->n.knot, npos, 0);
    g_object_set(G_OBJECT(n->n.knot),
                 "shape", SP_KNOT_SHAPE_CIRCLE,
                 "size", 7,
                 "anchor", GTK_ANCHOR_CENTER,
                 "fill", KNOT_FILL,
                 "fill_mouseover", KNOT_FILL_HI,
                 "stroke", KNOT_STROKE,
                 "stroke_mouseover", KNOT_STROKE_HI,
                 "tip", _("<b>Node handle</b>: drag to shape the curve; with <b>Ctrl</b> to snap angle; with <b>Alt</b> to lock length; with <b>Shift</b> to rotate the opposite handle in sync"),
                 NULL);
    g_signal_connect(G_OBJECT(n->n.knot), "clicked", G_CALLBACK(node_ctrl_clicked), n);
    g_signal_connect(G_OBJECT(n->n.knot), "grabbed", G_CALLBACK(node_ctrl_grabbed), n);
    g_signal_connect(G_OBJECT(n->n.knot), "ungrabbed", G_CALLBACK(node_ctrl_ungrabbed), n);
    g_signal_connect(G_OBJECT(n->n.knot), "request", G_CALLBACK(node_ctrl_request), n);
    g_signal_connect(G_OBJECT(n->n.knot), "moved", G_CALLBACK(node_ctrl_moved), n);
    g_signal_connect(G_OBJECT(n->n.knot), "event", G_CALLBACK(node_ctrl_event), n);
    sp_knot_hide(n->n.knot);
    n->n.line = sp_canvas_item_new(SP_DT_CONTROLS(n->subpath->nodepath->desktop),
                                   SP_TYPE_CTRLLINE, NULL);
    sp_canvas_item_hide(n->n.line);

    sp->nodes = g_list_prepend(sp->nodes, n);

    return n;
}

/**
 * Destroy node and its knots, link neighbors in subpath.
 */
static void sp_nodepath_node_destroy(Inkscape::NodePath::Node *node)
{
    g_assert(node);
    g_assert(node->subpath);
    g_assert(SP_IS_KNOT(node->knot));
    g_assert(SP_IS_KNOT(node->p.knot));
    g_assert(SP_IS_KNOT(node->n.knot));
    g_assert(g_list_find(node->subpath->nodes, node));

   Inkscape::NodePath::SubPath *sp = node->subpath;

    if (node->selected) { // first, deselect
        g_assert(g_list_find(node->subpath->nodepath->selected, node));
        node->subpath->nodepath->selected = g_list_remove(node->subpath->nodepath->selected, node);
    }

    node->subpath->nodes = g_list_remove(node->subpath->nodes, node);

    g_object_unref(G_OBJECT(node->knot));
    g_object_unref(G_OBJECT(node->p.knot));
    g_object_unref(G_OBJECT(node->n.knot));

    gtk_object_destroy(GTK_OBJECT(node->p.line));
    gtk_object_destroy(GTK_OBJECT(node->n.line));

    if (sp->nodes) { // there are others nodes on the subpath
        if (sp->closed) {
            if (sp->first == node) {
                g_assert(sp->last == node);
                sp->first = node->n.other;
                sp->last = sp->first;
            }
            node->p.other->n.other = node->n.other;
            node->n.other->p.other = node->p.other;
        } else {
            if (sp->first == node) {
                sp->first = node->n.other;
                sp->first->code = NR_MOVETO;
            }
            if (sp->last == node) sp->last = node->p.other;
            if (node->p.other) node->p.other->n.other = node->n.other;
            if (node->n.other) node->n.other->p.other = node->p.other;
        }
    } else { // this was the last node on subpath
        sp->nodepath->subpaths = g_list_remove(sp->nodepath->subpaths, sp);
    }

    g_mem_chunk_free(nodechunk, node);
}

/**
 * Returns one of the node's two knots (node sides).
 * \param which Indicates which side.
 * \return Pointer to previous node side if which==-1, next if which==1.
 */
static Inkscape::NodePath::NodeSide *sp_node_get_side(Inkscape::NodePath::Node *node, gint which)
{
    g_assert(node);

    switch (which) {
        case -1:
            return &node->p;
        case 1:
            return &node->n;
        default:
            break;
    }

    g_assert_not_reached();

    return NULL;
}

/**
 * Return knot on other side of node.
 */
static Inkscape::NodePath::NodeSide *sp_node_opposite_side(Inkscape::NodePath::Node *node,Inkscape::NodePath::NodeSide *me)
{
    g_assert(node);

    if (me == &node->p) return &node->n;
    if (me == &node->n) return &node->p;

    g_assert_not_reached();

    return NULL;
}

/**
 * Return NRPathcode on this knot's side of the node.
 */
static NRPathcode sp_node_path_code_from_side(Inkscape::NodePath::Node *node,Inkscape::NodePath::NodeSide *me)
{
    g_assert(node);

    if (me == &node->p) {
        if (node->p.other) return (NRPathcode)node->code;
        return NR_MOVETO;
    }

    if (me == &node->n) {
        if (node->n.other) return (NRPathcode)node->n.other->code;
        return NR_MOVETO;
    }

    g_assert_not_reached();

    return NR_END;
}

/**
 * Call sp_nodepath_line_add_node() at t on the segment denoted by piece
 */
Inkscape::NodePath::Node * 
sp_nodepath_get_node_by_index(int index)
{
    Inkscape::NodePath::Node *e = NULL;
    
    Inkscape::NodePath::Path *nodepath = sp_nodepath_current();
    if (!nodepath) {
        return e;
    }

    //find segment
    for (GList *l = nodepath->subpaths; l ; l=l->next) {
        
        Inkscape::NodePath::SubPath *sp = (Inkscape::NodePath::SubPath *)l->data;
        int n = g_list_length(sp->nodes);
        if (sp->closed) {
            n++;
        } 
        
        //if the piece belongs to this subpath grab it
        //otherwise move onto the next subpath
        if (index < n) {
            e = sp->first;
            for (int i = 0; i < index; ++i) {
                e = e->n.other;
            }
            break;
        } else {
            if (sp->closed) {
                index -= (n+1);
            } else {
                index -= n;
            }
        }
    }
    
    return e;
}

/**
 * Returns plain text meaning of node type.
 */
static gchar const *sp_node_type_description(Inkscape::NodePath::Node *node)
{
    unsigned retracted = 0;
    bool endnode = false;

    for (int which = -1; which <= 1; which += 2) {
        Inkscape::NodePath::NodeSide *side = sp_node_get_side(node, which);
        if (side->other && NR::L2(side->pos - node->pos) < 1e-6)
            retracted ++;
        if (!side->other)
            endnode = true;
    }

    if (retracted == 0) {
        if (endnode) {
                // TRANSLATORS: "end" is an adjective here (NOT a verb)
                return _("end node");
        } else {
            switch (node->type) {
                case Inkscape::NodePath::NODE_CUSP:
                    // TRANSLATORS: "cusp" means "sharp" (cusp node); see also the Advanced Tutorial
                    return _("cusp");
                case Inkscape::NodePath::NODE_SMOOTH:
                    // TRANSLATORS: "smooth" is an adjective here
                    return _("smooth");
                case Inkscape::NodePath::NODE_SYMM:
                    return _("symmetric");
            }
        }
    } else if (retracted == 1) {
        if (endnode) {
            // TRANSLATORS: "end" is an adjective here (NOT a verb)
            return _("end node, handle retracted (drag with <b>Shift</b> to extend)");
        } else {
            return _("one handle retracted (drag with <b>Shift</b> to extend)");
        }
    } else {
        return _("both handles retracted (drag with <b>Shift</b> to extend)");
    }

    return NULL;
}

/**
 * Handles content of statusbar as long as node tool is active.
 */
void
sp_nodepath_update_statusbar(Inkscape::NodePath::Path *nodepath)
{
    gchar const *when_selected = _("<b>Drag</b> nodes or node handles; <b>arrow</b> keys to move nodes");
    gchar const *when_selected_one = _("<b>Drag</b> the node or its handles; <b>arrow</b> keys to move the node");

    gint total = 0;
    gint selected = 0;
    SPDesktop *desktop = NULL;

    if (nodepath) {
        for (GList *spl = nodepath->subpaths; spl != NULL; spl = spl->next) {
            Inkscape::NodePath::SubPath *subpath = (Inkscape::NodePath::SubPath *) spl->data;
            total += g_list_length(subpath->nodes);
        }
        selected = g_list_length(nodepath->selected);
        desktop = nodepath->desktop;
    } else {
        desktop = SP_ACTIVE_DESKTOP;
    }

    SPEventContext *ec = desktop->event_context;
    if (!ec) return;
    Inkscape::MessageContext *mc = SP_NODE_CONTEXT (ec)->_node_message_context;
    if (!mc) return;

    if (selected == 0) {
        Inkscape::Selection *sel = desktop->selection;
        if (!sel || sel->isEmpty()) {
            mc->setF(Inkscape::NORMAL_MESSAGE,
                     _("Select a single object to edit its nodes or handles."));
        } else {
            if (nodepath) {
            mc->setF(Inkscape::NORMAL_MESSAGE,
                     ngettext("<b>0</b> out of <b>%i</b> node selected. <b>Click</b>, <b>Shift+click</b>, or <b>drag around</b> nodes to select.",
                              "<b>0</b> out of <b>%i</b> nodes selected. <b>Click</b>, <b>Shift+click</b>, or <b>drag around</b> nodes to select.",
                              total),
                     total);
            } else {
                if (g_slist_length((GSList *)sel->itemList()) == 1) {
                    mc->setF(Inkscape::NORMAL_MESSAGE, _("Drag the handles of the object to modify it."));
                } else {
                    mc->setF(Inkscape::NORMAL_MESSAGE, _("Select a single object to edit its nodes or handles."));
                }
            }
        }
    } else if (nodepath && selected == 1) {
        mc->setF(Inkscape::NORMAL_MESSAGE,
                 ngettext("<b>%i</b> of <b>%i</b> node selected; %s. %s.",
                          "<b>%i</b> of <b>%i</b> nodes selected; %s. %s.",
                          total),
                 selected, total, sp_node_type_description((Inkscape::NodePath::Node *) nodepath->selected->data), when_selected_one);
    } else {
        mc->setF(Inkscape::NORMAL_MESSAGE,
                 ngettext("<b>%i</b> of <b>%i</b> node selected. %s.",
                          "<b>%i</b> of <b>%i</b> nodes selected. %s.",
                          total),
                 selected, total, when_selected);
    }
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
