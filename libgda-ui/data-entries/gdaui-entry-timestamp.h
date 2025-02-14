/*
 * Copyright (C) 2009 - 2011 Vivien Malerba <malerba@gnome-db.org>
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


#ifndef __GDAUI_ENTRY_TIMESTAMP_H_
#define __GDAUI_ENTRY_TIMESTAMP_H_

#include "gdaui-entry-common-time.h"

G_BEGIN_DECLS

#define GDAUI_TYPE_ENTRY_TIMESTAMP          (gdaui_entry_timestamp_get_type())
#define GDAUI_ENTRY_TIMESTAMP(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, gdaui_entry_timestamp_get_type(), GdauiEntryTimestamp)
#define GDAUI_ENTRY_TIMESTAMP_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, gdaui_entry_timestamp_get_type (), GdauiEntryTimestampClass)
#define GDAUI_IS_ENTRY_TIMESTAMP(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, gdaui_entry_timestamp_get_type ())


typedef struct _GdauiEntryTimestamp GdauiEntryTimestamp;
typedef struct _GdauiEntryTimestampClass GdauiEntryTimestampClass;
typedef struct _GdauiEntryTimestampPrivate GdauiEntryTimestampPrivate;


/* struct for the object's data */
struct _GdauiEntryTimestamp
{
	GdauiEntryCommonTime           object;
};

/* struct for the object's class */
struct _GdauiEntryTimestampClass
{
	GdauiEntryCommonTimeClass      parent_class;
};

GType        gdaui_entry_timestamp_get_type        (void) G_GNUC_CONST;
GtkWidget   *gdaui_entry_timestamp_new             (GdaDataHandler *dh);


G_END_DECLS

#endif
