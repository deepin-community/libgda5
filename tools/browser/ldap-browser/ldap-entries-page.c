/*
 * Copyright (C) 2011 Murray Cumming <murrayc@murrayc.com>
 * Copyright (C) 2011 - 2012 Vivien Malerba <malerba@gnome-db.org>
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

#include <glib/gi18n-lib.h>
#include <string.h>
#include "ldap-entries-page.h"
#include "hierarchy-view.h"
#include "entry-properties.h"
#include "../dnd.h"
#include "../support.h"
#include "../gdaui-bar.h"
#include "../browser-page.h"
#include "../browser-stock-icons.h"
#include "../browser-window.h"
#include "../browser-connection.h"
#include <virtual/gda-ldap-connection.h>
#include "mgr-ldap-entries.h"
#include <libgda-ui/gdaui-tree-store.h>
#include "ldap-browser-perspective.h"
#include <libgda/gda-debug-macros.h>

typedef struct {
	gchar *dn;
	GtkTreeRowReference *reference;
} HistoryItem;

static void
history_item_free (HistoryItem *item)
{
	g_free (item->dn);
	gtk_tree_row_reference_free (item->reference);
	g_free (item);
}

struct _LdapEntriesPagePrivate {
	BrowserConnection *bcnc;

	GtkWidget *entries_view;
	GtkWidget *entry_props;

	GtkActionGroup *agroup;
	GArray *history_items; /* array of @HistoryItem */
	guint history_max_len; /* max allowed length of @history_items */
	gint current_hitem; /* index in @history_items, or -1 */
	gboolean add_hist_item;
};

static void ldap_entries_page_class_init (LdapEntriesPageClass *klass);
static void ldap_entries_page_init       (LdapEntriesPage *ebrowser, LdapEntriesPageClass *klass);
static void ldap_entries_page_dispose   (GObject *object);

/* BrowserPage interface */
static void                 ldap_entries_page_page_init (BrowserPageIface *iface);
static GtkActionGroup      *ldap_entries_page_page_get_actions_group (BrowserPage *page);
static const gchar         *ldap_entries_page_page_get_actions_ui (BrowserPage *page);
static GtkWidget           *ldap_entries_page_page_get_tab_label (BrowserPage *page, GtkWidget **out_close_button);

static GObjectClass *parent_class = NULL;


/*
 * LdapEntriesPage class implementation
 */

static void
ldap_entries_page_class_init (LdapEntriesPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = ldap_entries_page_dispose;
}

static void
ldap_entries_page_page_init (BrowserPageIface *iface)
{
	iface->i_get_actions_group = ldap_entries_page_page_get_actions_group;
	iface->i_get_actions_ui = ldap_entries_page_page_get_actions_ui;
	iface->i_get_tab_label = ldap_entries_page_page_get_tab_label;
}

static void
ldap_entries_page_init (LdapEntriesPage *ebrowser, G_GNUC_UNUSED LdapEntriesPageClass *klass)
{
	ebrowser->priv = g_new0 (LdapEntriesPagePrivate, 1);
	ebrowser->priv->history_items = g_array_new (FALSE, FALSE, sizeof (HistoryItem*));
	ebrowser->priv->history_max_len = 20;
	ebrowser->priv->add_hist_item = TRUE;

	gtk_orientable_set_orientation (GTK_ORIENTABLE (ebrowser), GTK_ORIENTATION_VERTICAL);
}

static void
ldap_entries_page_dispose (GObject *object)
{
	LdapEntriesPage *ebrowser = (LdapEntriesPage *) object;

	/* free memory */
	if (ebrowser->priv) {
		if (ebrowser->priv->bcnc)
			g_object_unref (ebrowser->priv->bcnc);
		if (ebrowser->priv->agroup)
			g_object_unref (ebrowser->priv->agroup);
		if (ebrowser->priv->history_items) {
			guint i;
			for (i = 0; i < ebrowser->priv->history_items->len; i++) {
				HistoryItem *hitem = g_array_index (ebrowser->priv->history_items,
								    HistoryItem*, i);
				history_item_free (hitem);
			}
			g_array_free (ebrowser->priv->history_items, TRUE);
		}
		g_free (ebrowser->priv);
		ebrowser->priv = NULL;
	}

	parent_class->dispose (object);
}

GType
ldap_entries_page_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo info = {
			sizeof (LdapEntriesPageClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) ldap_entries_page_class_init,
			NULL,
			NULL,
			sizeof (LdapEntriesPage),
			0,
			(GInstanceInitFunc) ldap_entries_page_init,
			0
		};

		static GInterfaceInfo page_info = {
                        (GInterfaceInitFunc) ldap_entries_page_page_init,
			NULL,
                        NULL
                };

		type = g_type_register_static (GTK_TYPE_BOX, "LdapEntriesPage", &info, 0);
		g_type_add_interface_static (type, BROWSER_PAGE_TYPE, &page_info);
	}
	return type;
}

static gchar *
ldap_entries_page_to_selection (G_GNUC_UNUSED LdapEntriesPage *ebrowser)
{
	const gchar *current_dn;
	current_dn = hierarchy_view_get_current_dn (HIERARCHY_VIEW (ebrowser->priv->entries_view), NULL);
	if (current_dn)
		return g_strdup (current_dn);
	else
		return NULL;
}

static void
source_drag_data_get_cb (G_GNUC_UNUSED GtkWidget *widget, G_GNUC_UNUSED GdkDragContext *context,
			 GtkSelectionData *selection_data,
			 guint browser, G_GNUC_UNUSED guint time, LdapEntriesPage *ebrowser)
{
	switch (browser) {
	case TARGET_KEY_VALUE: {
		gchar *str;
		str = ldap_entries_page_to_selection (ebrowser);
		gtk_selection_data_set (selection_data,
					gtk_selection_data_get_target (selection_data), 8, (guchar*) str,
					strlen (str));
		g_free (str);
		break;
	}
	default:
	case TARGET_PLAIN: {
		gtk_selection_data_set_text (selection_data, ldap_entries_page_get_current_dn (ebrowser), -1);
		break;
	}
	case TARGET_ROOTWIN:
		TO_IMPLEMENT; /* dropping on the Root Window => create a file */
		break;
	}
}

static void
update_history_actions (LdapEntriesPage *ebrowser)
{
	if (!ebrowser->priv->agroup)
		return;

	gboolean is_first = TRUE;
	gboolean is_last = TRUE;
	const gchar *current_dn;
	ebrowser->priv->current_hitem = -1;
	current_dn = ldap_entries_page_get_current_dn (ebrowser);
	if (current_dn) {
		guint i;
		for (i = 0; i < ebrowser->priv->history_items->len; i++) {
			HistoryItem *hitem;
			hitem = g_array_index (ebrowser->priv->history_items, HistoryItem*, i);
			if (!strcmp (hitem->dn, current_dn)) {
				if (i != 0)
					is_first = FALSE;
				if (i+1 < ebrowser->priv->history_items->len)
					is_last = FALSE;
				ebrowser->priv->current_hitem = (gint) i;
				break;
			}
		}
	}

	GtkAction *action;
	action = gtk_action_group_get_action (ebrowser->priv->agroup, "DnBack");
	gtk_action_set_sensitive (action, !is_first);
	action = gtk_action_group_get_action (ebrowser->priv->agroup, "DnForward");
	gtk_action_set_sensitive (action, !is_last);
}

static void
selection_changed_cb (GtkTreeSelection *sel, LdapEntriesPage *ebrowser)
{
	const gchar *current_dn;
	current_dn = hierarchy_view_get_current_dn (HIERARCHY_VIEW (ebrowser->priv->entries_view), NULL);
	entry_properties_set_dn (ENTRY_PROPERTIES (ebrowser->priv->entry_props), current_dn);

	if (ebrowser->priv->agroup) {
		GtkAction *action;
		action = gtk_action_group_get_action (ebrowser->priv->agroup, "AddToFav");
		current_dn = ldap_entries_page_get_current_dn (ebrowser);
		gtk_action_set_sensitive (action, (current_dn && *current_dn) ? TRUE : FALSE);
	}

	GtkTreeModel *model;
	GtkTreeIter iter;
	if (ebrowser->priv->add_hist_item && gtk_tree_selection_get_selected (sel, &model, &iter)) {
		GtkTreePath *path;
		HistoryItem *hitem;
		hitem = g_new (HistoryItem, 1);
		hitem->dn = g_strdup (current_dn);
		
		path = gtk_tree_model_get_path (model, &iter);
		if (path) {
			hitem->reference = gtk_tree_row_reference_new (model, path);
			gtk_tree_path_free (path);
		}
		else
			hitem->reference = NULL;

		if (ebrowser->priv->current_hitem >= 0) {
			guint i;
			for (i = (guint) ebrowser->priv->current_hitem + 1;
			     i < ebrowser->priv->history_items->len; ) {
				HistoryItem *tmphitem;
				tmphitem = g_array_index (ebrowser->priv->history_items, HistoryItem*, i);
				history_item_free (tmphitem);
				g_array_remove_index (ebrowser->priv->history_items, i);
			}
		}
		g_array_append_val (ebrowser->priv->history_items, hitem);
		if (ebrowser->priv->history_items->len > ebrowser->priv->history_max_len) {
			hitem = g_array_index (ebrowser->priv->history_items, HistoryItem*, 0);
			history_item_free (hitem);
			g_array_remove_index (ebrowser->priv->history_items, 0);
		}
		ebrowser->priv->current_hitem = ebrowser->priv->history_items->len -1;
	}
	update_history_actions (ebrowser);
}

static void
open_dn_requested_cb (G_GNUC_UNUSED EntryProperties *eprop, const gchar *dn, LdapEntriesPage *ebrowser)
{
	ldap_entries_page_set_current_dn (ebrowser, dn);
}

static void
open_class_requested_cb (G_GNUC_UNUSED EntryProperties *eprop, const gchar *classname, LdapEntriesPage *ebrowser)
{
	BrowserPerspective *bpers;
	bpers = browser_page_get_perspective (BROWSER_PAGE (ebrowser));
	ldap_browser_perspective_display_ldap_class (LDAP_BROWSER_PERSPECTIVE (bpers), classname);
}

/**
 * ldap_entries_page_new:
 *
 * Returns: a new #GtkWidget
 */
GtkWidget *
ldap_entries_page_new (BrowserConnection *bcnc, const gchar *dn)
{
	LdapEntriesPage *ebrowser;

	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);

	ebrowser = LDAP_ENTRIES_PAGE (g_object_new (LDAP_ENTRIES_PAGE_TYPE, NULL));
	ebrowser->priv->bcnc = g_object_ref ((GObject*) bcnc);

	/* header */
        GtkWidget *label;
	gchar *str;

	str = g_strdup_printf ("<b>%s</b>", _("LDAP entries browser"));
	label = gdaui_bar_new (str);
	g_free (str);
        gtk_box_pack_start (GTK_BOX (ebrowser), label, FALSE, FALSE, 0);
        gtk_widget_show (label);
	g_signal_connect (label, "drag-data-get",
			  G_CALLBACK (source_drag_data_get_cb), ebrowser);

	/* VPaned widget */
	GtkWidget *hp;
	hp = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start (GTK_BOX (ebrowser), hp, TRUE, TRUE, 0);

	/* tree */
	GtkWidget *vbox, *hview, *sw;
	gfloat yalign;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, FALSE);
	gtk_paned_add1 (GTK_PANED (hp), vbox);
	
	str = g_strdup_printf ("<b>%s:</b>", _("LDAP hierarchy"));
	label = gtk_label_new ("");
	gtk_label_set_markup (GTK_LABEL (label), str);
	g_free (str);
        gtk_misc_get_alignment (GTK_MISC (label), NULL, &yalign);
        gtk_misc_set_alignment (GTK_MISC (label), 0., yalign);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

	hview = hierarchy_view_new (bcnc, dn);
	ebrowser->priv->entries_view = hview;
	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (sw), hview);
	gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);

	/* tree selection */
	GtkTreeSelection *sel;
	sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (ebrowser->priv->entries_view));
	gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);
	g_signal_connect (sel, "changed",
			  G_CALLBACK (selection_changed_cb), ebrowser);

	/* details */
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, FALSE);
	gtk_paned_add2 (GTK_PANED (hp), vbox);

	str = g_strdup_printf ("<b>%s:</b>", _("LDAP entry's details"));
	label = gtk_label_new ("");
	gtk_label_set_markup (GTK_LABEL (label), str);
	g_free (str);
        gtk_misc_get_alignment (GTK_MISC (label), NULL, &yalign);
        gtk_misc_set_alignment (GTK_MISC (label), 0., yalign);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

	GtkWidget *props;
	props = entry_properties_new (bcnc);
	gtk_box_pack_start (GTK_BOX (vbox), props, TRUE, TRUE, 0);
	ebrowser->priv->entry_props = props;
	g_signal_connect (props, "open-dn",
			  G_CALLBACK (open_dn_requested_cb), ebrowser);
	g_signal_connect (props, "open-class",
			  G_CALLBACK (open_class_requested_cb), ebrowser);

	gtk_paned_set_position (GTK_PANED (hp), 250);

	gtk_widget_show_all (hp);

	return (GtkWidget*) ebrowser;
}

/**
 * ldap_entries_page_get_current_dn:
 */
const gchar *
ldap_entries_page_get_current_dn (LdapEntriesPage *ldap_entries_page)
{
	g_return_val_if_fail (IS_LDAP_ENTRIES_PAGE (ldap_entries_page), NULL);
	return hierarchy_view_get_current_dn (HIERARCHY_VIEW (ldap_entries_page->priv->entries_view), NULL);
}

/**
 * ldap_entries_page_set_current_dn:
 */
void
ldap_entries_page_set_current_dn (LdapEntriesPage *ldap_entries_page, const gchar *dn)
{
	g_return_if_fail (IS_LDAP_ENTRIES_PAGE (ldap_entries_page));
	hierarchy_view_set_current_dn (HIERARCHY_VIEW (ldap_entries_page->priv->entries_view), dn);
}


/*
 * UI actions
 */
static void
action_add_to_fav_cb (G_GNUC_UNUSED GtkAction *action, LdapEntriesPage *ebrowser)
{
	ToolsFavorites *bfav;
        ToolsFavoritesAttributes fav;
        GError *error = NULL;
	const gchar *cn;

	hierarchy_view_get_current_dn (HIERARCHY_VIEW (ebrowser->priv->entries_view), &cn);
        memset (&fav, 0, sizeof (ToolsFavoritesAttributes));
        fav.id = -1;
        fav.type = GDA_TOOLS_FAVORITES_LDAP_DN;
        fav.name = ldap_entries_page_to_selection (ebrowser);
        fav.descr = (gchar*) cn;
        fav.contents = ldap_entries_page_to_selection (ebrowser);

        bfav = browser_connection_get_favorites (ebrowser->priv->bcnc);
        if (! gda_tools_favorites_add (bfav, 0, &fav, ORDER_KEY_LDAP, G_MAXINT, &error)) {
                browser_show_error ((GtkWindow*) gtk_widget_get_toplevel ((GtkWidget*) ebrowser),
                                    _("Could not add favorite: %s"),
                                    error && error->message ? error->message : _("No detail"));
                if (error)
                        g_error_free (error);
        }
	g_free (fav.contents);
}

static void
action_dn_back_cb (G_GNUC_UNUSED GtkAction *action, LdapEntriesPage *ebrowser)
{
	ebrowser->priv->add_hist_item = FALSE;
	if (ebrowser->priv->current_hitem > 0) {
		HistoryItem *hitem = NULL;
		gboolean use_dn = TRUE;
		hitem = g_array_index (ebrowser->priv->history_items, HistoryItem*,
				       ebrowser->priv->current_hitem - 1);
		if (hitem->reference) {
			if (gtk_tree_row_reference_valid (hitem->reference)) {
				GtkTreeSelection *sel;
				GtkTreePath *path;
				path = gtk_tree_row_reference_get_path (hitem->reference);
				sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (ebrowser->priv->entries_view));
				gtk_tree_selection_select_path (sel, path);
				gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (ebrowser->priv->entries_view),
							      path, NULL, TRUE, 0.5, 0.5);
				gtk_tree_path_free (path);
				use_dn = FALSE;
			}
			else {
				gtk_tree_row_reference_free (hitem->reference);
				hitem->reference = NULL;
			}
		}
		if (use_dn)
			hierarchy_view_set_current_dn (HIERARCHY_VIEW (ebrowser->priv->entries_view),
						       hitem->dn);
	}
	ebrowser->priv->add_hist_item = TRUE;
}

static void
action_dn_forward_cb (G_GNUC_UNUSED GtkAction *action, LdapEntriesPage *ebrowser)
{
	ebrowser->priv->add_hist_item = FALSE;
	if ((ebrowser->priv->current_hitem >=0) &&
	    ((guint) ebrowser->priv->current_hitem < ebrowser->priv->history_items->len)) {
		HistoryItem *hitem = NULL;
		gboolean use_dn = TRUE;
		hitem = g_array_index (ebrowser->priv->history_items, HistoryItem*,
				       ebrowser->priv->current_hitem + 1);
		if (hitem->reference) {
			if (gtk_tree_row_reference_valid (hitem->reference)) {
				GtkTreeSelection *sel;
				GtkTreePath *path;
				path = gtk_tree_row_reference_get_path (hitem->reference);
				sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (ebrowser->priv->entries_view));
				gtk_tree_selection_select_path (sel, path);
				gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (ebrowser->priv->entries_view),
							      path, NULL, TRUE, 0.5, 0.5);
				gtk_tree_path_free (path);
				use_dn = FALSE;
			}
			else {
				gtk_tree_row_reference_free (hitem->reference);
				hitem->reference = NULL;
			}
		}
		if (use_dn)
			hierarchy_view_set_current_dn (HIERARCHY_VIEW (ebrowser->priv->entries_view),
						       hitem->dn);
	}
	ebrowser->priv->add_hist_item = TRUE;
}

static GtkActionEntry ui_actions[] = {
	{ "LDAP", NULL, N_("_LDAP"), NULL, N_("LDAP"), NULL },
	{ "AddToFav", STOCK_ADD_BOOKMARK, N_("Add to _Favorites"), NULL, N_("Add entry to favorites"),
	  G_CALLBACK (action_add_to_fav_cb)},
	{ "DnBack", GTK_STOCK_GO_BACK, N_("Previous Entry"), NULL, N_("Move back to previous LDAP entry"),
	  G_CALLBACK (action_dn_back_cb)},
	{ "DnForward", GTK_STOCK_GO_FORWARD, N_("Next Entry"), NULL, N_("Move to next LDAP entry"),
	  G_CALLBACK (action_dn_forward_cb)},
};
static const gchar *ui_actions_browser =
	"<ui>"
	"  <menubar name='MenuBar'>"
	"    <placeholder name='MenuExtension'>"
        "      <menu name='LDAP' action='LDAP'>"
        "        <menuitem name='AddToFav' action= 'AddToFav'/>"
	"        <separator/>"
        "        <menuitem name='DnBack' action= 'DnBack'/>"
        "        <menuitem name='DnForward' action= 'DnForward'/>"
        "      </menu>"
        "    </placeholder>"
	"  </menubar>"
	"  <toolbar name='ToolBar'>"
	"    <separator/>"
	"    <toolitem action='AddToFav'/>"
	"    <toolitem action='DnBack'/>"
	"    <toolitem action='DnForward'/>"
	"  </toolbar>"
	"</ui>";

static GtkActionGroup *
ldap_entries_page_page_get_actions_group (BrowserPage *page)
{
	LdapEntriesPage *ebrowser;

	ebrowser = LDAP_ENTRIES_PAGE (page);
	if (! ebrowser->priv->agroup) {
		GtkActionGroup *agroup;
		agroup = gtk_action_group_new ("LdapLdapEntriesPageActions");
		gtk_action_group_set_translation_domain (agroup, GETTEXT_PACKAGE);
		gtk_action_group_add_actions (agroup, ui_actions, G_N_ELEMENTS (ui_actions), page);
		ebrowser->priv->agroup = agroup;

		GtkAction *action;
		const gchar *current_dn;
		action = gtk_action_group_get_action (agroup, "AddToFav");
		current_dn = ldap_entries_page_get_current_dn (ebrowser);
		gtk_action_set_sensitive (action, (current_dn && *current_dn) ? TRUE : FALSE);

		update_history_actions (ebrowser);
	}
	
	return g_object_ref (ebrowser->priv->agroup);
}

static const gchar *
ldap_entries_page_page_get_actions_ui (G_GNUC_UNUSED BrowserPage *page)
{
	return ui_actions_browser;
}

static GtkWidget *
ldap_entries_page_page_get_tab_label (BrowserPage *page, GtkWidget **out_close_button)
{
	const gchar *tab_name;
	GdkPixbuf *entries_pixbuf;

	entries_pixbuf = browser_get_pixbuf_icon (BROWSER_ICON_LDAP_ORGANIZATION);
	tab_name = _("LDAP entries");
	return browser_make_tab_label_with_pixbuf (tab_name,
						   entries_pixbuf,
						   out_close_button ? TRUE : FALSE, out_close_button);
}
