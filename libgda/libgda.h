/*
 * Copyright (C) 2000 Reinhard Müller <reinhard@src.gnome.org>
 * Copyright (C) 2000 - 2004 Rodrigo Moya <rodrigo@gnome-db.org>
 * Copyright (C) 2001 Carlos Perelló Marín <carlos@gnome-db.org>
 * Copyright (C) 2001 - 2011 Vivien Malerba <malerba@gnome-db.org>
 * Copyright (C) 2002 Andrew Hill <andru@src.gnome.org>
 * Copyright (C) 2002 - 2003 Gonzalo Paniagua Javier <gonzalo@ximian.com>
 * Copyright (C) 2003 - 2006 Murray Cumming <murrayc@murrayc.com>
 * Copyright (C) 2004 Szalai Ferenc <szferi@einstein.ki.iif.hu>
 * Copyright (C) 2005 Bas Driessen <bas.driessen@xobas.com>
 * Copyright (C) 2005 Álvaro Peña <alvaropg@telefonica.net>
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

#ifndef __LIBGDA_H__
#define __LIBGDA_H__

#include <libgda/gda-attributes-manager.h>
#include <libgda/gda-column.h>
#include <libgda/gda-config.h>
#include <libgda/gda-connection-event.h>
#include <libgda/gda-connection.h>
#include <libgda/gda-connection-private.h>
#include <libgda/gda-data-comparator.h>
#include <libgda/gda-data-model-array.h>

#include <libgda/gda-data-model-ldap.h>
#include <libgda/gda-tree-mgr-ldap.h>
#include <libgda/gda-data-model.h>
#include <libgda/gda-data-model-iter.h>
#include <libgda/gda-data-model-import.h>
#include <libgda/gda-data-model-dir.h>
#include <libgda/gda-data-access-wrapper.h>
#include <libgda/gda-data-proxy.h>
#include <libgda/gda-data-select.h>
#include <libgda/gda-data-pivot.h>
#include <libgda/gda-lockable.h>
#include <libgda/gda-log.h>
#include <libgda/gda-quark-list.h>
#include <libgda/gda-row.h>
#include <libgda/gda-server-operation.h>
#include <libgda/gda-server-provider.h>
#include <libgda/gda-xa-transaction.h>
#include <libgda/gda-transaction-status.h>
#include <libgda/gda-transaction-status-private.h>
#include <libgda/gda-util.h>
#include <libgda/gda-value.h>
#include <libgda/gda-decl.h>
#include <libgda/gda-enums.h>
#include <libgda/gda-data-handler.h>
#include <libgda/handlers/gda-handler-bin.h>
#include <libgda/handlers/gda-handler-boolean.h>
#include <libgda/handlers/gda-handler-numerical.h>
#include <libgda/handlers/gda-handler-string.h>
#include <libgda/handlers/gda-handler-time.h>
#include <libgda/handlers/gda-handler-type.h>
#include <libgda/gda-meta-store.h>
#include <libgda/gda-meta-struct.h>

#include <libgda/gda-statement.h>
#include <libgda/gda-batch.h>
#include <libgda/gda-holder.h>
#include <libgda/gda-set.h>

#include <libgda/gda-tree.h> 
#include <libgda/gda-tree-manager.h> 
#include <libgda/gda-tree-mgr-columns.h> 
#include <libgda/gda-tree-mgr-label.h> 
#include <libgda/gda-tree-mgr-schemas.h> 
#include <libgda/gda-tree-mgr-select.h> 
#include <libgda/gda-tree-mgr-tables.h> 
#include <libgda/gda-tree-node.h>

#include <libgda/sql-parser/gda-sql-parser.h>

#include <libgda/gda-sql-builder.h>

#include <libgda/gda-meta-store.h>

#include <libgda/gda-mutex.h>

G_BEGIN_DECLS

/**
 * SECTION:libgda
 * @short_description: Library initialization and information
 * @title: Library initialization
 * @stability: Stable
 * @see_also:
 */

void gda_init (void);
void gda_locale_changed (void);
gchar *gda_get_application_exec_path (const gchar *app_name);

G_END_DECLS

#endif
