#define __SP_VERBS_C__
/**
	\file verbs.cpp

	This file implements routines necessary to deal with verbs.  A verb is a
	numeric identifier used to retrieve standard SPActions for particular views.
*/

/*
 * Actions for inkscape
 *
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Ted Gould <ted@gould.cx>
 *   MenTaLguY <mental@rydia.net>
 *
 * This code is in public domain
 */

#include <assert.h>

#include <gtk/gtkstock.h>

#include "helper/sp-intl.h"

#include "dialogs/text-edit.h"
#include "dialogs/export.h"
#include "dialogs/xml-tree.h"
#include "dialogs/align.h"
#include "dialogs/transformation.h"
#include "dialogs/object-properties.h"
#include "dialogs/desktop-properties.h"
#include "dialogs/display-settings.h"
#include "dialogs/tool-options.h"
#include "dialogs/item-properties.h"

#include "select-context.h"
#include "node-context.h"
#include "nodepath.h"
#include "rect-context.h"
#include "arc-context.h"
#include "star-context.h"
#include "spiral-context.h"
#include "draw-context.h"
#include "dyna-draw-context.h"
#include "text-context.h"
#include "zoom-context.h"
#include "dropper-context.h"

#include "tools-switch.h"
#include "inkscape-private.h"
#include "file.h"
#include "document.h"
#include "desktop.h"
#include "selection.h"
#include "selection-chemistry.h"
#include "path-chemistry.h"
#include "shortcuts.h"
#include "toolbox.h"
#include "view.h"
#include "prefs-utils.h"
#include "splivarot.h"

#include "verbs.h"

static SPAction *make_action (sp_verb_t verb, SPView *view);

/* FIXME !!! we should probably go ahead and use GHashTables, actually -- more portable */

#if defined(__GNUG__) && (__GNUG__ >= 3)
# include <ext/hash_map>
using __gnu_cxx::hash_map;
#else
# include <hash_map.h>
#endif

#if defined(__GNUG__) && (__GNUG__ >= 3)
namespace __gnu_cxx {
#endif

template <>
class hash<SPView *> {
	typedef SPView *T;
public:
	size_t operator()(const T& x) const {
		return (size_t)g_direct_hash((gpointer)x);
	}
};

#if defined(__GNUG__) && (__GNUG__ >= 3)
}; /* namespace __gnu_cxx */
#endif

typedef hash_map<sp_verb_t, SPAction *> ActionTable;
typedef hash_map<SPView *, ActionTable *> VerbTable;
typedef hash_map<sp_verb_t, SPVerbActionFactory *> FactoryTable;

static VerbTable verb_tables;
static FactoryTable factories;
static sp_verb_t next_verb=SP_VERB_LAST;

/**
	\return  A pointer to SPAction
	\breif   Retrieves an SPAction for a particular verb in a given view
	\param   verb  The verb in question
	\param   view  The SPView to request an SPAction for

*/
SPAction *
sp_verb_get_action (sp_verb_t verb, SPView * view)
{
	VerbTable::iterator view_found=verb_tables.find(view);
	ActionTable *actions;
	if (view_found != verb_tables.end()) {
		actions = (*view_found).second;
	} else {
		actions = new ActionTable;
		verb_tables.insert(VerbTable::value_type(view, actions));
		/* FIXME !!! add SPView::destroy callback to destroy actions
		             and free table when SPView is no more */
	}

	ActionTable::iterator action_found=actions->find(verb);
	if (action_found != actions->end()) {
		return (*action_found).second;
	} else {
		SPAction *action=NULL;
		if (verb < SP_VERB_LAST) {
			action = make_action(verb, view);
		} else {
			FactoryTable::iterator found;
			found = factories.find(verb);
			if (found != factories.end()) {
				action = (*found).second->make_action(verb, view);
			}
		}
		if (action) {
			actions->insert(ActionTable::value_type(verb, action));
			return action;
		} else {
			return NULL;
		}
	}
}

/**
	Return the name without underscores and ellipsis, for use in dialog titles, etc.
	Allocated memory must be freed by caller.
*/
gchar *
sp_action_get_title (const SPAction *action)
{
	char const *src = action->name;
	gchar *ret = g_new (gchar, strlen(src) + 1);
	unsigned ri = 0;

	for (unsigned si = 0 ; ; si++)  {
		int const c = src[si];
		if ( c != '_' && c != '.' ) {
			ret[ri] = c;
			ri++;
			if (c == '\0') {
				return ret;
			}
		}
	}
}

static void
sp_verb_action_file_perform (SPAction *action, void * data, void *pdata)
{
	switch ((int) data) {
	case SP_VERB_FILE_NEW:
		sp_file_new ();
		break;
	case SP_VERB_FILE_OPEN:
		sp_file_open_dialog (NULL, NULL);
		break;
	case SP_VERB_FILE_SAVE:
		sp_file_save (NULL, NULL);
		break;
	case SP_VERB_FILE_SAVE_AS:
		sp_file_save_as (NULL, NULL);
		break;
	case SP_VERB_FILE_PRINT:
		sp_file_print ();
		break;
	case SP_VERB_FILE_PRINT_DIRECT:
		sp_file_print_direct ();
		break;
	case SP_VERB_FILE_PRINT_PREVIEW:
		sp_file_print_preview (NULL, NULL);
		break;
	case SP_VERB_FILE_IMPORT:
		sp_file_import (NULL);
		break;
	case SP_VERB_FILE_EXPORT:
		sp_file_export_dialog (NULL);
		break;
	case SP_VERB_FILE_NEXT_DESKTOP:
		inkscape_switch_desktops_next();
		break;
	case SP_VERB_FILE_PREV_DESKTOP:
		inkscape_switch_desktops_prev();
		break;
	case SP_VERB_FILE_QUIT:
		sp_file_exit ();
		break;
	default:
		break;
	}
}

static void
sp_verb_action_edit_perform (SPAction *action, void * data, void * pdata)
{
	SPDesktop *dt;
	SPEventContext *ec;

	dt = SP_DESKTOP (sp_action_get_view (action));
	if (!dt) return;

	ec = dt->event_context;

	switch ((int) data) {
	case SP_VERB_EDIT_UNDO:
		sp_undo (dt, SP_DT_DOCUMENT (dt));
		break;
	case SP_VERB_EDIT_REDO:
		sp_redo (dt, SP_DT_DOCUMENT (dt));
		break;
	case SP_VERB_EDIT_CUT:
		sp_selection_cut (NULL);
		break;
	case SP_VERB_EDIT_COPY:
		sp_selection_copy (NULL);
		break;
	case SP_VERB_EDIT_PASTE:
		sp_selection_paste (NULL, false); 
		break;
	case SP_VERB_EDIT_PASTE_STYLE:
		sp_selection_paste_style (NULL); 
		break;
	case SP_VERB_EDIT_PASTE_IN_PLACE:
		sp_selection_paste (NULL, true); 
		break;
	case SP_VERB_EDIT_DELETE:
		sp_selection_delete (NULL, NULL);
		break;
	case SP_VERB_EDIT_DUPLICATE:
		sp_selection_duplicate (NULL, NULL);
		break;
	case SP_VERB_EDIT_CLEAR_ALL:
	  	sp_edit_clear_all (NULL, NULL);
		break;
	case SP_VERB_EDIT_SELECT_ALL:
 		if (tools_isactive (dt, TOOLS_NODES)) {
 			sp_nodepath_select_all (SP_NODE_CONTEXT(ec)->nodepath);
 		} else {
 			sp_edit_select_all (NULL, NULL);
 		}
		break;
	case SP_VERB_EDIT_DESELECT:
 		if (tools_isactive (dt, TOOLS_NODES)) {
 			sp_nodepath_deselect (SP_NODE_CONTEXT(ec)->nodepath);
 		} else {
 			sp_selection_empty (SP_DT_SELECTION(dt));
 		}
		break;
	default:
		break;
	}
}

static void
sp_verb_action_selection_perform (SPAction *action, void * data, void * pdata)
{
	SPDesktop *dt;

	dt = SP_DESKTOP (sp_action_get_view (action));
	if (!dt) return;

	switch ((int) data) {
	case SP_VERB_SELECTION_TO_FRONT:
		sp_selection_raise_to_top (NULL);
		break;
	case SP_VERB_SELECTION_TO_BACK:
		sp_selection_lower_to_bottom (NULL);
		break;
	case SP_VERB_SELECTION_RAISE:
		sp_selection_raise (NULL);
		break;
	case SP_VERB_SELECTION_LOWER:
		sp_selection_lower (NULL);
		break;
	case SP_VERB_SELECTION_GROUP:
		sp_selection_group (NULL, NULL);
		break;
	case SP_VERB_SELECTION_UNGROUP:
		sp_selection_ungroup (NULL, NULL);
		break;

	case SP_VERB_SELECTION_UNION:
		sp_selected_path_union ();
		break;
	case SP_VERB_SELECTION_INTERSECT:
		sp_selected_path_intersect ();
		break;
	case SP_VERB_SELECTION_DIFF:
		sp_selected_path_diff ();
		break;
	case SP_VERB_SELECTION_SYMDIFF:
		sp_selected_path_symdiff ();
		break;

	case SP_VERB_SELECTION_OFFSET:
		sp_selected_path_offset ();
		break;
	case SP_VERB_SELECTION_INSET:
		sp_selected_path_inset ();
		break;
	case SP_VERB_SELECTION_DYNAMIC_OFFSET:
		sp_selected_path_create_offset_object_zero ();
		break;
	case SP_VERB_SELECTION_LINKED_OFFSET:
		sp_selected_path_create_updating_offset_object_zero ();
		break;

	case SP_VERB_SELECTION_OUTLINE:
		sp_selected_path_outline ();
		break;
	case SP_VERB_SELECTION_SIMPLIFY:
		sp_selected_path_simplify ();
		break;


	case SP_VERB_SELECTION_COMBINE:
		sp_selected_path_combine ();
		break;
	case SP_VERB_SELECTION_BREAK_APART:
		sp_selected_path_break_apart ();
		break;
	default:
		break;
	}
}

static void
sp_verb_action_object_perform (SPAction *action, void * data, void * pdata)
{
	SPDesktop *dt;
	SPSelection *sel;
	NRRect bbox;
	NRPoint center;

	dt = SP_DESKTOP (sp_action_get_view (action));
	if (!dt) return;
	sel = SP_DT_SELECTION (dt);
	if (sp_selection_is_empty (sel)) return;
	sp_selection_bbox (sel, &bbox);
	center.x = 0.5 * (bbox.x0 + bbox.x1);
	center.y = 0.5 * (bbox.y0 + bbox.y1);

	switch ((int) data) {
	case SP_VERB_OBJECT_ROTATE_90:
		sp_selection_rotate_90 ();
		break;
	case SP_VERB_OBJECT_FLATTEN:
		sp_selection_remove_transform ();
		break;
	case SP_VERB_OBJECT_TO_CURVE:
		sp_selected_path_to_curves ();
		break;
	case SP_VERB_OBJECT_FLIP_HORIZONTAL:
		// TODO: make tool-sensitive, in node edit flip selected node(s)
		sp_selection_scale_relative (sel, &center, -1.0, 1.0);
		sp_document_done (SP_DT_DOCUMENT (dt));
		break;
	case SP_VERB_OBJECT_FLIP_VERTICAL:
		// TODO: make tool-sensitive, in node edit flip selected node(s)
		sp_selection_scale_relative (sel, &center, 1.0, -1.0);
		sp_document_done (SP_DT_DOCUMENT (dt));
		break;
	default:
		break;
	}
}

static void
sp_verb_action_ctx_perform (SPAction *action, void * data, void * pdata)
{
	SPDesktop *dt;
	sp_verb_t verb;
	int vidx;

	dt = SP_DESKTOP (sp_action_get_view (action));
	if (!dt) return;
	verb = (sp_verb_t)GPOINTER_TO_INT((gpointer)data);

	/* FIXME !!! hopefully this can go away soon and actions can look after themselves */
	for (vidx = SP_VERB_CONTEXT_SELECT; vidx <= SP_VERB_CONTEXT_DROPPER; vidx++) {
		SPAction *tool_action=sp_verb_get_action((sp_verb_t)vidx, SP_VIEW (dt));
		if (tool_action) {
			sp_action_set_active (tool_action, vidx == (int)verb);
		}
	}

	switch (verb) {
	case SP_VERB_CONTEXT_SELECT:
		tools_switch_current (TOOLS_SELECT);
		break;
	case SP_VERB_CONTEXT_NODE:
		tools_switch_current (TOOLS_NODES);
		break;
	case SP_VERB_CONTEXT_RECT:
		tools_switch_current (TOOLS_SHAPES_RECT);
		break;
	case SP_VERB_CONTEXT_ARC:
		tools_switch_current (TOOLS_SHAPES_ARC);
		break;
	case SP_VERB_CONTEXT_STAR:
		tools_switch_current (TOOLS_SHAPES_STAR);
		break;
	case SP_VERB_CONTEXT_SPIRAL:
		tools_switch_current (TOOLS_SHAPES_SPIRAL);
		break;
	case SP_VERB_CONTEXT_PENCIL:
		tools_switch_current (TOOLS_FREEHAND_PENCIL);
		break;
	case SP_VERB_CONTEXT_PEN:
		tools_switch_current (TOOLS_FREEHAND_PEN);
		break;
	case SP_VERB_CONTEXT_CALLIGRAPHIC:
		tools_switch_current (TOOLS_CALLIGRAPHIC);
		break;
	case SP_VERB_CONTEXT_TEXT:
		tools_switch_current (TOOLS_TEXT);
		break;
	case SP_VERB_CONTEXT_ZOOM:
		tools_switch_current (TOOLS_ZOOM);
		break;
	case SP_VERB_CONTEXT_DROPPER:
		tools_switch_current (TOOLS_DROPPER);
		break;
	default:
		break;
	}
}

static void
sp_verb_action_zoom_perform (SPAction *action, void * data, void * pdata)
{
	SPDesktop *dt;
	NRRect d;
	SPRepr *repr;
	unsigned int v = 0;

	dt = SP_DESKTOP (sp_action_get_view (action));
	if (!dt) return;
	repr = SP_OBJECT_REPR (dt->namedview);

	gdouble zoom_inc = prefs_get_double_attribute_limited ("options.zoomincrement", "value", 1.414213562, 1.01, 10);

	switch ((int) data) {
	case SP_VERB_ZOOM_IN:
		sp_desktop_get_display_area (dt, &d);
		sp_desktop_zoom_relative (dt, (d.x0 + d.x1) / 2, (d.y0 + d.y1) / 2, zoom_inc);
		break;
	case SP_VERB_ZOOM_OUT:
		sp_desktop_get_display_area (dt, &d);
		sp_desktop_zoom_relative (dt, (d.x0 + d.x1) / 2, (d.y0 + d.y1) / 2, 1 / zoom_inc);
		break;
	case SP_VERB_ZOOM_1_1:
		sp_desktop_get_display_area (dt, &d);
		sp_desktop_zoom_absolute (dt, (d.x0 + d.x1) / 2, (d.y0 + d.y1) / 2, 1.0);
		break;
	case SP_VERB_ZOOM_1_2:
		sp_desktop_get_display_area (dt, &d);
		sp_desktop_zoom_absolute (dt, (d.x0 + d.x1) / 2, (d.y0 + d.y1) / 2, 0.5);
		break;
	case SP_VERB_ZOOM_2_1:
		sp_desktop_get_display_area (dt, &d);
		sp_desktop_zoom_absolute (dt, (d.x0 + d.x1) / 2, (d.y0 + d.y1) / 2, 2.0);
		break;
	case SP_VERB_ZOOM_PAGE:
		sp_desktop_zoom_page (dt);
		break;
	case SP_VERB_ZOOM_PAGE_WIDTH:
		sp_desktop_zoom_page_width (dt);
		break;
	case SP_VERB_ZOOM_DRAWING:
		sp_desktop_zoom_drawing (dt);
		break;
	case SP_VERB_ZOOM_SELECTION:
		sp_desktop_zoom_selection (dt);
		break;
	case SP_VERB_ZOOM_NEXT:
		sp_desktop_next_zoom (dt);
		break;
	case SP_VERB_ZOOM_PREV:
		sp_desktop_prev_zoom (dt);
		break;
	case SP_VERB_TOGGLE_RULERS:
		sp_desktop_toggle_rulers (dt);
		break;
	case SP_VERB_TOGGLE_SCROLLBARS:
		sp_desktop_toggle_scrollbars (dt);
		break;
	case SP_VERB_TOGGLE_GUIDES:
		sp_repr_get_boolean (repr, "showguides", &v);
		sp_repr_set_boolean (repr, "showguides", !(v));
		break;
	case SP_VERB_TOGGLE_GRID:
		sp_repr_get_boolean (repr, "showgrid", &v);
		sp_repr_set_boolean (repr, "showgrid", !(v));
		break;
#ifdef HAVE_GTK_WINDOW_FULLSCREEN
	case SP_VERB_FULLSCREEN:
		fullscreen (dt);
		break;
#endif /* HAVE_GTK_WINDOW_FULLSCREEN */
	default:
		break;
	}
}

static void
sp_verb_action_dialog_perform (SPAction *action, void * data, void * pdata)
{
	if ((int) data != SP_VERB_DIALOG_TOGGLE) { // unhide all when opening a new dialog
		inkscape_dialogs_unhide ();
	}

	switch ((int) data) {
	case SP_VERB_DIALOG_DISPLAY:
		sp_display_dialog ();
		break;
	case SP_VERB_DIALOG_NAMEDVIEW:
		sp_desktop_dialog ();
		break;
	case SP_VERB_DIALOG_TOOL_OPTIONS:
		sp_tool_options_dialog ();
		break;
	case SP_VERB_DIALOG_FILL_STROKE:
		sp_object_properties_dialog ();
		break;
	case SP_VERB_DIALOG_TRANSFORM:
		sp_transformation_dialog_move ();
		break;
	case SP_VERB_DIALOG_ALIGN_DISTRIBUTE:
		sp_quick_align_dialog ();
		break;
	case SP_VERB_DIALOG_TEXT:
		sp_text_edit_dialog ();
		break;
	case SP_VERB_DIALOG_XML_EDITOR:
		sp_xml_tree_dialog ();
		break;
	case SP_VERB_DIALOG_TOGGLE:
		inkscape_dialogs_toggle ();
		break;
	case SP_VERB_DIALOG_ITEM:
		sp_item_dialog ();
		break;
	default:
		break;
	}
}

/** Action vector to define functions called if a staticly defined file verb is called */
static SPActionEventVector action_file_vector = {{NULL}, sp_verb_action_file_perform, NULL, NULL, NULL};
/** Action vector to define functions called if a staticly defined edit verb is called */
static SPActionEventVector action_edit_vector = {{NULL}, sp_verb_action_edit_perform, NULL, NULL, NULL};
/** Action vector to define functions called if a staticly defined selection verb is called */
static SPActionEventVector action_selection_vector = {{NULL}, sp_verb_action_selection_perform, NULL, NULL, NULL};
/** Action vector to define functions called if a staticly defined object editing verb is called */
static SPActionEventVector action_object_vector = {{NULL}, sp_verb_action_object_perform, NULL, NULL, NULL};
/** Action vector to define functions called if a staticly defined context verb is called */
static SPActionEventVector action_ctx_vector = {{NULL}, sp_verb_action_ctx_perform, NULL, NULL, NULL};
/** Action vector to define functions called if a staticly defined zoom verb is called */
static SPActionEventVector action_zoom_vector = {{NULL}, sp_verb_action_zoom_perform, NULL, NULL, NULL};
/** Action vector to define functions called if a staticly defined dialog verb is called */
static SPActionEventVector action_dialog_vector = {{NULL}, sp_verb_action_dialog_perform, NULL, NULL, NULL};

#define SP_VERB_IS_FILE(v) ((v >= SP_VERB_FILE_NEW) && (v <= SP_VERB_FILE_QUIT))
#define SP_VERB_IS_EDIT(v) ((v >= SP_VERB_EDIT_UNDO) && (v <= SP_VERB_EDIT_DESELECT))
#define SP_VERB_IS_SELECTION(v) ((v >= SP_VERB_SELECTION_TO_FRONT) && (v <= SP_VERB_SELECTION_BREAK_APART))
#define SP_VERB_IS_OBJECT(v) ((v >= SP_VERB_OBJECT_ROTATE_90) && (v <= SP_VERB_OBJECT_FLIP_VERTICAL))
#define SP_VERB_IS_CONTEXT(v) ((v >= SP_VERB_CONTEXT_SELECT) && (v <= SP_VERB_CONTEXT_DROPPER))
#if HAVE_GTK_WINDOW_FULLSCREEN
#define SP_VERB_IS_ZOOM(v) ((v >= SP_VERB_ZOOM_IN) && (v <= SP_VERB_FULLSCREEN))
#else
#define SP_VERB_IS_ZOOM(v) ((v >= SP_VERB_ZOOM_IN) && (v <= SP_VERB_ZOOM_SELECTION))
#endif /* HAVE_GTK_FULLSCREEN */

#define SP_VERB_IS_DIALOG(v) ((v >= SP_VERB_DIALOG_DISPLAY) && (v <= SP_VERB_DIALOG_ITEM))

/**  A structure to hold information about a verb */
typedef struct {
	sp_verb_t code;   /**< Verb number (staticly from enum) */
	const gchar *id;     /**< Textual identifier for the verb */
	const gchar *name;   /**< Name of the verb */
	const gchar *tip;    /**< Tooltip to print on hover */
	const gchar *image;  /**< Image to describe the verb */
} SPVerbActionDef;

static const SPVerbActionDef props[] = {
	/* Header */
	{SP_VERB_INVALID, NULL, NULL, NULL, NULL},
	{SP_VERB_NONE, "None", N_("None"), N_("Does nothing"), NULL},
	/* File */
	{SP_VERB_FILE_NEW, "FileNew", N_("_New"), N_("Create new document"), GTK_STOCK_NEW },
	{SP_VERB_FILE_OPEN, "FileOpen", N_("_Open..."), N_("Open existing document"), GTK_STOCK_OPEN },
	{SP_VERB_FILE_SAVE, "FileSave", N_("_Save"), N_("Save document"), GTK_STOCK_SAVE },
	{SP_VERB_FILE_SAVE_AS, "FileSaveAs", N_("Save _As..."), N_("Save document under new name"), GTK_STOCK_SAVE_AS },
	{SP_VERB_FILE_PRINT, "FilePrint", N_("_Print..."), N_("Print document"), GTK_STOCK_PRINT },
	{SP_VERB_FILE_PRINT_DIRECT, "FilePrintDirect", N_("Print _Direct"), N_("Print directly to file or pipe"), "file_print_direct" },
	{SP_VERB_FILE_PRINT_PREVIEW, "FilePrintPreview", N_("Print Previe_w"), N_("Preview document printout"), GTK_STOCK_PRINT_PREVIEW },
	{SP_VERB_FILE_IMPORT, "FileImport", N_("_Import..."), N_("Import bitmap or SVG image into document"), "file_import"},
	{SP_VERB_FILE_EXPORT, "FileExport", N_("_Export Bitmap..."), N_("Export document as PNG bitmap"), "file_export"},
	{SP_VERB_FILE_NEXT_DESKTOP, "FileNextDesktop", N_("N_ext window"), N_("Switch to the next document window"), NULL},
	{SP_VERB_FILE_PREV_DESKTOP, "FilePrevDesktop", N_("P_rev window"), N_("Switch to the previous document window"), NULL},
	{SP_VERB_FILE_QUIT, "FileQuit", N_("_Quit"), N_("Quit"), GTK_STOCK_QUIT},
	/* Edit */
	{SP_VERB_EDIT_UNDO, "EditUndo", N_("_Undo"), N_("Undo last action"), GTK_STOCK_UNDO},
	{SP_VERB_EDIT_REDO, "EditRedo", N_("_Redo"), N_("Do again last undone action"), GTK_STOCK_REDO},
	{SP_VERB_EDIT_CUT, "EditCut", N_("Cu_t"), N_("Cut selected objects to clipboard"), GTK_STOCK_CUT},
	{SP_VERB_EDIT_COPY, "EditCopy", N_("_Copy"), N_("Copy selected objects to clipboard"), GTK_STOCK_COPY},
	{SP_VERB_EDIT_PASTE, "EditPaste", N_("_Paste"), N_("Paste objects from clipboard"), GTK_STOCK_PASTE},
	{SP_VERB_EDIT_PASTE_STYLE, "EditPasteStyle", N_("Paste _Style"), N_("Apply style of copied object to selection"), NULL},
	{SP_VERB_EDIT_PASTE_IN_PLACE, "EditPasteInPlace", N_("Paste _In Place"), N_("Paste objects to the original location"), NULL},
	{SP_VERB_EDIT_DELETE, "EditDelete", N_("_Delete"), N_("Delete selected objects"), GTK_STOCK_DELETE},
	{SP_VERB_EDIT_DUPLICATE, "EditDuplicate", N_("D_uplicate"), N_("Duplicate selected objects"), "edit_duplicate"},
	{SP_VERB_EDIT_CLEAR_ALL, "EditClearAll", N_("Clea_r All"), N_("Delete all objects from document"), NULL},
	{SP_VERB_EDIT_SELECT_ALL, "EditSelectAll", N_("Select _All"), N_("Select all objects or all nodes"), NULL},
	{SP_VERB_EDIT_DESELECT, "EditDeselect", N_("D_eselect"), N_("Deselect any selected objects or nodes"), NULL},
	/* Selection */
	{SP_VERB_SELECTION_TO_FRONT, "SelectionToFront", N_("Bring to _Front"), N_("Raise selection to top"), "selection_top"},
	{SP_VERB_SELECTION_TO_BACK, "SelectionToBack", N_("Send to _Back"), N_("Lower selection to bottom"), "selection_bot"},
	{SP_VERB_SELECTION_RAISE, "SelectionRaise", N_("_Raise"), N_("Raise selection one step"), "selection_up"},
	{SP_VERB_SELECTION_LOWER, "SelectionLower", N_("_Lower"), N_("Lower selection one step"), "selection_down"},
	{SP_VERB_SELECTION_GROUP, "SelectionGroup", N_("_Group"), N_("Group selected objects"), "selection_group"},
	{SP_VERB_SELECTION_UNGROUP, "SelectionUnGroup", N_("_Ungroup"), N_("Ungroup selected group(s)"), "selection_ungroup"},
	{SP_VERB_SELECTION_UNION, "SelectionUnion", N_("_Union"), N_("Union of selected objects"), NULL},
	{SP_VERB_SELECTION_INTERSECT, "SelectionIntersect", N_("_Intersection"), N_("Intersection of selected objects"), NULL},
	{SP_VERB_SELECTION_DIFF, "SelectionDiff", N_("_Difference"), N_("Difference of selected objects"), NULL},
	{SP_VERB_SELECTION_SYMDIFF, "SelectionSymDiff", N_("E_xclusion"), N_("Exclusive OR of selected objects"), NULL},
	{SP_VERB_SELECTION_OFFSET, "SelectionOffset", N_("O_utset Path"), N_("Outset selected paths"), NULL},
	{SP_VERB_SELECTION_INSET, "SelectionInset", N_("I_nset Path"), N_("Inset selected paths"), NULL},
	{SP_VERB_SELECTION_DYNAMIC_OFFSET, "SelectionDynOffset", N_("D_ynamic Offset"), N_("Create a dynamic offset object"), NULL},
	{SP_VERB_SELECTION_LINKED_OFFSET, "SelectionLinkedOffset", N_("_Linked Offset"), N_("Create a dynamic offset object linked to the original path"), NULL},
	{SP_VERB_SELECTION_OUTLINE, "SelectionOutline", N_("_Stroke to Path"), N_("Convert selected stroke to path"), NULL},
	{SP_VERB_SELECTION_SIMPLIFY, "SelectionSimplify", N_("Simp_lify Path"), N_("Simplify selected path"), NULL},
	{SP_VERB_SELECTION_COMBINE, "SelectionCombine", N_("_Combine"), N_("Combine multiple paths"), "selection_combine"},
	{SP_VERB_SELECTION_BREAK_APART, "SelectionBreakApart", N_("Break _Apart"), N_("Break selected path to subpaths"), "selection_break"},
	/* Object */
	{SP_VERB_OBJECT_ROTATE_90, "ObjectRotate90", N_("Rotate 90 _Degrees"), N_("Rotate selection 90 degrees clockwise"), "object_rotate"},
	{SP_VERB_OBJECT_FLATTEN, "ObjectFlatten", N_("Remove _Transformations"), N_("Remove transformations from object"), "object_reset"},
	{SP_VERB_OBJECT_TO_CURVE, "ObjectToCurve", N_("_Object to Path"), N_("Convert selected objects to paths"), "object_tocurve"},
	{SP_VERB_OBJECT_FLIP_HORIZONTAL, "ObjectFlipHorizontally", N_("Flip _Horizontally"),
	 N_("Flip selection horizontally"), "object_flip_hor"},
	{SP_VERB_OBJECT_FLIP_VERTICAL, "ObjectFlipVertically", N_("Flip _Vertically"),
	 N_("Flip selection vertically"), "object_flip_ver"},
	/* Event contexts */
	// FIXME: add shortcuts to tooltips automatically!
	{SP_VERB_CONTEXT_SELECT, "DrawSelect", N_("Select"), N_("Select and transform objects (F1)"), "draw_select"},
	{SP_VERB_CONTEXT_NODE, "DrawNode", N_("Node Edit"), N_("Edit path nodes or control handles (F2)"), "draw_node"},
	{SP_VERB_CONTEXT_RECT, "DrawRect", N_("Rectangle"), N_("Create rectangles and squares (F4)"), "draw_rect"},
	{SP_VERB_CONTEXT_ARC, "DrawArc", N_("Ellipse"), N_("Create circles, ellipses, and arcs (F5)"), "draw_arc"},
	{SP_VERB_CONTEXT_STAR, "DrawStar", N_("Star"), N_("Create stars and polygons (*)"), "draw_star"},
	{SP_VERB_CONTEXT_SPIRAL, "DrawSpiral", N_("Spiral"), N_("Create spirals (F9)"), "draw_spiral"},
	{SP_VERB_CONTEXT_PENCIL, "DrawPencil", N_("Pencil"), N_("Draw freehand lines (F6)"), "draw_freehand"},
	{SP_VERB_CONTEXT_PEN, "DrawPen", N_("Pen"), N_("Draw Bezier curves and straight lines (Shift+F6)"), "draw_pen"},
	{SP_VERB_CONTEXT_CALLIGRAPHIC, "DrawCalligrphic", N_("Calligraphy"), N_("Draw calligraphic lines (Ctrl+F6)"), "draw_dynahand"},
	{SP_VERB_CONTEXT_TEXT, "DrawText", N_("Text"), N_("Create and edit text objects (F8)"), "draw_text"},
	{SP_VERB_CONTEXT_ZOOM, "DrawZoom", N_("Zoom"), N_("Zoom in or out (F3)"), "draw_zoom"},
	{SP_VERB_CONTEXT_DROPPER, "DrawDropper", N_("Dropper"), N_("Pick averaged colors from image (F7)"), "draw_dropper"},
	/* Zooming */
	{SP_VERB_ZOOM_IN, "ZoomIn", N_("Zoom In"), N_("Zoom in"), "zoom_in"},
	{SP_VERB_ZOOM_OUT, "ZoomOut", N_("Zoom Out"), N_("Zoom out"), "zoom_out"},
	{SP_VERB_TOGGLE_RULERS, "ToggleRulers", N_("Rulers"), N_("Show/hide rulers"), NULL},
	{SP_VERB_TOGGLE_SCROLLBARS, "ToggleScrollbars", N_("Scrollbars"), N_("Show/hide scrollbars"), NULL},
	{SP_VERB_TOGGLE_GRID, "ToggleGrid", N_("Grid"), N_("Show/hide grid"), NULL},
	{SP_VERB_TOGGLE_GUIDES, "ToggleGuides", N_("Guides"), N_("Show/hide guides"), "toggle_guides"},
	{SP_VERB_ZOOM_NEXT, "ZoomNext", N_("Nex_t zoom"), N_("Next zoom"), NULL},
	{SP_VERB_ZOOM_PREV, "ZoomPrev", N_("Pre_v zoom"), N_("Previous zoom"), NULL},
	{SP_VERB_ZOOM_1_1, "Zoom1:0", N_("Zoom 1:_1"), N_("Zoom to 1:1"), "zoom_1_to_1"},
	{SP_VERB_ZOOM_1_2, "Zoom1:2", N_("Zoom 1:_2"), N_("Zoom to 1:2"), "zoom_1_to_2"},
	{SP_VERB_ZOOM_2_1, "Zoom2:1", N_("_Zoom 2:1"), N_("Zoom to 2:1"), "zoom_2_to_1"},
	{SP_VERB_ZOOM_PAGE, "ZoomPage", N_("_Page"), N_("Fit page in window"), "zoom_page"},
	{SP_VERB_ZOOM_PAGE_WIDTH, "ZoomPageWidth", N_("Page _Width"), N_("Fit page width in window"), NULL},
	{SP_VERB_ZOOM_DRAWING, "ZoomDrawing", N_("_Drawing"), N_("Fit drawing in window"), "zoom_draw"},
	{SP_VERB_ZOOM_SELECTION, "ZoomSelection", N_("_Selection"), N_("Fit selection in window"), "zoom_select"},
#ifdef HAVE_GTK_WINDOW_FULLSCREEN
	{SP_VERB_FULLSCREEN, "FullScreen", N_("_Fullscreen"), N_("Fullscreen"), NULL},
#endif /* HAVE_GTK_WINDOW_FULLSCREEN */
	/* Dialogs */
	{SP_VERB_DIALOG_DISPLAY, "DialogDisplay", N_("Inkscape _Options"), N_("Global Inkscape options"), NULL},
	{SP_VERB_DIALOG_NAMEDVIEW, "DialogNamedview", N_("_Document Options"), N_("Options saved with the document"), NULL},
	{SP_VERB_DIALOG_TOOL_OPTIONS, "DialogToolOptions", N_("Tool Optio_ns"), N_("Tool options"), NULL},
	{SP_VERB_DIALOG_FILL_STROKE, "DialogFillStroke", N_("_Fill and Stroke"), N_("Fill and stroke settings"), NULL},
	{SP_VERB_DIALOG_TRANSFORM, "DialogTransform", N_("Transfor_m"), N_("Object transformations"), "object_trans"},
	{SP_VERB_DIALOG_ALIGN_DISTRIBUTE, "DialogAlignDistribute", N_("_Align and Distribute"), N_("Align and distribute objects"), "object_align"},
	{SP_VERB_DIALOG_TEXT, "Dialogtext", N_("_Text and Font"), N_("Text editing and font settings"), "object_font"},
	{SP_VERB_DIALOG_XML_EDITOR, "DialogXMLEditor", N_("_XML Editor"), N_("XML Editor"), NULL},
	{SP_VERB_DIALOG_TOGGLE, "DialogsToggle", N_("_Hide/View dialogs"), N_("Toggle visibility of all active dialogs"), NULL},
	{SP_VERB_DIALOG_ITEM, "DialogItem", N_("Item _Properties"), N_("Item properties"), NULL},
	/* Footer */
	{SP_VERB_LAST, NULL, NULL, NULL, NULL}
};

static SPAction *
make_action (sp_verb_t verb, SPView *view)
{
	SPAction *action=nr_new (SPAction, 1);
	assert (props[verb].code == verb);
	sp_action_setup (action, view, props[verb].id, _(props[verb].name), _(props[verb].tip), props[verb].image);
	/* fixme: Make more elegant (Lauris) */
	if (SP_VERB_IS_FILE (verb)) {
		nr_active_object_add_listener ((NRActiveObject *) action,
					       (NRObjectEventVector *) &action_file_vector,
					       sizeof (SPActionEventVector),
					       (void *) verb);
	} else if (SP_VERB_IS_EDIT (verb)) {
		nr_active_object_add_listener ((NRActiveObject *) action,
					       (NRObjectEventVector *) &action_edit_vector,
					       sizeof (SPActionEventVector),
					       (void *) verb);
	} else if (SP_VERB_IS_SELECTION (verb)) {
		nr_active_object_add_listener ((NRActiveObject *) action,
					       (NRObjectEventVector *) &action_selection_vector,
					       sizeof (SPActionEventVector),
					       (void *) verb);
	} else if (SP_VERB_IS_OBJECT (verb)) {
		nr_active_object_add_listener ((NRActiveObject *) action,
					       (NRObjectEventVector *) &action_object_vector,
					       sizeof (SPActionEventVector),
					       (void *) verb);
	} else if (SP_VERB_IS_CONTEXT (verb)) {
		nr_active_object_add_listener ((NRActiveObject *) action,
					       (NRObjectEventVector *) &action_ctx_vector,
					       sizeof (SPActionEventVector),
					       (void *) verb);
	} else if (SP_VERB_IS_ZOOM (verb)) {
		nr_active_object_add_listener ((NRActiveObject *) action,
					       (NRObjectEventVector *) &action_zoom_vector,
					       sizeof (SPActionEventVector),
					       (void *) verb);
	} else if (SP_VERB_IS_DIALOG (verb)) {
		nr_active_object_add_listener ((NRActiveObject *) action,
					       (NRObjectEventVector *) &action_dialog_vector,
					       sizeof (SPActionEventVector),
					       (void *) verb);
	}
	return action;
}

sp_verb_t
sp_verb_register (SPVerbActionFactory *factory)
{
	sp_verb_t verb=next_verb++;
	factories.insert(FactoryTable::value_type(verb, factory));
	return verb;
}

