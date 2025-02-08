/*
 * Copyright (C) 2000 - 2001 Rodrigo Moya <rodrigo@src.gnome.org>
 * Copyright (C) 2001 Reinhard Müller <reinhard@src.gnome.org>
 * Copyright (C) 2002 - 2003 Gonzalo Paniagua Javier <gonzalo@src.gnome.org>
 * Copyright (C) 2007 Baris Cicek <bcicek@src.gnome.org>
 * Copyright (C) 2007 - 2012 Vivien Malerba <malerba@gnome-db.org>
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

#ifndef __GDA_SQL_H_
#define __GDA_SQL_H_

#include <libgda/libgda.h>
#include <tools/gda-threader.h>
#include <sql-parser/gda-sql-parser.h>
#include "tools-favorites.h"
#include <cmdtool/tool.h>

G_BEGIN_DECLS

/*
 * structure representing an opened connection
 */
typedef struct {
	gchar          *name;
	GdaConnection  *cnc;
	GdaSqlParser   *parser;
	GString        *query_buffer;
	ToolsFavorites *favorites;

	GdaThreader    *threader;
	guint           meta_job_id;
} ConnectionSetting;

/*
 * Structure representing a context using a connection
 */
typedef struct {
	gchar *id;
	ConnectionSetting *current;

	ToolOutputFormat output_format;
	FILE *output_stream;
	gboolean output_is_pipe;

	GTimeVal last_time_used;
	ToolCommandGroup *command_group;
} SqlConsole;

const GSList            *gda_sql_get_all_connections (void);
const ConnectionSetting *gda_sql_get_connection (const gchar *name);
const ConnectionSetting *gda_sql_get_current_connection (void);

SqlConsole              *gda_sql_console_new (const gchar *id);
void                     gda_sql_console_free (SqlConsole *console);
gchar                   *gda_sql_console_execute (SqlConsole *console, const gchar *command,
						  GError **error, ToolOutputFormat format);

gchar                   *gda_sql_console_compute_prompt (SqlConsole *console, ToolOutputFormat format);

G_END_DECLS

#endif
