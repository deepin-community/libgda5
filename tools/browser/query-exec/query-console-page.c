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
#include "query-console-page.h"
#include "../dnd.h"
#include "../support.h"
#include "../gdaui-bar.h"
#include "query-exec-perspective.h"
#include "../browser-window.h"
#include "../browser-page.h"
#include "../browser-stock-icons.h"
#include "query-editor.h"
#include "query-result.h"
#include <libgda-ui/internal/popup-container.h>
#include <libgda/sql-parser/gda-sql-parser.h>
#include <libgda-ui/libgda-ui.h>
#include <libgda/gda-debug-macros.h>

/*
 * Statement execution structures
 */
typedef struct {
	GTimeVal start_time;
	GdaBatch *batch; /* ref held here */
	QueryEditorHistoryBatch *hist_batch; /* ref held here */
	GSList *statements; /* list of ExecutionStatament */
} ExecutionBatch;
static void execution_batch_free (ExecutionBatch *ebatch);

typedef struct {
	GdaStatement *stmt /* no ref held here */;
	gboolean within_transaction;
	GError *exec_error;
	GObject *result;
	guint exec_id; /* 0 when execution not requested */
} ExecutionStatement;
static void execution_statement_free (ExecutionStatement *estmt);

/*
 * Frees an @ExecutionBatch
 *
 * If necessary, may set up and idle handler to perform some
 * additionnal jobs
 */
static void
execution_batch_free (ExecutionBatch *ebatch)
{
	GSList *list;
	for (list = ebatch->statements; list; list = list->next)
		execution_statement_free ((ExecutionStatement*) list->data);
	g_slist_free (ebatch->statements);

	g_object_unref (ebatch->batch);
	if (ebatch->hist_batch)
		query_editor_history_batch_unref (ebatch->hist_batch);

	g_free (ebatch);
}

static void
execution_statement_free (ExecutionStatement *estmt)
{
	if (estmt->exec_id > 0) {
		/* job started! */
		TO_IMPLEMENT;
	}
	else {
		if (estmt->exec_error)
			g_error_free (estmt->exec_error);
		if (estmt->result)
			g_object_unref (estmt->result);
		g_free (estmt);
	}
}

struct _QueryConsolePagePrivate {
	BrowserConnection *bcnc;
	GdaSqlParser *parser;

	GtkActionGroup *agroup;

	GdauiBar *header;
	GtkWidget *vpaned; /* top=>query editor, bottom=>results */

	QueryEditor *editor;
	GtkWidget   *exec_button;
	GtkWidget   *indent_button;
	guint params_compute_id; /* timout ID to compute params */
	GdaSet *params; /* execution params */
	GtkWidget *params_popup; /* popup shown when invalid params are required */

	GtkToggleButton *params_toggle;
	GtkWidget *params_top;
	GtkWidget *params_form_box;
	GtkWidget *params_form;
	
	QueryEditor *history;
	GtkWidget *history_del_button;
	GtkWidget *history_copy_button;

	GtkWidget *query_result;

	ExecutionBatch *current_exec;

	gint fav_id;
	GtkWidget *favorites_menu;
};

static void query_console_page_class_init (QueryConsolePageClass *klass);
static void query_console_page_init       (QueryConsolePage *tconsole, QueryConsolePageClass *klass);
static void query_console_page_dispose   (GObject *object);
static void query_console_page_show_all (GtkWidget *widget);
static void query_console_page_grab_focus (GtkWidget *widget);

/* BrowserPage interface */
static void                 query_console_page_page_init (BrowserPageIface *iface);
static GtkActionGroup      *query_console_page_page_get_actions_group (BrowserPage *page);
static const gchar         *query_console_page_page_get_actions_ui (BrowserPage *page);
static GtkWidget           *query_console_page_page_get_tab_label (BrowserPage *page, GtkWidget **out_close_button);

static GObjectClass *parent_class = NULL;

/*
 * QueryConsolePage class implementation
 */

static void
query_console_page_class_init (QueryConsolePageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = query_console_page_dispose;
	GTK_WIDGET_CLASS (klass)->show_all = query_console_page_show_all;
	GTK_WIDGET_CLASS (klass)->grab_focus = query_console_page_grab_focus;
}

static void
query_console_page_show_all (GtkWidget *widget)
{
	QueryConsolePage *tconsole = (QueryConsolePage *) widget;
	GTK_WIDGET_CLASS (parent_class)->show_all (widget);

	if (gtk_toggle_button_get_active (tconsole->priv->params_toggle))
		gtk_widget_show (tconsole->priv->params_top);
	else
		gtk_widget_hide (tconsole->priv->params_top);
}

static void
query_console_page_page_init (BrowserPageIface *iface)
{
	iface->i_get_actions_group = query_console_page_page_get_actions_group;
	iface->i_get_actions_ui = query_console_page_page_get_actions_ui;
	iface->i_get_tab_label = query_console_page_page_get_tab_label;
}

static void
query_console_page_init (QueryConsolePage *tconsole, G_GNUC_UNUSED QueryConsolePageClass *klass)
{
	tconsole->priv = g_new0 (QueryConsolePagePrivate, 1);
	tconsole->priv->parser = NULL;
	tconsole->priv->params_compute_id = 0;
	tconsole->priv->params = NULL;
	tconsole->priv->params_popup = NULL;
	tconsole->priv->agroup = NULL;
	tconsole->priv->fav_id = -1;

	gtk_orientable_set_orientation (GTK_ORIENTABLE (tconsole), GTK_ORIENTATION_VERTICAL);
}

static void connection_busy_cb (BrowserConnection *bcnc, gboolean is_busy,
				gchar *reason, QueryConsolePage *tconsole);
static void
query_console_page_dispose (GObject *object)
{
	QueryConsolePage *tconsole = (QueryConsolePage *) object;

	/* free memory */
	if (tconsole->priv) {
		if (tconsole->priv->current_exec)
			execution_batch_free (tconsole->priv->current_exec);
		if (tconsole->priv->bcnc) {
			g_signal_handlers_disconnect_by_func (tconsole->priv->bcnc,
							      G_CALLBACK (connection_busy_cb), tconsole);
			g_object_unref (tconsole->priv->bcnc);
		}
		if (tconsole->priv->parser)
			g_object_unref (tconsole->priv->parser);
		if (tconsole->priv->params)
			g_object_unref (tconsole->priv->params);
		if (tconsole->priv->params_compute_id)
			g_source_remove (tconsole->priv->params_compute_id);
		if (tconsole->priv->params_popup)
			gtk_widget_destroy (tconsole->priv->params_popup);
		if (tconsole->priv->agroup)
			g_object_unref (tconsole->priv->agroup);
		if (tconsole->priv->favorites_menu)
			gtk_widget_destroy (tconsole->priv->favorites_menu);

		g_free (tconsole->priv);
		tconsole->priv = NULL;
	}

	parent_class->dispose (object);
}

GType
query_console_page_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo console = {
			sizeof (QueryConsolePageClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) query_console_page_class_init,
			NULL,
			NULL,
			sizeof (QueryConsolePage),
			0,
			(GInstanceInitFunc) query_console_page_init,
			0
		};

		static GInterfaceInfo page_console = {
                        (GInterfaceInitFunc) query_console_page_page_init,
			NULL,
                        NULL
                };

		type = g_type_register_static (GTK_TYPE_BOX, "QueryConsolePage", &console, 0);
		g_type_add_interface_static (type, BROWSER_PAGE_TYPE, &page_console);
	}
	return type;
}

static void editor_changed_cb (QueryEditor *editor, QueryConsolePage *tconsole);
static void editor_execute_request_cb (QueryEditor *editor, QueryConsolePage *tconsole);
static void sql_clear_clicked_cb (GtkButton *button, QueryConsolePage *tconsole);
static void sql_variables_clicked_cb (GtkToggleButton *button, QueryConsolePage *tconsole);
static void sql_execute_clicked_cb (GtkButton *button, QueryConsolePage *tconsole);
static void sql_indent_clicked_cb (GtkButton *button, QueryConsolePage *tconsole);
static void sql_favorite_clicked_cb (GtkButton *button, QueryConsolePage *tconsole);

static void history_copy_clicked_cb (GtkButton *button, QueryConsolePage *tconsole);
static void history_clear_clicked_cb (GtkButton *button, QueryConsolePage *tconsole);
static void history_changed_cb (QueryEditor *history, QueryConsolePage *tconsole);

static void rerun_requested_cb (QueryResult *qres, QueryEditorHistoryBatch *batch,
				QueryEditorHistoryItem *item, QueryConsolePage *tconsole);
/**
 * query_console_page_new
 *
 * Returns: a new #GtkWidget
 */
GtkWidget *
query_console_page_new (BrowserConnection *bcnc)
{
	QueryConsolePage *tconsole;

	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);

	tconsole = QUERY_CONSOLE_PAGE (g_object_new (QUERY_CONSOLE_PAGE_TYPE, NULL));

	tconsole->priv->bcnc = g_object_ref (bcnc);
	
	/* header */
        GtkWidget *label;
	gchar *str;
	str = g_strdup_printf ("<b>%s</b>", _("Query editor"));
	label = gdaui_bar_new (str);
	g_free (str);
        gtk_box_pack_start (GTK_BOX (tconsole), label, FALSE, FALSE, 0);
        gtk_widget_show (label);
	tconsole->priv->header = GDAUI_BAR (label);

	/* main contents */
	GtkWidget *vpaned;
	vpaned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
	tconsole->priv->vpaned = NULL;
	gtk_box_pack_start (GTK_BOX (tconsole), vpaned, TRUE, TRUE, 6);	

	/* top paned for the editor */
	GtkWidget *wid, *vbox, *hbox, *bbox, *hpaned, *button;

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_paned_pack1 (GTK_PANED (vpaned), hbox, TRUE, FALSE);

	hpaned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start (GTK_BOX (hbox), hpaned, TRUE, TRUE, 0);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_paned_pack1 (GTK_PANED (hpaned), vbox, TRUE, FALSE);

	wid = gtk_label_new ("");
	str = g_strdup_printf ("<b>%s</b>", _("SQL code to execute:"));
	gtk_label_set_markup (GTK_LABEL (wid), str);
	g_free (str);
	gtk_misc_set_alignment (GTK_MISC (wid), 0., -1);
	gtk_widget_set_tooltip_markup (wid, QUERY_EDITOR_TOOLTIP);
	gtk_box_pack_start (GTK_BOX (vbox), wid, FALSE, FALSE, 0);

	wid = query_editor_new ();
	tconsole->priv->editor = QUERY_EDITOR (wid);
	gtk_box_pack_start (GTK_BOX (vbox), wid, TRUE, TRUE, 0);
	g_signal_connect (wid, "changed",
			  G_CALLBACK (editor_changed_cb), tconsole);
	g_signal_connect (wid, "execute-request",
			  G_CALLBACK (editor_execute_request_cb), tconsole);
	
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	tconsole->priv->params_top = vbox;
	gtk_paned_pack2 (GTK_PANED (hpaned), vbox, FALSE, FALSE);
	
	wid = gtk_label_new ("");
	str = g_strdup_printf ("<b>%s</b>", _("Variables' values:"));
	gtk_label_set_markup (GTK_LABEL (wid), str);
	g_free (str);
	gtk_misc_set_alignment (GTK_MISC (wid), 0., -1);
	gtk_box_pack_start (GTK_BOX (vbox), wid, FALSE, FALSE, 0);
	
	GtkWidget *sw;
	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_NONE);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	tconsole->priv->params_form_box = gtk_viewport_new (NULL, NULL);
	gtk_widget_set_name (tconsole->priv->params_form_box, "gdaui-transparent-background");
	gtk_viewport_set_shadow_type (GTK_VIEWPORT (tconsole->priv->params_form_box), GTK_SHADOW_NONE);
	gtk_container_add (GTK_CONTAINER (sw), tconsole->priv->params_form_box);
	gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
	gtk_widget_set_size_request (tconsole->priv->params_form_box, 250, -1);

	wid = gtk_label_new ("");
	gtk_label_set_markup (GTK_LABEL (wid), VARIABLES_HELP);
	gtk_misc_set_alignment (GTK_MISC (wid), -1, 0.);
	gtk_container_add (GTK_CONTAINER (tconsole->priv->params_form_box), wid);
	tconsole->priv->params_form = wid;
	
	bbox = gtk_button_box_new (GTK_ORIENTATION_VERTICAL);
	gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
	gtk_box_pack_start (GTK_BOX (hbox), bbox, FALSE, FALSE, 5);

	button = browser_make_small_button (FALSE, FALSE, _("Clear"),
					    GTK_STOCK_CLEAR, _("Clear the editor's\ncontents"));
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (sql_clear_clicked_cb), tconsole);

	button = browser_make_small_button (TRUE, FALSE, _("Variables"), NULL,
					    _("Show variables needed\nto execute SQL"));
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);
	tconsole->priv->params_toggle = GTK_TOGGLE_BUTTON (button);
	g_signal_connect (button, "toggled",
			  G_CALLBACK (sql_variables_clicked_cb), tconsole);

	button = browser_make_small_button (FALSE, FALSE, _("Execute"), GTK_STOCK_EXECUTE, _("Execute SQL in editor"));
	tconsole->priv->exec_button = button;
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (sql_execute_clicked_cb), tconsole);
	
	button = browser_make_small_button (FALSE, FALSE, _("Indent"),
					    GTK_STOCK_INDENT, _("Indent SQL in editor\n"
								"and make the code more readable\n"
								"(removes comments)"));
	tconsole->priv->indent_button = button;
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (sql_indent_clicked_cb), tconsole);

	button = browser_make_small_button (FALSE, TRUE, _("Favorite"),
					    STOCK_ADD_BOOKMARK, _("Add SQL to favorite"));
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (sql_favorite_clicked_cb), tconsole);

	/* bottom paned for the results and history */
	hpaned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_pack2 (GTK_PANED (vpaned), hpaned, TRUE, FALSE);

	/* bottom left */
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_paned_pack1 (GTK_PANED (hpaned), vbox, FALSE, FALSE);

	wid = gtk_label_new ("");
	str = g_strdup_printf ("<b>%s</b>", _("Execution history:"));
	gtk_label_set_markup (GTK_LABEL (wid), str);
	g_free (str);
	gtk_misc_set_alignment (GTK_MISC (wid), 0., -1);
	gtk_box_pack_start (GTK_BOX (vbox), wid, FALSE, FALSE, 0);

	wid = query_editor_new ();
	tconsole->priv->history = QUERY_EDITOR (wid);
	query_editor_set_mode (tconsole->priv->history, QUERY_EDITOR_HISTORY);
	gtk_widget_set_size_request (wid, 200, -1);
	gtk_box_pack_start (GTK_BOX (vbox), wid, TRUE, TRUE, 6);
	g_signal_connect (wid, "changed",
			  G_CALLBACK (history_changed_cb), tconsole);

	bbox = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start (GTK_BOX (vbox), bbox, FALSE, FALSE, 0);
	gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);

	button = browser_make_small_button (FALSE, FALSE, _("Copy"), GTK_STOCK_COPY,
					    _("Copy selected history\nto editor"));
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (history_copy_clicked_cb), tconsole);
	tconsole->priv->history_copy_button = button;
	gtk_widget_set_sensitive (button, FALSE);

	button = browser_make_small_button (FALSE, FALSE, _("Clear"),
					    GTK_STOCK_CLEAR, _("Clear history"));
	gtk_box_pack_start (GTK_BOX (bbox), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked",
			  G_CALLBACK (history_clear_clicked_cb), tconsole);
	tconsole->priv->history_del_button = button;
	gtk_widget_set_sensitive (button, FALSE);

	/* bottom right */
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_paned_pack2 (GTK_PANED (hpaned), vbox, TRUE, FALSE);
	
	wid = gtk_label_new ("");
	str = g_strdup_printf ("<b>%s</b>", _("Execution Results:"));
	gtk_label_set_markup (GTK_LABEL (wid), str);
	g_free (str);
	gtk_misc_set_alignment (GTK_MISC (wid), 0., -1);
	gtk_box_pack_start (GTK_BOX (vbox), wid, FALSE, FALSE, 0);

	wid = query_result_new (tconsole->priv->history);
	tconsole->priv->query_result = wid;
	gtk_box_pack_start (GTK_BOX (vbox), wid, TRUE, TRUE, 0);
	g_signal_connect (wid, "rerun-requested",
			  G_CALLBACK (rerun_requested_cb), tconsole);

	/* show everything */
        gtk_widget_show_all (vpaned);
	gtk_widget_hide (tconsole->priv->params_top);

	/* busy connection handling */
	gchar *reason = NULL;
	if (browser_connection_is_busy (tconsole->priv->bcnc, &reason)) {
		connection_busy_cb (tconsole->priv->bcnc, TRUE, reason, tconsole);
		g_free (reason);
	}
	g_signal_connect (tconsole->priv->bcnc, "busy",
			  G_CALLBACK (connection_busy_cb), tconsole);

	return (GtkWidget*) tconsole;
}

static void
connection_busy_cb (G_GNUC_UNUSED BrowserConnection *bcnc, gboolean is_busy, G_GNUC_UNUSED gchar *reason, QueryConsolePage *tconsole)
{
	gtk_widget_set_sensitive (tconsole->priv->exec_button, !is_busy);
	gtk_widget_set_sensitive (tconsole->priv->indent_button, !is_busy);

	if (tconsole->priv->agroup) {
		GtkAction *action;
		action = gtk_action_group_get_action (tconsole->priv->agroup, "ExecuteQuery");
		gtk_action_set_sensitive (action, !is_busy);
	}
}

static void
history_changed_cb (G_GNUC_UNUSED QueryEditor *history, QueryConsolePage *tconsole)
{
	gboolean act = FALSE;
	QueryEditor *qe;
	
	qe = tconsole->priv->history;
        if (query_editor_get_current_history_item (qe, NULL)) {
		query_result_show_history_item (QUERY_RESULT (tconsole->priv->query_result),
						query_editor_get_current_history_item (qe, NULL));
		act = TRUE;
	}
	else if (query_editor_get_current_history_batch (qe)) {
		query_result_show_history_batch (QUERY_RESULT (tconsole->priv->query_result),
						 query_editor_get_current_history_batch (qe));
		act = TRUE;
	}
	else
		query_result_show_history_batch (QUERY_RESULT (tconsole->priv->query_result), NULL);

	gtk_widget_set_sensitive (tconsole->priv->history_copy_button, act);
	gtk_widget_set_sensitive (tconsole->priv->history_del_button, ! query_editor_history_is_empty (qe));
}

static void
history_clear_clicked_cb (G_GNUC_UNUSED GtkButton *button, QueryConsolePage *tconsole)
{
	query_editor_del_all_history_items (tconsole->priv->history);
}

static void
history_copy_clicked_cb (G_GNUC_UNUSED GtkButton *button, QueryConsolePage *tconsole)
{
	QueryEditorHistoryItem *qih;
	QueryEditor *qe;
	GString *string;

	string = g_string_new ("");
	qe = tconsole->priv->history;
        qih = query_editor_get_current_history_item (qe, NULL);
        if (qih)
		g_string_append (string, qih->sql);
        else {
                QueryEditorHistoryBatch *qib;
                qib = query_editor_get_current_history_batch (qe);
                if (qib) {
			GSList *list;
			for (list =  qib->hist_items; list; list = list->next) {
				if (list != qib->hist_items)
					g_string_append (string, "\n\n");
				g_string_append (string, ((QueryEditorHistoryItem*) list->data)->sql);
			}
		}
        }

	query_editor_set_text (tconsole->priv->editor, string->str);
	tconsole->priv->fav_id = -1;
	g_string_free (string, TRUE);
}

static void
params_form_activated_cb (GdauiBasicForm *form, QueryConsolePage *tconsole)
{
	sql_execute_clicked_cb (NULL, tconsole);
}

static gboolean
compute_params (QueryConsolePage *tconsole)
{
	gchar *sql;
	GdaBatch *batch;

	if (tconsole->priv->params) {
		browser_connection_keep_variables (tconsole->priv->bcnc, tconsole->priv->params);
		g_object_unref (tconsole->priv->params);
	}
	tconsole->priv->params = NULL;

	if (tconsole->priv->params_form) {
		gtk_widget_destroy (tconsole->priv->params_form);
		tconsole->priv->params_form = NULL;		
	}

	if (!tconsole->priv->parser)
		tconsole->priv->parser = browser_connection_create_parser (tconsole->priv->bcnc);

	sql = query_editor_get_all_text (tconsole->priv->editor);
	batch = gda_sql_parser_parse_string_as_batch (tconsole->priv->parser, sql, NULL, NULL);
	g_free (sql);
	if (batch) {
		GError *error = NULL;
		gboolean show_variables = FALSE;

		if (gda_batch_get_parameters (batch, &(tconsole->priv->params), &error)) {
			if (tconsole->priv->params) {
				browser_connection_define_ui_plugins_for_batch (tconsole->priv->bcnc,
										batch,
										tconsole->priv->params);
				show_variables = TRUE;
				tconsole->priv->params_form = gdaui_basic_form_new (tconsole->priv->params);
				g_object_set ((GObject*) tconsole->priv->params_form,
					      "show-actions", TRUE, NULL);
				g_signal_connect (tconsole->priv->params_form, "activated",
						  G_CALLBACK (params_form_activated_cb), tconsole);
			}
			else {
				tconsole->priv->params_form = gtk_label_new ("");
				gtk_label_set_markup (GTK_LABEL (tconsole->priv->params_form), VARIABLES_HELP);
			}
		}
		else {
			show_variables = TRUE;
			tconsole->priv->params_form = gtk_label_new ("");
			gtk_label_set_markup (GTK_LABEL (tconsole->priv->params_form), VARIABLES_HELP);
		}
		gtk_container_add (GTK_CONTAINER (tconsole->priv->params_form_box), tconsole->priv->params_form);
		gtk_widget_show (tconsole->priv->params_form);
		g_object_unref (batch);

		browser_connection_load_variables (tconsole->priv->bcnc, tconsole->priv->params);
		if (tconsole->priv->params && show_variables &&
		    gda_set_is_valid (tconsole->priv->params, NULL))
			show_variables = FALSE;
		if (show_variables && !gtk_toggle_button_get_active (tconsole->priv->params_toggle))
			gtk_toggle_button_set_active (tconsole->priv->params_toggle, TRUE);
	}
	else {
		tconsole->priv->params_form = gtk_label_new ("");
		gtk_label_set_markup (GTK_LABEL (tconsole->priv->params_form), VARIABLES_HELP);
		gtk_container_add (GTK_CONTAINER (tconsole->priv->params_form_box), tconsole->priv->params_form);
		gtk_widget_show (tconsole->priv->params_form);
	}
	
	/* remove timeout */
	tconsole->priv->params_compute_id = 0;
	return FALSE;
}

static void
editor_changed_cb (G_GNUC_UNUSED QueryEditor *editor, QueryConsolePage *tconsole)
{
	if (tconsole->priv->params_compute_id)
		g_source_remove (tconsole->priv->params_compute_id);
	tconsole->priv->params_compute_id = g_timeout_add_seconds (1, (GSourceFunc) compute_params, tconsole);
}

static void
editor_execute_request_cb (G_GNUC_UNUSED QueryEditor *editor, QueryConsolePage *tconsole)
{
	gboolean sensitive;
	g_object_get (tconsole->priv->exec_button, "sensitive", &sensitive, NULL);
	if (sensitive)
		sql_execute_clicked_cb (NULL, tconsole);
}
	
static void
sql_variables_clicked_cb (GtkToggleButton *button, QueryConsolePage *tconsole)
{
	if (gtk_toggle_button_get_active (button))
		gtk_widget_show (tconsole->priv->params_top);
	else
		gtk_widget_hide (tconsole->priv->params_top);
}

static void
sql_clear_clicked_cb (G_GNUC_UNUSED GtkButton *button, QueryConsolePage *tconsole)
{
	query_editor_set_text (tconsole->priv->editor, NULL);
	tconsole->priv->fav_id = -1;
	gtk_widget_grab_focus (GTK_WIDGET (tconsole->priv->editor));
}

static void
sql_indent_clicked_cb (G_GNUC_UNUSED GtkButton *button, QueryConsolePage *tconsole)
{
	gchar *sql;
	GdaBatch *batch;

	if (!tconsole->priv->parser)
		tconsole->priv->parser = browser_connection_create_parser (tconsole->priv->bcnc);

	sql = query_editor_get_all_text (tconsole->priv->editor);
	batch = gda_sql_parser_parse_string_as_batch (tconsole->priv->parser, sql, NULL, NULL);
	g_free (sql);
	if (batch) {
		GString *string;
		const GSList *stmt_list, *list;
		stmt_list = gda_batch_get_statements (batch);
		string = g_string_new ("");
		for (list = stmt_list; list; list = list->next) {
			sql = browser_connection_render_pretty_sql (tconsole->priv->bcnc,
								    GDA_STATEMENT (list->data));
			if (!sql)
				sql = gda_statement_to_sql (GDA_STATEMENT (list->data), NULL, NULL);
			if (list != stmt_list)
				g_string_append (string, "\n\n");
			g_string_append_printf (string, "%s;\n", sql);
			g_free (sql);

		}
		g_object_unref (batch);

		query_editor_set_text (tconsole->priv->editor, string->str);
		g_string_free (string, TRUE);
	}
}

static void sql_favorite_new_mitem_cb (G_GNUC_UNUSED GtkMenuItem *mitem, QueryConsolePage *tconsole);
static void sql_favorite_modify_mitem_cb (G_GNUC_UNUSED GtkMenuItem *mitem, QueryConsolePage *tconsole);

static void
sql_favorite_clicked_cb (G_GNUC_UNUSED GtkButton *button, QueryConsolePage *tconsole)
{
	GtkWidget *menu, *mitem;
	ToolsFavorites *bfav;

	if (tconsole->priv->favorites_menu)
		gtk_widget_destroy (tconsole->priv->favorites_menu);

	menu = gtk_menu_new ();
	tconsole->priv->favorites_menu = menu;
	mitem = gtk_menu_item_new_with_label (_("New favorite"));
	g_signal_connect (mitem, "activate",
			  G_CALLBACK (sql_favorite_new_mitem_cb), tconsole);
	gtk_widget_show (mitem);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), mitem);

	bfav = browser_connection_get_favorites (tconsole->priv->bcnc);
	if (tconsole->priv->fav_id >= 0) {
		ToolsFavoritesAttributes fav;
		if (gda_tools_favorites_get (bfav, tconsole->priv->fav_id, &fav, NULL)) {
			gchar *str;
			str = g_strdup_printf (_("Modify favorite '%s'"), fav.name);
			mitem = gtk_menu_item_new_with_label (str);
			g_free (str);
			g_signal_connect (mitem, "activate",
					  G_CALLBACK (sql_favorite_modify_mitem_cb), tconsole);
			g_object_set_data_full (G_OBJECT (mitem), "favname",
						g_strdup (fav.name), g_free);
			g_object_set_data (G_OBJECT (mitem), "favid",
					   GINT_TO_POINTER (tconsole->priv->fav_id));
			gtk_widget_show (mitem);
			gtk_menu_shell_append (GTK_MENU_SHELL (menu), mitem);
			gda_tools_favorites_reset_attributes (&fav);
		}
	}

	GSList *allfav;
	allfav = gda_tools_favorites_list (bfav, 0, GDA_TOOLS_FAVORITES_QUERIES, ORDER_KEY_QUERIES, NULL);
	if (allfav) {
		GtkWidget *submenu;
		GSList *list;

		mitem = gtk_menu_item_new_with_label (_("Modify a favorite"));
		gtk_widget_show (mitem);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), mitem);

		submenu = gtk_menu_new ();
		gtk_menu_item_set_submenu (GTK_MENU_ITEM (mitem), submenu);
		for (list = allfav; list; list = list->next) {
			ToolsFavoritesAttributes *fav;
			fav = (ToolsFavoritesAttributes*) list->data;
			if (fav->id == tconsole->priv->fav_id)
				continue;
			gchar *str;
			str = g_strdup_printf (_("Modify favorite '%s'"), fav->name);
			mitem = gtk_menu_item_new_with_label (str);
			g_free (str);
			g_signal_connect (mitem, "activate",
					  G_CALLBACK (sql_favorite_modify_mitem_cb), tconsole);
			g_object_set_data_full (G_OBJECT (mitem), "favname",
						g_strdup (fav->name), g_free);
			g_object_set_data (G_OBJECT (mitem), "favid",
					   GINT_TO_POINTER (fav->id));
			gtk_widget_show (mitem);
			gtk_menu_shell_append (GTK_MENU_SHELL (submenu), mitem);
		}
		gda_tools_favorites_free_list (allfav);
	}

	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			NULL, NULL, 0,
			gtk_get_current_event_time ());
}

static void
fav_form_name_activated_cb (G_GNUC_UNUSED GtkWidget *form, GtkWidget *dlg)
{
	gtk_dialog_response (GTK_DIALOG (dlg), GTK_RESPONSE_ACCEPT);
}

static void
sql_favorite_new_mitem_cb (G_GNUC_UNUSED GtkMenuItem *mitem, QueryConsolePage *tconsole)
{
	ToolsFavorites *bfav;
	ToolsFavoritesAttributes fav;
	GError *error = NULL;

	GdaSet *set;
	GtkWidget *dlg, *form;
	gint response;
	const GValue *cvalue;
	set = gda_set_new_inline (1, _("Favorite's name"),
				  G_TYPE_STRING, _("Unnamed query"));
	dlg = gdaui_basic_form_new_in_dialog (set,
					      (GtkWindow*) gtk_widget_get_toplevel (GTK_WIDGET (tconsole)),
					      _("Name of the favorite to create"),
					      _("Enter the name of the favorite to create"));
	form = g_object_get_data (G_OBJECT (dlg), "form");
	g_signal_connect (form, "activated",
			  G_CALLBACK (fav_form_name_activated_cb), dlg);
	response = gtk_dialog_run (GTK_DIALOG (dlg));
	if (response == GTK_RESPONSE_REJECT) {
		g_object_unref (set);
		gtk_widget_destroy (dlg);
		return;
	}

	memset (&fav, 0, sizeof (fav));
	fav.id = -1;
	fav.type = GDA_TOOLS_FAVORITES_QUERIES;
	fav.contents = query_editor_get_all_text (tconsole->priv->editor);
	cvalue = gda_set_get_holder_value (set, _("Favorite's name"));
	fav.name = (gchar*) g_value_get_string (cvalue);

	bfav = browser_connection_get_favorites (tconsole->priv->bcnc);

	if (! gda_tools_favorites_add (bfav, 0, &fav, ORDER_KEY_QUERIES, G_MAXINT, &error)) {
		browser_show_error ((GtkWindow*) gtk_widget_get_toplevel ((GtkWidget*) tconsole),
                                    _("Could not add favorite: %s"),
                                    error && error->message ? error->message : _("No detail"));
                if (error)
                        g_error_free (error);
	}
	else
		tconsole->priv->fav_id = fav.id;
	g_free (fav.contents);

	g_object_unref (set);
	gtk_widget_destroy (dlg);
}

static void
sql_favorite_modify_mitem_cb (G_GNUC_UNUSED GtkMenuItem *mitem, QueryConsolePage *tconsole)
{
	ToolsFavorites *bfav;
	ToolsFavoritesAttributes fav;
	GError *error = NULL;

	memset (&fav, 0, sizeof (fav));
	fav.id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (mitem), "favid"));
	fav.type = GDA_TOOLS_FAVORITES_QUERIES;
	fav.contents = query_editor_get_all_text (tconsole->priv->editor);
	fav.name = g_object_get_data (G_OBJECT (mitem), "favname");

	bfav = browser_connection_get_favorites (tconsole->priv->bcnc);

	if (! gda_tools_favorites_add (bfav, 0, &fav, ORDER_KEY_QUERIES, G_MAXINT, &error)) {
		browser_show_error ((GtkWindow*) gtk_widget_get_toplevel ((GtkWidget*) tconsole),
                                    _("Could not add favorite: %s"),
                                    error && error->message ? error->message : _("No detail"));
                if (error)
                        g_error_free (error);
	}
	g_free (fav.contents);
}

static void
popup_container_position_func (PopupContainer *cont, gint *out_x, gint *out_y)
{
	GtkWidget *console, *top;
	gint x, y;
        GtkRequisition req;

	console = g_object_get_data (G_OBJECT (cont), "console");
	top = gtk_widget_get_toplevel (console);	
	gtk_widget_get_preferred_size ((GtkWidget*) cont, NULL, &req);

	GtkAllocation alloc;
        gdk_window_get_origin (gtk_widget_get_window (top), &x, &y);
	gtk_widget_get_allocation (top, &alloc);
	x += (alloc.width - req.width) / 2;
	y += (alloc.height - req.height) / 2;

        if (x < 0)
                x = 0;

        if (y < 0)
                y = 0;

	*out_x = x;
	*out_y = y;
}

static void
params_form_changed_cb (GdauiBasicForm *form, G_GNUC_UNUSED GdaHolder *param,
			G_GNUC_UNUSED gboolean is_user_modif, QueryConsolePage *tconsole)
{
	/* if all params are valid => authorize the execute button */
	GtkWidget *button;

	button = g_object_get_data (G_OBJECT (tconsole->priv->params_popup), "exec");
	gtk_widget_set_sensitive (button,
				  gdaui_basic_form_is_valid (form));
}

static void actually_execute (QueryConsolePage *tconsole, const gchar *sql, GdaSet *params,
			      gboolean add_editor_history);
static void
sql_execute_clicked_cb (G_GNUC_UNUSED GtkButton *button, QueryConsolePage *tconsole)
{
	gchar *sql;

	if (tconsole->priv->params_popup)
		gtk_widget_hide (tconsole->priv->params_popup);

	/* compute parameters if necessary */
	if (tconsole->priv->params_compute_id > 0) {
		g_source_remove (tconsole->priv->params_compute_id);
		tconsole->priv->params_compute_id = 0;
		compute_params (tconsole);
	}

	if (tconsole->priv->params) {
		if (! gdaui_basic_form_is_valid (GDAUI_BASIC_FORM (tconsole->priv->params_form))) {
			GtkWidget *form, *cont;
			if (! tconsole->priv->params_popup) {
				tconsole->priv->params_popup = popup_container_new_with_func (popup_container_position_func);
				g_object_set_data (G_OBJECT (tconsole->priv->params_popup), "console", tconsole);

				GtkWidget *vbox, *label, *bbox, *button;
				gchar *str;
				vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
				gtk_container_add (GTK_CONTAINER (tconsole->priv->params_popup), vbox);
				gtk_container_set_border_width (GTK_CONTAINER (tconsole->priv->params_popup), 10);

				label = gtk_label_new ("");
				str = g_strdup_printf ("<b>%s</b>:\n<small>%s</small>",
						       _("Invalid variable's contents"),
						       _("assign values to the following variables"));
				gtk_label_set_markup (GTK_LABEL (label), str);
				g_free (str);
				gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

				cont = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
				gtk_box_pack_start (GTK_BOX (vbox), cont, FALSE, FALSE, 10);
				g_object_set_data (G_OBJECT (tconsole->priv->params_popup), "cont", cont);

				bbox = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
				gtk_box_pack_start (GTK_BOX (vbox), bbox, FALSE, FALSE, 10);
				gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
				
				button = gtk_button_new_from_stock (GTK_STOCK_EXECUTE);
				gtk_box_pack_start (GTK_BOX (bbox), button, TRUE, TRUE, 0);
				g_signal_connect_swapped (button, "clicked",
							  G_CALLBACK (gtk_widget_hide), tconsole->priv->params_popup);
				g_signal_connect (button, "clicked",
						  G_CALLBACK (sql_execute_clicked_cb), tconsole);
				gtk_widget_set_sensitive (button, FALSE);
				g_object_set_data (G_OBJECT (tconsole->priv->params_popup), "exec", button);

				button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
				gtk_box_pack_start (GTK_BOX (bbox), button, TRUE, TRUE, 0);
				g_signal_connect_swapped (button, "clicked",
							  G_CALLBACK (gtk_widget_hide), tconsole->priv->params_popup);
			}
			else {
				form = g_object_get_data (G_OBJECT (tconsole->priv->params_popup), "form");
				if (form)
					gtk_widget_destroy (form);
			}

			cont = g_object_get_data (G_OBJECT (tconsole->priv->params_popup), "cont");
			form = gdaui_basic_form_new (tconsole->priv->params);
			g_object_set ((GObject*) form, "show-actions", TRUE, NULL);
			g_signal_connect (form, "holder-changed",
					  G_CALLBACK (params_form_changed_cb), tconsole);
			g_signal_connect (form, "activated",
					  G_CALLBACK (sql_execute_clicked_cb), tconsole);

			gtk_box_pack_start (GTK_BOX (cont), form, TRUE, TRUE, 0);
			g_object_set_data (G_OBJECT (tconsole->priv->params_popup), "form", form);
			gtk_widget_show_all (tconsole->priv->params_popup);

			return;
		}
	}

	sql = query_editor_get_all_text (tconsole->priv->editor);
	actually_execute (tconsole, sql, tconsole->priv->params, TRUE);
	g_free (sql);
}

static void
query_exec_done_cb (BrowserConnection *bcnc, guint exec_id,
		    GObject *out_result, GdaSet *out_last_inserted_row,
		    GError *error, QueryConsolePage *tconsole)
{
	gboolean alldone = TRUE;
	
	if (! tconsole->priv->current_exec)
		return;

	if (! tconsole->priv->current_exec->statements) {
		execution_batch_free (tconsole->priv->current_exec);
		tconsole->priv->current_exec = NULL;
		return;
	}

	ExecutionStatement *estmt;
	estmt = (ExecutionStatement*) tconsole->priv->current_exec->statements->data;
	g_assert (estmt->exec_id == exec_id);
	g_assert (!estmt->result);
	g_assert (!estmt->exec_error);

	ExecutionBatch *ebatch;
	ebatch = tconsole->priv->current_exec;
	query_editor_start_history_batch (tconsole->priv->history, ebatch->hist_batch);
	if (out_result)
		estmt->result = g_object_ref (out_result);
	if (error)
		estmt->exec_error = g_error_copy (error);

	QueryEditorHistoryItem *history;
	GdaSqlStatement *sqlst;
	g_object_get (G_OBJECT (estmt->stmt), "structure", &sqlst, NULL);
	if (!sqlst->sql) {
		gchar *sql;
		sql = gda_statement_to_sql (GDA_STATEMENT (estmt->stmt), NULL, NULL);
		history = query_editor_history_item_new (sql, estmt->result, estmt->exec_error);
		g_free (sql);
	}
	else
		history = query_editor_history_item_new (sqlst->sql,
							 estmt->result, estmt->exec_error);
	gda_sql_statement_free (sqlst);

	history->within_transaction = estmt->within_transaction;

	/* display a message if a transaction has been started */
	if (! history->within_transaction &&
	    browser_connection_get_transaction_status (tconsole->priv->bcnc) &&
	    gda_statement_get_statement_type (estmt->stmt) != GDA_SQL_STATEMENT_BEGIN) {
		browser_window_show_notice_printf (BROWSER_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) tconsole)),
						   GTK_MESSAGE_INFO,
						   "QueryExecTransactionStarted",
						   "%s", _("A transaction has automatically been started\n"
							   "during this statement's execution, this usually\n"
							   "happens when blobs are selected (and the transaction\n"
							   "will have to remain opened while the blobs are still\n"
							   "accessible, clear the corresponding history item before\n"
							   "closing the transaction)."));
	}

	query_editor_add_history_item (tconsole->priv->history, history);
	query_editor_history_item_unref (history);

	if (estmt->exec_error)
		browser_show_error (GTK_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) tconsole)),
				    _("Error executing query:\n%s"),
				    estmt->exec_error && estmt->exec_error->message ?
				    estmt->exec_error->message : _("No detail"));
	else
		browser_window_push_status (BROWSER_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) tconsole)),
					    "QueryConsolePage", _("Statement executed"), TRUE);

	/* get rid of estmt */
	estmt->exec_id = 0;
	execution_statement_free (estmt);
	tconsole->priv->current_exec->statements =
		g_slist_delete_link (tconsole->priv->current_exec->statements,
				     tconsole->priv->current_exec->statements);
	if (error)
		goto onerror;

	/* more to do ? */
	if (tconsole->priv->current_exec->statements) {
		estmt = (ExecutionStatement*) tconsole->priv->current_exec->statements->data;
		estmt->within_transaction =
			browser_connection_get_transaction_status (tconsole->priv->bcnc) ? TRUE : FALSE;
		estmt->exec_id = browser_connection_execute_statement_cb (tconsole->priv->bcnc,
									  estmt->stmt,
									  tconsole->priv->params,
									  GDA_STATEMENT_MODEL_RANDOM_ACCESS,
									  FALSE,
									  (BrowserConnectionExecuteCallback) query_exec_done_cb,
									  tconsole, &(estmt->exec_error));
		if (estmt->exec_id == 0) {
			browser_show_error (GTK_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) tconsole)),
					    _("Error executing query:\n%s"),
					    estmt->exec_error && estmt->exec_error->message ?
					    estmt->exec_error->message : _("No detail"));
			goto onerror;
		}
	}
	
	if (! tconsole->priv->current_exec->statements) {
		execution_batch_free (tconsole->priv->current_exec);
		tconsole->priv->current_exec = NULL;
	}
	return;

 onerror:
	execution_batch_free (tconsole->priv->current_exec);
	tconsole->priv->current_exec = NULL;
}

static void
actually_execute (QueryConsolePage *tconsole, const gchar *sql, GdaSet *params,
		  gboolean add_editor_history)
{
	GdaBatch *batch;
	GError *error = NULL;
	const gchar *remain;

	if (!tconsole->priv->parser)
		tconsole->priv->parser = browser_connection_create_parser (tconsole->priv->bcnc);

	batch = gda_sql_parser_parse_string_as_batch (tconsole->priv->parser, sql, &remain, &error);
	if (!batch) {
		browser_show_error (GTK_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) tconsole)),
				    _("Error while parsing code: %s"),
				    error && error->message ? error->message : _("No detail"));
		g_clear_error (&error);
		return;
	}

	/* if a query is being executed, then show an error */
	if (tconsole->priv->current_exec) {
		g_object_unref (batch);
		browser_show_error (GTK_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) tconsole)),
				    _("A query is already being executed, "
				      "to execute another query, open a new connection."));
		return;
	}

	if (add_editor_history) {
		/* mark the current SQL to be kept by the editor as an internal history */
		query_editor_keep_current_state (tconsole->priv->editor);
	}

	/* actual Execution */
	const GSList *stmt_list, *list;
	ExecutionBatch *ebatch;
	ebatch = g_new0 (ExecutionBatch, 1);
	ebatch->batch = batch;
	g_get_current_time (&(ebatch->start_time));
	ebatch->hist_batch = query_editor_history_batch_new (ebatch->start_time, params);

	stmt_list = gda_batch_get_statements (batch);
	for (list = stmt_list; list; list = list->next) {
		ExecutionStatement *estmt;
		estmt = g_new0 (ExecutionStatement, 1);
		estmt->stmt = GDA_STATEMENT (list->data);
		ebatch->statements = g_slist_prepend (ebatch->statements, estmt);

		if (list != stmt_list)
			continue;

		/* only the 1st statement is actually executed, to be able to
		 * stop others following statements to be executed if there is an
		 * execution error.
		 */
		estmt->within_transaction =
			browser_connection_get_transaction_status (tconsole->priv->bcnc) ? TRUE : FALSE;
		estmt->exec_id = browser_connection_execute_statement_cb (tconsole->priv->bcnc,
									  estmt->stmt,
									  params,
									  GDA_STATEMENT_MODEL_RANDOM_ACCESS,
									  FALSE,
									  (BrowserConnectionExecuteCallback) query_exec_done_cb,
									  tconsole, &(estmt->exec_error));
		if (estmt->exec_id == 0) {
			browser_show_error (GTK_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) tconsole)),
					    _("Error executing query: %s"),
					    estmt->exec_error && estmt->exec_error->message ? estmt->exec_error->message : _("No detail"));
			execution_batch_free (ebatch);
			return;
		}
	}
	ebatch->statements = g_slist_reverse (ebatch->statements);
	tconsole->priv->current_exec = ebatch;
}

static void
rerun_requested_cb (QueryResult *qres, QueryEditorHistoryBatch *batch,
		    QueryEditorHistoryItem *item, QueryConsolePage *tconsole)
{
	if (!batch || !item || !item->sql) {
		browser_show_error (GTK_WINDOW (gtk_widget_get_toplevel ((GtkWidget*) tconsole)),
				    _("Internal error, please report error to "
				      "http://bugzilla.gnome.org/ for the \"libgda\" product"));
		return;
	}

	actually_execute (tconsole, item->sql, batch->params, FALSE);
}


/**
 * query_console_page_set_text
 * @console: a #QueryConsolePage
 * @text: the new text
 * @fav_id: the favorite ID or -1 if not a favorite.
 *
 * Replaces the edited SQL with @text in @console
 */
void
query_console_page_set_text (QueryConsolePage *console, const gchar *text, gint fav_id)
{
	g_return_if_fail (IS_QUERY_CONSOLE_PAGE_PAGE (console));
	console->priv->fav_id = fav_id;
	query_editor_set_text (console->priv->editor, text);
}

/*
 * UI actions
 */
static void
query_execute_cb (G_GNUC_UNUSED GtkAction *action, QueryConsolePage *tconsole)
{
	sql_execute_clicked_cb (NULL, tconsole);
}

#ifdef HAVE_GTKSOURCEVIEW
static void
editor_undo_cb (G_GNUC_UNUSED GtkAction *action, G_GNUC_UNUSED QueryConsolePage *tconsole)
{
	TO_IMPLEMENT;
}
#endif

static GtkActionEntry ui_actions[] = {
	{ "ExecuteQuery", GTK_STOCK_EXECUTE, N_("_Execute"), NULL, N_("Execute query"),
	  G_CALLBACK (query_execute_cb)},
#ifdef HAVE_GTKSOURCEVIEW
	{ "EditorUndo", GTK_STOCK_UNDO, N_("_Undo"), NULL, N_("Undo last change"),
	  G_CALLBACK (editor_undo_cb)},
#endif
};
static const gchar *ui_actions_console =
	"<ui>"
	"  <menubar name='MenuBar'>"
	"      <menu name='Edit' action='Edit'>"
        "        <menuitem name='EditorUndo' action= 'EditorUndo'/>"
        "      </menu>"
	"  </menubar>"
	"  <toolbar name='ToolBar'>"
	"    <separator/>"
	"    <toolitem action='ExecuteQuery'/>"
	"  </toolbar>"
	"</ui>";

static GtkActionGroup *
query_console_page_page_get_actions_group (BrowserPage *page)
{
	QueryConsolePage *tconsole;
	tconsole = QUERY_CONSOLE_PAGE (page);
	if (! tconsole->priv->agroup) {
		tconsole->priv->agroup = gtk_action_group_new ("QueryExecConsoleActions");
		gtk_action_group_set_translation_domain (tconsole->priv->agroup, GETTEXT_PACKAGE);
		gtk_action_group_add_actions (tconsole->priv->agroup,
					      ui_actions, G_N_ELEMENTS (ui_actions), page);

		GtkAction *action;
		action = gtk_action_group_get_action (tconsole->priv->agroup, "ExecuteQuery");
		gtk_action_set_sensitive (action, !browser_connection_is_busy (tconsole->priv->bcnc, NULL));
	}
	return g_object_ref (tconsole->priv->agroup);
}

static const gchar *
query_console_page_page_get_actions_ui (G_GNUC_UNUSED BrowserPage *page)
{
	return ui_actions_console;
}

static GtkWidget *
query_console_page_page_get_tab_label (BrowserPage *page, GtkWidget **out_close_button)
{
	const gchar *tab_name;

	tab_name = _("Query editor");
	return browser_make_tab_label_with_stock (tab_name,
						  STOCK_CONSOLE,
						  out_close_button ? TRUE : FALSE, out_close_button);
}

static void
query_console_page_grab_focus (GtkWidget *widget)
{
	QueryConsolePage *tconsole;

	tconsole = QUERY_CONSOLE_PAGE (widget);
	gtk_widget_grab_focus (GTK_WIDGET (tconsole->priv->editor));
}
