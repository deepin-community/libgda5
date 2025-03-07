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

#ifndef __GDAUI_ENTRY_COMBO__
#define __GDAUI_ENTRY_COMBO__

#include <gtk/gtk.h>
#include "gdaui-entry-shell.h"
#include <libgda-ui/gdaui-data-entry.h>
#include <libgda-ui/gdaui-set.h>

G_BEGIN_DECLS

#define GDAUI_TYPE_ENTRY_COMBO          (gdaui_entry_combo_get_type())
#define GDAUI_ENTRY_COMBO(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, gdaui_entry_combo_get_type(), GdauiEntryCombo)
#define GDAUI_ENTRY_COMBO_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, gdaui_entry_combo_get_type (), GdauiEntryComboClass)
#define GDAUI_IS_ENTRY_COMBO(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, gdaui_entry_combo_get_type ())


typedef struct _GdauiEntryCombo      GdauiEntryCombo;
typedef struct _GdauiEntryComboClass GdauiEntryComboClass;
typedef struct _GdauiEntryComboPriv  GdauiEntryComboPriv;


/* struct for the object's data */
struct _GdauiEntryCombo
{
	GdauiEntryShell        object;
	GdauiEntryComboPriv   *priv;
};

/* struct for the object's class */
struct _GdauiEntryComboClass
{
	GdauiEntryShellClass   parent_class;
};


GType           gdaui_entry_combo_get_type          (void) G_GNUC_CONST;
GtkWidget      *gdaui_entry_combo_new               (GdauiSet *paramlist, GdauiSetSource *source);

gboolean        gdaui_entry_combo_set_values        (GdauiEntryCombo *combo, GSList *values);
GSList         *gdaui_entry_combo_get_values        (GdauiEntryCombo *combo);
GSList         *gdaui_entry_combo_get_all_values    (GdauiEntryCombo *combo);
void            gdaui_entry_combo_set_reference_values (GdauiEntryCombo *combo, GSList *values);
GSList         *gdaui_entry_combo_get_reference_values (GdauiEntryCombo *combo);
void            gdaui_entry_combo_set_default_values (GdauiEntryCombo *combo, GSList *values);

G_END_DECLS

#endif
