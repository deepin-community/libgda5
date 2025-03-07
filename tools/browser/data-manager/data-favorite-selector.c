/*
 * Copyright (C) 2009 - 2012 Vivien Malerba <malerba@gnome-db.org>
 * Copyright (C) 2010 David King <davidk@openismus.com>
 * Copyright (C) 2011 Murray Cumming <murrayc@murrayc.com>
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
#include <gtk/gtk.h>
#include <libgda/gda-tree.h>
#include "data-favorite-selector.h"
#include "../mgr-favorites.h"
#include <libgda-ui/gdaui-tree-store.h>
#include "../dnd.h"
#include "../support.h"
#include "marshal.h"
#include "../gdaui-bar.h"
#include <gdk/gdkkeysyms.h>
#include <libgda-ui/internal/popup-container.h>

#ifdef HAVE_GTKSOURCEVIEW
  #ifdef GTK_DISABLE_SINGLE_INCLUDES
    #undef GTK_DISABLE_SINGLE_INCLUDES
  #endif

  #include <gtksourceview/gtksourceview.h>
  #include <gtksourceview/gtksourcelanguagemanager.h>
  #include <gtksourceview/gtksourcebuffer.h>
  #include <gtksourceview/gtksourcestyleschememanager.h>
  #include <gtksourceview/gtksourcestylescheme.h>
#endif

struct _DataFavoriteSelectorPrivate {
	BrowserConnection *bcnc;
	GdaTree *tree;
	GtkWidget *treeview;
	guint idle_update_favorites;

	GtkWidget *popup_menu;
	GtkWidget *popup_properties;
	GtkWidget *properties_name;
	GtkWidget *properties_text;
	gint       properties_id;
	gint       properties_position;
	guint      prop_save_timeout;
};

static void data_favorite_selector_class_init (DataFavoriteSelectorClass *klass);
static void data_favorite_selector_init       (DataFavoriteSelector *tsel,
				       DataFavoriteSelectorClass *klass);
static void data_favorite_selector_dispose   (GObject *object);

static void favorites_changed_cb (ToolsFavorites *bfav, DataFavoriteSelector *tsel);

enum {
	SELECTION_CHANGED,
	LAST_SIGNAL
};

static guint data_favorite_selector_signals[LAST_SIGNAL] = { 0 };
static GObjectClass *parent_class = NULL;

/* columns of the resulting GtkTreeModel */
enum {
	COLUMN_POSITION = 0,
	COLUMN_ICON = 1,
	COLUMN_CONTENTS = 2,
	COLUMN_TYPE = 3,
	COLUMN_ID = 4,
	COLUMN_NAME = 5,
};


/*
 * DataFavoriteSelector class implementation
 */

static void
data_favorite_selector_class_init (DataFavoriteSelectorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	/* signals */
	data_favorite_selector_signals [SELECTION_CHANGED] =
                g_signal_new ("selection-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (DataFavoriteSelectorClass, selection_changed),
                              NULL, NULL,
                              _dm_marshal_VOID__INT_ENUM_STRING, G_TYPE_NONE,
                              3, G_TYPE_INT, G_TYPE_UINT, G_TYPE_STRING);
	klass->selection_changed = NULL;

	object_class->dispose = data_favorite_selector_dispose;
}


static void
data_favorite_selector_init (DataFavoriteSelector *tsel, G_GNUC_UNUSED DataFavoriteSelectorClass *klass)
{
	tsel->priv = g_new0 (DataFavoriteSelectorPrivate, 1);
	tsel->priv->idle_update_favorites = 0;
	tsel->priv->prop_save_timeout = 0;

	gtk_orientable_set_orientation (GTK_ORIENTABLE (tsel), GTK_ORIENTATION_VERTICAL);
}

static void
data_favorite_selector_dispose (GObject *object)
{
	DataFavoriteSelector *tsel = (DataFavoriteSelector *) object;

	/* free memory */
	if (tsel->priv) {
		if (tsel->priv->idle_update_favorites != 0)
			g_source_remove (tsel->priv->idle_update_favorites);
		if (tsel->priv->tree)
			g_object_unref (tsel->priv->tree);

		if (tsel->priv->bcnc) {
			g_signal_handlers_disconnect_by_func (browser_connection_get_favorites (tsel->priv->bcnc),
							      G_CALLBACK (favorites_changed_cb), tsel);
			g_object_unref (tsel->priv->bcnc);
		}
		
		if (tsel->priv->popup_properties)
			gtk_widget_destroy (tsel->priv->popup_properties);
		if (tsel->priv->popup_menu)
			gtk_widget_destroy (tsel->priv->popup_menu);
		if (tsel->priv->prop_save_timeout)
			g_source_remove (tsel->priv->prop_save_timeout);

		g_free (tsel->priv);
		tsel->priv = NULL;
	}

	parent_class->dispose (object);
}

GType
data_favorite_selector_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo info = {
			sizeof (DataFavoriteSelectorClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) data_favorite_selector_class_init,
			NULL,
			NULL,
			sizeof (DataFavoriteSelector),
			0,
			(GInstanceInitFunc) data_favorite_selector_init,
			0
		};
		type = g_type_register_static (GTK_TYPE_BOX, "DataFavoriteSelector",
					       &info, 0);
	}
	return type;
}

static gboolean
key_press_event_cb (GtkTreeView *treeview, GdkEventKey *event, DataFavoriteSelector *tsel)
{
	if (event->keyval == GDK_KEY_Delete) {
		GtkTreeModel *model;
		GtkTreeSelection *select;
		GtkTreeIter iter;
		
		select = gtk_tree_view_get_selection (treeview);
		if (gtk_tree_selection_get_selected (select, &model, &iter)) {
			ToolsFavorites *bfav;
			ToolsFavoritesAttributes fav;
			GError *lerror = NULL;

			memset (&fav, 0, sizeof (ToolsFavoritesAttributes));
			gtk_tree_model_get (model, &iter,
					    COLUMN_ID, &(fav.id), -1);
			bfav = browser_connection_get_favorites (tsel->priv->bcnc);
			if (!gda_tools_favorites_delete (bfav, 0, &fav, NULL)) {
				browser_show_error ((GtkWindow*) gtk_widget_get_toplevel ((GtkWidget*)tsel),
						    _("Could not remove favorite: %s"),
						    lerror && lerror->message ? lerror->message : _("No detail"));
				if (lerror)
					g_error_free (lerror);
			}
		}
		
		return TRUE;
	}
	return FALSE; /* not handled */
}


static void
selection_changed_cb (GtkTreeView *treeview, G_GNUC_UNUSED GtkTreePath *path,
		      G_GNUC_UNUSED GtkTreeViewColumn *column, DataFavoriteSelector *tsel)
{
	GtkTreeModel *model;
	GtkTreeSelection *select;
	GtkTreeIter iter;
	
	select = gtk_tree_view_get_selection (treeview);
	if (gtk_tree_selection_get_selected (select, &model, &iter)) {
		gchar *str;
		guint type;
		gint fav_id;
		gtk_tree_model_get (model, &iter,
				    COLUMN_ID, &fav_id,
				    COLUMN_TYPE, &type,
				    COLUMN_CONTENTS, &str, -1);
		g_signal_emit (tsel, data_favorite_selector_signals [SELECTION_CHANGED], 0, fav_id, type, str);
		g_free (str);
	}
}

static gboolean
prop_save_timeout (DataFavoriteSelector *tsel)
{
	ToolsFavorites *bfav;
	ToolsFavoritesAttributes fav;
	GError *error = NULL;

	memset (&fav, 0, sizeof (ToolsFavoritesAttributes));
	fav.id = tsel->priv->properties_id;
	fav.type = GDA_TOOLS_FAVORITES_DATA_MANAGERS;
	fav.name = (gchar*) gtk_entry_get_text (GTK_ENTRY (tsel->priv->properties_name));
	fav.descr = NULL;

	GtkTextBuffer *buffer;
	GtkTextIter start, end;
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (tsel->priv->properties_text));
	gtk_text_buffer_get_start_iter (buffer, &start);
	gtk_text_buffer_get_end_iter (buffer, &end);
	fav.contents = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

	bfav = browser_connection_get_favorites (tsel->priv->bcnc);
	if (! gda_tools_favorites_add (bfav, 0, &fav, ORDER_KEY_DATA_MANAGERS, tsel->priv->properties_position, &error)) {
		browser_show_error ((GtkWindow*) gtk_widget_get_toplevel ((GtkWidget*) tsel),
				    _("Could not add favorite: %s"),
				    error && error->message ? error->message : _("No detail"));
		if (error)
			g_error_free (error);
	}

	g_free (fav.contents);
	tsel->priv->prop_save_timeout = 0;
	return FALSE; /* remove timeout */
}

static void
property_changed_cb (G_GNUC_UNUSED GtkWidget *multiple, DataFavoriteSelector *tsel)
{
	if (tsel->priv->prop_save_timeout)
		g_source_remove (tsel->priv->prop_save_timeout);
	tsel->priv->prop_save_timeout = g_timeout_add (100, (GSourceFunc) prop_save_timeout, tsel);
}

static void
properties_activated_cb (GtkMenuItem *mitem, DataFavoriteSelector *tsel)
{
	if (! tsel->priv->popup_properties) {
		GtkWidget *pcont, *vbox, *hbox, *label, *entry, *text, *grid;
		gchar *str;
		
		pcont = popup_container_new (GTK_WIDGET (mitem));
		vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		gtk_container_add (GTK_CONTAINER (pcont), vbox);
		
		label = gtk_label_new ("");
		str = g_strdup_printf ("<b>%s:</b>", _("Favorite's properties"));
		gtk_label_set_markup (GTK_LABEL (label), str);
		g_free (str);
		gtk_misc_set_alignment (GTK_MISC (label), 0., -1);
		gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
		
		hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0); /* HIG */
		gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 5);
		label = gtk_label_new ("      ");
		gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
		
		grid = gtk_grid_new ();
		gtk_box_pack_start (GTK_BOX (hbox), grid, TRUE, TRUE, 0);
		
		label = gtk_label_new ("");
		str = g_strdup_printf ("<b>%s:</b>", _("Name"));
		gtk_label_set_markup (GTK_LABEL (label), str);
		g_free (str);
		gtk_misc_set_alignment (GTK_MISC (label), 0., -1);
		gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
		
		label = gtk_label_new ("");
		str = g_strdup_printf ("<b>%s:</b>", _("Specifications"));
		gtk_label_set_markup (GTK_LABEL (label), str);
		g_free (str);
		gtk_misc_set_alignment (GTK_MISC (label), 0., 0.);
		gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);
		
		entry = gtk_entry_new ();
		gtk_grid_attach (GTK_GRID (grid), entry, 1, 0, 1, 1);
		tsel->priv->properties_name = entry;
		g_signal_connect (entry, "changed",
				  G_CALLBACK (property_changed_cb), tsel);
		
#ifdef HAVE_GTKSOURCEVIEW
		text = gtk_source_view_new ();
		gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (text))),
							TRUE);
		gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (text))),
						gtk_source_language_manager_get_language (gtk_source_language_manager_get_default (),
											  "xml"));
#else
		text = gtk_text_view_new ();
#endif
		GtkWidget *sw;
		gtk_widget_set_size_request (GTK_WIDGET (text), 400, 300);

		sw = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
						GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		gtk_grid_attach (GTK_GRID (grid), sw, 1, 1, 1, 1);
		gtk_container_add (GTK_CONTAINER (sw), text);
		tsel->priv->properties_text = text;
		g_signal_connect (gtk_text_view_get_buffer (GTK_TEXT_VIEW (text)), "changed",
				  G_CALLBACK (property_changed_cb), tsel);

		tsel->priv->popup_properties = pcont;
		gtk_widget_show_all (vbox);
	}

	/* adjust contents */
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tsel->priv->treeview));
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gchar *name, *contents;
		GtkTextBuffer *buffer;
		
		gtk_tree_model_get (model, &iter,
				    COLUMN_ID, &(tsel->priv->properties_id),
				    COLUMN_POSITION, &(tsel->priv->properties_position),
				    COLUMN_NAME, &name,
				    COLUMN_CONTENTS, &contents, -1);

		g_signal_handlers_block_by_func (tsel->priv->properties_name,
						 G_CALLBACK (property_changed_cb), tsel);
		gtk_entry_set_text (GTK_ENTRY (tsel->priv->properties_name), name);
		g_signal_handlers_unblock_by_func (tsel->priv->properties_name,
						   G_CALLBACK (property_changed_cb), tsel);
		g_free (name);

		g_signal_handlers_block_by_func (tsel->priv->properties_text,
						 G_CALLBACK (property_changed_cb), tsel);
		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (tsel->priv->properties_text));
		if (contents)
			gtk_text_buffer_set_text (buffer, contents, -1);
		else
			gtk_text_buffer_set_text (buffer, "", -1);
		g_signal_handlers_unblock_by_func (tsel->priv->properties_text,
						   G_CALLBACK (property_changed_cb), tsel);
		g_free (contents);

		gtk_widget_show (tsel->priv->popup_properties);
	}
}

static void
do_popup_menu (G_GNUC_UNUSED GtkWidget *widget, GdkEventButton *event, DataFavoriteSelector *tsel)
{
	int button, event_time;

	if (! tsel->priv->popup_menu) {
		GtkWidget *menu, *mitem;
		
		menu = gtk_menu_new ();
		g_signal_connect (menu, "deactivate", 
				  G_CALLBACK (gtk_widget_hide), NULL);
		
		mitem = gtk_image_menu_item_new_from_stock (GTK_STOCK_PROPERTIES, NULL);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), mitem);
		gtk_widget_show (mitem);
		g_signal_connect (mitem, "activate",
				  G_CALLBACK (properties_activated_cb), tsel);

		tsel->priv->popup_menu = menu;
	}
		
	if (event) {
		button = event->button;
		event_time = event->time;
	}
	else {
		button = 0;
		event_time = gtk_get_current_event_time ();
	}

	gtk_menu_popup (GTK_MENU (tsel->priv->popup_menu), NULL, NULL, NULL, NULL, 
			button, event_time);
}


static gboolean
popup_menu_cb (GtkWidget *widget, DataFavoriteSelector *tsel)
{
	do_popup_menu (widget, NULL, tsel);
	return TRUE;
}

static gboolean
button_press_event_cb (GtkTreeView *treeview, GdkEventButton *event, DataFavoriteSelector *tsel)
{
	if (event->button == 3 && event->type == GDK_BUTTON_PRESS) {
		do_popup_menu ((GtkWidget*) treeview, event, tsel);
		return TRUE;
	}

	return FALSE;
}

static void cell_data_func (GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
			    GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data);
static gboolean idle_update_favorites (DataFavoriteSelector *tsel);
static gboolean tree_store_drag_drop_cb (GdauiTreeStore *store, const gchar *path,
					 GtkSelectionData *selection_data, DataFavoriteSelector *tsel);
static gboolean tree_store_drag_can_drag_cb (GdauiTreeStore *store, const gchar *path,
					     DataFavoriteSelector *tsel);
static gboolean tree_store_drag_get_cb (GdauiTreeStore *store, const gchar *path,
					GtkSelectionData *selection_data, DataFavoriteSelector *tsel);

/**
 * data_favorite_selector_new
 *
 * Returns: a new #GtkWidget
 */
GtkWidget *
data_favorite_selector_new (BrowserConnection *bcnc)
{
	DataFavoriteSelector *tsel;
	GdaTreeManager *manager;

	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);
	tsel = DATA_FAVORITE_SELECTOR (g_object_new (DATA_FAVORITE_SELECTOR_TYPE, NULL));

	tsel->priv->bcnc = g_object_ref (bcnc);
	g_signal_connect (browser_connection_get_favorites (tsel->priv->bcnc), "favorites-changed",
			  G_CALLBACK (favorites_changed_cb), tsel);
	
	/* create tree managers */
	tsel->priv->tree = gda_tree_new ();
	manager = mgr_favorites_new (bcnc, GDA_TOOLS_FAVORITES_DATA_MANAGERS, ORDER_KEY_DATA_MANAGERS);
        gda_tree_add_manager (tsel->priv->tree, manager);
	g_object_unref (manager);

	/* update the tree's contents */
	if (! gda_tree_update_all (tsel->priv->tree, NULL)) {
		if (tsel->priv->idle_update_favorites == 0)
			tsel->priv->idle_update_favorites = g_idle_add ((GSourceFunc) idle_update_favorites, tsel);
	}

	/* header */
	GtkWidget *label;
	gchar *str;
	str = g_strdup_printf ("<b>%s</b>", _("Saved"));
	label = gdaui_bar_new (str);
	g_free (str);
	gdaui_bar_set_icon_from_pixbuf (GDAUI_BAR (label), browser_get_pixbuf_icon (BROWSER_ICON_BOOKMARK));
        gtk_box_pack_start (GTK_BOX (tsel), label, FALSE, FALSE, 0);
        gtk_widget_show (label);

	/* tree model */
	GtkTreeModel *model;
	GtkWidget *treeview;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	model = gdaui_tree_store_new (tsel->priv->tree, 6,
				      G_TYPE_INT, MGR_FAVORITES_POSITION_ATT_NAME,
				      G_TYPE_OBJECT, "icon",
				      G_TYPE_STRING, MGR_FAVORITES_CONTENTS_ATT_NAME,
				      G_TYPE_UINT, MGR_FAVORITES_TYPE_ATT_NAME,
				      G_TYPE_INT, MGR_FAVORITES_ID_ATT_NAME,
				      G_TYPE_STRING, MGR_FAVORITES_NAME_ATT_NAME);
	treeview = browser_make_tree_view (model);
	tsel->priv->treeview = treeview;
	g_object_unref (model);

	/* icon */
	column = gtk_tree_view_column_new ();

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "pixbuf", COLUMN_ICON);
	g_object_set ((GObject*) renderer, "yalign", 0., NULL);

	/* text */
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (column, renderer, (GtkTreeCellDataFunc) cell_data_func,
						 NULL, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
	gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
	
	/* scrolled window packing */
	GtkWidget *sw;
	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
					     GTK_SHADOW_ETCHED_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
					GTK_POLICY_NEVER,
					GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (sw), treeview);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview), FALSE);	

	gtk_box_pack_start (GTK_BOX (tsel), sw, TRUE, TRUE, 0);
	gtk_widget_show_all (sw);
	g_signal_connect (G_OBJECT (treeview), "row-activated",
			  G_CALLBACK (selection_changed_cb), tsel);
	g_signal_connect (G_OBJECT (treeview), "key-press-event",
			  G_CALLBACK (key_press_event_cb), tsel);
	g_signal_connect (G_OBJECT (treeview), "popup-menu",
			  G_CALLBACK (popup_menu_cb), tsel);
	g_signal_connect (G_OBJECT (treeview), "button-press-event",
			  G_CALLBACK (button_press_event_cb), tsel);

	/* DnD */
	gtk_tree_view_enable_model_drag_dest (GTK_TREE_VIEW (treeview), dbo_table, G_N_ELEMENTS (dbo_table),
					      GDK_ACTION_COPY);
	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (treeview), GDK_BUTTON1_MASK,
						dbo_table, G_N_ELEMENTS (dbo_table),
						GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_signal_connect (model, "drag-drop",
			  G_CALLBACK (tree_store_drag_drop_cb), tsel);
	g_signal_connect (model, "drag-can-drag",
			  G_CALLBACK (tree_store_drag_can_drag_cb), tsel);
	g_signal_connect (model, "drag-get",
			  G_CALLBACK (tree_store_drag_get_cb), tsel);

	return (GtkWidget*) tsel;
}

static void
cell_data_func (G_GNUC_UNUSED GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
		GtkTreeModel *tree_model, GtkTreeIter *iter, G_GNUC_UNUSED gpointer data)
{
	gchar *name;
	gchar *tmp;

	gtk_tree_model_get (tree_model, iter,
			    COLUMN_NAME, &name, -1);
	tmp = g_markup_printf_escaped ("%s", name);
	g_free (name);

	g_object_set ((GObject*) cell, "markup", tmp, NULL);
	g_free (tmp);
}


static gboolean
idle_update_favorites (DataFavoriteSelector *tsel)
{
	gboolean done;
	done = gda_tree_update_all (tsel->priv->tree, NULL);
	if (done)
		tsel->priv->idle_update_favorites = 0;
	else
		tsel->priv->idle_update_favorites = g_timeout_add_seconds (1, (GSourceFunc) idle_update_favorites,
									   tsel);
	return FALSE;
}

static gboolean
tree_store_drag_drop_cb (G_GNUC_UNUSED GdauiTreeStore *store, const gchar *path,
			 GtkSelectionData *selection_data, DataFavoriteSelector *tsel)
{
	ToolsFavorites *bfav;
	ToolsFavoritesAttributes fav;
	GError *error = NULL;
	gint pos;
	gboolean retval = TRUE;
	gint id;
	bfav = browser_connection_get_favorites (tsel->priv->bcnc);

	id = gda_tools_favorites_find (bfav, 0, (gchar*) gtk_selection_data_get_data (selection_data),
				     &fav, NULL);
	if (id < 0) {
		memset (&fav, 0, sizeof (ToolsFavoritesAttributes));
		fav.id = -1;
		fav.type = GDA_TOOLS_FAVORITES_DATA_MANAGERS;
		fav.name = _("Unnamed data manager");
		fav.descr = NULL;
		fav.contents = (gchar*) gtk_selection_data_get_data (selection_data);
	}

	pos = atoi (path);
	/*g_print ("%s() path => %s, pos: %d\n", __FUNCTION__, path, pos);*/
	
	if (! gda_tools_favorites_add (bfav, 0, &fav, ORDER_KEY_DATA_MANAGERS, pos, &error)) {
		browser_show_error ((GtkWindow*) gtk_widget_get_toplevel ((GtkWidget*) tsel),
				    _("Could not add favorite: %s"),
				    error && error->message ? error->message : _("No detail"));
		if (error)
			g_error_free (error);
		retval = FALSE;
	}
	
	if (id >= 0)
		gda_tools_favorites_reset_attributes (&fav);

	return retval;
}

static gboolean
tree_store_drag_can_drag_cb (G_GNUC_UNUSED GdauiTreeStore *store, const gchar *path,
			     DataFavoriteSelector *tsel)
{
	GdaTreeNode *node;
	node = gda_tree_get_node (tsel->priv->tree, path, FALSE);
	if (node) {
		const GValue *cvalue;
		cvalue = gda_tree_node_get_node_attribute (node, "fav_contents");
		if (cvalue)
			return TRUE;
	}
	return FALSE;
}

static gboolean
tree_store_drag_get_cb (G_GNUC_UNUSED GdauiTreeStore *store, const gchar *path,
			GtkSelectionData *selection_data, DataFavoriteSelector *tsel)
{
	GdaTreeNode *node;
	node = gda_tree_get_node (tsel->priv->tree, path, FALSE);
	if (node) {
		const GValue *cvalue;
		cvalue = gda_tree_node_get_node_attribute (node, "fav_contents");
		if (cvalue) {
			const gchar *str;
			str = g_value_get_string (cvalue);
			gtk_selection_data_set (selection_data, gtk_selection_data_get_target (selection_data),
						8, (guchar*) str, strlen (str));
			return TRUE;
		}
	}
	return FALSE;
}

static void
favorites_changed_cb (G_GNUC_UNUSED ToolsFavorites *bfav, DataFavoriteSelector *tsel)
{
	if (! gda_tree_update_all (tsel->priv->tree, NULL)) {
		if (tsel->priv->idle_update_favorites == 0)
			tsel->priv->idle_update_favorites = g_idle_add ((GSourceFunc) idle_update_favorites, tsel);

	}
}
