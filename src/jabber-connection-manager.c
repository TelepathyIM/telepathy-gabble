/* 
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * jabber-connection-manager.c- Source for JabberConnectionManager
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

#include "jabber-connection-manager.h"
#include "jabber-connection-manager-signals-marshal.h"

#include "jabber-connection-manager-glue.h"

G_DEFINE_TYPE(JabberConnectionManager, jabber_connection_manager, G_TYPE_OBJECT)

/*signal enum*/
enum
{
    NEW_CONNECTION,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};


static void
jabber_connection_manager_init (JabberConnectionManager *obj)
{
}

static void jabber_connection_manager_dispose (GObject *object);
static void jabber_connection_manager_finalize (GObject *object);

static void
jabber_connection_manager_class_init (JabberConnectionManagerClass *jabber_connection_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (jabber_connection_manager_class);

  object_class->dispose = jabber_connection_manager_dispose;
  object_class->finalize = jabber_connection_manager_finalize;


  signals[NEW_CONNECTION] = 
    g_signal_new ("new-connection",
                  G_OBJECT_CLASS_TYPE (jabber_connection_manager_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  jabber_connection_manager_marshal_VOID__STRING_BOXED_STRING,
                  G_TYPE_NONE, 3, G_TYPE_STRING, DBUS_TYPE_G_PROXY, G_TYPE_STRING);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (jabber_connection_manager_class), &dbus_glib_jabber_connection_manager_object_info);

}

void
jabber_connection_manager_dispose (GObject *object)
{
  JabberConnectionManager *jabber_connection_manager = JABBER_CONNECTION_MANAGER (object);
 
  /*do your stuff here*/
    
  if (G_OBJECT_CLASS (jabber_connection_manager_parent_class)->dispose)
    G_OBJECT_CLASS (jabber_connection_manager_parent_class)->dispose (object);
}

void
jabber_connection_manager_finalize (GObject *object)
{
  JabberConnectionManager *jabber_connection_manager = JABBER_CONNECTION_MANAGER (object);

  /* free any data held directly by the object here*/

  /* Chain up to the parent class */
  G_OBJECT_CLASS (jabber_connection_manager_parent_class)->finalize (object); 
 
}



/**
 * jabber_connection_manager_connect
 * 
 * Implememts DBus method Connect 
 * on interface org.freedesktop.Telepathy.ConnectionManager
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_manager_connect (JabberConnectionManager *obj, const gchar * proto, const GHashTable * parameters, gchar ** ret, gpointer* ret1, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_manager_get_parameter_defaults
 * 
 * Implememts DBus method GetParameterDefaults 
 * on interface org.freedesktop.Telepathy.ConnectionManager
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_manager_get_parameter_defaults (JabberConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_manager_get_optional_parameters
 * 
 * Implememts DBus method GetOptionalParameters 
 * on interface org.freedesktop.Telepathy.ConnectionManager
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_manager_get_optional_parameters (JabberConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_manager_get_mandatory_parameters
 * 
 * Implememts DBus method GetMandatoryParameters 
 * on interface org.freedesktop.Telepathy.ConnectionManager
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_manager_get_mandatory_parameters (JabberConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error)
{
  return TRUE;
}


/**
 * jabber_connection_manager_list_protocols
 * 
 * Implememts DBus method ListProtocols 
 * on interface org.freedesktop.Telepathy.ConnectionManager
 * 
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false
 * 
 * Returns: TRUE if sucessful, FALSE if an error was thrown
 */
gboolean jabber_connection_manager_list_protocols (JabberConnectionManager *obj, gchar *** ret, GError **error)
{
  return TRUE;
}

