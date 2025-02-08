/*
 * Copyright (C) 2001 - 2002 Gonzalo Paniagua Javier <gonzalo@gnome-db.org>
 * Copyright (C) 2001 - 2002 Rodrigo Moya <rodrigo@gnome-db.org>
 * Copyright (C) 2003 Danilo Schoeneberg <dj@starfire-programming.net>
 * Copyright (C) 2003 Laurent Sansonetti <lrz@gnome.org>
 * Copyright (C) 2005 - 2011 Vivien Malerba <malerba@gnome-db.org>
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

#ifndef __COMMON_BIN_H__
#define __COMMON_BIN_H__

#include <gtk/gtk.h>
#include "../internal/popup-container.h"

typedef void (*BinCallback) (gpointer, GValue *);
typedef struct {
        GtkWidget    *popup; /* PopupContainer popup window */
        GtkWidget    *load_button;
        GtkWidget    *save_button;
	gchar        *current_folder;
	GtkWidget    *props_label;

	GType         entry_type;
	GValue       *tmpvalue;

	BinCallback   loaded_value_cb;
	gpointer      loaded_value_cb_data;
} BinMenu;


void         common_bin_create_menu (BinMenu *binmenu, PopupContainerPositionFunc pos_func, GType entry_type,
				     BinCallback loaded_value_cb, gpointer loaded_value_cb_data);
void         common_bin_adjust_menu (BinMenu *binmenu, gboolean editable, const GValue *value);
void         common_bin_reset (BinMenu *binmenu);

#endif
