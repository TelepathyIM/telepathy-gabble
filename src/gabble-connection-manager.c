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
#include <dbus/dbus-protocol.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gabble-connection.h"
#include "telepathy-constants.h"
#include "telepathy-errors.h"
#include "telepathy-helpers.h"

#include "gabble-connection-manager.h"
#include "gabble-connection-manager-glue.h"
#include "gabble-connection-manager-signals-marshal.h"

#define BUS_NAME        "org.freedesktop.Telepathy.ConnectionManager.gabble"
#define OBJECT_PATH     "/org/freedesktop/Telepathy/ConnectionManager/gabble"

G_DEFINE_TYPE(GabbleConnectionManager, gabble_connection_manager, G_TYPE_OBJECT)

/* signal enum */
enum
{
    NEW_CONNECTION,
    NO_MORE_CONNECTIONS,
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

#define GABBLE_CONNECTION_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_CONNECTION_MANAGER, GabbleConnectionManagerPrivate))

/* type definition stuff */

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
                  gabble_connection_manager_marshal_VOID__STRING_STRING_STRING,
                  G_TYPE_NONE, 3, G_TYPE_STRING, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING);

  signals[NO_MORE_CONNECTIONS] =
    g_signal_new ("no-more-connections",
                  G_OBJECT_CLASS_TYPE (gabble_connection_manager_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

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

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_connection_manager_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_connection_manager_parent_class)->dispose (object);
}

void
gabble_connection_manager_finalize (GObject *object)
{
  GabbleConnectionManager *self = GABBLE_CONNECTION_MANAGER (object);
  GabbleConnectionManagerPrivate *priv = GABBLE_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_hash_table_destroy(priv->connections);

  G_OBJECT_CLASS (gabble_connection_manager_parent_class)->finalize (object);
}

/* private data */

typedef struct _GabbleParams GabbleParams;

struct _GabbleParams {
  guint set_mask;

  gchar *account;
  gchar *password;
  gchar *server;
  gchar *resource;
  gint priority;
  guint port;
  gboolean old_ssl;
  gboolean do_register;
  gboolean low_bandwidth;
  gchar *https_proxy_server;
  guint https_proxy_port;
  gchar *fallback_conference_server;
  gchar *stun_server;
  guint stun_port;
};

typedef struct _GabbleParamSpec GabbleParamSpec;

struct _GabbleParamSpec {
  const gchar *name;
  const gchar *dtype;
  const GType gtype;
  gboolean mandatory;
  const gpointer def;
  const gsize offset;
};

enum {
    JABBER_PARAM_ACCOUNT = 0,
    JABBER_PARAM_PASSWORD,
    JABBER_PARAM_SERVER,
    JABBER_PARAM_RESOURCE,
    JABBER_PARAM_PRIORITY,
    JABBER_PARAM_PORT,
    JABBER_PARAM_OLD_SSL,
    JABBER_PARAM_REGISTER,
    JABBER_PARAM_LOW_BANDWIDTH,
    JABBER_PARAM_HTTPS_PROXY_SERVER,
    JABBER_PARAM_HTTPS_PROXY_PORT,
    JABBER_PARAM_FALLBACK_CONFERENCE_SERVER,
    JABBER_PARAM_STUN_SERVER,
    JABBER_PARAM_STUN_PORT
};

static const GabbleParamSpec jabber_params[] = {
  { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TRUE, NULL, G_STRUCT_OFFSET(GabbleParams, account) },
  { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TRUE, NULL, G_STRUCT_OFFSET(GabbleParams, password) },
  { "server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, FALSE, NULL, G_STRUCT_OFFSET(GabbleParams, server) },
  { "resource", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, FALSE, GABBLE_PARAMS_DEFAULT_RESOURCE, G_STRUCT_OFFSET(GabbleParams, resource) },
  { "priority", DBUS_TYPE_INT16_AS_STRING, G_TYPE_INT, FALSE, GINT_TO_POINTER(0), G_STRUCT_OFFSET(GabbleParams, priority) },
  { "port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT, FALSE, GINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_PORT), G_STRUCT_OFFSET(GabbleParams, port) },
  { "old-ssl", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN, FALSE, GINT_TO_POINTER(FALSE), G_STRUCT_OFFSET(GabbleParams, old_ssl) },
  { "register", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN, FALSE, GINT_TO_POINTER(FALSE), G_STRUCT_OFFSET(GabbleParams, do_register) },
  { "low-bandwidth", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN, FALSE, GINT_TO_POINTER(FALSE), G_STRUCT_OFFSET(GabbleParams, low_bandwidth) },
  { "https-proxy-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, FALSE, NULL, G_STRUCT_OFFSET(GabbleParams, https_proxy_server) },
  { "https-proxy-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT, FALSE, GINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT), G_STRUCT_OFFSET(GabbleParams, https_proxy_port) },
  { "fallback-conference-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, FALSE, NULL, G_STRUCT_OFFSET(GabbleParams, fallback_conference_server) },
  { "stun-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, FALSE, NULL, G_STRUCT_OFFSET(GabbleParams, stun_server) },
  { "stun-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT, FALSE, GINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_STUN_PORT), G_STRUCT_OFFSET(GabbleParams, stun_port) },
  { NULL, NULL, 0, 0, NULL, 0 }
};

/* private methods */

static gboolean
get_parameters (const char *proto, const GabbleParamSpec **params, GError **error)
{
  if (!strcmp (proto, "jabber"))
    {
      *params = jabber_params;
    }
  else
    {
      g_debug ("%s: unknown protocol %s", G_STRFUNC, proto);

      *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                            "unknown protocol %s", proto);

      return FALSE;
    }

  return TRUE;
}

static gboolean
list_parameters (const GabbleParamSpec *params, gboolean mandatory, GHashTable **ret)
{
  int i;

  *ret = g_hash_table_new(g_str_hash, g_str_equal);

  for (i = 0; params[i].name; i++)
    {
      if (params[i].mandatory == mandatory)
        g_hash_table_insert (*ret, (gpointer) params[i].name, (gpointer) params[i].dtype);
    }

  return TRUE;
}

static gboolean
parameter_defaults (const GabbleParamSpec *params, GHashTable **ret)
{
  int i;

  *ret = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

  for (i = 0; params[i].name; i++)
    {
      GValue *value;

      if (params[i].mandatory)
        {
          g_assert (params[i].def == NULL);
          continue;
        }

      value = g_new0(GValue, 1);
      g_value_init (value, params[i].gtype);

      switch (params[i].dtype[0])
        {
          case DBUS_TYPE_STRING:
            g_value_set_static_string (value, (const gchar*) params[i].def);
            break;
          case DBUS_TYPE_INT16:
            g_value_set_int (value, GPOINTER_TO_INT (params[i].def));
            break;
          case DBUS_TYPE_UINT16:
            g_value_set_uint (value, GPOINTER_TO_INT (params[i].def));
            break;
          case DBUS_TYPE_BOOLEAN:
            g_value_set_boolean (value, GPOINTER_TO_INT (params[i].def));
            break;
          default:
            g_error ("parameter_defaults: encountered unknown type %s on argument %s",
                     params[i].dtype, params[i].name);
        }

      g_hash_table_insert (*ret, (gpointer) params[i].name, value);
    }

  return TRUE;
}

static gboolean
set_param_from_value (const GabbleParamSpec *paramspec,
                                     GValue *value,
                               GabbleParams *params,
                                    GError **error)
{
  if (G_VALUE_TYPE (value) != paramspec->gtype)
    {
      g_debug ("%s: expected type %s for parameter %s, got %s",
               G_STRFUNC,
               g_type_name (paramspec->gtype), paramspec->name,
               G_VALUE_TYPE_NAME (value));
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "expected type %s for account parameter %s, got %s",
                            g_type_name (paramspec->gtype), paramspec->name,
                            G_VALUE_TYPE_NAME (value));
      return FALSE;
    }

  switch (paramspec->dtype[0])
    {
      case DBUS_TYPE_STRING:
        {
          const char *str = g_value_get_string (value);
          if (!str || *str == '\0')
            return FALSE;
          else
            *((char **) ((void *)params + paramspec->offset)) = g_value_dup_string (value);
        }
        break;
      case DBUS_TYPE_INT16:
        *((gint *) ((void *)params + paramspec->offset)) = g_value_get_int (value);
        break;
      case DBUS_TYPE_UINT16:
        *((guint *) ((void *)params + paramspec->offset)) = g_value_get_uint (value);
        break;
      case DBUS_TYPE_BOOLEAN:
        *((gboolean *) ((void *)params + paramspec->offset)) = g_value_get_boolean (value);
        break;
      default:
        g_error ("set_param_from_value: encountered unknown type %s on argument %s",
                 paramspec->dtype, paramspec->name);
        return FALSE;
    }

  return TRUE;
}

static gboolean
parse_parameters (const GabbleParamSpec *paramspec,
                  GHashTable            *provided,
                  GabbleParams          *params,
                  GError               **error)
{
  int unhandled;
  int i;

  unhandled = g_hash_table_size (provided);

  for (i = 0; paramspec[i].name; i++)
    {
      GValue *value;

      value = g_hash_table_lookup (provided, paramspec[i].name);

      if (value == NULL)
        {
          if (paramspec[i].mandatory)
            {
              g_debug ("%s: missing mandatory param %s",
                       G_STRFUNC, paramspec[i].name);
              *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                    "missing mandatory account parameter %s",
                                    paramspec[i].name);
              return FALSE;
            }
          else
            {
              g_debug ("%s: using default value for param %s",
                       G_STRFUNC, paramspec[i].name);
            }
        }
      else
        {
          if (!set_param_from_value (&paramspec[i], value, params, error))
            return FALSE;

          params->set_mask |= 1 << i;

          unhandled--;
          if (paramspec[i].gtype == G_TYPE_STRING)
            {
              if (0 == strcmp (paramspec[i].name, "password"))
                {
                  g_debug ("%s: accepted value <hidden> for param password",
                      G_STRFUNC);
                }
              else
                {
                  g_debug ("%s: accepted value %s for param %s",
                      G_STRFUNC,
                      *((char **) ((void *)params + paramspec[i].offset)),
                      paramspec[i].name);
                }
            }
          else
            {
              g_debug ("%s: accepted value %u for param %s", G_STRFUNC,
                       *((guint *) ((void *)params + paramspec[i].offset)), paramspec[i].name);
            }
        }
    }

  if (unhandled)
    {
      g_debug ("%s: unknown argument name provided", G_STRFUNC);
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "unknown argument name provided");
      return FALSE;
    }

  return TRUE;
}

static void
free_params (GabbleParams *params)
{
  g_free (params->account);
  g_free (params->password);
  g_free (params->server);
  g_free (params->resource);
  g_free (params->https_proxy_server);
  g_free (params->fallback_conference_server);
  g_free (params->stun_server);
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
connection_disconnected_cb (GabbleConnection        *conn,
                            gpointer                 data)
{
  GabbleConnectionManager *self = GABBLE_CONNECTION_MANAGER (data);
  GabbleConnectionManagerPrivate *priv = GABBLE_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_assert (g_hash_table_lookup (priv->connections, conn));
  g_hash_table_remove (priv->connections, conn);

  g_object_unref (conn);

  g_debug ("%s: dereferenced connection", G_STRFUNC);
  if (g_hash_table_size (priv->connections) == 0)
    {
      g_signal_emit (self, signals[NO_MORE_CONNECTIONS], 0);
    }
}

/* public methods */

void
_gabble_connection_manager_register (GabbleConnectionManager *self)
{
  DBusGConnection *bus;
  DBusGProxy *bus_proxy;
  GError *error = NULL;
  guint request_name_result;

  g_assert (GABBLE_IS_CONNECTION_MANAGER (self));

  bus = tp_get_bus ();
  bus_proxy = tp_get_bus_proxy ();

  if (!dbus_g_proxy_call (bus_proxy, "RequestName", &error,
                          G_TYPE_STRING, BUS_NAME,
                          G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                          G_TYPE_INVALID,
                          G_TYPE_UINT, &request_name_result,
                          G_TYPE_INVALID))
    g_error ("Failed to request bus name: %s", error->message);

  if (request_name_result == DBUS_REQUEST_NAME_REPLY_EXISTS)
    g_error ("Failed to acquire bus name, connection manager already running?");

  dbus_g_connection_register_g_object (bus, OBJECT_PATH, G_OBJECT (self));
}

/* dbus-exported methods */

#define SET_PROPERTY_IF_PARAM_SET(prop, param, member) \
  if ((params.set_mask & (1 << param)) != 0) \
    { \
      g_object_set (conn, prop, member, NULL); \
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
gboolean gabble_connection_manager_connect (GabbleConnectionManager *obj, const gchar * proto, GHashTable * parameters, gchar ** bus_name, gchar ** object_path, GError **error)
{
  GabbleConnectionManagerPrivate *priv;
  GabbleConnection *conn;
  const GabbleParamSpec *paramspec;
  GabbleParams params = { 0, };

  g_assert (GABBLE_IS_CONNECTION_MANAGER (obj));

  priv = GABBLE_CONNECTION_MANAGER_GET_PRIVATE (obj);

  if (!get_parameters (proto, &paramspec, error))
    return FALSE;

  if (!parse_parameters (paramspec, parameters, &params, error))
    {
      free_params (&params);
      return FALSE;
    }

  conn = g_object_new (GABBLE_TYPE_CONNECTION,
                       "protocol",           proto,
                       "password",           params.password,
                       NULL);

  SET_PROPERTY_IF_PARAM_SET ("connect-server", JABBER_PARAM_SERVER,
                             params.server);
  SET_PROPERTY_IF_PARAM_SET ("resource", JABBER_PARAM_RESOURCE,
                             params.resource);
  SET_PROPERTY_IF_PARAM_SET ("priority", JABBER_PARAM_PRIORITY,
                             CLAMP (params.priority, G_MININT8, G_MAXINT8));
  SET_PROPERTY_IF_PARAM_SET ("port", JABBER_PARAM_PORT, params.port);
  SET_PROPERTY_IF_PARAM_SET ("old-ssl", JABBER_PARAM_OLD_SSL, params.old_ssl);
  SET_PROPERTY_IF_PARAM_SET ("register", JABBER_PARAM_REGISTER,
                             params.do_register);
  SET_PROPERTY_IF_PARAM_SET ("low-bandwidth", JABBER_PARAM_LOW_BANDWIDTH,
                             params.low_bandwidth);
  SET_PROPERTY_IF_PARAM_SET ("https-proxy-server",
                             JABBER_PARAM_HTTPS_PROXY_SERVER,
                             params.https_proxy_server);
  SET_PROPERTY_IF_PARAM_SET ("https-proxy-port", JABBER_PARAM_HTTPS_PROXY_PORT,
                             params.https_proxy_port);
  SET_PROPERTY_IF_PARAM_SET ("fallback-conference-server",
                             JABBER_PARAM_FALLBACK_CONFERENCE_SERVER,
                             params.fallback_conference_server);
  SET_PROPERTY_IF_PARAM_SET ("stun-server", JABBER_PARAM_STUN_SERVER,
                             params.stun_server);
  SET_PROPERTY_IF_PARAM_SET ("stun-port", JABBER_PARAM_STUN_PORT,
                             params.stun_port);

  /* split up account into username, stream-server and resource */
  if (!_gabble_connection_set_properties_from_account (conn, params.account, error))
    {
      goto ERROR;
    }

  /* free memory allocated by param parser */
  free_params(&params);

  /* register on bus and save bus name and object path */
  if (!_gabble_connection_register (conn, bus_name, object_path, error))
    {
      g_debug ("%s failed: %s", G_STRFUNC, (*error)->message);

      goto ERROR;
    }

  /* bind to status change signals from the connection object */
  g_signal_connect (conn, "disconnected",
                             G_CALLBACK (connection_disconnected_cb),
                             obj);

  /* store the connection, using a hash table as a set*/
  g_hash_table_insert (priv->connections, conn, GINT_TO_POINTER(TRUE));

  /* commence connecting */
  if (!_gabble_connection_connect (conn, error))
    {
      g_debug ("%s failed: %s", G_STRFUNC, (*error)->message);

      goto ERROR;
    }

  /* emit the new connection signal */
  g_signal_emit (obj, signals[NEW_CONNECTION], 0, *bus_name, *object_path, proto);

  return TRUE;

ERROR:
  if (conn)
    g_object_unref (G_OBJECT (conn));

  return FALSE;
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
  const GabbleParamSpec *params = NULL;

  if (!get_parameters (proto, &params, error))
    return FALSE;

  return list_parameters (params, TRUE, ret);
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
  const GabbleParamSpec *params = NULL;

  if (!get_parameters (proto, &params, error))
    return FALSE;

  return list_parameters (params, FALSE, ret);
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
  const GabbleParamSpec *params = NULL;

  if (!get_parameters (proto, &params, error))
    return FALSE;

  return parameter_defaults (params, ret);
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
  const char *protocols[] = { "jabber", NULL };

  *ret = g_strdupv ((gchar **)protocols);

  return TRUE;
}

