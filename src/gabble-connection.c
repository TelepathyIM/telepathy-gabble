/*
 * gabble-connection.c - Source for GabbleConnection
 * Copyright (C) 2005, 2006 Collabora Ltd.
 * Copyright (C) 2005, 2006 Nokia Corporation
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

#define DBUS_API_SUBJECT_TO_CHANGE

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <glib-object.h>
#include <loudmouth/loudmouth.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "handles.h"
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/channel-factory-iface.h>

#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/svc-properties-interface.h>

#include "gabble-connection.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "base64.h"
#include "capabilities.h"
#include "debug.h"
#include "disco.h"
#include "gabble-presence-cache.h"
#include "gabble-presence.h"
#include "gabble-register.h"
#include "im-factory.h"
#include "media-factory.h"
#include "muc-factory.h"
#include "namespaces.h"
#include "roster.h"
#include "sha1/sha1.h"
#include "util.h"
#include "vcard-manager.h"

#include "gabble-media-channel.h"
#include "gabble-roomlist-channel.h"

#define TP_ALIAS_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID))
#define TP_CAPABILITY_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID))
#define TP_CAPABILITIES_CHANGED_MONSTER_TYPE (dbus_g_type_get_struct \
    ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, \
                    G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID))
#define TP_GET_CAPABILITIES_MONSTER_TYPE (dbus_g_type_get_struct \
    ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, \
                    G_TYPE_INVALID))

#define ERROR_IF_NOT_CONNECTED_ASYNC(BASE, ERROR, CONTEXT) \
  if ((BASE)->status != TP_CONNECTION_STATUS_CONNECTED) \
    { \
      DEBUG ("rejected request as disconnected"); \
      (ERROR) = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE, \
          "Connection is disconnected"); \
      dbus_g_method_return_error ((CONTEXT), (ERROR)); \
      g_error_free ((ERROR)); \
      return; \
    }


static void conn_service_iface_init(gpointer, gpointer);
static void aliasing_service_iface_init(gpointer, gpointer);
static void avatars_service_iface_init(gpointer, gpointer);
static void capabilities_service_iface_init(gpointer, gpointer);
static void presence_service_iface_init(gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(GabbleConnection,
    gabble_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION,
      conn_service_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
      aliasing_service_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS,
      avatars_service_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CAPABILITIES,
      capabilities_service_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE,
      presence_service_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_PROPERTIES_INTERFACE,
      tp_properties_mixin_iface_init)
    )

typedef struct _StatusInfo StatusInfo;

struct _StatusInfo
{
  const gchar *name;
  TpConnectionPresenceType presence_type;
  const gboolean self;
  const gboolean exclusive;
};

/* order must match PresenceId enum in gabble-connection.h */
/* in increasing order of presence */
static const StatusInfo gabble_statuses[LAST_GABBLE_PRESENCE] = {
 { "offline",   TP_CONNECTION_PRESENCE_TYPE_OFFLINE,       TRUE, TRUE },
 { "hidden",    TP_CONNECTION_PRESENCE_TYPE_HIDDEN,        TRUE, TRUE },
 { "xa",        TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY, TRUE, TRUE },
 { "away",      TP_CONNECTION_PRESENCE_TYPE_AWAY,          TRUE, TRUE },
 { "dnd",       TP_CONNECTION_PRESENCE_TYPE_AWAY,          TRUE, TRUE },
 { "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE,     TRUE, TRUE },
 { "chat",      TP_CONNECTION_PRESENCE_TYPE_AVAILABLE,     TRUE, TRUE }
};

/* properties */
enum
{
    PROP_CONNECT_SERVER = 1,
    PROP_PORT,
    PROP_OLD_SSL,
    PROP_REGISTER,
    PROP_LOW_BANDWIDTH,
    PROP_STREAM_SERVER,
    PROP_USERNAME,
    PROP_PASSWORD,
    PROP_RESOURCE,
    PROP_PRIORITY,
    PROP_HTTPS_PROXY_SERVER,
    PROP_HTTPS_PROXY_PORT,
    PROP_FALLBACK_CONFERENCE_SERVER,
    PROP_STUN_SERVER,
    PROP_STUN_PORT,
    PROP_STUN_RELAY_MAGIC_COOKIE,
    PROP_STUN_RELAY_SERVER,
    PROP_STUN_RELAY_UDP_PORT,
    PROP_STUN_RELAY_TCP_PORT,
    PROP_STUN_RELAY_SSLTCP_PORT,
    PROP_STUN_RELAY_USERNAME,
    PROP_STUN_RELAY_PASSWORD,
    PROP_IGNORE_SSL_ERRORS,
    PROP_ALIAS,
    PROP_AUTH_MAC,
    PROP_AUTH_BTID,

    LAST_PROPERTY
};

/* TP properties */
enum
{
  CONN_PROP_STUN_SERVER = 0,
  CONN_PROP_STUN_PORT,
  CONN_PROP_STUN_RELAY_MAGIC_COOKIE,
  CONN_PROP_STUN_RELAY_SERVER,
  CONN_PROP_STUN_RELAY_UDP_PORT,
  CONN_PROP_STUN_RELAY_TCP_PORT,
  CONN_PROP_STUN_RELAY_SSLTCP_PORT,
  CONN_PROP_STUN_RELAY_USERNAME,
  CONN_PROP_STUN_RELAY_PASSWORD,

  NUM_CONN_PROPS,

  INVALID_CONN_PROP,
};

const TpPropertySignature connection_property_signatures[NUM_CONN_PROPS] = {
      { "stun-server",                  G_TYPE_STRING },
      { "stun-port",                    G_TYPE_UINT   },
      { "stun-relay-magic-cookie",      G_TYPE_STRING },
      { "stun-relay-server",            G_TYPE_STRING },
      { "stun-relay-udp-port",          G_TYPE_UINT   },
      { "stun-relay-tcp-port",          G_TYPE_UINT   },
      { "stun-relay-ssltcp-port",       G_TYPE_UINT   },
      { "stun-relay-username",          G_TYPE_STRING },
      { "stun-relay-password",          G_TYPE_STRING },
};

/* private structure */
typedef struct _GabbleConnectionPrivate GabbleConnectionPrivate;

struct _GabbleConnectionPrivate
{
  LmMessageHandler *iq_disco_cb;
  LmMessageHandler *iq_unknown_cb;
  LmMessageHandler *stream_error_cb;

  /* connection properties */
  gchar *connect_server;
  guint port;
  gboolean old_ssl;

  gboolean ignore_ssl_errors;
  TpConnectionStatusReason ssl_error;

  gboolean do_register;

  gboolean low_bandwidth;

  gchar *https_proxy_server;
  guint https_proxy_port;

  gchar *fallback_conference_server;

  /* authentication properties */
  gchar *stream_server;
  gchar *username;
  gchar *password;
  gchar *resource;
  gint8 priority;
  gchar *alias;
  gchar *auth_mac;
  gchar *auth_btid;

  /* reference to conference server name */
  const gchar *conference_server;

  /* channel factories */
  gboolean suppress_next_handler;

  /* serial number of current advertised caps */
  guint caps_serial;

  /* gobject housekeeping */
  gboolean dispose_has_run;
};

#define GABBLE_CONNECTION_GET_PRIVATE(obj) \
    ((GabbleConnectionPrivate *)obj->priv)

static void connection_nickname_update_cb (GObject *, TpHandle, gpointer);
static void connection_presence_update_cb (GabblePresenceCache *, TpHandle, gpointer);
static void connection_avatar_update_cb (GabblePresenceCache *, TpHandle, gpointer);
static void connection_capabilities_update_cb (GabblePresenceCache *, TpHandle, GabblePresenceCapabilities, GabblePresenceCapabilities, gpointer);
static void connection_got_self_initial_avatar_cb (GObject *, gchar *, gpointer);

static GPtrArray *
_gabble_connection_create_channel_factories (TpBaseConnection *conn)
{
  GabbleConnection *self = GABBLE_CONNECTION (conn);

  GPtrArray *channel_factories = g_ptr_array_sized_new (4);

  self->roster = gabble_roster_new (self);
  g_signal_connect (self->roster, "nickname-update", G_CALLBACK
      (connection_nickname_update_cb), self);

  g_ptr_array_add (channel_factories, self->roster);

  g_ptr_array_add (channel_factories,
                   g_object_new (GABBLE_TYPE_MUC_FACTORY,
                                 "connection", self,
                                 NULL));

  g_ptr_array_add (channel_factories,
                   g_object_new (GABBLE_TYPE_MEDIA_FACTORY,
                                 "connection", self,
                                 NULL));

  g_ptr_array_add (channel_factories,
                   g_object_new (GABBLE_TYPE_IM_FACTORY,
                                 "connection", self,
                                 NULL));

  return channel_factories;
}

static GObject *
gabble_connection_constructor (GType type,
                               guint n_construct_properties,
                               GObjectConstructParam *construct_params)
{
  GabbleConnection *self = GABBLE_CONNECTION (
      G_OBJECT_CLASS (gabble_connection_parent_class)->constructor (
        type, n_construct_properties, construct_params));

  DEBUG("Post-construction: (GabbleConnection *)%p", self);

  self->disco = gabble_disco_new (self);
  self->vcard_manager = gabble_vcard_manager_new (self);
  g_signal_connect (self->vcard_manager, "nickname-update", G_CALLBACK
      (connection_nickname_update_cb), self);
  g_signal_connect (self->vcard_manager, "got-self-initial-avatar", G_CALLBACK
      (connection_got_self_initial_avatar_cb), self);

  self->presence_cache = gabble_presence_cache_new (self);
  g_signal_connect (self->presence_cache, "nickname-update", G_CALLBACK
      (connection_nickname_update_cb), self);
  g_signal_connect (self->presence_cache, "presence-update", G_CALLBACK
      (connection_presence_update_cb), self);
  g_signal_connect (self->presence_cache, "avatar-update", G_CALLBACK
      (connection_avatar_update_cb), self);
  g_signal_connect (self->presence_cache, "capabilities-update", G_CALLBACK
      (connection_capabilities_update_cb), self);

  capabilities_fill_cache (self->presence_cache);

  return (GObject *)self;
}

static void
gabble_connection_init (GabbleConnection *self)
{
  GabbleConnectionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CONNECTION, GabbleConnectionPrivate);
  GValue val = { 0, };

  DEBUG("Initializing (GabbleConnection *)%p", self);

  self->priv = priv;
  self->lmconn = lm_connection_new (NULL);

  /* Set default parameters for optional parameters */
  priv->resource = g_strdup (GABBLE_PARAMS_DEFAULT_RESOURCE);
  priv->port = GABBLE_PARAMS_DEFAULT_PORT;
  priv->https_proxy_port = GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT;

  g_value_init (&val, G_TYPE_UINT);
  g_value_set_uint (&val, GABBLE_PARAMS_DEFAULT_STUN_PORT);

  /* initialize properties mixin */
  tp_properties_mixin_init (G_OBJECT (self), G_STRUCT_OFFSET (
        GabbleConnection, properties));

  tp_properties_mixin_change_value (G_OBJECT (self), CONN_PROP_STUN_PORT,
                                        &val, NULL);
  tp_properties_mixin_change_flags (G_OBJECT (self), CONN_PROP_STUN_PORT,
                                        TP_PROPERTY_FLAG_READ, 0, NULL);

  g_value_unset (&val);

  priv->caps_serial = 1;
}

static void
gabble_connection_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleConnection *self = (GabbleConnection *) object;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);
  const gchar *param_name;
  guint tp_property_id;

  switch (property_id) {
    case PROP_CONNECT_SERVER:
      g_value_set_string (value, priv->connect_server);
      break;
    case PROP_STREAM_SERVER:
      g_value_set_string (value, priv->stream_server);
      break;
    case PROP_PORT:
      g_value_set_uint (value, priv->port);
      break;
    case PROP_OLD_SSL:
      g_value_set_boolean (value, priv->old_ssl);
      break;
    case PROP_REGISTER:
      g_value_set_boolean (value, priv->do_register);
      break;
    case PROP_LOW_BANDWIDTH:
      g_value_set_boolean (value, priv->low_bandwidth);
      break;
    case PROP_USERNAME:
      g_value_set_string (value, priv->username);
      break;
    case PROP_PASSWORD:
      g_value_set_string (value, priv->password);
      break;
    case PROP_RESOURCE:
      g_value_set_string (value, priv->resource);
      break;
    case PROP_PRIORITY:
      g_value_set_int (value, priv->priority);
      break;
    case PROP_HTTPS_PROXY_SERVER:
      g_value_set_string (value, priv->https_proxy_server);
      break;
    case PROP_HTTPS_PROXY_PORT:
      g_value_set_uint (value, priv->https_proxy_port);
      break;
    case PROP_FALLBACK_CONFERENCE_SERVER:
      g_value_set_string (value, priv->fallback_conference_server);
      break;
    case PROP_IGNORE_SSL_ERRORS:
      g_value_set_boolean (value, priv->ignore_ssl_errors);
      break;
    case PROP_ALIAS:
      g_value_set_string (value, priv->alias);
      break;
    case PROP_AUTH_MAC:
      g_value_set_string (value, priv->auth_mac);
      break;
    case PROP_AUTH_BTID:
      g_value_set_string (value, priv->auth_btid);
      break;
    default:
      param_name = g_param_spec_get_name (pspec);

      if (tp_properties_mixin_has_property (object, param_name,
            &tp_property_id))
        {
          GValue *tp_property_value =
            self->properties.properties[tp_property_id].value;

          if (tp_property_value)
            {
              g_value_copy (tp_property_value, value);
              return;
            }
        }

      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_connection_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GabbleConnection *self = (GabbleConnection *) object;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);
  const gchar *param_name;
  guint tp_property_id;

  switch (property_id) {
    case PROP_CONNECT_SERVER:
      g_free (priv->connect_server);
      priv->connect_server = g_value_dup_string (value);
      break;
    case PROP_PORT:
      priv->port = g_value_get_uint (value);
      break;
    case PROP_OLD_SSL:
      priv->old_ssl = g_value_get_boolean (value);
      break;
    case PROP_REGISTER:
      priv->do_register = g_value_get_boolean (value);
      break;
    case PROP_LOW_BANDWIDTH:
      priv->low_bandwidth = g_value_get_boolean (value);
      break;
    case PROP_STREAM_SERVER:
      g_free (priv->stream_server);
      priv->stream_server = g_value_dup_string (value);
      break;
    case PROP_USERNAME:
      g_free (priv->username);
      priv->username = g_value_dup_string (value);
      break;
   case PROP_PASSWORD:
      g_free (priv->password);
      priv->password = g_value_dup_string (value);
      break;
    case PROP_RESOURCE:
      g_free (priv->resource);
      priv->resource = g_value_dup_string (value);
      break;
    case PROP_PRIORITY:
      priv->priority = CLAMP (g_value_get_int (value), G_MININT8, G_MAXINT8);
      break;
    case PROP_HTTPS_PROXY_SERVER:
      g_free (priv->https_proxy_server);
      priv->https_proxy_server = g_value_dup_string (value);
      break;
    case PROP_HTTPS_PROXY_PORT:
      priv->https_proxy_port = g_value_get_uint (value);
      break;
    case PROP_FALLBACK_CONFERENCE_SERVER:
      g_free (priv->fallback_conference_server);
      priv->fallback_conference_server = g_value_dup_string (value);
      break;
    case PROP_IGNORE_SSL_ERRORS:
      priv->ignore_ssl_errors = g_value_get_boolean (value);
      break;
   case PROP_ALIAS:
      g_free (priv->alias);
      priv->alias = g_value_dup_string (value);
      break;
   case PROP_AUTH_MAC:
      g_free (priv->auth_mac);
      priv->auth_mac = g_value_dup_string (value);
      break;
   case PROP_AUTH_BTID:
      g_free (priv->auth_btid);
      priv->auth_btid = g_value_dup_string (value);
      break;
    default:
      param_name = g_param_spec_get_name (pspec);

      if (tp_properties_mixin_has_property (object, param_name,
            &tp_property_id))
        {
          tp_properties_mixin_change_value (object, tp_property_id, value,
                                                NULL);
          tp_properties_mixin_change_flags (object, tp_property_id,
                                                TP_PROPERTY_FLAG_READ,
                                                0, NULL);

          return;
        }

      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_connection_dispose (GObject *object);
static void gabble_connection_finalize (GObject *object);
static void connect_callbacks (TpBaseConnection *base);
static void disconnect_callbacks (TpBaseConnection *base);
static void connection_shut_down (TpBaseConnection *base);
static gboolean _gabble_connection_connect (TpBaseConnection *base,
    GError **error);

static gchar *
gabble_connection_get_unique_name (TpBaseConnection *self)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (
      GABBLE_CONNECTION (self));

  return g_strdup_printf ("%s@%s/%s",
                          priv->username,
                          priv->stream_server,
                          priv->resource);
}

static void
gabble_connection_class_init (GabbleConnectionClass *gabble_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_connection_class);
  TpBaseConnectionClass *parent_class = TP_BASE_CONNECTION_CLASS (
      gabble_connection_class);
  GParamSpec *param_spec;

  DEBUG("Initializing (GabbleConnectionClass *)%p", gabble_connection_class);

  object_class->get_property = gabble_connection_get_property;
  object_class->set_property = gabble_connection_set_property;
  object_class->constructor = gabble_connection_constructor;

  parent_class->init_handle_repos = gabble_handle_repos_init;
  parent_class->get_unique_connection_name = gabble_connection_get_unique_name;
  parent_class->create_channel_factories =
    _gabble_connection_create_channel_factories;
  parent_class->connecting = connect_callbacks;
  parent_class->disconnected = disconnect_callbacks;
  parent_class->shut_down = connection_shut_down;
  parent_class->start_connecting = _gabble_connection_connect;

  g_type_class_add_private (gabble_connection_class, sizeof (GabbleConnectionPrivate));

  object_class->dispose = gabble_connection_dispose;
  object_class->finalize = gabble_connection_finalize;

  param_spec = g_param_spec_string ("connect-server", "Hostname or IP of Jabber server",
                                    "The server used when establishing a connection.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECT_SERVER, param_spec);

  param_spec = g_param_spec_uint ("port", "Jabber server port",
                                  "The port used when establishing a connection.",
                                  0, G_MAXUINT16, GABBLE_PARAMS_DEFAULT_PORT,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PORT, param_spec);

  param_spec = g_param_spec_boolean ("old-ssl", "Old-style SSL tunneled connection",
                                     "Establish the entire connection to the server "
                                     "within an SSL-encrypted tunnel. Note that this "
                                     "is not the same as connecting with TLS, which "
                                     "is not yet supported.", FALSE,
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OLD_SSL, param_spec);

  param_spec = g_param_spec_boolean ("register", "Register account on server",
                                     "Register a new account on server.", FALSE,
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_REGISTER, param_spec);

  param_spec = g_param_spec_boolean ("low-bandwidth", "Low bandwidth mode",
                                     "Determines whether we are in low "
                                     "bandwidth mode. This influences "
                                     "polling behaviour.", FALSE,
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_LOW_BANDWIDTH, param_spec);

  param_spec = g_param_spec_string ("stream-server", "The server name used to initialise the stream.",
                                    "The server name used when initialising the stream, "
                                    "which is usually the part after the @ in the user's JID.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STREAM_SERVER, param_spec);

  param_spec = g_param_spec_string ("username", "Jabber username",
                                    "The username used when authenticating.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_USERNAME, param_spec);

  param_spec = g_param_spec_string ("password", "Jabber password",
                                    "The password used when authenticating.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PASSWORD, param_spec);

  param_spec = g_param_spec_string ("resource", "Jabber resource",
                                    "The Jabber resource used when authenticating.",
                                    "Telepathy",
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_RESOURCE, param_spec);

  param_spec = g_param_spec_int ("priority", "Jabber presence priority",
                                 "The default priority used when reporting our presence.",
                                 G_MININT8, G_MAXINT8, 0,
                                 G_PARAM_READWRITE |
                                 G_PARAM_STATIC_NAME |
                                 G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PRIORITY, param_spec);

  param_spec = g_param_spec_string ("https-proxy-server", "The server name "
                                    "used as an HTTPS proxy server",
                                    "The server name used as an HTTPS proxy "
                                    "server.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HTTPS_PROXY_SERVER, param_spec);

  param_spec = g_param_spec_uint ("https-proxy-port", "The HTTP proxy server "
                                  "port", "The HTTP proxy server port.",
                                  0, G_MAXUINT16, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HTTPS_PROXY_PORT, param_spec);

  param_spec = g_param_spec_string ("fallback-conference-server",
                                    "The conference server used as fallback",
                                    "The conference server used as fallback when "
                                    "everything else fails.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_FALLBACK_CONFERENCE_SERVER,
                                   param_spec);

  param_spec = g_param_spec_string ("stun-server",
                                    "STUN server",
                                    "STUN server.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_SERVER, param_spec);

  param_spec = g_param_spec_uint ("stun-port",
                                  "STUN port",
                                  "STUN port.",
                                  0, G_MAXUINT16, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_PORT, param_spec);

  param_spec = g_param_spec_string ("stun-relay-magic-cookie",
                                    "STUN relay magic cookie",
                                    "STUN relay magic cookie.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_RELAY_MAGIC_COOKIE,
                                   param_spec);

  param_spec = g_param_spec_string ("stun-relay-server",
                                    "STUN relay server",
                                    "STUN relay server.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_RELAY_SERVER,
                                   param_spec);

  param_spec = g_param_spec_uint ("stun-relay-udp-port",
                                  "STUN relay UDP port",
                                  "STUN relay UDP port.",
                                  0, G_MAXUINT16, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_RELAY_UDP_PORT,
                                   param_spec);

  param_spec = g_param_spec_uint ("stun-relay-tcp-port",
                                  "STUN relay TCP port",
                                  "STUN relay TCP port.",
                                  0, G_MAXUINT16, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_RELAY_TCP_PORT,
                                   param_spec);

  param_spec = g_param_spec_uint ("stun-relay-ssltcp-port",
                                  "STUN relay SSL-TCP port",
                                  "STUN relay SSL-TCP port.",
                                  0, G_MAXUINT16, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_RELAY_SSLTCP_PORT,
                                   param_spec);

  param_spec = g_param_spec_string ("stun-relay-username",
                                    "STUN relay username",
                                    "STUN relay username.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_RELAY_USERNAME,
                                   param_spec);

  param_spec = g_param_spec_string ("stun-relay-password",
                                    "STUN relay password",
                                    "STUN relay password.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_RELAY_PASSWORD,
                                   param_spec);

  param_spec = g_param_spec_boolean ("ignore-ssl-errors", "Ignore SSL errors",
                                     "Continue connecting even if the server's "
                                     "SSL certificate is invalid or missing.",
                                     FALSE,
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_IGNORE_SSL_ERRORS, param_spec);

  param_spec = g_param_spec_string ("alias",
                                    "Alias/nick for local user",
                                    "Alias/nick for local user",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ALIAS, param_spec);

  param_spec = g_param_spec_string ("auth-mac",
                                    "MAC for authorization",
                                    "MAC for authorization",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_AUTH_MAC, param_spec);

  param_spec = g_param_spec_string ("auth-btid",
                                    "BTID for authorization",
                                    "BTID for authorization",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_AUTH_BTID, param_spec);

  tp_properties_mixin_class_init (G_OBJECT_CLASS (gabble_connection_class),
      G_STRUCT_OFFSET (GabbleConnectionClass, properties_class),
      connection_property_signatures, NUM_CONN_PROPS, NULL);
}

static gboolean
_unref_lm_connection (gpointer data)
{
  LmConnection *conn = (LmConnection *) data;

  lm_connection_unref (conn);
  return FALSE;
}

static void
gabble_connection_dispose (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  DEBUG ("called");

  g_assert ((base->status == TP_CONNECTION_STATUS_DISCONNECTED) ||
            (base->status == TP_INTERNAL_CONNECTION_STATUS_NEW));
  g_assert (base->self_handle == 0);

  g_object_unref (self->vcard_manager);
  self->vcard_manager = NULL;

  /* unreffing channel factories frees the roster */
  self->roster = NULL;

  g_object_unref (self->disco);
  self->disco = NULL;

  if (self->self_presence != NULL)
    g_object_unref (self->self_presence);
  self->self_presence = NULL;

  g_object_unref (self->presence_cache);
  self->presence_cache = NULL;

  /* if this is not already the case, we'll crash anyway */
  g_assert (!lm_connection_is_open (self->lmconn));

  g_assert (priv->iq_disco_cb == NULL);
  g_assert (priv->iq_unknown_cb == NULL);
  g_assert (priv->stream_error_cb == NULL);

  /*
   * The Loudmouth connection can't be unref'd immediately because this
   * function might (indirectly) return into Loudmouth code which expects the
   * connection to always be there.
   */
  g_idle_add (_unref_lm_connection, self->lmconn);

  if (G_OBJECT_CLASS (gabble_connection_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_connection_parent_class)->dispose (object);
}

static void
gabble_connection_finalize (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  DEBUG ("called with %p", object);

  g_free (priv->connect_server);
  g_free (priv->stream_server);
  g_free (priv->username);
  g_free (priv->password);
  g_free (priv->resource);

  g_free (priv->https_proxy_server);
  g_free (priv->fallback_conference_server);

  g_free (priv->alias);

  g_free (priv->auth_mac);
  g_free (priv->auth_btid);

  tp_properties_mixin_finalize (object);

  G_OBJECT_CLASS (gabble_connection_parent_class)->finalize (object);
}

/**
 * _gabble_connection_set_properties_from_account
 *
 * Parses an account string which may be one of the following forms:
 *  username
 *  username/resource
 *  username@server
 *  username@server/resource
 * and sets the properties for username, stream server and resource
 * appropriately. Also sets the connect server to the stream server if one has
 * not yet been specified.
 */
gboolean
_gabble_connection_set_properties_from_account (GabbleConnection *conn,
                                                const gchar      *account,
                                                GError          **error)
{
  GabbleConnectionPrivate *priv;
  char *username, *server, *resource;
  gboolean result;

  g_assert (GABBLE_IS_CONNECTION (conn));
  g_assert (account != NULL);

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  username = server = resource = NULL;
  result = TRUE;

  gabble_decode_jid (account, &username, &server, &resource);

  if (username == NULL || server == NULL ||
      *username == '\0' || *server == '\0')
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "unable to get username and server from account");
      result = FALSE;
      goto OUT;
    }

  g_object_set (G_OBJECT (conn),
                "username", username,
                "stream-server", server,
                NULL);

  /* only override the default resource if we actually got one */
  if (resource)
    g_object_set (G_OBJECT (conn), "resource", resource, NULL);

OUT:
  g_free (username);
  g_free (server);
  g_free (resource);

  return result;
}


/**
 * _gabble_connection_send
 *
 * Send an LmMessage and trap network errors appropriately.
 */
gboolean
_gabble_connection_send (GabbleConnection *conn, LmMessage *msg, GError **error)
{
  GabbleConnectionPrivate *priv;
  GError *lmerror = NULL;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (!lm_connection_send (conn->lmconn, msg, &lmerror))
    {
      DEBUG ("failed: %s", lmerror->message);

      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "message send failed: %s", lmerror->message);

      g_error_free (lmerror);

      return FALSE;
    }

  return TRUE;
}

typedef struct {
    GabbleConnectionMsgReplyFunc reply_func;

    GabbleConnection *conn;
    LmMessage *sent_msg;
    gpointer user_data;

    GObject *object;
    gboolean object_alive;
} GabbleMsgHandlerData;

static LmHandlerResult
message_send_reply_cb (LmMessageHandler *handler,
                       LmConnection *connection,
                       LmMessage *reply_msg,
                       gpointer user_data)
{
  GabbleMsgHandlerData *handler_data = user_data;
  LmMessageSubType sub_type;

  sub_type = lm_message_get_sub_type (reply_msg);

  /* Is it a reply to this message? If we're talking to another loudmouth,
   * they can send us messages which have the same ID as ones we send. :-O */
  if (sub_type != LM_MESSAGE_SUB_TYPE_RESULT &&
      sub_type != LM_MESSAGE_SUB_TYPE_ERROR)
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (handler_data->object_alive)
    {
      return handler_data->reply_func (handler_data->conn,
                                       handler_data->sent_msg,
                                       reply_msg,
                                       handler_data->object,
                                       handler_data->user_data);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
message_send_object_destroy_notify_cb (gpointer data,
                                       GObject *where_the_object_was)
{
  GabbleMsgHandlerData *handler_data = data;

  handler_data->object = NULL;
  handler_data->object_alive = FALSE;
}

static void
message_send_handler_destroy_cb (gpointer data)
{
  GabbleMsgHandlerData *handler_data = data;

  lm_message_unref (handler_data->sent_msg);

  if (handler_data->object != NULL)
    {
      g_object_weak_unref (handler_data->object,
                           message_send_object_destroy_notify_cb,
                           handler_data);
    }

  g_slice_free (GabbleMsgHandlerData, handler_data);
}

/**
 * _gabble_connection_send_with_reply
 *
 * Send a tracked LmMessage and trap network errors appropriately.
 *
 * If object is non-NULL the handler will follow the lifetime of that object,
 * which means that if the object is destroyed the callback will not be invoked.
 */
gboolean
_gabble_connection_send_with_reply (GabbleConnection *conn,
                                    LmMessage *msg,
                                    GabbleConnectionMsgReplyFunc reply_func,
                                    GObject *object,
                                    gpointer user_data,
                                    GError **error)
{
  GabbleConnectionPrivate *priv;
  LmMessageHandler *handler;
  GabbleMsgHandlerData *handler_data;
  gboolean ret;
  GError *lmerror = NULL;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  lm_message_ref (msg);

  handler_data = g_slice_new (GabbleMsgHandlerData);
  handler_data->reply_func = reply_func;
  handler_data->conn = conn;
  handler_data->sent_msg = msg;
  handler_data->user_data = user_data;

  handler_data->object = object;
  handler_data->object_alive = TRUE;

  if (object != NULL)
    {
      g_object_weak_ref (object, message_send_object_destroy_notify_cb,
                         handler_data);
    }

  handler = lm_message_handler_new (message_send_reply_cb, handler_data,
                                    message_send_handler_destroy_cb);

  ret = lm_connection_send_with_reply (conn->lmconn, msg, handler, &lmerror);
  if (!ret)
    {
      DEBUG ("failed: %s", lmerror->message);

      if (error)
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "message send failed: %s", lmerror->message);
        }

      g_error_free (lmerror);
    }

  lm_message_handler_unref (handler);

  return ret;
}

static LmHandlerResult connection_iq_disco_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_iq_unknown_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_stream_error_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmSSLResponse connection_ssl_cb (LmSSL*, LmSSLStatus, gpointer);
static void connection_open_cb (LmConnection*, gboolean, gpointer);
static void connection_auth_cb (LmConnection*, gboolean, gpointer);
static void connection_disco_cb (GabbleDisco *, GabbleDiscoRequest *, const gchar *, const gchar *, LmMessageNode *, GError *, gpointer);
static void connection_disconnected_cb (LmConnection *, LmDisconnectReason, gpointer);

static void emit_one_presence_update (GabbleConnection *self, TpHandle handle);


static gboolean
do_connect (GabbleConnection *conn, GError **error)
{
  GError *lmerror = NULL;

  DEBUG ("calling lm_connection_open");

  if (!lm_connection_open (conn->lmconn, connection_open_cb,
                           conn, NULL, &lmerror))
    {
      DEBUG ("lm_connection_open failed %s", lmerror->message);

      g_set_error (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
          "lm_connection_open failed: %s", lmerror->message);

      g_error_free (lmerror);

      return FALSE;
    }

  return TRUE;
}

static void
connect_callbacks (TpBaseConnection *base)
{
  GabbleConnection *conn = GABBLE_CONNECTION (base);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_assert (priv->iq_disco_cb == NULL);
  g_assert (priv->iq_unknown_cb == NULL);
  g_assert (priv->stream_error_cb == NULL);

  priv->iq_disco_cb = lm_message_handler_new (connection_iq_disco_cb,
                                              conn, NULL);
  lm_connection_register_message_handler (conn->lmconn, priv->iq_disco_cb,
                                          LM_MESSAGE_TYPE_IQ,
                                          LM_HANDLER_PRIORITY_NORMAL);

  priv->iq_unknown_cb = lm_message_handler_new (connection_iq_unknown_cb,
                                            conn, NULL);
  lm_connection_register_message_handler (conn->lmconn, priv->iq_unknown_cb,
                                          LM_MESSAGE_TYPE_IQ,
                                          LM_HANDLER_PRIORITY_LAST);

  priv->stream_error_cb = lm_message_handler_new (connection_stream_error_cb,
                                            conn, NULL);
  lm_connection_register_message_handler (conn->lmconn, priv->stream_error_cb,
                                          LM_MESSAGE_TYPE_STREAM_ERROR,
                                          LM_HANDLER_PRIORITY_LAST);
}

static void
disconnect_callbacks (TpBaseConnection *base)
{
  GabbleConnection *conn = GABBLE_CONNECTION (base);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_assert (priv->iq_disco_cb != NULL);
  g_assert (priv->iq_unknown_cb != NULL);
  g_assert (priv->stream_error_cb != NULL);

  lm_connection_unregister_message_handler (conn->lmconn, priv->iq_disco_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_disco_cb);
  priv->iq_disco_cb = NULL;

  lm_connection_unregister_message_handler (conn->lmconn, priv->iq_unknown_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_unknown_cb);
  priv->iq_unknown_cb = NULL;

  lm_connection_unregister_message_handler (conn->lmconn, priv->stream_error_cb,
                                            LM_MESSAGE_TYPE_STREAM_ERROR);
  lm_message_handler_unref (priv->stream_error_cb);
  priv->stream_error_cb = NULL;
}

/**
 * _gabble_connection_connect
 *
 * Use the stored server & authentication details to commence
 * the stages for connecting to the server and authenticating. Will
 * re-use an existing LmConnection if it is present, or create it
 * if necessary.
 *
 * Stage 1 is _gabble_connection_connect calling lm_connection_open
 * Stage 2 is connection_open_cb calling lm_connection_authenticate
 * Stage 3 is connection_auth_cb initiating service discovery
 * Stage 4 is connection_disco_cb advertising initial presence, requesting
 *   the roster and setting the CONNECTED state
 */
static gboolean
_gabble_connection_connect (TpBaseConnection *base,
                            GError **error)
{
  GabbleConnection *conn = GABBLE_CONNECTION (base);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  char *jid;

  g_assert (priv->port > 0 && priv->port <= G_MAXUINT16);
  g_assert (priv->stream_server != NULL);
  g_assert (priv->username != NULL);
  g_assert (priv->password != NULL);
  g_assert (priv->resource != NULL);
  g_assert (lm_connection_is_open (conn->lmconn) == FALSE);

  jid = g_strdup_printf ("%s@%s", priv->username, priv->stream_server);
  lm_connection_set_jid (conn->lmconn, jid);

  base->self_handle = gabble_handle_for_contact (
      base->handles[TP_HANDLE_TYPE_CONTACT], jid, FALSE);
  g_free (jid);

  if (base->self_handle == 0)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Invalid JID: %s@%s", priv->username, priv->stream_server);
      return FALSE;
    }
  tp_handle_ref (base->handles[TP_HANDLE_TYPE_CONTACT], base->self_handle);

  /* set initial presence */
  conn->self_presence = gabble_presence_new ();
  gabble_presence_update (conn->self_presence, priv->resource,
      GABBLE_PRESENCE_AVAILABLE, NULL, priv->priority);

  /* set initial capabilities */
  gabble_presence_set_capabilities (conn->self_presence, priv->resource,
      capabilities_get_initial_caps (), priv->caps_serial++);

  /* always override server and port if one was forced upon us */
  if (priv->connect_server != NULL)
    {
      lm_connection_set_server (conn->lmconn, priv->connect_server);
      lm_connection_set_port (conn->lmconn, priv->port);
    }
  /* otherwise set the server & port to the stream server,
   * if one didn't appear from a SRV lookup */
  else if (lm_connection_get_server (conn->lmconn) == NULL)
    {
      lm_connection_set_server (conn->lmconn, priv->stream_server);
      lm_connection_set_port (conn->lmconn, priv->port);
    }

  if (priv->https_proxy_server)
    {
      LmProxy *proxy;

      proxy = lm_proxy_new_with_server (LM_PROXY_TYPE_HTTP,
          priv->https_proxy_server, priv->https_proxy_port);

      lm_connection_set_proxy (conn->lmconn, proxy);

      lm_proxy_unref (proxy);
    }

  if (priv->old_ssl)
    {
      LmSSL *ssl = lm_ssl_new (NULL, connection_ssl_cb, conn, NULL);
      lm_connection_set_ssl (conn->lmconn, ssl);
      lm_ssl_unref (ssl);
    }

  /* send whitespace to the server every 30 seconds */
  lm_connection_set_keep_alive_rate (conn->lmconn, 30);

  lm_connection_set_disconnect_function (conn->lmconn,
                                         connection_disconnected_cb,
                                         conn,
                                         NULL);

  return do_connect (conn, error);
}



static void
connection_disconnected_cb (LmConnection *lmconn,
                            LmDisconnectReason lm_reason,
                            gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  TpBaseConnection *base = (TpBaseConnection *)user_data;

  g_assert (conn->lmconn == lmconn);

  DEBUG ("called with reason %u", lm_reason);

  /* if we were expecting this disconnection, we're done so can tell
   * the connection manager to unref us. otherwise it's a network error
   * or some other screw up we didn't expect, so we emit the status
   * change */
  if (base->status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      DEBUG ("expected; emitting DISCONNECTED");
      tp_base_connection_finish_shutdown ((TpBaseConnection *)conn);
    }
  else
    {
      DEBUG ("unexpected; calling tp_base_connection_change_status");
      tp_base_connection_change_status ((TpBaseConnection *)conn,
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
    }
}


static void
connection_shut_down (TpBaseConnection *base)
{
  GabbleConnection *conn = GABBLE_CONNECTION (base);

  g_assert (GABBLE_IS_CONNECTION (conn));

  /* If we're shutting down by user request, we don't want to be
   * unreffed until the LM connection actually closes; the event handler
   * will tell the base class that shutdown has finished.
   *
   * On the other hand, if we're shutting down because the connection
   * suffered a network error, the LM connection will already be closed,
   * so just tell the base class to finish shutting down immediately.
   */
  if (lm_connection_is_open (conn->lmconn))
    {
      DEBUG ("still open; calling lm_connection_close");
      lm_connection_close (conn->lmconn, NULL);
    }
  else
    {
      /* lm_connection_is_open() returns FALSE if LmConnection is in the
       * middle of connecting, so call this just in case */
      lm_connection_cancel_open (conn->lmconn);
      DEBUG ("closed; emitting DISCONNECTED");
      tp_base_connection_finish_shutdown (base);
    }
}

static void
connection_presence_update_cb (GabblePresenceCache *cache, TpHandle handle, gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  emit_one_presence_update (conn, handle);
}

GabbleConnectionAliasSource
_gabble_connection_get_cached_alias (GabbleConnection *conn,
                                     TpHandle handle,
                                     gchar **alias)
{
  TpBaseConnection *base = (TpBaseConnection *)conn;
  GabbleConnectionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (conn,
      GABBLE_TYPE_CONNECTION, GabbleConnectionPrivate);
  GabbleConnectionAliasSource ret = GABBLE_CONNECTION_ALIAS_NONE;
  GabblePresence *pres;
  const gchar *tmp;
  gchar *user = NULL, *resource = NULL;

  g_return_val_if_fail (NULL != conn, GABBLE_CONNECTION_ALIAS_NONE);
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), GABBLE_CONNECTION_ALIAS_NONE);
  g_return_val_if_fail (tp_handle_is_valid (
        base->handles[TP_HANDLE_TYPE_CONTACT], handle, NULL),
      GABBLE_CONNECTION_ALIAS_NONE);

  tmp = gabble_roster_handle_get_name (conn->roster, handle);
  if (NULL != tmp)
    {
      ret = GABBLE_CONNECTION_ALIAS_FROM_ROSTER;

      if (NULL != alias)
        *alias = g_strdup (tmp);

      goto OUT;
    }

  pres = gabble_presence_cache_get (conn->presence_cache, handle);
  if (NULL != pres && NULL != pres->nickname)
    {
      ret = GABBLE_CONNECTION_ALIAS_FROM_PRESENCE;

      if (NULL != alias)
        *alias = g_strdup (pres->nickname);

      goto OUT;
    }

  /* XXX: should this be more important than the ones from presence? */
  /* if it's our own handle, use alias passed to the connmgr, if any */
  if (handle == base->self_handle && priv->alias != NULL)
    {
      ret = GABBLE_CONNECTION_ALIAS_FROM_CONNMGR;

      if (NULL != alias)
        *alias = g_strdup (priv->alias);

      goto OUT;
    }

  /* if we've seen a nickname in their vCard, use that */
  tmp = gabble_vcard_manager_get_cached_alias (conn->vcard_manager, handle);
  if (NULL != tmp)
    {
      ret = GABBLE_CONNECTION_ALIAS_FROM_VCARD;

      if (NULL != alias)
        *alias = g_strdup (tmp);

      goto OUT;
    }

  /* fallback to JID */
  tmp = tp_handle_inspect (base->handles[TP_HANDLE_TYPE_CONTACT], handle);
  g_assert (NULL != tmp);

  gabble_decode_jid (tmp, &user, NULL, &resource);

  /* MUC handles have the nickname in the resource */
  if (NULL != resource)
    {
      ret = GABBLE_CONNECTION_ALIAS_FROM_JID;

      if (NULL != alias)
        {
          *alias = resource;
          resource = NULL;
        }

      goto OUT;
    }

  /* otherwise just take their local part */
  if (NULL != user)
    {
      ret = GABBLE_CONNECTION_ALIAS_FROM_JID;

      if (NULL != alias)
        {
          *alias = user;
          user = NULL;
        }

      goto OUT;
    }

OUT:
  g_free (user);
  g_free (resource);
  return ret;
}

static void
connection_nickname_update_cb (GObject *object,
                               TpHandle handle,
                               gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionAliasSource signal_source, real_source;
  gchar *alias = NULL;
  GPtrArray *aliases;
  GValue entry = { 0, };

  if (object == G_OBJECT (conn->roster))
    {
      signal_source = GABBLE_CONNECTION_ALIAS_FROM_ROSTER;
    }
  else if (object == G_OBJECT (conn->presence_cache))
    {
      signal_source = GABBLE_CONNECTION_ALIAS_FROM_PRESENCE;
    }
   else if (object == G_OBJECT (conn->vcard_manager))
     {
       signal_source = GABBLE_CONNECTION_ALIAS_FROM_VCARD;
     }
  else
    {
      g_assert_not_reached ();
      return;
    }

  real_source = _gabble_connection_get_cached_alias (conn, handle, &alias);

  g_assert (real_source != GABBLE_CONNECTION_ALIAS_NONE);

  /* if the active alias for this handle is already known and from
   * a higher priority, this signal is not interesting so we do
   * nothing */
  if (real_source > signal_source)
    {
      DEBUG ("ignoring boring alias change for handle %u, signal from %u "
          "but source %u has alias \"%s\"", handle, signal_source,
          real_source, alias);
      goto OUT;
    }

  g_value_init (&entry, TP_ALIAS_PAIR_TYPE);
  g_value_take_boxed (&entry, dbus_g_type_specialized_construct
      (TP_ALIAS_PAIR_TYPE));

  dbus_g_type_struct_set (&entry,
      0, handle,
      1, alias,
      G_MAXUINT);

  aliases = g_ptr_array_sized_new (1);
  g_ptr_array_add (aliases, g_value_get_boxed (&entry));


  tp_svc_connection_interface_aliasing_emit_aliases_changed (conn, aliases);

  g_value_unset (&entry);
  g_ptr_array_free (aliases, TRUE);

OUT:
  g_free (alias);
}

static gboolean signal_own_presence (GabbleConnection *self, GError **error);

/* If the SHA1 has changed, this function will copy it to self_presence,
 * emit a signal and push it to the server. */
static gboolean
update_own_avatar_sha1 (GabbleConnection *conn,
                        const gchar *sha1,
                        GError **out_error)
{
  TpBaseConnection *base = (TpBaseConnection *)conn;
  GError *error = NULL;

  if (!tp_strdiff (sha1, conn->self_presence->avatar_sha1))
    return TRUE;

  tp_svc_connection_interface_avatars_emit_avatar_updated (conn,
      base->self_handle, sha1);

  g_free (conn->self_presence->avatar_sha1);
  conn->self_presence->avatar_sha1 = g_strdup (sha1);

  if (!signal_own_presence (conn, &error))
    {
      if (out_error == NULL)
        {
          DEBUG ("failed to signal changed avatar sha1 to the server: %s",
              error->message);
          g_error_free (error);
        }

      g_propagate_error (out_error, error);

      return FALSE;
    }

  return TRUE;
}

static void
connection_avatar_update_cb (GabblePresenceCache *cache,
                             TpHandle handle,
                             gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  TpBaseConnection *base = (TpBaseConnection *)conn;
  GabblePresence *presence;

  presence = gabble_presence_cache_get (conn->presence_cache, handle);

  g_assert (presence != NULL);

  gabble_vcard_manager_invalidate_cache (conn->vcard_manager, handle);

  if (handle == base->self_handle)
    update_own_avatar_sha1 (conn, presence->avatar_sha1, NULL);
  else
    tp_svc_connection_interface_avatars_emit_avatar_updated (conn,
        handle, presence->avatar_sha1);
}

/* Called when our vCard is first fetched, so we can start putting the
 * SHA-1 of an existing avatar in our presence. */
static void
connection_got_self_initial_avatar_cb (GObject *obj,
                                       gchar *sha1,
                                       gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  update_own_avatar_sha1 (conn, sha1, NULL);
}

/**
 * status_is_available
 *
 * Returns a boolean to indicate whether the given gabble status is
 * available on this connection.
 */
static gboolean
status_is_available (GabbleConnection *conn, int status)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (conn));
  g_assert (status < LAST_GABBLE_PRESENCE);
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (gabble_statuses[status].presence_type == TP_CONNECTION_PRESENCE_TYPE_HIDDEN &&
      (conn->features & GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE) == 0)
    return FALSE;
  else
    return TRUE;
}

static GHashTable *
construct_presence_hash (GabbleConnection *self,
                         const GArray *contact_handles)
{
  TpBaseConnection *base = (TpBaseConnection *)self;
  guint i;
  TpHandle handle;
  GHashTable *presence_hash, *contact_status, *parameters;
  GValueArray *vals;
  GValue *message;
  GabblePresence *presence;
  GabblePresenceId status;
  const gchar *status_message;
  /* this is never set at the moment*/
  guint timestamp = 0;

  g_assert (tp_handles_are_valid (base->handles[TP_HANDLE_TYPE_CONTACT],
        contact_handles, FALSE, NULL));

  presence_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_value_array_free);

  for (i = 0; i < contact_handles->len; i++)
    {
      handle = g_array_index (contact_handles, TpHandle, i);

      if (handle == base->self_handle)
        presence = self->self_presence;
      else
        presence = gabble_presence_cache_get (self->presence_cache, handle);

      if (presence)
        {
          status = presence->status;
          status_message = presence->status_message;
        }
      else
        {
          status = GABBLE_PRESENCE_OFFLINE;
          status_message = NULL;
        }

      parameters = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
          (GDestroyNotify) tp_g_value_slice_free);
      if (status_message != NULL) {
        message = g_slice_new0 (GValue);
        g_value_init (message, G_TYPE_STRING);
        g_value_set_static_string (message, status_message);


        g_hash_table_insert (parameters, "message", message);
      }

      contact_status = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
          (GDestroyNotify) g_hash_table_destroy);
      g_hash_table_insert (contact_status,
          (gchar *) gabble_statuses[status].name, parameters);

      vals = g_value_array_new (2);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 0), timestamp);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 1),
          dbus_g_type_get_map ("GHashTable", G_TYPE_STRING,
            dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)));
      g_value_take_boxed (g_value_array_get_nth (vals, 1), contact_status);

      g_hash_table_insert (presence_hash, GINT_TO_POINTER (handle), vals);
    }

  return presence_hash;
}

/**
 * emit_presence_update:
 * @self: A #GabbleConnection
 * @contact_handles: A zero-terminated array of #TpHandle for
 *                    the contacts to emit presence for
 *
 * Emits the Telepathy PresenceUpdate signal with the current
 * stored presence information for the given contact.
 */
static void
emit_presence_update (GabbleConnection *self,
                      const GArray *contact_handles)
{
  GHashTable *presence_hash;

  presence_hash = construct_presence_hash (self, contact_handles);
  tp_svc_connection_interface_presence_emit_presence_update (self,
      presence_hash);
  g_hash_table_destroy (presence_hash);
}

/**
 * emit_one_presence_update:
 * Convenience function for calling emit_presence_update with one handle.
 */

static void
emit_one_presence_update (GabbleConnection *self,
                          TpHandle handle)
{
  GArray *handles = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);

  g_array_insert_val (handles, 0, handle);
  emit_presence_update (self, handles);
  g_array_free (handles, TRUE);
}

/**
 * signal_own_presence:
 * @self: A #GabbleConnection
 * @error: pointer in which to return a GError in case of failure.
 *
 * Signal the user's stored presence to the jabber server
 *
 * Retuns: FALSE if an error occurred
 */
static gboolean
signal_own_presence (GabbleConnection *self, GError **error)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);
  GabblePresence *presence = self->self_presence;
  LmMessage *message = gabble_presence_as_message (presence, priv->resource);
  LmMessageNode *node = lm_message_get_node (message);
  gboolean ret;
  gchar *ext_string = NULL;
  GSList *features, *i;

  if (presence->status == GABBLE_PRESENCE_HIDDEN)
    {
      if ((self->features & GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE) != 0)
        lm_message_node_set_attribute (node, "type", "invisible");
    }

  /* TODO: why is this not part of presence -> msg? */
  features = capabilities_get_features (presence->caps);

  for (i = features; NULL != i; i = i->next)
    {
      const Feature *feat = (const Feature *) i->data;

      if ((NULL != feat->bundle) && tp_strdiff (VERSION, feat->bundle))
        {
          if (NULL != ext_string)
            {
              gchar *tmp = ext_string;
              ext_string = g_strdup_printf ("%s %s", ext_string, feat->bundle);
              g_free (tmp);
            }
          else
            {
              ext_string = g_strdup (feat->bundle);
            }
        }
    }

  node = lm_message_node_add_child (node, "c", NULL);
  lm_message_node_set_attributes (
    node,
    "xmlns", NS_CAPS,
    "node",  NS_GABBLE_CAPS,
    "ver",   VERSION,
    NULL);

  if (NULL != ext_string)
      lm_message_node_set_attribute (node, "ext", ext_string);

  ret = _gabble_connection_send (self, message, error);

  lm_message_unref (message);

  g_free (ext_string);
  g_slist_free (features);

  return ret;
}

static LmMessage *_lm_iq_message_make_result (LmMessage *iq_message);

/**
 * _gabble_connection_send_iq_result
 *
 * Function used to acknowledge an IQ stanza.
 */
void
_gabble_connection_acknowledge_set_iq (GabbleConnection *conn,
                                       LmMessage *iq)
{
  LmMessage *result;

  g_assert (LM_MESSAGE_TYPE_IQ == lm_message_get_type (iq));
  g_assert (LM_MESSAGE_SUB_TYPE_SET == lm_message_get_sub_type (iq));

  result = _lm_iq_message_make_result (iq);

  if (NULL != result)
    {
      _gabble_connection_send (conn, result, NULL);
      lm_message_unref (result);
    }
}


/**
 * _gabble_connection_send_iq_error
 *
 * Function used to acknowledge an IQ stanza with an error.
 */
void
_gabble_connection_send_iq_error (GabbleConnection *conn,
                                  LmMessage *message,
                                  GabbleXmppError error,
                                  const gchar *errmsg)
{
  const gchar *to, *id;
  LmMessage *msg;
  LmMessageNode *iq_node;

  iq_node = lm_message_get_node (message);
  to = lm_message_node_get_attribute (iq_node, "from");
  id = lm_message_node_get_attribute (iq_node, "id");

  if (id == NULL)
    {
      NODE_DEBUG (iq_node, "can't acknowledge IQ with no id");
      return;
    }

  msg = lm_message_new_with_sub_type (to, LM_MESSAGE_TYPE_IQ,
                                      LM_MESSAGE_SUB_TYPE_ERROR);

  lm_message_node_set_attribute (msg->node, "id", id);

  lm_message_node_steal_children (msg->node, iq_node);

  gabble_xmpp_error_to_node (error, msg->node, errmsg);

  _gabble_connection_send (conn, msg, NULL);

  lm_message_unref (msg);
}

static LmMessage *
_lm_iq_message_make_result (LmMessage *iq_message)
{
  LmMessage *result;
  LmMessageNode *iq, *result_iq;
  const gchar *from_jid, *id;

  g_assert (lm_message_get_type (iq_message) == LM_MESSAGE_TYPE_IQ);
  g_assert (lm_message_get_sub_type (iq_message) == LM_MESSAGE_SUB_TYPE_GET ||
            lm_message_get_sub_type (iq_message) == LM_MESSAGE_SUB_TYPE_SET);

  iq = lm_message_get_node (iq_message);
  id = lm_message_node_get_attribute (iq, "id");

  if (id == NULL)
    {
      NODE_DEBUG (iq, "can't acknowledge IQ with no id");
      return NULL;
    }

  from_jid = lm_message_node_get_attribute (iq, "from");

  result = lm_message_new_with_sub_type (from_jid, LM_MESSAGE_TYPE_IQ,
                                         LM_MESSAGE_SUB_TYPE_RESULT);
  result_iq = lm_message_get_node (result);
  lm_message_node_set_attribute (result_iq, "id", id);

  return result;
}

/**
 * connection_iq_disco_cb
 *
 * Called by loudmouth when we get an incoming <iq>. This handler handles
 * disco-related IQs.
 */
static LmHandlerResult
connection_iq_disco_cb (LmMessageHandler *handler,
                        LmConnection *connection,
                        LmMessage *message,
                        gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (user_data);
  LmMessage *result;
  LmMessageNode *iq, *result_iq, *query, *result_query;
  const gchar *node, *suffix;
  GSList *features;
  GSList *i;

  if (lm_message_get_sub_type (message) != LM_MESSAGE_SUB_TYPE_GET)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  iq = lm_message_get_node (message);
  query = lm_message_node_get_child_with_namespace (iq, "query",
      NS_DISCO_INFO);

  if (!query)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  node = lm_message_node_get_attribute (query, "node");

  if (node && (
      0 != strncmp (node, NS_GABBLE_CAPS "#", strlen (NS_GABBLE_CAPS) + 1) ||
      strlen(node) < strlen (NS_GABBLE_CAPS) + 2))
    {
      NODE_DEBUG (iq, "got iq disco query with unexpected node attribute");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (node == NULL)
    suffix = NULL;
  else
    suffix = node + strlen (NS_GABBLE_CAPS) + 1;

  result = _lm_iq_message_make_result (message);
  result_iq = lm_message_get_node (result);
  result_query = lm_message_node_add_child (result_iq, "query", NULL);
  lm_message_node_set_attribute (result_query, "xmlns", NS_DISCO_INFO);

  if (node)
    lm_message_node_set_attribute (result_query, "node", node);

  DEBUG ("got disco request for bundle %s, caps are %x", node, self->self_presence->caps);
  features = capabilities_get_features (self->self_presence->caps);

  g_debug("%s: caps now %u", G_STRFUNC, self->self_presence->caps);

  for (i = features; NULL != i; i = i->next)
    {
      const Feature *feature = (const Feature *) i->data;

      if (NULL == node || !tp_strdiff (suffix, feature->bundle))
        {
          LmMessageNode *node = lm_message_node_add_child (result_query,
              "feature", NULL);
          lm_message_node_set_attribute (node, "var", feature->ns);
        }
    }

  NODE_DEBUG (result_iq, "sending disco response");

  if (!lm_connection_send (self->lmconn, result, NULL))
    {
      DEBUG ("sending disco response failed");
    }

  g_slist_free (features);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/**
 * connection_iq_unknown_cb
 *
 * Called by loudmouth when we get an incoming <iq>. This handler is
 * at a lower priority than the others, and should reply with an error
 * about unsupported get/set attempts.
 */
static LmHandlerResult
connection_iq_unknown_cb (LmMessageHandler *handler,
                          LmConnection *connection,
                          LmMessage *message,
                          gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  g_assert (connection == conn->lmconn);

  NODE_DEBUG (message->node, "got unknown iq");

  switch (lm_message_get_sub_type (message))
    {
    case LM_MESSAGE_SUB_TYPE_GET:
    case LM_MESSAGE_SUB_TYPE_SET:
      _gabble_connection_send_iq_error (conn, message,
          XMPP_ERROR_SERVICE_UNAVAILABLE, NULL);
      break;
    default:
      break;
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/**
 * connection_stream_error_cb
 *
 * Called by loudmouth when we get stream error, which means that
 * we're about to close the connection. The message contains the reason
 * for the connection hangup.
 */
static LmHandlerResult
connection_stream_error_cb (LmMessageHandler *handler,
                            LmConnection *connection,
                            LmMessage *message,
                            gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  LmMessageNode *conflict_node;

  g_assert (connection == conn->lmconn);

  NODE_DEBUG (message->node, "got stream error");

  conflict_node = lm_message_node_get_child (message->node, "conflict");
  if (conflict_node)
    {
      DEBUG ("found conflict node, emiting status change");

      /* Another client with the same resource just
       * appeared, we're going down. */
        tp_base_connection_change_status ((TpBaseConnection *)conn,
            TP_CONNECTION_STATUS_DISCONNECTED,
            TP_CONNECTION_STATUS_REASON_NAME_IN_USE);
        return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}


/**
 * connection_ssl_cb
 *
 * If we're doing old SSL, this function gets called if the certificate
 * is dodgy.
 */
static LmSSLResponse
connection_ssl_cb (LmSSL      *lmssl,
                   LmSSLStatus status,
                   gpointer    data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  const char *reason;
  TpConnectionStatusReason tp_reason;

  switch (status) {
    case LM_SSL_STATUS_NO_CERT_FOUND:
      reason = "The server doesn't provide a certificate.";
      tp_reason = TP_CONNECTION_STATUS_REASON_CERT_NOT_PROVIDED;
      break;
    case LM_SSL_STATUS_UNTRUSTED_CERT:
      reason = "The certificate can not be trusted.";
      tp_reason = TP_CONNECTION_STATUS_REASON_CERT_UNTRUSTED;
      break;
    case LM_SSL_STATUS_CERT_EXPIRED:
      reason = "The certificate has expired.";
      tp_reason = TP_CONNECTION_STATUS_REASON_CERT_EXPIRED;
      break;
    case LM_SSL_STATUS_CERT_NOT_ACTIVATED:
      reason = "The certificate has not been activated.";
      tp_reason = TP_CONNECTION_STATUS_REASON_CERT_NOT_ACTIVATED;
      break;
    case LM_SSL_STATUS_CERT_HOSTNAME_MISMATCH:
      reason = "The server hostname doesn't match the one in the certificate.";
      tp_reason = TP_CONNECTION_STATUS_REASON_CERT_HOSTNAME_MISMATCH;
      break;
    case LM_SSL_STATUS_CERT_FINGERPRINT_MISMATCH:
      reason = "The fingerprint doesn't match the expected value.";
      tp_reason = TP_CONNECTION_STATUS_REASON_CERT_FINGERPRINT_MISMATCH;
      break;
    case LM_SSL_STATUS_GENERIC_ERROR:
      reason = "An unknown SSL error occurred.";
      tp_reason = TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR;
      break;
    default:
      g_assert_not_reached();
      reason = "Unknown SSL error code from Loudmouth.";
      tp_reason = TP_CONNECTION_STATUS_REASON_ENCRYPTION_ERROR;
      break;
  }

  DEBUG ("called: %s", reason);

  if (priv->ignore_ssl_errors)
    {
      return LM_SSL_RESPONSE_CONTINUE;
    }
  else
    {
      priv->ssl_error = tp_reason;
      return LM_SSL_RESPONSE_STOP;
    }
}

static void
do_auth (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GError *error = NULL;

  DEBUG ("authenticating with username: %s, password: <hidden>, resource: %s",
           priv->username, priv->resource);

  if (!lm_connection_authenticate (conn->lmconn, priv->username, priv->password,
                                   priv->resource, connection_auth_cb,
                                   conn, NULL, &error))
    {
      DEBUG ("failed: %s", error->message);
      g_error_free (error);

      /* the reason this function can fail is through network errors,
       * authentication failures are reported to our auth_cb */
      tp_base_connection_change_status ((TpBaseConnection *)conn,
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
    }
}

static void
registration_finished_cb (GabbleRegister *reg,
                          gboolean success,
                          gint err_code,
                          const gchar *err_msg,
                          gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  TpBaseConnection *base = (TpBaseConnection *)conn;

  if (base->status != TP_CONNECTION_STATUS_CONNECTING)
    {
      g_assert (base->status == TP_CONNECTION_STATUS_DISCONNECTED);
      return;
    }

  DEBUG ("%s", (success) ? "succeeded" : "failed");

  g_object_unref (reg);

  if (success)
    {
      do_auth (conn);
    }
  else
    {
      DEBUG ("err_code = %d, err_msg = '%s'",
               err_code, err_msg);

      tp_base_connection_change_status ((TpBaseConnection *)conn,
          TP_CONNECTION_STATUS_DISCONNECTED,
          (err_code == TP_ERROR_INVALID_ARGUMENT) ?
            TP_CONNECTION_STATUS_REASON_NAME_IN_USE :
            TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);
    }
}

static void
do_register (GabbleConnection *conn)
{
  GabbleRegister *reg;

  reg = gabble_register_new (conn);

  g_signal_connect (reg, "finished", (GCallback) registration_finished_cb,
                    conn);

  gabble_register_start (reg);
}

/**
 * connection_open_cb
 *
 * Stage 2 of connecting, this function is called by loudmouth after the
 * result of the non-blocking lm_connection_open call is known. It makes
 * a request to authenticate the user with the server, or optionally
 * registers user on the server first.
 */
static void
connection_open_cb (LmConnection *lmconn,
                    gboolean      success,
                    gpointer      data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  TpBaseConnection *base = (TpBaseConnection *)conn;

  if ((base->status != TP_CONNECTION_STATUS_CONNECTING) &&
      (base->status != TP_INTERNAL_CONNECTION_STATUS_NEW))
    {
      g_assert (base->status == TP_CONNECTION_STATUS_DISCONNECTED);
      return;
    }

  g_assert (priv);
  g_assert (lmconn == conn->lmconn);

  if (!success)
    {
      if (lm_connection_get_proxy (lmconn))
        {
          DEBUG ("failed, retrying without proxy");

          lm_connection_set_proxy (lmconn, NULL);

          if (do_connect (conn, NULL))
            {
              return;
            }
        }
      else
        {
          DEBUG ("failed");
        }

      if (priv->ssl_error)
        {
          tp_base_connection_change_status ((TpBaseConnection *)conn,
            TP_CONNECTION_STATUS_DISCONNECTED,
            priv->ssl_error);
        }
      else
        {
          tp_base_connection_change_status ((TpBaseConnection *)conn,
              TP_CONNECTION_STATUS_DISCONNECTED,
              TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
        }

      return;
    }

  if (!priv->do_register)
    do_auth (conn);
  else
    do_register (conn);
}

/**
 * connection_auth_cb
 *
 * Stage 3 of connecting, this function is called by loudmouth after the
 * result of the non-blocking lm_connection_authenticate call is known.
 * It sends a discovery request to find the server's features.
 */
static void
connection_auth_cb (LmConnection *lmconn,
                    gboolean      success,
                    gpointer      data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (data);
  TpBaseConnection *base = (TpBaseConnection *)conn;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GError *error = NULL;

  if (base->status != TP_CONNECTION_STATUS_CONNECTING)
    {
      g_assert (base->status == TP_CONNECTION_STATUS_DISCONNECTED);
      return;
    }

  g_assert (priv);
  g_assert (lmconn == conn->lmconn);

  if (!success)
    {
      DEBUG ("failed");

      tp_base_connection_change_status ((TpBaseConnection *)conn,
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);

      return;
    }

  if (!gabble_disco_request_with_timeout (conn->disco, GABBLE_DISCO_TYPE_INFO,
                                          priv->stream_server, NULL, 5000,
                                          connection_disco_cb, conn,
                                          G_OBJECT (conn), &error))
    {
      DEBUG ("sending disco request failed: %s",
          error->message);

      g_error_free (error);

      tp_base_connection_change_status ((TpBaseConnection *)conn,
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
    }
}

/**
 * connection_disco_cb
 *
 * Stage 4 of connecting, this function is called by GabbleDisco after the
 * result of the non-blocking server feature discovery call is known. It sends
 * the user's initial presence to the server, marking them as available,
 * and requests the roster.
 */
static void
connection_disco_cb (GabbleDisco *disco,
                     GabbleDiscoRequest *request,
                     const gchar *jid,
                     const gchar *node,
                     LmMessageNode *result,
                     GError *disco_error,
                     gpointer user_data)
{
  GabbleConnection *conn = user_data;
  TpBaseConnection *base = (TpBaseConnection *)conn;
  GabbleConnectionPrivate *priv;
  GError *error;

  if (base->status != TP_CONNECTION_STATUS_CONNECTING)
    {
      g_assert (base->status == TP_CONNECTION_STATUS_DISCONNECTED);
      return;
    }

  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (disco_error)
    {
      DEBUG ("got disco error, setting no features: %s", disco_error->message);
    }
  else
    {
      LmMessageNode *iter;

      NODE_DEBUG (result, "got");

      for (iter = result->children; iter != NULL; iter = iter->next)
        {
          if (0 == strcmp (iter->name, "feature"))
            {
              const gchar *var = lm_message_node_get_attribute (iter, "var");

              if (var == NULL)
                continue;

              if (0 == strcmp (var, NS_GOOGLE_JINGLE_INFO))
                conn->features |= GABBLE_CONNECTION_FEATURES_GOOGLE_JINGLE_INFO;
              else if (0 == strcmp (var, NS_GOOGLE_ROSTER))
                conn->features |= GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER;
              else if (0 == strcmp (var, NS_PRESENCE_INVISIBLE))
                conn->features |= GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE;
              else if (0 == strcmp (var, NS_PRIVACY))
                conn->features |= GABBLE_CONNECTION_FEATURES_PRIVACY;
            }
        }

      DEBUG ("set features flags to %d", conn->features);
    }

  /* send presence to the server to indicate availability */
  /* TODO: some way for the user to set this */
  if (!signal_own_presence (conn, &error))
    {
      DEBUG ("sending initial presence failed: %s", error->message);
      goto ERROR;
    }

  /* go go gadget on-line */
  tp_base_connection_change_status (base,
      TP_CONNECTION_STATUS_CONNECTED, TP_CONNECTION_STATUS_REASON_REQUESTED);

  emit_one_presence_update (conn, base->self_handle);

  return;

ERROR:
  if (error)
    g_error_free (error);

  tp_base_connection_change_status (base,
      TP_CONNECTION_STATUS_DISCONNECTED,
      TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

  return;
}


static GHashTable *
get_statuses_arguments()
{
  static GHashTable *arguments = NULL;

  if (arguments == NULL)
    {
      arguments = g_hash_table_new (g_str_hash, g_str_equal);

      g_hash_table_insert (arguments, "message", "s");
      g_hash_table_insert (arguments, "priority", "n");
    }

  return arguments;
}

/****************************************************************************
 *                          D-BUS EXPORTED METHODS                          *
 ****************************************************************************/


/**
 * gabble_connection_add_status
 *
 * Implements D-Bus method AddStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_add_status (TpSvcConnectionInterfacePresence *iface,
                              const gchar *status,
                              GHashTable *parms,
                              DBusGMethodInvocation *context)
{
  TpBaseConnection *self = TP_BASE_CONNECTION (iface);
  GError *error;
  GError unimpl = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
    "Only one status is possible at a time with this protocol" };

  ERROR_IF_NOT_CONNECTED_ASYNC (self, error, context);

  dbus_g_method_return_error (context, &unimpl);
}

static void
_emit_capabilities_changed (GabbleConnection *conn,
                            TpHandle handle,
                            GabblePresenceCapabilities old_caps,
                            GabblePresenceCapabilities new_caps)
{
  GPtrArray *caps_arr;
  const CapabilityConversionData *ccd;
  guint i;

  if (old_caps == new_caps)
    return;

  caps_arr = g_ptr_array_new ();

  for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
    {
      if (ccd->c2tf_fn (old_caps | new_caps))
        {
          GValue caps_monster_struct = {0, };
          guint old_tpflags = ccd->c2tf_fn (old_caps);
          guint old_caps = old_tpflags ?
            TP_CONNECTION_CAPABILITY_FLAG_CREATE |
            TP_CONNECTION_CAPABILITY_FLAG_INVITE : 0;
          guint new_tpflags = ccd->c2tf_fn (new_caps);
          guint new_caps = new_tpflags ?
            TP_CONNECTION_CAPABILITY_FLAG_CREATE |
            TP_CONNECTION_CAPABILITY_FLAG_INVITE : 0;

          if (0 == (old_tpflags ^ new_tpflags))
            continue;

          g_value_init (&caps_monster_struct,
              TP_CAPABILITIES_CHANGED_MONSTER_TYPE);
          g_value_take_boxed (&caps_monster_struct,
              dbus_g_type_specialized_construct
                (TP_CAPABILITIES_CHANGED_MONSTER_TYPE));

          dbus_g_type_struct_set (&caps_monster_struct,
              0, handle,
              1, ccd->iface,
              2, old_caps,
              3, new_caps,
              4, old_tpflags,
              5, new_tpflags,
              G_MAXUINT);

          g_ptr_array_add (caps_arr, g_value_get_boxed (&caps_monster_struct));
        }
    }

  if (caps_arr->len)
    tp_svc_connection_interface_capabilities_emit_capabilities_changed (
        conn, caps_arr);


  for (i = 0; i < caps_arr->len; i++)
    {
      g_boxed_free (TP_CAPABILITIES_CHANGED_MONSTER_TYPE,
          g_ptr_array_index (caps_arr, i));
    }
  g_ptr_array_free (caps_arr, TRUE);
}

static void
connection_capabilities_update_cb (GabblePresenceCache *cache,
                                   TpHandle handle,
                                   GabblePresenceCapabilities old_caps,
                                   GabblePresenceCapabilities new_caps,
                                   gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  _emit_capabilities_changed (conn, handle, old_caps, new_caps);
}

/**
 * gabble_connection_advertise_capabilities
 *
 * Implements D-Bus method AdvertiseCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_advertise_capabilities (TpSvcConnectionInterfaceCapabilities *iface,
                                          const GPtrArray *add,
                                          const gchar **remove,
                                          DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  guint i;
  GabblePresence *pres = self->self_presence;
  GabblePresenceCapabilities add_caps = 0, remove_caps = 0, caps, save_caps;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);
  const CapabilityConversionData *ccd;
  GPtrArray *ret;
  GError *error;

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context);

  DEBUG ("caps before: %x", pres->caps);

  for (i = 0; i < add->len; i++)
    {
      GValue iface_flags_pair = {0, };
      gchar *iface;
      guint flags;

      g_value_init (&iface_flags_pair, TP_CAPABILITY_PAIR_TYPE);
      g_value_set_static_boxed (&iface_flags_pair, g_ptr_array_index (add, i));

      dbus_g_type_struct_get (&iface_flags_pair,
                              0, &iface,
                              1, &flags,
                              G_MAXUINT);

      for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
          if (g_str_equal (iface, ccd->iface))
            add_caps |= ccd->tf2c_fn (flags);
    }

  for (i = 0; NULL != remove[i]; i++)
    {
      for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
          if (g_str_equal (remove[i], ccd->iface))
            remove_caps |= ccd->tf2c_fn (~0);
    }

  save_caps = caps = pres->caps;

  caps |= add_caps;
  caps ^= (caps & remove_caps);

  DEBUG ("caps to add: %x", add_caps);
  DEBUG ("caps to remove: %x", remove_caps);
  DEBUG ("caps after: %x", caps);

  if (caps ^ save_caps)
    {
      DEBUG ("before != after, changing");
      gabble_presence_set_capabilities (pres, priv->resource, caps, priv->caps_serial++);
      DEBUG ("set caps: %x", pres->caps);
    }

  ret = g_ptr_array_new ();

  for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
    {
      if (ccd->c2tf_fn (pres->caps))
        {
          GValue iface_flags_pair = {0, };

          g_value_init (&iface_flags_pair, TP_CAPABILITY_PAIR_TYPE);
          g_value_take_boxed (&iface_flags_pair,
              dbus_g_type_specialized_construct (TP_CAPABILITY_PAIR_TYPE));

          dbus_g_type_struct_set (&iface_flags_pair,
                                  0, ccd->iface,
                                  1, ccd->c2tf_fn (pres->caps),
                                  G_MAXUINT);

          g_ptr_array_add (ret, g_value_get_boxed (&iface_flags_pair));
        }
    }

  if (caps ^ save_caps)
    {
      if (!signal_own_presence (self, &error))
        {
          dbus_g_method_return_error (context, error);
          return;
        }

      _emit_capabilities_changed (self, base->self_handle,
          save_caps, caps);
    }

  tp_svc_connection_interface_capabilities_return_from_advertise_capabilities (
      context, ret);
  g_ptr_array_free (ret, TRUE);
}

/**
 * gabble_connection_clear_status
 *
 * Implements D-Bus method ClearStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_clear_status (TpSvcConnectionInterfacePresence *iface,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GError *error;
  GabbleConnectionPrivate *priv;
  gboolean ok;

  g_assert (GABBLE_IS_CONNECTION (self));

  priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context);

  gabble_presence_update (self->self_presence, priv->resource,
      GABBLE_PRESENCE_AVAILABLE, NULL, priv->priority);
  emit_one_presence_update (self, base->self_handle);
  ok = signal_own_presence (self, &error);

  if (!ok)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  tp_svc_connection_interface_presence_return_from_clear_status (
      context);
}


/**
 * gabble_connection_get_alias_flags
 *
 * Implements D-Bus method GetAliasFlags
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_get_alias_flags (TpSvcConnectionInterfaceAliasing *iface,
                                   DBusGMethodInvocation *context)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (iface);
  GError *error;

  g_assert (GABBLE_IS_CONNECTION (base));

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  tp_svc_connection_interface_aliasing_return_from_get_alias_flags (
      context, TP_CONNECTION_ALIAS_FLAG_USER_SET);
}

/**
 * gabble_connection_get_avatar_requirements
 *
 * Implements D-Bus method GetAvatarRequirements
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_get_avatar_requirements (TpSvcConnectionInterfaceAvatars *iface,
                                           DBusGMethodInvocation *context)
{
  /* Jabber prescribes no MIME type for avatars, but XEP-0153 says support
   * for image/png is REQUIRED, with image/jpeg and image/gif RECOMMENDED */
  static const char *mimetypes[] = { 
      "image/png", "image/jpeg", "image/gif", NULL };

  /* Jabber has no min/max width/height or max size, but XEP-0153 says
   * you SHOULD use 32-96px either way, and no more than 8K of data */

  tp_svc_connection_interface_avatars_return_from_get_avatar_requirements (
      context, mimetypes, 32, 32, 96, 96, 8192);
}


typedef struct {
  DBusGMethodInvocation *invocation;
  gchar **ret;
  guint my_index;
  gulong signal_conn;
} GetAvatarTokensContext;


static void
_got_self_avatar_for_get_avatar_tokens (GObject *obj,
                                        gchar *sha1,
                                        gpointer user_data)
{
  GetAvatarTokensContext *context = (GetAvatarTokensContext *) user_data;

  g_signal_handler_disconnect (obj, context->signal_conn);
  g_free (context->ret[context->my_index]);
  context->ret[context->my_index] = g_strdup(sha1);

  /* FIXME: I'm not entirely sure why gcc warns without this cast from
   * (gchar **) to (const gchar **) */
  tp_svc_connection_interface_avatars_return_from_get_avatar_tokens (
      context->invocation, (const gchar **)context->ret);
  g_strfreev (context->ret);

  g_slice_free (GetAvatarTokensContext, context);
}


/**
 * gabble_connection_get_avatar_tokens
 *
 * Implements D-Bus method GetAvatarTokens
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_get_avatar_tokens (TpSvcConnectionInterfaceAvatars *iface,
                                     const GArray *contacts,
                                     DBusGMethodInvocation *invocation)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  gboolean my_handle_requested = FALSE;
  guint i, my_index = 0;
  gchar **ret;
  GError *err;

  if (!tp_handles_are_valid (base->handles[TP_HANDLE_TYPE_CONTACT],
        contacts, FALSE, &err))
    {
      dbus_g_method_return_error (invocation, err);
      g_error_free (err);
      return;
    }

  ret = g_new0 (gchar *, contacts->len + 1);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle;
      GabblePresence *presence;

      handle = g_array_index (contacts, TpHandle, i);
      presence = gabble_presence_cache_get (self->presence_cache, handle);

      /* TODO: always call the callback so we can defer presence lookups until
       * we return the method, then we don't need to strdup the strings we're
       * returning. */
      if (NULL != presence && NULL != presence->avatar_sha1)
          ret[i] = g_strdup (presence->avatar_sha1);
      else
          ret[i] = g_strdup ("");

      if (base->self_handle == handle)
        {
          my_handle_requested = TRUE;
          my_index = i;
        }
    }

  if (my_handle_requested)
    {
      gboolean have_self_avatar;

      g_object_get (self->vcard_manager,
          "have-self-avatar", &have_self_avatar,
          NULL);

      if (!have_self_avatar)
        {
          GetAvatarTokensContext *context = g_slice_new (GetAvatarTokensContext);

          context->invocation = invocation;
          context->my_index = my_index;
          context->ret = ret;
          context->signal_conn = g_signal_connect (self->vcard_manager,
              "got-self-initial-avatar",
              G_CALLBACK (_got_self_avatar_for_get_avatar_tokens),
              context);

          return;
        }
    }

  /* FIXME: I'm not entirely sure why gcc warns without this cast from
   * (gchar **) to (const gchar **) */
  tp_svc_connection_interface_avatars_return_from_get_avatar_tokens (
      invocation, (const gchar **)ret);
  g_strfreev (ret);
}


static const gchar *assumed_caps[] =
{
  TP_IFACE_CHANNEL_TYPE_TEXT,
  TP_IFACE_CHANNEL_INTERFACE_GROUP,
  NULL
};


/**
 * gabble_connection_get_capabilities
 *
 * Implements D-Bus method GetCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_get_capabilities (TpSvcConnectionInterfaceCapabilities *iface,
                                    const GArray *handles,
                                    DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  guint i;
  GPtrArray *ret;
  GError *error;

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context);

  if (!tp_handles_are_valid (base->handles[TP_HANDLE_TYPE_CONTACT],
                             handles, TRUE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  ret = g_ptr_array_new ();

  for (i = 0; i < handles->len; i++)
    {
      TpHandle handle = g_array_index (handles, guint, i);
      GabblePresence *pres;
      const CapabilityConversionData *ccd;
      guint typeflags;
      const gchar **assumed;

      if (0 == handle)
        {
          /* FIXME report the magical channel types available on the connection itself */
          continue;
        }

      if (handle == base->self_handle)
        pres = self->self_presence;
      else
        pres = gabble_presence_cache_get (self->presence_cache, handle);

      if (NULL != pres)
        for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
          {
            typeflags = ccd->c2tf_fn (pres->caps);

            if (typeflags)
              {
                GValue monster = {0, };

                g_value_init (&monster, TP_GET_CAPABILITIES_MONSTER_TYPE);
                g_value_take_boxed (&monster,
                    dbus_g_type_specialized_construct (
                      TP_GET_CAPABILITIES_MONSTER_TYPE));

                dbus_g_type_struct_set (&monster,
                    0, handle,
                    1, ccd->iface,
                    2, TP_CONNECTION_CAPABILITY_FLAG_CREATE |
                        TP_CONNECTION_CAPABILITY_FLAG_INVITE,
                    3, typeflags,
                    G_MAXUINT);

                g_ptr_array_add (ret, g_value_get_boxed (&monster));
              }
          }

      for (assumed = assumed_caps; NULL != *assumed; assumed++)
        {
          GValue monster = {0, };

          g_value_init (&monster, TP_GET_CAPABILITIES_MONSTER_TYPE);
          g_value_take_boxed (&monster,
              dbus_g_type_specialized_construct (TP_GET_CAPABILITIES_MONSTER_TYPE));

          dbus_g_type_struct_set (&monster,
              0, handle,
              1, *assumed,
              2, TP_CONNECTION_CAPABILITY_FLAG_CREATE |
                  TP_CONNECTION_CAPABILITY_FLAG_INVITE,
              3, 0,
              G_MAXUINT);

          g_ptr_array_add (ret, g_value_get_boxed (&monster));
        }
    }

  tp_svc_connection_interface_capabilities_return_from_get_capabilities (
      context, ret);
  g_ptr_array_free (ret, TRUE);
}


/**
 * gabble_connection_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_get_interfaces (TpSvcConnection *iface,
                                  DBusGMethodInvocation *context)
{
  const char *interfaces[] = {
      TP_IFACE_CONNECTION_INTERFACE_ALIASING,
      TP_IFACE_CONNECTION_INTERFACE_CAPABILITIES,
      TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
      TP_IFACE_CONNECTION_INTERFACE_AVATARS,
      TP_IFACE_PROPERTIES_INTERFACE,
      NULL };
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GabbleConnectionPrivate *priv;
  GError *error;

  g_assert (GABBLE_IS_CONNECTION (self));

  priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  tp_svc_connection_return_from_get_interfaces(
      context, interfaces);
}


/**
 * gabble_connection_get_presence
 *
 * Implements D-Bus method GetPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_get_presence (TpSvcConnectionInterfacePresence *iface,
                                const GArray *contacts,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GHashTable *presence_hash;
  GError *error = NULL;

  if (!tp_handles_are_valid (base->handles[TP_HANDLE_TYPE_CONTACT],
        contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  presence_hash = construct_presence_hash (self, contacts);
  tp_svc_connection_interface_presence_return_from_get_presence (
      context, presence_hash);
  g_hash_table_destroy (presence_hash);
}


/**
 * gabble_connection_get_statuses
 *
 * Implements D-Bus method GetStatuses
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_get_statuses (TpSvcConnectionInterfacePresence *iface,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GHashTable *ret;
  GError *error;
  GabbleConnectionPrivate *priv;
  GValueArray *status;
  int i;

  g_assert (GABBLE_IS_CONNECTION (self));

  priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  DEBUG ("called.");

  ret = g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) g_value_array_free);

  for (i=0; i < LAST_GABBLE_PRESENCE; i++)
    {
      /* don't report the invisible presence if the server
       * doesn't have the presence-invisible feature */
      if (!status_is_available (self, i))
        continue;

      status = g_value_array_new (5);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (status, 0),
          gabble_statuses[i].presence_type);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 1), G_TYPE_BOOLEAN);
      g_value_set_boolean (g_value_array_get_nth (status, 1),
          gabble_statuses[i].self);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 2), G_TYPE_BOOLEAN);
      g_value_set_boolean (g_value_array_get_nth (status, 2),
          gabble_statuses[i].exclusive);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 3),
          DBUS_TYPE_G_STRING_STRING_HASHTABLE);
      g_value_set_static_boxed (g_value_array_get_nth (status, 3),
          get_statuses_arguments());

      g_hash_table_insert (ret, (gchar*) gabble_statuses[i].name, status);
    }

  tp_svc_connection_interface_presence_return_from_get_statuses (
      context, ret);
  g_hash_table_destroy (ret);
}


/**
 * gabble_connection_remove_status
 *
 * Implements D-Bus method RemoveStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_remove_status (TpSvcConnectionInterfacePresence *iface,
                                 const gchar *status,
                                 DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GabblePresence *presence = self->self_presence;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);
  GError *error;

  g_assert (GABBLE_IS_CONNECTION (self));

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  if (strcmp (status, gabble_statuses[presence->status].name) == 0)
    {
      gabble_presence_update (presence, priv->resource,
          GABBLE_PRESENCE_AVAILABLE, NULL, priv->priority);
      emit_one_presence_update (self, base->self_handle);
      if (signal_own_presence (self, &error))
        {
          tp_svc_connection_interface_presence_return_from_remove_status (
              context);
        }
      else
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
        }
    }
  else
    {
      GError nonexistent = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Attempting to remove non-existent presence." };
      dbus_g_method_return_error (context, &nonexistent);
    }
}


typedef struct _AliasesRequest AliasesRequest;

struct _AliasesRequest
{
  GabbleConnection *conn;
  DBusGMethodInvocation *request_call;
  guint pending_vcard_requests;
  GArray *contacts;
  GabbleVCardManagerRequest **vcard_requests;
  gchar **aliases;
};


static AliasesRequest *
aliases_request_new (GabbleConnection *conn,
                     DBusGMethodInvocation *request_call,
                     const GArray *contacts)
{
  AliasesRequest *request;

  request = g_slice_new0 (AliasesRequest);
  request->conn = conn;
  request->request_call = request_call;
  request->contacts = g_array_new (FALSE, FALSE, sizeof (TpHandle));
  g_array_insert_vals (request->contacts, 0, contacts->data, contacts->len);
  request->vcard_requests =
    g_new0 (GabbleVCardManagerRequest *, contacts->len);
  request->aliases = g_new0 (gchar *, contacts->len + 1);
  return request;
}


static void
aliases_request_free (AliasesRequest *request)
{
  guint i;

  for (i = 0; i < request->contacts->len; i++)
    {
      if (request->vcard_requests[i] != NULL)
        gabble_vcard_manager_cancel_request (request->conn->vcard_manager,
            request->vcard_requests[i]);
    }

  g_array_free (request->contacts, TRUE);
  g_free (request->vcard_requests);
  g_strfreev (request->aliases);
  g_slice_free (AliasesRequest, request);
}


static gboolean
aliases_request_try_return (AliasesRequest *request)
{
  if (request->pending_vcard_requests == 0)
    {
      /* FIXME: I'm not entirely sure why gcc warns without this cast from
       * (gchar **) to (const gchar **) */
      tp_svc_connection_interface_aliasing_return_from_request_aliases (
          request->request_call, (const gchar **)request->aliases);
      return TRUE;
    }

  return FALSE;
}


static void
aliases_request_vcard_cb (GabbleVCardManager *manager,
                          GabbleVCardManagerRequest *request,
                          TpHandle handle,
                          LmMessageNode *vcard,
                          GError *error,
                          gpointer user_data)
{
  AliasesRequest *aliases_request = (AliasesRequest *) user_data;
  GabbleConnectionAliasSource source;
  guint i;
  gboolean found = FALSE;
  gchar *alias = NULL;

  g_assert (aliases_request->pending_vcard_requests > 0);

  /* The index of the vCard request in the vCard request array is the
   * index of the contact/alias in their respective arrays. */

  for (i = 0; i < aliases_request->contacts->len; i++)
    if (aliases_request->vcard_requests[i] == request)
      {
        found = TRUE;
        break;
      }

  g_assert (found);
  source = _gabble_connection_get_cached_alias (aliases_request->conn,
      g_array_index (aliases_request->contacts, TpHandle, i), &alias);
  g_assert (source != GABBLE_CONNECTION_ALIAS_NONE);
  g_assert (NULL != alias);

  aliases_request->pending_vcard_requests--;
  aliases_request->vcard_requests[i] = NULL;
  aliases_request->aliases[i] = alias;

  if (aliases_request_try_return (aliases_request))
    aliases_request_free (aliases_request);
}


/**
 * gabble_connection_request_aliases
 *
 * Implements D-Bus method RequestAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_request_aliases (TpSvcConnectionInterfaceAliasing *iface,
                                   const GArray *contacts,
                                   DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  guint i;
  AliasesRequest *request;
  GError *error = NULL;

  g_assert (GABBLE_IS_CONNECTION (self));

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  if (!tp_handles_are_valid (base->handles[TP_HANDLE_TYPE_CONTACT],
        contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  request = aliases_request_new (self, context, contacts);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GabbleConnectionAliasSource source;
      GabbleVCardManagerRequest *vcard_request;
      gchar *alias;

      source = _gabble_connection_get_cached_alias (self, handle, &alias);
      g_assert (source != GABBLE_CONNECTION_ALIAS_NONE);
      g_assert (NULL != alias);

      if (source >= GABBLE_CONNECTION_ALIAS_FROM_VCARD ||
          gabble_vcard_manager_has_cached_alias (self->vcard_manager, handle))
        {
          /* Either the alias we got was from a vCard or better, or we already
           * tried getting an alias from a vcard and failed, so there's no
           * point trying again. */
          request->aliases[i] = alias;
        }
      else
        {
          DEBUG ("requesting vCard for alias of contact %s",
              tp_handle_inspect (base->handles[TP_HANDLE_TYPE_CONTACT],
                handle));

          g_free (alias);

          if (gabble_vcard_manager_get_cached (self->vcard_manager, handle, NULL))
            {
                GabbleConnectionAliasSource source;
                source = _gabble_connection_get_cached_alias (self, handle, &alias);
                request->vcard_requests[i] = NULL;
                request->aliases[i] = alias;
                request->pending_vcard_requests++;
                continue;
            }

          vcard_request = gabble_vcard_manager_request (self->vcard_manager,
              handle, 0, aliases_request_vcard_cb, request, G_OBJECT (self),
              &error);

          if (NULL != error)
            {
              dbus_g_method_return_error (context, error);
              g_error_free (error);
              aliases_request_free (request);
              return;
            }

          request->vcard_requests[i] = vcard_request;
          request->pending_vcard_requests++;
        }
    }

  if (aliases_request_try_return (request))
    aliases_request_free (request);
}


static void
_request_avatar_cb (GabbleVCardManager *self,
                    GabbleVCardManagerRequest *request,
                    TpHandle handle,
                    LmMessageNode *vcard,
                    GError *vcard_error,
                    gpointer user_data)
{
  DBusGMethodInvocation *context = (DBusGMethodInvocation *) user_data;
  GabbleConnection *conn;
  TpBaseConnection *base;
  LmMessageNode *photo_node, *type_node, *binval_node;
  const gchar *mime_type;
  GArray *arr;
  GError *error = NULL;
  GString *avatar;
  GabblePresence *presence;

  g_object_get (self, "connection", &conn, NULL);
  base = TP_BASE_CONNECTION (conn);

  if (NULL == vcard)
    {
      dbus_g_method_return_error (context, vcard_error);
      goto out;
    }

  photo_node = lm_message_node_get_child (vcard, "PHOTO");

  if (NULL == photo_node)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "contact vCard has no photo");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      goto out;
    }

  type_node = lm_message_node_get_child (photo_node, "TYPE");

  if (NULL == type_node)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "contact avatar is missing type node");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      goto out;
    }

  binval_node = lm_message_node_get_child (photo_node, "BINVAL");

  if (NULL == binval_node)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "contact avatar is missing binval node");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      goto out;
    }

  avatar = base64_decode (lm_message_node_get_value (binval_node));

  if (NULL == avatar)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "failed to decode avatar from base64");
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      goto out;
    }

  if (handle == base->self_handle)
    presence = conn->self_presence;
  else
    presence = gabble_presence_cache_get (conn->presence_cache, handle);

  if (presence != NULL)
    {
      gchar *sha1;

      sha1 = sha1_hex (avatar->str, avatar->len);

      if (tp_strdiff (presence->avatar_sha1, sha1))
        {
          /* the thinking here is that we have to return an error, because we
           * can't give the user the vcard they're expecting, which has the
           * hash from the time that they requested it. */
          DEBUG ("treason uncloaked! avatar hash in presence does not match "
              "avatar in vCard for handle %u", handle);

          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "avatar hash in presence does not match avatar in vCard");
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          error = NULL;

          if (handle == base->self_handle)
            {
              update_own_avatar_sha1 (conn, sha1, NULL);
              g_free (sha1);
            }
          else
            {
              g_free (presence->avatar_sha1);
              presence->avatar_sha1 = sha1; /* take ownership */

              tp_svc_connection_interface_avatars_emit_avatar_updated (
                  conn, handle, sha1);
            }

          goto out;
        }

      g_free (sha1);
    }

  mime_type = lm_message_node_get_value (type_node);
  arr = g_array_new (FALSE, FALSE, sizeof (gchar));
  g_array_append_vals (arr, avatar->str, avatar->len);
  tp_svc_connection_interface_avatars_return_from_request_avatar (
      context, arr, mime_type);
  g_array_free (arr, TRUE);

out:
  g_object_unref (conn);
}

/**
 * gabble_connection_request_avatar
 *
 * Implements D-Bus method RequestAvatar
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_request_avatar (TpSvcConnectionInterfaceAvatars *iface,
                                  guint contact,
                                  DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  LmMessageNode *vcard_node;

  if (gabble_vcard_manager_get_cached (self->vcard_manager,
      contact, &vcard_node))
    {
      _request_avatar_cb (self->vcard_manager, NULL, contact, vcard_node, NULL,
          context);
    }
  else
    {
      gabble_vcard_manager_request (self->vcard_manager, contact, 0,
          _request_avatar_cb, context, NULL, NULL);
    }
}


static void
hold_and_return_handles (DBusGMethodInvocation *context,
                         TpHandleRepoIface *repo,
                         GArray *handles)
{
  GError *error;
  gchar *sender = dbus_g_method_get_sender(context);
  guint i;

  for (i = 0; i < handles->len; i++)
    {
      TpHandle handle = (TpHandle) g_array_index (handles, guint, i);
      if (!tp_handle_client_hold (repo, sender, handle, &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }
    }
  dbus_g_method_return (context, handles);
}


const char *
_gabble_connection_find_conference_server (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (priv->conference_server == NULL)
    {
      /* Find first server that has NS_MUC feature */
      const GabbleDiscoItem *item = gabble_disco_service_find (conn->disco,
          NULL, NULL, NS_MUC);
      if (item != NULL)
        priv->conference_server = item->jid;
    }

  if (priv->conference_server == NULL)
    priv->conference_server = priv->fallback_conference_server;

  return priv->conference_server;
}


static gchar *
_gabble_connection_get_canonical_room_name (GabbleConnection *conn,
                                           const gchar *name)
{
  const gchar *server;

  g_assert (GABBLE_IS_CONNECTION (conn));

  if (index (name, '@'))
    return g_strdup (name);

  server = _gabble_connection_find_conference_server (conn);

  if (server == NULL)
    return NULL;

  return g_strdup_printf ("%s@%s", name, server);
}


typedef struct _RoomVerifyContext RoomVerifyContext;

typedef struct {
    GabbleConnection *conn;
    DBusGMethodInvocation *invocation;
    gboolean errored;
    guint count;
    GArray *handles;
    RoomVerifyContext *contexts;
} RoomVerifyBatch;

struct _RoomVerifyContext {
    gchar *jid;
    guint index;
    RoomVerifyBatch *batch;
    GabbleDiscoRequest *request;
};

static void
room_verify_batch_free (RoomVerifyBatch *batch)
{
  guint i;

  g_array_free (batch->handles, TRUE);
  for (i = 0; i < batch->count; i++)
    {
      g_free(batch->contexts[i].jid);
    }
  g_free (batch->contexts);
  g_slice_free (RoomVerifyBatch, batch);
}

/* Frees the error and the batch. */
static void
room_verify_batch_raise_error (RoomVerifyBatch *batch,
                               GError *error)
{
  guint i;

  dbus_g_method_return_error (batch->invocation, error);
  g_error_free (error);
  batch->errored = TRUE;
  for (i = 0; i < batch->count; i++)
    {
      if (batch->contexts[i].request)
        {
          gabble_disco_cancel_request(batch->conn->disco,
                                      batch->contexts[i].request);
        }
    }
  room_verify_batch_free (batch);
}

static RoomVerifyBatch *
room_verify_batch_new (GabbleConnection *conn,
                       DBusGMethodInvocation *invocation,
                       guint count,
                       const gchar **jids)
{
  TpBaseConnection *base = (TpBaseConnection *)conn;
  RoomVerifyBatch *batch = g_slice_new (RoomVerifyBatch);
  guint i;

  batch->errored = FALSE;
  batch->conn = conn;
  batch->invocation = invocation;
  batch->count = count;
  batch->handles = g_array_sized_new(FALSE, FALSE, sizeof(TpHandle), count);
  batch->contexts = g_new0(RoomVerifyContext, count);
  for (i = 0; i < count; i++)
    {
      const gchar *name = jids[i];
      gchar *qualified_name;
      TpHandle handle;

      batch->contexts[i].index = i;
      batch->contexts[i].batch = batch;

      qualified_name = _gabble_connection_get_canonical_room_name (conn, name);

      if (!qualified_name)
        {
          GError *error;
          DEBUG ("requested handle %s contains no conference server",
                 name);
          error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "requested "
                  "room handle %s does not specify a server, but we have not discovered "
                  "any local conference servers and no fallback was provided", name);
          room_verify_batch_raise_error (batch, error);
          return NULL;
        }

      batch->contexts[i].jid = qualified_name;

      /* has the handle been verified before? */
      if (gabble_handle_for_room_exists (
            base->handles[TP_HANDLE_TYPE_ROOM], qualified_name, FALSE))
        {
          handle = gabble_handle_for_room (
              base->handles[TP_HANDLE_TYPE_ROOM], qualified_name);
        }
      else
        {
          handle = 0;
        }
      g_array_append_val (batch->handles, handle);
    }

  return batch;
}

/* If all handles in the array have been disco'd or got from cache,
free the batch and return TRUE. Else return FALSE. */
static gboolean
room_verify_batch_try_return (RoomVerifyBatch *batch)
{
  guint i;
  TpBaseConnection *conn = (TpBaseConnection *)batch->conn;

  for (i = 0; i < batch->count; i++)
    {
      if (!g_array_index(batch->handles, TpHandle, i))
        {
          /* we're not ready yet */
          return FALSE;
        }
    }

  hold_and_return_handles (batch->invocation,
      conn->handles[TP_HANDLE_TYPE_ROOM], batch->handles);
  room_verify_batch_free (batch);
  return TRUE;
}

static void
room_jid_disco_cb (GabbleDisco *disco,
                   GabbleDiscoRequest *request,
                   const gchar *jid,
                   const gchar *node,
                   LmMessageNode *query_result,
                   GError *error,
                   gpointer user_data)
{
  RoomVerifyContext *rvctx = user_data;
  RoomVerifyBatch *batch = rvctx->batch;
  TpBaseConnection *conn = (TpBaseConnection *)batch->conn;
  LmMessageNode *lm_node;
  gboolean found = FALSE;
  TpHandle handle;

  /* stop the request getting cancelled after it's already finished */
  rvctx->request = NULL;

  /* if an error is being handled already, quietly go away */
  if (batch->errored)
    {
      return;
    }

  if (error != NULL)
    {
      DEBUG ("disco reply error %s", error->message);

      /* disco will free the old error, _raise_error will free the new one */
      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "can't retrieve room info: %s", error->message);

      room_verify_batch_raise_error (batch, error);

      return;
    }

  for (lm_node = query_result->children; lm_node; lm_node = lm_node->next)
    {
      const gchar *var;

      if (tp_strdiff (lm_node->name, "feature"))
        continue;

      var = lm_message_node_get_attribute (lm_node, "var");

      /* for servers who consider schema compliance to be an optional bonus */
      if (var == NULL)
        var = lm_message_node_get_attribute (lm_node, "type");

      if (!tp_strdiff (var, NS_MUC))
        {
          found = TRUE;
          break;
        }
    }

  if (!found)
    {
      DEBUG ("no MUC support for service name in jid %s", rvctx->jid);

      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "specified server "
          "doesn't support MUC");

      room_verify_batch_raise_error (batch, error);

      return;
    }

  handle = gabble_handle_for_room (
      conn->handles[TP_HANDLE_TYPE_ROOM], rvctx->jid);
  g_assert (handle != 0);

  DEBUG ("disco reported MUC support for service name in jid %s", rvctx->jid);
  g_array_index (batch->handles, TpHandle, rvctx->index) = handle;

  /* if this was the last callback to be run, send off the result */
  room_verify_batch_try_return (batch);
}

/**
 * room_jid_verify:
 *
 * Utility function that verifies that the service name of
 * the specified jid exists and reports MUC support.
 */
static gboolean
room_jid_verify (RoomVerifyBatch *batch,
                 guint index,
                 DBusGMethodInvocation *context)
{
  gchar *room, *service;
  gboolean ret;
  GError *error;

  room = service = NULL;
  gabble_decode_jid (batch->contexts[index].jid, &room, &service, NULL);

  g_assert (room && service);

  ret = (gabble_disco_request (batch->conn->disco, GABBLE_DISCO_TYPE_INFO,
                               service, NULL, room_jid_disco_cb,
                               batch->contexts + index,
                               G_OBJECT (batch->conn), &error) != NULL);
  if (!ret)
    {
      room_verify_batch_raise_error (batch, error);
    }

  g_free (room);
  g_free (service);

  return ret;
}


/**
 * gabble_connection_request_handles
 *
 * Implements D-Bus method RequestHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_request_handles (TpSvcConnection *iface,
                                   guint handle_type,
                                   const gchar **names,
                                   DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  guint count = 0, i;
  const gchar **cur_name;
  GError *error = NULL;
  GArray *handles = NULL;
  RoomVerifyBatch *batch = NULL;

  for (cur_name = names; *cur_name != NULL; cur_name++)
    {
      count++;
    }

  g_assert (GABBLE_IS_CONNECTION (self));

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  if (!tp_handle_type_is_valid (handle_type, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  switch (handle_type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      handles = g_array_sized_new(FALSE, FALSE, sizeof(TpHandle), count);

      for (i = 0; i < count; i++)
        {
          TpHandle handle;
          const gchar *name = names[i];

          if (!gabble_handle_jid_is_valid (handle_type, name, &error))
            {
              dbus_g_method_return_error (context, error);
              g_error_free (error);

              g_array_free (handles, TRUE);
              return;
            }

          handle = gabble_handle_for_contact (
              base->handles[TP_HANDLE_TYPE_CONTACT], name, FALSE);

          if (handle == 0)
            {
              DEBUG ("requested handle %s was invalid", name);

              error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                                   "requested handle %s was invalid", name);
              dbus_g_method_return_error (context, error);
              g_error_free (error);

              g_array_free (handles, TRUE);
              return;
            }

          g_array_append_val(handles, handle);
        }
      hold_and_return_handles (context,
          base->handles[TP_HANDLE_TYPE_CONTACT],handles);
      g_array_free(handles, TRUE);
      break;

    case TP_HANDLE_TYPE_ROOM:
      batch = room_verify_batch_new (self, context, count, names);
      if (!batch)
        {
          /* an error occurred while setting up the batch, and we returned error
          to dbus */
          return;
        }

      /* have all the handles been verified already? If so, nothing to do */
      if (room_verify_batch_try_return (batch))
        {
          return;
        }

      for (i = 0; i < count; i++)
        {
          if (!room_jid_verify (batch, i, context))
            {
              return;
            }
        }

      /* we've set the verification process going - the callback will handle
      returning or raising error */
      break;

    case TP_HANDLE_TYPE_LIST:
    case TP_HANDLE_TYPE_GROUP:
      handles = g_array_sized_new(FALSE, FALSE, sizeof(TpHandle), count);

      for (i = 0; i < count; i++)
        {
          TpHandle handle;
          const gchar *name = names[i];

          handle = tp_handle_request (base->handles[handle_type],
              name, TRUE);

          if (handle == 0)
            {
              DEBUG ("requested %s channel %s not available", 
                     handle_type == TP_HANDLE_TYPE_LIST ? "list" : "group",
                     name);

              error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                                   "requested %s channel %s not available",
                                   handle_type == TP_HANDLE_TYPE_LIST ? "list"
                                                                      : "group",
                                   name);
              dbus_g_method_return_error (context, error);
              g_error_free (error);

              g_array_free (handles, TRUE);
              return;
            }
          g_array_append_val(handles, handle);
        }
      hold_and_return_handles (context, base->handles[handle_type],
          handles);
      g_array_free(handles, TRUE);
      break;

    default:
      DEBUG ("unimplemented handle type %u", handle_type);

      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                          "unimplemented handle type %u", handle_type);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}


/**
 * gabble_connection_request_presence
 *
 * Implements D-Bus method RequestPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 */
static void
gabble_connection_request_presence (TpSvcConnectionInterfacePresence *iface,
                                    const GArray *contacts,
                                    DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GabbleConnectionPrivate *priv;
  GError *error;

  g_assert (GABBLE_IS_CONNECTION (self));

  priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  if (!tp_handles_are_valid (base->handles[TP_HANDLE_TYPE_CONTACT],
        contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  if (contacts->len)
    emit_presence_update (self, contacts);

  tp_svc_connection_interface_presence_return_from_request_presence (
      context);
}


struct _i_hate_g_hash_table_foreach
{
  GabbleConnection *conn;
  GError **error;
  gboolean retval;
};

static void
setaliases_foreach (gpointer key, gpointer value, gpointer user_data)
{
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach *) user_data;
  TpHandle handle = GPOINTER_TO_INT (key);
  gchar *alias = (gchar *) value;
  GError *error = NULL;
  TpBaseConnection *base = (TpBaseConnection *)data->conn;

  if (!tp_handle_is_valid (base->handles[TP_HANDLE_TYPE_CONTACT],
        handle, &error))
    {
      data->retval = FALSE;
    }
  else if (base->self_handle == handle)
    {
      /* only alter the roster if we're already there, e.g. because someone
       * added us with another client
       */
      if (gabble_roster_handle_has_entry (data->conn->roster, handle)
          && !gabble_roster_handle_set_name (data->conn->roster, handle,
                                             alias, data->error))
        {
          data->retval = FALSE;
        }
    }
  else if (!gabble_roster_handle_set_name (data->conn->roster, handle, alias,
        data->error))
    {
      data->retval = FALSE;
    }

  if (base->self_handle == handle)
    {
      /* User has done SetAliases on themselves - patch their vCard.
       * FIXME: because SetAliases is currently synchronous, we ignore errors
       * here, and just let the request happen in the background
       */
      gabble_vcard_manager_edit (data->conn->vcard_manager, 0, NULL, NULL,
          G_OBJECT(data->conn), NULL, "NICKNAME", alias, NULL);
    }

  if (NULL != error)
    {
      if (NULL == *(data->error))
        {
          *(data->error) = error;
        }
      else
        {
          g_error_free (error);
        }
    }
}

/**
 * gabble_connection_set_aliases
 *
 * Implements D-Bus method SetAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 */
static void
gabble_connection_set_aliases (TpSvcConnectionInterfaceAliasing *iface,
                               GHashTable *aliases,
                               DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GError *error;
  GabbleConnectionPrivate *priv;
  struct _i_hate_g_hash_table_foreach data = { NULL, NULL, TRUE };

  g_assert (GABBLE_IS_CONNECTION (self));

  priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  data.conn = self;
  data.error = &error;

  g_hash_table_foreach (aliases, setaliases_foreach, &data);

  if (data.retval)
    {
      tp_svc_connection_interface_aliasing_return_from_set_aliases (
          context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}


struct _set_avatar_ctx {
  GabbleConnection *conn;
  DBusGMethodInvocation *invocation;
  LmMessage *new_vcard_msg;
  GString *avatar;
  gchar *mime_type;
};


static void
_set_avatar_ctx_free (struct _set_avatar_ctx *ctx)
{
  lm_message_unref (ctx->new_vcard_msg);
  if (ctx->avatar)
      g_string_free (ctx->avatar, TRUE);
  g_free (ctx->mime_type);
  g_free (ctx);
}


static void
_set_avatar_cb2 (GabbleVCardManager *manager,
                 GabbleVCardManagerRequest *request,
                 TpHandle handle,
                 LmMessageNode *vcard,
                 GError *vcard_error,
                 gpointer user_data)
{
  struct _set_avatar_ctx *ctx = (struct _set_avatar_ctx *) user_data;
  TpBaseConnection *base = (TpBaseConnection *)ctx->conn;

  if (NULL == vcard)
    {
      dbus_g_method_return_error (ctx->invocation, vcard_error);
    }
  else
    {
      GabblePresence *presence = ctx->conn->self_presence;
      GError *error;

      g_free (presence->avatar_sha1);
      if (ctx->avatar)
        {
          presence->avatar_sha1 = sha1_hex (ctx->avatar->str,
                                            ctx->avatar->len);
        }
      else
        {
          presence->avatar_sha1 = NULL;
        }

      if (signal_own_presence (ctx->conn, &error))
        {
          tp_svc_connection_interface_avatars_return_from_set_avatar (
              ctx->invocation, presence->avatar_sha1);
          tp_svc_connection_interface_avatars_emit_avatar_updated (
              ctx->conn, base->self_handle, presence->avatar_sha1);
        }
      else
        {
          dbus_g_method_return_error (ctx->invocation, error);
          g_error_free (error);
        }
    }

  _set_avatar_ctx_free (ctx);
}


static void
_set_avatar_cb1 (GabbleVCardManager *manager,
                 GabbleVCardManagerRequest *request,
                 TpHandle handle,
                 LmMessageNode *vcard,
                 GError *vcard_error,
                 gpointer user_data)
{
  struct _set_avatar_ctx *ctx = (struct _set_avatar_ctx *) user_data;
  LmMessageNode *new_vcard, *photo_node, *type_node, *binval_node, *i, *next;
  gchar *encoded;

  if (NULL == vcard)
    {
      dbus_g_method_return_error (ctx->invocation, vcard_error);
      _set_avatar_ctx_free (ctx);
      return;
    }

  ctx->new_vcard_msg = lm_message_new (NULL, LM_MESSAGE_TYPE_UNKNOWN);
  new_vcard = lm_message_node_add_child (ctx->new_vcard_msg->node, "vCard", "");
  lm_message_node_set_attribute (new_vcard, "xmlns", NS_VCARD_TEMP);
  lm_message_node_steal_children (new_vcard, vcard);

  for (i = new_vcard->children; i; i = next)
    {
      next = i->next;
      if (0 == strcmp (i->name, "PHOTO"))
        {
          lm_message_node_unlink (i);
          lm_message_node_unref (i);
        }
    }

  photo_node = lm_message_node_add_child (new_vcard, "PHOTO", "");

  if (ctx->avatar)
    {

      type_node = lm_message_node_get_child (photo_node, "TYPE");

      if (NULL == type_node)
        type_node = lm_message_node_add_child (photo_node, "TYPE", "");

      lm_message_node_set_value (type_node, ctx->mime_type);

      binval_node = lm_message_node_get_child (photo_node, "BINVAL");

      if (NULL == binval_node)
        binval_node = lm_message_node_add_child (photo_node, "BINVAL", "");

      encoded = base64_encode (ctx->avatar);
      lm_message_node_set_value (binval_node, encoded);
      g_free (encoded);
    }

  gabble_vcard_manager_replace (
    manager, new_vcard, 0, _set_avatar_cb2, ctx, NULL, NULL);
}


/**
 * gabble_connection_set_avatar
 *
 * Implements D-Bus method SetAvatar
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_set_avatar (TpSvcConnectionInterfaceAvatars *iface,
                              const GArray *avatar,
                              const gchar *mime_type,
                              DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  struct _set_avatar_ctx *ctx = g_new0 (struct _set_avatar_ctx, 1);

  ctx->conn = self;
  ctx->invocation = context;
  if (avatar)
    {
      ctx->avatar = g_string_new_len (avatar->data, avatar->len);
      ctx->mime_type = g_strdup (mime_type);
    }

  DEBUG ("called");
  gabble_vcard_manager_invalidate_cache (self->vcard_manager,
      base->self_handle);
  gabble_vcard_manager_request (self->vcard_manager,
      base->self_handle, 0, _set_avatar_cb1, ctx, NULL, NULL);
}

/**
 * gabble_connection_clear_avatar
 *
 * Implements D-Bus method ClearAvatar
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_clear_avatar (TpSvcConnectionInterfaceAvatars *iface,
                                DBusGMethodInvocation *context)
{
  gabble_connection_set_avatar (iface, NULL, NULL, context);
}


/**
 * gabble_connection_set_last_activity_time
 *
 * Implements D-Bus method SetLastActivityTime
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 */
static void
gabble_connection_set_last_activity_time (TpSvcConnectionInterfacePresence *iface,
                                          guint time,
                                          DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GabbleConnectionPrivate *priv;
  GError *error;

  g_assert (GABBLE_IS_CONNECTION (self));

  priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  tp_svc_connection_interface_presence_return_from_set_last_activity_time (
      context);
}


static void
setstatuses_foreach (gpointer key, gpointer value, gpointer user_data)
{
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach*) user_data;
  TpBaseConnection *base = (TpBaseConnection *)data->conn;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (data->conn);

  int i;

  for (i = 0; i < LAST_GABBLE_PRESENCE; i++)
    {
      if (0 == strcmp (gabble_statuses[i].name, (const gchar*) key))
        break;
    }

  if (i < LAST_GABBLE_PRESENCE)
    {
      GHashTable *args = (GHashTable *)value;
      GValue *message = g_hash_table_lookup (args, "message");
      GValue *priority = g_hash_table_lookup (args, "priority");
      const gchar *status = NULL;
      gint8 prio = priv->priority;

      if (!status_is_available (data->conn, i))
        {
          DEBUG ("requested status %s is not available", (const gchar *) key);
          g_set_error (data->error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "requested status '%s' is not available on this connection",
              (const gchar *) key);
          data->retval = FALSE;
          return;
        }

      if (message)
        {
          if (!G_VALUE_HOLDS_STRING (message))
            {
              DEBUG ("got a status message which was not a string");
              g_set_error (data->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "Status argument 'message' requires a string");
              data->retval = FALSE;
              return;
            }
          status = g_value_get_string (message);
        }

      if (priority)
        {
          if (!G_VALUE_HOLDS_INT (priority))
            {
              DEBUG ("got a priority value which was not a signed integer");
              g_set_error (data->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "Status argument 'priority' requires a signed integer");
              data->retval = FALSE;
              return;
            }
          prio = CLAMP (g_value_get_int (priority), G_MININT8, G_MAXINT8);
        }

      gabble_presence_update (data->conn->self_presence, priv->resource, i, status, prio);
      emit_one_presence_update (data->conn, base->self_handle);
      data->retval = signal_own_presence (data->conn, data->error);
    }
  else
    {
      DEBUG ("got unknown status identifier %s", (const gchar *) key);
      g_set_error (data->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "unknown status identifier: %s", (const gchar *) key);
      data->retval = FALSE;
    }
}

/**
 * gabble_connection_set_status
 *
 * Implements D-Bus method SetStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_set_status (TpSvcConnectionInterfacePresence *iface,
                              GHashTable *statuses,
                              DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GabbleConnectionPrivate *priv;
  struct _i_hate_g_hash_table_foreach data = { NULL, NULL, TRUE };
  GError *error;

  g_assert (GABBLE_IS_CONNECTION (self));

  priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  if (g_hash_table_size (statuses) != 1)
    {
      GError invalid = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Only one status may be set at a time in this protocol" };
      DEBUG ("got more than one status");
      dbus_g_method_return_error (context, &invalid);
      return;
    }

  data.conn = self;
  data.error = &error;
  g_hash_table_foreach (statuses, setstatuses_foreach, &data);

  if (data.retval)
    {
      tp_svc_connection_interface_presence_return_from_set_status (
          context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

static void
conn_service_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionClass *klass = (TpSvcConnectionClass *)g_iface;

#define IMPLEMENT(x) tp_svc_connection_implement_##x (klass, \
    gabble_connection_##x)
  IMPLEMENT(get_interfaces);
  IMPLEMENT(request_handles);
#undef IMPLEMENT
}

static void
aliasing_service_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceAliasingClass *klass = (TpSvcConnectionInterfaceAliasingClass *)g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(get_alias_flags);
  IMPLEMENT(request_aliases);
  IMPLEMENT(set_aliases);
#undef IMPLEMENT
}

static void
avatars_service_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceAvatarsClass *klass = (TpSvcConnectionInterfaceAvatarsClass *)g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_avatars_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(get_avatar_requirements);
  IMPLEMENT(get_avatar_tokens);
  IMPLEMENT(request_avatar);
  IMPLEMENT(set_avatar);
  IMPLEMENT(clear_avatar);
#undef IMPLEMENT
}

static void
capabilities_service_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceCapabilitiesClass *klass = (TpSvcConnectionInterfaceCapabilitiesClass *)g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_capabilities_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(advertise_capabilities);
  IMPLEMENT(get_capabilities);
#undef IMPLEMENT
}

static void
presence_service_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfacePresenceClass *klass = (TpSvcConnectionInterfacePresenceClass *)g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_presence_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(add_status);
  IMPLEMENT(clear_status);
  IMPLEMENT(get_presence);
  IMPLEMENT(get_statuses);
  IMPLEMENT(remove_status);
  IMPLEMENT(request_presence);
  IMPLEMENT(set_last_activity_time);
  IMPLEMENT(set_status);
#undef IMPLEMENT
}
