/*
 * Copyright (C) 2007 - 2011 Vivien Malerba <malerba@gnome-db.org>
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
#include <libgda/libgda.h>
#include <gda-report-engine.h>
#include <sql-parser/gda-sql-parser.h>

GdaConnection *open_connection (void);
GSList        *create_queries (GdaConnection *cnc);

int 
main (int argc, char **argv)
{
	gchar *spec;
	GdaReportEngine *eng;
	GdaConnection *cnc, *store_cnc;
	GdaHolder *param;
	GdaMetaStore *store;
	GError *error = NULL;

	gda_init ();

	/* Engine object */
	g_assert (g_file_get_contents ("schema-report-spec.xml", &spec, NULL, NULL));
	eng = gda_report_engine_new_from_string (spec);
	g_free (spec);

	/* GdaConnection */
	cnc = open_connection ();
	gda_report_engine_declare_object (eng, G_OBJECT (cnc), "main_cnc");
	
	g_print ("Collecting meta data...");
	if (! gda_connection_update_meta_store (cnc, NULL, &error)) {
                g_print ("Could not collect meta data: %s\n",
                         error && error->message ? error->message : "No detail");
                exit (1);
        }
	g_print ("done\n");
	store = gda_connection_get_meta_store (cnc);
	store_cnc = gda_meta_store_get_internal_connection (store);
	gda_report_engine_declare_object (eng, G_OBJECT (store_cnc), "meta_store_cnc");

	/* define parameters */
	param = gda_holder_new_string ("abstract", "-- This text is from a parameter set in the code, not in the spec. file --");
	gda_report_engine_declare_object (eng, G_OBJECT (param), "abstract");
	g_object_unref (param);

	/* create queries */
	GSList *queries, *list;
	queries = create_queries (cnc);
	for (list = queries; list; list = list->next) {
		gda_report_engine_declare_object (eng, G_OBJECT (list->data),
						  g_object_get_data (G_OBJECT (list->data), "name"));
		g_object_unref (G_OBJECT (list->data));
	}
	g_slist_free (queries);

	/* run the report */
	xmlDocPtr doc;
	doc = gda_report_engine_run_as_doc (eng, &error);
	if (!doc) {
		g_print ("gda_report_engine_run error: %s\n",
			 error && error->message ? error->message : "No detail");
		exit (1);
	}
	else {
		g_print ("Report generated in schema-report.xml.\n");
		xmlSaveFile ("schema-report.xml", doc);
		xmlFreeDoc (doc);
	}

	g_object_unref (eng);
	g_object_unref (cnc);

	return 0;
}

GdaConnection *
open_connection (void)
{
        GdaConnection *cnc;
        GError *error = NULL;
        cnc = gda_connection_open_from_dsn ("SalesTest", NULL,
					    GDA_CONNECTION_OPTIONS_NONE,
					    &error);
        if (!cnc) {
                g_print ("Could not open connection to DSN 'SalesTest': %s\n",
                         error && error->message ? error->message : "No detail");
                exit (1);
        }
        return cnc;
}

GSList *
create_queries (GdaConnection *cnc)
{
	GdaSqlParser *parser;
	GdaStatement *stmt;
	GSList *list = NULL;

	parser = gda_connection_create_parser (cnc);

	stmt = gda_sql_parser_parse_string (parser, "SELECT * FROM _schemata", NULL, NULL);
	g_object_set_data ((GObject*) stmt, "name", "schemas");
	list = g_slist_prepend (list, stmt);
	
	g_object_unref (parser);

	return list;
}
