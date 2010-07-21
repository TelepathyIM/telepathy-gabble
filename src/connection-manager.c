/*
 * gabble-connection-manager.c - Source for GabbleConnectionManager
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2007 Nokia Corporation
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

#include "config.h"
#include "connection-manager.h"

#include <string.h>

#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>

#include "caps-cache.h"
#include "connection.h"
#include "debug.h"

#include "extensions/extensions.h"

#include "protocol.h"

G_DEFINE_TYPE(GabbleConnectionManager,
    gabble_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

/* type definition stuff */

static void
gabble_connection_manager_init (GabbleConnectionManager *self)
{
}

static void
gabble_connection_manager_constructed (GObject *object)
{
  GabbleConnectionManager *self = GABBLE_CONNECTION_MANAGER (object);
  TpBaseConnectionManager *base = (TpBaseConnectionManager *) self;
  void (*constructed) (GObject *) =
      ((GObjectClass *) gabble_connection_manager_parent_class)->constructed;
  TpBaseProtocol *protocol;

  if (constructed != NULL)
    constructed (object);

  protocol = g_object_new (GABBLE_TYPE_JABBER_PROTOCOL,
      "name", "jabber",
      NULL);
  tp_base_connection_manager_add_protocol (base, protocol);
  g_object_unref (protocol);
}

static void
gabble_connection_manager_finalize (GObject *object)
{
  gabble_caps_cache_free_shared ();
  gabble_debug_free ();

  G_OBJECT_CLASS (gabble_connection_manager_parent_class)->finalize (object);
}

static void
gabble_connection_manager_class_init (GabbleConnectionManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  TpBaseConnectionManagerClass *base_class =
    (TpBaseConnectionManagerClass *) klass;

  base_class->new_connection = NULL;
  base_class->cm_dbus_name = "gabble";
  base_class->protocol_params = NULL;
  object_class->constructed = gabble_connection_manager_constructed;
  object_class->finalize = gabble_connection_manager_finalize;
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
  gboolean require_encryption;
  gboolean do_register;
  gboolean low_bandwidth;
  gchar *https_proxy_server;
  guint https_proxy_port;
  gchar *fallback_conference_server;
  gchar *stun_server;
  guint stun_port;
  gchar *fallback_stun_server;
  guint fallback_stun_port;
  gboolean ignore_ssl_errors;
  gchar *alias;
  GStrv fallback_socks5_proxies;
  guint keepalive_interval;
  gboolean decloak_automatically;
};

enum {
    JABBER_PARAM_ACCOUNT = 0,
    JABBER_PARAM_PASSWORD,
    JABBER_PARAM_SERVER,
    JABBER_PARAM_RESOURCE,
    JABBER_PARAM_PRIORITY,
    JABBER_PARAM_PORT,
    JABBER_PARAM_OLD_SSL,
    JABBER_PARAM_REQUIRE_ENCRYPTION,
    JABBER_PARAM_REGISTER,
    JABBER_PARAM_LOW_BANDWIDTH,
    JABBER_PARAM_HTTPS_PROXY_SERVER,
    JABBER_PARAM_HTTPS_PROXY_PORT,
    JABBER_PARAM_FALLBACK_CONFERENCE_SERVER,
    JABBER_PARAM_STUN_SERVER,
    JABBER_PARAM_STUN_PORT,
    JABBER_PARAM_FALLBACK_STUN_SERVER,
    JABBER_PARAM_FALLBACK_STUN_PORT,
    JABBER_PARAM_IGNORE_SSL_ERRORS,
    JABBER_PARAM_ALIAS,
    JABBER_PARAM_FALLBACK_SOCKS5_PROXIES,
    JABBER_PARAM_KEEPALIVE_INTERVAL,
    JABBER_PARAM_DECLOAK_AUTOMATICALLY,

    LAST_JABBER_PARAM
};

static TpCMParamSpec jabber_params[] = {
  { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_REGISTER, NULL,
    G_STRUCT_OFFSET(GabbleParams, account),
    /* FIXME: validate the JID according to the RFC */
    tp_cm_param_filter_string_nonempty, NULL },
  { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_REGISTER | TP_CONN_MGR_PARAM_FLAG_SECRET,
    NULL,
    G_STRUCT_OFFSET(GabbleParams, password), NULL, NULL },

  { "server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(GabbleParams, server),
    /* FIXME: validate the server properly */
    tp_cm_param_filter_string_nonempty, NULL },

  { "resource", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(GabbleParams, resource),
    /* FIXME: validate the resource according to the RFC */
    tp_cm_param_filter_string_nonempty, NULL },

  { "priority", DBUS_TYPE_INT16_AS_STRING, G_TYPE_INT,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(0),
    G_STRUCT_OFFSET(GabbleParams, priority), NULL, NULL },

  { "port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(5222),
    G_STRUCT_OFFSET(GabbleParams, port),
    tp_cm_param_filter_uint_nonzero, NULL },

  { "old-ssl", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE),
    G_STRUCT_OFFSET(GabbleParams, old_ssl), NULL, NULL },

  { "require-encryption", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE),
    G_STRUCT_OFFSET(GabbleParams, require_encryption), NULL, NULL },

  { "register", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE),
    G_STRUCT_OFFSET(GabbleParams, do_register), NULL, NULL },

  { "low-bandwidth", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE),
    G_STRUCT_OFFSET(GabbleParams, low_bandwidth), NULL, NULL },

  { "https-proxy-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(GabbleParams, https_proxy_server),
    /* FIXME: validate properly */
    tp_cm_param_filter_string_nonempty, NULL },
  { "https-proxy-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
    GUINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT),
    G_STRUCT_OFFSET(GabbleParams, https_proxy_port),
    tp_cm_param_filter_uint_nonzero, NULL },

  { "fallback-conference-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    0, NULL, G_STRUCT_OFFSET(GabbleParams, fallback_conference_server),
    /* FIXME: validate properly */
    tp_cm_param_filter_string_nonempty, NULL },

  { "stun-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(GabbleParams, stun_server),
    /* FIXME: validate properly */
    tp_cm_param_filter_string_nonempty, NULL },
  { "stun-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
    GUINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_STUN_PORT),
    G_STRUCT_OFFSET(GabbleParams, stun_port),
    tp_cm_param_filter_uint_nonzero, NULL },

  { "fallback-stun-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
    GABBLE_PARAMS_DEFAULT_FALLBACK_STUN_SERVER,
    G_STRUCT_OFFSET(GabbleParams, fallback_stun_server),
    /* FIXME: validate properly */
    tp_cm_param_filter_string_nonempty, NULL },
  { "fallback-stun-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
    GUINT_TO_POINTER(GABBLE_PARAMS_DEFAULT_STUN_PORT),
    G_STRUCT_OFFSET(GabbleParams, fallback_stun_port),
    tp_cm_param_filter_uint_nonzero, NULL },

  { "ignore-ssl-errors", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER(FALSE),
    G_STRUCT_OFFSET(GabbleParams, ignore_ssl_errors), NULL, NULL },

  { "alias", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(GabbleParams, alias),
    /* setting a 0-length alias makes no sense */
    tp_cm_param_filter_string_nonempty, NULL },

  { "fallback-socks5-proxies", "as", 0,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, NULL,
    G_STRUCT_OFFSET (GabbleParams, fallback_socks5_proxies),
    NULL, NULL },

  { "keepalive-interval", "u", G_TYPE_UINT,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER (30),
    G_STRUCT_OFFSET (GabbleParams, keepalive_interval), NULL, NULL },

  { GABBLE_PROP_CONNECTION_INTERFACE_GABBLE_DECLOAK_DECLOAK_AUTOMATICALLY,
    DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GINT_TO_POINTER (FALSE),
    G_STRUCT_OFFSET (GabbleParams, decloak_automatically), NULL, NULL },

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
  GabbleParams *params = (GabbleParams *) p;

  g_free (params->account);
  g_free (params->password);
  g_free (params->server);
  g_free (params->resource);
  g_free (params->https_proxy_server);
  g_free (params->fallback_conference_server);
  g_free (params->stun_server);
  g_free (params->fallback_stun_server);
  g_free (params->alias);
  g_strfreev (params->fallback_socks5_proxies);

  g_slice_free (GabbleParams, params);
}

const TpCMProtocolSpec gabble_protocols[] = {
  { "jabber", jabber_params, alloc_params, free_params },
  { NULL, NULL }
};

const gchar *default_socks5_proxies[] = GABBLE_PARAMS_DEFAULT_SOCKS5_PROXIES;

const TpCMProtocolSpec *
gabble_connection_manager_get_protocols (void)
{
  jabber_params[JABBER_PARAM_FALLBACK_SOCKS5_PROXIES].gtype = G_TYPE_STRV;
  jabber_params[JABBER_PARAM_FALLBACK_SOCKS5_PROXIES].def =
    default_socks5_proxies;

  return gabble_protocols;
}
