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

#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gabble-connection.h"
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>

#include "gabble-connection-manager.h"

G_DEFINE_TYPE(GabbleConnectionManager,
    gabble_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

/* type definition stuff */

static void
gabble_connection_manager_init (GabbleConnectionManager *self)
{
}

static TpBaseConnection *_gabble_connection_manager_new_connection (
    TpBaseConnectionManager *self, const gchar *proto,
    TpIntSet *params_present, void *parsed_params, GError **error);

static void
gabble_connection_manager_class_init (GabbleConnectionManagerClass *klass)
{
  TpBaseConnectionManagerClass *base_class = 
    (TpBaseConnectionManagerClass *)klass;

  base_class->new_connection = _gabble_connection_manager_new_connection;
  base_class->cm_dbus_name = "gabble";
  base_class->protocol_params = gabble_protocols;
}

/* private data */

typedef struct _GabbleParams GabbleParams;

struct _GabbleParams {
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

static const TpCMParamSpec jabber_params[] = {
  { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_REGISTER, NULL,
    G_STRUCT_OFFSET(GabbleParams, account) },
  { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_REGISTER, NULL,
    G_STRUCT_OFFSET(GabbleParams, password) },
  { "server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(GabbleParams, server) },
  { "resource", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GABBLE_PARAMS_DEFAULT_RESOURCE,
    G_STRUCT_OFFSET(GabbleParams, resource) },
  { "priority", DBUS_TYPE_INT16_AS_STRING, G_TYPE_INT,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(0),
    G_STRUCT_OFFSET(GabbleParams, priority) },
  { "port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
    GINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_PORT),
    G_STRUCT_OFFSET(GabbleParams, port) },
  { "old-ssl", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE),
    G_STRUCT_OFFSET(GabbleParams, old_ssl) },
  { "register", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE),
    G_STRUCT_OFFSET(GabbleParams, do_register) },
  { "low-bandwidth", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE),
    G_STRUCT_OFFSET(GabbleParams, low_bandwidth) },
  { "https-proxy-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(GabbleParams, https_proxy_server) },
  { "https-proxy-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
    GINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT),
    G_STRUCT_OFFSET(GabbleParams, https_proxy_port) },
  { "fallback-conference-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    0, NULL, G_STRUCT_OFFSET(GabbleParams, fallback_conference_server) },
  { "stun-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(GabbleParams, stun_server) },
  { "stun-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
    GINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_STUN_PORT),
    G_STRUCT_OFFSET(GabbleParams, stun_port) },
  { "ignore-ssl-errors", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE),
    G_STRUCT_OFFSET(GabbleParams, ignore_ssl_errors) },
  { "alias", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(GabbleParams, alias) },
  { "mac", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(GabbleParams, auth_mac) },
  { "btid", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(GabbleParams, auth_btid) },
  { NULL, NULL, 0, 0, NULL, 0 }
};

static void *
alloc_params (void)
{
  return g_slice_new0 (GabbleParams);
}

static void
free_params (void *p)
{
  GabbleParams *params = (GabbleParams *)p;

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

  g_slice_free (GabbleParams, params);
}

const TpCMProtocolSpec gabble_protocols[] = {
  { "jabber", jabber_params, alloc_params, free_params },
  { NULL, NULL }
};

/* dbus-exported methods */

#define SET_PROPERTY_IF_PARAM_SET(prop, param, member) \
  if (tp_intset_is_member (params_present, param)) \
    { \
      g_object_set (conn, prop, member, NULL); \
    }


static TpBaseConnection *
_gabble_connection_manager_new_connection (TpBaseConnectionManager *self,
                                           const gchar *proto,
                                           TpIntSet *params_present,
                                           void *parsed_params,
                                           GError **error)
{
  GabbleConnection *conn;
  GabbleParams *params = (GabbleParams *)parsed_params;

  g_assert (GABBLE_IS_CONNECTION_MANAGER (self));

  conn = g_object_new (GABBLE_TYPE_CONNECTION,
                       "protocol",           proto,
                       "password",           params->password,
                       NULL);

  SET_PROPERTY_IF_PARAM_SET ("connect-server", JABBER_PARAM_SERVER,
                             params->server);
  SET_PROPERTY_IF_PARAM_SET ("resource", JABBER_PARAM_RESOURCE,
                             params->resource);
  SET_PROPERTY_IF_PARAM_SET ("priority", JABBER_PARAM_PRIORITY,
                             (char) CLAMP (params->priority, G_MININT8, G_MAXINT8));
  SET_PROPERTY_IF_PARAM_SET ("port", JABBER_PARAM_PORT, params->port);
  SET_PROPERTY_IF_PARAM_SET ("old-ssl", JABBER_PARAM_OLD_SSL, params->old_ssl);
  SET_PROPERTY_IF_PARAM_SET ("register", JABBER_PARAM_REGISTER,
                             params->do_register);
  SET_PROPERTY_IF_PARAM_SET ("low-bandwidth", JABBER_PARAM_LOW_BANDWIDTH,
                             params->low_bandwidth);
  SET_PROPERTY_IF_PARAM_SET ("https-proxy-server",
                             JABBER_PARAM_HTTPS_PROXY_SERVER,
                             params->https_proxy_server);
  SET_PROPERTY_IF_PARAM_SET ("https-proxy-port", JABBER_PARAM_HTTPS_PROXY_PORT,
                             params->https_proxy_port);
  SET_PROPERTY_IF_PARAM_SET ("fallback-conference-server",
                             JABBER_PARAM_FALLBACK_CONFERENCE_SERVER,
                             params->fallback_conference_server);
  SET_PROPERTY_IF_PARAM_SET ("stun-server", JABBER_PARAM_STUN_SERVER,
                             params->stun_server);
  SET_PROPERTY_IF_PARAM_SET ("stun-port", JABBER_PARAM_STUN_PORT,
                             params->stun_port);
  SET_PROPERTY_IF_PARAM_SET ("ignore-ssl-errors",
                              JABBER_PARAM_IGNORE_SSL_ERRORS,
                              params->ignore_ssl_errors);
  SET_PROPERTY_IF_PARAM_SET ("alias", JABBER_PARAM_ALIAS, params->alias);
  SET_PROPERTY_IF_PARAM_SET ("auth-mac", JABBER_PARAM_AUTH_MAC,
                             params->auth_mac);
  SET_PROPERTY_IF_PARAM_SET ("auth-btid", JABBER_PARAM_AUTH_BTID,
                             params->auth_btid);

  /* split up account into username, stream-server and resource */
  if (!_gabble_connection_set_properties_from_account (conn, params->account,
        error))
    {
      g_object_unref (G_OBJECT (conn));
      conn = NULL;
    }

  return (TpBaseConnection *)conn;
}
