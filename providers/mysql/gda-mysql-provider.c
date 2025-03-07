/*
 * Copyright (C) 2001 - 2004 Rodrigo Moya <rodrigo@gnome-db.org>
 * Copyright (C) 2002 - 2003 Gonzalo Paniagua Javier <gonzalo@ximian.com>
 * Copyright (C) 2002 Holger Thon <holger.thon@gnome-db.org>
 * Copyright (C) 2002 Zbigniew Chyla <cyba@gnome.pl>
 * Copyright (C) 2002 Cleber Rodrigues <cleber@gnome-db.org>
 * Copyright (C) 2003 Akira TAGOH <tagoh@gnome-db.org>
 * Copyright (C) 2003 Paisa Seeluangsawat <paisa@users.sf.net>
 * Copyright (C) 2004 - 2005 Alan Knowles <alan@akbkhome.com>
 * Copyright (C) 2004 Caolan McNamara <caolanm@redhat.com>
 * Copyright (C) 2004 Julio M. Merino Vidal <jmmv@menta.net>
 * Copyright (C) 2004 Jürg Billeter <j@bitron.ch>
 * Copyright (C) 2004 Szalai Ferenc <szferi@einstein.ki.iif.hu>
 * Copyright (C) 2005 - 2009 Bas Driessen <bas.driessen@xobas.com>
 * Copyright (C) 2005 - 2013 Vivien Malerba <malerba@gnome-db.org>
 * Copyright (C) 2005 Álvaro Peña <alvaropg@telefonica.net>
 * Copyright (C) 2007 Armin Burgmeier <armin@openismus.com>
 * Copyright (C) 2007 - 2011 Murray Cumming <murrayc@murrayc.com>
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

#undef GDA_DISABLE_DEPRECATED
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <libgda/libgda.h>
#include <libgda/gda-data-model-private.h>
#include <libgda/gda-server-provider-extra.h>
#include <libgda/binreloc/gda-binreloc.h>
#include <libgda/gda-statement-extra.h>
#include <sql-parser/gda-sql-parser.h>
#include <libgda/gda-blob-op.h>
#include "gda-mysql.h"
#include "gda-mysql-provider.h"
#include "gda-mysql-recordset.h"
#include "gda-mysql-ddl.h"
#include "gda-mysql-meta.h"

#include "gda-mysql-util.h"
#include "gda-mysql-parser.h"
#include "gda-mysql-handler-boolean.h"
#include "gda-mysql-handler-bin.h"
#include <libgda/gda-debug-macros.h>

#define _GDA_PSTMT(x) ((GdaPStmt*)(x))

#ifdef MYSQL8
typedef bool my_bool;
#endif
/*
 * GObject methods
 */
static void gda_mysql_provider_class_init (GdaMysqlProviderClass  *klass);
static void gda_mysql_provider_init       (GdaMysqlProvider       *provider,
					   GdaMysqlProviderClass  *klass);
static void gda_mysql_provider_set_property (GObject *object,
					     guint param_id,
					     const GValue *value,
					     GParamSpec *pspec);
static void gda_mysql_provider_get_property (GObject *object,
					     guint param_id,
					     GValue *value,
					     GParamSpec *pspec);
static GObjectClass *parent_class = NULL;

/* properties */
enum
{
        PROP_0,
        PROP_IDENT_CASE_SENSITIVE, /* used for tests */
};

/*
 * GdaServerProvider's virtual methods
 */
/* connection management */
static gboolean            gda_mysql_provider_open_connection (GdaServerProvider               *provider,
							       GdaConnection                   *cnc,
							       GdaQuarkList                    *params,
							       GdaQuarkList                    *auth,
							       guint                           *task_id,
							       GdaServerProviderAsyncCallback   async_cb,
							       gpointer                         cb_data);
static gboolean            gda_mysql_provider_close_connection (GdaServerProvider  *provider,
								GdaConnection      *cnc);
static const gchar        *gda_mysql_provider_get_server_version (GdaServerProvider  *provider,
								  GdaConnection      *cnc);
static const gchar        *gda_mysql_provider_get_database (GdaServerProvider  *provider,
							    GdaConnection      *cnc);

/* DDL operations */
static gboolean            gda_mysql_provider_supports_operation (GdaServerProvider       *provider,
								  GdaConnection           *cnc,
								  GdaServerOperationType   type,
								  GdaSet                  *options);
static GdaServerOperation *gda_mysql_provider_create_operation (GdaServerProvider       *provider,
								GdaConnection           *cnc,
								GdaServerOperationType   type,
								G_GNUC_UNUSED GdaSet                  *options,
								GError                **error);
static gchar              *gda_mysql_provider_render_operation (GdaServerProvider   *provider,
								GdaConnection       *cnc,
								GdaServerOperation  *op,
								GError             **error);

static gboolean            gda_mysql_provider_perform_operation (GdaServerProvider               *provider,
								 GdaConnection                   *cnc,
								 GdaServerOperation              *op,
								 guint                           *task_id, 
								 GdaServerProviderAsyncCallback   async_cb,
								 gpointer                         cb_data,
								 GError                         **error);
/* transactions */
static gboolean            gda_mysql_provider_begin_transaction (GdaServerProvider        *provider,
								 GdaConnection            *cnc,
								 const gchar              *name,
								 GdaTransactionIsolation   level,
								 GError                  **error);
static gboolean            gda_mysql_provider_commit_transaction (GdaServerProvider  *provider,
								  GdaConnection *cnc,
								  const gchar *name, GError **error);
static gboolean            gda_mysql_provider_rollback_transaction (GdaServerProvider *provider,
								    GdaConnection     *cnc,
								    const gchar       *name,
								    GError           **error);
static gboolean            gda_mysql_provider_add_savepoint (GdaServerProvider  *provider,
							     GdaConnection      *cnc,
							     const gchar        *name,
							     GError            **error);
static gboolean            gda_mysql_provider_rollback_savepoint (GdaServerProvider  *provider,
								  GdaConnection      *cnc,
								  const gchar        *name,
								  GError            **error);
static gboolean            gda_mysql_provider_delete_savepoint (GdaServerProvider  *provider,
								GdaConnection      *cnc,
								const gchar        *name,
								GError            **error);

/* information retrieval */
static const gchar        *gda_mysql_provider_get_version (GdaServerProvider  *provider);
static gboolean            gda_mysql_provider_supports_feature (GdaServerProvider     *provider,
								GdaConnection         *cnc,
								GdaConnectionFeature   feature);

static const gchar        *gda_mysql_provider_get_name (GdaServerProvider  *provider);

static GdaDataHandler     *gda_mysql_provider_get_data_handler (GdaServerProvider  *provider,
								GdaConnection      *cnc,
								GType               g_type,
								const gchar        *dbms_type);

static const gchar*        gda_mysql_provider_get_default_dbms_type (GdaServerProvider  *provider,
								     GdaConnection      *cnc,
								     GType               type);
/* statements */
static GdaSqlParser        *gda_mysql_provider_create_parser (GdaServerProvider  *provider,
							      GdaConnection      *cnc);
static gchar               *gda_mysql_provider_statement_to_sql  (GdaServerProvider    *provider,
								  GdaConnection        *cnc,
								  GdaStatement         *stmt,
								  GdaSet               *params, 
								  GdaStatementSqlFlag   flags,
								  GSList              **params_used,
								  GError              **error);
static gboolean             gda_mysql_provider_statement_prepare (GdaServerProvider  *provider,
								  GdaConnection      *cnc,
								  GdaStatement       *stmt,
								  GError            **error);
static GObject             *gda_mysql_provider_statement_execute (GdaServerProvider               *provider,
								  GdaConnection                   *cnc,
								  GdaStatement                    *stmt,
								  GdaSet                          *params,
								  GdaStatementModelUsage           model_usage, 
								  GType                           *col_types,
								  GdaSet                         **last_inserted_row, 
								  guint                           *task_id,
								  GdaServerProviderExecCallback    async_cb, 
								  gpointer                         cb_data,
								  GError                         **error);
static GdaSqlStatement     *gda_mysql_statement_rewrite          (GdaServerProvider *provider, GdaConnection *cnc,
								  GdaStatement *stmt, GdaSet *params, GError **error);


/* Quoting */
static gchar               *gda_mysql_identifier_quote    (GdaServerProvider *provider, GdaConnection *cnc,
							   const gchar *id,
							   gboolean meta_store_convention, gboolean force_quotes);

/* distributed transactions */
static gboolean gda_mysql_provider_xa_start    (GdaServerProvider         *provider,
						GdaConnection             *cnc, 
						const GdaXaTransactionId  *xid,
						GError                   **error);

static gboolean gda_mysql_provider_xa_end      (GdaServerProvider         *provider,
						GdaConnection             *cnc, 
						const GdaXaTransactionId  *xid,
						GError                   **error);
static gboolean gda_mysql_provider_xa_prepare  (GdaServerProvider         *provider,
						GdaConnection             *cnc, 
						const GdaXaTransactionId  *xid,
						GError                   **error);

static gboolean gda_mysql_provider_xa_commit   (GdaServerProvider        *provider,
						GdaConnection            *cnc, 
						const GdaXaTransactionId  *xid,
						GError                   **error);
static gboolean gda_mysql_provider_xa_rollback (GdaServerProvider         *provider,
						GdaConnection             *cnc, 
						const GdaXaTransactionId  *xid,
						GError                   **error);

static GList   *gda_mysql_provider_xa_recover  (GdaServerProvider  *provider,
						GdaConnection      *cnc, 
						GError            **error);


/* 
 * private connection data destroy 
 */
static void gda_mysql_free_cnc_data (MysqlConnectionData  *cdata);


/*
 * Prepared internal statements
 * TO_ADD: any prepared statement to be used internally by the provider should be
 *         declared here, as constants and as SQL statements
 */
static GMutex init_mutex;
static GdaStatement **internal_stmt = NULL;

typedef enum {
	INTERNAL_STMT1
} InternalStatementItem;

gchar *internal_sql[] = {
	"SQL for INTERNAL_STMT1"
};

/*
 * GdaMysqlProvider class implementation
 */
static void
gda_mysql_provider_class_init (GdaMysqlProviderClass  *klass)
{
	GdaServerProviderClass *provider_class = GDA_SERVER_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	/* Properties */
        object_class->set_property = gda_mysql_provider_set_property;
        object_class->get_property = gda_mysql_provider_get_property;

	g_object_class_install_property (object_class, PROP_IDENT_CASE_SENSITIVE,
                                         g_param_spec_boolean ("identifiers-case-sensitive", NULL, NULL, TRUE,
							       G_PARAM_READABLE | G_PARAM_WRITABLE));


	/* virtual methods */
	provider_class->get_version = gda_mysql_provider_get_version;
	provider_class->get_server_version = gda_mysql_provider_get_server_version;
	provider_class->get_name = gda_mysql_provider_get_name;
	provider_class->supports_feature = gda_mysql_provider_supports_feature;

	provider_class->get_data_handler = gda_mysql_provider_get_data_handler;
	provider_class->get_def_dbms_type = gda_mysql_provider_get_default_dbms_type;

	provider_class->open_connection = gda_mysql_provider_open_connection;
	provider_class->close_connection = gda_mysql_provider_close_connection;
	provider_class->get_database = gda_mysql_provider_get_database;

	provider_class->supports_operation = gda_mysql_provider_supports_operation;
        provider_class->create_operation = gda_mysql_provider_create_operation;
        provider_class->render_operation = gda_mysql_provider_render_operation;
        provider_class->perform_operation = gda_mysql_provider_perform_operation;

	provider_class->begin_transaction = gda_mysql_provider_begin_transaction;
	provider_class->commit_transaction = gda_mysql_provider_commit_transaction;
	provider_class->rollback_transaction = gda_mysql_provider_rollback_transaction;
	provider_class->add_savepoint = gda_mysql_provider_add_savepoint;
        provider_class->rollback_savepoint = gda_mysql_provider_rollback_savepoint;
        provider_class->delete_savepoint = gda_mysql_provider_delete_savepoint;

	provider_class->create_parser = gda_mysql_provider_create_parser;
	provider_class->statement_to_sql = gda_mysql_provider_statement_to_sql;
	provider_class->statement_prepare = gda_mysql_provider_statement_prepare;
	provider_class->statement_execute = gda_mysql_provider_statement_execute;
	provider_class->statement_rewrite = gda_mysql_statement_rewrite;

	provider_class->is_busy = NULL;
	provider_class->cancel = NULL;
	provider_class->create_connection = NULL;

	provider_class->identifier_quote = gda_mysql_identifier_quote;

	memset (&(provider_class->meta_funcs), 0, sizeof (GdaServerProviderMeta));
	provider_class->meta_funcs._info = _gda_mysql_meta__info;
        provider_class->meta_funcs._btypes = _gda_mysql_meta__btypes;
        provider_class->meta_funcs._udt = _gda_mysql_meta__udt;
        provider_class->meta_funcs.udt = _gda_mysql_meta_udt;
        provider_class->meta_funcs._udt_cols = _gda_mysql_meta__udt_cols;
        provider_class->meta_funcs.udt_cols = _gda_mysql_meta_udt_cols;
        provider_class->meta_funcs._enums = _gda_mysql_meta__enums;
        provider_class->meta_funcs.enums = _gda_mysql_meta_enums;
        provider_class->meta_funcs._domains = _gda_mysql_meta__domains;
        provider_class->meta_funcs.domains = _gda_mysql_meta_domains;
        provider_class->meta_funcs._constraints_dom = _gda_mysql_meta__constraints_dom;
        provider_class->meta_funcs.constraints_dom = _gda_mysql_meta_constraints_dom;
        provider_class->meta_funcs._el_types = _gda_mysql_meta__el_types;
        provider_class->meta_funcs._collations = _gda_mysql_meta__collations;
        provider_class->meta_funcs.collations = _gda_mysql_meta_collations;
        provider_class->meta_funcs._character_sets = _gda_mysql_meta__character_sets;
        provider_class->meta_funcs.character_sets = _gda_mysql_meta_character_sets;
        provider_class->meta_funcs._schemata = _gda_mysql_meta__schemata;
        provider_class->meta_funcs.schemata = _gda_mysql_meta_schemata;
        provider_class->meta_funcs._tables_views = _gda_mysql_meta__tables_views;
        provider_class->meta_funcs.tables_views = _gda_mysql_meta_tables_views;
        provider_class->meta_funcs._columns = _gda_mysql_meta__columns;
        provider_class->meta_funcs.columns = _gda_mysql_meta_columns;
        provider_class->meta_funcs._view_cols = _gda_mysql_meta__view_cols;
        provider_class->meta_funcs.view_cols = _gda_mysql_meta_view_cols;
        provider_class->meta_funcs._constraints_tab = _gda_mysql_meta__constraints_tab;
        provider_class->meta_funcs.constraints_tab = _gda_mysql_meta_constraints_tab;
        provider_class->meta_funcs._constraints_ref = _gda_mysql_meta__constraints_ref;
        provider_class->meta_funcs.constraints_ref = _gda_mysql_meta_constraints_ref;
        provider_class->meta_funcs._key_columns = _gda_mysql_meta__key_columns;
        provider_class->meta_funcs.key_columns = _gda_mysql_meta_key_columns;
        provider_class->meta_funcs._check_columns = _gda_mysql_meta__check_columns;
        provider_class->meta_funcs.check_columns = _gda_mysql_meta_check_columns;
        provider_class->meta_funcs._triggers = _gda_mysql_meta__triggers;
        provider_class->meta_funcs.triggers = _gda_mysql_meta_triggers;
        provider_class->meta_funcs._routines = _gda_mysql_meta__routines;
        provider_class->meta_funcs.routines = _gda_mysql_meta_routines;
        provider_class->meta_funcs._routine_col = _gda_mysql_meta__routine_col;
        provider_class->meta_funcs.routine_col = _gda_mysql_meta_routine_col;
        provider_class->meta_funcs._routine_par = _gda_mysql_meta__routine_par;
        provider_class->meta_funcs.routine_par = _gda_mysql_meta_routine_par;
	provider_class->meta_funcs._indexes_tab = _gda_mysql_meta__indexes_tab;
        provider_class->meta_funcs.indexes_tab = _gda_mysql_meta_indexes_tab;
        provider_class->meta_funcs._index_cols = _gda_mysql_meta__index_cols;
        provider_class->meta_funcs.index_cols = _gda_mysql_meta_index_cols;

	/* distributed transactions: if not supported, then provider_class->xa_funcs should be set to NULL */
	provider_class->xa_funcs = g_new0 (GdaServerProviderXa, 1);
	provider_class->xa_funcs->xa_start = gda_mysql_provider_xa_start;
	provider_class->xa_funcs->xa_end = gda_mysql_provider_xa_end;
	provider_class->xa_funcs->xa_prepare = gda_mysql_provider_xa_prepare;
	provider_class->xa_funcs->xa_commit = gda_mysql_provider_xa_commit;
	provider_class->xa_funcs->xa_rollback = gda_mysql_provider_xa_rollback;
	provider_class->xa_funcs->xa_recover = gda_mysql_provider_xa_recover;
	
	if (!mysql_thread_safe ()) {
		gda_log_message ("MySQL was not compiled with the --enable-thread-safe-client flag, "
				 "only one thread can access the provider");
		provider_class->limiting_thread = GDA_SERVER_PROVIDER_UNDEFINED_LIMITING_THREAD;
	}
	else
		provider_class->limiting_thread = NULL;
	
}

static void
gda_mysql_provider_init (GdaMysqlProvider       *mysql_prv,
			 G_GNUC_UNUSED GdaMysqlProviderClass  *klass)
{
	g_mutex_lock (&init_mutex);

	if (!internal_stmt) {
		InternalStatementItem i;
		GdaSqlParser *parser;

		parser = gda_server_provider_internal_get_parser ((GdaServerProvider*) mysql_prv);
		internal_stmt = g_new0 (GdaStatement *, sizeof (internal_sql) / sizeof (gchar*));
		for (i = INTERNAL_STMT1; i < sizeof (internal_sql) / sizeof (gchar*); i++) {
			internal_stmt[i] = gda_sql_parser_parse_string (parser, internal_sql[i], NULL, NULL);
			if (!internal_stmt[i])
				g_error ("Could not parse internal statement: %s\n", internal_sql[i]);
		}
	}

	/* meta data init */
	_gda_mysql_provider_meta_init ((GdaServerProvider*) mysql_prv);

	/* for tests */
	mysql_prv->test_mode = FALSE;
	mysql_prv->test_identifiers_case_sensitive = TRUE;

	g_mutex_unlock (&init_mutex);
}

GType
gda_mysql_provider_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GMutex registering;
		static GTypeInfo info = {
			sizeof (GdaMysqlProviderClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gda_mysql_provider_class_init,
			NULL, NULL,
			sizeof (GdaMysqlProvider),
			0,
			(GInstanceInitFunc) gda_mysql_provider_init,
			NULL
		};
		g_mutex_lock (&registering);
		if (type == 0)
			type = g_type_register_static (GDA_TYPE_SERVER_PROVIDER, "GdaMysqlProvider", &info, 0);
		g_mutex_unlock (&registering);
	}

	return type;
}

static void
gda_mysql_provider_set_property (GObject *object,
				 guint param_id,
				 const GValue *value,
				 G_GNUC_UNUSED GParamSpec *pspec)
{
	GdaMysqlProvider *mysql_prv;
	mysql_prv = GDA_MYSQL_PROVIDER (object);
	switch (param_id) {
	case PROP_IDENT_CASE_SENSITIVE:
		mysql_prv->test_identifiers_case_sensitive = g_value_get_boolean (value);
		mysql_prv->test_mode = TRUE;
		break;
	}
}

static void
gda_mysql_provider_get_property (GObject *object,
				 guint param_id,
				 GValue *value,
				 G_GNUC_UNUSED GParamSpec *pspec)
{
	GdaMysqlProvider *mysql_prv;
	mysql_prv = GDA_MYSQL_PROVIDER (object);
	switch (param_id) {
	case PROP_IDENT_CASE_SENSITIVE:
		g_value_set_boolean (value, mysql_prv->test_identifiers_case_sensitive);
		break;
	}
}

/*
 * Get provider name request
 */
static const gchar *
gda_mysql_provider_get_name (G_GNUC_UNUSED GdaServerProvider *provider)
{
	return MYSQL_PROVIDER_NAME;
}

/* 
 * Get provider's version, no need to change this
 */
static const gchar *
gda_mysql_provider_get_version (G_GNUC_UNUSED GdaServerProvider  *provider)
{
	return PACKAGE_VERSION;
}


/*
 * Open a MYSQL connection.
 * If @port <= 0 then @port is not used.
 */
static MYSQL *
real_open_connection (const gchar  *host,
		      gint          port,
		      const gchar  *socket,
		      const gchar  *db,
		      const gchar  *username,
		      const gchar  *password,
		      gboolean      use_ssl,
		      gboolean      compress,
		      const gchar  *proto,
		      GError      **error)
{
	unsigned int flags = CLIENT_FOUND_ROWS;

	/* Exclusive: host/pair otherwise unix socket. */
	if ((host || port > 0) && socket) {
		g_set_error (error, GDA_SERVER_PROVIDER_ERROR, GDA_SERVER_PROVIDER_MISUSE_ERROR,
			     "%s", 
			     _("Cannot give a UNIX SOCKET if you also provide "
			       "either a HOST or a PORT"));
		return NULL;
	}

	if (port > 65535) {
		g_set_error (error, GDA_SERVER_PROVIDER_ERROR, GDA_SERVER_PROVIDER_MISUSE_ERROR,
			     "%s", 
			     _("Invalid port number"));
		return NULL;
	} 

	/* Defaults. */
	if (!socket) {
		if (!host)
			host = "localhost";
		else if (port <= 0)
			port = 3306;
	}

	if (use_ssl)
		flags |= CLIENT_SSL;
	if (compress)
		flags |= CLIENT_COMPRESS;
	
	MYSQL *mysql = NULL;
	mysql = mysql_init (NULL);

	if ((port > 0) || proto) {
		gint p = MYSQL_PROTOCOL_DEFAULT;
		if (proto) {
			if (! g_ascii_strcasecmp (proto, "DEFAULT"))
				p = MYSQL_PROTOCOL_DEFAULT;
			else if (! g_ascii_strcasecmp (proto, "TCP"))
				p = MYSQL_PROTOCOL_TCP;
			else if (! g_ascii_strcasecmp (proto, "SOCKET"))
				p = MYSQL_PROTOCOL_SOCKET;
			else if (! g_ascii_strcasecmp (proto, "PIPE"))
				p = MYSQL_PROTOCOL_PIPE;
			else if (! g_ascii_strcasecmp (proto, "MEMORY"))
				p = MYSQL_PROTOCOL_MEMORY;
			else {
				g_set_error (error, GDA_CONNECTION_ERROR, GDA_CONNECTION_OPEN_ERROR,
					     _("Unknown MySQL protocol '%s'"), proto);
				mysql_close (mysql);
				return NULL;
			}
		}
		else
			p = MYSQL_PROTOCOL_TCP;

		if (mysql_options (mysql, MYSQL_OPT_PROTOCOL, (const char *) &p)) {
			g_set_error (error, GDA_CONNECTION_ERROR, GDA_CONNECTION_OPEN_ERROR,
				     "%s", mysql_error (mysql));
			mysql_close (mysql);
			return NULL;
		}
	}

	MYSQL *return_mysql = mysql_real_connect (mysql, host,
						  username, password,
#if MYSQL_VERSION_ID >= 32200
						  db,
#endif
						  (port > 0) ? port : 0,
						  socket, flags);
	if (!return_mysql || mysql != return_mysql) {
		g_set_error (error, GDA_CONNECTION_ERROR, GDA_CONNECTION_OPEN_ERROR,
			     "%s", mysql_error (mysql));
		mysql_close (mysql);
		mysql = NULL;
	}

	/* Optionnally set some attributes for the newly opened connection (encoding to UTF-8 for example )*/

#if MYSQL_VERSION_ID < 32200
	if (mysql &&
	    mysql_select_db (mysql, db) != 0) {
		g_set_error (error, GDA_CONNECTION_ERROR, GDA_CONNECTION_OPEN_ERROR,
			     "%s", mysql_error (mysql));
		mysql_close (mysql);
		mysql = NULL;
	}
#endif

#if MYSQL_VERSION_ID >= 50007
	if (mysql &&
	    mysql_set_character_set (mysql, "utf8")) {
		g_warning (_("Could not set client charset to UTF8. "
			     "Using %s. It'll be problems with non UTF-8 characters"),
			   mysql_character_set_name (mysql));
	}
#endif

	return mysql;
}

int
gda_mysql_real_query_wrap (GdaConnection *cnc, MYSQL *mysql, const char *stmt_str, unsigned long length)
{
	GdaConnectionEvent *event;
	
	event = gda_connection_point_available_event (cnc, GDA_CONNECTION_EVENT_COMMAND);
	gda_connection_event_set_description (event, stmt_str);
	gda_connection_add_event (cnc, event);
	
	return mysql_real_query (mysql, stmt_str, length);
}

/* 
 * Open connection request
 *
 * In this function, the following _must_ be done:
 *   - check for the presence and validify of the parameters required to actually open a connection,
 *     using @params
 *   - open the real connection to the database using the parameters previously checked
 *   - create a MysqlConnectionData structure and associate it to @cnc
 *
 * Returns: TRUE if no error occurred, or FALSE otherwise (and an ERROR gonnection event must be added to @cnc)
 */
static gboolean
gda_mysql_provider_open_connection (GdaServerProvider               *provider,
				    GdaConnection                   *cnc,
				    GdaQuarkList                    *params,
				    GdaQuarkList                    *auth,
				    G_GNUC_UNUSED guint             *task_id,
				    GdaServerProviderAsyncCallback   async_cb,
				    gpointer                         cb_data)
{
	g_return_val_if_fail (GDA_IS_MYSQL_PROVIDER (provider), FALSE);
	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);

	/* If asynchronous connection opening is not supported, then exit now */
	if (async_cb) {
		gda_connection_add_event_string (cnc, _("Provider does not support asynchronous connection open"));
                return FALSE;
	}

	/* Check for connection parameters */
	/* TO_ADD: your own connection parameters */
	const gchar *db_name;
	db_name = gda_quark_list_find (params, "DB_NAME");
	if (!db_name) {
		gda_connection_add_event_string (cnc,
						 _("The connection string must contain the DB_NAME values"));
		return FALSE;
	}
	
	const gchar *host;
	host = gda_quark_list_find (params, "HOST");

	const gchar *user, *password;
	user = gda_quark_list_find (auth, "USERNAME");
	if (!user)
		user = gda_quark_list_find (params, "USERNAME");
	password = gda_quark_list_find (auth, "PASSWORD");
	if (!password)
		password = gda_quark_list_find (params, "PASSWORD");

	const gchar *port, *unix_socket, *use_ssl, *compress, *proto;
	port = gda_quark_list_find (params, "PORT");
	unix_socket = gda_quark_list_find (params, "UNIX_SOCKET");
	use_ssl = gda_quark_list_find (params, "USE_SSL");
	compress = gda_quark_list_find (params, "COMPRESS");
	proto = gda_quark_list_find (params, "PROTOCOL");
	
	/* open the real connection to the database */
	/* TO_ADD: C API specific function calls;
	 * if it fails, add a connection event and return FALSE */
	// TO_IMPLEMENT;
	
	GError *error = NULL;
	MYSQL *mysql = real_open_connection (host, (port != NULL) ? atoi (port) : -1,
					     unix_socket, db_name,
					     user, password,
					     (use_ssl && ((*use_ssl == 't') || (*use_ssl == 'T'))) ? TRUE : FALSE,
					     (compress && ((*compress == 't') || (*compress == 'T'))) ? TRUE : FALSE,
					     proto,
					     &error);
	if (!mysql) {
		GdaConnectionEvent *event_error = gda_connection_point_available_event (cnc, GDA_CONNECTION_EVENT_ERROR);
		gda_connection_event_set_sqlstate (event_error, _("Unknown"));
		gda_connection_event_set_description (event_error,
						      error && error->message ? error->message :
						      _("No description"));
		gda_connection_event_set_code (event_error, GDA_CONNECTION_EVENT_CODE_UNKNOWN);
		gda_connection_event_set_source (event_error, "gda-mysql");
		gda_connection_add_event (cnc, event_error);
		g_clear_error (&error);

		return FALSE;
	}

	/* Set some attributes for the newly opened connection (encoding to UTF-8)*/
	int res;
	res = mysql_query (mysql, "SET NAMES 'utf8'");
	if (res != 0) {
		_gda_mysql_make_error (cnc, mysql, NULL, NULL);
		mysql_close (mysql);
		return FALSE;
	}

	/* Create a new instance of the provider specific data associated to a connection (MysqlConnectionData),
	 * and set its contents */
	MysqlConnectionData *cdata;
	cdata = g_new0 (MysqlConnectionData, 1);
	gda_connection_internal_set_provider_data (cnc, cdata, (GDestroyNotify) gda_mysql_free_cnc_data);
	cdata->cnc = cnc;
	cdata->mysql = mysql;

	/* handle the reuseable part */
	GdaProviderReuseableOperations *ops;
	ops = _gda_mysql_reuseable_get_ops ();
	cdata->reuseable = (GdaMysqlReuseable*) ops->re_new_data ();
	if (! _gda_mysql_compute_version (cnc, cdata->reuseable, &error)) {
		GdaConnectionEvent *event_error = gda_connection_point_available_event (cnc, GDA_CONNECTION_EVENT_ERROR);
		gda_connection_event_set_sqlstate (event_error, _("Unknown"));
		gda_connection_event_set_description (event_error,
						      error && error->message ? error->message :
						      _("No description"));
		gda_connection_event_set_code (event_error, GDA_CONNECTION_EVENT_CODE_UNKNOWN);
		gda_connection_event_set_source (event_error, "gda-mysql");
		gda_connection_add_event (cnc, event_error);
		g_clear_error (&error);

		gda_mysql_free_cnc_data (cdata);
		gda_connection_internal_set_provider_data (cnc, NULL, NULL);
		return FALSE;
	}

	return TRUE;
}

/* 
 * Close connection request
 *
 * In this function, the following _must_ be done:
 *   - Actually close the connection to the database using @cnc's associated MysqlConnectionData structure
 *   - Free the MysqlConnectionData structure and its contents
 *
 * Returns: TRUE if no error occurred, or FALSE otherwise (and an ERROR gonnection event must be added to @cnc)
 */
static gboolean
gda_mysql_provider_close_connection (GdaServerProvider  *provider,
				     GdaConnection      *cnc)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);

	/* Close the connection using the C API */
	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data (cnc);
	if (!cdata) 
		return FALSE;
	// TO_IMPLEMENT;

	/* Free the MysqlConnectionData structure and its contents*/
	gda_mysql_free_cnc_data (cdata);
	gda_connection_internal_set_provider_data (cnc, NULL, NULL);

	return TRUE;
}

/*
 * Server version request
 *
 * Returns the server version as a string, which should be stored in @cnc's associated MysqlConnectionData structure
 */
static const gchar *
gda_mysql_provider_get_server_version (GdaServerProvider  *provider,
				       GdaConnection      *cnc)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), NULL);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, NULL);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data (cnc);
	if (!cdata) 
		return FALSE;
	if (! ((GdaProviderReuseable*)cdata->reuseable)->server_version)
		_gda_mysql_compute_version (cnc, cdata->reuseable, NULL);
	return ((GdaProviderReuseable*)cdata->reuseable)->server_version;
}

/*
 * Get database request
 *
 * Returns the server version as a string, which should be stored in @cnc's associated MysqlConnectionData structure
 */
static const gchar *
gda_mysql_provider_get_database (GdaServerProvider  *provider,
				 GdaConnection      *cnc)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), NULL);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, NULL);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data (cnc);
	if (!cdata) 
		return NULL;
	TO_IMPLEMENT;
	return NULL;
}

/*
 * Support operation request
 *
 * Tells what the implemented server operations are. To add support for an operation, the following steps are required:
 *   - create a mysql_specs_....xml.in file describing the required and optional parameters for the operation
 *   - add it to the Makefile.am
 *   - make this method return TRUE for the operation type
 *   - implement the gda_mysql_provider_render_operation() and gda_mysql_provider_perform_operation() methods
 *
 * In this example, the GDA_SERVER_OPERATION_CREATE_TABLE is implemented
 */
static gboolean
gda_mysql_provider_supports_operation (GdaServerProvider       *provider,
				       GdaConnection           *cnc,
				       GdaServerOperationType   type,
				       G_GNUC_UNUSED GdaSet                  *options)
{
	if (cnc) {
		g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
		g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	}

        switch (type) {
        case GDA_SERVER_OPERATION_CREATE_DB:
        case GDA_SERVER_OPERATION_DROP_DB:
        case GDA_SERVER_OPERATION_CREATE_TABLE:
        case GDA_SERVER_OPERATION_DROP_TABLE:
        case GDA_SERVER_OPERATION_RENAME_TABLE:
        case GDA_SERVER_OPERATION_COMMENT_TABLE:
        case GDA_SERVER_OPERATION_ADD_COLUMN:
	case GDA_SERVER_OPERATION_DROP_COLUMN:
	case GDA_SERVER_OPERATION_COMMENT_COLUMN:
        case GDA_SERVER_OPERATION_CREATE_INDEX:
        case GDA_SERVER_OPERATION_DROP_INDEX:
        case GDA_SERVER_OPERATION_CREATE_VIEW:
        case GDA_SERVER_OPERATION_DROP_VIEW:
		return TRUE;
        default:
                return FALSE;
        }
}

/*
 * Create operation request
 *
 * Creates a #GdaServerOperation. The following code is generic and should only be changed
 * if some further initialization is required, or if operation's contents is dependent on @cnc
 */
static GdaServerOperation *
gda_mysql_provider_create_operation (GdaServerProvider       *provider,
				     GdaConnection           *cnc,
				     GdaServerOperationType   type,
				     G_GNUC_UNUSED GdaSet                  *options,
				     GError                 **error)
{
        gchar *file;
        GdaServerOperation *op;
        gchar *str;
	gchar *dir;

	if (cnc) {
		g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
		g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	}

        file = g_utf8_strdown (gda_server_operation_op_type_to_string (type), -1);
        str = g_strdup_printf ("mysql_specs_%s.xml", file);
        g_free (file);

	dir = gda_gbr_get_file_path (GDA_DATA_DIR, LIBGDA_ABI_NAME, NULL);
        file = gda_server_provider_find_file (provider, dir, str);
	g_free (dir);

        if (!file) {
                g_set_error (error, GDA_SERVER_PROVIDER_ERROR, GDA_SERVER_PROVIDER_FILE_NOT_FOUND_ERROR,
			     _("Missing spec. file '%s'"), str);
		g_free (str);
                return NULL;
        }
        g_free (str);

        op = gda_server_operation_new (type, file);
        g_free (file);

        return op;
}

/*
 * Render operation request
 */
static gchar *
gda_mysql_provider_render_operation (GdaServerProvider   *provider,
				     GdaConnection       *cnc,
				     GdaServerOperation  *op,
				     GError             **error)
{
        gchar *sql = NULL;
        gchar *file;
        gchar *str;
	gchar *dir;

	if (cnc) {
		g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
		g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	}

	/* test @op's validity */
        file = g_utf8_strdown (gda_server_operation_op_type_to_string (gda_server_operation_get_op_type (op)), -1);
        str = g_strdup_printf ("mysql_specs_%s.xml", file);
        g_free (file);

	dir = gda_gbr_get_file_path (GDA_DATA_DIR, LIBGDA_ABI_NAME, NULL);
        file = gda_server_provider_find_file (provider, dir, str);
	g_free (dir);

        if (!file) {
                g_set_error (error, GDA_SERVER_PROVIDER_ERROR, GDA_SERVER_PROVIDER_FILE_NOT_FOUND_ERROR,
			     _("Missing spec. file '%s'"), str);
		g_free (str);
                return NULL;
        }
        g_free (str);
        if (!gda_server_operation_is_valid (op, file, error)) {
                g_free (file);
                return NULL;
        }
        g_free (file);

	/* actual rendering */
        switch (gda_server_operation_get_op_type (op)) {
        case GDA_SERVER_OPERATION_CREATE_DB:
		sql = gda_mysql_render_CREATE_DB (provider, cnc, op, error);
		break;
        case GDA_SERVER_OPERATION_DROP_DB:
		sql = gda_mysql_render_DROP_DB (provider, cnc, op, error);
                break;
        case GDA_SERVER_OPERATION_CREATE_TABLE:
                sql = gda_mysql_render_CREATE_TABLE (provider, cnc, op, error);
                break;
        case GDA_SERVER_OPERATION_DROP_TABLE:
		sql = gda_mysql_render_DROP_TABLE (provider, cnc, op, error);
		break;
        case GDA_SERVER_OPERATION_RENAME_TABLE:
		sql = gda_mysql_render_RENAME_TABLE (provider, cnc, op, error);
		break;
        case GDA_SERVER_OPERATION_COMMENT_TABLE:
		sql = gda_mysql_render_COMMENT_TABLE (provider, cnc, op, error);
		break;
        case GDA_SERVER_OPERATION_ADD_COLUMN:
		sql = gda_mysql_render_ADD_COLUMN (provider, cnc, op, error);
		break;
	case GDA_SERVER_OPERATION_DROP_COLUMN:
		sql = gda_mysql_render_DROP_COLUMN (provider, cnc, op, error);
		break;
	case GDA_SERVER_OPERATION_COMMENT_COLUMN:
		sql = gda_mysql_render_COMMENT_COLUMN (provider, cnc, op, error);
		break;
        case GDA_SERVER_OPERATION_CREATE_INDEX:
		sql = gda_mysql_render_CREATE_INDEX (provider, cnc, op, error);
		break;
        case GDA_SERVER_OPERATION_DROP_INDEX:
		sql = gda_mysql_render_DROP_INDEX (provider, cnc, op, error);
		break;
        case GDA_SERVER_OPERATION_CREATE_VIEW:
		sql = gda_mysql_render_CREATE_VIEW (provider, cnc, op, error);
		break;
        case GDA_SERVER_OPERATION_DROP_VIEW:
		sql = gda_mysql_render_DROP_VIEW (provider, cnc, op, error);
		break;
        default:
                g_assert_not_reached ();
        }
        return sql;
}

/*
 * Perform operation request
 */
static gboolean
gda_mysql_provider_perform_operation (GdaServerProvider               *provider,
				      GdaConnection                   *cnc,
				      GdaServerOperation              *op,
				      G_GNUC_UNUSED guint                           *task_id, 
				      GdaServerProviderAsyncCallback   async_cb,
				      gpointer                         cb_data,
				      GError                         **error)
{
        GdaServerOperationType optype;

	/* If asynchronous connection opening is not supported, then exit now */
	if (async_cb) {
		g_set_error (error, GDA_SERVER_PROVIDER_ERROR, GDA_SERVER_PROVIDER_METHOD_NON_IMPLEMENTED_ERROR,
			     "%s", _("Provider does not support asynchronous server operation"));
                return FALSE;
	}

	if (cnc) {
		g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
		g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	}
        optype = gda_server_operation_get_op_type (op);
	if (!cnc &&
	    ((optype == GDA_SERVER_OPERATION_CREATE_DB) ||
	     (optype == GDA_SERVER_OPERATION_DROP_DB))) {
		const GValue *value;
		MYSQL *mysql;
		const gchar *login = NULL;
		const gchar *password = NULL;
		const gchar *host = NULL;
		gint         port = -1;
		const gchar *socket = NULL;
		gboolean     usessl = FALSE;
		const gchar *proto = NULL;

		value = gda_server_operation_get_value_at (op, "/SERVER_CNX_P/HOST");
		if (value && G_VALUE_HOLDS (value, G_TYPE_STRING) && g_value_get_string (value))
			host = g_value_get_string (value);
		
		value = gda_server_operation_get_value_at (op, "/SERVER_CNX_P/PORT");
		if (value && G_VALUE_HOLDS (value, G_TYPE_INT) && (g_value_get_int (value) > 0))
			port = g_value_get_int (value);

		value = gda_server_operation_get_value_at (op, "/SERVER_CNX_P/UNIX_SOCKET");
		if (value && G_VALUE_HOLDS (value, G_TYPE_STRING) && g_value_get_string (value))
			socket = g_value_get_string (value);

		value = gda_server_operation_get_value_at (op, "/SERVER_CNX_P/USE_SSL");
		if (value && G_VALUE_HOLDS (value, G_TYPE_BOOLEAN) && g_value_get_boolean (value))
			usessl = TRUE;

		value = gda_server_operation_get_value_at (op, "/SERVER_CNX_P/ADM_LOGIN");
		if (value && G_VALUE_HOLDS (value, G_TYPE_STRING) && g_value_get_string (value))
			login = g_value_get_string (value);

		value = gda_server_operation_get_value_at (op, "/SERVER_CNX_P/ADM_PASSWORD");
		if (value && G_VALUE_HOLDS (value, G_TYPE_STRING) && g_value_get_string (value))
			password = g_value_get_string (value);

		value = gda_server_operation_get_value_at (op, "/SERVER_CNX_P/PROTO");
		if (value && G_VALUE_HOLDS (value, G_TYPE_STRING) && g_value_get_string (value))
			proto = g_value_get_string (value);

		mysql = real_open_connection (host, port, socket,
                                              "mysql", login, password, usessl, FALSE, proto, error);
                if (!mysql)
                        return FALSE;
		else {
			gchar *sql;
			int res;
			
			sql = gda_server_provider_render_operation (provider, cnc, op, error);
			if (!sql)
				return FALSE;

			res = mysql_query (mysql, sql);
			g_free (sql);
			
			if (res) {
			  g_set_error (error, GDA_SERVER_PROVIDER_ERROR,
				       GDA_SERVER_PROVIDER_OPERATION_ERROR,
				       "%s", mysql_error (mysql));
				mysql_close (mysql);
				return FALSE;
			}
			else {
				mysql_close (mysql);
				return TRUE;
			}
		}
	}
	else {
		/* use the SQL from the provider to perform the action */
		return gda_server_provider_perform_operation_default (provider, cnc, op, error);
	}
}

/*
 * Begin transaction request
 */
static gboolean
gda_mysql_provider_begin_transaction (GdaServerProvider        *provider,
				      GdaConnection            *cnc,
				      G_GNUC_UNUSED const gchar              *name,
				      GdaTransactionIsolation   level,
				      GError                  **error)
{
	MysqlConnectionData *cdata;
	gint rc;
	GdaConnectionEvent *event = NULL;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	/* set isolation level */
        switch (level) {
        case GDA_TRANSACTION_ISOLATION_READ_COMMITTED :
                rc = gda_mysql_real_query_wrap (cnc, cdata->mysql, "SET TRANSACTION ISOLATION LEVEL READ COMMITTED", 46);
                break;
        case GDA_TRANSACTION_ISOLATION_READ_UNCOMMITTED :
                rc = gda_mysql_real_query_wrap (cnc, cdata->mysql, "SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED", 48);
                break;
        case GDA_TRANSACTION_ISOLATION_REPEATABLE_READ :
                rc = gda_mysql_real_query_wrap (cnc, cdata->mysql, "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ", 47);
                break;
        case GDA_TRANSACTION_ISOLATION_SERIALIZABLE :
                rc = gda_mysql_real_query_wrap (cnc, cdata->mysql, "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE", 44);
                break;
        default :
                rc = 0;
        }

	if (rc != 0)
                event = _gda_mysql_make_error (cnc, cdata->mysql, NULL, error);
        else {
                /* start the transaction */
                rc = gda_mysql_real_query_wrap (cnc, cdata->mysql, "BEGIN", 5);
                if (rc != 0)
                        event = _gda_mysql_make_error (cnc, cdata->mysql, NULL, error);
        }
	
	if (event)
		return FALSE;
	else {
		gda_connection_internal_transaction_started (cnc, NULL, NULL, level);
		return TRUE;
	}
}

/*
 * Commit transaction request
 */
static gboolean
gda_mysql_provider_commit_transaction (GdaServerProvider  *provider,
				       GdaConnection      *cnc,
				       G_GNUC_UNUSED const gchar        *name,
				       GError            **error)
{
	MysqlConnectionData *cdata;
	gint rc;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	rc = gda_mysql_real_query_wrap (cnc, cdata->mysql, "COMMIT", 6);
	if (rc != 0) {
		_gda_mysql_make_error (cnc, cdata->mysql, NULL, error);
		return FALSE;
	}
	else {
		gda_connection_internal_transaction_committed (cnc, NULL);
		return TRUE;
	}
}

/*
 * Rollback transaction request
 */
static gboolean
gda_mysql_provider_rollback_transaction (GdaServerProvider  *provider,
					 GdaConnection      *cnc,
					 G_GNUC_UNUSED const gchar        *name,
					 GError            **error)
{
	MysqlConnectionData *cdata;
	gint rc;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	rc = gda_mysql_real_query_wrap (cnc, cdata->mysql, "ROLLBACK", 8);
	if (rc != 0) {
		_gda_mysql_make_error (cnc, cdata->mysql, NULL, error);
		return FALSE;
	}
	else {
		gda_connection_internal_transaction_rolledback (cnc, NULL);
		return TRUE;
	}
}

/*
 * Add savepoint request
 */
static gboolean
gda_mysql_provider_add_savepoint (GdaServerProvider  *provider,
				  GdaConnection      *cnc,
				  G_GNUC_UNUSED const gchar        *name,
				  G_GNUC_UNUSED GError            **error)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	TO_IMPLEMENT;

	return FALSE;
}

/*
 * Rollback savepoint request
 */
static gboolean
gda_mysql_provider_rollback_savepoint (GdaServerProvider  *provider,
				       GdaConnection      *cnc,
				       G_GNUC_UNUSED const gchar        *name,
				       G_GNUC_UNUSED GError            **error)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	TO_IMPLEMENT;

	return FALSE;
}

/*
 * Delete savepoint request
 */
static gboolean
gda_mysql_provider_delete_savepoint (GdaServerProvider  *provider,
				     GdaConnection      *cnc,
				     G_GNUC_UNUSED const gchar        *name,
				     G_GNUC_UNUSED GError            **error)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	TO_IMPLEMENT;

	return FALSE;
}

/*
 * Feature support request
 */
static gboolean
gda_mysql_provider_supports_feature (GdaServerProvider     *provider,
				     GdaConnection         *cnc,
				     GdaConnectionFeature   feature)
{
	if (cnc) {
		g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
		g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	}

	switch (feature) {
	case GDA_CONNECTION_FEATURE_SQL :
		return TRUE;
	case GDA_CONNECTION_FEATURE_MULTI_THREADING:
		return mysql_thread_safe () ? TRUE : FALSE;
	default: 
		return FALSE;
	}
}

/*
 * Get data handler request
 *
 * This method allows one to obtain a pointer to a #GdaDataHandler object specific to @type or @dbms_type (@dbms_type
 * must be considered only if @type is not a valid GType).
 *
 * A data handler allows one to convert a value between its different representations which are a human readable string,
 * an SQL representation and a GValue.
 *
 * The recommended method is to create GdaDataHandler objects only when they are needed and to keep a reference to them
 * for further usage, using the gda_server_provider_handler_declare() method.
 *
 * The implementation shown here does not define any specific data handler, but there should be some for at least 
 * binary and time related types.
 */
static GdaDataHandler *
gda_mysql_provider_get_data_handler (GdaServerProvider  *provider,
				     GdaConnection      *cnc,
				     GType               type,
				     const gchar        *dbms_type)
{
	GdaDataHandler *dh;
	if (cnc) {
		g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
		g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	}

	if (type == G_TYPE_INVALID) {
		TO_IMPLEMENT; /* use @dbms_type */
		dh = NULL;
	}
	else if (type == GDA_TYPE_BINARY) {
		dh = gda_server_provider_handler_find (provider, cnc, type, NULL);
                if (!dh) {
			dh = _gda_mysql_handler_bin_new ();
                        gda_server_provider_handler_declare (provider, dh, NULL, GDA_TYPE_BINARY, NULL);
                        g_object_unref (dh);
                }
	}
	else if ((type == GDA_TYPE_TIME) ||
		 (type == GDA_TYPE_TIMESTAMP) ||
		 (type == G_TYPE_DATE)) {
		dh = gda_server_provider_handler_find (provider, NULL, type, NULL);
                if (!dh) {
                        dh = gda_handler_time_new ();
                        gda_handler_time_set_sql_spec ((GdaHandlerTime *) dh, G_DATE_YEAR,
                                                       G_DATE_MONTH, G_DATE_DAY, '-', FALSE);
                        gda_server_provider_handler_declare (provider, dh, NULL, G_TYPE_DATE, NULL);
                        gda_server_provider_handler_declare (provider, dh, NULL, GDA_TYPE_TIME, NULL);
                        gda_server_provider_handler_declare (provider, dh, NULL, GDA_TYPE_TIMESTAMP, NULL);
                        g_object_unref (dh);
                }
	}
	else if (type == G_TYPE_BOOLEAN){
		dh = gda_server_provider_handler_find (provider, cnc, type, NULL);
                if (!dh) {
                        dh = gda_mysql_handler_boolean_new ();
                        if (dh) {
                                gda_server_provider_handler_declare (provider, dh, cnc, G_TYPE_BOOLEAN, NULL);
                                g_object_unref (dh);
                        }
                }
	}
	else
		dh = gda_server_provider_handler_use_default (provider, type);

	return dh;
}

/*
 * Get default DBMS type request
 *
 * This method returns the "preferred" DBMS type for GType
 */
static const gchar*
gda_mysql_provider_get_default_dbms_type (GdaServerProvider  *provider,
					  GdaConnection      *cnc,
					  GType               type)
{
	if (cnc) {
		g_return_val_if_fail (GDA_IS_CONNECTION (cnc), NULL);
		g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, NULL);
	}

	if (type == G_TYPE_INT64)
		return "bigint";
	if (type == G_TYPE_UINT64)
		return "bigint unsigned";
	if (type == GDA_TYPE_BINARY)
		return "varbinary";
	if (type == GDA_TYPE_BLOB)
		return "blob";
	if (type == G_TYPE_BOOLEAN)
		return "bool";
	if (type == G_TYPE_DATE)
		return "date";
	if (type == G_TYPE_DOUBLE)
		return "double";
	if (type == GDA_TYPE_GEOMETRIC_POINT)
		return "point";
	if (type == G_TYPE_OBJECT)
		return "text";
	if (type == G_TYPE_INT)
		return "int";
	if (type == GDA_TYPE_NUMERIC)
		return "numeric";
	if (type == G_TYPE_FLOAT)
		return "float";
	if (type == GDA_TYPE_SHORT)
		return "smallint";
	if (type == GDA_TYPE_USHORT)
		return "smallint unsigned";
	if (type == G_TYPE_STRING)
		return "varchar";
	if (type == GDA_TYPE_TIME)
		return "time";
	if (type == GDA_TYPE_TIMESTAMP)
		return "timestamp";
	if (type == G_TYPE_CHAR)
		return "tinyint";
	if (type == G_TYPE_UCHAR)
		return "tinyint unsigned";
	if (type == G_TYPE_ULONG)
		return "bigint unsigned";
	if (type == G_TYPE_UINT)
		return "int unsigned";

	if ((type == GDA_TYPE_NULL) ||
	    (type == G_TYPE_GTYPE))
		return NULL;

	return "text";
}

/*
 * Create parser request
 *
 * This method is responsible for creating a #GdaSqlParser object specific to the SQL dialect used
 * by the database. See the PostgreSQL provider implementation for an example.
 */
static GdaSqlParser *
gda_mysql_provider_create_parser (G_GNUC_UNUSED GdaServerProvider  *provider,
				  G_GNUC_UNUSED GdaConnection      *cnc)
{
	return (GdaSqlParser *) g_object_new (GDA_TYPE_MYSQL_PARSER,
					      "tokenizer-flavour", GDA_SQL_PARSER_FLAVOUR_MYSQL,
					      NULL);
}

/*
 * GdaStatement to SQL request
 * 
 * This method renders a #GdaStatement into its SQL representation.
 */
static gchar *mysql_render_insert (GdaSqlStatementInsert *stmt, GdaSqlRenderingContext *context, GError **error);
static gchar *mysql_render_function (GdaSqlFunction *func, GdaSqlRenderingContext *context, GError **error);
static gchar *mysql_render_expr (GdaSqlExpr *expr, GdaSqlRenderingContext *context, 
				 gboolean *is_default, gboolean *is_null,
				 GError **error);
static gchar *
gda_mysql_provider_statement_to_sql (GdaServerProvider    *provider,
				     GdaConnection        *cnc,
				     GdaStatement         *stmt,
				     GdaSet               *params,
				     GdaStatementSqlFlag   flags,
				     GSList              **params_used,
				     GError              **error)
{
	gchar *str;
        GdaSqlRenderingContext context;

        g_return_val_if_fail (GDA_IS_STATEMENT (stmt), NULL);
        if (cnc) {
                g_return_val_if_fail (GDA_IS_CONNECTION (cnc), NULL);
                g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, NULL);
        }

        memset (&context, 0, sizeof (context));
	context.provider = provider;
	context.cnc = cnc;
        context.params = params;
        context.flags = flags;
	context.render_function = (GdaSqlRenderingFunc) mysql_render_function; /* don't put any space between function name
										* and the opening parenthesis, see
										* http://blog.152.org/2009/12/mysql-error-1305-function-xxx-does-not.html */
	context.render_expr = mysql_render_expr; /* render "FALSE" as 0 and TRUE as 1 */
	context.render_insert = (GdaSqlRenderingFunc) mysql_render_insert;

        str = gda_statement_to_sql_real (stmt, &context, error);

        if (str) {
                if (params_used)
                        *params_used = context.params_used;
                else
                        g_slist_free (context.params_used);
        }
        else {
                if (params_used)
                        *params_used = NULL;
                g_slist_free (context.params_used);
        }
        return str;
}

static gchar *
mysql_render_insert (GdaSqlStatementInsert *stmt, GdaSqlRenderingContext *context, GError **error)
{
	GString *string;
	gchar *str;
	GSList *list;
	gboolean pretty = context->flags & GDA_STATEMENT_SQL_PRETTY;

	g_return_val_if_fail (stmt, NULL);
	g_return_val_if_fail (GDA_SQL_ANY_PART (stmt)->type == GDA_SQL_ANY_STMT_INSERT, NULL);

	string = g_string_new ("INSERT ");
	
	/* conflict algo */
	if (stmt->on_conflict)
		g_string_append_printf (string, "OR %s ", stmt->on_conflict);

	/* INTO */
	g_string_append (string, "INTO ");
	str = context->render_table (GDA_SQL_ANY_PART (stmt->table), context, error);
	if (!str) goto err;
	g_string_append (string, str);
	g_free (str);

	/* fields list */
	for (list = stmt->fields_list; list; list = list->next) {
		if (list == stmt->fields_list)
			g_string_append (string, " (");
		else
			g_string_append (string, ", ");
		str = context->render_field (GDA_SQL_ANY_PART (list->data), context, error);
		if (!str) goto err;
		g_string_append (string, str);
		g_free (str);
	}
	if (stmt->fields_list)
		g_string_append_c (string, ')');

	/* values */
	if (stmt->select) {
		if (pretty)
			g_string_append_c (string, '\n');
		else
			g_string_append_c (string, ' ');
		str = context->render_select (GDA_SQL_ANY_PART (stmt->select), context, error);
		if (!str) goto err;
		g_string_append (string, str);
			g_free (str);
	}
	else {
		for (list = stmt->values_list; list; list = list->next) {
			GSList *rlist;
			if (list == stmt->values_list) {
				if (pretty)
					g_string_append (string, "\nVALUES");
				else
					g_string_append (string, " VALUES");
			}
			else
				g_string_append_c (string, ',');
			for (rlist = (GSList*) list->data; rlist; rlist = rlist->next) {
				if (rlist == (GSList*) list->data)
					g_string_append (string, " (");
				else
					g_string_append (string, ", ");
				str = context->render_expr ((GdaSqlExpr*) rlist->data, context, NULL, NULL, error);
				if (!str) goto err;
				if (pretty && (rlist != (GSList*) list->data))
					g_string_append (string, "\n\t");
				g_string_append (string, str);
				g_free (str);
			}
			g_string_append_c (string, ')');
		}

		if (!stmt->fields_list && !stmt->values_list)
			g_string_append (string, " () VALUES ()");
	}

	str = string->str;
	g_string_free (string, FALSE);
	return str;

 err:
	g_string_free (string, TRUE);
	return NULL;	
}

static gchar *
mysql_render_function (GdaSqlFunction *func, GdaSqlRenderingContext *context, GError **error)
{
	GString *string;
	gchar *str;
	GSList *list;

	g_return_val_if_fail (func, NULL);
	g_return_val_if_fail (GDA_SQL_ANY_PART (func)->type == GDA_SQL_ANY_SQL_FUNCTION, NULL);

	/* can't have: func->function_name == NULL */
	if (!gda_sql_any_part_check_structure (GDA_SQL_ANY_PART (func), error)) return NULL;

	string = g_string_new (func->function_name);
	g_string_append_c (string, '(');
	for (list = func->args_list; list; list = list->next) {
		if (list != func->args_list)
			g_string_append (string, ", ");
		str = context->render_expr (list->data, context, NULL, NULL, error);
		if (!str) goto err;
		g_string_append (string, str);
		g_free (str);
	}
	g_string_append_c (string, ')');

	str = string->str;
	g_string_free (string, FALSE);
	return str;

 err:
	g_string_free (string, TRUE);
	return NULL;
}

/*
 * The difference with the default implementation is to render TRUE and FALSE as 0 and 1
 */
static gchar *
mysql_render_expr (GdaSqlExpr *expr, GdaSqlRenderingContext *context, gboolean *is_default,
		   gboolean *is_null, GError **error)
{
	GString *string;
	gchar *str = NULL;

	g_return_val_if_fail (expr, NULL);
	g_return_val_if_fail (GDA_SQL_ANY_PART (expr)->type == GDA_SQL_ANY_EXPR, NULL);

	if (is_default)
		*is_default = FALSE;
	if (is_null)
		*is_null = FALSE;

	/* can't have:
	 *  - expr->cast_as && expr->param_spec
	 */
	if (!gda_sql_any_part_check_structure (GDA_SQL_ANY_PART (expr), error)) return NULL;

	string = g_string_new ("");
	if (expr->param_spec) {
		str = context->render_param_spec (expr->param_spec, expr, context, is_default, is_null, error);
		if (!str) goto err;
	}
	else if (expr->value) {
		if (G_VALUE_TYPE (expr->value) == G_TYPE_STRING) {
			/* specific treatment for strings, see documentation about GdaSqlExpr's value attribute */
			const gchar *vstr;
			vstr = g_value_get_string (expr->value);
			if (vstr) {
				if (expr->value_is_ident) {
					gchar **ids_array;
					gint i;
					GString *string = NULL;
					GdaConnectionOptions cncoptions = 0;
					if (context->cnc)
						g_object_get (G_OBJECT (context->cnc), "options", &cncoptions, NULL);
					ids_array = gda_sql_identifier_split (vstr);
					if (!ids_array)
						str = g_strdup (vstr);
					else if (!(ids_array[0])) goto err;
					else {
						for (i = 0; ids_array[i]; i++) {
							gchar *tmp;
							if (!string)
								string = g_string_new ("");
							else
								g_string_append_c (string, '.');
							tmp = gda_sql_identifier_quote (ids_array[i], context->cnc,
											context->provider, FALSE,
											cncoptions & GDA_CONNECTION_OPTIONS_SQL_IDENTIFIERS_CASE_SENSITIVE);
							g_string_append (string, tmp);
							g_free (tmp);
						}
						g_strfreev (ids_array);
						str = g_string_free (string, FALSE);
					}
				}
				else {
					/* we don't have an identifier */
					if (!g_ascii_strcasecmp (vstr, "default")) {
						if (is_default)
							*is_default = TRUE;
						str = g_strdup ("DEFAULT");
					}
					else if (!g_ascii_strcasecmp (vstr, "FALSE")) {
						g_free (str);
						str = g_strdup ("0");
					}
					else if (!g_ascii_strcasecmp (vstr, "TRUE")) {
						g_free (str);
						str = g_strdup ("1");
					}
					else
						str = g_strdup (vstr);
				}
			}
			else {
				str = g_strdup ("NULL");
				if (is_null)
					*is_null = TRUE;
			}
		}
		if (!str) {
			/* use a GdaDataHandler to render the value as valid SQL */
			GdaDataHandler *dh;
			if (context->cnc) {
				GdaServerProvider *prov;
				prov = gda_connection_get_provider (context->cnc);
				dh = gda_server_provider_get_data_handler_g_type (prov, context->cnc,
										  G_VALUE_TYPE (expr->value));
				if (!dh) goto err;
			}
			else
				dh = gda_data_handler_get_default (G_VALUE_TYPE (expr->value));

			if (dh)
				str = gda_data_handler_get_sql_from_value (dh, expr->value);
			else
				str = gda_value_stringify (expr->value);
			if (!str) goto err;
		}
	}
	else if (expr->func) {
		str = context->render_function (GDA_SQL_ANY_PART (expr->func), context, error);
		if (!str) goto err;
	}
	else if (expr->cond) {
		gchar *tmp;
		tmp = context->render_operation (GDA_SQL_ANY_PART (expr->cond), context, error);
		if (!tmp) goto err;
		str = NULL;
		if (GDA_SQL_ANY_PART (expr)->parent) {
			if (GDA_SQL_ANY_PART (expr)->parent->type == GDA_SQL_ANY_STMT_SELECT) {
				GdaSqlStatementSelect *selst;
				selst = (GdaSqlStatementSelect*) (GDA_SQL_ANY_PART (expr)->parent);
				if ((expr == selst->where_cond) ||
				    (expr == selst->having_cond))
					str = tmp;
			}
			else if (GDA_SQL_ANY_PART (expr)->parent->type == GDA_SQL_ANY_STMT_DELETE) {
				GdaSqlStatementDelete *delst;
				delst = (GdaSqlStatementDelete*) (GDA_SQL_ANY_PART (expr)->parent);
				if (expr == delst->cond)
					str = tmp;
			}
			else if (GDA_SQL_ANY_PART (expr)->parent->type == GDA_SQL_ANY_STMT_UPDATE) {
				GdaSqlStatementUpdate *updst;
				updst = (GdaSqlStatementUpdate*) (GDA_SQL_ANY_PART (expr)->parent);
				if (expr == updst->cond)
					str = tmp;
			}
		}

		if (!str) {
			str = g_strconcat ("(", tmp, ")", NULL);
			g_free (tmp);
		}
	}
	else if (expr->select) {
		gchar *str1;
		if (GDA_SQL_ANY_PART (expr->select)->type == GDA_SQL_ANY_STMT_SELECT)
			str1 = context->render_select (GDA_SQL_ANY_PART (expr->select), context, error);
		else if (GDA_SQL_ANY_PART (expr->select)->type == GDA_SQL_ANY_STMT_COMPOUND)
			str1 = context->render_compound (GDA_SQL_ANY_PART (expr->select), context, error);
		else
			g_assert_not_reached ();
		if (!str1) goto err;

		if (! GDA_SQL_ANY_PART (expr)->parent ||
		    (GDA_SQL_ANY_PART (expr)->parent->type != GDA_SQL_ANY_SQL_FUNCTION)) {
			str = g_strconcat ("(", str1, ")", NULL);
			g_free (str1);
		}
		else
			str = str1;
	}
	else if (expr->case_s) {
		str = context->render_case (GDA_SQL_ANY_PART (expr->case_s), context, error);
		if (!str) goto err;
	}
	else {
		if (is_null)
			*is_null = TRUE;
		str = g_strdup ("NULL");
	}

	if (!str) goto err;

	if (expr->cast_as)
		g_string_append_printf (string, "CAST (%s AS %s)", str, expr->cast_as);
	else
		g_string_append (string, str);
	g_free (str);

	str = string->str;
	g_string_free (string, FALSE);
	return str;

 err:
	g_string_free (string, TRUE);
	return NULL;
}

static GdaMysqlPStmt *
real_prepare (GdaServerProvider *provider, GdaConnection *cnc, GdaStatement *stmt, GError **error)
{
	GdaMysqlPStmt *ps = NULL;
	MysqlConnectionData *cdata;

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return NULL;

	/* Render as SQL understood by Mysql. */
	GdaSet *set;
	if (!gda_statement_get_parameters (stmt, &set, error))
		return NULL;

	GSList *used_set = NULL;
	gchar *sql = gda_mysql_provider_statement_to_sql (provider, cnc, stmt, set,
							  GDA_STATEMENT_SQL_PARAMS_AS_UQMARK, &used_set, error);

	if (!sql)
		goto cleanup;

	MYSQL_STMT *mysql_stmt = mysql_stmt_init (cdata->mysql);
	if (!mysql_stmt) {
		_gda_mysql_make_error (cnc, NULL, mysql_stmt, error);
		return FALSE;
	}

	my_bool update_max_length = 1;
	if (mysql_stmt_attr_set (mysql_stmt, STMT_ATTR_UPDATE_MAX_LENGTH, (const void *) &update_max_length)) {
		_gda_mysql_make_error (cnc, NULL, mysql_stmt, error);
		mysql_stmt_close (mysql_stmt);
		return FALSE;
	}

	if (mysql_stmt_prepare (mysql_stmt, sql, strlen (sql))) {
		_gda_mysql_make_error (cdata->cnc, NULL, mysql_stmt, error);
		mysql_stmt_close (mysql_stmt);
		goto cleanup;
	}

	GSList *param_ids = NULL, *current;
	for (current = used_set; current; current = current->next) {
		const gchar *id = gda_holder_get_id
			(GDA_HOLDER(current->data));
		if (id) {
			param_ids = g_slist_append (param_ids, g_strdup (id));
		}
		else {
			g_set_error (error, GDA_SERVER_PROVIDER_ERROR,
				     GDA_SERVER_PROVIDER_PREPARE_STMT_ERROR,
				     "%s", _("Unnamed statement parameter is not allowed in prepared statement."));
			g_slist_foreach (param_ids, (GFunc) g_free, NULL);
			g_slist_free (param_ids);
			param_ids = NULL;
			mysql_stmt_close (mysql_stmt);
			goto cleanup;
		}
	}

	/* Create prepared statement. */
	ps = gda_mysql_pstmt_new (cnc, cdata->mysql, mysql_stmt);
	
	if (!ps)
		return NULL;
	else {
		gda_pstmt_set_gda_statement (_GDA_PSTMT(ps), stmt);
		_GDA_PSTMT(ps)->param_ids = param_ids;
		_GDA_PSTMT(ps)->sql = sql;
		return ps;
	}
	
 cleanup:

	if (set)
		g_object_unref (G_OBJECT(set));
	if (used_set)
		g_slist_free (used_set);
	g_free (sql);
	
	return NULL;
}

/*
 * Statement prepare request
 *
 * This methods "converts" @stmt into a prepared statement. A prepared statement is a notion
 * specific in its implementation details to the C API used here. If successfull, it must create
 * a new #GdaMysqlPStmt object and declare it to @cnc.
 */
static gboolean
gda_mysql_provider_statement_prepare (GdaServerProvider  *provider,
				      GdaConnection      *cnc,
				      GdaStatement       *stmt,
				      GError            **error)
{
	GdaMysqlPStmt *ps;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	g_return_val_if_fail (GDA_IS_STATEMENT (stmt), FALSE);

	/* fetch prepares stmt if already done */
	ps = (GdaMysqlPStmt*) gda_connection_get_prepared_statement (cnc, stmt);
	if (ps)
		return TRUE;

	ps = real_prepare (provider, cnc, stmt, error);
        if (!ps)
                return FALSE;
        else {
                gda_connection_add_prepared_statement (cnc, stmt, (GdaPStmt *) ps);
                g_object_unref (ps);
                return TRUE;
        }
}


static GdaMysqlPStmt *
prepare_stmt_simple (MysqlConnectionData  *cdata,
		     const gchar          *sql,
		     GError              **error,
		     gboolean *out_protocol_error)
{
	GdaMysqlPStmt *ps = NULL;
	*out_protocol_error = FALSE;
	g_return_val_if_fail (sql != NULL, NULL);

	MYSQL_STMT *mysql_stmt = mysql_stmt_init (cdata->mysql);
	if (!mysql_stmt) {
		_gda_mysql_make_error (cdata->cnc, NULL, mysql_stmt, error);
		return FALSE;
	}

	my_bool update_max_length = 1;
	if (mysql_stmt_attr_set (mysql_stmt, STMT_ATTR_UPDATE_MAX_LENGTH, (const void *) &update_max_length)) {
		_gda_mysql_make_error (cdata->cnc, NULL, mysql_stmt, error);
		mysql_stmt_close (mysql_stmt);
		return FALSE;
	}
	
	if (mysql_stmt_prepare (mysql_stmt, sql, strlen (sql))) {
		_gda_mysql_make_error (cdata->cnc, NULL, mysql_stmt, error);
		if (mysql_stmt_errno (mysql_stmt) == ER_UNSUPPORTED_PS)
			*out_protocol_error = TRUE;
		mysql_stmt_close (mysql_stmt);
		ps = NULL;
	}
	else {
		ps = gda_mysql_pstmt_new (cdata->cnc, cdata->mysql, mysql_stmt);
		_GDA_PSTMT(ps)->param_ids = NULL;
		_GDA_PSTMT(ps)->sql = g_strdup (sql);
	}
	
	return ps;
}

static GdaSet *
make_last_inserted_set (GdaConnection *cnc, GdaStatement *stmt, my_ulonglong last_id)
{
	GError *lerror = NULL;

	/* analyze @stmt */
	GdaSqlStatement *sql_insert;
	GdaSqlStatementInsert *insert;
	if (gda_statement_get_statement_type (stmt) != GDA_SQL_STATEMENT_INSERT)
		/* unable to compute anything */
		return NULL;

	g_object_get (G_OBJECT (stmt), "structure", &sql_insert, NULL);
	g_assert (sql_insert);
	insert = (GdaSqlStatementInsert *) sql_insert->contents;

	/* find the name of the 1st column which has the AUTO_INCREMENT */
	gchar *autoinc_colname = NULL;
	gint rc;
	gchar *sql;
	MysqlConnectionData *cdata;
	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data (cnc);
	if (!cdata) 
		return NULL;
	sql = g_strdup_printf ("DESCRIBE %s", insert->table->table_name);
	rc = gda_mysql_real_query_wrap (cnc, cdata->mysql, sql, strlen (sql));
	g_free (sql);
	if (!rc) {
		MYSQL_RES *result;
		result = mysql_store_result (cdata->mysql);
		if (result) {
			MYSQL_ROW row;
			unsigned int nfields;
			nfields = mysql_num_fields (result);
			while ((row = mysql_fetch_row(result))) {
				if (row [nfields-1] &&
				    !g_ascii_strcasecmp (row [nfields-1], "auto_increment")) {
					autoinc_colname = g_strdup (row [0]);
					break;
				}
			}
			mysql_free_result (result);
		}
	}
	if (! autoinc_colname) {
		gda_sql_statement_free (sql_insert);
		return NULL;
	}

	/* build corresponding SELECT statement */
	GdaSqlStatementSelect *select;
        GdaSqlSelectTarget *target;
        GdaSqlStatement *sql_statement = gda_sql_statement_new (GDA_SQL_STATEMENT_SELECT);

	select = g_new0 (GdaSqlStatementSelect, 1);
        GDA_SQL_ANY_PART (select)->type = GDA_SQL_ANY_STMT_SELECT;
        sql_statement->contents = select;

	/* FROM */
        select->from = gda_sql_select_from_new (GDA_SQL_ANY_PART (select));
        target = gda_sql_select_target_new (GDA_SQL_ANY_PART (select->from));
        gda_sql_select_from_take_new_target (select->from, target);

	/* Filling in the target */
        GValue *value;
        g_value_set_string ((value = gda_value_new (G_TYPE_STRING)), insert->table->table_name);
        gda_sql_select_target_take_table_name (target, value);
	gda_sql_statement_free (sql_insert);

	/* selected fields */
        GdaSqlSelectField *field;
        GSList *fields_list = NULL;

	field = gda_sql_select_field_new (GDA_SQL_ANY_PART (select));
	g_value_set_string ((value = gda_value_new (G_TYPE_STRING)), "*");
	gda_sql_select_field_take_star_value (field, value);
	fields_list = g_slist_append (fields_list, field);

	gda_sql_statement_select_take_expr_list (sql_statement, fields_list);

	/* WHERE */
        GdaSqlExpr *where, *expr;
	GdaSqlOperation *cond;
	where = gda_sql_expr_new (GDA_SQL_ANY_PART (select));
	cond = gda_sql_operation_new (GDA_SQL_ANY_PART (where));
	where->cond = cond;
	cond->operator_type = GDA_SQL_OPERATOR_TYPE_EQ;
	expr = gda_sql_expr_new (GDA_SQL_ANY_PART (cond));
	g_value_take_string ((value = gda_value_new (G_TYPE_STRING)), autoinc_colname);
	expr->value = value;
	cond->operands = g_slist_append (NULL, expr);
	gchar *str;
	str = g_strdup_printf ("%llu", last_id);
	expr = gda_sql_expr_new (GDA_SQL_ANY_PART (cond));
	g_value_take_string ((value = gda_value_new (G_TYPE_STRING)), str);
	expr->value = value;
	cond->operands = g_slist_append (cond->operands, expr);

	gda_sql_statement_select_take_where_cond (sql_statement, where);

	if (gda_sql_statement_check_structure (sql_statement, &lerror) == FALSE) {
                g_warning (_("Can't build SELECT statement to get last inserted row: %s"),
			     lerror && lerror->message ? lerror->message : _("No detail"));
		if (lerror)
			g_error_free (lerror);
                gda_sql_statement_free (sql_statement);
                return NULL;
        }

	/* execute SELECT statement */
	GdaDataModel *model;
	GdaStatement *statement = g_object_new (GDA_TYPE_STATEMENT, "structure", sql_statement, NULL);
        gda_sql_statement_free (sql_statement);
        model = gda_connection_statement_execute_select (cnc, statement, NULL, &lerror);
	g_object_unref (statement);
        if (!model) {
                g_warning (_("Can't execute SELECT statement to get last inserted row: %s"),
			   lerror && lerror->message ? lerror->message : _("No detail"));
		if (lerror)
			g_error_free (lerror);
		return NULL;
        }
	else {
		GdaSet *set = NULL;
		GSList *holders = NULL;
		gint nrows, ncols, i;

		nrows = gda_data_model_get_n_rows (model);
		if (nrows <= 0) {
			g_warning (_("SELECT statement to get last inserted row did not return any row"));
			return NULL;
		}
		else if (nrows > 1) {
			g_warning (_("SELECT statement to get last inserted row returned too many (%d) rows"),
				   nrows);
			return NULL;
		}
		ncols = gda_data_model_get_n_columns (model);
		for (i = 0; i < ncols; i++) {
			GdaHolder *h;
			GdaColumn *col;
			gchar *id;
			const GValue *cvalue;

			col = gda_data_model_describe_column (model, i);
			h = gda_holder_new (gda_column_get_g_type (col));
			id = g_strdup_printf ("+%d", i);
			g_object_set (G_OBJECT (h), "id", id, "not-null", FALSE,
				      "name", gda_column_get_name (col), NULL);
			g_free (id);
			cvalue = gda_data_model_get_value_at (model, i, 0, NULL);
			if (!cvalue || !gda_holder_set_value (h, cvalue, NULL)) {
				if (holders) {
					g_slist_foreach (holders, (GFunc) g_object_unref, NULL);
					g_slist_free (holders);
					holders = NULL;
				}
				break;
			}
			holders = g_slist_prepend (holders, h);
		}
		g_object_unref (model);

		if (holders) {
			holders = g_slist_reverse (holders);
			set = gda_set_new (holders);
			g_slist_foreach (holders, (GFunc) g_object_unref, NULL);
			g_slist_free (holders);
		}

		return set;
	}
}

static void
free_bind_param_data (GSList *mem_to_free)
{
	if (mem_to_free) {
		g_slist_foreach (mem_to_free, (GFunc) g_free, NULL);
		g_slist_free (mem_to_free);
	}
}

/*
 * Execute statement request
 *
 * Executes a statement. This method should do the following:
 *    - try to prepare the statement if not yet done
 *    - optionnally reset the prepared statement
 *    - bind the variables's values (which are in @params)
 *    - add a connection event to log the execution
 *    - execute the prepared statement
 *
 * If @stmt is an INSERT statement and @last_inserted_row is not NULL then additional actions must be taken to return the
 * actual inserted row
 */
static GObject *
gda_mysql_provider_statement_execute (GdaServerProvider               *provider,
				      GdaConnection                   *cnc,
				      GdaStatement                    *stmt,
				      GdaSet                          *params,
				      GdaStatementModelUsage           model_usage, 
				      GType                           *col_types,
				      GdaSet                         **last_inserted_row, 
				      guint                           *task_id, 
				      GdaServerProviderExecCallback    async_cb,
				      gpointer                         cb_data,
				      GError                         **error)
{
	GdaMysqlPStmt *ps;
	MysqlConnectionData *cdata;
	gboolean allow_noparam;
        gboolean empty_rs = FALSE; /* TRUE when @allow_noparam is TRUE and there is a problem with @params
                                      => resulting data model will be empty (0 row) */

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), NULL);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, NULL);
	g_return_val_if_fail (GDA_IS_STATEMENT (stmt), NULL);

	/* If asynchronous connection opening is not supported, then exit now */
	if (async_cb) {
		g_set_error (error, GDA_SERVER_PROVIDER_ERROR, GDA_SERVER_PROVIDER_METHOD_NON_IMPLEMENTED_ERROR,
			     "%s", _("Provider does not support asynchronous statement execution"));
                return FALSE;
	}

	allow_noparam = (model_usage & GDA_STATEMENT_MODEL_ALLOW_NOPARAM) &&
		(gda_statement_get_statement_type (stmt) == GDA_SQL_STATEMENT_SELECT);

	if (last_inserted_row)
		*last_inserted_row = NULL;

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	/* get/create new prepared statement */
	ps = (GdaMysqlPStmt *) gda_connection_get_prepared_statement (cnc, stmt);
	if (!ps) {
		if (!gda_mysql_provider_statement_prepare (provider, cnc, stmt, NULL)) {
			/* this case can appear for example if some variables are used in places
			 * where the C API cannot allow them (for example if the variable is the table name
			 * in a SELECT statement). The action here is to get the actual SQL code for @stmt,
			 * and use that SQL instead of @stmt to create another GdaMysqlPStmt object.
			 */
			gchar *sql = gda_mysql_provider_statement_to_sql (provider, cnc, stmt, 
									  params, GDA_STATEMENT_SQL_TIMEZONE_TO_GMT, NULL, error);
			gboolean proto_error;
			if (!sql)
				return NULL;
			ps = prepare_stmt_simple (cdata, sql, error, &proto_error);
			if (!ps) {
				if (proto_error) {
					/* MySQL's "command is not supported in the prepared
					 * statement protocol yet" error
					 * => try to execute the SQL using mysql_real_query() */
					GdaConnectionEvent *event;
					if (mysql_real_query (cdata->mysql, sql, strlen (sql))) {
						event = _gda_mysql_make_error (cnc, cdata->mysql, NULL, error);
						gda_connection_add_event (cnc, event);
						g_free (sql);
						return NULL;
					}

					g_free (sql);
					my_ulonglong affected_rows;
					/* Create a #GdaSet containing "IMPACTED_ROWS" */
					/* Create GdaConnectionEvent notice with the type of command and impacted rows */
					affected_rows = mysql_affected_rows (cdata->mysql);
					if (affected_rows == (my_ulonglong)~0) {
						GdaDataModelAccessFlags flags;
						
						if (model_usage & GDA_STATEMENT_MODEL_RANDOM_ACCESS)
							flags = GDA_DATA_MODEL_ACCESS_RANDOM;
						else
							flags = GDA_DATA_MODEL_ACCESS_CURSOR_FORWARD;
						gda_connection_internal_statement_executed (cnc, stmt,
											    NULL, NULL); /* required: help @cnc keep some stats */

						return (GObject *) gda_mysql_recordset_new_direct (cnc,
												   flags,
												   col_types);
					}
					else {
						gchar *str;
						event = gda_connection_point_available_event (cnc, GDA_CONNECTION_EVENT_NOTICE);
						str = g_strdup_printf ("%llu", affected_rows);
						gda_connection_event_set_description (event, str);
						g_free (str);
						
						gda_connection_internal_statement_executed (cnc, stmt, params, event); /* required: help @cnc keep some stats */
						return (GObject *) gda_set_new_inline
							(1, "IMPACTED_ROWS", G_TYPE_INT, (int) affected_rows);
					}
				}
				else {
					g_free (sql);
					return NULL;
				}
			}
			g_free (sql);
		}
		else {
			ps = (GdaMysqlPStmt *) gda_connection_get_prepared_statement (cnc, stmt);
			g_object_ref (ps);
		}
	}
	else
		g_object_ref (ps);

	g_assert (ps);
	if (ps->stmt_used) {
                /* Don't use @ps => prepare stmt again */
                GdaMysqlPStmt *nps;
                nps = real_prepare (provider, cnc, stmt, error);
                if (!nps)
                        return NULL;
                //gda_pstmt_copy_contents ((GdaPStmt *) ps, (GdaPStmt *) nps);
		g_object_unref (ps);
		ps = nps;
	}

	/* bind statement's parameters */
	GSList *list;
	GdaConnectionEvent *event = NULL;
	int i;
	
	gint nb_params = g_slist_length (_GDA_PSTMT (ps)->param_ids);
	/*g_print ("NB=%d, SQL=%s\n", nb_params, _GDA_PSTMT(ps)->sql);*/

	MYSQL_BIND *mysql_bind_param = NULL;
	GSList *mem_to_free = NULL;

	if (nb_params > 0) {
		mysql_bind_param = g_new0 (MYSQL_BIND, nb_params);
		mem_to_free = g_slist_prepend (mem_to_free, mysql_bind_param);
	}

	for (i = 0, list = _GDA_PSTMT (ps)->param_ids; list; list = list->next, i++) {
		const gchar *pname = (gchar *) list->data;

		/* find requested parameter */
		if (!params) {
			event = gda_connection_point_available_event (cnc, GDA_CONNECTION_EVENT_ERROR);
			gda_connection_event_set_description (event, _("Missing parameter(s) to execute query"));
			g_set_error (error, GDA_SERVER_PROVIDER_ERROR,
				     GDA_SERVER_PROVIDER_MISSING_PARAM_ERROR,
				     "%s", _("Missing parameter(s) to execute query"));
			break;
		}

		GdaHolder *h = gda_set_get_holder (params, pname);
		if (!h) {
			gchar *tmp = gda_alphanum_to_text (g_strdup (pname + 1));
			if (tmp) {
				h = gda_set_get_holder (params, tmp);
				g_free (tmp);
			}
		}
		if (!h) {
			if (allow_noparam) {
                                /* bind param to NULL */
				mysql_bind_param[i].buffer_type = MYSQL_TYPE_NULL;
				mysql_bind_param[i].is_null = (my_bool*)1;
                                empty_rs = TRUE;
                                continue;
			}
			else {
				gchar *str;
				str = g_strdup_printf (_("Missing parameter '%s' to execute query"), pname);
				event = gda_connection_point_available_event (cnc, GDA_CONNECTION_EVENT_ERROR);
				gda_connection_event_set_description (event, str);
				g_set_error (error, GDA_SERVER_PROVIDER_ERROR,
					     GDA_SERVER_PROVIDER_MISSING_PARAM_ERROR, "%s", str);
				g_free (str);
				break;
			}
		}

		if (!gda_holder_is_valid (h)) {
			if (allow_noparam) {
                                /* bind param to NULL */
				mysql_bind_param[i].buffer_type = MYSQL_TYPE_NULL;
				mysql_bind_param[i].is_null = (my_bool*)1;
                                empty_rs = TRUE;
                                continue;
			}
			else {
				gchar *str;
				str = g_strdup_printf (_("Parameter '%s' is invalid"), pname);
				event = gda_connection_point_available_event (cnc, GDA_CONNECTION_EVENT_ERROR);
				gda_connection_event_set_description (event, str);
				g_set_error (error, GDA_SERVER_PROVIDER_ERROR,
					     GDA_SERVER_PROVIDER_MISSING_PARAM_ERROR, "%s", str);
				g_free (str);
				break;
                        }
		}
		else if (gda_holder_value_is_default (h) && !gda_holder_get_value (h)) {
			/* create a new GdaStatement to handle all default values and execute it instead */
			GdaSqlStatement *sqlst;
			GError *lerror = NULL;
			sqlst = gda_statement_rewrite_for_default_values (stmt, params, FALSE, &lerror);
			if (!sqlst) {
				event = gda_connection_point_available_event (cnc,
									      GDA_CONNECTION_EVENT_ERROR);
				gda_connection_event_set_description (event, lerror && lerror->message ? 
								      lerror->message :
								      _("Can't rewrite statement handle default values"));
				g_propagate_error (error, lerror);
				break;
			}
			
			GdaStatement *rstmt;
			GObject *res;
			rstmt = g_object_new (GDA_TYPE_STATEMENT, "structure", sqlst, NULL);
			gda_sql_statement_free (sqlst);
			free_bind_param_data (mem_to_free);
			res = gda_mysql_provider_statement_execute (provider, cnc,
								    rstmt, params,
								    model_usage,
								    col_types, last_inserted_row,
								    task_id,
								    async_cb, cb_data, error);
			g_object_unref (rstmt);
			return res;
		}

		/* actual binding using the C API, for parameter at position @i */
		const GValue *value = gda_holder_get_value (h);

		if (!value || gda_value_is_null (value)) {
			GdaStatement *rstmt;
			if (! gda_rewrite_statement_for_null_parameters (stmt, params, &rstmt, error)) {
				mysql_bind_param[i].buffer_type = MYSQL_TYPE_NULL;
				mysql_bind_param[i].is_null = (my_bool*)1;
			}
			else if (!rstmt)
				return NULL;
			else {
				free_bind_param_data (mem_to_free);

				/* The strategy here is to execute @rstmt using its prepared
				 * statement, but with common data from @ps. Beware that
				 * the @param_ids attribute needs to be retained (i.e. it must not
				 * be the one copied from @ps) */
				GObject *obj;
				GdaMysqlPStmt *tps;
				GdaPStmt *gtps;
				GSList *prep_param_ids, *copied_param_ids;
				if (!gda_mysql_provider_statement_prepare (provider, cnc,
									   rstmt, error))
					return NULL;
				tps = (GdaMysqlPStmt *)
					gda_connection_get_prepared_statement (cnc, rstmt);
				gtps = (GdaPStmt *) tps;

				/* keep @param_ids to avoid being cleared by gda_pstmt_copy_contents() */
				prep_param_ids = gtps->param_ids;
				gtps->param_ids = NULL;
				
				/* actual copy */
				gda_pstmt_copy_contents ((GdaPStmt *) ps, (GdaPStmt *) tps);

				/* restore previous @param_ids */
				copied_param_ids = gtps->param_ids;
				gtps->param_ids = prep_param_ids;

				/* execute */
				obj = gda_mysql_provider_statement_execute (provider, cnc,
									    rstmt, params,
									    model_usage,
									    col_types,
									    last_inserted_row,
									    task_id, async_cb,
									    cb_data, error);
				/* clear original @param_ids and restore copied one */
				g_slist_foreach (prep_param_ids, (GFunc) g_free, NULL);
				g_slist_free (prep_param_ids);

				gtps->param_ids = copied_param_ids;

				/*if (GDA_IS_DATA_MODEL (obj))
				  gda_data_model_dump ((GdaDataModel*) obj, NULL);*/

				g_object_unref (rstmt);
				return obj;
			}
		}
		else if (G_VALUE_TYPE (value) == GDA_TYPE_TIMESTAMP) {
			GdaTimestamp *ts;

			ts = (GdaTimestamp*) gda_value_get_timestamp (value);
			if (!ts) {
				mysql_bind_param[i].buffer_type = MYSQL_TYPE_NULL;
				mysql_bind_param[i].is_null = (my_bool*)1;
			}
			else {
				gboolean tofree = FALSE;
				if (ts->timezone != GDA_TIMEZONE_INVALID) {
					/* MySQL does not store timezone information, so if timezone information is
					 * provided, we do our best and convert it to GMT */
					ts = gda_timestamp_copy (ts);
					tofree = TRUE;
					gda_timestamp_change_timezone (ts, 0);
				}

				MYSQL_TIME *mtime;
				mtime = g_new0 (MYSQL_TIME, 1);
				mem_to_free = g_slist_prepend (mem_to_free, mtime);
				mtime->year = ts->year;
				mtime->month = ts->month;
				mtime->day = ts->day;
				mtime->hour = ts->hour;
				mtime->minute = ts->minute;
				mtime->second = ts->second;
				mtime->second_part = ts->fraction;
				if (tofree)
					gda_timestamp_free (ts);

				mysql_bind_param[i].buffer_type= MYSQL_TYPE_TIMESTAMP;
				mysql_bind_param[i].buffer= (char *)mtime;
				mysql_bind_param[i].buffer_length = sizeof (MYSQL_TIME);
			}
		}
		else if (G_VALUE_TYPE (value) == GDA_TYPE_TIME) {
			GdaTime *ts;

			ts = (GdaTime*) gda_value_get_time (value);
			if (!ts) {
				mysql_bind_param[i].buffer_type = MYSQL_TYPE_NULL;
				mysql_bind_param[i].is_null = (my_bool*)1;
			}
			else {
				gboolean tofree = FALSE;
				if (ts->timezone != GDA_TIMEZONE_INVALID) {
					/* MySQL does not store timezone information, so if timezone information is
					 * provided, we do our best and convert it to GMT */
					ts = gda_time_copy (ts);
					tofree = TRUE;
					gda_time_change_timezone (ts, 0);
				}

				MYSQL_TIME *mtime;
				mtime = g_new0 (MYSQL_TIME, 1);
				mem_to_free = g_slist_prepend (mem_to_free, mtime);
				mtime->hour = ts->hour;
				mtime->minute = ts->minute;
				mtime->second = ts->second;
				mtime->second_part = ts->fraction;
				if (tofree)
					gda_time_free (ts);

				mysql_bind_param[i].buffer_type= MYSQL_TYPE_TIME;
				mysql_bind_param[i].buffer= (char *)mtime;
				mysql_bind_param[i].buffer_length = sizeof (MYSQL_TIME);
			}
		}
		else if (G_VALUE_TYPE (value) == G_TYPE_DATE) {
			const GDate *ts;

			ts = (GDate*) g_value_get_boxed (value);
			if (!ts) {
				mysql_bind_param[i].buffer_type = MYSQL_TYPE_NULL;
				mysql_bind_param[i].is_null = (my_bool*)1;
			}
			else {
				MYSQL_TIME *mtime;
				mtime = g_new0 (MYSQL_TIME, 1);
				mem_to_free = g_slist_prepend (mem_to_free, mtime);
				mtime->year = g_date_get_year (ts);
				mtime->month = g_date_get_month (ts);
				mtime->day = g_date_get_day (ts);

				mysql_bind_param[i].buffer_type= MYSQL_TYPE_DATE;
				mysql_bind_param[i].buffer= (char *)mtime;
				mysql_bind_param[i].buffer_length = sizeof (MYSQL_TIME);
			}
		}
		else if (G_VALUE_TYPE (value) == G_TYPE_STRING) {
			const gchar *str;
			str = g_value_get_string (value);
			if (!str) {
				mysql_bind_param[i].buffer_type = MYSQL_TYPE_NULL;
				mysql_bind_param[i].is_null = (my_bool*)1;
			}
			else {
				mysql_bind_param[i].buffer_type= MYSQL_TYPE_STRING;
				mysql_bind_param[i].buffer= (char *) str;
				mysql_bind_param[i].buffer_length = strlen (str);
				mysql_bind_param[i].length = NULL; /* str is 0 terminated */
			}
		}
		else if (G_VALUE_TYPE (value) == G_TYPE_DOUBLE) {
			gdouble *pv;
			pv = g_new (gdouble, 1);
			mem_to_free = g_slist_prepend (mem_to_free, pv);
			*pv = g_value_get_double (value);
			mysql_bind_param[i].buffer_type= MYSQL_TYPE_DOUBLE;
			mysql_bind_param[i].buffer= pv;
			mysql_bind_param[i].buffer_length = sizeof (gdouble);
			mysql_bind_param[i].length = NULL;
		}
		else if (G_VALUE_TYPE (value) == G_TYPE_FLOAT) {
			gfloat *pv;
			pv = g_new (gfloat, 1);
			mem_to_free = g_slist_prepend (mem_to_free, pv);
			*pv = g_value_get_float (value);
			mysql_bind_param[i].buffer_type= MYSQL_TYPE_FLOAT;
			mysql_bind_param[i].buffer= pv;
			mysql_bind_param[i].buffer_length = sizeof (gfloat);
			mysql_bind_param[i].length = NULL;
		}
		else if (G_VALUE_TYPE (value) == G_TYPE_CHAR) {
			gint8 *pv;
			pv = g_new (gint8, 1);
			*pv = g_value_get_schar (value);
			mem_to_free = g_slist_prepend (mem_to_free, pv);
			mysql_bind_param[i].buffer_type= MYSQL_TYPE_TINY;
			mysql_bind_param[i].buffer = pv;
			mysql_bind_param[i].buffer_length = sizeof (gchar);
			mysql_bind_param[i].length = NULL;
		}
		else if (G_VALUE_TYPE (value) == GDA_TYPE_SHORT) {
			gshort *pv;
			pv = g_new (gshort, 1);
			mem_to_free = g_slist_prepend (mem_to_free, pv);
			*pv = gda_value_get_short (value);
			mysql_bind_param[i].buffer_type= MYSQL_TYPE_SHORT;
			mysql_bind_param[i].buffer= pv;
			mysql_bind_param[i].buffer_length = sizeof (gshort);
			mysql_bind_param[i].length = NULL;
		}
		else if (G_VALUE_TYPE (value) == G_TYPE_LONG) {
			glong *pv;
			pv = g_new (glong, 1);
			mem_to_free = g_slist_prepend (mem_to_free, pv);
			*pv = g_value_get_long (value);
			mysql_bind_param[i].buffer_type= MYSQL_TYPE_LONG;
			mysql_bind_param[i].buffer= pv;
			mysql_bind_param[i].buffer_length = sizeof (glong);
			mysql_bind_param[i].length = NULL;
		}
		else if (G_VALUE_TYPE (value) == G_TYPE_INT64) {
			gint64 *pv;
			pv = g_new (gint64, 1);
			mem_to_free = g_slist_prepend (mem_to_free, pv);
			*pv = g_value_get_long (value);
			mysql_bind_param[i].buffer_type= MYSQL_TYPE_LONGLONG;
			mysql_bind_param[i].buffer= pv;
			mysql_bind_param[i].buffer_length = sizeof (gint64);
			mysql_bind_param[i].length = NULL;
		}
		else if (G_VALUE_TYPE (value) == GDA_TYPE_BLOB) {
			const GdaBinary *bin = NULL;
			GdaBlob *blob = (GdaBlob*) gda_value_get_blob (value);

			bin = ((GdaBinary*) blob);
			if (!bin) {
				mysql_bind_param[i].buffer_type = MYSQL_TYPE_NULL;
				mysql_bind_param[i].is_null = (my_bool*)1;
			}
			else {
				gchar *str = NULL;
				glong blob_len;
				if (blob->op) {
					blob_len = gda_blob_op_get_length (blob->op);
					if ((blob_len != bin->binary_length) &&
					    ! gda_blob_op_read_all (blob->op, blob)) {
						/* force reading the complete BLOB into memory */
						str = _("Can't read whole BLOB into memory");
					}
				}
				else
					blob_len = bin->binary_length;
				if (blob_len < 0)
					str = _("Can't get BLOB's length");
				else if (blob_len >= G_MAXINT)
					str = _("BLOB is too big");
				
				if (str) {
					event = gda_connection_point_available_event (cnc, GDA_CONNECTION_EVENT_ERROR);
					gda_connection_event_set_description (event, str);
					g_set_error (error, GDA_SERVER_PROVIDER_ERROR,
						     GDA_SERVER_PROVIDER_DATA_ERROR, "%s", str);
					break;
				}
				
				else {
					mysql_bind_param[i].buffer_type= MYSQL_TYPE_BLOB;
					mysql_bind_param[i].buffer= (char *) bin->data;
					mysql_bind_param[i].buffer_length = bin->binary_length;
					mysql_bind_param[i].length = NULL;
				}
			}
		}
		else if (G_VALUE_TYPE (value) == GDA_TYPE_BINARY) {
			const GdaBinary *bin;
			bin = gda_value_get_binary (value);
			if (!bin) {
				mysql_bind_param[i].buffer_type = MYSQL_TYPE_NULL;
				mysql_bind_param[i].is_null = (my_bool*)1;
			}
			else {
				mysql_bind_param[i].buffer_type= MYSQL_TYPE_BLOB;
				mysql_bind_param[i].buffer= (char *) bin->data;
				mysql_bind_param[i].buffer_length = bin->binary_length;
				mysql_bind_param[i].length = NULL;
			}
		}
		else {
			gchar *str = NULL;
			GdaDataHandler *data_handler =
				gda_server_provider_get_data_handler_g_type (provider, cnc, 
									     G_VALUE_TYPE (value));
			if (data_handler == NULL) {
				/* there is an error here */
				str = g_strdup_printf(_("Unhandled data type '%s', please report this bug to "
							"http://bugzilla.gnome.org/ for the \"libgda\" product."),
						      gda_g_type_to_string (G_VALUE_TYPE (value)));
				event = gda_connection_point_available_event (cnc, GDA_CONNECTION_EVENT_ERROR);
				gda_connection_event_set_description (event, str);
				g_set_error (error, GDA_SERVER_PROVIDER_ERROR,
					     GDA_SERVER_PROVIDER_DATA_ERROR, "%s", str);
				g_free (str);
				break;
			}
			else {
				str = gda_data_handler_get_str_from_value (data_handler, value);
				mem_to_free = g_slist_prepend (mem_to_free, str);
				mysql_bind_param[i].buffer_type = MYSQL_TYPE_STRING;
				mysql_bind_param[i].buffer = str;
				mysql_bind_param[i].buffer_length = strlen (str);
				mysql_bind_param[i].length = NULL; /* str is 0 terminated */
			}
		}
	}
		
	if (!event && mysql_bind_param && mysql_stmt_bind_param (ps->mysql_stmt, mysql_bind_param)) {
		//g_warning ("mysql_stmt_bind_param failed: %s\n", mysql_stmt_error (ps->mysql_stmt));
		event = _gda_mysql_make_error (cnc, cdata->mysql, ps->mysql_stmt, error);
	}

	if (event) {
		gda_connection_add_event (cnc, event);
		g_object_unref (ps);
		free_bind_param_data (mem_to_free);
		return NULL;
	}


	/* use cursor when retrieving result */
	if ((model_usage & GDA_STATEMENT_MODEL_RANDOM_ACCESS) == 0 &&
	    gda_statement_get_statement_type (stmt) == GDA_SQL_STATEMENT_SELECT) {
#if MYSQL_VERSION_ID >= 50002
		const unsigned long cursor_type = CURSOR_TYPE_READ_ONLY;
		if (mysql_stmt_attr_set (ps->mysql_stmt, STMT_ATTR_CURSOR_TYPE, (void *) &cursor_type)) {
			_gda_mysql_make_error (cnc, NULL, ps->mysql_stmt, NULL);
			g_object_unref (ps);
			free_bind_param_data (mem_to_free);
			return NULL;
		}
#else
		model_usage = GDA_STATEMENT_MODEL_RANDOM_ACCESS;
		g_warning (_("Could not use CURSOR. Mysql version 5.0 at least is required. "
			     "Using random access anyway."));
#endif
	}

	/* add a connection event for the execution */
	event = gda_connection_point_available_event (cnc, GDA_CONNECTION_EVENT_COMMAND);
        gda_connection_event_set_description (event, _GDA_PSTMT (ps)->sql);
        gda_connection_add_event (cnc, event);

	if (empty_rs) {
		/* There are some missing parameters, so the SQL can't be executed but we still want
		 * to execute something to get the columns correctly. A possibility is to actually
		 * execute another SQL which is the code shown here.
		 *
		 * To adapt depending on the C API and its features */
		GdaStatement *stmt_for_empty;
                gchar *sql_for_empty;
                stmt_for_empty = gda_select_alter_select_for_empty (stmt, error);
                if (!stmt_for_empty) {
			g_object_unref (ps);
			free_bind_param_data (mem_to_free);
                        return NULL;
		}
                sql_for_empty = gda_statement_to_sql (stmt_for_empty, NULL, error);
                g_object_unref (stmt_for_empty);
                if (!sql_for_empty) {
			g_object_unref (ps);
			free_bind_param_data (mem_to_free);
                        return NULL;
		}

		/* This is a re-prepare of the statement.  The function mysql_stmt_prepare
		 * will handle this on the server side. */
		if (mysql_stmt_prepare (ps->mysql_stmt, sql_for_empty, strlen (sql_for_empty))) {
			_gda_mysql_make_error (cdata->cnc, NULL, ps->mysql_stmt, error);
			g_object_unref (ps);
			free_bind_param_data (mem_to_free);
			return NULL;
		}

		/* Execute the 'sql_for_empty' SQL code */
                g_free (sql_for_empty);
	}

	
	GObject *return_value = NULL;
	if (mysql_stmt_execute (ps->mysql_stmt)) {
		event = _gda_mysql_make_error (cnc, NULL, ps->mysql_stmt, error);
	}
	else {
		/* execute prepared statement using C API depending on its kind */
		my_ulonglong affected_rows;
		affected_rows = mysql_stmt_affected_rows (ps->mysql_stmt);
		if ((affected_rows == (my_ulonglong)~0) ||
		    !g_ascii_strncasecmp (_GDA_PSTMT (ps)->sql, "SELECT", 6) ||
		    !g_ascii_strncasecmp (_GDA_PSTMT (ps)->sql, "SHOW", 4) ||
		    !g_ascii_strncasecmp (_GDA_PSTMT (ps)->sql, "DESCRIBE", 8) ||
		    !g_ascii_strncasecmp (_GDA_PSTMT (ps)->sql, "EXECUTE", 7) ||
		    !g_ascii_strncasecmp (_GDA_PSTMT (ps)->sql, "EXPLAIN", 7)) {
			if (mysql_stmt_store_result (ps->mysql_stmt)) {
				_gda_mysql_make_error (cnc, NULL, ps->mysql_stmt, error);
			}
			else {
				GdaDataModelAccessFlags flags;

				if (model_usage & GDA_STATEMENT_MODEL_RANDOM_ACCESS)
					flags = GDA_DATA_MODEL_ACCESS_RANDOM;
				else
					flags = GDA_DATA_MODEL_ACCESS_CURSOR_FORWARD;

				return_value = (GObject *) gda_mysql_recordset_new (cnc, ps, params, flags, col_types);
				gda_connection_internal_statement_executed (cnc, stmt, params, NULL); /* required: help @cnc keep some stats */
			}
		}
		else {

			/* Create a #GdaSet containing "IMPACTED_ROWS" */
			/* Create GdaConnectionEvent notice with the type of command and impacted rows */
			GdaConnectionEvent *event;
			gchar *str;
			event = gda_connection_point_available_event (cnc, GDA_CONNECTION_EVENT_NOTICE);
			str = g_strdup_printf ("%llu", affected_rows);
			gda_connection_event_set_description (event, str);
			g_free (str);
			
			return_value = (GObject *) gda_set_new_inline
				(1, "IMPACTED_ROWS", G_TYPE_INT, (int) affected_rows);
			
			gda_connection_internal_statement_executed (cnc, stmt, params, event); /* required: help @cnc keep some stats */

			if (last_inserted_row) {
				my_ulonglong last_row;
				last_row = mysql_insert_id (cdata->mysql);
				if (last_row)
					*last_inserted_row = make_last_inserted_set (cnc, stmt, last_row);
			}

		}
	}
	g_object_unref (ps);
	free_bind_param_data (mem_to_free);
	return return_value;
}

/*
 * Rewrites a statement in case some parameters in @params are set to DEFAULT, for INSERT or UPDATE statements
 */
static GdaSqlStatement *
gda_mysql_statement_rewrite (GdaServerProvider *provider, GdaConnection *cnc,
			     GdaStatement *stmt, GdaSet *params, GError **error)
{
	if (cnc) {
		g_return_val_if_fail (GDA_IS_CONNECTION (cnc), NULL);
		g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, NULL);
	}
	return gda_statement_rewrite_for_default_values (stmt, params, FALSE, error);
}


/*
 * starts a distributed transaction: put the XA transaction in the ACTIVE state
 */
static gboolean
gda_mysql_provider_xa_start (GdaServerProvider         *provider,
			     GdaConnection             *cnc, 
			     const GdaXaTransactionId  *xid,
			     G_GNUC_UNUSED GError                   **error)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	g_return_val_if_fail (xid, FALSE);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	TO_IMPLEMENT;
	return FALSE;
}

/*
 * put the XA transaction in the IDLE state: the connection won't accept any more modifications.
 * This state is required by some database providers before actually going to the PREPARED state
 */
static gboolean
gda_mysql_provider_xa_end (GdaServerProvider         *provider,
			   GdaConnection             *cnc, 
			   const GdaXaTransactionId  *xid,
			   G_GNUC_UNUSED GError                   **error)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	g_return_val_if_fail (xid, FALSE);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	TO_IMPLEMENT;
	return FALSE;
}

/*
 * prepares the distributed transaction: put the XA transaction in the PREPARED state
 */
static gboolean
gda_mysql_provider_xa_prepare (GdaServerProvider         *provider,
			       GdaConnection             *cnc, 
			       const GdaXaTransactionId  *xid,
			       G_GNUC_UNUSED GError                   **error)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	g_return_val_if_fail (xid, FALSE);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	TO_IMPLEMENT;
	return FALSE;
}

/*
 * commits the distributed transaction: actually write the prepared data to the database and
 * terminates the XA transaction
 */
static gboolean
gda_mysql_provider_xa_commit (GdaServerProvider         *provider,
			      GdaConnection             *cnc, 
			      const GdaXaTransactionId  *xid,
			      G_GNUC_UNUSED GError                   **error)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	g_return_val_if_fail (xid, FALSE);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	TO_IMPLEMENT;
	return FALSE;
}

/*
 * Rolls back an XA transaction, possible only if in the ACTIVE, IDLE or PREPARED state
 */
static gboolean
gda_mysql_provider_xa_rollback (GdaServerProvider         *provider,
				GdaConnection             *cnc, 
				const GdaXaTransactionId  *xid,
				G_GNUC_UNUSED GError                   **error)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), FALSE);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, FALSE);
	g_return_val_if_fail (xid, FALSE);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return FALSE;

	TO_IMPLEMENT;
	return FALSE;
}

/*
 * Lists all XA transactions that are in the PREPARED state
 *
 * Returns: a list of GdaXaTransactionId structures, which will be freed by the caller
 */
static GList *
gda_mysql_provider_xa_recover (GdaServerProvider  *provider,
			       GdaConnection      *cnc,
			       G_GNUC_UNUSED GError            **error)
{
	MysqlConnectionData *cdata;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), NULL);
	g_return_val_if_fail (gda_connection_get_provider (cnc) == provider, NULL);

	cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data_error (cnc, error);
	if (!cdata) 
		return NULL;

	TO_IMPLEMENT;
	return NULL;
}

static gchar *
identifier_add_quotes (const gchar *str)
{
        gchar *retval, *rptr;
        const gchar *sptr;
        gint len;

        if (!str)
                return NULL;

        len = strlen (str);
        retval = g_new (gchar, 2*len + 3);
        *retval = '`';
        for (rptr = retval+1, sptr = str; *sptr; sptr++, rptr++) {
                if (*sptr == '`') {
                        *rptr = '\\';
                        rptr++;
                        *rptr = *sptr;
                }
                else
                        *rptr = *sptr;
        }
        *rptr = '`';
        rptr++;
        *rptr = 0;
        return retval;
}

static gboolean
_sql_identifier_needs_quotes (const gchar *str)
{
	const gchar *ptr;

	g_return_val_if_fail (str, FALSE);
	for (ptr = str; *ptr; ptr++) {
		/* quote if 1st char is a number */
		if ((*ptr <= '9') && (*ptr >= '0')) {
			if (ptr == str)
				return TRUE;
			continue;
		}
		if (((*ptr >= 'A') && (*ptr <= 'Z')) ||
		    ((*ptr >= 'a') && (*ptr <= 'z')))
			continue;

		if ((*ptr != '$') && (*ptr != '_') && (*ptr != '#'))
			return TRUE;
	}
	return FALSE;
}

/* Returns: @str */
static gchar *
my_remove_quotes (gchar *str)
{
        glong total;
        gchar *ptr;
        glong offset = 0;
	char delim;
	
	if (!str)
		return NULL;
	delim = *str;
	if ((delim != '`') && (delim != '"'))
		return str;

        total = strlen (str);
        if (str[total-1] == delim) {
		/* string is correctly terminated */
		memmove (str, str+1, total-2);
		total -=2;
	}
	else {
		/* string is _not_ correctly terminated */
		memmove (str, str+1, total-1);
		total -=1;
	}
        str[total] = 0;

        ptr = (gchar *) str;
        while (offset < total) {
                /* we accept the "''" as a synonym of "\'" */
                if (*ptr == delim) {
                        if (*(ptr+1) == delim) {
                                memmove (ptr+1, ptr+2, total - offset);
                                offset += 2;
                        }
                        else {
                                *str = 0;
                                return str;
                        }
                }
                if (*ptr == '\\') {
                        if (*(ptr+1) == '\\') {
                                memmove (ptr+1, ptr+2, total - offset);
                                offset += 2;
                        }
                        else {
                                if (*(ptr+1) == delim) {
                                        *ptr = delim;
                                        memmove (ptr+1, ptr+2, total - offset);
                                        offset += 2;
                                }
                                else {
                                        *str = 0;
                                        return str;
                                }
                        }
                }
                else
                        offset ++;

                ptr++;
        }

        return str;
}

static gchar *
gda_mysql_identifier_quote (GdaServerProvider *provider, GdaConnection *cnc,
			    const gchar *id,
			    gboolean for_meta_store, gboolean force_quotes)
{
	GdaSqlReservedKeywordsFunc kwfunc;
	MysqlConnectionData *cdata = NULL;
	gboolean case_sensitive = TRUE;

	if (cnc) {
		cdata = (MysqlConnectionData*) gda_connection_internal_get_provider_data (cnc);
		if (cdata) 
			case_sensitive = cdata->reuseable->identifiers_case_sensitive;
	}
	if (!cdata && ((GdaMysqlProvider*) provider)->test_mode)
		case_sensitive = ((GdaMysqlProvider*) provider)->test_identifiers_case_sensitive;

	kwfunc = _gda_mysql_reuseable_get_reserved_keywords_func
		(cdata ? (GdaProviderReuseable*) cdata->reuseable : NULL);

	if (case_sensitive) {
		/*
		 * case sensitive mode
		 */
		if (for_meta_store) {
			gchar *tmp, *ptr;
			tmp = my_remove_quotes (g_strdup (id));
			if (kwfunc (tmp)) {
				ptr = gda_sql_identifier_force_quotes (tmp);
				g_free (tmp);
				return ptr;
			}
			for (ptr = tmp; *ptr; ptr++) {
				if (((*ptr >= 'a') && (*ptr <= 'z')) ||
				    ((*ptr >= '0') && (*ptr <= '9') && (ptr != tmp)) ||
				    (*ptr == '_'))
					continue;
				else {
					ptr = gda_sql_identifier_force_quotes (tmp);
					g_free (tmp);
					return ptr;
				}
			}
			return tmp;
			/*			ptr = gda_sql_identifier_force_quotes (tmp);
			g_free (tmp);
			return ptr;*/
		}
		else {
			if ((*id == '`') || (*id == '"'))  {
				/* there are already some quotes */
				gchar *ptr, *tmp = g_strdup (id);
				for (ptr = tmp; *ptr; ptr++)
					if (*ptr == '"')
						*ptr = '`';
				return tmp;
			}
			return identifier_add_quotes (id);
		}
	}
	else {
		/*
		 * case insensitive mode
		 */
		if (for_meta_store) {
			gchar *tmp, *ptr;
			tmp = my_remove_quotes (g_strdup (id));
			if (kwfunc (tmp)) {
				ptr = gda_sql_identifier_force_quotes (tmp);
				g_free (tmp);
				return ptr;
			}
			else if (0 && force_quotes) {
				/* quote if non LC characters or digits at the 1st char or non allowed characters */
				for (ptr = tmp; *ptr; ptr++) {
					if (((*ptr >= 'a') && (*ptr <= 'z')) ||
					    ((*ptr >= '0') && (*ptr <= '9') && (ptr != tmp)) ||
					    (*ptr == '_'))
						continue;
					else {
						ptr = gda_sql_identifier_force_quotes (tmp);
						g_free (tmp);
						return ptr;
					}
				}
				return tmp;
			}
			else {
				for (ptr = tmp; *ptr; ptr++) {
					if ((*ptr >= 'A') && (*ptr <= 'Z'))
						*ptr += 'a' - 'A';
					else if (((*ptr >= 'a') && (*ptr <= 'z')) ||
					    ((*ptr >= '0') && (*ptr <= '9') && (ptr != tmp)) ||
					    (*ptr == '_'))
						continue;
					else {
						ptr = gda_sql_identifier_force_quotes (tmp);
						g_free (tmp);
						return ptr;
					}
				}
				return tmp;
			}
		}
		else {
			if ((*id == '`') || (*id == '"'))  {
				/* there are already some quotes */
				gchar *ptr, *tmp = g_strdup (id);
				for (ptr = tmp; *ptr; ptr++)
					if (*ptr == '"')
						*ptr = '`';
				return tmp;
			}
			if (kwfunc (id) || _sql_identifier_needs_quotes (id) || force_quotes)
				return identifier_add_quotes (id);
			
			/* nothing to do */
			return g_strdup (id);
		}
	}
}

/*
 * Free connection's specific data
 */
static void
gda_mysql_free_cnc_data (MysqlConnectionData  *cdata)
{
	if (!cdata)
		return;

	if (cdata->mysql) {
		mysql_close (cdata->mysql);
		cdata->mysql = NULL;
	}

	if (cdata->reuseable) {
		GdaProviderReuseable *rdata = (GdaProviderReuseable*)cdata->reuseable;
		rdata->operations->re_reset_data (rdata);
		g_free (cdata->reuseable);
	}
	
	g_free (cdata);
}
