/*
 * Copyright (C) 2010 David King <davidk@openismus.com>
 * Copyright (C) 2010 - 2012 Vivien Malerba <malerba@gnome-db.org>
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

#include <string.h>
#include <glib/gi18n-lib.h>
#include <libgda/libgda.h>
#include "ui-formgrid.h"
#include <libgda-ui/gdaui-data-proxy.h>
#include <libgda-ui/gdaui-raw-form.h>
#include <libgda-ui/gdaui-data-selector.h>
#include "../support.h"
#include "../browser-window.h"
#include "../browser-stock-icons.h"
#include "widget-overlay.h"
#include <libgda/gda-data-model-extra.h>
#include "favorites-actions.h"
#ifdef HAVE_LDAP
#include "../ldap-browser/ldap-browser-perspective.h"
#endif
#include "../../tool-utils.h"

static void ui_formgrid_class_init (UiFormGridClass * class);
static void ui_formgrid_init (UiFormGrid *wid);
static void ui_formgrid_dispose (GObject *object);

static void ui_formgrid_set_property (GObject *object,
				      guint param_id,
				      const GValue *value,
				      GParamSpec *pspec);
static void ui_formgrid_get_property (GObject *object,
				      guint param_id,
				      GValue *value,
				      GParamSpec *pspec);
static void ui_formgrid_show (GtkWidget *widget);
static BrowserConnection *get_browser_connection (UiFormGrid *formgrid);
static void compute_modification_statements (UiFormGrid *formgrid, GdaDataModel *model);

#define GRID_FLAGS (GDAUI_DATA_PROXY_INFO_CURRENT_ROW | GDAUI_DATA_PROXY_INFO_CHUNCK_CHANGE_BUTTONS)
#define FORM_FLAGS (GDAUI_DATA_PROXY_INFO_CURRENT_ROW | GDAUI_DATA_PROXY_INFO_ROW_MOVE_BUTTONS)

typedef enum {
	MOD_INSERT,
	MOD_UPDATE,
	MOD_DELETE,
	MOD_LAST
} ModType;

struct _UiFormGridPriv
{
	GtkWidget   *nb;
	GtkWidget   *raw_form;
	GtkWidget   *raw_grid;
	GtkWidget   *info;
	GtkWidget   *overlay_form;
	GtkWidget   *overlay_grid;
	GtkWidget   *autoupdate_toggle;
	gboolean     autoupdate;
	gboolean     autoupdate_possible;
	GdauiDataProxyInfoFlag flags;
	
	BrowserConnection *bcnc;
	gboolean     scroll_form;

	/* modifications to the data */
	gboolean           compute_mod_stmt; /* if %TRUE, then the INSERT, UPDATE and DELETE
					       * statements are automatically computed, see the PROP_AUTOMOD property */
	gboolean           mod_stmt_auto_computed; /* %TRUE if mod_stmt[*] have automatically
						    * been computed */
	GdaStatement      *mod_stmt[MOD_LAST];
};

/* get a pointer to the parents to be able to call their destructor */
static GObjectClass *parent_class = NULL;

/* signals */
enum {
        DATA_SET_CHANGED,
        LAST_SIGNAL
};

gint ui_formgrid_signals [LAST_SIGNAL] = { 0 };

/* properties */
enum {
	PROP_0,
	PROP_RAW_GRID,
	PROP_RAW_FORM,
	PROP_INFO,
	PROP_SCROLL_FORM,
	PROP_AUTOMOD
};

GType
ui_formgrid_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			sizeof (UiFormGridClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) ui_formgrid_class_init,
			NULL,
			NULL,
			sizeof (UiFormGrid),
			0,
			(GInstanceInitFunc) ui_formgrid_init,
			0
		};		

		type = g_type_register_static (GTK_TYPE_BOX, "UiFormGrid", &info, 0);
	}

	return type;
}

static void
ui_formgrid_class_init (UiFormGridClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_peek_parent (klass);
	object_class->dispose = ui_formgrid_dispose;
	GTK_WIDGET_CLASS (klass)->show = ui_formgrid_show;

	/* signals */
	ui_formgrid_signals [DATA_SET_CHANGED] = 
		g_signal_new ("data-set-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (UiFormGridClass, data_set_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
	klass->data_set_changed = NULL;

	/* Properties */
        object_class->set_property = ui_formgrid_set_property;
        object_class->get_property = ui_formgrid_get_property;
	g_object_class_install_property (object_class, PROP_RAW_GRID,
                                         g_param_spec_object ("raw_grid", NULL, NULL, 
							      GDAUI_TYPE_RAW_GRID,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_RAW_FORM,
                                         g_param_spec_object ("raw_form", NULL, NULL, 
							      GDAUI_TYPE_RAW_GRID,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_INFO,
                                         g_param_spec_object ("widget_info", NULL, NULL, 
							      GDAUI_TYPE_DATA_PROXY_INFO,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class, PROP_SCROLL_FORM,
					 g_param_spec_boolean ("scroll-form", NULL, NULL,
							       FALSE,
							       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class, PROP_AUTOMOD,
					 g_param_spec_boolean ("compute-mod-statements", NULL, NULL,
							       FALSE,
							       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));
}

static void
ui_formgrid_dispose (GObject *object)
{
        UiFormGrid *formgrid;

        formgrid = UI_FORMGRID (object);
        if (formgrid->priv) {
                if (formgrid->priv->bcnc)
                        g_object_unref (formgrid->priv->bcnc);

		ModType mod;
		for (mod = MOD_INSERT; mod < MOD_LAST; mod++) {
			if (formgrid->priv->mod_stmt[mod]) {
				g_object_unref (formgrid->priv->mod_stmt[mod]);
				formgrid->priv->mod_stmt[mod] = NULL;
			}
		}

                g_free (formgrid->priv);
                formgrid->priv = NULL;
        }

        /* parent class */
        parent_class->dispose (object);
}

static void
ui_formgrid_show (GtkWidget *widget)
{
	UiFormGrid *formgrid;
	GtkWidget *ovl, *packed;
	formgrid = UI_FORMGRID (widget);

	if (! formgrid->priv->overlay_grid) {
		/* finalize packing */
		GtkWidget *sw, *vp;
		sw = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
						GTK_POLICY_AUTOMATIC,
						GTK_POLICY_AUTOMATIC);
		gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
						     GTK_SHADOW_NONE);
		gtk_container_add (GTK_CONTAINER (sw), formgrid->priv->raw_grid);
		gtk_widget_show_all (sw);
		packed = sw;

		/* overlay */
		ovl = widget_overlay_new ();
		formgrid->priv->overlay_grid = ovl;
		g_object_set (G_OBJECT (ovl), "add-scale", TRUE, NULL);
		g_object_set (G_OBJECT (ovl), "add-scale", FALSE, NULL);
		gtk_container_add (GTK_CONTAINER (ovl), packed);
		widget_overlay_set_child_props (WIDGET_OVERLAY (ovl), packed,
						WIDGET_OVERLAY_CHILD_HALIGN,
						WIDGET_OVERLAY_ALIGN_FILL,
						WIDGET_OVERLAY_CHILD_VALIGN,
						WIDGET_OVERLAY_ALIGN_FILL,
						WIDGET_OVERLAY_CHILD_SCALE, .9,
						-1);
		gtk_widget_show (ovl);
		gtk_notebook_append_page (GTK_NOTEBOOK (formgrid->priv->nb), ovl, NULL);
	}

	if (! formgrid->priv->overlay_form) {
		/* finalize packing */
		if (formgrid->priv->scroll_form) {
			GtkWidget *sw, *vp;
			sw = gtk_scrolled_window_new (NULL, NULL);
			gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
							GTK_POLICY_AUTOMATIC,
							GTK_POLICY_AUTOMATIC);
			gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
							     GTK_SHADOW_NONE);
			vp = gtk_viewport_new (NULL, NULL);
			gtk_widget_set_name (vp, "gdaui-transparent-background");
			gtk_container_add (GTK_CONTAINER (sw), vp);
			gtk_viewport_set_shadow_type (GTK_VIEWPORT (vp), GTK_SHADOW_NONE);
			gtk_container_add (GTK_CONTAINER (vp), formgrid->priv->raw_form);
			gtk_widget_show_all (sw);
			packed = sw;
		}
		else {
			gtk_widget_show (formgrid->priv->raw_form);
			packed = formgrid->priv->raw_form;
		}
		
		/* overlay */
		ovl = widget_overlay_new ();
		formgrid->priv->overlay_form = ovl;
		g_object_set (G_OBJECT (ovl), "add-scale", TRUE, NULL);
		g_object_set (G_OBJECT (ovl), "add-scale", FALSE, NULL);
		gtk_container_add (GTK_CONTAINER (ovl), packed);
		widget_overlay_set_child_props (WIDGET_OVERLAY (ovl), packed,
						WIDGET_OVERLAY_CHILD_HALIGN,
						WIDGET_OVERLAY_ALIGN_FILL,
						WIDGET_OVERLAY_CHILD_VALIGN,
						WIDGET_OVERLAY_ALIGN_FILL,
						WIDGET_OVERLAY_CHILD_SCALE, 1.,
						-1);
		gtk_widget_show (ovl);
		gtk_notebook_append_page (GTK_NOTEBOOK (formgrid->priv->nb), ovl, NULL);
		gtk_notebook_set_current_page (GTK_NOTEBOOK (formgrid->priv->nb),
					       0);
	}

	((GtkWidgetClass *)parent_class)->show (widget);
	if (! formgrid->priv->autoupdate_possible)
		gtk_widget_hide (formgrid->priv->autoupdate_toggle);
}

static void form_grid_autoupdate_cb (GtkToggleButton *button, UiFormGrid *formgrid);
static void form_grid_toggled_cb (GtkToggleAction *action, UiFormGrid *formgrid);
static void form_grid_populate_popup_cb (GtkWidget *wid, GtkMenu *menu, UiFormGrid *formgrid);
static void selection_changed_cb (GdauiDataSelector *sel, UiFormGrid *formgrid);

static void
ui_formgrid_init (UiFormGrid *formgrid)
{
	GtkWidget *sw;
	GtkWidget *hbox, *button;
	
	formgrid->priv = g_new0 (UiFormGridPriv, 1);
	formgrid->priv->raw_grid = NULL;
	formgrid->priv->info = NULL;
	formgrid->priv->flags = GDAUI_DATA_PROXY_INFO_CURRENT_ROW | GDAUI_DATA_PROXY_INFO_ROW_MODIFY_BUTTONS;
	formgrid->priv->bcnc = NULL;
	formgrid->priv->autoupdate = TRUE;
	formgrid->priv->autoupdate_possible = FALSE;
	formgrid->priv->scroll_form = FALSE;
	formgrid->priv->compute_mod_stmt = FALSE;

	gtk_orientable_set_orientation (GTK_ORIENTABLE (formgrid), GTK_ORIENTATION_VERTICAL);

	/* notebook */
	formgrid->priv->nb = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (formgrid->priv->nb), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (formgrid->priv->nb), FALSE);
	gtk_box_pack_start (GTK_BOX (formgrid), formgrid->priv->nb, TRUE, TRUE, 0);
	gtk_widget_show (formgrid->priv->nb);

	/* grid on 1st page of notebook, not added there */
	formgrid->priv->raw_grid = gdaui_raw_grid_new (NULL);
	gdaui_data_proxy_column_show_actions (GDAUI_DATA_PROXY (formgrid->priv->raw_grid), -1, FALSE);
	g_signal_connect (formgrid->priv->raw_grid, "populate-popup",
			  G_CALLBACK (form_grid_populate_popup_cb), formgrid);

	/* form on the 2nd page of the notebook, not added there */
	formgrid->priv->raw_form = gdaui_raw_form_new (NULL);
	gdaui_data_proxy_column_show_actions (GDAUI_DATA_PROXY (formgrid->priv->raw_form), -1, FALSE);
	g_signal_connect (formgrid->priv->raw_form, "populate-popup",
			  G_CALLBACK (form_grid_populate_popup_cb), formgrid);

	/* info widget and toggle button at last */
	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start (GTK_BOX (formgrid), hbox, FALSE, TRUE, 0);
	gtk_widget_show (hbox);

	/* button to toggle between auto update and not */
	button = gtk_toggle_button_new ();
	GtkWidget *img = gtk_image_new_from_stock (GTK_STOCK_EXECUTE, GTK_ICON_SIZE_MENU);
	gtk_button_set_image (GTK_BUTTON (button), img);
	formgrid->priv->autoupdate_toggle  = button;

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), formgrid->priv->autoupdate);
	gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, TRUE, 0);
	g_signal_connect (G_OBJECT (button), "toggled",
			  G_CALLBACK (form_grid_autoupdate_cb), formgrid);
	gtk_widget_set_tooltip_text (button, _("Enable or disable auto update of data"));

	/* Proxy info */
	formgrid->priv->info = gdaui_data_proxy_info_new (GDAUI_DATA_PROXY (formgrid->priv->raw_grid), 
							  formgrid->priv->flags |
							  GDAUI_DATA_PROXY_INFO_CURRENT_ROW |
							  GDAUI_DATA_PROXY_INFO_CHUNCK_CHANGE_BUTTONS);

	GtkUIManager *uimanager;
	GtkActionGroup *agroup;
	GtkAction *action;
	guint mid;
	
	g_object_get (G_OBJECT (formgrid->priv->info), "ui-manager", &uimanager, NULL);
	agroup = gtk_action_group_new ("FormGrid");
	gtk_action_group_set_translation_domain (agroup, GETTEXT_PACKAGE);

	action = GTK_ACTION (gtk_toggle_action_new ("FGToggle", "FGToggle",
						    _("Toggle between grid and form presentations"),
						    BROWSER_STOCK_GRID));
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
	gtk_action_group_add_action (agroup, action);
	g_signal_connect (G_OBJECT (action), "toggled",
			  G_CALLBACK (form_grid_toggled_cb), formgrid);
	g_object_unref (action);
	gtk_ui_manager_insert_action_group (uimanager, agroup, 0);
	g_object_unref (agroup);

	mid = gtk_ui_manager_new_merge_id (uimanager);
	gtk_ui_manager_add_ui (uimanager, mid, "/ToolBar", "FGToggle", "FGToggle",
			       GTK_UI_MANAGER_AUTO, TRUE);
	gtk_ui_manager_ensure_update (uimanager);
	gtk_box_pack_start (GTK_BOX (hbox), formgrid->priv->info, TRUE, TRUE, 0);
	gtk_widget_show (formgrid->priv->info);
	g_object_unref (G_OBJECT (uimanager));


	/*gchar *tmp;
	g_object_get (uimanager, "ui", &tmp, NULL);
	g_print ("==>[%s]\n", tmp);
	g_free (tmp);
	*/

	/* keep data in sync */
	g_signal_connect (formgrid->priv->raw_grid, "selection-changed",
			  G_CALLBACK (selection_changed_cb), formgrid);
	g_signal_connect (formgrid->priv->raw_form, "selection-changed",
			  G_CALLBACK (selection_changed_cb), formgrid);
}

/**
 * ui_formgrid_get_ui_manager:
 *
 * Returns: (transfer none): the #GtkUIManager
 */
GtkUIManager *
ui_formgrid_get_ui_manager (UiFormGrid *formgrid)
{
	GtkUIManager *uimanager;
	g_return_val_if_fail (UI_IS_FORMGRID (formgrid), NULL);
	g_object_get (G_OBJECT (formgrid->priv->info), "ui-manager", &uimanager, NULL);
	g_object_unref (uimanager);
	return uimanager;
}

static void
selection_changed_cb (GdauiDataSelector *sel, UiFormGrid *formgrid)
{
	GdaDataModelIter *iter;
	GdauiDataSelector *tosel;
	gint row;
	if (sel == (GdauiDataSelector*) formgrid->priv->raw_grid)
		tosel = (GdauiDataSelector*) formgrid->priv->raw_form;
	else
		tosel = (GdauiDataSelector*) formgrid->priv->raw_grid;

	iter = gdaui_data_selector_get_data_set (sel);
	g_assert (iter);
	row = gda_data_model_iter_get_row (iter);
	/*g_print ("Moved %s to row %d\n", sel == (GdauiDataSelector*) formgrid->priv->raw_grid ? "grid" : "form", row);*/
	iter = gdaui_data_selector_get_data_set (tosel);
	if (iter) {
		g_signal_handlers_block_by_func (tosel, G_CALLBACK (selection_changed_cb), formgrid);
		gda_data_model_iter_move_to_row (iter, row >= 0 ? row : 0);
		g_signal_handlers_unblock_by_func (tosel, G_CALLBACK (selection_changed_cb), formgrid);
	}
}

static void
form_grid_autoupdate_cb (GtkToggleButton *button, UiFormGrid *formgrid)
{
	formgrid->priv->autoupdate = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
}

static void
form_grid_toggled_cb (GtkToggleAction *action, UiFormGrid *formgrid)
{
	if (!gtk_toggle_action_get_active (action)) {
		/* switch to form  view */
		gtk_notebook_set_current_page (GTK_NOTEBOOK (formgrid->priv->nb), 1);
		g_object_set (G_OBJECT (formgrid->priv->info),
			      "data-proxy", formgrid->priv->raw_form,
			      "flags", formgrid->priv->flags | FORM_FLAGS, NULL);

		g_object_set (G_OBJECT (action), "stock-id", BROWSER_STOCK_FORM, NULL);
	}
	else {
		/* switch to grid view */
		gtk_notebook_set_current_page (GTK_NOTEBOOK (formgrid->priv->nb), 0);
		g_object_set (G_OBJECT (formgrid->priv->info),
			      "data-proxy", formgrid->priv->raw_grid,
			      "flags", formgrid->priv->flags | GRID_FLAGS, NULL);

		g_object_set (G_OBJECT (action), "stock-id", BROWSER_STOCK_GRID, NULL);
	}
}

static BrowserConnection *
get_browser_connection (UiFormGrid *formgrid)
{
	if (formgrid->priv->bcnc)
		return formgrid->priv->bcnc;
	else {
		GtkWidget *toplevel;
		
		toplevel = gtk_widget_get_toplevel (GTK_WIDGET (formgrid));
		if (BROWSER_IS_WINDOW (toplevel))
			return browser_window_get_connection (BROWSER_WINDOW (toplevel));
	}
	return NULL;
}


static void execute_action_mitem_cb (GtkMenuItem *menuitem, UiFormGrid *formgrid);
#ifdef HAVE_LDAP
static void ldap_view_dn_mitem_cb (GtkMenuItem *menuitem, UiFormGrid *formgrid);
#endif
static void zoom_form_mitem_cb (GtkCheckMenuItem *checkmenuitem, UiFormGrid *formgrid);
static void zoom_grid_mitem_cb (GtkCheckMenuItem *checkmenuitem, UiFormGrid *formgrid);

static void
form_grid_populate_popup_cb (GtkWidget *wid, GtkMenu *menu, UiFormGrid *formgrid)
{
	/* add actions to execute to menu */
	GdaDataModelIter *iter;
	BrowserConnection *bcnc = NULL;

	bcnc = get_browser_connection (formgrid);
	if (!bcnc)
		return;
	
	iter = gdaui_data_selector_get_data_set (GDAUI_DATA_SELECTOR (formgrid->priv->raw_grid));

	/* actions */
	GSList *actions_list, *list;
	actions_list = gda_tools_favorites_get_actions (browser_connection_get_favorites (bcnc),
						      bcnc, GDA_SET (iter));
	if (actions_list) {
		GtkWidget *mitem, *submenu;
		mitem = gtk_menu_item_new_with_label (_("Execute action"));
		gtk_widget_show (mitem);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mitem);
		
		submenu = gtk_menu_new ();
		gtk_menu_item_set_submenu (GTK_MENU_ITEM (mitem), submenu);
		for (list = actions_list; list; list = list->next) {
			ToolsFavoriteAction *act = (ToolsFavoriteAction*) list->data;
			mitem = gtk_menu_item_new_with_label (act->name);
			gtk_widget_show (mitem);
			gtk_menu_shell_append (GTK_MENU_SHELL (submenu), mitem);
			g_object_set_data_full (G_OBJECT (mitem), "action", act,
						(GDestroyNotify) gda_tools_favorites_free_action);
			g_signal_connect (mitem, "activate",
					  G_CALLBACK (execute_action_mitem_cb), formgrid);
		}
		g_slist_free (actions_list);
	}

#ifdef HAVE_LDAP
	/* LDAP specific */
	if (browser_connection_is_ldap (bcnc)) {
		GdaHolder *dnh;
		dnh = gda_set_get_holder (GDA_SET (iter), "dn");
		if (dnh) {
			const GValue *cvalue;
			cvalue = gda_holder_get_value (GDA_HOLDER (dnh));
			if (!cvalue && (G_VALUE_TYPE (cvalue) != G_TYPE_STRING))
				dnh = NULL;
		}
		if (!dnh) {
			GSList *list;
			for (list = GDA_SET (iter)->holders; list; list = list->next) {
				const GValue *cvalue;
				cvalue = gda_holder_get_value (GDA_HOLDER (list->data));
				if (cvalue && (G_VALUE_TYPE (cvalue) == G_TYPE_STRING) &&
				    gda_ldap_is_dn (g_value_get_string (cvalue))) {
					dnh = GDA_HOLDER (list->data);
					break;
				}
			}
		}

		if (dnh) {
			const GValue *cvalue;
			cvalue = gda_holder_get_value (dnh);

			GtkWidget *mitem;
			mitem = gtk_menu_item_new_with_label (_("View LDAP entry's details"));
			gtk_widget_show (mitem);
			gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mitem);
			g_object_set_data_full (G_OBJECT (mitem), "dn",
						g_value_dup_string (cvalue), g_free);
			g_signal_connect (mitem, "activate",
					  G_CALLBACK (ldap_view_dn_mitem_cb), formgrid);
		}
	}
#endif

	if (wid == formgrid->priv->raw_form) {
		GtkWidget *mitem;
		gboolean add_scale;
		g_object_get (G_OBJECT (formgrid->priv->overlay_form), "add-scale", &add_scale, NULL);
		mitem = gtk_check_menu_item_new_with_label (_("Zoom..."));
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (mitem), add_scale);
		gtk_widget_show (mitem);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mitem);
		g_signal_connect (mitem, "toggled",
				  G_CALLBACK (zoom_form_mitem_cb), formgrid);
	}
	else if (wid == formgrid->priv->raw_grid) {
		GtkWidget *mitem;
		gboolean add_scale;
		g_object_get (G_OBJECT (formgrid->priv->overlay_grid), "add-scale", &add_scale, NULL);
		mitem = gtk_check_menu_item_new_with_label (_("Zoom..."));
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (mitem), add_scale);
		gtk_widget_show (mitem);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), mitem);
		g_signal_connect (mitem, "toggled",
				  G_CALLBACK (zoom_grid_mitem_cb), formgrid);
	}
}

static void
zoom_form_mitem_cb (GtkCheckMenuItem *checkmenuitem, UiFormGrid *formgrid)
{
	g_object_set (G_OBJECT (formgrid->priv->overlay_form), "add-scale",
		      gtk_check_menu_item_get_active (checkmenuitem), NULL);
}

static void
zoom_grid_mitem_cb (GtkCheckMenuItem *checkmenuitem, UiFormGrid *formgrid)
{
	g_object_set (G_OBJECT (formgrid->priv->overlay_grid), "add-scale",
		      gtk_check_menu_item_get_active (checkmenuitem), NULL);
}

typedef struct {
	BrowserConnection *bcnc; /* ref held */
	UiFormGrid    *formgrid; /* ref held */
	gchar         *name;
        GdaStatement  *stmt; /* ref held */
        GdaSet        *params; /* ref held */
	GdaDataModel  *model; /* ref held */
	guint          exec_id;
	guint          timer_id;
} ActionExecutedData;

static void action_executed_holder_changed_cb (G_GNUC_UNUSED GdaSet *params, G_GNUC_UNUSED GdaHolder *holder,
					       ActionExecutedData *aed);
static void
action_executed_data_free (ActionExecutedData *data)
{
	g_object_unref ((GObject*) data->bcnc);
	if (data->formgrid)
		g_object_unref ((GObject*) data->formgrid);
	g_free (data->name);
	g_object_unref ((GObject*) data->stmt);

	if (data->params) {
		g_signal_handlers_disconnect_by_func (data->params,
						      G_CALLBACK (action_executed_holder_changed_cb), data);
		g_object_unref ((GObject*) data->params);
	}
	if (data->model)
		g_object_unref ((GObject*) data->model);
	if (data->timer_id)
		g_source_remove (data->timer_id);
	g_free (data);
}

static gboolean
exec_end_timeout_cb (ActionExecutedData *aed)
{
	GError *error = NULL;
	GObject *obj;
	obj = browser_connection_execution_get_result (aed->bcnc, aed->exec_id,
                                                       NULL, &error);
	if (obj) {
                if (GDA_IS_DATA_MODEL (obj)) {
                        g_assert (aed->model == (GdaDataModel*) obj);
			gda_data_model_thaw (aed->model);
			gda_data_model_reset (aed->model);

			aed->exec_id = 0;
			aed->timer_id = 0;
			return FALSE;
                }
                else {
			g_object_unref (obj);
                        g_set_error (&error, GDA_TOOLS_ERROR, GDA_TOOLS_COMMAND_ARGUMENTS_ERROR,
                                     "%s", _("Statement to execute is not a selection statement"));
                }
        }

        if (error) {
		GtkWidget *toplevel;
		toplevel = gtk_widget_get_toplevel (GTK_WIDGET (aed->formgrid));
		browser_show_error (GTK_WINDOW (toplevel),
                                    _("Error executing query:\n%s"),
                                    error->message ?
                                    error->message : _("No detail"));
		g_clear_error (&error);
		gda_data_model_thaw (aed->model);
                aed->exec_id = 0;
		aed->timer_id = 0;
                return FALSE;
        }
        else
                return TRUE; /* keep timer */
}

static void
action_executed_holder_changed_cb (G_GNUC_UNUSED GdaSet *params, G_GNUC_UNUSED GdaHolder *holder,
				   ActionExecutedData *aed)
{
	if (! aed->formgrid->priv->autoupdate || ! aed->formgrid->priv->autoupdate_possible)
		return;

	GError *error = NULL;
	guint jid;
	gda_data_model_freeze (aed->model);
	jid = browser_connection_rerun_select (aed->bcnc, aed->model, &error);
	if (!jid) {
		GtkWidget *toplevel;
		toplevel = gtk_widget_get_toplevel (GTK_WIDGET (aed->formgrid));
		browser_show_error (GTK_WINDOW (toplevel),
                                    _("Error executing query:\n%s"),
                                    error->message ?
                                    error->message : _("No detail"));
		gda_data_model_thaw (aed->model);
	}
	else {
		/* watch the end of execution */
		aed->exec_id = jid;
		if (! aed->timer_id)
			aed->timer_id = g_timeout_add (50, (GSourceFunc) exec_end_timeout_cb, aed);
	}
}

/*
 * Called after an action's statement has been executed
 */
static void
statement_executed_cb (G_GNUC_UNUSED BrowserConnection *bcnc,
		       G_GNUC_UNUSED guint exec_id,
		       GObject *out_result,
		       G_GNUC_UNUSED GdaSet *out_last_inserted_row, GError *error,
		       ActionExecutedData *aed)
{
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (aed->formgrid));
	g_object_unref (aed->formgrid);
	aed->formgrid = NULL;

        if (error)
                browser_show_error (GTK_WINDOW (toplevel),
                                    _("Error executing query:\n%s"),
                                    error->message ?
                                    error->message : _("No detail"));
	else if (out_result && GDA_IS_DATA_MODEL (out_result)) {
		GtkWidget *dialog, *label, *fg;
		GtkWidget *dcontents;
		
		gchar *tmp;
		dialog = gtk_dialog_new_with_buttons (aed->name,
						      NULL,
						      0,
						      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, NULL);
		dcontents = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
		gtk_box_set_spacing (GTK_BOX (dcontents), 5);
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE, TRUE);
		
		tmp = g_markup_printf_escaped ("<b>%s:</b>", aed->name);
		label = gtk_label_new ("");
		gtk_label_set_markup (GTK_LABEL (label), tmp);
		g_free (tmp);
		gtk_misc_set_alignment (GTK_MISC (label), 0., 0.);
		gtk_box_pack_start (GTK_BOX (dcontents), label, FALSE, FALSE, 5);
		
		fg = ui_formgrid_new (GDA_DATA_MODEL (out_result), TRUE,
				      GDAUI_DATA_PROXY_INFO_CURRENT_ROW);
		ui_formgrid_set_connection (UI_FORMGRID (fg), aed->bcnc);

		if (GDA_IS_DATA_SELECT (out_result)) {
			GdaStatement *stmt;
			g_object_get (G_OBJECT (out_result), "select-stmt", &stmt, NULL);
			if (stmt) {
				ui_formgrid_handle_user_prefs (UI_FORMGRID (fg), NULL, stmt);
				g_object_unref (stmt);
			}
			aed->model = g_object_ref (out_result);
			g_signal_connect (aed->params, "holder-changed",
					  G_CALLBACK (action_executed_holder_changed_cb), aed);

			aed->formgrid = g_object_ref (fg);
			aed->formgrid->priv->autoupdate_possible = TRUE;
			gtk_widget_show (aed->formgrid->priv->autoupdate_toggle);
		}
		gtk_box_pack_start (GTK_BOX (dcontents), fg, TRUE, TRUE, 0);
		
		gtk_window_set_default_size (GTK_WINDOW (dialog), 400, 600);
		gtk_widget_show_all (dialog);
		
		g_signal_connect (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), NULL);
		g_signal_connect (dialog, "close",
				  G_CALLBACK (gtk_widget_destroy), NULL);
		g_object_set_data_full (G_OBJECT (dialog), "aed", aed,
					(GDestroyNotify) action_executed_data_free);
		aed = NULL; /* don't free it yet */
	}
        else if (BROWSER_IS_WINDOW (toplevel)) {
		browser_window_show_notice_printf (BROWSER_WINDOW (toplevel),
						   GTK_MESSAGE_INFO,
						   "ActionExecution",
						   "%s", _("Action successfully executed"));
	}
	else
		browser_show_message (GTK_WINDOW (toplevel),
				      "%s", _("Action successfully executed"));

	if (aed)
		action_executed_data_free (aed);
}

static void
execute_action_mitem_cb (GtkMenuItem *menuitem, UiFormGrid *formgrid)
{
	ToolsFavoriteAction *act;
	GtkWidget *dlg;
	gchar *tmp;
	gint response;
	GtkWidget *toplevel;

	act = (ToolsFavoriteAction*) g_object_get_data (G_OBJECT (menuitem), "action");
	toplevel = gtk_widget_get_toplevel ((GtkWidget*) formgrid);
	tmp = g_strdup_printf (_("Set or confirm the parameters to execute\n"
				 "action '%s'"), act->name);
	dlg = gdaui_basic_form_new_in_dialog (act->params,
					      (GtkWindow*) toplevel,
					      _("Execution of action"), tmp);
	g_free (tmp);
	response = gtk_dialog_run (GTK_DIALOG (dlg));
	gtk_widget_destroy (dlg);
	if (response == GTK_RESPONSE_ACCEPT) {
                GError *lerror = NULL;
		BrowserConnection *bcnc;
		ActionExecutedData *aed;
		
		bcnc = get_browser_connection (formgrid);
		g_assert (bcnc);
		
		aed = g_new0 (ActionExecutedData, 1);
		aed->formgrid = g_object_ref (formgrid);
		aed->bcnc = g_object_ref (bcnc);
		if (act->name)
			aed->name = g_strdup (act->name);
		aed->stmt = g_object_ref (act->stmt);
		aed->params = g_object_ref (act->params);

                if (! browser_connection_execute_statement_cb (bcnc,
                                                               act->stmt, act->params,
                                                               GDA_STATEMENT_MODEL_RANDOM_ACCESS,
                                                               FALSE,
                                                               (BrowserConnectionExecuteCallback) statement_executed_cb,
                                                               aed, &lerror)) {
                        browser_show_error (GTK_WINDOW (toplevel),
                                            _("Error executing query: %s"),
                                            lerror && lerror->message ? lerror->message : _("No detail"));
                        g_clear_error (&lerror);
			action_executed_data_free (aed);
                }
	}
}

#ifdef HAVE_LDAP
static void ldap_view_dn_mitem_cb (GtkMenuItem *menuitem, UiFormGrid *formgrid)
{
	const gchar *dn;
	BrowserWindow *bwin;
        BrowserPerspective *pers;

	dn = g_object_get_data (G_OBJECT (menuitem), "dn");
        bwin = (BrowserWindow*) gtk_widget_get_toplevel ((GtkWidget*) formgrid);
        pers = browser_window_change_perspective (bwin, _("LDAP browser"));

	ldap_browser_perspective_display_ldap_entry (LDAP_BROWSER_PERSPECTIVE (pers), dn);
}
#endif

static void
proxy_changed_cb (G_GNUC_UNUSED GdauiDataProxy *dp, G_GNUC_UNUSED GdaDataProxy *proxy, UiFormGrid *formgrid)
{
	g_signal_emit (formgrid, ui_formgrid_signals [DATA_SET_CHANGED], 0);
}

/**
 * ui_formgrid_new
 * @model: a #GdaDataModel
 * @scroll_form: set to %TRUE to wrap the embedded form in a scrolled window
 * @flags: the #GdauiDataProxyInfoFlag, specifying what to display in the new widget
 *
 * Creates a new #UiFormGrid widget suitable to display the data in @model
 *
 *  Returns: the new widget
 */
GtkWidget *
ui_formgrid_new (GdaDataModel *model, gboolean scroll_form, GdauiDataProxyInfoFlag flags)
{
	UiFormGrid *formgrid;
	GdaDataProxy *proxy;

	g_return_val_if_fail (!model || GDA_IS_DATA_MODEL (model), NULL);

	formgrid = (UiFormGrid *) g_object_new (UI_TYPE_FORMGRID, "scroll-form", scroll_form, NULL);
	formgrid->priv->flags = flags;

	/* a raw form and a raw grid for the same proxy */
	g_object_set (formgrid->priv->raw_grid, "model", model, NULL);
	proxy = gdaui_data_proxy_get_proxy (GDAUI_DATA_PROXY (formgrid->priv->raw_grid));
	g_object_set (formgrid->priv->raw_form, "model", proxy, NULL);
	gdaui_data_proxy_set_write_mode (GDAUI_DATA_PROXY (formgrid->priv->raw_form),
					 GDAUI_DATA_PROXY_WRITE_ON_DEMAND);
	g_object_set (G_OBJECT (formgrid->priv->info),
		      "flags", formgrid->priv->flags | GRID_FLAGS, NULL);

	g_signal_connect (formgrid->priv->raw_grid, "proxy-changed",
			  G_CALLBACK (proxy_changed_cb), formgrid);

	/* no more than 300 rows at a time */
	if (model) {
		gda_data_proxy_set_sample_size (proxy, 300);
		if (flags & GDAUI_DATA_PROXY_INFO_ROW_MODIFY_BUTTONS)
			g_object_set (G_OBJECT (formgrid), "compute-mod-statements", TRUE, NULL);
	}


	return (GtkWidget *) formgrid;
}

static void
handle_user_prefs_for_sql_statement (UiFormGrid *formgrid, BrowserConnection *bcnc,
				     GdaSqlStatement *sqlst)
{
	g_assert (sqlst);
	if (sqlst->stmt_type == GDA_SQL_STATEMENT_COMPOUND) {
		GdaSqlStatementCompound *comp;
		GSList *list;
		comp = (GdaSqlStatementCompound*) sqlst->contents;
		for (list = comp->stmt_list; list; list = list->next)
			handle_user_prefs_for_sql_statement (formgrid, bcnc,
							     (GdaSqlStatement *) list->data);
	}
	else {
		GdaSet *set;
		set = (GdaSet*) ui_formgrid_get_form_data_set (UI_FORMGRID (formgrid));

		GdaSqlStatementSelect *sel;
		GSList *list;
		gint pos;
		sel = (GdaSqlStatementSelect*) sqlst->contents;
		for (pos = 0, list = sel->expr_list; list; pos ++, list = list->next) {
			GdaSqlSelectField *field = (GdaSqlSelectField*) list->data;
			if (! field->validity_meta_object ||
			    (field->validity_meta_object->obj_type != GDA_META_DB_TABLE) ||
			    !field->validity_meta_table_column)
				continue;

			gchar *plugin;
			plugin = browser_connection_get_table_column_attribute (bcnc,
										GDA_META_TABLE (field->validity_meta_object),
										field->validity_meta_table_column,
										BROWSER_CONNECTION_COLUMN_PLUGIN, NULL);
			if (!plugin)
				continue;

			GdaHolder *holder;
			holder = gda_set_get_nth_holder (set, pos);
			if (holder) {
				GValue *value;
				value = gda_value_new_from_string (plugin, G_TYPE_STRING);
				gda_holder_set_attribute_static (holder, GDAUI_ATTRIBUTE_PLUGIN, value);
				gda_value_free (value);
			}
			g_free (plugin);
		}
	}
}

/**
 * ui_formgrid_handle_user_prefs
 * @formgrid: a #UiFormGrid widget
 * @bcnc: (allow-none): a #BrowserConnection, or %NULL to let @formgrid determine it itself
 * @stmt: the #GdaStatement which has been executed to produce the #GdaDataModel displayed in @formgrid
 *
 * Takes into account the UI preferences of the user
 */
void
ui_formgrid_handle_user_prefs (UiFormGrid *formgrid, BrowserConnection *bcnc, GdaStatement *stmt)
{
	g_return_if_fail (UI_IS_FORMGRID (formgrid));
	if (bcnc)
		g_return_if_fail (BROWSER_IS_CONNECTION (bcnc));
	else {
		bcnc = get_browser_connection (formgrid);
		if (!bcnc)
			return;
	}
	if (stmt)
		g_return_if_fail (GDA_IS_STATEMENT (stmt));
	else
		return;

	GdaSqlStatement *sqlst;
	g_object_get ((GObject*) stmt, "structure", &sqlst, NULL);
	if (!sqlst)
		return;
	
	GError *lerror = NULL;
	if (((sqlst->stmt_type != GDA_SQL_STATEMENT_SELECT) &&
	     (sqlst->stmt_type != GDA_SQL_STATEMENT_COMPOUND)) ||
	    !browser_connection_normalize_sql_statement (bcnc, sqlst, &lerror)) {
		if (lerror)
			g_print ("[%s]\n", lerror->message);
		goto out;	
	}
	
	handle_user_prefs_for_sql_statement (formgrid, bcnc, sqlst);
 out:
	gda_sql_statement_free (sqlst);
}



static void
ui_formgrid_set_property (GObject *object,
			  guint param_id,
			  G_GNUC_UNUSED const GValue *value,
			  GParamSpec *pspec)
{
	UiFormGrid *formgrid;
	
	formgrid = UI_FORMGRID (object);
	
	switch (param_id) {
	case PROP_SCROLL_FORM:
		formgrid->priv->scroll_form = g_value_get_boolean (value);
		break;
	case PROP_AUTOMOD:
		formgrid->priv->compute_mod_stmt = g_value_get_boolean (value);
		if (formgrid->priv->mod_stmt_auto_computed && !formgrid->priv->compute_mod_stmt) {
			/* clean up the mod stmt */
			ModType mod;
			GdaDataModel *model;
			g_object_get (formgrid->priv->raw_grid, "model", &model, NULL);
			for (mod = MOD_INSERT; mod < MOD_LAST; mod++) {
				if (! formgrid->priv->mod_stmt[mod])
					continue;
				g_object_unref (formgrid->priv->mod_stmt [mod]);
			}
			g_object_set (model, "delete-stmt", NULL,
				      "insert-stmt", NULL, NULL,
				      "update-stmt", NULL, NULL);
			g_object_unref (model);
			formgrid->priv->mod_stmt_auto_computed = FALSE;
		}
		else if (!formgrid->priv->mod_stmt_auto_computed && formgrid->priv->compute_mod_stmt) {
			GdaDataModel *model;
			g_object_get (formgrid->priv->raw_grid, "model", &model, NULL);
			compute_modification_statements (formgrid, model);
			g_object_unref (model);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
ui_formgrid_get_property (GObject *object,
			  guint param_id,
			  GValue *value,
			  GParamSpec *pspec)
{
	UiFormGrid *formgrid;

	formgrid = UI_FORMGRID (object);
	
	switch (param_id) {
	case PROP_RAW_GRID:
		g_value_set_object (value, formgrid->priv->raw_grid);
		break;
	case PROP_RAW_FORM:
		g_value_set_object (value, formgrid->priv->raw_form);
		break;
	case PROP_INFO:
		g_value_set_object (value, formgrid->priv->info);
		break;
	case PROP_AUTOMOD:
		g_value_set_boolean (value, formgrid->priv->compute_mod_stmt);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}	
}

/**
 * ui_formgrid_get_selection
 * @formgrid: a #UiFormGrid widget
 * 
 * Returns the list of the currently selected rows in a #UiFormGrid widget. 
 * The returned value is a list of integers, which represent each of the selected rows.
 *
 * If new rows have been inserted, then those new rows will have a row number equal to -1.
 * This function is a wrapper around the gdaui_raw_grid_get_selection() function.
 *
 * Returns: a new array, should be freed (by calling g_array_free() and passing %TRUE as last argument) when no longer needed.
 */
GArray *
ui_formgrid_get_selection (UiFormGrid *formgrid)
{
	g_return_val_if_fail (UI_IS_FORMGRID (formgrid), NULL);
	g_return_val_if_fail (formgrid->priv, NULL);

	return gdaui_data_selector_get_selected_rows (GDAUI_DATA_SELECTOR (formgrid->priv->raw_grid));
}

/**
 * ui_formgrid_get_form_data_set
 */
GdaDataModelIter *
ui_formgrid_get_form_data_set (UiFormGrid *formgrid)
{
	g_return_val_if_fail (UI_IS_FORMGRID (formgrid), NULL);
	g_return_val_if_fail (formgrid->priv, NULL);

	return gdaui_data_selector_get_data_set (GDAUI_DATA_SELECTOR (formgrid->priv->raw_form));
}

/**
 * ui_formgrid_get_grid_data_set
 */
GdaDataModelIter *
ui_formgrid_get_grid_data_set (UiFormGrid *formgrid)
{
	g_return_val_if_fail (UI_IS_FORMGRID (formgrid), NULL);
	g_return_val_if_fail (formgrid->priv, NULL);

	return gdaui_data_selector_get_data_set (GDAUI_DATA_SELECTOR (formgrid->priv->raw_grid));
}


/**
 * ui_formgrid_set_sample_size
 * @formgrid: a #UiFormGrid widget
 * @sample_size: the sample size
 *
 * Set the size of the sample displayed in @formgrid, see gdaui_raw_grid_set_sample_size()
 */
void
ui_formgrid_set_sample_size (UiFormGrid *formgrid, gint sample_size)
{
	g_return_if_fail (UI_IS_FORMGRID (formgrid));
	g_return_if_fail (formgrid->priv);

	gdaui_raw_grid_set_sample_size (GDAUI_RAW_GRID (formgrid->priv->raw_grid), sample_size);
}

/**
 * ui_formgrid_get_grid_widget
 * @formgrid: a #UiFormGrid widget
 *
 * Returns: the #GdauiRawGrid embedded in @formgrid
 */
GdauiRawGrid *
ui_formgrid_get_grid_widget (UiFormGrid *formgrid)
{
	g_return_val_if_fail (UI_IS_FORMGRID (formgrid), NULL);
	g_return_val_if_fail (formgrid->priv, NULL);

	return GDAUI_RAW_GRID (formgrid->priv->raw_grid);
}

/**
 * ui_formgrid_set_connection
 * @formgrid: a #UiFormGrid widget
 * @bcnc: (allow-none): a #BrowserConnection, or %NULL
 *
 * Tells @formgrid to use @bcnc as connection when actions have to be executed
 */
void
ui_formgrid_set_connection (UiFormGrid *formgrid, BrowserConnection *bcnc)
{
	g_return_if_fail (UI_IS_FORMGRID (formgrid));
	g_return_if_fail (!bcnc || BROWSER_IS_CONNECTION (bcnc));

	if (formgrid->priv->bcnc) {
		g_object_unref (formgrid->priv->bcnc);
		formgrid->priv->bcnc = NULL;
	}
	if (bcnc)
		formgrid->priv->bcnc = g_object_ref (bcnc);
}

static void
compute_modification_statements (UiFormGrid *formgrid, GdaDataModel *model)
{
	/* clear existing modification statements */
	ModType mod;
	for (mod = MOD_INSERT; mod < MOD_LAST; mod++) {
		if (formgrid->priv->mod_stmt[mod]) {
			g_object_unref (formgrid->priv->mod_stmt[mod]);
			formgrid->priv->mod_stmt[mod] = NULL;
		}
	}

	if (!model || !GDA_IS_DATA_SELECT (model))
		return;

	if (! formgrid->priv->compute_mod_stmt)
		return;

	gda_data_select_compute_modification_statements_ext (GDA_DATA_SELECT (model),
							     GDA_DATA_SELECT_COND_ALL_COLUMNS, NULL);

	formgrid->priv->mod_stmt_auto_computed = TRUE;
	g_object_get (model,
		      "insert-stmt", &formgrid->priv->mod_stmt[MOD_INSERT],
		      "update-stmt", &formgrid->priv->mod_stmt[MOD_UPDATE],
		      "delete-stmt", &formgrid->priv->mod_stmt[MOD_DELETE], NULL);

	/*
	for (mod = MOD_INSERT; mod < MOD_LAST; mod++) {
		if (formgrid->priv->mod_stmt[mod]) {
			gchar *sql;
			GError *lerror = NULL;
			sql = gda_statement_to_sql_extended (formgrid->priv->mod_stmt[mod], NULL, NULL,
							     GDA_STATEMENT_SQL_PARAMS_LONG, NULL, &lerror);
			g_print ("STMT[%d] = [%s]", mod, sql ? sql : "ERR");
			if (!sql)
				g_print (" --- %s", lerror && lerror->message ? lerror->message : "No detail");
			g_clear_error (&lerror);
			g_print ("\n");
			g_free (sql);
		}
		else
			g_print ("STMT[%d] = ---\n", mod);
	}
	*/
}
