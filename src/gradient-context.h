#ifndef __SP_GRADIENT_CONTEXT_H__
#define __SP_GRADIENT_CONTEXT_H__

/*
 * Gradient drawing and editing tool
 *
 * Authors:
 *   bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 2005 Authors
 *
 * Released under GNU GPL
 */

#include <sigc++/sigc++.h>
#include "event-context.h"
#include "libnr/nr-point.h"
struct SPKnotHolder;

#define SP_TYPE_GRADIENT_CONTEXT            (sp_gradient_context_get_type ())
#define SP_GRADIENT_CONTEXT(obj)            (GTK_CHECK_CAST ((obj), SP_TYPE_GRADIENT_CONTEXT, SPGradientContext))
#define SP_GRADIENT_CONTEXT_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), SP_TYPE_GRADIENT_CONTEXT, SPGradientContextClass))
#define SP_IS_GRADIENT_CONTEXT(obj)         (GTK_CHECK_TYPE ((obj), SP_TYPE_GRADIENT_CONTEXT))
#define SP_IS_GRADIENT_CONTEXT_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), SP_TYPE_GRADIENT_CONTEXT))

class SPGradientContext;
class SPGradientContextClass;

struct SPGradientContext : public SPEventContext {
	SPItem *item;
	NR::Point center;

	SPKnotHolder *knot_holder;
	Inkscape::XML::Node *repr;
	
  	gdouble rx;	/* roundness radius (x direction) */
  	gdouble ry;	/* roundness radius (y direction) */

	sigc::connection sel_changed_connection;

	bool vector_created;

	NR::Point origin;

	Inkscape::MessageContext *_message_context;
};

struct SPGradientContextClass {
	SPEventContextClass parent_class;
};

/* Standard Gtk function */

GtkType sp_gradient_context_get_type (void);

#endif
