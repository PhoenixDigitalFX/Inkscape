/*
 * A quick hack to use the print output to write out a file.  This
 * then makes 'save as...' Postscript.
 *
 * Authors:
 *   Ted Gould <ted@gould.cx>
 *
 * Copyright (C) 2004 Authors
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "pov-out.h"
#include <inkscape.h>
#include <sp-path.h>
#include <display/curve.h>
#include <libnr/n-art-bpath.h>
#include <extension/system.h>
#include <extension/db.h>

#include <stdio.h>
#include <vector>
#include <string.h>

namespace Inkscape {
namespace Extension {
namespace Internal {

bool
PovOutput::check (Inkscape::Extension::Extension * module)
{
	//if (NULL == Inkscape::Extension::db.get(SP_MODULE_KEY_PRINT_PS))
	//	return FALSE;

	return TRUE;
}

static void
findElementsByTagName(std::vector<SPRepr *> &results, SPRepr *node, const char *name)
{

    if (strcmp(sp_repr_name(node), name) == 0)
        results.push_back(node);

    for (SPRepr *child = node->children; child ; child = child->next)
        findElementsByTagName ( results, child, name );

}


/**
 * Saves the <paths> of an Inkscape SVG file as PovRay spline definitions
*/
void
PovOutput::save (Inkscape::Extension::Output *mod, SPDocument *doc, const gchar *uri)
{
    std::vector<SPRepr *>results;
    findElementsByTagName(results, SP_ACTIVE_DOCUMENT->rroot, "path");
    if (results.size() == 0)
        return;
    FILE *f = fopen(uri, "w");
    if (!f)
        return;

    unsigned int indx;
    for (indx = 0; indx < results.size() ; indx++)
        {
        SPRepr *rpath = results[indx];
        gchar *id  = (gchar *)sp_repr_attr(rpath, "id");
        SPObject *reprobj = SP_ACTIVE_DOCUMENT->getObjectByRepr(rpath);
        if (!reprobj)
            continue;
        if (!SP_IS_PATH(reprobj))
            continue;
        SPPath *opath = SP_PATH(reprobj);
        SPCurve *curve = SP_SHAPE(opath)->curve; 
        if (sp_curve_empty(curve))
            continue;
        int curveNr;
        NArtBpath *bp = curve->bpath;
        for (curveNr=0 ; curveNr<curve->length ; curveNr++, bp++)
            {
            g_message("Code:%d [%f,%f]  [%f,%f]  [%f,%f]\n", bp->code,
               bp->x1, bp->y1, bp->x2, bp->y2, bp->x3, bp->y3);
            }
        }
    fclose(f);
}

/**
	\brief   A function allocate a copy of this function.

	This is the definition of postscript out.  This function just
	calls the extension system with the memory allocated XML that
	describes the data.
*/
void
PovOutput::init (void)
{
	Inkscape::Extension::build_from_mem(
		"<inkscape-extension>\n"
			"<name>PovRay Output</name>\n"
			"<id>org.inkscape.output.pov</id>\n"
			"<output>\n"
				"<extension>.pov</extension>\n"
				"<mimetype>text/x-povray-script</mimetype>\n"
				"<filetypename>PovRay (*.pov)</filetypename>\n"
				"<filetypetooltip>PovRay File (export curves)</filetypetooltip>\n"
			"</output>\n"
		"</inkscape-extension>", new PovOutput());

	return;
}

};};}; /* namespace Inkscape, Extension, Internal */
