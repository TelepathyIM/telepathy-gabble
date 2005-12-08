/*
 * gabble-connection-manager.c - Source for GabbleConnectionManager
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "gabble-connection-manager.h"
#include "gabble-connection-manager-signals-marshal.h"

#include "gabble-connection-manager-glue.h"

G_DEFINE_TYPE(GabbleConnectionManager, gabble_connection_manager, G_TYPE_OBJECT)

/* signal enum */
enum
{
    NEW_CONNECTION,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _GabbleConnectionManagerPrivate GabbleConnectionManagerPrivate;

struct _GabbleConnectionManagerPrivate
{
  gboolean dispose_has_run;
  GHashTable *connections;
};

#define GABBLE_CONNECTION_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_CONNECTION_TYPE_MANAGER, GabbleConnectionManagerPrivate))

static void
gabble_connection_manager_init (GabbleConnectionManager *obj)
{
  GabbleConnectionManagerPrivate *priv = GABBLE_CONNECTION_MANAGER_GET_PRIVATE (obj);

  priv->connections = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void gabble_connection_manager_dispose (GObject *object);
static void gabble_connection_manager_finalize (GObject *object);

static void
gabble_connection_manager_class_init (GabbleConnectionManagerClass *gabble_connection_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_connection_manager_class);

  g_type_class_add_private (gabble_connection_manager_class, sizeof (GabbleConnectionManagerPrivate));

  object_class->dispose = gabble_connection_manager_dispose;
  object_class->finalize = gabble_connection_manager_finalize;

  signals[NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_OBJECT_CLASS_TYPE (gabble_connection_manager_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_manager_marshal_VOID__STRING_BOXED_STRING,
                  G_TYPE_NONE, 3, G_TYPE_STRING, DBUS_TYPE_G_PROXY, G_TYPE_STRING);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_connection_manager_class), &dbus_glib_gabble_connection_manager_object_info);
}

void
gabble_connection_manager_dispose (GObject *object)
{
  GabbleConnectionManager *self = GABBLE_CONNECTION_MANAGER (object);
  GabbleConnectionManagerPrivate *priv = GABBLE_CONNECTION_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* do your stuff here */

  if (G_OBJECT_CLASS (gabble_connection_manager_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_connection_manager_parent_class)->dispose (object);
}

void
gabble_connection_manager_finalize (GObject *object)
{
  GabbleConnectionManager *self = GABBLE_CONNECTION_MANAGER (object);
  GabbleConnectionManagerPrivate *priv = GABBLE_CONNECTION_MANAGER_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  /* Chain up to the parent class */
  G_OBJECT_CLASS (gabble_connection_manager_parent_class)->finalize (object);
}



/**
 * gabble_connection_manager_connect
 *
 * Implements DBus method Connect
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_manager_connect (GabbleConnectionManager *obj, const gchar * proto, const GHashTable * parameters, gchar ** ret, gpointer* ret1, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_manager_get_parameter_defaults
 *
 * Implements DBus method GetParameterDefaults
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_manager_get_parameter_defaults (GabbleConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_manager_get_optional_parameters
 *
 * Implements DBus method GetOptionalParameters
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_manager_get_optional_parameters (GabbleConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_manager_get_mandatory_parameters
 *
 * Implements DBus method GetMandatoryParameters
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_manager_get_mandatory_parameters (GabbleConnectionManager *obj, const gchar * proto, GHashTable ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_manager_list_protocols
 *
 * Implements DBus method ListProtocols
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_manager_list_protocols (GabbleConnectionManager *obj, gchar *** ret, GError **error)
{
  static const char *protocols[] = {"jabber", "google-talk", NULL};

  *ret = g_strdupv((gchar **)protocols);

  return TRUE;
}

