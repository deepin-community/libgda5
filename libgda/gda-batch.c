/*
 * Copyright (C) 2000 - 2001 Reinhard Müller <reinhard@src.gnome.org>
 * Copyright (C) 2000 - 2003 Rodrigo Moya <rodrigo@gnome-db.org>
 * Copyright (C) 2001 - 2011 Vivien Malerba <malerba@gnome-db.org>
 * Copyright (C) 2002 - 2003 Gonzalo Paniagua Javier <gonzalo@gnome-db.org>
 * Copyright (C) 2010 David King <davidk@openismus.com>
 * Copyright (C) 2010 Jonh Wendell <jwendell@gnome.org>
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

#include <string.h>
#include <glib/gi18n-lib.h>
#include <libgda/gda-batch.h>
#include <libgda/gda-set.h>
#include <libgda/gda-holder.h>

/* 
 * Main static functions 
 */
static void gda_batch_class_init (GdaBatchClass *klass);
static void gda_batch_init (GdaBatch *batch);
static void gda_batch_dispose (GObject *object);
static void gda_batch_finalize (GObject *object);

static void gda_batch_set_property (GObject *object,
				    guint param_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void gda_batch_get_property (GObject *object,
				    guint param_id,
				    GValue *value,
				    GParamSpec *pspec);
/* get a pointer to the parents to be able to call their destructor */
static GObjectClass  *parent_class = NULL;

struct _GdaBatchPrivate {
	GSList *statements; /* list of GdaStatement objects */
};

/* signals */
enum
{
	CHANGED,
	LAST_SIGNAL
};

static gint gda_batch_signals[LAST_SIGNAL] = { 0 };

/* properties */
enum
{
	PROP_0,
};

/* module error */
GQuark gda_batch_error_quark (void)
{
	static GQuark quark;
	if (!quark)
		quark = g_quark_from_static_string ("gda_batch_error");
	return quark;
}

GType
gda_batch_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GMutex registering;
		static const GTypeInfo info = {
			sizeof (GdaBatchClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gda_batch_class_init,
			NULL,
			NULL,
			sizeof (GdaBatch),
			0,
			(GInstanceInitFunc) gda_batch_init,
			0
		};
		
		g_mutex_lock (&registering);
		if (type == 0)
			type = g_type_register_static (G_TYPE_OBJECT, "GdaBatch", &info, 0);
		g_mutex_unlock (&registering);
	}
	return type;
}

static void m_changed_cb (GdaBatch *batch, GdaStatement *changed_stmt);
static void
gda_batch_class_init (GdaBatchClass * klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	parent_class = g_type_class_peek_parent (klass);

	/**
	 * GdaBatch::changed:
	 * @batch: the #GdaBatch object
	 * @changed_stmt: the statement which has been changed
	 *
	 * Gets emitted whenever a #GdaStatement in the @batch object changes
	 */
	gda_batch_signals[CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (GdaBatchClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE,
			      1, G_TYPE_OBJECT);

	klass->changed = m_changed_cb;

	object_class->dispose = gda_batch_dispose;
	object_class->finalize = gda_batch_finalize;

	/* Properties */
	object_class->set_property = gda_batch_set_property;
	object_class->get_property = gda_batch_get_property;
}

static void
m_changed_cb (G_GNUC_UNUSED GdaBatch *batch, G_GNUC_UNUSED GdaStatement *changed_stmt)
{
	
}


static void
gda_batch_init (GdaBatch * batch)
{
	batch->priv = g_new0 (GdaBatchPrivate, 1);
	batch->priv->statements = NULL;
}

/**
 * gda_batch_new:
 *
 * Creates a new #GdaBatch object
 *
 * Returns: the new object
 */
GdaBatch*
gda_batch_new (void)
{
	GObject *obj;

	obj = g_object_new (GDA_TYPE_BATCH, NULL);
	return GDA_BATCH (obj);
}


/**
 * gda_batch_copy:
 * @orig: a #GdaBatch to make a copy of
 * 
 * Copy constructor
 *
 * Returns: (transfer full): a the new copy of @orig
 */
GdaBatch *
gda_batch_copy (GdaBatch *orig)
{
	GObject *obj;
	GdaBatch *batch;
	GSList *list;

	g_return_val_if_fail (GDA_IS_BATCH (orig), NULL);
	g_return_val_if_fail (orig->priv, NULL);

	obj = g_object_new (GDA_TYPE_BATCH, NULL);
	batch = (GdaBatch *) obj;
	for (list = orig->priv->statements; list; list = list->next) {
		GdaStatement *copy;

		copy = gda_statement_copy (GDA_STATEMENT (list->data));
		batch->priv->statements = g_slist_prepend (batch->priv->statements, copy);
	}
	batch->priv->statements = g_slist_reverse (batch->priv->statements);

	return batch;
}

static void
gda_batch_dispose (GObject *object)
{
	GdaBatch *batch;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDA_IS_BATCH (object));

	batch = GDA_BATCH (object);
	if (batch->priv) {
		if (batch->priv->statements) {
			g_slist_foreach (batch->priv->statements, (GFunc) g_object_unref, NULL);
			g_slist_free (batch->priv->statements);
			batch->priv->statements = NULL;
		}
	}

	/* parent class */
	parent_class->dispose (object);
}

static void
gda_batch_finalize (GObject *object)
{
	GdaBatch *batch;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDA_IS_BATCH (object));

	batch = GDA_BATCH (object);
	if (batch->priv) {
		g_free (batch->priv);
		batch->priv = NULL;
	}

	/* parent class */
	parent_class->finalize (object);
}


static void 
gda_batch_set_property (GObject *object,
			     guint param_id,
			     G_GNUC_UNUSED const GValue *value,
			     GParamSpec *pspec)
{
	GdaBatch *batch;

	batch = GDA_BATCH (object);
	if (batch->priv) {
		switch (param_id) {
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
			break;
		}
	}
}

static void
gda_batch_get_property (GObject *object,
			     guint param_id,
			     G_GNUC_UNUSED GValue *value,
			     GParamSpec *pspec)
{
	GdaBatch *batch;
	batch = GDA_BATCH (object);
	
	if (batch->priv) {
		switch (param_id) {
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
			break;
		}	
	}
}

static void
stmt_reset_cb (GdaStatement *stmt, GdaBatch *batch)
{
	g_signal_emit (batch, gda_batch_signals [CHANGED], 0, stmt);
}

/**
 * gda_batch_add_statement:
 * @batch: a #GdaBatch object
 * @stmt: a statement to add to @batch's statements list
 *
 * Add @stmt to the list of statements managed by @batch. A #GdaStatement object can be
 * added multiple times to a #GdaBatch object.
 */
void
gda_batch_add_statement (GdaBatch *batch, GdaStatement *stmt)
{
	g_return_if_fail (GDA_IS_BATCH (batch));
	g_return_if_fail (batch->priv);
	g_return_if_fail (GDA_IS_STATEMENT (stmt));

	g_signal_connect (G_OBJECT (stmt), "reset",
			  G_CALLBACK (stmt_reset_cb), batch);
	
	batch->priv->statements = g_slist_append (batch->priv->statements, stmt);
	g_object_ref (stmt);
}

/**
 * gda_batch_remove_statement:
 * @batch: a #GdaBatch object
 * @stmt: a statement to remove from @batch's statements list
 *
 * Removes @stmt from the list of statements managed by @batch. If @stmt is present several
 * times in @batch's statements' list, then only the first one is removed.
 */
void
gda_batch_remove_statement (GdaBatch *batch, GdaStatement *stmt)
{
	g_return_if_fail (GDA_IS_BATCH (batch));
	g_return_if_fail (batch->priv);
	g_return_if_fail (GDA_IS_STATEMENT (stmt));

	if (g_slist_index (batch->priv->statements, stmt) < 0) {
		g_warning (_("Statement could not be found in batch's statements"));
		return;
	}

	batch->priv->statements = g_slist_remove (batch->priv->statements, stmt);
	if (g_slist_index (batch->priv->statements, stmt) < 0) 
		/* @stmt is no more in @batch's list */
		g_signal_handlers_disconnect_by_func (G_OBJECT (stmt),
						      G_CALLBACK (stmt_reset_cb), batch);
	g_object_unref (stmt);
}


/**
 * gda_batch_serialize:
 * @batch: a #GdaBatch object
 *
 * Creates a string representing the contents of @batch.
 *
 * Returns: a string containing the serialized version of @batch
 */
gchar *
gda_batch_serialize (GdaBatch *batch)
{
	GSList *list;
	GString *string;
	gchar *str;

	g_return_val_if_fail (GDA_IS_BATCH (batch), NULL);
	g_return_val_if_fail (batch->priv, NULL);

	string = g_string_new ("{");
	g_string_append (string, "\"statements\":");
	if (batch->priv->statements) {
		g_string_append_c (string, '[');
		for (list = batch->priv->statements; list; list = list->next) {
			str = gda_statement_serialize (GDA_STATEMENT (list->data));
			if (list != batch->priv->statements)
				g_string_append_c (string, ',');
			g_string_append (string, str);
			g_free (str);
		}
		g_string_append_c (string, ']');
	}
	else
		g_string_append (string, "null");
	g_string_append_c (string, '}');

	str = string->str;
	g_string_free (string, FALSE);
	return str;
}

/**
 * gda_batch_get_statements:
 * @batch: a #GdaBatch object
 *
 * Get a list of the #GdaStatement objects contained in @batch
 *
 * Returns: (element-type GdaStatement) (transfer none): a list of #GdaStatement which should not be modified.
 */
const GSList *
gda_batch_get_statements (GdaBatch *batch)
{
	g_return_val_if_fail (GDA_IS_BATCH (batch), NULL);
	g_return_val_if_fail (batch->priv, NULL);

	return batch->priv->statements;
}

/**
 * gda_batch_get_parameters:
 * @batch: a #GdaBatch object
 * @out_params: (out) (transfer full) (allow-none): a place to store a new #GdaSet object, or %NULL
 * @error: a place to store errors, or %NULL
 *
 * Get a new #GdaSet object which groups all the execution parameters
 * which @batch needs for all the statements it includes.
 * This new object is returned though @out_params.
 *
 * Note that if @batch does not need any parameter, then @out_params is set to %NULL.
 *
 * Returns: TRUE if no error occurred.
 */
gboolean
gda_batch_get_parameters (GdaBatch *batch, GdaSet **out_params, GError **error)
{
	GdaSet *set = NULL;
	GSList *list;

	g_return_val_if_fail (GDA_IS_BATCH (batch), FALSE);
	g_return_val_if_fail (batch->priv, FALSE);

	if (out_params)
		*out_params = NULL;	

	if (!batch->priv->statements)
		return TRUE;

	for (list = batch->priv->statements; list; list = list->next) {
		GdaSet *tmpset = NULL;
		if (!gda_statement_get_parameters (GDA_STATEMENT (list->data), out_params ? &tmpset : NULL, error)) {
			if (tmpset)
				g_object_unref (tmpset);
			if (set)
				g_object_unref (set);
			return FALSE;
		}

		if (tmpset && tmpset->holders) {
			if (!set) {
				set = tmpset;
				tmpset = NULL;
			}
			else {
				/* merge @set and @tmp_set */
				GSList *holders;
				for (holders = tmpset->holders; holders; holders = holders->next) {
					GdaHolder *holder = (GdaHolder *) holders->data;
					if (! gda_set_add_holder (set, holder)) {
						GdaHolder *eholder = gda_set_get_holder (set, gda_holder_get_id (holder));
						if (!eholder ||
						    (gda_holder_get_g_type (eholder) != (gda_holder_get_g_type (holder)))) {
							/* error */
							g_set_error (error, GDA_BATCH_ERROR, GDA_BATCH_CONFLICTING_PARAMETER_ERROR,
								     _("Conflicting parameter '%s'"), gda_holder_get_id (holder));
							g_object_unref (tmpset);
							g_object_unref (set);
							return FALSE;
						}
					}
				}
			}
		}
		if (tmpset)
			g_object_unref (tmpset);
	}

	if (set) {
		if (out_params)
			*out_params = set;
		else
			g_object_unref (set);
	}
	return TRUE;
}
