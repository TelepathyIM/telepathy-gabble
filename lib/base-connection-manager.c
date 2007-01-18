/*
 * base-connection-manager.c - Source for TpBaseConnectionManager
 *
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007 Nokia Corporation
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

#include <telepathy-glib/base-connection-manager.h>
#include <telepathy-glib/connection-manager-service-iface.h>
#include <telepathy-glib/dbus.h>
#include "_gen/signals-marshal.h"

#define BUS_NAME_BASE    "org.freedesktop.Telepathy.ConnectionManager."
#define OBJECT_PATH_BASE "/org/freedesktop/Telepathy/ConnectionManager/"

static void service_iface_init (gpointer, gpointer);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE(TpBaseConnectionManager,
    tp_base_connection_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(TP_TYPE_CONNECTION_MANAGER_SERVICE_IFACE,
        service_iface_init))

#define TP_BASE_CONNECTION_MANAGER_GET_PRIVATE(obj) \
    ((TpBaseConnectionManagerPrivate *)obj->priv)

typedef struct _TpBaseConnectionManagerPrivate
{
  /* if TRUE, the object has gone away */
  gboolean dispose_has_run;
  /* used as a set: key is TpBaseConnection *, value is TRUE */
  GHashTable *connections;
} TpBaseConnectionManagerPrivate;

/* signal enum */
enum
{
    NEW_CONNECTION,
    NO_MORE_CONNECTIONS,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

static void
tp_base_connection_manager_dispose (GObject *object)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (object);
  TpBaseConnectionManagerPrivate *priv = 
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
}

static void
tp_base_connection_manager_finalize (GObject *object)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (object);
  TpBaseConnectionManagerPrivate *priv = 
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_hash_table_destroy(priv->connections);

  G_OBJECT_CLASS (tp_base_connection_manager_parent_class)->finalize (object);
}

static void
tp_base_connection_manager_class_init (TpBaseConnectionManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (TpBaseConnectionManagerPrivate));
  object_class->dispose = tp_base_connection_manager_dispose;
  object_class->finalize = tp_base_connection_manager_finalize;

  signals[NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _tp_marshal_VOID__STRING_STRING_STRING,
                  G_TYPE_NONE, 3, G_TYPE_STRING, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING);

  signals[NO_MORE_CONNECTIONS] =
    g_signal_new ("no-more-connections",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
tp_base_connection_manager_init (TpBaseConnectionManager *self)
{
  TpBaseConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      TP_TYPE_BASE_CONNECTION_MANAGER, TpBaseConnectionManagerPrivate);
  TpBaseConnectionManagerClass *cls =
    TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);

  (void)cls;

  self->priv = priv;

  priv->connections = g_hash_table_new (g_direct_hash, g_direct_equal);
}

/**
 * connection_disconnected_cb:
 * @conn: #GabbleConnection
 * @data: data passed in callback
 *
 * Signal handler called when a connection object disconnects.
 * When they become disconnected, we can unref and discard
 * them, and they will disappear from the bus.
 */
static void
connection_disconnected_cb (TpBaseConnection        *conn,
                            gpointer                 data)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (data);
  TpBaseConnectionManagerPrivate *priv = TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_assert (g_hash_table_lookup (priv->connections, conn));
  g_hash_table_remove (priv->connections, conn);

  g_object_unref (conn);

  g_debug ("%s: dereferenced connection", G_STRFUNC);
  if (g_hash_table_size (priv->connections) == 0)
    {
      g_signal_emit (self, signals[NO_MORE_CONNECTIONS], 0);
    }
}

static void
tp_base_connection_manager_get_parameters (TpConnectionManagerServiceIface *self,
                                           const gchar *proto,
                                           DBusGMethodInvocation *context)
{
  GError *error;

  g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED, "Not implemented");
  dbus_g_method_return_error(context, error);
}

static void
tp_base_connection_manager_list_protocols (TpConnectionManagerServiceIface *self,
                                           DBusGMethodInvocation *context)
{
  GError *error;

  g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED, "Not implemented");
  dbus_g_method_return_error(context, error);
}

/**
 * tp_base_connection_manager_request_connection
 *
 * Implements D-Bus method RequestConnection
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
void
tp_base_connection_manager_request_connection (TpConnectionManagerServiceIface *iface,
                                               const gchar *proto,
                                               GHashTable *parameters,
                                               DBusGMethodInvocation *context)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (iface);
  TpBaseConnectionManagerClass *cls =
    TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);
  TpBaseConnectionManagerPrivate *priv =
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *conn;
  gchar *bus_name;
  gchar *object_path;
  GError *error = NULL;

  g_assert (cls->new_connection);
  g_assert (cls->cm_dbus_name);
  conn = (cls->new_connection)(self, proto, parameters, &error);
  if (!conn)
    {
      dbus_g_method_return_error (context, error);
      return;
    }

  /* register on bus and save bus name and object path */
  if (!tp_base_connection_register ((TpBaseConnection *)conn,
        cls->cm_dbus_name, &bus_name, &object_path, &error))
    {
      g_debug ("%s failed: %s", G_STRFUNC, error->message);

      g_object_unref (G_OBJECT (conn));
      dbus_g_method_return_error (context, error);
      return;
    }

  /* bind to status change signals from the connection object */
  g_signal_connect (conn, "disconnected",
                             G_CALLBACK (connection_disconnected_cb),
                             self);

  /* store the connection, using a hash table as a set */
  g_hash_table_insert (priv->connections, conn, GINT_TO_POINTER(TRUE));

  /* emit the new connection signal */
  g_signal_emit (self, signals[NEW_CONNECTION], 0, bus_name, object_path, proto);

  tp_connection_manager_service_iface_return_from_request_connection (
      context, bus_name, object_path);
}

gboolean
tp_base_connection_manager_register (TpBaseConnectionManager *self)
{
  DBusGConnection *bus;
  DBusGProxy *bus_proxy;
  GError *error = NULL;
  guint request_name_result;
  TpBaseConnectionManagerClass *cls;
  GString *string;

  g_assert (TP_IS_BASE_CONNECTION_MANAGER (self));
  cls = TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);
  g_assert (cls->cm_dbus_name);

  bus = tp_get_bus ();
  bus_proxy = tp_get_bus_proxy ();

  string = g_string_new (BUS_NAME_BASE);
  g_string_append (string, cls->cm_dbus_name);

  if (!dbus_g_proxy_call (bus_proxy, "RequestName", &error,
                          G_TYPE_STRING, string->str,
                          G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                          G_TYPE_INVALID,
                          G_TYPE_UINT, &request_name_result,
                          G_TYPE_INVALID))
    g_error ("Failed to request bus name: %s", error->message);

  if (request_name_result == DBUS_REQUEST_NAME_REPLY_EXISTS)
    {
      g_warning ("Failed to acquire bus name, connection manager already running?");

      g_string_free (string, TRUE);
      return FALSE;
    }

  g_string_assign (string, OBJECT_PATH_BASE);
  g_string_append (string, cls->cm_dbus_name);
  dbus_g_connection_register_g_object (bus, string->str, G_OBJECT (self));

  g_string_free (string, TRUE);

  return TRUE;
}

static void
service_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpConnectionManagerServiceIfaceClass *klass = (TpConnectionManagerServiceIfaceClass *)g_iface;

  klass->get_parameters = tp_base_connection_manager_get_parameters;
  klass->list_protocols = tp_base_connection_manager_list_protocols;
  klass->request_connection = tp_base_connection_manager_request_connection;
}
