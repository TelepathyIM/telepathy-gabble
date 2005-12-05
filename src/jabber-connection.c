/* 
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * jabber-connection.c - Source for JabberConnection
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "jabber-connection.h"
#include "jabber-connection-signals-marshal.h"

#include "jabber-connection-glue.h"

G_DEFINE_TYPE(JabberConnection, jabber_connection, G_TYPE_OBJECT)

/*signal enum*/
enum
{
    NEW_CHANNEL,
    STATUS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};


static void
jabber_connection_init (JabberConnection *obj)
{
}

static void jabber_connection_dispose (GObject *object);
static void jabber_connection_finalize (GObject *object);

static void
jabber_connection_class_init (JabberConnectionClass *jabber_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (jabber_connection_class);

  object_class->dispose = jabber_connection_dispose;
  object_class->finalize = jabber_connection_finalize;


  signals[NEW_CHANNEL] = 
    g_signal_new ("new-channel",
                  G_OBJECT_CLASS_TYPE (jabber_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  jabber_connection_marshal_VOID__BOXED_STRING_INT_INT_BOOLEAN,
                  G_TYPE_NONE, 5, DBUS_TYPE_G_PROXY, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN);

  signals[STATUS_CHANGED] = 
    g_signal_new ("status-changed",
                  G_OBJECT_CLASS_TYPE (jabber_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  jabber_connection_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (jabber_connection_class), &dbus_glib_jabber_connection_object_info);

}

void
jabber_connection_dispose (GObject *object)
{
  JabberConnection *jabber_connection = JABBER_CONNECTION (object);
 
  /*do your stuff here*/
    
  if (G_OBJECT_CLASS (jabber_connection_parent_class)->dispose)
    G_OBJECT_CLASS (jabber_connection_parent_class)->dispose (object);
}

void
jabber_connection_finalize (GObject *object)
{
  JabberConnection *jabber_connection = JABBER_CONNECTION (object);

  /* free any data held directly by the object here*/

  /* Chain up to the parent class */
  G_OBJECT_CLASS (jabber_connection_parent_class)->finalize (object); 
 
}



/**
 * jabber_connection_disconnect
 * 
 * Implememts DBus method Disconnect 
 * on interface org.freedesktop.Telepathy.Connection
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_disconnect (JabberConnection *obj, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_inspect_handle
 * 
 * Implememts DBus method InspectHandle 
 * on interface org.freedesktop.Telepathy.Connection
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_inspect_handle (JabberConnection *obj, guint handle_type, guint handle, gchar ** ret, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_release_handle
 * 
 * Implememts DBus method ReleaseHandle 
 * on interface org.freedesktop.Telepathy.Connection
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_release_handle (JabberConnection *obj, guint handle_type, guint handle, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_request_channel
 * 
 * Implememts DBus method RequestChannel 
 * on interface org.freedesktop.Telepathy.Connection
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_request_channel (JabberConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean supress_handler, gpointer* ret, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_request_handle
 * 
 * Implememts DBus method RequestHandle 
 * on interface org.freedesktop.Telepathy.Connection
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_request_handle (JabberConnection *obj, guint handle_type, const gchar * name, guint* ret, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_get_protocol
 * 
 * Implememts DBus method GetProtocol 
 * on interface org.freedesktop.Telepathy.Connection
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_get_protocol (JabberConnection *obj, gchar ** ret, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_get_status
 * 
 * Implememts DBus method GetStatus 
 * on interface org.freedesktop.Telepathy.Connection
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_get_status (JabberConnection *obj, guint* ret, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_list_channels
 * 
 * Implememts DBus method ListChannels 
 * on interface org.freedesktop.Telepathy.Connection
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_list_channels (JabberConnection *obj, gpointer* ret, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_hold_handle
 * 
 * Implememts DBus method HoldHandle 
 * on interface org.freedesktop.Telepathy.Connection
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_hold_handle (JabberConnection *obj, guint handle_type, guint handle, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_get_self_handle
 * 
 * Implememts DBus method GetSelfHandle 
 * on interface org.freedesktop.Telepathy.Connection
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_get_self_handle (JabberConnection *obj, guint* ret, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_get_interfaces
 * 
 * Implememts DBus method GetInterfaces 
 * on interface org.freedesktop.Telepathy.Connection
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_get_interfaces (JabberConnection *obj, gchar *** ret, GError **error)
{
  return TRUE;
}

