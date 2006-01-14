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

#include "gabble.h"
#include "gabble-connection.h"
#include "telepathy-errors.h"

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
  char *account;
  char *password;
  char *server;
  guint16 port;
  gboolean old_ssl;
};

typedef struct _GabbleParamSpec GabbleParamSpec;

struct _GabbleParamSpec {
  const char *name;
  const char *dtype;
  const GType gtype;
  gboolean mandatory;
  const gpointer def;
  const gsize offset;
};

static const GabbleParamSpec google_talk_params[] = {
  { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TRUE, NULL, G_STRUCT_OFFSET(GabbleParams, account) },
  { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TRUE, NULL, G_STRUCT_OFFSET(GabbleParams, password) },
  { "server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, FALSE, "talk.google.com", G_STRUCT_OFFSET(GabbleParams, server) },
  { "port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT, FALSE, GINT_TO_POINTER(5223), G_STRUCT_OFFSET(GabbleParams, port) },
  { "old-ssl", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN, FALSE, GINT_TO_POINTER(TRUE), G_STRUCT_OFFSET(GabbleParams, old_ssl) },
  { NULL, NULL, 0, 0, NULL, 0 }
};

static const GabbleParamSpec jabber_params[] = {
  { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TRUE, NULL, G_STRUCT_OFFSET(GabbleParams, account) },
  { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TRUE, NULL, G_STRUCT_OFFSET(GabbleParams, password) },
  { "server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, FALSE, NULL, G_STRUCT_OFFSET(GabbleParams, server) },
  { "port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT, FALSE, GINT_TO_POINTER(5222), G_STRUCT_OFFSET(GabbleParams, port) },
  { "old-ssl", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN, FALSE, GINT_TO_POINTER(FALSE), G_STRUCT_OFFSET(GabbleParams, old_ssl) },
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
  else if (!strcmp (proto, "google-talk"))
    {
      *params = google_talk_params;
    }
  else
    {
      g_debug ("get_parameters: unknown protocol %s", proto);

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

static void
set_default_param (const GabbleParamSpec *paramspec,
                            GabbleParams *params)
{
  switch (paramspec->dtype[0])
    {
      case DBUS_TYPE_STRING:
        *((char **) ((void *)params + paramspec->offset)) = g_strdup (paramspec->def);
        break;
      case DBUS_TYPE_UINT16:
        *((guint16 *) ((void *)params + paramspec->offset)) = GPOINTER_TO_INT (paramspec->def);
        break;
      case DBUS_TYPE_BOOLEAN:
        *((gboolean *) ((void *)params + paramspec->offset)) = GPOINTER_TO_INT (paramspec->def);
        break;
      default:
        g_error ("set_default_param: encountered unknown type %s on argument %s",
                 paramspec->dtype, paramspec->name);
    }
}

static gboolean
set_param_from_value (const GabbleParamSpec *paramspec,
                                     GValue *value,
                               GabbleParams *params,
                                    GError **error)
{
  if (G_VALUE_TYPE (value) != paramspec->gtype)
    {
      g_debug ("set_param_from_value: expected type %s for parameter %s, got %s",
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
        *((char **) ((void *)params + paramspec->offset)) = g_value_dup_string (value);
        break;
      case DBUS_TYPE_UINT16:
        *((guint16 *) ((void *)params + paramspec->offset)) = g_value_get_uint (value);
        break;
      case DBUS_TYPE_BOOLEAN:
        *((gboolean *) ((void *)params + paramspec->offset)) = g_value_get_boolean (value);
        break;
      default:
        g_error ("set_param_from_value: encountered unknown type %s on argument %s",
                 paramspec->dtype, paramspec->name);
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
              g_debug ("parse_parameters: missing mandatory param %s",
                       paramspec[i].name);
              *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                    "missing mandatory account parameter %s",
                                    paramspec[i].name);
              return FALSE;
            }
          else
            {
              g_debug ("parse_parameters: using default value for param %s",
                       paramspec[i].name);
              set_default_param (&paramspec[i], params);
            }
        }
      else
        {
          if (!set_param_from_value (&paramspec[i], value, params, error))
            return FALSE;

          unhandled--;
          if (paramspec[i].gtype == G_TYPE_STRING)
            g_debug ("parse_parameters: accepted value %s for param %s",
                     *((char **) ((void *)params + paramspec[i].offset)), paramspec[i].name);
          else
            g_debug ("parse_parameters: accepted value %u for param %s",
                     *((guint *) ((void *)params + paramspec[i].offset)), paramspec[i].name);
        }
    }

  if (unhandled)
    {
      g_debug ("parse_parameters: unknown argument name provided");
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "unknown argument name provided");
      return FALSE;
    }

  return TRUE;
}

void
free_params (GabbleParams *params)
{
  if (params->account)
    g_free (params->account);

  if (params->password)
    g_free (params->password);

  if (params->server)
    g_free (params->server);
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

  bus = gabble_get_bus ();
  bus_proxy = gabble_get_bus_proxy ();

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
gboolean gabble_connection_manager_connect (GabbleConnectionManager *obj, const gchar * proto, GHashTable * parameters, gchar ** ret, gchar ** ret1, GError **error)
{
  GabbleConnection *conn;
  const GabbleParamSpec *paramspec;
  GabbleParams params = { NULL };

  if (!get_parameters (proto, &paramspec, error))
    return FALSE;

  if (!parse_parameters (paramspec, parameters, &params, error))
    {
      free_params (&params);
      return FALSE;
    }

  conn = g_object_new (GABBLE_TYPE_CONNECTION,
                       "protocol",       proto,
                       "password",       params.password,
                       "connect-server", params.server,
                       "port",           params.port,
                       "old-ssl",        params.old_ssl,
                       NULL);

  /* split up account into username, stream-server and resource */
  _gabble_connection_set_properties_from_account (conn, params.account);

  free_params(&params);

  if (!_gabble_connection_connect (conn, error))
    {
      g_debug("_gabble_connection_connect failed: %s", (*error)->message);

      goto ERROR;
    }

  while (1)
    g_main_context_iteration (g_main_context_default (), TRUE);

  *ret = g_strdup ("service name");
  *ret1 = conn;

  return TRUE;

ERROR:
  if (conn)
    g_object_unref (G_OBJECT (conn));

  return FALSE;
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

