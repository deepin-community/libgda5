/*
 * Copyright (C) 2009 - 2011 Vivien Malerba <malerba@gnome-db.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __SUPPORT_H__
#define __SUPPORT_H__

#include <libgda/libgda.h>
#include <gtk/gtk.h>
#include "browser-connection.h"

#ifdef HAVE_MAC_INTEGRATION
#include <gtkosxapplication.h>
extern GtkOSXApplication *theApp;
#endif
#ifdef HAVE_LDAP
#include <libgda/sqlite/virtual/gda-ldap-connection.h>
#endif

G_BEGIN_DECLS

/**
 * SECTION:support
 * @short_description: Misc. functions for various situations
 * @title: Support functions
 * @stability: Stable
 * @see_also:
 */

BrowserConnection *browser_connection_open (GError **error);
gboolean           browser_connection_close (GtkWindow *parent, BrowserConnection *bcnc);
void               browser_show_error (GtkWindow *parent, const gchar *format, ...);
void               browser_show_message (GtkWindow *parent, const gchar *format, ...);
#ifdef HAVE_GDU
void               browser_show_help (GtkWindow *parent, const gchar *topic);
#endif

GtkWidget*         browser_make_tab_label_with_stock (const gchar *label,
						      const gchar *stock_id, gboolean with_close,
						      GtkWidget **out_close_button);
GtkWidget*         browser_make_tab_label_with_pixbuf (const gchar *label,
						       GdkPixbuf *pixbuf, gboolean with_close,
						       GtkWidget **out_close_button);

GtkWidget          *browser_make_small_button (gboolean is_toggle, gboolean with_arrow,
					       const gchar *label, const gchar *stock_id,
					       const gchar *tooltip);
GtkWidget          *browser_make_tree_view (GtkTreeModel *model);

/*
 * Widgets navigation
 */
GtkWidget          *browser_find_parent_widget (GtkWidget *current, GType requested_type);

/*
 * icons, see browser_get_pixbuf_icon() for the associated icons
 */
typedef enum {
	BROWSER_ICON_BOOKMARK,
	BROWSER_ICON_SCHEMA,
	BROWSER_ICON_TABLE,
	BROWSER_ICON_COLUMN,
	BROWSER_ICON_COLUMN_PK,
	BROWSER_ICON_COLUMN_FK,
	BROWSER_ICON_COLUMN_FK_NN,
	BROWSER_ICON_COLUMN_NN,
	BROWSER_ICON_REFERENCE,
	BROWSER_ICON_DIAGRAM,
	BROWSER_ICON_QUERY,
	BROWSER_ICON_ACTION,
	
	BROWSER_ICON_MENU_INDICATOR,

	BROWSER_ICON_LDAP_ENTRY,
	BROWSER_ICON_LDAP_GROUP,
	BROWSER_ICON_LDAP_ORGANIZATION,
	BROWSER_ICON_LDAP_PERSON,
	BROWSER_ICON_LDAP_CLASS_STRUCTURAL,
	BROWSER_ICON_LDAP_CLASS_ABSTRACT,
	BROWSER_ICON_LDAP_CLASS_AUXILIARY,
	BROWSER_ICON_LDAP_CLASS_UNKNOWN,

	BROWSER_ICON_LAST
} BrowserIconType;

GdkPixbuf          *browser_get_pixbuf_icon (BrowserIconType type);
#ifdef HAVE_LDAP
GdkPixbuf          *browser_get_pixbuf_for_ldap_class (GdaLdapClassKind kind);
const gchar        *browser_get_kind_for_ldap_class (GdaLdapClassKind kind);
#endif

/*
 * Connections list
 */
enum
{
	CNC_LIST_COLUMN_BCNC = 0,
	CNC_LIST_COLUMN_NAME = 1,
	CNC_LIST_NUM_COLUMNS
};
GdaDataModel       *browser_get_connections_list (void);

#define VARIABLES_HELP _("<small>This area allows to give values to\n" \
			 "variables defined in the SQL code\n"		\
			 "using the following syntax:\n"		\
			 "<b><tt>##&lt;variable name&gt;::&lt;type&gt;[::null]</tt></b>\n" \
			 "For example:\n"				\
			 "<span foreground=\"#4e9a06\"><b><tt>##id::int</tt></b></span>\n      defines <b>id</b> as a non NULL integer\n" \
			 "<span foreground=\"#4e9a06\"><b><tt>##age::string::null</tt></b></span>\n      defines <b>age</b> as a string\n\n" \
			 "Valid types are: <tt>string</tt>, <tt>boolean</tt>, <tt>int</tt>,\n" \
			 "<tt>date</tt>, <tt>time</tt>, <tt>timestamp</tt>, <tt>guint</tt>, <tt>blob</tt> and\n" \
			 "<tt>binary</tt></small>")

G_END_DECLS

#endif
