/*
 * Utility functions for reading and setting preferences
 *
 * Authors:
 *   bulia byak <bulia@dr.com>
 *
 * Copyright (C) 2003 authors
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include <string.h>

#include <config.h>

#include "inkscape.h"
#include "xml/repr.h"

#include "prefs-utils.h"

void
prefs_set_int_attribute(gchar const *path, gchar const *attr, gint value)
{
    Inkscape::XML::Node *repr = inkscape_get_repr(INKSCAPE, path);
    if (repr) {
        sp_repr_set_int(repr, attr, value);
    }
}

gint
prefs_get_int_attribute(gchar const *path, gchar const *attr, gint def)
{
    Inkscape::XML::Node *repr = inkscape_get_repr(INKSCAPE, path);
    if (repr) {
        return sp_repr_get_int_attribute(repr, attr, def);
    } else {
        return def;
    }
}

/**
\brief Retrieves an int attribute guarding against screwed-up data; if the value is beyond limits, default is returned
*/
gint
prefs_get_int_attribute_limited(gchar const *path, gchar const *attr, gint def, gint min, gint max)
{
    Inkscape::XML::Node *repr = inkscape_get_repr(INKSCAPE, path);
    if (repr) {
        gint const v = sp_repr_get_int_attribute(repr, attr, def);
        if (v >= min && v <= max) {
            return v;
        } else {
            return def;
        }
    } else {
        return def;
    }
}

void
prefs_set_double_attribute(gchar const *path, gchar const *attr, double value)
{
    Inkscape::XML::Node *repr = inkscape_get_repr(INKSCAPE, path);
    if (repr) {
        sp_repr_set_double(repr, attr, value);
    }
}

double
prefs_get_double_attribute(gchar const *path, gchar const *attr, double def)
{
    Inkscape::XML::Node *repr = inkscape_get_repr(INKSCAPE, path);
    if (repr) {
        return sp_repr_get_double_attribute(repr, attr, def);
    } else {
        return def;
    }
}

/**
\brief Retrieves an int attribute guarding against screwed-up data; if the value is beyond limits, default is returned
*/
double
prefs_get_double_attribute_limited(gchar const *path, gchar const *attr, double def, double min, double max)
{
    Inkscape::XML::Node *repr = inkscape_get_repr(INKSCAPE, path);
    if (repr) {
        double const v = sp_repr_get_double_attribute(repr, attr, def);
        if (v >= min && v <= max) {
            return v;
        } else {
            return def;
        }
    } else {
        return def;
    }
}

gchar const *
prefs_get_string_attribute(gchar const *path, gchar const *attr)
{
    Inkscape::XML::Node *repr = inkscape_get_repr(INKSCAPE, path);
    if (repr) {
        return (char *) repr->attribute(attr);
    }
    return NULL;
}

void
prefs_set_string_attribute(gchar const *path, gchar const *attr, gchar const *value)
{
    Inkscape::XML::Node *repr = inkscape_get_repr(INKSCAPE, path);
    if (repr) {
        sp_repr_set_attr(repr, attr, value);
    }
}

void
prefs_set_recent_file(gchar const *uri, gchar const *name)
{
    unsigned const max_documents = prefs_get_int_attribute("options.maxrecentdocuments", "value", 20);

    if (uri != NULL) {
        Inkscape::XML::Node *recent = inkscape_get_repr(INKSCAPE, "documents.recent");
        if (recent) {
            Inkscape::XML::Node *child = sp_repr_lookup_child(recent, "uri", uri);
            if (child) {
                sp_repr_change_order(recent, child, NULL);
            } else {
                if (recent->childCount() >= max_documents) {
                    child = recent->firstChild();
                    // count to the last
                    for (unsigned i = 0; i + 2 < max_documents; ++i) {
                        child = child->next();
                    }
                    // remove all after the last
                    while (child->next()) {
                        sp_repr_unparent(child->next());
                    }
                }
                child = sp_repr_new("document");
                sp_repr_set_attr(child, "uri", uri);
                sp_repr_add_child(recent, child, NULL);
            }
            sp_repr_set_attr(child, "name", name);
        }
    }
}

gchar const **
prefs_get_recent_files()
{
    Inkscape::XML::Node *recent = inkscape_get_repr(INKSCAPE, "documents.recent");
    if (recent) {
        unsigned const docs = recent->childCount();
        gchar const **datalst = (gchar const **) g_malloc(sizeof(gchar *) * ((docs * 2) + 1));

        gint i;
        Inkscape::XML::Node *child;
        for (i = 0, child = recent->firstChild();
             child != NULL;
             child = child->next(), i += 2)
        {
            gchar const *uri = child->attribute("uri");
            gchar const *name = child->attribute("name");
            datalst[i]     = uri;
            datalst[i + 1] = name;
        }

        datalst[i] = NULL;
        return datalst;
    }

    return NULL;
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
