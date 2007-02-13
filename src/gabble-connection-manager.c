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
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>

#include "gabble-connection-manager.h"

#define TP_TYPE_PARAM (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_VALUE, \
      G_TYPE_INVALID))

static void cm_service_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(GabbleConnectionManager,
    gabble_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER,
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_MANAGER,
      cm_service_iface_init))

/* type definition stuff */

static void
gabble_connection_manager_init (GabbleConnectionManager *self)
{
}

static TpBaseConnection *_gabble_connection_manager_new_connection (
    TpBaseConnectionManager *self, const gchar *proto, GHashTable *parameters,
    GError **error);

static void
gabble_connection_manager_class_init (GabbleConnectionManagerClass *gabble_connection_manager_class)
{
  gabble_connection_manager_class->parent_class.new_connection =
    _gabble_connection_manager_new_connection;

  gabble_connection_manager_class->parent_class.cm_dbus_name = "gabble";
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
  gboolean ignore_ssl_errors;
  gchar *alias;
  gchar *auth_mac;
  gchar *auth_btid;
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
    JABBER_PARAM_STUN_PORT,
    JABBER_PARAM_IGNORE_SSL_ERRORS,
    JABBER_PARAM_ALIAS,
    JABBER_PARAM_AUTH_MAC,
    JABBER_PARAM_AUTH_BTID,
    LAST_JABBER_PARAM
};

static const GabbleParamSpec jabber_params[] = {
  { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_REGISTER, NULL, G_STRUCT_OFFSET(GabbleParams, account) },
  { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_REGISTER, NULL, G_STRUCT_OFFSET(GabbleParams, password) },
  { "server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(GabbleParams, server) },
  { "resource", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GABBLE_PARAMS_DEFAULT_RESOURCE, G_STRUCT_OFFSET(GabbleParams, resource) },
  { "priority", DBUS_TYPE_INT16_AS_STRING, G_TYPE_INT, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(0), G_STRUCT_OFFSET(GabbleParams, priority) },
  { "port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_PORT), G_STRUCT_OFFSET(GabbleParams, port) },
  { "old-ssl", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE), G_STRUCT_OFFSET(GabbleParams, old_ssl) },
  { "register", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE), G_STRUCT_OFFSET(GabbleParams, do_register) },
  { "low-bandwidth", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE), G_STRUCT_OFFSET(GabbleParams, low_bandwidth) },
  { "https-proxy-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(GabbleParams, https_proxy_server) },
  { "https-proxy-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT), G_STRUCT_OFFSET(GabbleParams, https_proxy_port) },
  { "fallback-conference-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(GabbleParams, fallback_conference_server) },
  { "stun-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(GabbleParams, stun_server) },
  { "stun-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_STUN_PORT), G_STRUCT_OFFSET(GabbleParams, stun_port) },
  { "ignore-ssl-errors", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN, TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE), G_STRUCT_OFFSET(GabbleParams, ignore_ssl_errors) },
  { "alias", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(GabbleParams, alias) },
  { "mac", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(GabbleParams, auth_mac) },
  { "btid", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, G_STRUCT_OFFSET(GabbleParams, auth_btid) },
  { NULL, NULL, 0, 0, NULL, 0 }
};

static const GabbleProtocolSpec _gabble_protocols[] = {
  { "jabber", jabber_params },
  { NULL, NULL }
};
const GabbleProtocolSpec *gabble_protocols = _gabble_protocols;

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

      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "unknown protocol %s", proto);

      return FALSE;
    }

  return TRUE;
}

static GValue *param_default_value (const GabbleParamSpec *params, int i)
{
  GValue *value;

  value = g_new0(GValue, 1);
  g_value_init(value, params[i].gtype);

  /* TODO: this check could be stricter if we knew whether register
  was true. In practice REQUIRED and REGISTER always go together in
  the Gabble params, though */
  if (params[i].flags & TP_CONN_MGR_PARAM_FLAG_REQUIRED & TP_CONN_MGR_PARAM_FLAG_REGISTER)
    {
      g_assert(params[i].def == NULL);
      goto OUT;
    }

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
        g_error("parameter_defaults: encountered unknown type %s on argument %s",
                params[i].dtype, params[i].name);
    }

OUT:
  return value;
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
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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

static void
report_unknown_param (gpointer key, gpointer value, gpointer user_data)
{
  const char *arg = (const char *) key;
  g_debug ("%s: unknown argument provided: %s", G_STRFUNC, arg);
}

static gboolean
parse_parameters (const GabbleParamSpec *paramspec,
                  GHashTable            *provided,
                  GabbleParams          *params,
                  GError               **error)
{
  int i;
  guint mandatory_flag = TP_CONN_MGR_PARAM_FLAG_REQUIRED;
  GValue *value;

  value = g_hash_table_lookup (provided, "register");
  if (value != NULL && G_VALUE_TYPE(value) == G_TYPE_BOOLEAN &&
      g_value_get_boolean(value))
    {
      mandatory_flag = TP_CONN_MGR_PARAM_FLAG_REGISTER;
    }

  for (i = 0; paramspec[i].name; i++)
    {
      value = g_hash_table_lookup (provided, paramspec[i].name);

      if (value == NULL)
        {
          if (paramspec[i].flags & mandatory_flag)
            {
              g_debug ("%s: missing mandatory param %s",
                       G_STRFUNC, paramspec[i].name);
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "missing mandatory account parameter %s", paramspec[i].name);
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
            {
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "invalid value for parameter %s", paramspec[i].name);
              return FALSE;
            }

          params->set_mask |= 1 << i;

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

          g_hash_table_remove (provided, paramspec[i].name);
        }
    }

  if (g_hash_table_size (provided) != 0)
    {
      g_hash_table_foreach (provided, report_unknown_param, NULL);
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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
  g_free (params->alias);
  g_free (params->auth_mac);
  g_free (params->auth_btid);
}

/* dbus-exported methods */

/**
 * gabble_connection_manager_get_parameters
 *
 * Implements D-Bus method GetParameters
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_manager_get_parameters (TpSvcConnectionManager *iface,
                                          const gchar *proto,
                                          DBusGMethodInvocation *context)
{
  GPtrArray *ret;
  GError *error;
  const GabbleParamSpec *params = NULL;
  int i;

  if (!get_parameters (proto, &params, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = g_ptr_array_new ();

  for (i = 0; params[i].name; i++)
    {
      GValue *def_value;
      GValue param = { 0, };

      g_value_init (&param, TP_TYPE_PARAM);
      g_value_set_static_boxed (&param,
        dbus_g_type_specialized_construct (TP_TYPE_PARAM));
      
      def_value = param_default_value (params, i);
      dbus_g_type_struct_set (&param,
        0, params[i].name,
        1, params[i].flags,
        2, params[i].dtype,
        3, def_value,
        G_MAXUINT);
      g_value_unset(def_value);
      g_free(def_value);

      g_ptr_array_add (ret, g_value_get_boxed (&param));
    }

  tp_svc_connection_manager_return_from_get_parameters (
      context, ret);
  g_ptr_array_free (ret, TRUE);
}


/**
 * gabble_connection_manager_list_protocols
 *
 * Implements D-Bus method ListProtocols
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_manager_list_protocols (TpSvcConnectionManager *iface,
                                          DBusGMethodInvocation *context)
{
  const char *protocols[] = { "jabber", NULL };

  tp_svc_connection_manager_return_from_list_protocols (
      context, protocols);
}


#define SET_PROPERTY_IF_PARAM_SET(prop, param, member) \
  if ((params.set_mask & (1 << param)) != 0) \
    { \
      g_object_set (conn, prop, member, NULL); \
    }


static TpBaseConnection *
_gabble_connection_manager_new_connection (TpBaseConnectionManager *self,
                                           const gchar *proto,
                                           GHashTable *parameters,
                                           GError **error)
{
  GabbleConnection *conn;
  const GabbleParamSpec *paramspec;
  GabbleParams params = { 0, };

  g_assert (GABBLE_IS_CONNECTION_MANAGER (self));

  if (!get_parameters (proto, &paramspec, error))
    return NULL;

  if (!parse_parameters (paramspec, parameters, &params, error))
    {
      free_params (&params);
      return NULL;
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
  SET_PROPERTY_IF_PARAM_SET ("ignore-ssl-errors",
                              JABBER_PARAM_IGNORE_SSL_ERRORS,
                              params.ignore_ssl_errors);
  SET_PROPERTY_IF_PARAM_SET ("alias", JABBER_PARAM_ALIAS, params.alias);
  SET_PROPERTY_IF_PARAM_SET ("auth-mac", JABBER_PARAM_AUTH_MAC,
                             params.auth_mac);
  SET_PROPERTY_IF_PARAM_SET ("auth-btid", JABBER_PARAM_AUTH_BTID,
                             params.auth_btid);

  /* split up account into username, stream-server and resource */
  if (!_gabble_connection_set_properties_from_account (conn, params.account, error))
    {
      g_object_unref (G_OBJECT (conn));
      conn = NULL;
    }

  /* free memory allocated by param parser */
  free_params(&params);

  return (TpBaseConnection *)conn;
}

static void
cm_service_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionManagerClass *klass = (TpSvcConnectionManagerClass *)g_iface;

#define IMPLEMENT(x) tp_svc_connection_manager_implement_##x (klass, \
    gabble_connection_manager_##x)
  IMPLEMENT(get_parameters); 
  IMPLEMENT(list_protocols);
#undef IMPLEMENT
}
