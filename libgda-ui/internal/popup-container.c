/*
 * Copyright (C) 2009 - 2012 Vivien Malerba <malerba@gnome-db.org>
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

#include <string.h>
#include <gtk/gtk.h>
#include "popup-container.h"
#include <gdk/gdkkeysyms.h>

struct _PopupContainerPrivate {
	PopupContainerPositionFunc position_func;
};

static void popup_container_class_init (PopupContainerClass *klass);
static void popup_container_init       (PopupContainer *container,
				       PopupContainerClass *klass);
static void popup_container_dispose   (GObject *object);
static void popup_container_show   (GtkWidget *widget);
static void popup_container_hide   (GtkWidget *widget);

static GObjectClass *parent_class = NULL;

/*
 * PopupContainer class implementation
 */

static void
popup_container_class_init (PopupContainerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = popup_container_dispose;
	widget_class->show = popup_container_show;
	widget_class->hide = popup_container_hide;
}

static gboolean
delete_popup (G_GNUC_UNUSED GtkWidget *widget, PopupContainer *container)
{
        gtk_widget_hide (GTK_WIDGET (container));
        gtk_grab_remove (GTK_WIDGET (container));
        return TRUE;
}

static gboolean
key_press_popup (GtkWidget *widget, GdkEventKey *event, PopupContainer *container)
{
        if (event->keyval != GDK_KEY_Escape)
                return FALSE;

        g_signal_stop_emission_by_name (widget, "key-press-event");
        gtk_widget_hide (GTK_WIDGET (container));
        gtk_grab_remove (GTK_WIDGET (container));
        return TRUE;
}

static gboolean
button_press_popup (GtkWidget *widget, GdkEventButton *event, PopupContainer *container)
{
        GtkWidget *child;

        child = gtk_get_event_widget ((GdkEvent *) event);

        /* We don't ask for button press events on the grab widget, so
         *  if an event is reported directly to the grab widget, it must
         *  be on a window outside the application (and thus we remove
         *  the popup window). Otherwise, we check if the widget is a child
         *  of the grab widget, and only remove the popup window if it
         *  is not.
         */
        if (child != widget) {
                while (child) {
                        if (child == widget)
                                return FALSE;
                        child = gtk_widget_get_parent (child);
                }
        }
        gtk_widget_hide (GTK_WIDGET (container));
	gtk_grab_remove (GTK_WIDGET (container));
        return TRUE;
}

static void
popup_container_init (PopupContainer *container, G_GNUC_UNUSED PopupContainerClass *klass)
{
	container->priv = g_new0 (PopupContainerPrivate, 1);
	container->priv->position_func = NULL;

	gtk_widget_set_events (GTK_WIDGET (container),
			       gtk_widget_get_events (GTK_WIDGET (container)) | GDK_KEY_PRESS_MASK);
	gtk_window_set_resizable (GTK_WINDOW (container), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (container), 5);
	g_signal_connect (G_OBJECT (container), "delete-event",
			  G_CALLBACK (delete_popup), container);
	g_signal_connect (G_OBJECT (container), "key-press-event",
			  G_CALLBACK (key_press_popup), container);
	g_signal_connect (G_OBJECT (container), "button-press-event",
			  G_CALLBACK (button_press_popup), container);

}

/* FIXME:
 *  - implement the show() virtual method with popup_grab_on_window()...
 *  - implement the position_popup()
 */

static void
popup_container_dispose (GObject *object)
{
	PopupContainer *container = (PopupContainer *) object;

	/* free memory */
	if (container->priv) {
		g_free (container->priv);
		container->priv = NULL;
	}

	parent_class->dispose (object);
}

static void
default_position_func (G_GNUC_UNUSED PopupContainer *container, gint *out_x, gint *out_y)
{
	GdkDeviceManager *manager;
	GdkDevice *pointer;
	GtkWidget *widget;
	widget = GTK_WIDGET (container);
	manager = gdk_display_get_device_manager (gtk_widget_get_display (widget));
	pointer = gdk_device_manager_get_client_pointer (manager);
	gdk_device_get_position (pointer, NULL, out_x, out_y);
}

static gboolean
popup_grab_on_window (GtkWidget *widget, guint32 activate_time)
{
	GdkDeviceManager *manager;
	GdkDevice *pointer;
	GdkWindow *window;
	window = gtk_widget_get_window (widget);
	manager = gdk_display_get_device_manager (gtk_widget_get_display (widget));
	pointer = gdk_device_manager_get_client_pointer (manager);
        if (gdk_device_grab (pointer, window, GDK_OWNERSHIP_WINDOW, TRUE,
			     GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			     GDK_POINTER_MOTION_MASK,
			     NULL, activate_time) == GDK_GRAB_SUCCESS) {
		GdkDevice *keyb;
		keyb = gdk_device_get_associated_device (pointer);
                if (gdk_device_grab (keyb, window, GDK_OWNERSHIP_WINDOW, TRUE,
				     GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK, NULL, activate_time) ==
		    GDK_GRAB_SUCCESS)
                         return TRUE;
                 else {
                        gdk_device_ungrab (pointer, activate_time);
			return FALSE;
		 }
        }
        return FALSE;
}

static void
popup_container_show (GtkWidget *widget)
{
	PopupContainer *container = (PopupContainer *) widget;
	gint x, y;

	GTK_WIDGET_CLASS (parent_class)->show (widget);
	if (container->priv->position_func)
		container->priv->position_func (container, &x, &y);
	else
		default_position_func (container, &x, &y);
	gtk_window_move (GTK_WINDOW (widget), x + 1, y + 1);
	gtk_window_move (GTK_WINDOW (widget), x, y);

	gtk_grab_add (widget);

	GdkScreen *screen;
        gint swidth, sheight;
        gint root_x, root_y;
        gint wwidth, wheight;
        gboolean do_move = FALSE;
        screen = gtk_window_get_screen (GTK_WINDOW (widget));
        if (screen) {
                swidth = gdk_screen_get_width (screen);
                sheight = gdk_screen_get_height (screen);
        }
        else {
                swidth = gdk_screen_width ();
                sheight = gdk_screen_height ();
        }
        gtk_window_get_position (GTK_WINDOW (widget), &root_x, &root_y);
        gtk_window_get_size (GTK_WINDOW (widget), &wwidth, &wheight);
        if (root_x + wwidth > swidth) {
                do_move = TRUE;
                root_x = swidth - wwidth;
        }
        else if (root_x < 0) {
                do_move = TRUE;
                root_x = 0;
        }
	if (root_y + wheight > sheight) {
                do_move = TRUE;
                root_y = sheight - wheight;
        }
        else if (root_y < 0) {
                do_move = TRUE;
                root_y = 0;
        }
        if (do_move)
                gtk_window_move (GTK_WINDOW (widget), root_x, root_y);

	popup_grab_on_window (widget,
                              gtk_get_current_event_time ());
}

static void
popup_container_hide (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (parent_class)->hide (widget);
	gtk_grab_remove (widget);
}

GType
popup_container_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo info = {
			sizeof (PopupContainerClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) popup_container_class_init,
			NULL,
			NULL,
			sizeof (PopupContainer),
			0,
			(GInstanceInitFunc) popup_container_init,
			0
		};

		type = g_type_from_name ("GdauiPopupContainer");
		if (!type)
			type = g_type_register_static (GTK_TYPE_WINDOW, "GdauiPopupContainer",
						       &info, 0);
	}
	return type;
}

static void
popup_position (PopupContainer *container, gint *out_x, gint *out_y)
{
	GtkWidget *poswidget;
	poswidget = g_object_get_data (G_OBJECT (container), "__poswidget");

	gint x, y;
        GtkRequisition req;

	gtk_widget_get_preferred_size (poswidget, NULL, &req);

	GtkAllocation alloc;
        gdk_window_get_origin (gtk_widget_get_window (poswidget), &x, &y);
	gtk_widget_get_allocation (poswidget, &alloc);
        x += alloc.x;
        y += alloc.y;
        y += alloc.height;

        if (x < 0)
                x = 0;

        if (y < 0)
                y = 0;

	*out_x = x;
	*out_y = y;
}

/**
 * popup_container_new_with_func
 *
 * Returns:
 */
GtkWidget *
popup_container_new (GtkWidget *position_widget)
{
	PopupContainer *container;
	g_return_val_if_fail (GTK_IS_WIDGET (position_widget), NULL);
	
	container = POPUP_CONTAINER (g_object_new (POPUP_CONTAINER_TYPE, "type", GTK_WINDOW_POPUP,
						   NULL));
	g_object_set_data (G_OBJECT (container), "__poswidget", position_widget);
	container->priv->position_func = popup_position;
	return (GtkWidget*) container;
}


/**
 * popup_container_new_with_func
 *
 * Returns:
 */
GtkWidget *
popup_container_new_with_func (PopupContainerPositionFunc pos_func)
{
	PopupContainer *container;

	container = POPUP_CONTAINER (g_object_new (POPUP_CONTAINER_TYPE, "type", GTK_WINDOW_POPUP,
						   NULL));

	container->priv->position_func = pos_func;
	return (GtkWidget*) container;
}
