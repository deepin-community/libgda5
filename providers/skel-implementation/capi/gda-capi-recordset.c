/*
 * Copyright (C) YEAR The GNOME Foundation.
 *
 * AUTHORS:
 *      TO_ADD: your name and email
 *      Vivien Malerba <malerba@gnome-db.org>
 *
 * This Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <stdarg.h>
#include <string.h>
#include <glib/gi18n-lib.h>
#include <libgda/gda-util.h>
#include <libgda/gda-connection-private.h>
#include "gda-capi.h"
#include "gda-capi-recordset.h"
#include "gda-capi-provider.h"
#include <libgda/gda-debug-macros.h>

#define _GDA_PSTMT(x) ((GdaPStmt*)(x))

static void gda_capi_recordset_class_init (GdaCapiRecordsetClass *klass);
static void gda_capi_recordset_init       (GdaCapiRecordset *recset,
					     GdaCapiRecordsetClass *klass);
static void gda_capi_recordset_dispose   (GObject *object);

/* virtual methods */
static gint     gda_capi_recordset_fetch_nb_rows (GdaDataSelect *model);
static gboolean gda_capi_recordset_fetch_random (GdaDataSelect *model, GdaRow **prow, gint rownum, GError **error);
static gboolean gda_capi_recordset_fetch_next (GdaDataSelect *model, GdaRow **prow, gint rownum, GError **error);
static gboolean gda_capi_recordset_fetch_prev (GdaDataSelect *model, GdaRow **prow, gint rownum, GError **error);
static gboolean gda_capi_recordset_fetch_at (GdaDataSelect *model, GdaRow **prow, gint rownum, GError **error);


struct _GdaCapiRecordsetPrivate {
	GdaConnection *cnc;
	/* TO_ADD: specific information */
};
static GObjectClass *parent_class = NULL;

/*
 * Object init and finalize
 */
static void
gda_capi_recordset_init (GdaCapiRecordset *recset,
			   G_GNUC_UNUSED GdaCapiRecordsetClass *klass)
{
	g_return_if_fail (GDA_IS_CAPI_RECORDSET (recset));
	recset->priv = g_new0 (GdaCapiRecordsetPrivate, 1);
	recset->priv->cnc = NULL;

	/* initialize specific information */
	TO_IMPLEMENT;
}

static void
gda_capi_recordset_class_init (GdaCapiRecordsetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GdaDataSelectClass *pmodel_class = GDA_DATA_SELECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = gda_capi_recordset_dispose;
	pmodel_class->fetch_nb_rows = gda_capi_recordset_fetch_nb_rows;
	pmodel_class->fetch_random = gda_capi_recordset_fetch_random;

	pmodel_class->fetch_next = gda_capi_recordset_fetch_next;
	pmodel_class->fetch_prev = gda_capi_recordset_fetch_prev;
	pmodel_class->fetch_at = gda_capi_recordset_fetch_at;
}

static void
gda_capi_recordset_dispose (GObject *object)
{
	GdaCapiRecordset *recset = (GdaCapiRecordset *) object;

	g_return_if_fail (GDA_IS_CAPI_RECORDSET (recset));

	if (recset->priv) {
		if (recset->priv->cnc) 
			g_object_unref (recset->priv->cnc);

		/* free specific information */
		TO_IMPLEMENT;
		g_free (recset->priv);
		recset->priv = NULL;
	}

	parent_class->dispose (object);
}

/*
 * Public functions
 */

GType
gda_capi_recordset_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GMutex registering;
		static const GTypeInfo info = {
			sizeof (GdaCapiRecordsetClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gda_capi_recordset_class_init,
			NULL,
			NULL,
			sizeof (GdaCapiRecordset),
			0,
			(GInstanceInitFunc) gda_capi_recordset_init,
			0
		};
		g_mutex_lock (&registering);
		if (type == 0)
			type = g_type_register_static (GDA_TYPE_DATA_SELECT, "GdaCapiRecordset", &info, 0);
		g_mutex_unlock (&registering);
	}

	return type;
}

/*
 * the @ps struct is modified and transferred to the new data model created in
 * this function
 */
GdaDataModel *
gda_capi_recordset_new (GdaConnection *cnc, GdaCapiPStmt *ps, GdaSet *exec_params,
			GdaDataModelAccessFlags flags, GType *col_types)
{
	GdaCapiRecordset *model;
        CapiConnectionData *cdata;
        gint i;
	GdaDataModelAccessFlags rflags;

        g_return_val_if_fail (GDA_IS_CONNECTION (cnc), NULL);
        g_return_val_if_fail (ps != NULL, NULL);

	cdata = (CapiConnectionData*) gda_connection_internal_get_provider_data (cnc);
	if (!cdata)
		return NULL;

	/* make sure @ps reports the correct number of columns using the API*/
        if (_GDA_PSTMT (ps)->ncols < 0)
                /*_GDA_PSTMT (ps)->ncols = ...;*/
		TO_IMPLEMENT;

        /* completing @ps if not yet done */
        if (!_GDA_PSTMT (ps)->types && (_GDA_PSTMT (ps)->ncols > 0)) {
		/* create prepared statement's columns */
		GSList *list;
		for (i = 0; i < _GDA_PSTMT (ps)->ncols; i++)
			_GDA_PSTMT (ps)->tmpl_columns = g_slist_prepend (_GDA_PSTMT (ps)->tmpl_columns, 
									 gda_column_new ());
		_GDA_PSTMT (ps)->tmpl_columns = g_slist_reverse (_GDA_PSTMT (ps)->tmpl_columns);

		/* create prepared statement's types, all types are initialized to GDA_TYPE_NULL */
		_GDA_PSTMT (ps)->types = g_new (GType, _GDA_PSTMT (ps)->ncols);
		for (i = 0; i < _GDA_PSTMT (ps)->ncols; i++)
			_GDA_PSTMT (ps)->types [i] = GDA_TYPE_NULL;

		if (col_types) {
			for (i = 0; ; i++) {
				if (col_types [i] > 0) {
					if (col_types [i] == G_TYPE_NONE)
						break;
					if (i >= _GDA_PSTMT (ps)->ncols) {
						g_warning (_("Column %d out of range (0-%d), ignoring its specified type"), i,
							   _GDA_PSTMT (ps)->ncols - 1);
						break;
					}
					else
						_GDA_PSTMT (ps)->types [i] = col_types [i];
				}
			}
		}
		
		/* fill GdaColumn's data */
		for (i=0, list = _GDA_PSTMT (ps)->tmpl_columns; 
		     i < GDA_PSTMT (ps)->ncols; 
		     i++, list = list->next) {
			GdaColumn *column;
			
			column = GDA_COLUMN (list->data);

			/* use C API to set columns' information using gda_column_set_*() */
			g_warning("column not used: %p", column); /* Avoids a compiler warning. */
			TO_IMPLEMENT;
		}
        }

	/* determine access mode: RANDOM or CURSOR FORWARD are the only supported */
	if (flags & GDA_DATA_MODEL_ACCESS_RANDOM)
		rflags = GDA_DATA_MODEL_ACCESS_RANDOM;
	else
		rflags = GDA_DATA_MODEL_ACCESS_CURSOR_FORWARD;

	/* create data model */
        model = g_object_new (GDA_TYPE_CAPI_RECORDSET, 
			      "prepared-stmt", ps, 
			      "model-usage", rflags, 
			      "exec-params", exec_params, NULL);
        model->priv->cnc = cnc;
	g_object_ref (cnc);

	/* post init specific code */
	TO_IMPLEMENT;

        return GDA_DATA_MODEL (model);
}


/*
 * Get the number of rows in @model, if possible
 */
static gint
gda_capi_recordset_fetch_nb_rows (GdaDataSelect *model)
{
	GdaCapiRecordset *imodel;

	imodel = GDA_CAPI_RECORDSET (model);
	if (model->advertized_nrows >= 0)
		return model->advertized_nrows;

	/* use C API to determine number of rows,if possible */
	g_warning("imodel not used: %p", imodel); /* Avoids a compiler warning. */
	TO_IMPLEMENT;

	return model->advertized_nrows;
}

/*
 * Create a new filled #GdaRow object for the row at position @rownum, and put it into *prow.
 *
 * NOTES:
 * - @prow will NOT be NULL, but *prow WILL be NULL.
 * - a new #GdaRow object has to be created, corresponding to the @rownum row
 * - memory management for that new GdaRow object is left to the implementation, which
 *   can use gda_data_select_take_row() to "give" the GdaRow to @model (in this case
 *   this method won't be called anymore for the same @rownum), or may decide to
 *   keep a cache of GdaRow object and "recycle" them.
 * - implementing this method is MANDATORY if the data model supports random access
 * - this method is only called when data model is used in random access mode
 */
static gboolean 
gda_capi_recordset_fetch_random (GdaDataSelect *model, GdaRow **prow, gint rownum,
				 GError **error)
{
	/*GdaCapiRecordset *imodel = GDA_CAPI_RECORDSET (model);*/

	TO_IMPLEMENT;

	return TRUE;
}

/*
 * Create and "give" filled #GdaRow object for all the rows in the model
 */
#if 0
static gboolean
gda_capi_recordset_store_all (GdaDataSelect *model, GError **error)
{
	GdaCapiRecordset *imodel;
	gint i;

	imodel = GDA_CAPI_RECORDSET (model);

	/* default implementation */
	for (i = 0; i < model->advertized_nrows; i++) {
		GdaRow *prow;
		if (! gda_capi_recordset_fetch_random (model, &prow, i, error))
			return FALSE;
	}
	return TRUE;
}
#endif

/*
 * Create a new filled #GdaRow object for the next cursor row, and put it into *prow.
 *
 * NOTES:
 * - @prow will NOT be NULL, but *prow WILL be NULL.
 * - a new #GdaRow object has to be created, corresponding to the @rownum row
 * - memory management for that new GdaRow object is left to the implementation, which
 *   can use gda_data_select_take_row() to "give" the GdaRow to @model (in this case
 *   this method won't be called anymore for the same @rownum), or may decide to
 *   keep a cache of GdaRow object and "recycle" them.
 * - implementing this method is MANDATORY
 * - this method is only called when data model is used in cursor access mode
 */
static gboolean 
gda_capi_recordset_fetch_next (GdaDataSelect *model, GdaRow **prow,
			       gint rownum, GError **error)
{
	/* GdaCapiRecordset *imodel = (GdaCapiRecordset*) model; */

	TO_IMPLEMENT;

	return TRUE;
}

/*
 * Create a new filled #GdaRow object for the previous cursor row, and put it into *prow.
 *
 * NOTES:
 * - @prow will NOT be NULL, but *prow WILL be NULL.
 * - a new #GdaRow object has to be created, corresponding to the @rownum row
 * - memory management for that new GdaRow object is left to the implementation, which
 *   can use gda_data_select_take_row() to "give" the GdaRow to @model (in this case
 *   this method won't be called anymore for the same @rownum), or may decide to
 *   keep a cache of GdaRow object and "recycle" them.
 * - implementing this method is OPTIONAL (in this case the data model is assumed not to
 *   support moving iterators backward)
 * - this method is only called when data model is used in cursor access mode
 */
static gboolean 
gda_capi_recordset_fetch_prev (GdaDataSelect *model, GdaRow **prow,
			       gint rownum, GError **error)
{
	/* GdaCapiRecordset *imodel = (GdaCapiRecordset*) model; */

	TO_IMPLEMENT;

	return TRUE;
}

/*
 * Create a new filled #GdaRow object for the cursor row at position @rownum, and put it into *prow.
 *
 * NOTES:
 * - @prow will NOT be NULL, but *prow WILL be NULL.
 * - a new #GdaRow object has to be created, corresponding to the @rownum row
 * - memory management for that new GdaRow object is left to the implementation, which
 *   can use gda_data_select_take_row() to "give" the GdaRow to @model (in this case
 *   this method won't be called anymore for the same @rownum), or may decide to
 *   keep a cache of GdaRow object and "recycle" them.
 * - implementing this method is OPTIONAL and usefull only if there is a method quicker
 *   than iterating one step at a time to the correct position.
 * - this method is only called when data model is used in cursor access mode
 */
static gboolean 
gda_capi_recordset_fetch_at (GdaDataSelect *model, GdaRow **prow,
			     gint rownum, GError **error)
{
	/* GdaCapiRecordset *imodel = (GdaCapiRecordset*) model; */
	
	TO_IMPLEMENT;

	return TRUE;
}
