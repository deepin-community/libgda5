/*
 * Copyright (C) 2008 - 2011 Vivien Malerba <malerba@gnome-db.org>
 * Copyright (C) 2010 David King <davidk@openismus.com>
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

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <glib.h>
#include <libgda/binreloc/gda-binreloc.h>
#include <string.h>

#define FILE_NAME "information_schema.xml"
#define OUT_FILE "i_s_doc.xml"

int
main (G_GNUC_UNUSED int argc, G_GNUC_UNUSED char** argv)
{
        xmlDocPtr doc;
        xmlNodePtr node;
        gchar *fname;
	xmlNodePtr out_top_node;
	xmlBufferPtr buf;

        fname = g_build_filename (ROOT_DIR, "libgda", FILE_NAME, NULL);
	if (! g_file_test (fname, G_FILE_TEST_EXISTS)) {
		g_free (fname);
		fname = gda_gbr_get_file_path (GDA_DATA_DIR, LIBGDA_ABI_NAME, FILE_NAME, NULL);
		if (! g_file_test (fname, G_FILE_TEST_EXISTS)) {
			g_print ("Could not find '%s'.\n", FILE_NAME);
			exit (1);
		}
	}

	doc = xmlParseFile (fname);
	if (!doc) {
		g_print ("Missing or malformed file '%s', check your installation", fname);
		g_free (fname);
		exit (1);
        }
	
	node = xmlDocGetRootElement (doc);
	g_free (fname);
	if (strcmp ((gchar *) node->name, "schema")) {
		g_print ("Root node should be <schema>, got <%s>\n", (gchar *) node->name);
		xmlFreeDoc (doc);
		exit (1);
	}

	out_top_node = xmlNewNode (NULL, BAD_CAST "sect2");
	xmlAddChild (out_top_node, xmlNewComment (BAD_CAST 
						  "File generated by the tools/information-schema-doc program from the\n"
						  "libgda/information-schema.xml file,\ndo not modify"));
	xmlNewChild (out_top_node, NULL, BAD_CAST "title", BAD_CAST "Individual table description");
	xmlNewChild (out_top_node, NULL, BAD_CAST "para", BAD_CAST "This section individually describes each table.");
	
	for (node = node->children; node; node = node->next) {
		if (!strcmp ((gchar *) node->name, "table")) {
			xmlNodePtr snode, child, table, row, ref = NULL;
			xmlChar *prop;
			snode = xmlNewChild (out_top_node, NULL, BAD_CAST "sect3", NULL);
			prop = xmlGetProp (node, BAD_CAST "name");
			if (prop) {
				gchar *str;
				str = g_strdup_printf ("is:%s", (gchar *) prop);
				xmlSetProp (snode, BAD_CAST "id", BAD_CAST str);
				g_free (str);

				str = g_strdup_printf ("%s table", (gchar *) prop);
				xmlNewTextChild (snode, NULL, BAD_CAST "title", BAD_CAST str);
				g_free (str);
				xmlFree (prop);
			}
			else
				xmlNewChild (snode, NULL, BAD_CAST "title", BAD_CAST "FIXME: table not named");

			prop = xmlGetProp (node, BAD_CAST "descr");
			if (prop) {
				xmlNewTextChild (snode, NULL, BAD_CAST "para", prop);
				xmlFree (prop);
			}
			table = xmlNewChild (snode, NULL, BAD_CAST "para", BAD_CAST "The following table describes the columns:");
			table = xmlNewChild (table, NULL, BAD_CAST "informaltable", NULL);
			xmlSetProp (table, BAD_CAST "frame", BAD_CAST "all");
			table = xmlNewChild (table, NULL, BAD_CAST "tgroup", NULL);
			xmlSetProp (table, BAD_CAST "cols", BAD_CAST "5");
			xmlSetProp (table, BAD_CAST "colsep", BAD_CAST "1");
			xmlSetProp (table, BAD_CAST "rowsep", BAD_CAST "1");
			xmlSetProp (table, BAD_CAST "align", BAD_CAST "justify");
			
			child = xmlNewChild (table, NULL, BAD_CAST "thead", NULL);
			row = xmlNewChild (child, NULL, BAD_CAST "row", NULL);
			xmlNewChild (row, NULL, BAD_CAST "entry", BAD_CAST "Column name");
			xmlNewChild (row, NULL, BAD_CAST "entry", BAD_CAST "Type");
			xmlNewChild (row, NULL, BAD_CAST "entry", BAD_CAST "Key");
			xmlNewChild (row, NULL, BAD_CAST "entry", BAD_CAST "Can be NULL");
			xmlNewChild (row, NULL, BAD_CAST "entry", BAD_CAST "description");
			table = xmlNewChild (table, NULL, BAD_CAST "tbody", NULL);	
			for (child = node->children; child; child = child->next) {
				if (!strcmp ((gchar *) child->name, "column")) {
					row = xmlNewChild (table, NULL, BAD_CAST "row", NULL);
					
					prop = xmlGetProp (child, BAD_CAST "name");
					xmlNewChild (row, NULL, BAD_CAST "entry", prop ? prop : BAD_CAST "FIXME");
					if (prop) xmlFree (prop);
					
					prop = xmlGetProp (child, BAD_CAST "type");
					xmlNewChild (row, NULL, BAD_CAST "entry", prop ? prop : BAD_CAST "string");
					if (prop) xmlFree (prop);
					
					prop = xmlGetProp (child, BAD_CAST "pkey");
					xmlNewChild (row, NULL, BAD_CAST "entry", 
						     prop && ((*prop == 't') || (*prop == 'T')) ? 
						     BAD_CAST "Yes" : BAD_CAST "");
					if (prop) xmlFree (prop);

					prop = xmlGetProp (child, BAD_CAST "nullok");
					xmlNewChild (row, NULL, BAD_CAST "entry", 
						     prop && ((*prop == 't') || (*prop == 'T')) ? 
						     BAD_CAST "Yes" : BAD_CAST "No");
					if (prop) xmlFree (prop);

					prop = xmlGetProp (child, BAD_CAST "descr");
					xmlNewTextChild (row, NULL, BAD_CAST "entry", prop ? prop : BAD_CAST "");
					if (prop) xmlFree (prop);
				}
				else if (!strcmp ((gchar *) child->name, "fkey")) {
					xmlNodePtr fkey;
					if (!ref) {
						ref = xmlNewChild (snode, NULL, BAD_CAST "para", NULL);
						ref = xmlNewChild (ref, NULL, BAD_CAST "itemizedlist", NULL);
					}
					fkey = xmlNewChild (ref, NULL, BAD_CAST "listitem", NULL);

					prop = xmlGetProp (child, BAD_CAST "ref_table");
					if (prop) {
						gchar *str;
						GString *fk_str = NULL, *ref_pk_str = NULL;
						xmlNodePtr subnode, link;

						fkey = xmlNewChild (fkey, NULL, BAD_CAST "para", NULL);
						
						for (subnode = child->children; subnode; subnode = subnode->next) {
							xmlChar *column, *ref_column;
							if (strcmp ((gchar *) subnode->name, "part")) 
								continue;
							column = xmlGetProp (subnode, BAD_CAST "column");
							if (!column)
								continue;
							if (!fk_str) {
								fk_str = g_string_new ("(");
								ref_pk_str = g_string_new ("(");
							}
							else {
								g_string_append (fk_str, ", ");
								g_string_append (ref_pk_str, ", ");
							}
							g_string_append (fk_str, (gchar *) column);
							ref_column = xmlGetProp (subnode, BAD_CAST "ref_column");
							if (ref_column) {
								g_string_append (ref_pk_str, (gchar *) ref_column);
								xmlFree (ref_column);
							}
							else
								g_string_append (ref_pk_str, (gchar *) column);
							xmlFree (column);
						}
						if (fk_str) {
							g_string_append (fk_str, ") ");
							g_string_append (ref_pk_str, ") ");
						}

						if (fk_str) {
							xmlNodeAddContent (fkey, BAD_CAST fk_str->str);
							g_string_free (fk_str, TRUE);
						}
						xmlNodeAddContent (fkey, BAD_CAST "references ");
						link = xmlNewTextChild (fkey, NULL, BAD_CAST "link", prop);
						str = g_strdup_printf ("is:%s", prop);
						xmlSetProp (link, BAD_CAST "linkend", BAD_CAST str);
						g_free (str);
						if (ref_pk_str) {
							xmlNodeAddContent (fkey, BAD_CAST ref_pk_str->str);
							g_string_free (ref_pk_str, TRUE);
						}
						
						if (prop) xmlFree (prop);
					}
					else
						fkey = xmlNewChild (ref, NULL, BAD_CAST "para", BAD_CAST "FIXME");
				}
			}
			
		}
		else if (!strcmp ((gchar *) node->name, "view")) {
			xmlNodePtr snode, child, para;
			xmlChar *prop;
			snode = xmlNewChild (out_top_node, NULL, BAD_CAST "sect3", NULL);
			prop = xmlGetProp (node, BAD_CAST "name");
			if (prop) {
				gchar *str;
				str = g_strdup_printf ("is:%s", (gchar *) prop);
				xmlSetProp (snode, BAD_CAST "id", BAD_CAST str);
				g_free (str);

				str = g_strdup_printf ("%s view", (gchar *) prop);
				xmlNewTextChild (snode, NULL, BAD_CAST "title", BAD_CAST str);
				g_free (str);
				xmlFree (prop);
			}
			else
				xmlNewChild (snode, NULL, BAD_CAST "title", BAD_CAST "FIXME: view not named");

			prop = xmlGetProp (node, BAD_CAST "descr");
			if (prop) {
				xmlNewTextChild (snode, NULL, BAD_CAST "para", prop);
				xmlFree (prop);
			}
			for (child = node->children; child; child = child->next) {
				if (!strcmp ((gchar *) child->name, "definition")) {
					xmlChar *def;

					def = xmlNodeGetContent (child);
					para = xmlNewChild (snode, NULL, BAD_CAST "para", BAD_CAST "Definition is:");
					para = xmlNewTextChild (para, NULL, BAD_CAST "programlisting", def);
					xmlSetProp (para, BAD_CAST "width", BAD_CAST "80");
					xmlFree (def);
					break;
				}
			}
		}
	}
	xmlFreeDoc (doc);

	buf = xmlBufferCreate();
	xmlNodeDump (buf, NULL, out_top_node, 0, 1);
	if (! g_file_set_contents (OUT_FILE, (gchar*) xmlBufferContent (buf), -1, NULL)) 
		g_print ("Could not write output file '%s'\n", OUT_FILE);
	else
		g_print ("Doc. written to '%s'\n", OUT_FILE);
	xmlBufferFree (buf);
	
	return 0;
}
