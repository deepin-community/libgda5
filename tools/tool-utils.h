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

#ifndef __GDA_TOOLS_UTILS__
#define __GDA_TOOLS_UTILS__

#include <libgda/libgda.h>

const gchar *gda_tools_utils_fk_policy_to_string (GdaMetaForeignKeyPolicy policy);

/*
 * error reporting
 */
extern GQuark gda_tools_error_quark (void);
#define GDA_TOOLS_ERROR gda_tools_error_quark ()

typedef enum {
	GDA_TOOLS_NO_CONNECTION_ERROR,
	GDA_TOOLS_CONNECTION_CLOSED_ERROR,
	GDA_TOOLS_INTERNAL_COMMAND_ERROR,
	GDA_TOOLS_COMMAND_ARGUMENTS_ERROR,
	GDA_TOOLS_OBJECT_NOT_FOUND_ERROR,
	GDA_TOOLS_PROVIDER_NOT_FOUND_ERROR,
	GDA_TOOLS_DSN_NOT_FOUND_ERROR,
	GDA_TOOLS_STORED_DATA_ERROR,
	GDA_TOOLS_PURGE_ERROR
} ToolsError;

#endif
