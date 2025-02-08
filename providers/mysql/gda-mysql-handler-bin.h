/*
 * Copyright (C) 2006 - 2011 Vivien Malerba <malerba@gnome-db.org>
 * Copyright (C) 2007 Murray Cumming <murrayc@murrayc.com>
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

#ifndef __GDA_MYSQL_HANDLER_BIN__
#define __GDA_MYSQL_HANDLER_BIN__

#include <glib-object.h>
#include <libgda/gda-data-handler.h>

G_BEGIN_DECLS

#define GDA_TYPE_MYSQL_HANDLER_BIN          (_gda_mysql_handler_bin_get_type())
#define GDA_MYSQL_HANDLER_BIN(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, _gda_mysql_handler_bin_get_type(), GdaMysqlHandlerBin)
#define GDA_MYSQL_HANDLER_BIN_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, _gda_mysql_handler_bin_get_type (), GdaMysqlHandlerBinClass)
#define GDA_IS_MYSQL_HANDLER_BIN(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, _gda_mysql_handler_bin_get_type ())


typedef struct _GdaMysqlHandlerBin      GdaMysqlHandlerBin;
typedef struct _GdaMysqlHandlerBinClass GdaMysqlHandlerBinClass;
typedef struct _GdaMysqlHandlerBinPriv  GdaMysqlHandlerBinPriv;


/* struct for the object's data */
struct _GdaMysqlHandlerBin
{
	GObject                   object;

	GdaMysqlHandlerBinPriv  *priv;
};

/* struct for the object's class */
struct _GdaMysqlHandlerBinClass
{
	GObjectClass              parent_class;
};


GType           _gda_mysql_handler_bin_get_type      (void) G_GNUC_CONST;
GdaDataHandler *_gda_mysql_handler_bin_new           (void);

G_END_DECLS

#endif
