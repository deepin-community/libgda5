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


#ifndef __GDAUI_DATA_PROXY_H_
#define __GDAUI_DATA_PROXY_H_

#include <glib-object.h>
#include <gtk/gtk.h>
#include <libgda/gda-decl.h>
#include "gdaui-decl.h"
#include "gdaui-enums.h"

G_BEGIN_DECLS

#define GDAUI_TYPE_DATA_PROXY          (gdaui_data_proxy_get_type())
#define GDAUI_DATA_PROXY(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, GDAUI_TYPE_DATA_PROXY, GdauiDataProxy)
#define GDAUI_IS_DATA_PROXY(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, GDAUI_TYPE_DATA_PROXY)
#define GDAUI_DATA_PROXY_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GDAUI_TYPE_DATA_PROXY, GdauiDataProxyIface))

/**
 * GdauiDataProxyWriteMode:
 * @GDAUI_DATA_PROXY_WRITE_ON_DEMAND: write only when explicitly requested 
 * @GDAUI_DATA_PROXY_WRITE_ON_ROW_CHANGE: write when the current selected row changes
 * @GDAUI_DATA_PROXY_WRITE_ON_VALUE_ACTIVATED: write when user activates a value change
 * @GDAUI_DATA_PROXY_WRITE_ON_VALUE_CHANGE: write when a parameters's value changes
 *
 * Defines when the data modifications held in the underlying #GdaDataProxy are written to the
 * data model being proxied (using gda_data_proxy_apply_row_changes()).
 */
typedef enum {
	GDAUI_DATA_PROXY_WRITE_ON_DEMAND           = 0,
	GDAUI_DATA_PROXY_WRITE_ON_ROW_CHANGE       = 1,
	GDAUI_DATA_PROXY_WRITE_ON_VALUE_ACTIVATED  = 2,
	GDAUI_DATA_PROXY_WRITE_ON_VALUE_CHANGE     = 3 
} GdauiDataProxyWriteMode;

/* struct for the interface */
struct _GdauiDataProxyIface
{
	GTypeInterface           g_iface;

	/* virtual table */
	GdaDataProxy        *(* get_proxy)           (GdauiDataProxy *iface);
	void                 (* set_column_editable) (GdauiDataProxy *iface, gint column, gboolean editable);
	void                 (* show_column_actions) (GdauiDataProxy *iface, gint column, gboolean show_actions);
	GtkActionGroup      *(* get_actions_group)   (GdauiDataProxy *iface);
	gboolean             (* set_write_mode)      (GdauiDataProxy *iface, GdauiDataProxyWriteMode mode);
	GdauiDataProxyWriteMode (* get_write_mode)(GdauiDataProxy *iface);

	/* signals */
	void                 (* proxy_changed)       (GdauiDataProxy *iface, GdaDataProxy *proxy);
};

/**
 * SECTION:gdaui-data-proxy
 * @short_description: Displaying and modifying data in a #GdaDataProxy
 * @title: GdauiDataProxy
 * @stability: Stable
 * @Image:
 * @see_also: The #GdauiDataSelector interface which is usually also implemented by the widgets which implement the #GdauiDataProxy interface.
 *
 * The #GdauiDataProxy interface is implemented by widgets which allow modifications
 * to a #GdaDataModel (through a #GdaDataProxy to actually proxy the changes before they
 * are written to the data model).
 */

GType             gdaui_data_proxy_get_type                  (void) G_GNUC_CONST;

GdaDataProxy     *gdaui_data_proxy_get_proxy                 (GdauiDataProxy *iface);
GtkActionGroup   *gdaui_data_proxy_get_actions_group         (GdauiDataProxy *iface);
void              gdaui_data_proxy_perform_action            (GdauiDataProxy *iface, GdauiAction action);

void              gdaui_data_proxy_column_set_editable       (GdauiDataProxy *iface, gint column, gboolean editable);
void              gdaui_data_proxy_column_show_actions       (GdauiDataProxy *iface, gint column, gboolean show_actions);

gboolean          gdaui_data_proxy_set_write_mode            (GdauiDataProxy *iface, GdauiDataProxyWriteMode mode);
GdauiDataProxyWriteMode gdaui_data_proxy_get_write_mode   (GdauiDataProxy *iface);

G_END_DECLS

#endif
