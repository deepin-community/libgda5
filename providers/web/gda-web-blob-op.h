/*
 * Copyright (C) 2008 - 2011 Vivien Malerba <malerba@gnome-db.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#ifndef __GDA_WEB_BLOB_OP_H__
#define __GDA_WEB_BLOB_OP_H__

#include <libgda/gda-blob-op.h>

G_BEGIN_DECLS

#define GDA_TYPE_WEB_BLOB_OP            (gda_web_blob_op_get_type())
#define GDA_WEB_BLOB_OP(obj)            (G_TYPE_CHECK_INSTANCE_CAST (obj, GDA_TYPE_WEB_BLOB_OP, GdaWebBlobOp))
#define GDA_WEB_BLOB_OP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST (klass, GDA_TYPE_WEB_BLOB_OP, GdaWebBlobOpClass))
#define GDA_IS_WEB_BLOB_OP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE (obj, GDA_TYPE_WEB_BLOB_OP))
#define GDA_IS_WEB_BLOB_OP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDA_TYPE_WEB_BLOB_OP))

typedef struct _GdaWebBlobOp        GdaWebBlobOp;
typedef struct _GdaWebBlobOpClass   GdaWebBlobOpClass;
typedef struct _GdaWebBlobOpPrivate GdaWebBlobOpPrivate;

struct _GdaWebBlobOp {
	GdaBlobOp             parent;
	GdaWebBlobOpPrivate *priv;
};

struct _GdaWebBlobOpClass {
	GdaBlobOpClass        parent_class;
};

GType         gda_web_blob_op_get_type     (void) G_GNUC_CONST;
GdaBlobOp    *gda_web_blob_op_new          (GdaConnection *cnc);

/* TO_ADD: more convenient API to create a GdaBlobOp with some specific information as argument */

G_END_DECLS

#endif

