/*
 * Copyright (C) 2008 Massimo Cora <maxcvs@email.it>
 * Copyright (C) 2008 - 2011 Murray Cumming <murrayc@murrayc.com>
 * Copyright (C) 2008 - 2013 Vivien Malerba <malerba@gnome-db.org>
 * Copyright (C) 2009 Bas Driessen <bas.driessen@xobas.com>
 * Copyright (C) 2010 David King <davidk@openismus.com>
 * Copyright (C) 2010 Jonh Wendell <jwendell@gnome.org>
 * Copyright (C) 2011 Daniel Espinosa <despinosa@src.gnome.org>
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
#include <stdarg.h>
#include <string.h>
#include "gda-holder.h"
#include "gda-statement.h"
#include "gda-data-model.h"
#include "gda-data-handler.h"
#include "gda-marshal.h"
#include "gda-util.h"
#include <libgda.h>
#include <libgda/gda-attributes-manager.h>
#include <libgda/gda-custom-marshal.h>
#include <libgda/gda-types.h>

/* 
 * Main static functions 
 */
static void gda_holder_class_init (GdaHolderClass * class);
static void gda_holder_init (GdaHolder *holder);
static void gda_holder_dispose (GObject *object);
static void gda_holder_finalize (GObject *object);

static void gda_holder_set_property (GObject *object,
				     guint param_id,
				     const GValue *value,
				     GParamSpec *pspec);
static void gda_holder_get_property (GObject *object,
				     guint param_id,
				     GValue *value,
				     GParamSpec *pspec);

/* GdaLockable interface */
static void                 gda_holder_lockable_init (GdaLockableIface *iface);
static void                 gda_holder_lock      (GdaLockable *lockable);
static gboolean             gda_holder_trylock   (GdaLockable *lockable);
static void                 gda_holder_unlock    (GdaLockable *lockable);


static void bound_holder_changed_cb (GdaHolder *alias_of, GdaHolder *holder);
static void full_bound_holder_changed_cb (GdaHolder *alias_of, GdaHolder *holder);
static void gda_holder_set_full_bind (GdaHolder *holder, GdaHolder *alias_of);

/* get a pointer to the parents to be able to call their destructor */
static GObjectClass  *parent_class = NULL;
GdaAttributesManager *gda_holder_attributes_manager;

/* signals */
enum
{
	CHANGED,
        SOURCE_CHANGED,
	VALIDATE_CHANGE,
	ATT_CHANGED,
        LAST_SIGNAL
};

static gint gda_holder_signals[LAST_SIGNAL] = { 0, 0, 0, 0 };


/* properties */
enum
{
        PROP_0,
	PROP_ID,
	PROP_NAME,
	PROP_DESCR,
	PROP_SIMPLE_BIND,
	PROP_FULL_BIND,
	PROP_SOURCE_MODEL,
	PROP_SOURCE_COLUMN,
	PROP_GDA_TYPE,
	PROP_NOT_NULL,
	PROP_VALIDATE_CHANGES
};


struct _GdaHolderPrivate
{
	gchar           *id;

	GType            g_type;
	GdaHolder       *full_bind;     /* FULL bind to holder */
	GdaHolder       *simple_bind;  /* SIMPLE bind to holder */
	gulong           simple_bind_type_changed_id;
	
	gboolean         invalid_forced;
	GError          *invalid_error;
	gboolean         valid;
	gboolean         is_freeable;

	GValue           *value;
	GValue          *default_value; /* CAN be either NULL or of any type */
	gboolean         default_forced;
	gboolean         not_null;      /* TRUE if 'value' must not be NULL when passed to destination fields */

	GdaDataModel    *source_model;
	gint             source_col;

	GdaMutex        *mutex;

	gboolean         validate_changes;
};

/* module error */
GQuark gda_holder_error_quark (void)
{
        static GQuark quark;
        if (!quark)
                quark = g_quark_from_static_string ("gda_holder_error");
        return quark;
}

GType
gda_holder_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GMutex registering;
		static const GTypeInfo info = {
			sizeof (GdaHolderClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gda_holder_class_init,
			NULL,
			NULL,
			sizeof (GdaHolder),
			0,
			(GInstanceInitFunc) gda_holder_init,
			0
		};

		static GInterfaceInfo lockable_info = {
                        (GInterfaceInitFunc) gda_holder_lockable_init,
			NULL,
                        NULL
                };
		
		g_mutex_lock (&registering);
		if (type == 0) {
			type = g_type_register_static (G_TYPE_OBJECT, "GdaHolder", &info, 0);
			g_type_add_interface_static (type, GDA_TYPE_LOCKABLE, &lockable_info);
		}
		g_mutex_unlock (&registering);
	}

	return type;
}

static gboolean
validate_change_accumulator (G_GNUC_UNUSED GSignalInvocationHint *ihint,
			   GValue *return_accu,
			   const GValue *handler_return,
			   G_GNUC_UNUSED gpointer data)
{
	GError *error;

	error = g_value_get_boxed (handler_return);
	g_value_set_boxed (return_accu, error);

	return error ? FALSE : TRUE; /* stop signal if error has been set */
}

static GError *
m_validate_change (G_GNUC_UNUSED GdaHolder *holder, G_GNUC_UNUSED const GValue *new_value)
{
	return NULL;
}

static void
holder_attribute_set_cb (GObject *obj, const gchar *att_name, const GValue *value,
			 G_GNUC_UNUSED gpointer data)
{
	g_signal_emit (obj, gda_holder_signals[ATT_CHANGED], 0, att_name, value);
}

static void
gda_holder_class_init (GdaHolderClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (class);

	/**
	 * GdaHolder::source-changed:
	 * @holder: the #GdaHolder
	 * 
	 * Gets emitted when the data model in which @holder's values should be has changed
	 */
	gda_holder_signals[SOURCE_CHANGED] =
                g_signal_new ("source-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdaHolderClass, source_changed),
                              NULL, NULL,
                              _gda_marshal_VOID__VOID, G_TYPE_NONE, 0);
	/**
	 * GdaHolder::changed:
	 * @holder: the #GdaHolder
	 * 
	 * Gets emitted when @holder's value has changed
	 */
	gda_holder_signals[CHANGED] =
                g_signal_new ("changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdaHolderClass, changed),
                              NULL, NULL,
                              _gda_marshal_VOID__VOID, G_TYPE_NONE, 0);
	/**
	 * GdaHolder::attribute-changed:
	 * @holder: the #GdaHolder
	 * @att_name: attribute's name
	 * @att_value: attribute's value
	 * 
	 * Gets emitted when any @holder's attribute has changed
	 */
	gda_holder_signals[ATT_CHANGED] =
                g_signal_new ("attribute-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdaHolderClass, att_changed),
                              NULL, NULL,
                              _gda_marshal_VOID__STRING_VALUE, G_TYPE_NONE, 2, 
			      G_TYPE_STRING, G_TYPE_VALUE);

	/**
	 * GdaHolder::validate-change:
	 * @holder: the object which received the signal
	 * @new_value: the proposed new value for @holder
	 * 
	 * Gets emitted when @holder is going to change its value. One can connect to
	 * this signal to control which values @holder can have (for example to implement some business rules)
	 *
	 * Returns: NULL if @holder is allowed to change its value to @new_value, or a #GError
	 * otherwise.
	 */
	gda_holder_signals[VALIDATE_CHANGE] =
		g_signal_new ("validate-change",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdaHolderClass, validate_change),
                              validate_change_accumulator, NULL,
                              _gda_marshal_ERROR__VALUE, G_TYPE_ERROR, 1, G_TYPE_VALUE);

        class->changed = NULL;
        class->source_changed = NULL;
        class->validate_change = m_validate_change;
	class->att_changed = NULL;

	/* virtual functions */
	object_class->dispose = gda_holder_dispose;
	object_class->finalize = gda_holder_finalize;

	/* Properties */
	object_class->set_property = gda_holder_set_property;
	object_class->get_property = gda_holder_get_property;
	g_object_class_install_property (object_class, PROP_ID,
					 g_param_spec_string ("id", NULL, "Holder's ID", NULL, 
							      (G_PARAM_READABLE | G_PARAM_WRITABLE)));
	g_object_class_install_property (object_class, PROP_NAME,
					 g_param_spec_string ("name", NULL, "Holder's name", NULL, 
							      (G_PARAM_READABLE | G_PARAM_WRITABLE)));
	g_object_class_install_property (object_class, PROP_DESCR,
					 g_param_spec_string ("description", NULL, "Holder's description", NULL, 
							      (G_PARAM_READABLE | G_PARAM_WRITABLE)));
	g_object_class_install_property (object_class, PROP_GDA_TYPE,
					 g_param_spec_gtype ("g-type", NULL, "Holder's GType", G_TYPE_NONE, 
							     (G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT)));
	g_object_class_install_property (object_class, PROP_NOT_NULL,
					 g_param_spec_boolean ("not-null", NULL, "Can the value holder be NULL?", FALSE,
							       (G_PARAM_READABLE | G_PARAM_WRITABLE)));
	g_object_class_install_property (object_class, PROP_SIMPLE_BIND,
					 g_param_spec_object ("simple-bind", NULL, 
							      "Make value holder follow other GdaHolder's changes", 
                                                               GDA_TYPE_HOLDER,
							       (G_PARAM_READABLE | G_PARAM_WRITABLE)));
	g_object_class_install_property (object_class, PROP_FULL_BIND,
					 g_param_spec_object ("full-bind", NULL,
							      "Make value holder follow other GdaHolder's changes "
							      "and the other way around", 
                                                               GDA_TYPE_HOLDER,
							       (G_PARAM_READABLE | G_PARAM_WRITABLE)));
	g_object_class_install_property (object_class, PROP_SOURCE_MODEL,
                                         g_param_spec_object ("source-model", NULL, "Data model among which the holder's "
							      "value should be",
							      GDA_TYPE_DATA_MODEL,
                                                               (G_PARAM_READABLE | G_PARAM_WRITABLE)));
        g_object_class_install_property (object_class, PROP_SOURCE_COLUMN,
                                         g_param_spec_int ("source-column", NULL, "Column number to use in coordination "
							   "with the source-model property",
							   0, G_MAXINT, 0,
							   (G_PARAM_READABLE | G_PARAM_WRITABLE)));

	/**
	 * GdaHolder:validate-changes:
	 *
	 * Defines if the "validate-change" signal gets emitted when
	 * the holder's value changes.
	 *
	 * Since: 5.2.0
	 */
	g_object_class_install_property (object_class, PROP_VALIDATE_CHANGES,
					 g_param_spec_boolean ("validate-changes", NULL, "Defines if the validate-change signal is emitted on value change", TRUE,
							       (G_PARAM_READABLE | G_PARAM_WRITABLE)));
	
	/* extra */
	gda_holder_attributes_manager = gda_attributes_manager_new (TRUE, holder_attribute_set_cb, NULL);
}

static void
gda_holder_lockable_init (GdaLockableIface *iface)
{
	iface->i_lock = gda_holder_lock;
	iface->i_trylock = gda_holder_trylock;
	iface->i_unlock = gda_holder_unlock;
}

static void
gda_holder_init (GdaHolder *holder)
{
	holder->priv = g_new0 (GdaHolderPrivate, 1);

	holder->priv->id = NULL;

	holder->priv->g_type = GDA_TYPE_NULL;
	holder->priv->full_bind = NULL;
	holder->priv->simple_bind = NULL;
	holder->priv->simple_bind_type_changed_id = 0;

	holder->priv->invalid_forced = FALSE;
	holder->priv->invalid_error = NULL;
	holder->priv->valid = TRUE;
	holder->priv->default_forced = FALSE;
	holder->priv->is_freeable = TRUE;
	holder->priv->value = NULL;
	holder->priv->default_value = NULL;

	holder->priv->not_null = FALSE;
	holder->priv->source_model = NULL;
	holder->priv->source_col = 0;

	holder->priv->mutex = gda_mutex_new ();

	holder->priv->validate_changes = TRUE;
}

/**
 * gda_holder_new:
 * @type: the #GType requested
 *
 * Creates a new holder of type @type
 *
 * Returns: a new #GdaHolder object
 */
GdaHolder *
gda_holder_new (GType type)
{
	return (GdaHolder*) g_object_new (GDA_TYPE_HOLDER, "g-type", type, NULL);
}

/**
 * gda_holder_copy:
 * @orig: a #GdaHolder object to copy
 *
 * Copy constructor.
 * 
 * Note1: if @orig is set with a static value (see gda_holder_take_static_value()) 
 * its copy will have a fresh new allocated GValue, so that user should free it when done.
 *
 * Returns: (transfer full): a new #GdaHolder object
 */
GdaHolder *
gda_holder_copy (GdaHolder *orig)
{
	GObject *obj;
	GdaHolder *holder;
	gboolean allok = TRUE;

	g_return_val_if_fail (orig && GDA_IS_HOLDER (orig), NULL);
	g_return_val_if_fail (orig->priv, NULL);

	gda_holder_lock ((GdaLockable*) orig);
	obj = g_object_new (GDA_TYPE_HOLDER, "g-type", orig->priv->g_type, NULL);
	holder = GDA_HOLDER (obj);

	if (orig->priv->id)
		holder->priv->id = g_strdup (orig->priv->id);

	if (orig->priv->full_bind)
		gda_holder_set_full_bind (holder, orig->priv->full_bind);
	if (orig->priv->simple_bind) 
		allok = gda_holder_set_bind (holder, orig->priv->simple_bind, NULL);
	
	if (allok && orig->priv->source_model) {
		/*g_print ("Source holder %p\n", holder);*/
		allok = gda_holder_set_source_model (holder, orig->priv->source_model, orig->priv->source_col,
						     NULL);
	}

	if (allok) {
		/* direct settings */
		holder->priv->invalid_forced = orig->priv->invalid_forced;
		if (orig->priv->invalid_error)
			holder->priv->invalid_error = g_error_copy (orig->priv->invalid_error);
		holder->priv->valid = orig->priv->valid;
		holder->priv->is_freeable = TRUE;
		holder->priv->default_forced = orig->priv->default_forced;	
		if (orig->priv->value)
			holder->priv->value = gda_value_copy (orig->priv->value);
		if (orig->priv->default_value)
			holder->priv->default_value = gda_value_copy (orig->priv->default_value);
		holder->priv->not_null = orig->priv->not_null;
		gda_attributes_manager_copy (gda_holder_attributes_manager, (gpointer) orig, gda_holder_attributes_manager, (gpointer) holder);

		GValue *att_value;
		g_value_set_boolean ((att_value = gda_value_new (G_TYPE_BOOLEAN)), holder->priv->default_forced);
		gda_holder_set_attribute_static (holder, GDA_ATTRIBUTE_IS_DEFAULT, att_value);
		gda_value_free (att_value);


		gda_holder_unlock ((GdaLockable*) orig);
		return holder;
	}
	else {
		g_warning ("Internal error: could not copy GdaHolder (please report a bug).");
		g_object_unref (holder);
		gda_holder_unlock ((GdaLockable*) orig);
		return NULL;
	}
}

/**
 * gda_holder_new_inline:
 * @type: a valid GLib type
 * @id: (allow-none): the id of the holder to create, or %NULL
 * @...: value to set
 *
 * Creates a new #GdaHolder object with an ID set to @id, of type @type, 
 * and containing the value passed as the last argument.
 *
 * Note that this function is a utility function and that only a limited set of types are supported. Trying
 * to use an unsupported type will result in a warning, and the returned value holder holding a safe default
 * value.
 *
 * Returns: a new #GdaHolder object
 */
GdaHolder *
gda_holder_new_inline (GType type, const gchar *id, ...)
{
	GdaHolder *holder;

	static GMutex serial_mutex;
	static guint serial = 0;

	holder = gda_holder_new (type);
	if (holder) {
		GValue *value;
		va_list ap;
		GError *lerror = NULL;

		if (id)
			holder->priv->id = g_strdup (id);
		else {
			g_mutex_lock (&serial_mutex);
			holder->priv->id = g_strdup_printf ("%d", serial++);
			g_mutex_unlock (&serial_mutex);
		}

		va_start (ap, id);
		value = gda_value_new (type);
		if (type == G_TYPE_BOOLEAN) 
			g_value_set_boolean (value, va_arg (ap, int));
                else if (type == G_TYPE_STRING)
			g_value_set_string (value, va_arg (ap, gchar *));
                else if (type == G_TYPE_OBJECT)
			g_value_set_object (value, va_arg (ap, gpointer));
		else if (type == G_TYPE_INT)
			g_value_set_int (value, va_arg (ap, gint));
		else if (type == G_TYPE_UINT)
			g_value_set_uint (value, va_arg (ap, guint));
		else if (type == GDA_TYPE_BINARY)
			gda_value_set_binary (value, va_arg (ap, GdaBinary *));
		else if (type == G_TYPE_INT64)
			g_value_set_int64 (value, va_arg (ap, gint64));
		else if (type == G_TYPE_UINT64)
			g_value_set_uint64 (value, va_arg (ap, guint64));
		else if (type == GDA_TYPE_SHORT)
			gda_value_set_short (value, va_arg (ap, int));
		else if (type == GDA_TYPE_USHORT)
			gda_value_set_ushort (value, va_arg (ap, guint));
		else if (type == G_TYPE_CHAR)
			g_value_set_schar (value, va_arg (ap, int));
		else if (type == G_TYPE_UCHAR)
			g_value_set_uchar (value, va_arg (ap, guint));
		else if (type == G_TYPE_FLOAT)
			g_value_set_float (value, va_arg (ap, double));
		else if (type == G_TYPE_DOUBLE)
			g_value_set_double (value, va_arg (ap, gdouble));
		else if (type == G_TYPE_GTYPE)
			g_value_set_gtype (value, va_arg (ap, GType));
		else if (type == G_TYPE_LONG)
			g_value_set_long (value, va_arg (ap, glong));
		else if (type == G_TYPE_ULONG)
			g_value_set_ulong (value, va_arg (ap, gulong));
		else if (type == GDA_TYPE_NUMERIC)
			gda_value_set_numeric (value, va_arg (ap, GdaNumeric *));
		else if (type == G_TYPE_DATE)
			g_value_set_boxed (value, va_arg (ap, GDate *));
		else {
			g_warning ("%s() does not handle values of type %s, value will not be assigned.",
				   __FUNCTION__, g_type_name (type));
			g_object_unref (holder);
			holder = NULL;
		}
		va_end (ap);

		if (holder && !gda_holder_set_value (holder, value, &lerror)) {
			g_warning (_("Unable to set holder's value: %s"),
				   lerror && lerror->message ? lerror->message : _("No detail"));
			if (lerror)
				g_error_free (lerror);
			g_object_unref (holder);
			holder = NULL;
		}
		gda_value_free (value);
	}

	return holder;
}

static void
gda_holder_dispose (GObject *object)
{
	GdaHolder *holder;

	holder = GDA_HOLDER (object);
	if (holder->priv) {
		gda_holder_set_bind (holder, NULL, NULL);
		gda_holder_set_full_bind (holder, NULL);

		if (holder->priv->source_model) {
			g_object_unref (holder->priv->source_model);
			holder->priv->source_model = NULL;
		}

		holder->priv->g_type = G_TYPE_INVALID;

		if (holder->priv->value) {			
			if (holder->priv->is_freeable)
				gda_value_free (holder->priv->value);
			holder->priv->value = NULL;
		}

		if (holder->priv->default_value) {
			gda_value_free (holder->priv->default_value);
			holder->priv->default_value = NULL;
		}

		if (holder->priv->invalid_error) {
			g_error_free (holder->priv->invalid_error);
			holder->priv->invalid_error = NULL;
		}
	}

	/* parent class */
	parent_class->dispose (object);
}

static void
gda_holder_finalize (GObject   * object)
{
	GdaHolder *holder;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDA_IS_HOLDER (object));

	holder = GDA_HOLDER (object);
	if (holder->priv) {
		g_free (holder->priv->id);

		gda_mutex_free (holder->priv->mutex);

		g_free (holder->priv);
		holder->priv = NULL;
	}

	/* parent class */
	parent_class->finalize (object);
}


static void 
gda_holder_set_property (GObject *object,
			 guint param_id,
			 const GValue *value,
			 GParamSpec *pspec)
{
	GdaHolder *holder;

	holder = GDA_HOLDER (object);
	if (holder->priv) {
		switch (param_id) {
		case PROP_ID:
			g_free (holder->priv->id);
			holder->priv->id = g_value_dup_string (value);
			break;
		case PROP_NAME:
			gda_holder_set_attribute_static (holder, GDA_ATTRIBUTE_NAME, value);
			break;
		case PROP_DESCR:
			gda_holder_set_attribute_static (holder, GDA_ATTRIBUTE_DESCRIPTION, value);
			break;
		case PROP_GDA_TYPE:
			if (holder->priv->g_type == GDA_TYPE_NULL) {
				holder->priv->g_type = g_value_get_gtype (value);
				g_object_notify ((GObject*) holder, "g-type");
			}
			else
				g_warning (_("The 'g-type' property cannot be changed"));
			break;
		case PROP_NOT_NULL: {
			gboolean not_null = g_value_get_boolean (value);
			if (not_null != holder->priv->not_null) {
				holder->priv->not_null = not_null;
				
				/* updating the holder's validity regarding the NULL value */
				if (!not_null && 
				    (!holder->priv->value || GDA_VALUE_HOLDS_NULL (holder->priv->value)))
					holder->priv->valid = TRUE;
				
				if (not_null && 
				    (!holder->priv->value || GDA_VALUE_HOLDS_NULL (holder->priv->value)))
					holder->priv->valid = FALSE;
				
				g_signal_emit (holder, gda_holder_signals[CHANGED], 0);
			}
			break;
		}
		case PROP_SIMPLE_BIND:
			if (!gda_holder_set_bind (holder, (GdaHolder*) g_value_get_object (value), NULL))
				g_warning ("Could not set the 'simple-bind' property");
			break;
		case PROP_FULL_BIND:
			gda_holder_set_full_bind (holder, (GdaHolder*) g_value_get_object (value));
			break;
		case PROP_SOURCE_MODEL: {
			GdaDataModel* ptr = g_value_get_object (value);
			g_return_if_fail (gda_holder_set_source_model (holder, 
								       (GdaDataModel *)ptr, -1, NULL));
			break;
                }
		case PROP_SOURCE_COLUMN:
			holder->priv->source_col = g_value_get_int (value);
			break;
		case PROP_VALIDATE_CHANGES:
			holder->priv->validate_changes = g_value_get_boolean (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
			break;
		}
	}
}

static void
gda_holder_get_property (GObject *object,
			 guint param_id,
			 GValue *value,
			 GParamSpec *pspec)
{
	GdaHolder *holder;
	const GValue *cvalue;

	holder = GDA_HOLDER (object);
	if (holder->priv) {
		switch (param_id) {
		case PROP_ID:
			g_value_set_string (value, holder->priv->id);
			break;
		case PROP_NAME:
			cvalue = gda_holder_get_attribute (holder, GDA_ATTRIBUTE_NAME);
			if (cvalue)
				g_value_set_string (value, g_value_get_string (cvalue));
			else
				g_value_set_string (value, holder->priv->id);
			break;
		case PROP_DESCR:
			cvalue = gda_holder_get_attribute (holder, GDA_ATTRIBUTE_DESCRIPTION);
			if (cvalue)
				g_value_set_string (value, g_value_get_string (cvalue));
			else
				g_value_set_string (value, NULL);
			break;
		case PROP_GDA_TYPE:
			g_value_set_gtype (value, holder->priv->g_type);
			break;
		case PROP_NOT_NULL:
			g_value_set_boolean (value, gda_holder_get_not_null (holder));
			break;
		case PROP_SIMPLE_BIND:
			g_value_set_object (value, (GObject*) holder->priv->simple_bind);
			break;
		case PROP_FULL_BIND:
			g_value_set_object (value, (GObject*) holder->priv->full_bind);
			break;
		case PROP_SOURCE_MODEL:
			g_value_set_object (value, (GObject*) holder->priv->source_model);
			break;
		case PROP_SOURCE_COLUMN:
			g_value_set_int (value, holder->priv->source_col);
			break;
		case PROP_VALIDATE_CHANGES:
			g_value_set_boolean (value, holder->priv->validate_changes);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
			break;
		}
	}
}

GType
gda_holder_get_g_type (GdaHolder *holder)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), G_TYPE_INVALID);
	g_return_val_if_fail (holder->priv, G_TYPE_INVALID);

	return holder->priv->g_type;
}

/**
 * gda_holder_get_id:
 * @holder: a #GdaHolder object
 *
 * Get the ID of @holder. The ID can be set using @holder's "id" property
 *
 * Returns: the ID (don't modify the string).
 */
const gchar *
gda_holder_get_id (GdaHolder *holder)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), NULL);
	g_return_val_if_fail (holder->priv, NULL);

	return holder->priv->id;
}


/**
 * gda_holder_get_value:
 * @holder: a #GdaHolder object
 *
 * Get the value held into the holder. If @holder is set to use its default value
 * and that default value is not of the same type as @holder, then %NULL is returned.
 *
 * If @holder is set to NULL, then the returned value is a #GDA_TYPE_NULL GValue.
 *
 * If @holder is invalid, then the returned value is %NULL.
 *
 * Returns: (allow-none) (transfer none): the value, or %NULL
 */
const GValue *
gda_holder_get_value (GdaHolder *holder)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), NULL);
	g_return_val_if_fail (holder->priv, NULL);

	if (holder->priv->full_bind)
		return gda_holder_get_value (holder->priv->full_bind);
	else {
		if (holder->priv->valid) {
			/* return default value if possible */
			if (holder->priv->default_forced) {
				g_assert (holder->priv->default_value);
				if (G_VALUE_TYPE (holder->priv->default_value) == holder->priv->g_type) 
					return holder->priv->default_value;
				else
					return NULL;
			}
			
			if (!holder->priv->value)
				holder->priv->value = gda_value_new_null ();
			return holder->priv->value;
		}
		else
			return NULL;
	}
}

/**
 * gda_holder_get_value_str:
 * @holder: a #GdaHolder object
 * @dh: (allow-none): a #GdaDataHandler to use, or %NULL
 *
 * Same functionality as gda_holder_get_value() except that it returns the value as a string
 * (the conversion is done using @dh if not %NULL, or the default data handler otherwise).
 *
 * Returns: (transfer full): the value, or %NULL
 */
gchar *
gda_holder_get_value_str (GdaHolder *holder, GdaDataHandler *dh)
{
	const GValue *current_val;

	g_return_val_if_fail (GDA_IS_HOLDER (holder), NULL);
	g_return_val_if_fail (holder->priv, NULL);

	gda_holder_lock ((GdaLockable*) holder);
	current_val = gda_holder_get_value (holder);
        if (!current_val || GDA_VALUE_HOLDS_NULL (current_val)) {
		gda_holder_unlock ((GdaLockable*) holder);
                return NULL;
	}
        else {
		gchar *retval = NULL;
                if (!dh)
			dh = gda_data_handler_get_default (holder->priv->g_type);
		if (dh)
                        retval = gda_data_handler_get_str_from_value (dh, current_val);
		gda_holder_unlock ((GdaLockable*) holder);
		return retval;
        }
}

static gboolean real_gda_holder_set_value (GdaHolder *holder, GValue *value, gboolean do_copy, GError **error);

/**
 * gda_holder_set_value:
 * @holder: a #GdaHolder object
 * @value: (allow-none): a value to set the holder to, or %NULL
 * @error: a place to store errors, or %NULL
 *
 * Sets the value within the holder. If @holder is an alias for another
 * holder, then the value is also set for that other holder.
 *
 * On success, the action of any call to gda_holder_force_invalid() is cancelled
 * as soon as this method is called (even if @holder's value does not actually change)
 * 
 * If the value is not different from the one already contained within @holder,
 * then @holder is not changed and no signal is emitted.
 *
 * Note1: the @value argument is treated the same way if it is %NULL or if it is a #GDA_TYPE_NULL value
 *
 * Note2: if @holder can't accept the @value value, then this method returns FALSE, and @holder will be left
 * in an invalid state.
 *
 * Note3: before the change is accepted by @holder, the "validate-change" signal will be emitted (the value
 * of which can prevent the change from happening) which can be connected to to have a greater control
 * of which values @holder can have, or implement some business rules.
 *
 * Returns: TRUE if value has been set
 */
gboolean
gda_holder_set_value (GdaHolder *holder, const GValue *value, GError **error)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	g_return_val_if_fail (holder->priv, FALSE);

	return real_gda_holder_set_value (holder, (GValue*) value, TRUE, error);
}

/**
 * gda_holder_set_value_str:
 * @holder: a #GdaHolder object
 * @dh: a #GdaDataHandler to use, or %NULL
 * @value: a value to set the holder to, as a string
 * @error: a place to store errors, or %NULL
 *
 * Same functionality as gda_holder_set_value() except that it uses a string representation
 * of the value to set, which will be converted into a GValue first (using default data handler if
 * @dh is %NULL).
 *
 * Note1: if @value is %NULL or is the "NULL" string, then @holder's value is set to %NULL.
 * Note2: if @holder can't accept the @value value, then this method returns FALSE, and @holder will be left
 * in an invalid state.
 *
 * Returns: TRUE if value has been set
 */
gboolean
gda_holder_set_value_str (GdaHolder *holder, GdaDataHandler *dh, const gchar *value, GError **error)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	g_return_val_if_fail (holder->priv, FALSE);
	g_return_val_if_fail (!dh || GDA_IS_DATA_HANDLER (dh), FALSE);

	if (!value || !g_ascii_strcasecmp (value, "NULL")) 
                return gda_holder_set_value (holder, NULL, error);
	else {
		GValue *gdaval = NULL;
		gboolean retval = FALSE;

		gda_holder_lock ((GdaLockable*) holder);
		if (!dh)
			dh = gda_data_handler_get_default (holder->priv->g_type);
		if (dh)
			gdaval = gda_data_handler_get_value_from_str (dh, value, holder->priv->g_type);
		
		if (gdaval)
			retval = real_gda_holder_set_value (holder, gdaval, FALSE, error);
		else
			g_set_error (error, GDA_HOLDER_ERROR, GDA_HOLDER_STRING_CONVERSION_ERROR,
				     _("Unable to convert string to '%s' type"), 
				     gda_g_type_to_string (holder->priv->g_type));
		gda_holder_unlock ((GdaLockable*) holder);
		return retval;
	}
}

/**
 * gda_holder_take_value:
 * @holder: a #GdaHolder object
 * @value: (transfer full): a value to set the holder to
 * @error: a place to store errors, or %NULL
 *
 * Sets the value within the holder. If @holder is an alias for another
 * holder, then the value is also set for that other holder.
 *
 * On success, the action of any call to gda_holder_force_invalid() is cancelled
 * as soon as this method is called (even if @holder's value does not actually change).
 * 
 * If the value is not different from the one already contained within @holder,
 * then @holder is not changed and no signal is emitted.
 *
 * Note1: if @holder can't accept the @value value, then this method returns FALSE, and @holder will be left
 * in an invalid state.
 *
 * Note2: before the change is accepted by @holder, the "validate-change" signal will be emitted (the value
 * of which can prevent the change from happening) which can be connected to to have a greater control
 * of which values @holder can have, or implement some business rules.
 *
 * Note3: if user previously set this holder with gda_holder_take_static_value () the GValue
 * stored internally will be forgiven and replaced by the @value. User should then
 * take care of the 'old' static GValue.
 *
 * Returns: TRUE if value has been set
 */
gboolean
gda_holder_take_value (GdaHolder *holder, GValue *value, GError **error)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	g_return_val_if_fail (holder->priv, FALSE);

	return real_gda_holder_set_value (holder, (GValue*) value, FALSE, error);
}

static gboolean
real_gda_holder_set_value (GdaHolder *holder, GValue *value, gboolean do_copy, GError **error)
{
	gboolean changed = TRUE;
	gboolean newvalid;
	const GValue *current_val;
	gboolean newnull;
	gboolean was_valid;
#define DEBUG_HOLDER
#undef DEBUG_HOLDER

	gda_holder_lock ((GdaLockable*) holder);
	was_valid = gda_holder_is_valid (holder);

	/* if the value has been set with gda_holder_take_static_value () you'll be able
	 * to change the value only with another call to real_gda_holder_set_value 
	 */
	if (!holder->priv->is_freeable) {
		gda_holder_unlock ((GdaLockable*) holder);
		g_warning (_("Can't use this method to set value because there is already a static value"));
		return FALSE;
	}
		
	/* holder will be changed? */
	newnull = !value || GDA_VALUE_HOLDS_NULL (value);
	current_val = gda_holder_get_value (holder);
	if (current_val == value)
		changed = FALSE;
	else if ((!current_val || GDA_VALUE_HOLDS_NULL ((GValue *)current_val)) && newnull)
		changed = FALSE;
	else if (value && current_val &&
		 (G_VALUE_TYPE (value) == G_VALUE_TYPE ((GValue *)current_val)))
		changed = gda_value_differ (value, (GValue *)current_val);
		
	/* holder's validity */
	newvalid = TRUE;
	if (newnull && holder->priv->not_null) {
		g_set_error (error, GDA_HOLDER_ERROR, GDA_HOLDER_VALUE_NULL_ERROR,
			     _("(%s): Holder does not allow NULL values"),
			     holder->priv->id);
		newvalid = FALSE;
		changed = TRUE;
	}
	else if (!newnull && (G_VALUE_TYPE (value) != holder->priv->g_type)) {
		g_set_error (error, GDA_HOLDER_ERROR, GDA_HOLDER_VALUE_TYPE_ERROR,
			     _("(%s): Wrong Holder value type, expected type '%s' when value's type is '%s'"),
			     holder->priv->id,
			     gda_g_type_to_string (holder->priv->g_type),
			     gda_g_type_to_string (G_VALUE_TYPE (value)));
		newvalid = FALSE;
		changed = TRUE;
	}

	if (was_valid != newvalid)
		changed = TRUE;

#ifdef DEBUG_HOLDER
	g_print ("Holder to change %p (%s): value %s --> %s \t(type %d -> %d) VALID: %d->%d CHANGED: %d\n", 
		 holder, holder->priv->id,
		 gda_value_stringify ((GValue *)current_val),
		 gda_value_stringify ((value)),
		 current_val ? G_VALUE_TYPE ((GValue *)current_val) : 0,
		 value ? G_VALUE_TYPE (value) : 0, 
		 was_valid, newvalid, changed);
#endif

	/* end of procedure if the value has not been changed, after calculating the holder's validity */
	if (!changed) {
		if (!do_copy && value)
			gda_value_free (value);
		holder->priv->invalid_forced = FALSE;
		if (holder->priv->invalid_error) {
			g_error_free (holder->priv->invalid_error);
			holder->priv->invalid_error = NULL;
		}
		holder->priv->valid = newvalid;
		gda_holder_unlock ((GdaLockable*) holder);
		return TRUE;
	}

	/* check if we are allowed to change value */
	if (holder->priv->validate_changes) {
		GError *lerror = NULL;
		g_signal_emit (holder, gda_holder_signals[VALIDATE_CHANGE], 0, value, &lerror);
		if (lerror) {
			/* change refused by signal callback */
#ifdef DEBUG_HOLDER
			g_print ("Holder change refused %p (ERROR %s)\n", holder,
				 lerror->message);
#endif
			g_propagate_error (error, lerror);
			if (!do_copy) 
				gda_value_free (value);
			gda_holder_unlock ((GdaLockable*) holder);
			return FALSE;
		}
	}

	/* new valid status */
	holder->priv->invalid_forced = FALSE;
	if (holder->priv->invalid_error) {
		g_error_free (holder->priv->invalid_error);
		holder->priv->invalid_error = NULL;
	}
	holder->priv->valid = newvalid;
	/* we're setting a non-static value, so be sure to flag is as freeable */
	holder->priv->is_freeable = TRUE;

	/* check is the new value is the default one */
	holder->priv->default_forced = FALSE;
	if (holder->priv->default_value) {
		if ((G_VALUE_TYPE (holder->priv->default_value) == GDA_TYPE_NULL) && newnull)
			holder->priv->default_forced = TRUE;
		else if ((G_VALUE_TYPE (holder->priv->default_value) == holder->priv->g_type) &&
			 value && (G_VALUE_TYPE (value) == holder->priv->g_type))
			holder->priv->default_forced = !gda_value_compare (holder->priv->default_value, value);
	}
	GValue att_value = {0};
	g_value_init (&att_value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&att_value, holder->priv->default_forced);
	gda_holder_set_attribute_static (holder, GDA_ATTRIBUTE_IS_DEFAULT, &att_value);

	/* real setting of the value */
	if (holder->priv->full_bind) {
#ifdef DEBUG_HOLDER
		g_print ("Holder %p is alias of holder %p => propagating changes to holder %p\n",
			 holder, holder->priv->full_bind, holder->priv->full_bind);
#endif
		gda_holder_unlock ((GdaLockable*) holder);
		return real_gda_holder_set_value (holder->priv->full_bind, value, do_copy, error);
	}
	else {
		if (holder->priv->value) {
			gda_value_free (holder->priv->value);
			holder->priv->value = NULL;
		}

		if (value) {
			if (newvalid) {
				if (do_copy)
					holder->priv->value = gda_value_copy (value);
				else
					holder->priv->value = value;
			}
			else if (!do_copy) 
				gda_value_free (value);
		}

		g_signal_emit (holder, gda_holder_signals[CHANGED], 0);
	}

	gda_holder_unlock ((GdaLockable*) holder);
	return newvalid;
}

static GValue *
real_gda_holder_set_const_value (GdaHolder *holder, const GValue *value, 
								 gboolean *value_changed, GError **error)
{
	gboolean changed = TRUE;
	gboolean newvalid;
	const GValue *current_val;
	GValue *value_to_return = NULL;
	gboolean newnull;
#define DEBUG_HOLDER
#undef DEBUG_HOLDER

#ifdef DEBUG_HOLDER
	gboolean was_valid = gda_holder_is_valid (holder);
#endif

	/* holder will be changed? */
	newnull = !value || GDA_VALUE_HOLDS_NULL (value);
	current_val = gda_holder_get_value (holder);
	if (current_val == value)
		changed = FALSE;
	else if ((!current_val || GDA_VALUE_HOLDS_NULL (current_val)) && newnull) 
		changed = FALSE;
	else if (value && current_val &&
		 (G_VALUE_TYPE (value) == G_VALUE_TYPE (current_val))) 
		changed = gda_value_differ (value, current_val);
		
	/* holder's validity */
	newvalid = TRUE;
	if (newnull && holder->priv->not_null) {
		g_set_error (error, GDA_HOLDER_ERROR, GDA_HOLDER_VALUE_NULL_ERROR,
			     _("(%s): Holder does not allow NULL values"),
			     holder->priv->id);
		newvalid = FALSE;
		changed = TRUE;
	}
	else if (!newnull && (G_VALUE_TYPE (value) != holder->priv->g_type)) {
		g_set_error (error, GDA_HOLDER_ERROR, GDA_HOLDER_VALUE_TYPE_ERROR,
			     _("(%s): Wrong value type: expected type '%s' when value's type is '%s'"),
			     holder->priv->id,
			     gda_g_type_to_string (holder->priv->g_type),
			     gda_g_type_to_string (G_VALUE_TYPE (value)));
		newvalid = FALSE;
		changed = TRUE;
	}

#ifdef DEBUG_HOLDER
	g_print ("Changed holder %p (%s): value %s --> %s \t(type %d -> %d) VALID: %d->%d CHANGED: %d\n", 
		 holder, holder->priv->id,
		 current_val ? gda_value_stringify ((GValue *)current_val) : "_NULL_",
		 value ? gda_value_stringify ((value)) : "_NULL_",
		 current_val ? G_VALUE_TYPE ((GValue *)current_val) : 0,
		 value ? G_VALUE_TYPE (value) : 0, 
		 was_valid, newvalid, changed);
#endif
	

	/* end of procedure if the value has not been changed, after calculating the holder's validity */
	if (!changed) {
		holder->priv->invalid_forced = FALSE;
		if (holder->priv->invalid_error) {
			g_error_free (holder->priv->invalid_error);
			holder->priv->invalid_error = NULL;
		}
		holder->priv->valid = newvalid;
#ifdef DEBUG_HOLDER		
		g_print ("Holder is not changed");
#endif		
		/* set the changed status */
		*value_changed = FALSE;
	}
	else {
		*value_changed = TRUE;
	}

	/* check if we are allowed to change value */
	if (holder->priv->validate_changes) {
		GError *lerror = NULL;
		g_signal_emit (holder, gda_holder_signals[VALIDATE_CHANGE], 0, value, &lerror);
		if (lerror) {
			/* change refused by signal callback */
			g_propagate_error (error, lerror);
			return NULL;
		}
	}

	/* new valid status */
	holder->priv->invalid_forced = FALSE;
	if (holder->priv->invalid_error) {
		g_error_free (holder->priv->invalid_error);
		holder->priv->invalid_error = NULL;
	}
	holder->priv->valid = newvalid;
	/* we're setting a static value, so be sure to flag is as unfreeable */
	holder->priv->is_freeable = FALSE;

	/* check is the new value is the default one */
	holder->priv->default_forced = FALSE;
	if (holder->priv->default_value) {
		if ((G_VALUE_TYPE (holder->priv->default_value) == GDA_TYPE_NULL) && newnull)
			holder->priv->default_forced = TRUE;
		else if ((G_VALUE_TYPE (holder->priv->default_value) == holder->priv->g_type) &&
			 value && (G_VALUE_TYPE (value) == holder->priv->g_type))
			holder->priv->default_forced = !gda_value_compare (holder->priv->default_value, value);
	}
	GValue *att_value;
	g_value_set_boolean ((att_value = gda_value_new (G_TYPE_BOOLEAN)), holder->priv->default_forced);
	gda_holder_set_attribute_static (holder, GDA_ATTRIBUTE_IS_DEFAULT, att_value);
	gda_value_free (att_value);

	/* real setting of the value */
	if (holder->priv->full_bind) {
#ifdef DEBUG_HOLDER
		g_print ("Holder %p is alias of holder %p => propagating changes to holder %p\n",
			 holder, holder->priv->full_bind, holder->priv->full_bind);
#endif
		return real_gda_holder_set_const_value (holder->priv->full_bind, value, 
												value_changed, error);
	}
	else {
		if (holder->priv->value) {
			if (G_IS_VALUE (holder->priv->value))
				value_to_return = holder->priv->value;
			else
				value_to_return = NULL;
			holder->priv->value = NULL;			
		}

		if (value) {
			if (newvalid) {
				holder->priv->value = (GValue*)value;
			}
		}

		g_signal_emit (holder, gda_holder_signals[CHANGED], 0);
	}

#ifdef DEBUG_HOLDER	
	g_print ("returning %p, wannabe was %p\n", value_to_return,
			 value);
#endif	
	
	return value_to_return;
}

/**
 * gda_holder_take_static_value:
 * @holder: a #GdaHolder object
 * @value: a const value to set the holder to
 * @value_changed: a boolean set with TRUE if the value changes, FALSE elsewhere.
 * @error: a place to store errors, or %NULL
 *
 * Sets the const value within the holder. If @holder is an alias for another
 * holder, then the value is also set for that other holder.
 *
 * The value will not be freed, and user should take care of it, either for its
 * freeing or for its correct value at the moment of query.
 * 
 * If the value is not different from the one already contained within @holder,
 * then @holder is not changed and no signal is emitted.
 *
 * Note1: if @holder can't accept the @value value, then this method returns NULL, and @holder will be left
 * in an invalid state.
 *
 * Note2: before the change is accepted by @holder, the "validate-change" signal will be emitted (the value
 * of which can prevent the change from happening) which can be connected to to have a greater control
 * of which values @holder can have, or implement some business rules.
 *
 * Returns: NULL if an error occurred or if the previous GValue was NULL itself. It returns
 * the static GValue user set previously, so that he can free it.
 */
GValue *
gda_holder_take_static_value (GdaHolder *holder, const GValue *value, gboolean *value_changed,
			      GError **error)
{
	GValue *retvalue;
	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	g_return_val_if_fail (holder->priv, FALSE);

	gda_holder_lock ((GdaLockable*) holder);
	retvalue = real_gda_holder_set_const_value (holder, value, value_changed, error);
	gda_holder_unlock ((GdaLockable*) holder);

	return retvalue;
}

/**
 * gda_holder_force_invalid:
 * @holder: a #GdaHolder object
 *
 * Forces a holder to be invalid; to set it valid again, a new value must be assigned
 * to it using gda_holder_set_value() or gda_holder_take_value().
 *
 * @holder's value is set to %NULL.
 */
void
gda_holder_force_invalid (GdaHolder *holder)
{
	g_return_if_fail (GDA_IS_HOLDER (holder));
	gda_holder_force_invalid_e (holder, NULL);
}

/**
 * gda_holder_force_invalid_e:
 * @holder: a #GdaHolder object
 * @error: (allow-none) (transfer full): a #GError explaining why @holder is declared invalid, or %NULL
 *
 * Forces a holder to be invalid; to set it valid again, a new value must be assigned
 * to it using gda_holder_set_value() or gda_holder_take_value().
 *
 * @holder's value is set to %NULL.
 *
 * Since: 4.2.10
 */
void
gda_holder_force_invalid_e (GdaHolder *holder, GError *error)
{
	g_return_if_fail (GDA_IS_HOLDER (holder));
	g_return_if_fail (holder->priv);

#ifdef GDA_DEBUG_NO
	g_print ("Holder %p (%s): declare invalid\n", holder, holder->priv->id);
#endif

	gda_holder_lock ((GdaLockable*) holder);
	if (holder->priv->invalid_error)
		g_error_free (holder->priv->invalid_error);
	holder->priv->invalid_error = error;

	if (holder->priv->invalid_forced) {
		gda_holder_unlock ((GdaLockable*) holder);
		return;
	}

	holder->priv->invalid_forced = TRUE;
	holder->priv->valid = FALSE;
	if (holder->priv->value) {
		if (holder->priv->is_freeable)
			gda_value_free (holder->priv->value);
		holder->priv->value = NULL;
	}

	/* if we are an alias, then we forward the value setting to the master */
	if (holder->priv->full_bind) 
		gda_holder_force_invalid (holder->priv->full_bind);
	else 
		g_signal_emit (holder, gda_holder_signals[CHANGED], 0);
	gda_holder_unlock ((GdaLockable*) holder);
}

/**
 * gda_holder_is_valid:
 * @holder: a #GdaHolder object
 *
 * Get the validity of @holder (that is, of the value held by @holder)
 *
 * Returns: TRUE if @holder's value can safely be used
 */
gboolean
gda_holder_is_valid (GdaHolder *holder)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	return gda_holder_is_valid_e (holder, NULL);
}

/**
 * gda_holder_is_valid_e:
 * @holder: a #GdaHolder object
 * @error: (allow-none): a place to store invalid error, or %NULL
 *
 * Get the validity of @holder (that is, of the value held by @holder)
 *
 * Returns: TRUE if @holder's value can safely be used
 *
 * Since: 4.2.10
 */
gboolean
gda_holder_is_valid_e (GdaHolder *holder, GError **error)
{
	gboolean retval;
	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	g_return_val_if_fail (holder->priv, FALSE);

	gda_holder_lock ((GdaLockable*) holder);
	if (holder->priv->full_bind)
		retval = gda_holder_is_valid_e (holder->priv->full_bind, error);
	else {
		if (holder->priv->invalid_forced) 
			retval = FALSE;
		else {
			if (holder->priv->default_forced) 
				retval = holder->priv->default_value ? TRUE : FALSE;
			else 
				retval = holder->priv->valid;
		}
		if (!retval && holder->priv->invalid_error)
			g_propagate_error (error,  g_error_copy (holder->priv->invalid_error));
	}
	gda_holder_unlock ((GdaLockable*) holder);
	return retval;
}

/**
 * gda_holder_set_value_to_default:
 * @holder: a #GdaHolder object
 *
 * Set @holder's value to its default value.
 *
 * Returns: TRUE if @holder has got a default value
 */
gboolean
gda_holder_set_value_to_default (GdaHolder *holder)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	g_return_val_if_fail (holder->priv, FALSE);

	gda_holder_lock ((GdaLockable*) holder);
	if (holder->priv->default_forced) {
		gda_holder_unlock ((GdaLockable*) holder);
		return TRUE;
	}

	if (!holder->priv->default_value) {
		gda_holder_unlock ((GdaLockable*) holder);
		return FALSE;
	}
	else {
		holder->priv->default_forced = TRUE;
		holder->priv->invalid_forced = FALSE;
		if (holder->priv->invalid_error) {
			g_error_free (holder->priv->invalid_error);
			holder->priv->invalid_error = NULL;
		}

		if (holder->priv->value) {
			if (holder->priv->is_freeable)
				gda_value_free (holder->priv->value);
			holder->priv->value = NULL;
		}
	}

	GValue *att_value;
	g_value_set_boolean ((att_value = gda_value_new (G_TYPE_BOOLEAN)), TRUE);
	gda_holder_set_attribute_static (holder, GDA_ATTRIBUTE_IS_DEFAULT, att_value);
	gda_value_free (att_value);
	g_signal_emit (holder, gda_holder_signals[CHANGED], 0);

	gda_holder_unlock ((GdaLockable*) holder);
	return TRUE;
}

/**
 * gda_holder_value_is_default:
 * @holder: a #GdaHolder object
 *
 * Tells if @holder's current value is the default one.
 *
 * Returns: TRUE if @holder @holder's current value is the default one
 */
gboolean
gda_holder_value_is_default (GdaHolder *holder)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	g_return_val_if_fail (holder->priv, FALSE);

	return holder->priv->default_forced;
}


/**
 * gda_holder_get_default_value:
 * @holder: a #GdaHolder object
 *
 * Get the default value held into the holder. WARNING: the default value does not need to be of 
 * the same type as the one required by @holder.
 *
 * Returns: the default value
 */
const GValue *
gda_holder_get_default_value (GdaHolder *holder)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), NULL);
	g_return_val_if_fail (holder->priv, NULL);

	return holder->priv->default_value;
}


/**
 * gda_holder_set_default_value:
 * @holder: a #GdaHolder object
 * @value: a value to set the holder's default value, or %NULL
 *
 * Sets the default value within the holder. If @value is %NULL then @holder won't have a
 * default value anymore. To set a default value to %NULL, then pass a #GValue created using
 * gda_value_new_null().
 *
 * NOTE: the default value does not need to be of the same type as the one required by @holder.
 */
void
gda_holder_set_default_value (GdaHolder *holder, const GValue *value)
{
	g_return_if_fail (GDA_IS_HOLDER (holder));
	g_return_if_fail (holder->priv);

	gda_holder_lock ((GdaLockable*) holder);
	if (holder->priv->default_value) {
		if (holder->priv->default_forced) {
			gda_holder_take_value (holder, holder->priv->default_value, NULL);
			holder->priv->default_forced = FALSE;
			holder->priv->default_value = NULL;
		}
		else {
			gda_value_free (holder->priv->default_value);
			holder->priv->default_value = NULL;
		}
	}

	holder->priv->default_forced = FALSE;
	if (value) {
		const GValue *current = gda_holder_get_value (holder);

		/* check if default is equal to current value */
		if (GDA_VALUE_HOLDS_NULL (value) &&
		    (!current || GDA_VALUE_HOLDS_NULL (current)))
			holder->priv->default_forced = TRUE;
		else if ((G_VALUE_TYPE (value) == holder->priv->g_type) &&
			 current && !gda_value_compare (value, current))
			holder->priv->default_forced = TRUE;

		holder->priv->default_value = gda_value_copy ((GValue *)value);
	}
	
	GValue *att_value;
	g_value_set_boolean ((att_value = gda_value_new (G_TYPE_BOOLEAN)), holder->priv->default_forced);
	gda_holder_set_attribute_static (holder, GDA_ATTRIBUTE_IS_DEFAULT, att_value);
	gda_value_free (att_value);

	/* don't emit the "changed" signal */
	gda_holder_unlock ((GdaLockable*) holder);
}

/**
 * gda_holder_set_not_null:
 * @holder: a #GdaHolder object
 * @not_null: TRUE if @holder should not accept %NULL values
 *
 * Sets if the holder can have a NULL value. If @not_null is TRUE, then that won't be allowed
 */
void
gda_holder_set_not_null (GdaHolder *holder, gboolean not_null)
{
	g_return_if_fail (GDA_IS_HOLDER (holder));
	g_return_if_fail (holder->priv);

	g_object_set (G_OBJECT (holder), "not-null", not_null, NULL);
}

/**
 * gda_holder_get_not_null:
 * @holder: a #GdaHolder object
 *
 * Get wether the holder can be NULL or not
 *
 * Returns: TRUE if the holder cannot be NULL
 */
gboolean
gda_holder_get_not_null (GdaHolder *holder)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	g_return_val_if_fail (holder->priv, FALSE);

	return holder->priv->not_null;
}

/**
 * gda_holder_set_source_model:
 * @holder: a #GdaHolder object
 * @model: a #GdaDataModel object or %NULL
 * @col: the reference column in @model
 * @error: location to store error, or %NULL
 *
 * Sets an hint that @holder's values should be restricted among the values
 * contained in the @col column of the @model data model. Note that this is just a hint,
 * meaning this policy is not enforced by @holder's implementation.
 *
 * If @model is %NULL, then the effect is to cancel ant previous call to gda_holder_set_source_model()
 * where @model was not %NULL.
 *
 * Returns: TRUE if no error occurred
 */
gboolean
gda_holder_set_source_model (GdaHolder *holder, GdaDataModel *model,
			     gint col, GError **error)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	g_return_val_if_fail (holder->priv, FALSE);
	if (model)
		g_return_val_if_fail (GDA_IS_DATA_MODEL (model), FALSE);

	/* No check is done on the validity of @col or even its existance */
	/* Note: for internal implementation if @col<0, then it's ignored */

	gda_holder_lock ((GdaLockable*) holder);
	if (model && (col >= 0)) {
		GType htype, ctype;
		GdaColumn *gcol;
		htype = gda_holder_get_g_type (holder);
		gcol = gda_data_model_describe_column (model, col);
		if (gcol) {
			ctype = gda_column_get_g_type (gcol);
			if ((htype != GDA_TYPE_NULL) && (ctype != GDA_TYPE_NULL) &&
			    (htype != ctype)) {
				g_set_error (error, GDA_HOLDER_ERROR, GDA_HOLDER_VALUE_TYPE_ERROR,
					     _("GdaHolder has a gda type (%s) incompatible with "
                                               "source column %d type (%s)"),
                                             gda_g_type_to_string (htype),
                                             col, gda_g_type_to_string (ctype));
				gda_holder_unlock ((GdaLockable*) holder);
				return FALSE;
			}
		}
	}

	if (col >= 0)
		holder->priv->source_col = col;

	if (holder->priv->source_model != model) {
		if (holder->priv->source_model) {
			g_object_unref (holder->priv->source_model);
			holder->priv->source_model = NULL;
		}
		
		holder->priv->source_model = model;
		if (model)
			g_object_ref (model);
		else
			holder->priv->source_col = 0;
	}

#ifdef GDA_DEBUG_signal
        g_print (">> 'SOURCE_CHANGED' from %p\n", holder);
#endif
	g_signal_emit (holder, gda_holder_signals[SOURCE_CHANGED], 0);
#ifdef GDA_DEBUG_signal
        g_print ("<< 'SOURCE_CHANGED' from %p\n", holder);
#endif

	gda_holder_unlock ((GdaLockable*) holder);
	return TRUE;
}


/**
 * gda_holder_get_source_model:
 * @holder: a #GdaHolder
 * @col: a place to store the column in the model sourcing the holder, or %NULL
 *
 * If gda_holder_set_source_model() has been used to provide a hint that @holder's value
 * should be among the values contained in a column of a data model, then this method
 * returns which data model, and if @col is not %NULL, then it is set to the restricting column
 * as well.
 *
 * Otherwise, this method returns %NULL, and if @col is not %NULL, then it is set to 0.
 *
 * Returns: (transfer none): a pointer to a #GdaDataModel, or %NULL
 */
GdaDataModel *
gda_holder_get_source_model (GdaHolder *holder, gint *col)
{
	GdaDataModel *model;
	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	g_return_val_if_fail (holder->priv, FALSE);

	gda_holder_lock ((GdaLockable*) holder);
	if (col)
		*col = holder->priv->source_col;
	model = holder->priv->source_model;
	gda_holder_unlock ((GdaLockable*) holder);
	return model;
}

/*
 * This callback is called when @holder->priv->simple_bind's GType was GDA_TYPE_NULL at the time
 * gda_holder_set_bind() was called, and it makes sure @holder's GType is the same as @holder->priv->simple_bind's
 */
static void
bind_to_notify_cb (GdaHolder *bind_to, G_GNUC_UNUSED GParamSpec *pspec, GdaHolder *holder)
{
	gda_holder_lock ((GdaLockable*) holder);
	gda_holder_lock ((GdaLockable*) bind_to);

	g_signal_handler_disconnect (holder->priv->simple_bind,
				     holder->priv->simple_bind_type_changed_id);
	holder->priv->simple_bind_type_changed_id = 0;
	if (holder->priv->g_type == GDA_TYPE_NULL) {
		holder->priv->g_type = bind_to->priv->g_type;
		g_object_notify ((GObject*) holder, "g-type");
	}
	else if (holder->priv->g_type != bind_to->priv->g_type) {
		/* break holder's binding because type differ */
		g_warning (_("Cannot bind holders if their type is not the same, "
			     "breaking existing bind where '%s' was bound to '%s'"),
			   gda_holder_get_id (holder), gda_holder_get_id (bind_to));
		gda_holder_set_bind (holder, NULL, NULL);
	}

	gda_holder_unlock ((GdaLockable*) holder);
	gda_holder_unlock ((GdaLockable*) bind_to);
}

/**
 * gda_holder_set_bind:
 * @holder: a #GdaHolder
 * @bind_to: a #GdaHolder or %NULL
 * @error: a place to store errors, or %NULL
 *
 * Sets @holder to change when @bind_to changes (and does not make @bind_to change when @holder changes).
 * For the operation to succeed, the GType of @holder and @bind_to must be the same, with the exception that
 * any of them can have a %GDA_TYPE_NULL type (in this situation, the GType of the two #GdaHolder objects
 * involved is set to match the other when any of them sets its type to something different than GDA_TYPE_NULL).
 *
 * If @bind_to is %NULL, then @holder will not be bound anymore.
 *
 * Returns: TRUE if no error occurred
 */
gboolean
gda_holder_set_bind (GdaHolder *holder, GdaHolder *bind_to, GError **error)
{
	const GValue *cvalue;
	GValue *value1 = NULL;
	const GValue *value2 = NULL;

	g_return_val_if_fail (GDA_IS_HOLDER (holder), FALSE);
	g_return_val_if_fail (holder->priv, FALSE);
	g_return_val_if_fail (holder != bind_to, FALSE);

	gda_holder_lock ((GdaLockable*) holder);
	if (holder->priv->simple_bind == bind_to) {
		gda_holder_unlock ((GdaLockable*) holder);
		return TRUE;
	}

	/* get a copy of the current values of @holder and @bind_to */
	if (bind_to) {
		g_return_val_if_fail (GDA_IS_HOLDER (bind_to), FALSE);
		g_return_val_if_fail (bind_to->priv, FALSE);
		
		if ((holder->priv->g_type != GDA_TYPE_NULL) &&
		    (bind_to->priv->g_type != GDA_TYPE_NULL) &&
		    (holder->priv->g_type != bind_to->priv->g_type)) {
			g_set_error (error, GDA_HOLDER_ERROR, GDA_HOLDER_VALUE_TYPE_ERROR,
				     "%s", _("Cannot bind holders if their type is not the same"));
			gda_holder_unlock ((GdaLockable*) holder);
			return FALSE;
		}
		value2 = gda_holder_get_value (bind_to);
	}

	cvalue = gda_holder_get_value (holder);
	if (cvalue)
		value1 = gda_value_copy ((GValue*)cvalue);

	/* get rid of the old alias */
	if (holder->priv->simple_bind) {
		g_signal_handlers_disconnect_by_func (holder->priv->simple_bind,
						      G_CALLBACK (bound_holder_changed_cb), holder);

		if (holder->priv->simple_bind_type_changed_id) {
			g_signal_handler_disconnect (holder->priv->simple_bind,
						     holder->priv->simple_bind_type_changed_id);
			holder->priv->simple_bind_type_changed_id = 0;
		}
		g_object_unref (holder->priv->simple_bind);
		holder->priv->simple_bind = NULL;
	}

	/* setting the new alias or reseting the value if there is no new alias */
	gboolean retval;
	if (bind_to) {
		holder->priv->simple_bind = g_object_ref (bind_to);
		g_signal_connect (holder->priv->simple_bind, "changed",
				  G_CALLBACK (bound_holder_changed_cb), holder);

		if (bind_to->priv->g_type == GDA_TYPE_NULL)
			holder->priv->simple_bind_type_changed_id = g_signal_connect (bind_to, "notify::g-type",
										      G_CALLBACK (bind_to_notify_cb),
										      holder);
		else if (holder->priv->g_type == GDA_TYPE_NULL)
			g_object_set ((GObject*) holder, "g-type", bind_to->priv->g_type , NULL);

		/* if bind_to has a different value than holder, then we set holder to the new value */
		if (value1)
			gda_value_free (value1);
		retval = gda_holder_set_value (holder, value2, error);
	}
	else
		retval = gda_holder_take_value (holder, value1, error);

	gda_holder_unlock ((GdaLockable*) holder);
	return retval;
}

/*
 * gda_holder_set_full_bind
 * @holder: a #GdaHolder
 * @alias_of: a #GdaHolder or %NULL
 *
 * Sets @holder to change when @alias_of changes and makes @alias_of change when @holder changes.
 * The difference with gda_holder_set_bind is that when @holder changes, then @alias_of also
 * changes.
 */
static void
gda_holder_set_full_bind (GdaHolder *holder, GdaHolder *alias_of)
{
	const GValue *cvalue;
	GValue *value1 = NULL, *value2 = NULL;

	g_return_if_fail (GDA_IS_HOLDER (holder));
	g_return_if_fail (holder->priv);

	gda_holder_lock ((GdaLockable*) holder);
	if (holder->priv->full_bind == alias_of) {
		gda_holder_unlock ((GdaLockable*) holder);
		return;
	}

	/* get a copy of the current values of @holder and @alias_of */
	if (alias_of) {
		g_return_if_fail (GDA_IS_HOLDER (alias_of));
		g_return_if_fail (alias_of->priv);
		g_return_if_fail (holder->priv->g_type == alias_of->priv->g_type);
		cvalue = gda_holder_get_value (alias_of);
		if (cvalue && !GDA_VALUE_HOLDS_NULL ((GValue*)cvalue))
			value2 = gda_value_copy ((GValue*)cvalue);
	}

	cvalue = gda_holder_get_value (holder);
	if (cvalue && !GDA_VALUE_HOLDS_NULL ((GValue*)cvalue))
		value1 = gda_value_copy ((GValue*)cvalue);
	
	/* get rid of the old alias */
	if (holder->priv->full_bind) {
		g_signal_handlers_disconnect_by_func (holder->priv->full_bind,
						      G_CALLBACK (full_bound_holder_changed_cb), holder);
		g_object_unref (holder->priv->full_bind);
		holder->priv->full_bind = NULL;
	}

	/* setting the new alias or reseting the value if there is no new alias */
	if (alias_of) {
		gboolean equal = FALSE;

		/* get rid of the internal holder's value */
		if (holder->priv->value) {
			if (holder->priv->is_freeable)
				gda_value_free (holder->priv->value);
			holder->priv->value = NULL;
		}

		holder->priv->full_bind = g_object_ref (alias_of);
		g_signal_connect (holder->priv->full_bind, "changed",
				  G_CALLBACK (full_bound_holder_changed_cb), holder);

		/* if alias_of has a different value than holder, then we emit a CHANGED signal */
		if (value1 && value2 &&
		    (G_VALUE_TYPE (value1) == G_VALUE_TYPE (value2)))
			equal = !gda_value_compare (value1, value2);
		else {
			if (!value1 && !value2)
				equal = TRUE;
		}

		if (!equal)
			g_signal_emit (holder, gda_holder_signals[CHANGED], 0);
	}
	else {
		/* restore the value that was in the previous alias holder, 
		 * if there was such a value, and don't emit a signal */
		g_assert (! holder->priv->value);
		if (value1)
			holder->priv->value = value1;
		value1 = NULL;
	}

	if (value1) gda_value_free (value1);
	if (value2) gda_value_free (value2);
	gda_holder_unlock ((GdaLockable*) holder);
}

static void
full_bound_holder_changed_cb (GdaHolder *alias_of, GdaHolder *holder)
{
	gda_holder_lock ((GdaLockable*) holder);
	gda_holder_lock ((GdaLockable*) alias_of);

	g_assert (alias_of == holder->priv->full_bind);
	g_signal_emit (holder, gda_holder_signals [CHANGED], 0);

	gda_holder_unlock ((GdaLockable*) holder);
	gda_holder_unlock ((GdaLockable*) alias_of);
}

static void
bound_holder_changed_cb (GdaHolder *alias_of, GdaHolder *holder)
{
	gda_holder_lock ((GdaLockable*) holder);
	gda_holder_lock ((GdaLockable*) alias_of);

	g_assert (alias_of == holder->priv->simple_bind);
	const GValue *cvalue;
	GError *lerror = NULL;
	cvalue = gda_holder_get_value (alias_of);
	if (! gda_holder_set_value (holder, cvalue, &lerror)) {
		if (lerror && ((lerror->domain != GDA_HOLDER_ERROR) || (lerror->code != GDA_HOLDER_VALUE_NULL_ERROR)))
			g_warning (_("Could not change GdaHolder to match value change in bound GdaHolder: %s"),
				   lerror && lerror->message ? lerror->message : _("No detail"));
		g_clear_error (&lerror);
	}
	gda_holder_unlock ((GdaLockable*) holder);
	gda_holder_unlock ((GdaLockable*) alias_of);
}

/**
 * gda_holder_get_bind:
 * @holder: a #GdaHolder
 *
 * Get the holder which makes @holder change its value when the holder's value is changed.
 *
 * Returns: (transfer none): the #GdaHolder or %NULL
 */
GdaHolder *
gda_holder_get_bind (GdaHolder *holder)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), NULL);
	g_return_val_if_fail (holder->priv, NULL);

	return holder->priv->simple_bind;
}

/**
 * gda_holder_get_alphanum_id:
 * @holder: a #GdaHolder object
 *
 * Get an "encoded" version of @holder's name. The "encoding" consists in replacing non
 * alphanumeric character with the string "__gdaXX" where XX is the hex. representation
 * of the non alphanumeric char.
 *
 * This method is just a wrapper around the gda_text_to_alphanum() function.
 *
 * Returns: (transfer full): a new string
 */
gchar *
gda_holder_get_alphanum_id (GdaHolder *holder)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), NULL);
	g_return_val_if_fail (holder->priv, NULL);
	return gda_text_to_alphanum (holder->priv->id);
}

/**
 * gda_holder_get_attribute:
 * @holder: a #GdaHolder
 * @attribute: attribute name as a string
 *
 * Get the value associated to a named attribute.
 *
 * Attributes can have any name, but Libgda proposes some default names, see <link linkend="libgda-40-Attributes-manager.synopsis">this section</link>.
 *
 * Returns: a read-only #GValue, or %NULL if not attribute named @attribute has been set for @holder
 */
const GValue *
gda_holder_get_attribute (GdaHolder *holder, const gchar *attribute)
{
	g_return_val_if_fail (GDA_IS_HOLDER (holder), NULL);
	/*g_print ("GdaHolder %p ATTR '%s' get => '%s'\n", holder, attribute, 
	  gda_value_stringify (gda_attributes_manager_get (gda_holder_attributes_manager, holder, attribute))); */
	return gda_attributes_manager_get (gda_holder_attributes_manager, holder, attribute);
}

/**
 * gda_holder_set_attribute:
 * @holder: a #GdaHolder
 * @attribute: attribute name
 * @value: a #GValue, or %NULL
 * @destroy: a function to be called when @attribute is not needed anymore, or %NULL
 *
 * Set the value associated to a named attribute. The @attribute string is 'stolen' by this method, and
 * the memory it uses will be freed using the @destroy function when no longer needed (if @destroy is %NULL,
 * then the string will not be freed at all).
 *
 * Attributes can have any name, but Libgda proposes some default names, 
 * see <link linkend="libgda-5.0-Attributes-manager.synopsis">this section</link>.
 *
 * For example one would use it as:
 *
 * <code>gda_holder_set_attribute (holder, g_strdup (my_attribute), my_value, g_free);</code>
 * <code>gda_holder_set_attribute (holder, GDA_ATTRIBUTE_NAME, my_value, NULL);</code>
 *
 * If there is already an attribute named @attribute set, then its value is replaced with the new value (@value is
 * copied), except if @value is %NULL, in which case the attribute is removed.
 */
void
gda_holder_set_attribute (GdaHolder *holder, const gchar *attribute, const GValue *value, GDestroyNotify destroy)
{
	const GValue *cvalue;
	g_return_if_fail (GDA_IS_HOLDER (holder));

	gda_holder_lock ((GdaLockable*) holder);
	cvalue = gda_attributes_manager_get (gda_holder_attributes_manager, holder, attribute);
	if ((value && cvalue && !gda_value_differ (cvalue, value)) ||
	    (!value && !cvalue)) {
		gda_holder_unlock ((GdaLockable*) holder);
		return;
	}

	gda_attributes_manager_set_full (gda_holder_attributes_manager, holder, attribute, value, destroy);
	//g_print ("GdaHolder %p ATTR '%s' set to '%s'\n", holder, attribute, gda_value_stringify (value)); 
	gda_holder_unlock ((GdaLockable*) holder);
}

static void
gda_holder_lock (GdaLockable *lockable)
{
	GdaHolder *holder = (GdaHolder *) lockable;
	gda_mutex_lock (holder->priv->mutex);
}

static gboolean
gda_holder_trylock (GdaLockable *lockable)
{
	GdaHolder *holder = (GdaHolder *) lockable;
	return gda_mutex_trylock (holder->priv->mutex);
}

static void
gda_holder_unlock (GdaLockable *lockable)
{
	GdaHolder *holder = (GdaHolder *) lockable;
	gda_mutex_unlock (holder->priv->mutex);
}
