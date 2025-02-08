/*
 * Copyright (C) 2007 - 2011 Vivien Malerba <malerba@gnome-db.org>
 * Copyright (C) 2008 Murray Cumming <murrayc@murrayc.com>
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
#include "gda-virtual-connection.h"
#include <gda-connection-private.h>
#include <libgda/gda-connection-internal.h>
#include <libgda/thread-wrapper/gda-thread-provider.h>

#define PARENT_TYPE GDA_TYPE_CONNECTION
#define CLASS(obj) (GDA_VIRTUAL_CONNECTION_CLASS (G_OBJECT_GET_CLASS (obj)))
#define PROV_CLASS(provider) (GDA_SERVER_PROVIDER_CLASS (G_OBJECT_GET_CLASS (provider)))

struct _GdaVirtualConnectionPrivate {
	gpointer              v_provider_data;
        GDestroyNotify        v_provider_data_destroy_func;
};


static void gda_virtual_connection_class_init (GdaVirtualConnectionClass *klass);
static void gda_virtual_connection_init       (GdaVirtualConnection *vcnc, GdaVirtualConnectionClass *klass);
static void gda_virtual_connection_finalize   (GObject *object);
static GObjectClass *parent_class = NULL;

/*
 * GdaVirtualConnection class implementation
 */
static void
conn_closed_cb (GdaVirtualConnection *vcnc)
{
	if (vcnc->priv->v_provider_data) {
                if (vcnc->priv->v_provider_data_destroy_func)
                        vcnc->priv->v_provider_data_destroy_func (vcnc->priv->v_provider_data);
                else
                        g_warning ("Provider did not clean its connection data");
                vcnc->priv->v_provider_data = NULL;
        }
}

static void
gda_virtual_connection_class_init (GdaVirtualConnectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	/* virtual methods */
	object_class->finalize = gda_virtual_connection_finalize;
	GDA_CONNECTION_CLASS (klass)->conn_closed = (void (*) (GdaConnection*)) conn_closed_cb;
}

static void
gda_virtual_connection_init (GdaVirtualConnection *vcnc, G_GNUC_UNUSED GdaVirtualConnectionClass *klass)
{
	vcnc->priv = g_new0 (GdaVirtualConnectionPrivate, 1);
	vcnc->priv->v_provider_data = NULL;
	vcnc->priv->v_provider_data_destroy_func = NULL;
}

static void
gda_virtual_connection_finalize (GObject *object)
{
	GdaVirtualConnection *vcnc = (GdaVirtualConnection *) object;

	g_return_if_fail (GDA_IS_VIRTUAL_CONNECTION (vcnc));

	/* free memory */
	g_free (vcnc->priv);
	vcnc->priv = NULL;

	/* chain to parent class */
	parent_class->finalize (object);
}

GType
gda_virtual_connection_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GMutex registering;
		static GTypeInfo info = {
			sizeof (GdaVirtualConnectionClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gda_virtual_connection_class_init,
			NULL, NULL,
			sizeof (GdaVirtualConnection),
			0,
			(GInstanceInitFunc) gda_virtual_connection_init,
			0
		};
			
		g_mutex_lock (&registering);
		if (type == 0)
			type = g_type_register_static (PARENT_TYPE, "GdaVirtualConnection", &info, G_TYPE_FLAG_ABSTRACT);
		g_mutex_unlock (&registering);
	}

	return type;
}

/**
 * gda_virtual_connection_open
 * @virtual_provider: a #GdaVirtualProvider object
 * @error: a place to store errors, or %NULL
 *
 * Creates and opens a new virtual connection using the @virtual_provider provider. The returned value
 * is a new #GdaVirtualConnection which needs to be used to actually add some contents to the virtual connection.
 *
 * Returns: a new #GdaConnection object, or %NULL if an error occurred
 */
GdaConnection *
gda_virtual_connection_open (GdaVirtualProvider *virtual_provider, GError **error)
{
	GdaConnection *cnc = NULL;
	g_return_val_if_fail (GDA_IS_VIRTUAL_PROVIDER (virtual_provider), NULL);

	if (PROV_CLASS (virtual_provider)->create_connection) {
		cnc = PROV_CLASS (virtual_provider)->create_connection ((GdaServerProvider*) virtual_provider);
		if (cnc) {
			g_object_set (G_OBJECT (cnc), "provider", virtual_provider, NULL);
			if (!gda_connection_open (cnc, error)) {
				g_object_unref (cnc);
				cnc = NULL;
			}
		}
	}
	else
		g_set_error (error, GDA_CONNECTION_ERROR, GDA_CONNECTION_PROVIDER_ERROR, "%s", 
			     _("Internal error: virtual provider does not implement the create_operation() virtual method"));
	return cnc;
}

/**
 * gda_virtual_connection_open_extended
 * @virtual_provider: a #GdaVirtualProvider object
 * @options: a set of options to specify the new connection
 * @error: a place to store errors, or %NULL
 *
 * Creates and opens a new virtual connection using the @virtual_provider provider. If @options
 * contains the %GDA_CONNECTION_OPTIONS_THREAD_ISOLATED flag, then the returned connection will be
 * a thread wrapped connection, and the actual (wrapped) virtual connection can be obtained through
 * the "gda-virtual-connection" user property (use g_object_get_data() to get it).
 *
 * The returned value is a new #GdaVirtualConnection which needs to be used to actually add some
 * contents to the virtual connection.
 *
 * Returns: a new #GdaConnection object, or %NULL if an error occurred
 */
GdaConnection *
gda_virtual_connection_open_extended (GdaVirtualProvider *virtual_provider, GdaConnectionOptions options, GError **error)
{
	GdaConnection *cnc = NULL;
	g_return_val_if_fail (GDA_IS_VIRTUAL_PROVIDER (virtual_provider), NULL);

	if (PROV_CLASS (virtual_provider)->create_connection) {
		cnc = PROV_CLASS (virtual_provider)->create_connection ((GdaServerProvider*) virtual_provider);
		if (cnc) {
			g_object_set (G_OBJECT (cnc), "provider", virtual_provider, 
				      "options",
				      options & (~ (GDA_CONNECTION_OPTIONS_THREAD_ISOLATED | GDA_CONNECTION_OPTIONS_THREAD_SAFE)), NULL);
			if (!gda_connection_open (cnc, error)) {
				g_object_unref (cnc);
				cnc = NULL;
			}
		}
	}
	else
		g_set_error (error, GDA_CONNECTION_ERROR, GDA_CONNECTION_PROVIDER_ERROR, "%s", 
			     _("Internal error: virtual provider does not implement the create_operation() virtual method"));

	if (cnc && (options & GDA_CONNECTION_OPTIONS_THREAD_ISOLATED)) {
		GdaConnection *wcnc;
		wcnc = _gda_thread_provider_handle_virtual_connection (GDA_THREAD_PROVIDER (_gda_connection_get_internal_thread_provider ()),
								       cnc);
		g_object_set_data (G_OBJECT (wcnc), "gda-virtual-connection", cnc);
		g_object_unref (cnc);
		cnc = wcnc;
	}

	return cnc;
}

/**
 * gda_virtual_connection_internal_set_provider_data
 * @vcnc: a #GdaConnection object
 * @data: an opaque structure, known only to the provider for which @vcnc is opened
 * @destroy_func: function to call when the connection closes and @data needs to be destroyed
 *
 * Note: calling this function more than once will not make it call @destroy_func on any previously
 * set opaque @data, you'll have to do it yourself.
 */
void
gda_virtual_connection_internal_set_provider_data (GdaVirtualConnection *vcnc, gpointer data, GDestroyNotify destroy_func)
{
        g_return_if_fail (GDA_IS_VIRTUAL_CONNECTION (vcnc));
        vcnc->priv->v_provider_data = data;
        vcnc->priv->v_provider_data_destroy_func = destroy_func;
}

/**
 * gda_virtual_connection_internal_get_provider_data
 * @vcnc: a #GdaConnection object
 *
 * Get the opaque pointer previously set using gda_virtual_connection_internal_set_provider_data().
 * If it's not set, then add a connection event and returns %NULL
 *
 * Returns: the pointer to the opaque structure set using gda_virtual_connection_internal_set_provider_data()
 */
gpointer
gda_virtual_connection_internal_get_provider_data (GdaVirtualConnection *vcnc)
{
	g_return_val_if_fail (GDA_IS_VIRTUAL_CONNECTION (vcnc), NULL);
        if (! vcnc->priv->v_provider_data)
                gda_connection_add_event_string (GDA_CONNECTION (vcnc), _("Internal error: invalid provider handle"));
        return vcnc->priv->v_provider_data;
}
