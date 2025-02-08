/*
 * Copyright (C) 2009 - 2011 Vivien Malerba <malerba@gnome-db.org>
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

#include <string.h>
#include <glib/gi18n-lib.h>
#include "browser-window.h"
#include <libgda/binreloc/gda-binreloc.h>
#include "login-dialog.h"
#include "browser-core.h"
#include "support.h"
#include "browser-perspective.h"
#include "browser-connection.h"
#include "browser-connections-list.h"
#include "browser-spinner.h"
#include "browser-stock-icons.h"
#include "connection-binding-properties.h"
#include <gdk/gdkkeysyms.h>

/*
 * structure representing a 'tab' in a window
 *
 * a 'page' is a set of widgets created by a single BrowserPerspectiveFactory object, for a connection
 * Each is owned by a BrowserWindow
 */
typedef struct {
        BrowserWindow      *bwin; /* pointer to window the tab is in, no ref held here */
        BrowserPerspectiveFactory *factory;
        gint                page_number; /* in reference to bwin->perspectives_nb */
        BrowserPerspective  *perspective_widget;

	GtkActionGroup      *customized_actions;
	guint                customized_merge_id;
	gchar               *customized_ui;
} PerspectiveData;
#define PERSPECTIVE_DATA(x) ((PerspectiveData*)(x))
static PerspectiveData *perspective_data_new (BrowserWindow *bwin, BrowserPerspectiveFactory *factory);
static void         perspective_data_free (PerspectiveData *pers);

/* 
 * Main static functions 
 */
static void browser_window_class_init (BrowserWindowClass *klass);
static void browser_window_init (BrowserWindow *bwin);
static void browser_window_dispose (GObject *object);

static gboolean window_state_event (GtkWidget *widget, GdkEventWindowState *event);
static gboolean key_press_event (GtkWidget *widget, GdkEventKey *event);
static void connection_busy_cb (BrowserConnection *bcnc, gboolean is_busy, gchar *reason, BrowserWindow *bwin);

static void connection_added_cb (BrowserCore *bcore, BrowserConnection *bcnc, BrowserWindow *bwin);
static void connection_removed_cb (BrowserCore *bcore, BrowserConnection *bcnc, BrowserWindow *bwin);

static void transaction_status_changed_cb (BrowserConnection *bcnc, BrowserWindow *bwin);


enum {
        FULLSCREEN_CHANGED,
        LAST_SIGNAL
};

static gint browser_window_signals[LAST_SIGNAL] = { 0 };


/* get a pointer to the parents to be able to call their destructor */
static GObjectClass  *parent_class = NULL;

struct _BrowserWindowPrivate {
	BrowserConnection *bcnc;
	GtkNotebook       *perspectives_nb; /* notebook used to switch between tabs, for the selector part */
        GSList            *perspectives; /* list of PerspectiveData pointers, owned here */
	PerspectiveData   *current_perspective;
	guint              ui_manager_merge_id; /* for current perspective */

	GtkWidget         *menubar;
	GtkWidget         *toolbar;
	gboolean           toolbar_shown;
	gboolean           cursor_in_toolbar;
	GtkWidget         *spinner;
	guint              spinner_timer;
	GtkUIManager      *ui_manager;
	GtkActionGroup    *agroup;
	GtkActionGroup    *perspectives_actions;
	gboolean           updating_transaction_status;

	GtkToolbarStyle    toolbar_style;
	GtkActionGroup    *cnc_agroup; /* one GtkAction for each BrowserConnection */
	gulong             cnc_added_sigid;
	gulong             cnc_removed_sigid;

	GtkWidget         *notif_box;
	GSList            *notif_widgets;

	GtkWidget         *statusbar;
	guint              cnc_statusbar_context;

	gboolean           fullscreen;
	gulong             fullscreen_motion_sig_id;
	guint              fullscreen_timer_id;
};

GType
browser_window_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GMutex registering;
		static const GTypeInfo info = {
			sizeof (BrowserWindowClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) browser_window_class_init,
			NULL,
			NULL,
			sizeof (BrowserWindow),
			0,
			(GInstanceInitFunc) browser_window_init,
			0
		};

		
		g_mutex_lock (&registering);
		if (type == 0)
			type = g_type_register_static (GTK_TYPE_WINDOW, "BrowserWindow", &info, 0);
		g_mutex_unlock (&registering);
	}
	return type;
}

static void
browser_window_class_init (BrowserWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->window_state_event = window_state_event;
	widget_class->key_press_event = key_press_event;
	parent_class = g_type_class_peek_parent (klass);

	browser_window_signals[FULLSCREEN_CHANGED] =
                g_signal_new ("fullscreen_changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (BrowserWindowClass, fullscreen_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	object_class->dispose = browser_window_dispose;
	klass->fullscreen_changed = NULL;
}

static void
browser_window_init (BrowserWindow *bwin)
{
	bwin->priv = g_new0 (BrowserWindowPrivate, 1);
	bwin->priv->bcnc = NULL;
	bwin->priv->perspectives_nb = NULL;
	bwin->priv->perspectives = NULL;
	bwin->priv->cnc_agroup = NULL;
	bwin->priv->cnc_added_sigid = 0;
	bwin->priv->cnc_removed_sigid = 0;
	bwin->priv->updating_transaction_status = FALSE;
	bwin->priv->fullscreen = FALSE;
	bwin->priv->fullscreen_motion_sig_id = 0;
	bwin->priv->fullscreen_timer_id = 0;
	bwin->priv->spinner_timer = 0;
}

static void
browser_window_dispose (GObject *object)
{
	BrowserWindow *bwin;

	g_return_if_fail (object != NULL);
	g_return_if_fail (BROWSER_IS_WINDOW (object));

	bwin = BROWSER_WINDOW (object);
	if (bwin->priv) {
		GSList *connections, *list;

		if (bwin->priv->spinner_timer) {
			g_source_remove (bwin->priv->spinner_timer);
			bwin->priv->spinner_timer = 0;
		}
		connections = browser_core_get_connections ();
		for (list = connections; list; list = list->next)
			connection_removed_cb (browser_core_get(), BROWSER_CONNECTION (list->data), bwin);
		g_slist_free (connections);

		if (bwin->priv->fullscreen_timer_id)
			g_source_remove (bwin->priv->fullscreen_timer_id);

		if (bwin->priv->fullscreen_motion_sig_id)
			g_signal_handler_disconnect (bwin, bwin->priv->fullscreen_motion_sig_id);

		if (bwin->priv->cnc_added_sigid > 0)
			g_signal_handler_disconnect (browser_core_get (), bwin->priv->cnc_added_sigid);
		if (bwin->priv->cnc_removed_sigid > 0)
			g_signal_handler_disconnect (browser_core_get (), bwin->priv->cnc_removed_sigid);
		if (bwin->priv->ui_manager)
			g_object_unref (bwin->priv->ui_manager);
		if (bwin->priv->cnc_agroup)
			g_object_unref (bwin->priv->cnc_agroup);

		if (bwin->priv->bcnc) {
			g_signal_handlers_disconnect_by_func (bwin->priv->bcnc,
							      G_CALLBACK (transaction_status_changed_cb),
							      bwin);
			g_object_unref (bwin->priv->bcnc);
		}
		if (bwin->priv->perspectives) {
			g_slist_foreach (bwin->priv->perspectives, (GFunc) perspective_data_free, NULL);
			g_slist_free (bwin->priv->perspectives);
		}
		if (bwin->priv->perspectives_nb)
			g_object_unref (bwin->priv->perspectives_nb);

		if (bwin->priv->notif_widgets)
			g_slist_free (bwin->priv->notif_widgets);
		g_free (bwin->priv);
		bwin->priv = NULL;
	}

	/* parent class */
	parent_class->dispose (object);
}


static gboolean
delete_event (G_GNUC_UNUSED GtkWidget *widget, G_GNUC_UNUSED GdkEvent *event, BrowserWindow *bwin)
{
	browser_core_close_window (bwin);
        return TRUE;
}

static void transaction_begin_cb (GtkAction *action, BrowserWindow *bwin);
static void transaction_commit_cb (GtkAction *action, BrowserWindow *bwin);
static void transaction_rollback_cb (GtkAction *action, BrowserWindow *bwin);
static void quit_cb (GtkAction *action, BrowserWindow *bwin);
static void about_cb (GtkAction *action, BrowserWindow *bwin);
#ifdef HAVE_GDU
static void manual_cb (GtkAction *action, BrowserWindow *bwin);
#endif
static void window_close_cb (GtkAction *action, BrowserWindow *bwin);
static void window_fullscreen_cb (GtkToggleAction *action, BrowserWindow *bwin);
static void window_new_cb (GtkAction *action, BrowserWindow *bwin);
static void window_new_with_cnc_cb (GtkAction *action, BrowserWindow *bwin);
static void connection_properties_cb (GtkAction *action, BrowserWindow *bwin);
static void connection_close_cb (GtkAction *action, BrowserWindow *bwin);
static void connection_open_cb (GtkAction *action, BrowserWindow *bwin);
static void connection_bind_cb (GtkAction *action, BrowserWindow *bwin);
static void connection_list_cb (GtkAction *action, BrowserWindow *bwin);
static void connection_meta_update_cb (GtkAction *action, BrowserWindow *bwin);
static void perspective_toggle_cb (GtkRadioAction *action, GtkRadioAction *current, BrowserWindow *bwin);

static const GtkToggleActionEntry ui_toggle_actions [] =
{
        { "WindowFullScreen", GTK_STOCK_FULLSCREEN, N_("_Fullscreen"), "F11", N_("Use the whole screen"), G_CALLBACK (window_fullscreen_cb), FALSE}
};

static const GtkActionEntry ui_actions[] = {
        { "Connection", NULL, N_("_Connection"), NULL, N_("Connection"), NULL },
        { "ConnectionOpen", GTK_STOCK_CONNECT, N_("_Connect"), NULL, N_("Open a connection"), G_CALLBACK (connection_open_cb)},
        { "ConnectionBind", NULL, N_("_Bind Connection"), "<control>I", N_("Use connection to create\n"
						    "a new binding connection to access data\n"
						    "from multiple databases at once"), G_CALLBACK (connection_bind_cb)},
        { "ConnectionProps", GTK_STOCK_PROPERTIES, N_("_Properties"), NULL, N_("Connection properties"), G_CALLBACK (connection_properties_cb)},
        { "ConnectionList", NULL, N_("_Connections List"), NULL, N_("Connections list"), G_CALLBACK (connection_list_cb)},
        { "ConnectionMetaSync", GTK_STOCK_REFRESH, N_("_Fetch Meta Data"), NULL, N_("Fetch meta data"), G_CALLBACK (connection_meta_update_cb)},
        { "ConnectionClose", GTK_STOCK_CLOSE, N_("_Close Connection"), NULL, N_("Close this connection"), G_CALLBACK (connection_close_cb)},
        { "Quit", GTK_STOCK_QUIT, N_("_Quit"), NULL, N_("Quit"), G_CALLBACK (quit_cb)},
        { "Edit", NULL, N_("_Edit"), NULL, N_("Edit"), NULL },
        { "Display", NULL, N_("_Display"), NULL, N_("Display"), NULL },
        { "Perspective", NULL, N_("_Perspective"), NULL, N_("Perspective"), NULL },
        { "Window", NULL, N_("_Window"), NULL, N_("Window"), NULL },
        { "WindowNew", STOCK_NEW_WINDOW, N_("_New Window"), "<control>N", N_("Open a new window for current connection"), G_CALLBACK (window_new_cb)},
        { "WindowNewOthers", NULL, N_("New Window for _Connection"), NULL, N_("Open a new window for a connection"), NULL},
        { "WindowClose", GTK_STOCK_CLOSE, N_("_Close"), NULL, N_("Close this window"), G_CALLBACK (window_close_cb)},
        { "Help", NULL, N_("_Help"), NULL, N_("Help"), NULL },
        { "HelpAbout", GTK_STOCK_ABOUT, N_("_About"), NULL, N_("About"), G_CALLBACK (about_cb) },
#ifdef HAVE_GDU
        { "HelpManual", GTK_STOCK_HELP, N_("_Manual"), "F1", N_("Manual"), G_CALLBACK (manual_cb) },
#endif
	{ "TransactionBegin", BROWSER_STOCK_BEGIN, N_("Begin"), NULL, N_("Begin a new transaction"),
          G_CALLBACK (transaction_begin_cb)},
        { "TransactionCommit", BROWSER_STOCK_COMMIT, N_("Commit"), NULL, N_("Commit current transaction"),
          G_CALLBACK (transaction_commit_cb)},
        { "TransactionRollback", BROWSER_STOCK_ROLLBACK, N_("Rollback"), NULL, N_("Rollback current transaction"),
          G_CALLBACK (transaction_rollback_cb)},
};

static const gchar *ui_actions_info =
        "<ui>"
        "  <menubar name='MenuBar'>"
        "    <menu name='Connection' action='Connection'>"
        "      <menuitem name='ConnectionOpen' action= 'ConnectionOpen'/>"
        "      <menuitem name='ConnectionList' action= 'ConnectionList'/>"
        "      <menuitem name='ConnectionMetaSync' action= 'ConnectionMetaSync'/>"
        "      <separator/>"
        "      <menuitem name='ConnectionProps' action= 'ConnectionProps'/>"
        "      <menuitem name='ConnectionBind' action= 'ConnectionBind'/>"
        "      <menuitem name='ConnectionClose' action= 'ConnectionClose'/>"
        "      <separator/>"
	"      <menuitem name='TransactionBegin' action= 'TransactionBegin'/>"
        "      <menuitem name='TransactionCommit' action= 'TransactionCommit'/>"
        "      <menuitem name='TransactionRollback' action= 'TransactionRollback'/>"
        "      <separator/>"
        "      <menuitem name='Quit' action= 'Quit'/>"
        "      <separator/>"
        "    </menu>"
        "    <menu name='Edit' action='Edit'>"
        "    </menu>"
        "    <menu name='Display' action='Display'>"
        "    </menu>"
        "    <menu name='Perspective' action='Perspective'>"
        "      <placeholder name='PersList'/>"
        "    </menu>"
        "    <menu name='Window' action='Window'>"
        "      <menuitem name='WindowFullScreen' action= 'WindowFullScreen'/>"
        "      <separator/>"
        "      <menuitem name='WindowNew' action= 'WindowNew'/>"
        "      <menu name='WindowNewOthers' action='WindowNewOthers'>"
        "          <placeholder name='CncList'/>"
        "      </menu>"
        "      <separator/>"
        "      <menuitem name='WindowClose' action= 'WindowClose'/>"
        "    </menu>"
	"    <placeholder name='MenuExtension'/>"
        "    <menu name='Help' action='Help'>"
#ifdef HAVE_GDU
        "      <menuitem name='HelpManual' action= 'HelpManual'/>"
#endif
        "      <menuitem name='HelpAbout' action= 'HelpAbout'/>"
        "    </menu>"
        "  </menubar>"
        "  <toolbar name='ToolBar'>"
        "    <toolitem action='WindowClose'/>"
        "    <toolitem action='WindowFullScreen'/>"
        "    <toolitem action='TransactionBegin'/>"
        "    <toolitem action='TransactionCommit'/>"
        "    <toolitem action='TransactionRollback'/>"
        "  </toolbar>"
        "</ui>";

/**
 * browser_window_new
 * @bcnc: a #BrowserConnection
 * @factory: a #BrowserPerspectiveFactory, may be %NULL
 *
 * Creates a new #BrowserWindow window for the @bcnc connection, and displays it.
 * If @factory is not %NULL, then the new window will show the perspective corresponding
 * to @factory. If it's %NULL, then the default #BrowserPerspectiveFactory will be used,
 * see browser_core_get_default_factory().
 *
 * Don't forget to call browser_core_take_window() to have the new window correctly
 * managed by the browser. Similarly, to close the window, use browser_core_close_window()
 * and not simply gtk_widget_destroy().
 *
 * Returns: the new object
 */
BrowserWindow*
browser_window_new (BrowserConnection *bcnc, BrowserPerspectiveFactory *factory)
{
	BrowserWindow *bwin;

	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);

	bwin = BROWSER_WINDOW (g_object_new (BROWSER_TYPE_WINDOW, NULL));
	bwin->priv->bcnc = g_object_ref (bcnc);
	g_signal_connect (bcnc, "transaction-status-changed",
			  G_CALLBACK (transaction_status_changed_cb), bwin);

	gchar *tmp;
	tmp = browser_connection_get_long_name (bcnc);
	gtk_window_set_title (GTK_WINDOW (bwin), tmp);
	g_free (tmp);

	gtk_window_set_default_size ((GtkWindow*) bwin, 900, 650);
	g_signal_connect (G_OBJECT (bwin), "delete-event",
                          G_CALLBACK (delete_event), bwin);
	/* icon */
	GdkPixbuf *icon;
        gchar *path;
        path = gda_gbr_get_file_path (GDA_DATA_DIR, LIBGDA_ABI_NAME, "pixmaps", "gda-browser.png", NULL);
        icon = gdk_pixbuf_new_from_file (path, NULL);
        g_free (path);
        if (icon) {
                gtk_window_set_icon (GTK_WINDOW (bwin), icon);
                g_object_unref (icon);
        }

	/* main VBox */
	GtkWidget *vbox;
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add (GTK_CONTAINER (bwin), vbox);
        gtk_widget_show (vbox);

	/* menu */
	GtkWidget *menubar;
        GtkWidget *toolbar;
        GtkUIManager *ui;
	GtkActionGroup *group;

        group = gtk_action_group_new ("Actions");
	gtk_action_group_set_translation_domain (group, GETTEXT_PACKAGE);

	bwin->priv->agroup = group;
        gtk_action_group_add_actions (group, ui_actions, G_N_ELEMENTS (ui_actions), bwin);
	gtk_action_group_add_toggle_actions (group, ui_toggle_actions, G_N_ELEMENTS (ui_toggle_actions), bwin);
	if (browser_connection_is_virtual (bwin->priv->bcnc)) {
		GtkAction *action;
		action = gtk_action_group_get_action (bwin->priv->agroup, "TransactionBegin");
		gtk_action_set_visible (action, FALSE);
		action = gtk_action_group_get_action (bwin->priv->agroup, "TransactionCommit");
		gtk_action_set_visible (action, FALSE);
		action = gtk_action_group_get_action (bwin->priv->agroup, "TransactionRollback");
		gtk_action_set_visible (action, FALSE);
	}
	transaction_status_changed_cb (bwin->priv->bcnc, bwin);

        ui = gtk_ui_manager_new ();
        gtk_ui_manager_insert_action_group (ui, group, 0);
        gtk_ui_manager_add_ui_from_string (ui, ui_actions_info, -1, NULL);
	bwin->priv->ui_manager = ui;

	GtkAccelGroup *accel_group;
        accel_group = gtk_ui_manager_get_accel_group (ui);
	gtk_window_add_accel_group (GTK_WINDOW (bwin), accel_group);

        menubar = gtk_ui_manager_get_widget (ui, "/MenuBar");
	bwin->priv->menubar = menubar;
#ifdef HAVE_MAC_INTEGRATION
	gtk_osxapplication_set_menu_bar (theApp, GTK_MENU_SHELL (menubar));
#else
        gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, FALSE, 0);
        gtk_widget_show (menubar);
#endif

        toolbar = gtk_ui_manager_get_widget (ui, "/ToolBar");
	bwin->priv->toolbar = toolbar;
	bwin->priv->toolbar_shown = TRUE;
        gtk_box_pack_start (GTK_BOX (vbox), toolbar, FALSE, TRUE, 0);
	gtk_toolbar_set_show_arrow (GTK_TOOLBAR (toolbar), TRUE);
        gtk_widget_show (toolbar);
	bwin->priv->toolbar_style = gtk_toolbar_get_style (GTK_TOOLBAR (toolbar));

	bwin->priv->notif_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start (GTK_BOX (vbox), bwin->priv->notif_box, FALSE, FALSE, 0);
        gtk_widget_show (bwin->priv->notif_box);
	bwin->priv->notif_widgets = NULL;

	GtkToolItem *ti;
	GtkWidget *spinner, *svbox, *align;

	ti = gtk_separator_tool_item_new ();
	gtk_separator_tool_item_set_draw (GTK_SEPARATOR_TOOL_ITEM (ti), FALSE);
	gtk_tool_item_set_expand (ti, TRUE);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), ti, -1);
        gtk_widget_show (GTK_WIDGET (ti));

	spinner = browser_spinner_new ();
	browser_spinner_set_size ((BrowserSpinner*) spinner, GTK_ICON_SIZE_SMALL_TOOLBAR);

	svbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	align = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (align), spinner);
	gtk_box_pack_start (GTK_BOX (svbox), align, TRUE, TRUE, 0);

	ti = gtk_tool_item_new  ();
	gtk_container_add (GTK_CONTAINER (ti), svbox);
	gtk_toolbar_insert (GTK_TOOLBAR (toolbar), ti, -1);
        gtk_widget_show_all (GTK_WIDGET (ti));
	gtk_widget_hide (GTK_WIDGET (spinner));
	bwin->priv->spinner = spinner;

	/* statusbar */
	bwin->priv->statusbar = gtk_statusbar_new ();

	GSList *connections, *list;
	bwin->priv->cnc_agroup = gtk_action_group_new ("CncActions");
	gtk_action_group_set_translation_domain (bwin->priv->cnc_agroup, GETTEXT_PACKAGE);

	connections = browser_core_get_connections ();
	for (list = connections; list; list = list->next)
		connection_added_cb (browser_core_get(), BROWSER_CONNECTION (list->data), bwin);
	g_slist_free (connections);

	gtk_ui_manager_insert_action_group (bwin->priv->ui_manager, bwin->priv->cnc_agroup, 0);
	bwin->priv->cnc_added_sigid = g_signal_connect (browser_core_get (), "connection-added",
							G_CALLBACK (connection_added_cb), bwin);
	bwin->priv->cnc_removed_sigid = g_signal_connect (browser_core_get (), "connection-removed",
							  G_CALLBACK (connection_removed_cb), bwin);


	/* create a PerspectiveData */
	PerspectiveData *pers;
	if (! factory && browser_connection_is_ldap (bcnc))
		factory = browser_core_get_factory (_("LDAP browser"));
	pers = perspective_data_new (bwin, factory);
	bwin->priv->perspectives = g_slist_prepend (bwin->priv->perspectives, pers);
	GtkActionGroup *actions;
	actions = browser_perspective_get_actions_group (BROWSER_PERSPECTIVE (pers->perspective_widget));
	if (actions) {
		gtk_action_group_set_translation_domain (actions, GETTEXT_PACKAGE);
		gtk_ui_manager_insert_action_group (bwin->priv->ui_manager, actions, 0);
		g_object_unref (actions);
	}
	const gchar *ui_info;
	ui_info = browser_perspective_get_actions_ui (BROWSER_PERSPECTIVE (pers->perspective_widget));
	if (ui_info)
		bwin->priv->ui_manager_merge_id = gtk_ui_manager_add_ui_from_string (bwin->priv->ui_manager,
										     ui_info, -1, NULL);
	bwin->priv->current_perspective = pers;
	browser_perspective_get_current_customization (BROWSER_PERSPECTIVE (pers->perspective_widget),
						       &actions, &ui_info);
	browser_window_customize_perspective_ui (bwin, BROWSER_PERSPECTIVE (pers->perspective_widget),
						 actions, ui_info);
	if (actions)
		g_object_unref (actions);
	
	/* insert perspective into window */
        bwin->priv->perspectives_nb = (GtkNotebook*) gtk_notebook_new ();
        g_object_ref (bwin->priv->perspectives_nb);
        gtk_notebook_set_show_tabs (bwin->priv->perspectives_nb, FALSE);
	gtk_box_pack_start (GTK_BOX (vbox), (GtkWidget*) bwin->priv->perspectives_nb,
			    TRUE, TRUE, 0);

	pers->page_number = gtk_notebook_append_page (bwin->priv->perspectives_nb,
						      GTK_WIDGET (pers->perspective_widget), NULL);
        gtk_widget_show_all ((GtkWidget*) bwin->priv->perspectives_nb);
	gtk_widget_grab_focus (GTK_WIDGET (pers->perspective_widget));

	/* build the perspectives menu */
	GtkActionGroup *agroup;
	const GSList *plist;
	GSList *radio_group = NULL;
	guint mid;

	mid = gtk_ui_manager_new_merge_id (bwin->priv->ui_manager);
	agroup = gtk_action_group_new ("Perspectives");
	gtk_action_group_set_translation_domain (agroup, GETTEXT_PACKAGE);

	gtk_ui_manager_insert_action_group (bwin->priv->ui_manager, agroup, 0);
	bwin->priv->perspectives_actions = agroup;
	g_object_unref (agroup);

	GtkAction *active_action = NULL;
	for (plist = browser_core_get_factories (); plist; plist = plist->next) {
		GtkAction *action;
		const gchar *name;

		name = BROWSER_PERSPECTIVE_FACTORY (plist->data)->perspective_name;
		if (!strcmp (name, _("LDAP browser")) && !browser_connection_is_ldap (bcnc))
			continue;

		action = GTK_ACTION (gtk_radio_action_new (name, name, NULL, NULL, FALSE));

		if (!active_action && 
		    ((factory && (BROWSER_PERSPECTIVE_FACTORY (plist->data) == factory)) ||
		     (!factory && (BROWSER_PERSPECTIVE_FACTORY (plist->data) == browser_core_get_default_factory ()))))
			active_action = action;
		if (BROWSER_PERSPECTIVE_FACTORY (plist->data)->menu_shortcut)
			gtk_action_group_add_action_with_accel (agroup, action,
								BROWSER_PERSPECTIVE_FACTORY (plist->data)->menu_shortcut);
		else
			gtk_action_group_add_action (agroup, action);
		
		gtk_radio_action_set_group (GTK_RADIO_ACTION (action), radio_group);
		radio_group = gtk_radio_action_get_group (GTK_RADIO_ACTION (action));

		g_object_set_data (G_OBJECT (action), "pers", plist->data);
		g_signal_connect (action, "changed",
				  G_CALLBACK (perspective_toggle_cb), bwin);

		g_object_unref (action);

		gtk_ui_manager_add_ui (bwin->priv->ui_manager, mid,  "/MenuBar/Perspective/PersList",
				       name, name,
				       GTK_UI_MANAGER_AUTO, FALSE);
	}
	
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (active_action), TRUE);

	gtk_box_pack_start (GTK_BOX (vbox), bwin->priv->statusbar, FALSE, FALSE, 0);
        gtk_widget_show (bwin->priv->statusbar);
	bwin->priv->cnc_statusbar_context = gtk_statusbar_get_context_id (GTK_STATUSBAR (bwin->priv->statusbar),
									  "cncbusy");

        gtk_widget_show (GTK_WIDGET (bwin));

	gtk_widget_set_can_focus ((GtkWidget* )pers->perspective_widget, TRUE);
	gtk_widget_grab_focus ((GtkWidget* )pers->perspective_widget);

	return bwin;
}

static void
perspective_toggle_cb (GtkRadioAction *action, GtkRadioAction *current, BrowserWindow *bwin)
{
	BrowserPerspectiveFactory *pf;
	GSList *list;
	PerspectiveData *pers;
	if (action != current)
		return;

	pf = BROWSER_PERSPECTIVE_FACTORY (g_object_get_data (G_OBJECT (action), "pers"));
	g_assert (pf);

	/* current perspective's cleanups */
	if (bwin->priv->current_perspective) {
		pers = bwin->priv->current_perspective;
		if (pers->customized_merge_id) {
			gtk_ui_manager_remove_ui (bwin->priv->ui_manager, pers->customized_merge_id);
			pers->customized_merge_id = 0;
		}
		bwin->priv->current_perspective = NULL;
	}

	/* check if perspective already exists */
	for (list = bwin->priv->perspectives, pers = NULL; list; list = list->next) {
		if (PERSPECTIVE_DATA (list->data)->factory == pf) {
			pers = PERSPECTIVE_DATA (list->data);
			break;
		}
	}

	if (!pers) {
		pers = perspective_data_new (bwin, pf);
		bwin->priv->perspectives = g_slist_prepend (bwin->priv->perspectives, pers);
		pers->page_number = gtk_notebook_append_page (bwin->priv->perspectives_nb,
							      GTK_WIDGET (pers->perspective_widget), NULL);
		gtk_widget_show (GTK_WIDGET (pers->perspective_widget));

		GtkActionGroup *actions;
		actions = browser_perspective_get_actions_group (BROWSER_PERSPECTIVE (pers->perspective_widget));
		if (actions) {
			gtk_action_group_set_translation_domain (actions, GETTEXT_PACKAGE);
			gtk_ui_manager_insert_action_group (bwin->priv->ui_manager, actions, 0);
			g_object_unref (actions);
		}
	}

	gtk_notebook_set_current_page (bwin->priv->perspectives_nb, pers->page_number);

	/* menus and toolbar handling */
	if (bwin->priv->ui_manager_merge_id > 0) {
		gtk_ui_manager_remove_ui (bwin->priv->ui_manager, bwin->priv->ui_manager_merge_id);
		bwin->priv->ui_manager_merge_id = 0;
	}

	const gchar *ui_info;
	ui_info = browser_perspective_get_actions_ui (BROWSER_PERSPECTIVE (pers->perspective_widget));
	if (ui_info)
		bwin->priv->ui_manager_merge_id = gtk_ui_manager_add_ui_from_string (bwin->priv->ui_manager,
										     ui_info, -1, NULL);

	/* current perspective's customizations */
	bwin->priv->current_perspective = pers;

	GtkActionGroup *actions;
	browser_perspective_get_current_customization (BROWSER_PERSPECTIVE (pers->perspective_widget),
						       &actions, &ui_info);
	browser_window_customize_perspective_ui (bwin, BROWSER_PERSPECTIVE (pers->perspective_widget),
						 actions, ui_info);
	if (actions)
		g_object_unref (actions);
}

static gboolean
spinner_stop_timer (BrowserWindow *bwin)
{
	browser_spinner_stop (BROWSER_SPINNER (bwin->priv->spinner));
	bwin->priv->spinner_timer = 0;
	return FALSE; /* remove timer */
}

static void
connection_busy_cb (BrowserConnection *bcnc, gboolean is_busy, gchar *reason, BrowserWindow *bwin)
{
	GtkAction *action;
	if (bcnc == bwin->priv->bcnc) {
		/* @bcbc is @bwin's own connection */
		if (bwin->priv->spinner_timer) {
			g_source_remove (bwin->priv->spinner_timer);
			bwin->priv->spinner_timer = 0;
		}
		if (is_busy) {
			browser_spinner_start (BROWSER_SPINNER (bwin->priv->spinner));
			gtk_widget_set_tooltip_text (bwin->priv->spinner, reason);
			gtk_statusbar_push (GTK_STATUSBAR (bwin->priv->statusbar),
					    bwin->priv->cnc_statusbar_context,
					    reason);
		}
		else {
			bwin->priv->spinner_timer = g_timeout_add (300,
						    (GSourceFunc) spinner_stop_timer, bwin);
			gtk_widget_set_tooltip_text (bwin->priv->spinner, NULL);
			gtk_statusbar_pop (GTK_STATUSBAR (bwin->priv->statusbar),
					   bwin->priv->cnc_statusbar_context);
		}

		gboolean bsens = FALSE, csens = FALSE;
		if (!is_busy) {
			if (browser_connection_get_transaction_status (bcnc))
				csens = TRUE;
			else
				bsens = TRUE;
		}
		action = gtk_action_group_get_action (bwin->priv->agroup, "TransactionBegin");
		gtk_action_set_sensitive (action, bsens);
		action = gtk_action_group_get_action (bwin->priv->agroup, "TransactionCommit");
		gtk_action_set_sensitive (action, csens);
		action = gtk_action_group_get_action (bwin->priv->agroup, "TransactionRollback");
		gtk_action_set_sensitive (action, csens);

		action = gtk_action_group_get_action (bwin->priv->agroup, "WindowNew");
		gtk_action_set_sensitive (action, !is_busy);
		action = gtk_action_group_get_action (bwin->priv->agroup, "ConnectionMetaSync");
		gtk_action_set_sensitive (action, !is_busy);
	}

	gchar *cncname;
	cncname = browser_connection_get_long_name (bcnc);
	action = gtk_action_group_get_action (bwin->priv->cnc_agroup, cncname);
	g_free (cncname);
	if (action)
		gtk_action_set_sensitive (action, !is_busy);
}

/* update @bwin->priv->cnc_agroup and @bwin->priv->ui_manager */
static void
connection_added_cb (G_GNUC_UNUSED BrowserCore *bcore, BrowserConnection *bcnc, BrowserWindow *bwin)
{
	GtkAction *action;
	gchar *cncname;
	guint mid;

	mid = gtk_ui_manager_new_merge_id (bwin->priv->ui_manager);
	cncname = browser_connection_get_long_name (bcnc);
	action = gtk_action_new (cncname, cncname, NULL, NULL);
	gtk_action_group_add_action (bwin->priv->cnc_agroup, action);
	guint *amid = g_new (guint, 1);
	*amid = mid;
	g_object_set_data_full (G_OBJECT (action), "mid", amid, g_free);
	
	gtk_ui_manager_add_ui (bwin->priv->ui_manager, mid, "/MenuBar/Window/WindowNewOthers/CncList",
			       cncname, cncname,
			       GTK_UI_MANAGER_AUTO, FALSE);
	g_free (cncname);
	g_signal_connect (action, "activate",
			  G_CALLBACK (window_new_with_cnc_cb), bwin);
	g_object_set_data (G_OBJECT (action), "bcnc", bcnc);
	gtk_action_set_sensitive (action, ! browser_connection_is_busy (bcnc, NULL));
	g_object_unref (action);

	gchar *reason = NULL;
	if (browser_connection_is_busy (bcnc, &reason)) {
		connection_busy_cb (bcnc, TRUE, reason, bwin);
		g_free (reason);
	}
	g_signal_connect (bcnc, "busy",
			  G_CALLBACK (connection_busy_cb), bwin);
}

/* update @bwin->priv->cnc_agroup and @bwin->priv->ui_manager */
static void
connection_removed_cb (G_GNUC_UNUSED BrowserCore *bcore, BrowserConnection *bcnc, BrowserWindow *bwin)
{
	GtkAction *action;
	gchar *path;
	gchar *cncname;
	guint *mid;

	cncname = browser_connection_get_long_name (bcnc);
	path = g_strdup_printf ("/MenuBar/Window/WindowNewOthers/CncList/%s", cncname);
	g_free (cncname);
	action = gtk_ui_manager_get_action (bwin->priv->ui_manager, path);
	g_free (path);
	g_assert (action);

	mid = g_object_get_data (G_OBJECT (action), "mid");
	g_assert (mid);
	
	gtk_ui_manager_remove_ui (bwin->priv->ui_manager, *mid);
	gtk_action_group_remove_action (bwin->priv->cnc_agroup, action);

	g_signal_handlers_disconnect_by_func (bcnc,
					      G_CALLBACK (connection_busy_cb), bwin);
}

static void
connection_close_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	/* confirmation dialog */
	GtkWidget *dialog;
	char *str;
	BrowserConnection *bcnc;
	bcnc = browser_window_get_connection (bwin);
		
	str = g_strdup_printf (_("Do you want to close the '%s' connection?"),
			       browser_connection_get_name (bcnc));
	dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (bwin), GTK_DIALOG_MODAL,
						     GTK_MESSAGE_QUESTION,
						     GTK_BUTTONS_YES_NO,
						     "<b>%s</b>", str);
	g_free (str);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_YES) {
		/* actual connection closing */
		browser_core_close_connection (bcnc);

		/* list all the windows using bwin's connection */
		GSList *list, *windows, *bwin_list = NULL;

		windows = browser_core_get_windows ();
		for (list = windows; list; list = list->next) {
			if (browser_window_get_connection (BROWSER_WINDOW (list->data)) == bcnc)
				bwin_list = g_slist_prepend (bwin_list, list->data);
		}
		g_slist_free (windows);

		for (list = bwin_list; list; list = list->next)
			delete_event (NULL, NULL, BROWSER_WINDOW (list->data));
		g_slist_free (bwin_list);
	}
	gtk_widget_destroy (dialog);
}

static void
quit_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	/* confirmation dialog */
	GtkWidget *dialog;
	GSList *connections;

	connections = browser_core_get_connections ();
	if (connections && connections->next)
		dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (bwin), GTK_DIALOG_MODAL,
							     GTK_MESSAGE_QUESTION,
							     GTK_BUTTONS_YES_NO,
							     "<b>%s</b>\n<small>%s</small>",
							     _("Do you want to quit the application?"),
							     _("all the connections will be closed."));
	else
		dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (bwin), GTK_DIALOG_MODAL,
							     GTK_MESSAGE_QUESTION,
							     GTK_BUTTONS_YES_NO,
							     "<b>%s</b>\n<small>%s</small>",
							     _("Do you want to quit the application?"),
							     _("the connection will be closed."));
	

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_YES) {
		/* actual connection closing */
		GSList *list;
		for (list = connections; list; list = list->next)
			browser_core_close_connection (BROWSER_CONNECTION (list->data));

		/* list all the windows using bwin's connection */
		GSList *windows;
		windows = browser_core_get_windows ();
		for (list = windows; list; list = list->next)
			delete_event (NULL, NULL, BROWSER_WINDOW (list->data));
		g_slist_free (windows);
	}
	g_slist_free (connections);
	gtk_widget_destroy (dialog);	
}

static void
transaction_status_changed_cb (BrowserConnection *bcnc, BrowserWindow *bwin)
{
	if (!bwin->priv->agroup)
		return;

	GtkAction *action;
	gboolean trans_started;

	trans_started = browser_connection_get_transaction_status (bcnc) ? TRUE : FALSE;
	bwin->priv->updating_transaction_status = TRUE;

	action = gtk_action_group_get_action (bwin->priv->agroup, "TransactionBegin");
	gtk_action_set_sensitive (action, !trans_started);

	action = gtk_action_group_get_action (bwin->priv->agroup, "TransactionCommit");
	gtk_action_set_sensitive (action, trans_started);

	action = gtk_action_group_get_action (bwin->priv->agroup, "TransactionRollback");
	gtk_action_set_sensitive (action, trans_started);
				      
	bwin->priv->updating_transaction_status = FALSE;
}

static void
transaction_begin_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	if (!bwin->priv->updating_transaction_status) {
		GError *error = NULL;
		if (! browser_connection_begin (bwin->priv->bcnc, &error)) {
			browser_show_error (GTK_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) bwin)),
					    _("Error starting transaction: %s"),
					    error && error->message ? error->message : _("No detail"));
			g_clear_error (&error);
		}
	}
}

static void
transaction_commit_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	if (!bwin->priv->updating_transaction_status) {
		GError *error = NULL;
		if (! browser_connection_commit (bwin->priv->bcnc, &error)) {
			browser_show_error (GTK_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) bwin)),
					    _("Error committing transaction: %s"),
					    error && error->message ? error->message : _("No detail"));
			g_clear_error (&error);
		}
	}
}

static void
transaction_rollback_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	if (!bwin->priv->updating_transaction_status) {
		GError *error = NULL;
		if (! browser_connection_rollback (bwin->priv->bcnc, &error)) {
			browser_show_error (GTK_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) bwin)),
					    _("Error rolling back transaction: %s"),
					    error && error->message ? error->message : _("No detail"));
			g_clear_error (&error);
		}
	}
}


static void
window_close_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	delete_event (NULL, NULL, bwin);
}

static gboolean
toolbar_hide_timeout_cb (BrowserWindow *bwin)
{
	if (!bwin->priv->cursor_in_toolbar) {
		gtk_widget_hide (bwin->priv->toolbar);
		gtk_widget_hide (bwin->priv->menubar);
		bwin->priv->toolbar_shown = FALSE;
		
		/* remove timer */
		bwin->priv->fullscreen_timer_id = 0;
		return FALSE;
	}
	else
		/* keep timer */
		return TRUE;
}

#define BWIN_WINDOW_FULLSCREEN_POPUP_THRESHOLD 15
#define BWIN_WINDOW_FULLSCREEN_POPUP_TIMER 5

static gboolean
fullscreen_motion_notify_cb (GtkWidget *widget, GdkEventMotion *event, G_GNUC_UNUSED gpointer user_data)
{
	BrowserWindow *bwin = BROWSER_WINDOW (widget);
	if (gtk_widget_get_window (widget) != event->window)
		return FALSE;

	if (event->y < BWIN_WINDOW_FULLSCREEN_POPUP_THRESHOLD) {
		gtk_widget_show (bwin->priv->toolbar);
		gtk_widget_show (bwin->priv->menubar);
		bwin->priv->toolbar_shown = TRUE;
	}

	if (bwin->priv->toolbar_shown) {
		/* reset toolbar hiding timer */
		if (bwin->priv->fullscreen_timer_id)
			g_source_remove (bwin->priv->fullscreen_timer_id);
		bwin->priv->fullscreen_timer_id = g_timeout_add_seconds (BWIN_WINDOW_FULLSCREEN_POPUP_TIMER,
									 (GSourceFunc) toolbar_hide_timeout_cb,
									 bwin);
	}
	return FALSE;
}

gboolean
toolbar_enter_notify_cb (G_GNUC_UNUSED GtkWidget *widget, G_GNUC_UNUSED GdkEventCrossing *event,
			 BrowserWindow *bwin)
{
	bwin->priv->cursor_in_toolbar = TRUE;
	return FALSE;
}

gboolean
toolbar_leave_notify_cb (G_GNUC_UNUSED GtkWidget *widget, G_GNUC_UNUSED GdkEventCrossing *event, BrowserWindow *bwin)
{
	bwin->priv->cursor_in_toolbar = FALSE;
	return FALSE;
}

static void
window_fullscreen_cb (GtkToggleAction *action, BrowserWindow *bwin)
{
	if (gtk_toggle_action_get_active (action)) {
		gtk_window_fullscreen (GTK_WINDOW (bwin));
		browser_window_show_notice_printf (bwin, GTK_MESSAGE_INFO,
						   "fullscreen-esc",
						   "%s", _("Hit the Escape key to leave the fullscreen mode"));
		gtk_widget_hide (bwin->priv->toolbar);
		gtk_widget_hide (bwin->priv->menubar);
		bwin->priv->toolbar_shown = FALSE;
		bwin->priv->fullscreen_motion_sig_id = g_signal_connect (bwin, "motion-notify-event",
									 G_CALLBACK (fullscreen_motion_notify_cb),
									 NULL);
		g_signal_connect (bwin->priv->toolbar, "enter-notify-event",
				  G_CALLBACK (toolbar_enter_notify_cb), bwin);
		g_signal_connect (bwin->priv->toolbar, "leave-notify-event",
				  G_CALLBACK (toolbar_leave_notify_cb), bwin);
		g_signal_connect (bwin->priv->menubar, "enter-notify-event",
				  G_CALLBACK (toolbar_enter_notify_cb), bwin);
		g_signal_connect (bwin->priv->menubar, "leave-notify-event",
				  G_CALLBACK (toolbar_leave_notify_cb), bwin);
	}
	else {
		gtk_window_unfullscreen (GTK_WINDOW (bwin));
		g_signal_handler_disconnect (bwin, bwin->priv->fullscreen_motion_sig_id);
		bwin->priv->fullscreen_motion_sig_id = 0;
		g_signal_handlers_disconnect_by_func (bwin->priv->toolbar,
						      G_CALLBACK (toolbar_enter_notify_cb), bwin);
		g_signal_handlers_disconnect_by_func (bwin->priv->toolbar,
						      G_CALLBACK (toolbar_leave_notify_cb), bwin);
		g_signal_handlers_disconnect_by_func (bwin->priv->menubar,
						      G_CALLBACK (toolbar_enter_notify_cb), bwin);
		g_signal_handlers_disconnect_by_func (bwin->priv->menubar,
						      G_CALLBACK (toolbar_leave_notify_cb), bwin);

		gtk_widget_show (bwin->priv->toolbar);
		gtk_widget_show (bwin->priv->menubar);
		bwin->priv->toolbar_shown = TRUE;

		if (bwin->priv->fullscreen_timer_id) {
			g_source_remove (bwin->priv->fullscreen_timer_id);
			bwin->priv->fullscreen_timer_id = 0;
		}
	}
}

static gboolean
key_press_event (GtkWidget *widget, GdkEventKey *event)
{
	if ((event->keyval == GDK_KEY_Escape) &&
	    browser_window_is_fullscreen (BROWSER_WINDOW (widget))) {
		browser_window_set_fullscreen (BROWSER_WINDOW (widget), FALSE);
		return TRUE;
	}

	/* parent class */
	return GTK_WIDGET_CLASS (parent_class)->key_press_event (widget, event);
}

static gboolean
window_state_event (GtkWidget *widget, GdkEventWindowState *event)
{
	BrowserWindow *bwin = BROWSER_WINDOW (widget);
	gboolean (* window_state_event) (GtkWidget *, GdkEventWindowState *);
	window_state_event = GTK_WIDGET_CLASS (parent_class)->window_state_event;

	/* calling parent's method */
	if (window_state_event)
                window_state_event (widget, event);

	if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) {
                gboolean fullscreen;
		GtkWidget *wid;

		wid = gtk_ui_manager_get_widget (bwin->priv->ui_manager, "/ToolBar");

                fullscreen = event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN;
		bwin->priv->fullscreen = fullscreen;
		if (fullscreen) {
			gtk_toolbar_set_style (GTK_TOOLBAR (wid), GTK_TOOLBAR_ICONS);
			browser_spinner_set_size (BROWSER_SPINNER (bwin->priv->spinner),
						  GTK_ICON_SIZE_LARGE_TOOLBAR);
		}
		else {
			gtk_toolbar_set_style (GTK_TOOLBAR (wid), bwin->priv->toolbar_style);
			browser_spinner_set_size (BROWSER_SPINNER (bwin->priv->spinner),
						  GTK_ICON_SIZE_SMALL_TOOLBAR);
		}

		wid = gtk_ui_manager_get_widget (bwin->priv->ui_manager, "/MenuBar");
		if (fullscreen)
			gtk_widget_hide (wid);
		else
			gtk_widget_show (wid);
		
		g_signal_emit (G_OBJECT (bwin), browser_window_signals[FULLSCREEN_CHANGED], 0, fullscreen);
        }
	return FALSE;
}

static void
window_new_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	BrowserWindow *nbwin;
	BrowserConnection *bcnc;
	bcnc = browser_window_get_connection (bwin);
	nbwin = browser_window_new (bcnc, NULL);
	
	browser_core_take_window (nbwin);
}

static void
window_new_with_cnc_cb (GtkAction *action, G_GNUC_UNUSED BrowserWindow *bwin)
{
	BrowserWindow *nbwin;
	BrowserConnection *bcnc;
	bcnc = g_object_get_data (G_OBJECT (action), "bcnc");
	g_return_if_fail (BROWSER_IS_CONNECTION (bcnc));
	
	nbwin = browser_window_new (bcnc, NULL);
	
	browser_core_take_window (nbwin);
}

static void
connection_open_cb (G_GNUC_UNUSED GtkAction *action, G_GNUC_UNUSED BrowserWindow *bwin)
{
	browser_connection_open (NULL);
}

static void
connection_properties_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	if (BROWSER_IS_VIRTUAL_CONNECTION (bwin->priv->bcnc)) {
		GtkWidget *win;
		BrowserVirtualConnectionSpecs *specs;
		gint res;

		g_object_get (G_OBJECT (bwin->priv->bcnc), "specs", &specs, NULL);
		win = connection_binding_properties_new_edit (specs); /* @specs is not modified */
		gtk_window_set_transient_for (GTK_WINDOW (win), GTK_WINDOW (bwin));
		gtk_widget_show (win);
		
		res = gtk_dialog_run (GTK_DIALOG (win));
		gtk_widget_hide (win);
		if (res == GTK_RESPONSE_OK) {
			GError *error = NULL;
			if (!browser_virtual_connection_modify_specs (BROWSER_VIRTUAL_CONNECTION (bwin->priv->bcnc),
								      connection_binding_properties_get_specs (CONNECTION_BINDING_PROPERTIES (win)),
								      &error)) {
				browser_show_error ((GtkWindow*) bwin,
						    _("Error updating bound connection: %s"),
						    error && error->message ? error->message : _("No detail"));
				g_clear_error (&error);
			}

			/* update meta store */
			browser_connection_update_meta_data (bwin->priv->bcnc);
		}
		gtk_widget_destroy (win);
	}
	else {
		browser_connections_list_show (bwin->priv->bcnc);
	}
}

static void
connection_bind_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	GtkWidget *win;
	gint res;

	win = connection_binding_properties_new_create (bwin->priv->bcnc);
	gtk_window_set_transient_for (GTK_WINDOW (win), GTK_WINDOW (bwin));
	gtk_widget_show (win);

	res = gtk_dialog_run (GTK_DIALOG (win));
	gtk_widget_hide (win);
	if (res == GTK_RESPONSE_OK) {
		BrowserConnection *bcnc;
		GError *error = NULL;
		bcnc = browser_virtual_connection_new (connection_binding_properties_get_specs
						       (CONNECTION_BINDING_PROPERTIES (win)), &error);
		if (bcnc) {
			BrowserWindow *bwin;
			bwin = browser_window_new (bcnc, NULL);
			
			browser_core_take_window (bwin);
			browser_core_take_connection (bcnc);
		}
		else {
			browser_show_error ((GtkWindow*) bwin,
					    _("Could not open binding connection: %s"),
					    error && error->message ? error->message : _("No detail"));
			g_clear_error (&error);
		}
	}
	gtk_widget_destroy (win);
}

static void
connection_list_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	browser_connections_list_show (bwin->priv->bcnc);
}

static void
connection_meta_update_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	browser_connection_update_meta_data (bwin->priv->bcnc);
}

static void
about_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	GdkPixbuf *icon;
        GtkWidget *dialog;
        const gchar *authors[] = {
                "Vivien Malerba <malerba@gnome-db.org> (current maintainer)",
                NULL
        };
        const gchar *documenters[] = {
                NULL
        };
        const gchar *translator_credits = "";

        gchar *path;
        path = gda_gbr_get_file_path (GDA_DATA_DIR, LIBGDA_ABI_NAME, "pixmaps", "gda-browser.png", NULL);
        icon = gdk_pixbuf_new_from_file (path, NULL);
        g_free (path);

        dialog = gtk_about_dialog_new ();
	gtk_about_dialog_set_program_name (GTK_ABOUT_DIALOG (dialog), _("Database browser"));
        gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (dialog), PACKAGE_VERSION);
        gtk_about_dialog_set_copyright (GTK_ABOUT_DIALOG (dialog), "(C) 2009 - 2011 GNOME Foundation");
	gtk_about_dialog_set_comments (GTK_ABOUT_DIALOG (dialog), _("Database access services for the GNOME Desktop"));
        gtk_about_dialog_set_license (GTK_ABOUT_DIALOG (dialog), "GNU General Public License");
        gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (dialog), "http://www.gnome-db.org");
        gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (dialog), authors);
        gtk_about_dialog_set_documenters (GTK_ABOUT_DIALOG (dialog), documenters);
        gtk_about_dialog_set_translator_credits (GTK_ABOUT_DIALOG (dialog), translator_credits);
        gtk_about_dialog_set_logo (GTK_ABOUT_DIALOG (dialog), icon);
        g_signal_connect (G_OBJECT (dialog), "response",
                          G_CALLBACK (gtk_widget_destroy),
                          dialog);
        gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (bwin));
        gtk_widget_show (dialog);
}

#ifdef HAVE_GDU
void
manual_cb (G_GNUC_UNUSED GtkAction *action, BrowserWindow *bwin)
{
	browser_show_help (GTK_WINDOW (bwin), NULL);
}
#endif


/**
 * browser_window_get_connection
 * @bwin: a #BrowserWindow
 * 
 * Returns: the #BrowserConnection used in @bwin
 */
BrowserConnection *
browser_window_get_connection (BrowserWindow *bwin)
{
	g_return_val_if_fail (BROWSER_IS_WINDOW (bwin), NULL);
	return bwin->priv->bcnc;
}


/*
 * perspective_data_new
 * @bwin: a #BrowserWindow in which the perspective will be
 * @factory: (allow-none): a #BrowserPerspectiveFactory, or %NULL
 *
 * Creates a new #PerspectiveData structure, it increases @bcnc's reference count.
 *
 * Returns: a new #PerspectiveData
 */
static PerspectiveData *
perspective_data_new (BrowserWindow *bwin, BrowserPerspectiveFactory *factory)
{
        PerspectiveData *pers;

        pers = g_new0 (PerspectiveData, 1);
        pers->bwin = NULL;
        pers->factory = factory;
        if (!pers->factory)
                pers->factory = browser_core_get_default_factory ();
        pers->page_number = -1;
        g_assert (pers->factory);
	pers->perspective_widget = g_object_ref (pers->factory->perspective_create (bwin));

        return pers;
}

/*
 * perspective_data_free
 */
static void
perspective_data_free (PerspectiveData *pers)
{
        if (pers->perspective_widget)
                g_object_unref (pers->perspective_widget);
	if (pers->customized_actions)
		g_object_unref (pers->customized_actions);
	g_free (pers->customized_ui);
        g_free (pers);
}

typedef struct {
	BrowserWindow *bwin;
	guint cid; /* statusbar context id */
	guint msgid; /* statusbar message id */
} StatusData;

static gboolean
status_auto_pop_timeout (StatusData *sd)
{
	if (sd->bwin) {
		g_object_remove_weak_pointer (G_OBJECT (sd->bwin), (gpointer*) &(sd->bwin));
		gtk_statusbar_remove (GTK_STATUSBAR (sd->bwin->priv->statusbar), sd->cid, sd->msgid);
	}
	g_free (sd);

	return FALSE; /* remove source */
}

/**
 * browser_window_push_status
 * @bwin: a #BrowserWindow
 * @context: textual description of what context the new message is being used in
 * @text: textual message
 * @auto_clear: %TRUE if the message has to disappear after a while
 *
 * Pushes a new message onto @bwin's statusbar's stack.
 *
 * Returns: the message ID, see gtk_statusbar_push(), or %0 if @auto_clear is %TRUE
 */
guint
browser_window_push_status (BrowserWindow *bwin, const gchar *context, const gchar *text, gboolean auto_clear)
{
	guint cid;
	guint retval;

	g_return_val_if_fail (BROWSER_IS_WINDOW (bwin), 0);
	g_return_val_if_fail (context, 0);
	g_return_val_if_fail (text, 0);
	
	cid = gtk_statusbar_get_context_id (GTK_STATUSBAR (bwin->priv->statusbar), context);
	retval = gtk_statusbar_push (GTK_STATUSBAR (bwin->priv->statusbar), cid, text);
	if (auto_clear) {
		StatusData *sd;
		sd = g_new0 (StatusData, 1);
		sd->bwin = bwin;
		g_object_add_weak_pointer (G_OBJECT (sd->bwin), (gpointer*) &(sd->bwin));
		sd->cid = cid;
		sd->msgid = retval;
		g_timeout_add_seconds (5, (GSourceFunc) status_auto_pop_timeout, sd);
		return 0;
	}
	else
		return retval;
}

/**
 * browser_window_pop_status
 * @bwin: a #BrowserWindow
 * @context: textual description of what context the message is being used in
 *
 * Removes the first message in the @bwin's statusbar's stack with the given context.
 */
void
browser_window_pop_status (BrowserWindow *bwin, const gchar *context)
{
	guint cid;

	g_return_if_fail (BROWSER_IS_WINDOW (bwin));
	g_return_if_fail (context);

	cid = gtk_statusbar_get_context_id (GTK_STATUSBAR (bwin->priv->statusbar), context);
	gtk_statusbar_pop (GTK_STATUSBAR (bwin->priv->statusbar), cid);
}

/**
 * browser_window_show_notice_printf
 * @bwin: a #BrowserWindow
 * @context: textual description of what context the message is being used in
 * @format: the text to display
 *
 * Make @bwin display a notice
 */
void
browser_window_show_notice_printf (BrowserWindow *bwin, GtkMessageType type, const gchar *context,
				   const gchar *format, ...)
{
	va_list args;
        gchar sz[2048];

	g_return_if_fail (BROWSER_IS_WINDOW (bwin));

        /* build the message string */
        va_start (args, format);
        vsnprintf (sz, sizeof (sz), format, args);
        va_end (args);
	browser_window_show_notice (bwin, type, context, sz);
}


static void
info_bar_response_cb (GtkInfoBar *ibar, G_GNUC_UNUSED gint response, BrowserWindow *bwin)
{
	bwin->priv->notif_widgets = g_slist_remove (bwin->priv->notif_widgets, ibar);	
	gtk_widget_destroy ((GtkWidget*) ibar);
}

/* hash table to remain which context notices have to be hidden: key=context, value=GINT_TO_POINTER (1) */
static GHashTable *hidden_contexts = NULL;
static void
hidden_contexts_foreach_save (const gchar *context, G_GNUC_UNUSED gint value, xmlNodePtr root)
{
	xmlNewChild (root, NULL, BAD_CAST "hide-notice", BAD_CAST context);
}

static void
hide_notice_toggled_cb (GtkToggleButton *toggle, gchar *context)
{
	g_assert (hidden_contexts);
	if (gtk_toggle_button_get_active (toggle))
		g_hash_table_insert (hidden_contexts, g_strdup (context), GINT_TO_POINTER (TRUE));
	else
		g_hash_table_remove (hidden_contexts, context);

	/* save config */
	xmlDocPtr doc;
	xmlNodePtr root;
	xmlChar *xml_contents;
	gint size;
	doc = xmlNewDoc (BAD_CAST "1.0");
	root = xmlNewNode (NULL, BAD_CAST "gda-browser-preferences");
	xmlDocSetRootElement (doc, root);
	g_hash_table_foreach (hidden_contexts, (GHFunc) hidden_contexts_foreach_save, root);
	xmlDocDumpFormatMemory (doc, &xml_contents, &size, 1);
	xmlFreeDoc (doc);

	/* save to disk */
	gchar *confdir, *conffile = NULL;
	GError *lerror = NULL;
	confdir = g_build_path (G_DIR_SEPARATOR_S, g_get_user_config_dir(), "gda-browser", NULL);
	if (!g_file_test (confdir, G_FILE_TEST_EXISTS)) {
		if (g_mkdir_with_parents (confdir, 0700)) {
			g_warning ("Can't create configuration directory '%s' to save preferences.",
				   confdir);
			goto out;
		}
	}
	conffile = g_build_filename (confdir, "preferences.xml", NULL);
	if (! g_file_set_contents (conffile, (gchar *) xml_contents, size, &lerror)) {
		g_warning ("Can't save preferences file '%s': %s", conffile,
			   lerror && lerror->message ? lerror->message : _("No detail"));
		g_clear_error (&lerror);
	}

 out:
	xmlFree (xml_contents);
	g_free (confdir);
	g_free (conffile);
}

static void
load_preferences (void)
{
	/* load preferences */
	xmlDocPtr doc;
	xmlNodePtr root, node;
	gchar *conffile;
	conffile = g_build_path (G_DIR_SEPARATOR_S, g_get_user_config_dir(),
				 "gda-browser", "preferences.xml", NULL);
	if (!g_file_test (conffile, G_FILE_TEST_EXISTS)) {
		g_free (conffile);
		return;
	}

	doc = xmlParseFile (conffile);
	if (!doc) {
		g_warning ("Error loading preferences from file '%s'", conffile);
		g_free (conffile);
		return;
	}
	g_free (conffile);

	root = xmlDocGetRootElement (doc);
	for (node = root->children; node; node = node->next) {
		xmlChar *contents;
		if (strcmp ((gchar *) node->name, "hide-notice"))
			continue;
		contents = xmlNodeGetContent (node);
		if (contents) {
			g_hash_table_insert (hidden_contexts, g_strdup ((gchar*) contents),
					     GINT_TO_POINTER (TRUE));
			xmlFree (contents);
		}
	}
	xmlFreeDoc (doc);
}


/**
 * browser_window_show_notice
 * @bwin: a #BrowserWindow
 * @context: textual description of what context the message is being used in
 * @text: the information's text
 *
 * Makes @bwin display a notice
 */
void
browser_window_show_notice (BrowserWindow *bwin, GtkMessageType type, const gchar *context, const gchar *text)
{
	g_return_if_fail (BROWSER_IS_WINDOW (bwin));
	gboolean hide = FALSE;

	if ((type != GTK_MESSAGE_ERROR) && context) {
		if (!hidden_contexts) {
			hidden_contexts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
			load_preferences ();
		}
		hide = GPOINTER_TO_INT (g_hash_table_lookup (hidden_contexts, context));
	}

	if (hide) {
		gchar *ptr, *tmp;
		tmp = g_strdup (text);
		for (ptr = tmp; *ptr && (*ptr != '\n'); ptr++);
		if (*ptr) {
			*ptr = '.'; ptr++;
			*ptr = '.'; ptr++;
			*ptr = '.'; ptr++;
			*ptr = 0;
		}
		browser_window_push_status (bwin, "SupportNotice", tmp, TRUE);
		g_free (tmp);
	}
	else {
		if (bwin->priv->notif_widgets) {
			GSList *list;
			for (list = bwin->priv->notif_widgets; list; list = list->next) {
				const gchar *c1, *t1;
				c1 = g_object_get_data (G_OBJECT (list->data), "context");
				t1 = g_object_get_data (G_OBJECT (list->data), "text");
				if (((c1 && context && !strcmp (c1, context)) || (!c1 && !context)) &&
				    ((t1 && text && !strcmp (t1, text)) || (!t1 && !text)))
					break;
			}
			if (list) {
				GtkWidget *wid;
				wid = GTK_WIDGET (list->data);
				g_object_ref ((GObject*) wid);
				gtk_container_remove (GTK_CONTAINER (bwin->priv->notif_box), wid);
				gtk_box_pack_end (GTK_BOX (bwin->priv->notif_box), wid, TRUE, TRUE, 0);
				g_object_unref ((GObject*) wid);
				return;
			}
		}

		GtkWidget *cb = NULL;
		if (context && (type == GTK_MESSAGE_INFO)) {
			cb = gtk_check_button_new_with_label (_("Don't show this message again"));
			g_signal_connect_data (cb, "toggled",
					       G_CALLBACK (hide_notice_toggled_cb), g_strdup (context),
					       (GClosureNotify) g_free, 0);
		}

		/* use a GtkInfoBar */
		GtkWidget *ibar, *content_area, *label;
		
		ibar = gtk_info_bar_new_with_buttons (GTK_STOCK_CLOSE, 1, NULL);
		if (context)
			g_object_set_data_full (G_OBJECT (ibar), "context", g_strdup (context), g_free);
		if (text)
			g_object_set_data_full (G_OBJECT (ibar), "text", g_strdup (text), g_free);
		gtk_info_bar_set_message_type (GTK_INFO_BAR (ibar), type);
		label = gtk_label_new ("");
		gtk_label_set_markup (GTK_LABEL (label), text);
		gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
		gtk_misc_set_alignment (GTK_MISC (label), 0., -1);
		content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (ibar));
		if (cb) {
			GtkWidget *box;
			box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
			gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
			gtk_box_pack_start (GTK_BOX (box), cb, FALSE, FALSE, 0);
			gtk_container_add (GTK_CONTAINER (content_area), box);
			gtk_widget_show_all (box);
		}
		else {
			gtk_container_add (GTK_CONTAINER (content_area), label);
			gtk_widget_show (label);
		}
		g_signal_connect (ibar, "response",
				  G_CALLBACK (info_bar_response_cb), bwin);
		gtk_box_pack_end (GTK_BOX (bwin->priv->notif_box), ibar, TRUE, TRUE, 0);
		bwin->priv->notif_widgets = g_slist_append (bwin->priv->notif_widgets, ibar);
		if (g_slist_length (bwin->priv->notif_widgets) > 2) {
			gtk_widget_destroy (GTK_WIDGET (bwin->priv->notif_widgets->data));
			bwin->priv->notif_widgets = g_slist_delete_link (bwin->priv->notif_widgets,
									 bwin->priv->notif_widgets);
		}
		gtk_widget_show (ibar);
	}
}


/**
 * browser_window_customize_perspective_ui
 * @bwin: a #BrowserWindow
 * @bpers: the #BrowserPerspective concerned
 * @actions_group: (allow-none): a #GtkActionGroup object, or %NULL
 * @ui_info: (allow-none): a merge UI string, or %NULL. See gtk_ui_manager_add_ui_from_string()
 *
 * Customizes a UI specific to the @bpers perspective. Any
 * previous customization is removed, replaced by the new requested one.
 *
 * If @actions_group is %NULL then any it simply removes the customization.
 */
void
browser_window_customize_perspective_ui (BrowserWindow *bwin, BrowserPerspective *bpers,
					 GtkActionGroup *actions_group,
					 const gchar *ui_info)
{
	PerspectiveData *pdata = NULL;
	GSList *list;

	g_return_if_fail (BROWSER_IS_WINDOW (bwin));
	g_return_if_fail (IS_BROWSER_PERSPECTIVE (bpers));

	for (list = bwin->priv->perspectives; list; list = list->next) {
		if (PERSPECTIVE_DATA (list->data)->perspective_widget == bpers) {
			pdata = PERSPECTIVE_DATA (list->data);
			break;
		}
	}
	if (! pdata)
		return;

	/* cleanups */
	if (pdata->customized_merge_id) {
		gtk_ui_manager_remove_ui (bwin->priv->ui_manager, pdata->customized_merge_id);
		pdata->customized_merge_id = 0;
	}
	if (pdata->customized_actions) {
		gtk_ui_manager_remove_action_group (bwin->priv->ui_manager, pdata->customized_actions);
		g_object_unref (pdata->customized_actions);
		pdata->customized_actions = NULL;
	}
	g_free (pdata->customized_ui);
	pdata->customized_ui = NULL;
	gtk_ui_manager_ensure_update (bwin->priv->ui_manager);

	if (actions_group) {
		g_return_if_fail (GTK_IS_ACTION_GROUP (actions_group));
		gtk_action_group_set_translation_domain (actions_group, GETTEXT_PACKAGE);
		gtk_ui_manager_insert_action_group (bwin->priv->ui_manager, actions_group, 0);
		pdata->customized_actions = g_object_ref (actions_group);
	}
	if (ui_info) {
		pdata->customized_ui = g_strdup (ui_info);
		pdata->customized_merge_id = gtk_ui_manager_add_ui_from_string (bwin->priv->ui_manager,
										pdata->customized_ui,
										-1, NULL);
	}
}

/**
 * browser_window_change_perspective
 * @bwin: a #BrowserWindow
 * @perspective: the name of the perspective to change to
 *
 * Make @bwin switch to the perspective named @perspective
 *
 * Returns: a pointer to the #BrowserPerspective, or %NULL if not found
 */
BrowserPerspective *
browser_window_change_perspective (BrowserWindow *bwin, const gchar *perspective)
{
	BrowserPerspectiveFactory *bpf;
	BrowserPerspective *bpers = NULL;
	PerspectiveData *current_pdata;

	g_return_val_if_fail (BROWSER_IS_WINDOW (bwin), NULL);
	g_return_val_if_fail (perspective, NULL);

	current_pdata = bwin->priv->current_perspective;

	bpf = browser_core_get_factory (perspective);
	if (!bpf)
		return NULL;
	GList *actions, *list;
	actions = gtk_action_group_list_actions (bwin->priv->perspectives_actions);
	for (list = actions; list; list = list->next) {
		GtkAction *action = (GtkAction *) list->data;
		BrowserPerspectiveFactory *pf;
		pf = BROWSER_PERSPECTIVE_FACTORY (g_object_get_data (G_OBJECT (action), "pers"));
		if (pf == bpf) {
			gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
			PerspectiveData *pdata = bwin->priv->current_perspective;
			if (pdata && ! g_ascii_strcasecmp (pdata->factory->perspective_name, perspective))
				bpers = pdata->perspective_widget;
			break;
		}
	}
	g_list_free (actions);

	gchar *tmp;
	tmp = g_markup_printf_escaped (_("The current perspective has changed to the '%s' perspective, you "
					 "can switch back to previous perspective through the "
					 "'Perspective/%s' menu, or using the '%s' shortcut"),
				       bwin->priv->current_perspective->factory->perspective_name,
				       current_pdata->factory->perspective_name,
				       current_pdata->factory->menu_shortcut);

			
	browser_window_show_notice (bwin, GTK_MESSAGE_INFO, "Perspective change", tmp);
	g_free (tmp);

	return bpers;
}

/**
 * browser_window_is_fullscreen
 * @bwin: a #BrowserWindow
 *
 * Returns: %TRUE if @bwin is fullscreen
 */
gboolean
browser_window_is_fullscreen (BrowserWindow *bwin)
{
	g_return_val_if_fail (BROWSER_IS_WINDOW (bwin), FALSE);
	return bwin->priv->fullscreen;
}

/**
 * browser_window_set_fullscreen
 * @bwin: a #BrowserWindow
 * @fullscreen:
 *
 * Requires @bwin to be fullscreen if @fullscreen is %TRUE
 */
void
browser_window_set_fullscreen (BrowserWindow *bwin, gboolean fullscreen)
{
	GtkAction *action;
	g_return_if_fail (BROWSER_IS_WINDOW (bwin));
	
	action = gtk_action_group_get_action (bwin->priv->agroup, "WindowFullScreen");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), fullscreen);
}
