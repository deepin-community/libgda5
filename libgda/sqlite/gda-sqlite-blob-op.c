/*
 * Copyright (C) 2009 - 2011 Vivien Malerba <malerba@gnome-db.org>
 * Copyright (C) 2010 David King <davidk@openismus.com>
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

#include <glib/gi18n-lib.h>
#include <string.h>
#include <libgda/libgda.h>
#include "gda-sqlite.h"
#include "gda-sqlite-blob-op.h"
#include "gda-sqlite-util.h"
#include <sql-parser/gda-sql-parser.h>

struct _GdaSqliteBlobOpPrivate {
	sqlite3_blob  *sblob;
};

static void gda_sqlite_blob_op_class_init (GdaSqliteBlobOpClass *klass);
static void gda_sqlite_blob_op_init       (GdaSqliteBlobOp *blob,
					   GdaSqliteBlobOpClass *klass);
static void gda_sqlite_blob_op_finalize   (GObject *object);

static glong gda_sqlite_blob_op_get_length (GdaBlobOp *op);
static glong gda_sqlite_blob_op_read       (GdaBlobOp *op, GdaBlob *blob, glong offset, glong size);
static glong gda_sqlite_blob_op_write      (GdaBlobOp *op, GdaBlob *blob, glong offset);

static GObjectClass *parent_class = NULL;

/*
 * Object init and finalize
 */
GType
_gda_sqlite_blob_op_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GMutex registering;
		static const GTypeInfo info = {
			sizeof (GdaSqliteBlobOpClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gda_sqlite_blob_op_class_init,
			NULL,
			NULL,
			sizeof (GdaSqliteBlobOp),
			0,
			(GInstanceInitFunc) gda_sqlite_blob_op_init,
			0
		};
		g_mutex_lock (&registering);
		if (type == 0)
			type = g_type_register_static (GDA_TYPE_BLOB_OP, CLASS_PREFIX "BlobOp", &info, 0);
		g_mutex_unlock (&registering);
	}
	return type;
}

static void
gda_sqlite_blob_op_init (GdaSqliteBlobOp *op, G_GNUC_UNUSED GdaSqliteBlobOpClass *klass)
{
	g_return_if_fail (GDA_IS_SQLITE_BLOB_OP (op));

	op->priv = g_new0 (GdaSqliteBlobOpPrivate, 1);
	op->priv->sblob = NULL;
}

static void
gda_sqlite_blob_op_class_init (GdaSqliteBlobOpClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GdaBlobOpClass *blob_class = GDA_BLOB_OP_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = gda_sqlite_blob_op_finalize;
	blob_class->get_length = gda_sqlite_blob_op_get_length;
	blob_class->read = gda_sqlite_blob_op_read;
	blob_class->write = gda_sqlite_blob_op_write;
}

static void
gda_sqlite_blob_op_finalize (GObject * object)
{
	GdaSqliteBlobOp *bop = (GdaSqliteBlobOp *) object;

	g_return_if_fail (GDA_IS_SQLITE_BLOB_OP (bop));

	/* free specific information */
	if (bop->priv->sblob) {
		SQLITE3_CALL (sqlite3_blob_close) (bop->priv->sblob);
#ifdef GDA_DEBUG_NO
		g_print ("CLOSED blob %p\n", bop);
#endif
	}
	g_free (bop->priv);
	bop->priv = NULL;

	parent_class->finalize (object);
}

GdaBlobOp *
_gda_sqlite_blob_op_new (SqliteConnectionData *cdata,
			 const gchar *db_name, const gchar *table_name,
			 const gchar *column_name, sqlite3_int64 rowid)
{
	GdaSqliteBlobOp *bop = NULL;
	int rc;
	sqlite3_blob *sblob;
	gchar *db, *table;
	gboolean free_strings = TRUE;
	gboolean transaction_started = FALSE;

	g_return_val_if_fail (table_name, NULL);
	g_return_val_if_fail (column_name, NULL);

	if (db_name) {
		db = (gchar *) db_name;
		table = (gchar *) table_name;
		free_strings = FALSE;
	}
	else if (! _split_identifier_string (g_strdup (table_name), &db, &table))
		return NULL;

	if (! _gda_sqlite_check_transaction_started (cdata->gdacnc, &transaction_started, NULL))
		return NULL;

	rc = SQLITE3_CALL (sqlite3_blob_open) (cdata->connection, db ? db : "main",
					       table, column_name, rowid,
				1, /* Read & Write */
				&(sblob));
	if (rc != SQLITE_OK) {
#ifdef GDA_DEBUG_NO
		g_print ("ERROR: %s\n", SQLITE3_CALL (sqlite3_errmsg) (cdata->connection));
#endif
		if (transaction_started)
			gda_connection_rollback_transaction (cdata->gdacnc, NULL, NULL);
		goto out;
	}

	bop = g_object_new (GDA_TYPE_SQLITE_BLOB_OP, NULL);
	bop->priv->sblob = sblob;
#ifdef GDA_DEBUG_NO
	g_print ("OPENED blob %p\n", bop);
#endif

 out:
	if (free_strings) {
		g_free (db);
		g_free (table);
	}
	return (GdaBlobOp*) bop;
}

/*
 * Get length request
 */
static glong
gda_sqlite_blob_op_get_length (GdaBlobOp *op)
{
	GdaSqliteBlobOp *bop;
	int len;

	g_return_val_if_fail (GDA_IS_SQLITE_BLOB_OP (op), -1);
	bop = GDA_SQLITE_BLOB_OP (op);
	g_return_val_if_fail (bop->priv, -1);
	g_return_val_if_fail (bop->priv->sblob, -1);

	len = SQLITE3_CALL (sqlite3_blob_bytes) (bop->priv->sblob);
	return len >= 0 ? len : 0;
}

/*
 * Blob read request
 */
static glong
gda_sqlite_blob_op_read (GdaBlobOp *op, GdaBlob *blob, glong offset, glong size)
{
	GdaSqliteBlobOp *bop;
	GdaBinary *bin;
	int rc;

	g_return_val_if_fail (GDA_IS_SQLITE_BLOB_OP (op), -1);
	bop = GDA_SQLITE_BLOB_OP (op);
	g_return_val_if_fail (bop->priv, -1);
	g_return_val_if_fail (bop->priv->sblob, -1);
	if (offset >= G_MAXINT)
		return -1;
	g_return_val_if_fail (blob, -1);

	if (offset > G_MAXINT)
		return -1;
	if (size > G_MAXINT)
		return -1;

	bin = (GdaBinary *) blob;
	if (bin->data) 
		g_free (bin->data);
	bin->data = g_new0 (guchar, size);
	bin->binary_length = 0;

	/* fetch blob data using C API into bin->data, and set bin->binary_length */
	int rsize;
	int len;

	len = SQLITE3_CALL (sqlite3_blob_bytes) (bop->priv->sblob);
	if (len < 0)
		return -1;
	else if (len == 0)
		return 0;
		
	rsize = (int) size;
	if (offset >= len)
		return -1;

	if (len - offset < rsize)
		rsize = len - offset;

	rc = SQLITE3_CALL (sqlite3_blob_read) (bop->priv->sblob, bin->data, rsize, offset);
	if (rc != SQLITE_OK) {
		g_free (bin->data);
		bin->data = NULL;
		return -1;
	}
	bin->binary_length = rsize;

	return bin->binary_length;
}

/*
 * Blob write request
 */
static glong
gda_sqlite_blob_op_write (GdaBlobOp *op, GdaBlob *blob, glong offset)
{
	GdaSqliteBlobOp *bop;
	GdaBinary *bin;
	glong nbwritten = -1;
	int len;

	g_return_val_if_fail (GDA_IS_SQLITE_BLOB_OP (op), -1);
	bop = GDA_SQLITE_BLOB_OP (op);
	g_return_val_if_fail (bop->priv, -1);
	g_return_val_if_fail (bop->priv->sblob, -1);
	g_return_val_if_fail (blob, -1);

	len = SQLITE3_CALL (sqlite3_blob_bytes) (bop->priv->sblob);
	if (len < 0)
		return -1;

	if (blob->op && (blob->op != op)) {
		/* use data through blob->op */
		#define buf_size 16384
		gint nread = 0;
		GdaBlob *tmpblob = g_new0 (GdaBlob, 1);
		gda_blob_set_op (tmpblob, blob->op);

		nbwritten = 0;

		for (nread = gda_blob_op_read (tmpblob->op, tmpblob, nbwritten, buf_size);
		     nread > 0;
		     nread = gda_blob_op_read (tmpblob->op, tmpblob, nbwritten, buf_size)) {
			int tmp_written;
			int rc;
			int wlen;
			
			if (nread + offset + nbwritten > len)
				wlen = 	len - offset - nbwritten;
			else
				wlen = nread;

			rc = SQLITE3_CALL (sqlite3_blob_write) (bop->priv->sblob,
								((GdaBinary *)tmpblob)->data, wlen, offset + nbwritten);
			if (rc != SQLITE_OK)
				tmp_written = -1;
			else
				tmp_written = wlen;
			
			if (tmp_written < 0) {
				/* treat error */
				gda_blob_free ((gpointer) tmpblob);
				return -1;
			}
			nbwritten += tmp_written;
			if (nread < buf_size)
				/* nothing more to read */
				break;
		}
		gda_blob_free ((gpointer) tmpblob);
	}
	else {
		/* write blob using bin->data and bin->binary_length */
		int rc;
		int wlen;
		bin = (GdaBinary *) blob;
		if (bin->binary_length + offset > len)
			wlen = 	len - offset;
		else
			wlen = bin->binary_length;

		rc = SQLITE3_CALL (sqlite3_blob_write) (bop->priv->sblob, bin->data, wlen, offset);
		if (rc != SQLITE_OK)
			nbwritten = -1;
		else
			nbwritten = wlen;
	}

	return nbwritten;
}
