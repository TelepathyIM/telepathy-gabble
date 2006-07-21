/*
 * gabble-connection.c - Source for GabbleConnection
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

#include "config.h"

#define DBUS_API_SUBJECT_TO_CHANGE

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <glib-object.h>
#include <loudmouth/loudmouth.h>
#include <stdlib.h>
#include <string.h>

#include "handles.h"
#include "handle-set.h"
#include "telepathy-constants.h"
#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"

#include "tp-channel-iface.h"
#include "tp-channel-factory-iface.h"

#include "gabble-connection.h"
#include "gabble-connection-glue.h"
#include "gabble-connection-signals-marshal.h"

#include "disco.h"
#include "gabble-presence-cache.h"
#include "gabble-presence.h"
#include "jingle-info.h"
#include "gabble-register.h"
#include "im-factory.h"
#include "muc-factory.h"
#include "namespaces.h"
#include "roster.h"
#include "util.h"

#include "gabble-media-channel.h"
#include "gabble-roomlist-channel.h"

#define BUS_NAME        "org.freedesktop.Telepathy.Connection.gabble"
#define OBJECT_PATH     "/org/freedesktop/Telepathy/Connection/gabble"

#define TP_ALIAS_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID))
#define TP_CAPABILITY_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID))
#define TP_CHANNEL_LIST_ENTRY_TYPE (dbus_g_type_get_struct ("GValueArray", \
      DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, \
      G_TYPE_INVALID))

#define ERROR_IF_NOT_CONNECTED(CONN, ERROR) \
  if ((CONN)->status != TP_CONN_STATUS_CONNECTED) \
    { \
      g_debug ("%s: rejected request as disconnected", G_STRFUNC); \
      (ERROR) = g_error_new(TELEPATHY_ERRORS, NotAvailable, \
                            "Connection is disconnected"); \
      return FALSE; \
    }

#define ERROR_IF_NOT_CONNECTED_ASYNC(CONN, ERROR, CONTEXT) \
  if ((CONN)->status != TP_CONN_STATUS_CONNECTED) \
    { \
      g_debug ("%s: rejected request as disconnected", G_STRFUNC); \
      (ERROR) = g_error_new(TELEPATHY_ERRORS, NotAvailable, \
                            "Connection is disconnected"); \
      dbus_g_method_return_error ((CONTEXT), (ERROR)); \
      g_error_free ((ERROR)); \
      return FALSE; \
    }


G_DEFINE_TYPE(GabbleConnection, gabble_connection, G_TYPE_OBJECT)

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
 { "offline",   TP_CONN_PRESENCE_TYPE_OFFLINE,       TRUE, TRUE },
 { "hidden",    TP_CONN_PRESENCE_TYPE_HIDDEN,        TRUE, TRUE },
 { "xa",        TP_CONN_PRESENCE_TYPE_EXTENDED_AWAY, TRUE, TRUE },
 { "away",      TP_CONN_PRESENCE_TYPE_AWAY,          TRUE, TRUE },
 { "dnd",       TP_CONN_PRESENCE_TYPE_AWAY,          TRUE, TRUE },
 { "available", TP_CONN_PRESENCE_TYPE_AVAILABLE,     TRUE, TRUE },
 { "chat",      TP_CONN_PRESENCE_TYPE_AVAILABLE,     TRUE, TRUE }
};

/* signal enum */
enum
{
    ALIASES_CHANGED,
    NEW_CHANNEL,
    PRESENCE_UPDATE,
    STATUS_CHANGED,
    DISCONNECTED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
    PROP_PROTOCOL = 1,
    PROP_CONNECT_SERVER,
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

const GabblePropertySignature connection_property_signatures[NUM_CONN_PROPS] = {
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
  LmMessageHandler *iq_jingle_info_cb;
  LmMessageHandler *iq_jingle_cb;
  LmMessageHandler *iq_disco_cb;
  LmMessageHandler *iq_unknown_cb;

  /* telepathy properties */
  gchar *protocol;

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

  /*
   * FIXME: remove these when stored in properties mixin
  gchar *stun_server;
  guint stun_port;
  gchar *stun_relay_magic_cookie;
  gchar *stun_relay_server;
  guint stun_relay_udp_port;
  guint stun_relay_tcp_port;
  guint stun_relay_ssltcp_port;
  gchar *stun_relay_username;
  gchar *stun_relay_password;
  */

  /* authentication properties */
  gchar *stream_server;
  gchar *username;
  gchar *password;
  gchar *resource;
  gint8 priority;

  /* jingle sessions */
  GHashTable *jingle_sessions;

  GPtrArray *media_channels;
  guint media_channel_index;

  GabbleRoomlistChannel *roomlist_channel;

  /* clients */
  GData *client_contact_handle_sets;
  GData *client_room_handle_sets;
  GData *client_list_handle_sets;

  /* server services */
  GList *conference_servers;

  /* channel factories */
  GPtrArray *channel_factories;
  GPtrArray *channel_requests;
  gboolean suppress_next_handler;

  /* gobject housekeeping */
  gboolean dispose_has_run;
};

#define GABBLE_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_CONNECTION, GabbleConnectionPrivate))

typedef struct _ChannelRequest ChannelRequest;

struct _ChannelRequest
{
  DBusGMethodInvocation *context;
  gchar *channel_type;
  guint handle_type;
  guint handle;
  gboolean suppress_handler;
};

static void connection_new_channel_cb (TpChannelFactoryIface *, GObject *, gpointer);
static void connection_channel_error_cb (TpChannelFactoryIface *, GObject *, GError *, gpointer);
static void connection_nickname_update_cb (GObject *, GabbleHandle, gpointer);
static void connection_presence_update_cb (GabblePresenceCache *, GabbleHandle, gpointer);

static void
gabble_connection_init (GabbleConnection *obj)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (obj);
  guint i;
  GValue val = { 0, };

  obj->lmconn = lm_connection_new (NULL);
  obj->status = TP_CONN_STATUS_DISCONNECTED;
  obj->handles = gabble_handle_repo_new ();
  obj->disco = gabble_disco_new (obj);

  obj->presence_cache = gabble_presence_cache_new (obj);
  g_signal_connect (obj->presence_cache, "nickname-update", G_CALLBACK
      (connection_nickname_update_cb), obj);
  g_signal_connect (obj->presence_cache, "presence-update", G_CALLBACK
      (connection_presence_update_cb), obj);

  obj->roster = gabble_roster_new (obj);
  g_signal_connect (obj->roster, "nickname-update", G_CALLBACK
      (connection_nickname_update_cb), obj);

  priv->channel_factories = g_ptr_array_sized_new (1);

  g_ptr_array_add (priv->channel_factories, obj->roster);

  g_ptr_array_add (priv->channel_factories,
                   g_object_new (GABBLE_TYPE_MUC_FACTORY, "connection", obj, NULL));

  g_ptr_array_add (priv->channel_factories,
                   g_object_new (GABBLE_TYPE_IM_FACTORY, "connection", obj, NULL));

  for (i = 0; i < priv->channel_factories->len; i++)
    {
      GObject *factory = g_ptr_array_index (priv->channel_factories, i);
      g_signal_connect (factory, "new-channel", G_CALLBACK
          (connection_new_channel_cb), obj);
      g_signal_connect (factory, "channel-error", G_CALLBACK
          (connection_channel_error_cb), obj);
    }

  priv->channel_requests = g_ptr_array_new ();

  priv->jingle_sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, NULL);

  priv->media_channels = g_ptr_array_sized_new (1);
  priv->media_channel_index = 0;

  g_datalist_init (&priv->client_contact_handle_sets);
  g_datalist_init (&priv->client_room_handle_sets);
  g_datalist_init (&priv->client_list_handle_sets);

  /* Set default parameters for optional parameters */
  priv->resource = g_strdup (GABBLE_PARAMS_DEFAULT_RESOURCE);
  priv->port = GABBLE_PARAMS_DEFAULT_PORT;
  priv->https_proxy_port = GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT;

  /* initialize properties mixin */
  gabble_properties_mixin_init (G_OBJECT (obj), G_STRUCT_OFFSET (
        GabbleConnection, properties));

  g_value_init (&val, G_TYPE_UINT);
  g_value_set_uint (&val, GABBLE_PARAMS_DEFAULT_STUN_PORT);

  gabble_properties_mixin_change_value (G_OBJECT (obj), CONN_PROP_STUN_PORT,
                                        &val, NULL);
  gabble_properties_mixin_change_flags (G_OBJECT (obj), CONN_PROP_STUN_PORT,
                                        TP_PROPERTY_FLAG_READ, 0, NULL);

  g_value_unset (&val);
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
    case PROP_PROTOCOL:
      g_value_set_string (value, priv->protocol);
      break;
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
/* FIXME: remove these
    case PROP_STUN_SERVER:
      g_value_set_string (value, priv->stun_server);
      break;
    case PROP_STUN_PORT:
      g_value_set_uint (value, priv->stun_port);
      break;
    case PROP_STUN_RELAY_MAGIC_COOKIE:
      g_value_set_string (value, priv->stun_relay_magic_cookie);
      break;
    case PROP_STUN_RELAY_SERVER:
      g_value_set_string (value, priv->stun_relay_server);
      break;
    case PROP_STUN_RELAY_UDP_PORT:
      g_value_set_uint (value, priv->stun_relay_udp_port);
      break;
    case PROP_STUN_RELAY_TCP_PORT:
      g_value_set_uint (value, priv->stun_relay_tcp_port);
      break;
    case PROP_STUN_RELAY_SSLTCP_PORT:
      g_value_set_uint (value, priv->stun_relay_ssltcp_port);
      break;
    case PROP_STUN_RELAY_USERNAME:
      g_value_set_string (value, priv->stun_relay_username);
      break;
    case PROP_STUN_RELAY_PASSWORD:
      g_value_set_string (value, priv->stun_relay_password);
      break;
*/
    default:
      param_name = g_param_spec_get_name (pspec);

      if (gabble_properties_mixin_has_property (object, param_name,
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
    case PROP_PROTOCOL:
      g_free (priv->protocol);
      priv->protocol = g_value_dup_string (value);
      break;
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
/* FIXME: remove these
    case PROP_STUN_SERVER:
      g_free (priv->stun_server);
      priv->stun_server = g_value_dup_string (value);
      break;
    case PROP_STUN_PORT:
      priv->stun_port = g_value_get_uint (value);
      break;
    case PROP_STUN_RELAY_MAGIC_COOKIE:
      g_free (priv->stun_relay_magic_cookie);
      priv->stun_relay_magic_cookie = g_value_dup_string (value);
      break;
    case PROP_STUN_RELAY_SERVER:
      g_free (priv->stun_relay_server);
      priv->stun_relay_server = g_value_dup_string (value);
      break;
    case PROP_STUN_RELAY_UDP_PORT:
      priv->stun_relay_udp_port = g_value_get_uint (value);
      break;
    case PROP_STUN_RELAY_TCP_PORT:
      priv->stun_relay_tcp_port = g_value_get_uint (value);
      break;
    case PROP_STUN_RELAY_SSLTCP_PORT:
      priv->stun_relay_ssltcp_port = g_value_get_uint (value);
      break;
    case PROP_STUN_RELAY_USERNAME:
      g_free (priv->stun_relay_username);
      priv->stun_relay_username = g_value_dup_string (value);
      break;
    case PROP_STUN_RELAY_PASSWORD:
      g_free (priv->stun_relay_password);
      priv->stun_relay_password = g_value_dup_string (value);
      break;
*/
    default:
      param_name = g_param_spec_get_name (pspec);

      if (gabble_properties_mixin_has_property (object, param_name,
            &tp_property_id))
        {
          gabble_properties_mixin_change_value (object, tp_property_id, value,
                                                NULL);
          gabble_properties_mixin_change_flags (object, tp_property_id,
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

static void
gabble_connection_class_init (GabbleConnectionClass *gabble_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_connection_class);
  GParamSpec *param_spec;

  object_class->get_property = gabble_connection_get_property;
  object_class->set_property = gabble_connection_set_property;

  g_type_class_add_private (gabble_connection_class, sizeof (GabbleConnectionPrivate));

  object_class->dispose = gabble_connection_dispose;
  object_class->finalize = gabble_connection_finalize;

  param_spec = g_param_spec_string ("protocol", "Telepathy identifier for protocol",
                                    "Identifier string used when the protocol "
                                    "name is required. Unused internally.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PROTOCOL, param_spec);

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

  /* signal definitions */

  signals[ALIASES_CHANGED] =
    g_signal_new ("aliases-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID)))));

  signals[NEW_CHANNEL] =
    g_signal_new ("new-channel",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__STRING_STRING_INT_INT_BOOLEAN,
                  G_TYPE_NONE, 5, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN);

  signals[PRESENCE_UPDATE] =
    g_signal_new ("presence-update",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_map ("GHashTable", G_TYPE_UINT, (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)))), G_TYPE_INVALID)))));

  signals[STATUS_CHANGED] =
    g_signal_new ("status-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[DISCONNECTED] =
    g_signal_new ("disconnected",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_connection_class), &dbus_glib_gabble_connection_object_info);

  gabble_properties_mixin_class_init (G_OBJECT_CLASS (gabble_connection_class),
                                      G_STRUCT_OFFSET (GabbleConnectionClass, properties_class),
                                      connection_property_signatures, NUM_CONN_PROPS,
                                      NULL);
}

static gboolean
_unref_lm_connection (gpointer data)
{
  LmConnection *conn = (LmConnection *) data;

  lm_connection_unref (conn);
  return FALSE;
}

void
gabble_connection_dispose (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);
  DBusGProxy *bus_proxy;
  bus_proxy = tp_get_bus_proxy ();

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_debug ("%s: dispose called", G_STRFUNC);

  if (priv->jingle_sessions)
    {
      g_assert (g_hash_table_size (priv->jingle_sessions) == 0);
      g_hash_table_destroy (priv->jingle_sessions);
      priv->jingle_sessions = NULL;
    }

  if (priv->media_channels)
    {
      g_assert (priv->media_channels->len == 0);
      g_ptr_array_free (priv->media_channels, TRUE);
      priv->media_channels = NULL;
    }

  if (priv->channel_requests)
    {
      g_assert (priv->channel_requests->len == 0);
      g_ptr_array_free (priv->channel_requests, TRUE);
      priv->channel_requests = NULL;
    }

  g_ptr_array_foreach (priv->channel_factories, (GFunc) g_object_unref, NULL);
  g_ptr_array_free (priv->channel_factories, TRUE);
  priv->channel_factories = NULL;

  /* unreffing channel factories frees the roster */
  self->roster = NULL;

  g_object_unref (self->disco);
  self->disco = NULL;

  g_object_unref (self->presence_cache);
  self->presence_cache = NULL;

  /* if this is not already the case, we'll crash anyway */
  g_assert (!lm_connection_is_open (self->lmconn));

  g_assert (priv->iq_jingle_info_cb == NULL);
  g_assert (priv->iq_jingle_cb == NULL);
  g_assert (priv->iq_disco_cb == NULL);
  g_assert (priv->iq_unknown_cb == NULL);

  /*
   * The Loudmouth connection can't be unref'd immediately because this
   * function might (indirectly) return into Loudmouth code which expects the
   * connection to always be there.
   */
  g_idle_add (_unref_lm_connection, self->lmconn);

  if (NULL != self->bus_name)
    {
      dbus_g_proxy_call_no_reply (bus_proxy, "ReleaseName",
                                  G_TYPE_STRING, self->bus_name,
                                  G_TYPE_INVALID);
    }

  if (G_OBJECT_CLASS (gabble_connection_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_connection_parent_class)->dispose (object);
}

void
gabble_connection_finalize (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  g_debug ("%s called with %p", G_STRFUNC, object);

  g_free (self->bus_name);
  g_free (self->object_path);

  g_free (priv->protocol);
  g_free (priv->connect_server);
  g_free (priv->stream_server);
  g_free (priv->username);
  g_free (priv->password);
  g_free (priv->resource);

  g_free (priv->https_proxy_server);
  g_free (priv->fallback_conference_server);
/* FIXME: remove these
  g_free (priv->stun_server);
  g_free (priv->stun_relay_magic_cookie);
  g_free (priv->stun_relay_server);
  g_free (priv->stun_relay_username);
  g_free (priv->stun_relay_password);
*/

  g_list_free (priv->conference_servers);

  g_datalist_clear (&priv->client_room_handle_sets);
  g_datalist_clear (&priv->client_contact_handle_sets);
  g_datalist_clear (&priv->client_list_handle_sets);

  gabble_handle_repo_destroy (self->handles);

  gabble_properties_mixin_finalize (object);

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

  gabble_handle_decode_jid (account, &username, &server, &resource);

  if (username == NULL || server == NULL ||
      *username == '\0' || *server == '\0')
    {
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
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
 * _gabble_connection_register
 *
 * Make the connection object appear on the bus, returning the bus
 * name and object path used.
 */
gboolean
_gabble_connection_register (GabbleConnection *conn,
                             gchar           **bus_name,
                             gchar           **object_path,
                             GError          **error)
{
  DBusGConnection *bus;
  DBusGProxy *bus_proxy;
  GabbleConnectionPrivate *priv;
  const char *allowed_chars = "_1234567890"
                              "abcdefghijklmnopqrstuvwxyz"
                              "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  char *safe_proto;
  char *unique_name;
  guint request_name_result;
  GError *request_error;

  g_assert (GABBLE_IS_CONNECTION (conn));

  bus = tp_get_bus ();
  bus_proxy = tp_get_bus_proxy ();
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  safe_proto = g_strdup (priv->protocol);
  g_strcanon (safe_proto, allowed_chars, '_');

  unique_name = g_strdup_printf ("%s_%s_%s",
                                 priv->username,
                                 priv->stream_server,
                                 priv->resource);
  g_strcanon (unique_name, allowed_chars, '_');

  conn->bus_name = g_strdup_printf (BUS_NAME ".%s.%s",
                                    safe_proto,
                                    unique_name);
  conn->object_path = g_strdup_printf (OBJECT_PATH "/%s/%s",
                                       safe_proto,
                                       unique_name);

  g_free (safe_proto);
  g_free (unique_name);

  if (!dbus_g_proxy_call (bus_proxy, "RequestName", &request_error,
                          G_TYPE_STRING, conn->bus_name,
                          G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                          G_TYPE_INVALID,
                          G_TYPE_UINT, &request_name_result,
                          G_TYPE_INVALID))
    {
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "Error acquiring "
          "bus name %s: %s", conn->bus_name, request_error->message);

      g_error_free (request_error);

      g_free (conn->bus_name);
      conn->bus_name = NULL;

      return FALSE;
    }

  if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
      gchar *msg;

      switch (request_name_result)
        {
        case DBUS_REQUEST_NAME_REPLY_IN_QUEUE:
          msg = "Request has been queued, though we request non-queueing.";
          break;
        case DBUS_REQUEST_NAME_REPLY_EXISTS:
          msg = "A connection manger already has this busname.";
          break;
        case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
          msg = "Connection manager already has a connection to this account.";
          break;
        default:
          msg = "Unknown error return from ReleaseName";
        }

      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "Error acquiring "
          "bus name %s: %s", conn->bus_name, msg);

      g_free (conn->bus_name);
      conn->bus_name = NULL;

      return FALSE;
    }

  g_debug ("%s: bus name %s", G_STRFUNC, conn->bus_name);

  dbus_g_connection_register_g_object (bus, conn->object_path, G_OBJECT (conn));

  g_debug ("%s: object path %s", G_STRFUNC, conn->object_path);

  *bus_name = g_strdup (conn->bus_name);
  *object_path = g_strdup (conn->object_path);

  return TRUE;
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
      g_debug ("_gabble_connection_send failed: %s", lmerror->message);

      if (error)
        {
          *error = g_error_new (TELEPATHY_ERRORS, NetworkError,
                                "message send failed: %s", lmerror->message);
        }

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

  g_free (handler_data);
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

  handler_data = g_new (GabbleMsgHandlerData, 1);
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
      g_debug ("_gabble_connection_send_with_reply failed: %s",
               lmerror->message);

      if (error)
        {
          *error = g_error_new (TELEPATHY_ERRORS, NetworkError,
                                "message send failed: %s", lmerror->message);
        }

      g_error_free (lmerror);
    }

  lm_message_handler_unref (handler);

  return ret;
}

static LmHandlerResult connection_iq_jingle_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_iq_disco_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_iq_unknown_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmSSLResponse connection_ssl_cb (LmSSL*, LmSSLStatus, gpointer);
static void connection_open_cb (LmConnection*, gboolean, gpointer);
static void connection_auth_cb (LmConnection*, gboolean, gpointer);
static void connection_disco_cb (GabbleDisco *, GabbleDiscoRequest *, const gchar *, const gchar *, LmMessageNode *, GError *, gpointer);
static void connection_disconnected_cb (LmConnection *, LmDisconnectReason, gpointer);
static void connection_status_change (GabbleConnection *, TpConnectionStatus, TpConnectionStatusReason);

static void channel_request_cancel (gpointer data, gpointer user_data);

static void close_all_channels (GabbleConnection *conn);
static void discover_services (GabbleConnection *conn);
static void emit_one_presence_update (GabbleConnection *self, GabbleHandle handle);


static gboolean
do_connect (GabbleConnection *conn, GError **error)
{
  GError *lmerror = NULL;

  g_debug ("%s: calling lm_connection_open", G_STRFUNC);

  if (!lm_connection_open (conn->lmconn, connection_open_cb,
                           conn, NULL, &lmerror))
    {
      g_debug ("%s: lm_connection_open failed %s", G_STRFUNC, lmerror->message);

      if (error)
        *error = g_error_new (TELEPATHY_ERRORS, NetworkError,
                              "lm_connection_open failed: %s",
                              lmerror->message);

      g_error_free (lmerror);

      return FALSE;
    }

  return TRUE;
}

static void
connect_callbacks (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_assert (priv->iq_jingle_info_cb == NULL);
  g_assert (priv->iq_jingle_cb == NULL);
  g_assert (priv->iq_disco_cb == NULL);
  g_assert (priv->iq_unknown_cb == NULL);

  priv->iq_jingle_info_cb = lm_message_handler_new (jingle_info_iq_callback,
                                                    conn, NULL);
  lm_connection_register_message_handler (conn->lmconn,
                                          priv->iq_jingle_info_cb,
                                          LM_MESSAGE_TYPE_IQ,
                                          LM_HANDLER_PRIORITY_NORMAL);

  priv->iq_jingle_cb = lm_message_handler_new (connection_iq_jingle_cb,
                                               conn, NULL);
  lm_connection_register_message_handler (conn->lmconn, priv->iq_jingle_cb,
                                          LM_MESSAGE_TYPE_IQ,
                                          LM_HANDLER_PRIORITY_NORMAL);

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
}

static void
disconnect_callbacks (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_assert (priv->iq_jingle_info_cb != NULL);
  g_assert (priv->iq_jingle_cb != NULL);
  g_assert (priv->iq_disco_cb != NULL);
  g_assert (priv->iq_unknown_cb != NULL);

  lm_connection_unregister_message_handler (conn->lmconn, priv->iq_jingle_info_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_jingle_info_cb);
  priv->iq_jingle_info_cb = NULL;

  lm_connection_unregister_message_handler (conn->lmconn, priv->iq_jingle_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_jingle_cb);
  priv->iq_jingle_cb = NULL;

  lm_connection_unregister_message_handler (conn->lmconn, priv->iq_disco_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_disco_cb);
  priv->iq_disco_cb = NULL;

  lm_connection_unregister_message_handler (conn->lmconn, priv->iq_unknown_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_unknown_cb);
  priv->iq_unknown_cb = NULL;
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
gboolean
_gabble_connection_connect (GabbleConnection *conn,
                            GError          **error)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  char *jid;
  gboolean valid;
  GabblePresence *presence;

  g_assert (priv->port > 0 && priv->port <= G_MAXUINT16);
  g_assert (priv->stream_server != NULL);
  g_assert (priv->username != NULL);
  g_assert (priv->password != NULL);
  g_assert (priv->resource != NULL);
  g_assert (lm_connection_is_open (conn->lmconn) == FALSE);

  jid = g_strdup_printf ("%s@%s", priv->username, priv->stream_server);
  lm_connection_set_jid (conn->lmconn, jid);

  conn->self_handle = gabble_handle_for_contact (conn->handles,
                                                 jid, FALSE);
  g_free (jid);

  if (conn->self_handle == 0)
    {
      /* FIXME: check this sooner and return an error to the user
       * this will be when we implement Connect() in spec 0.13 */
      g_error ("%s: invalid jid %s@%s", G_STRFUNC, priv->username,
          priv->stream_server);
      return FALSE;
    }

  valid = gabble_handle_ref (conn->handles,
                             TP_HANDLE_TYPE_CONTACT,
                             conn->self_handle);
  g_assert (valid);

  /* set initial presence */
  /* TODO: some way for the user to set this */
  gabble_presence_cache_update (conn->presence_cache, conn->self_handle,
      priv->resource, GABBLE_PRESENCE_AVAILABLE, NULL, priv->priority);
  emit_one_presence_update (conn, conn->self_handle);

  /* set initial capabilities */
  /* TODO: get these from AdvertiseCapabilities  */
  presence = gabble_presence_cache_get (conn->presence_cache, conn->self_handle);
  g_assert (presence);
  gabble_presence_set_capabilities (presence, priv->resource,
      PRESENCE_CAP_JINGLE_VOICE | PRESENCE_CAP_GOOGLE_VOICE);

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

  if (do_connect (conn, error))
    {
      connection_status_change (conn,
          TP_CONN_STATUS_CONNECTING,
          TP_CONN_STATUS_REASON_REQUESTED);
    }
  else
    {
      return FALSE;
    }

  return TRUE;
}



static void
connection_disconnected_cb (LmConnection *lmconn,
                            LmDisconnectReason lm_reason,
                            gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  g_assert (conn->lmconn == lmconn);

  g_debug ("%s: called with reason %u", G_STRFUNC, lm_reason);

  /* if we were expecting this disconnection, we're done so can tell
   * the connection manager to unref us. otherwise it's a network error
   * or some other screw up we didn't expect, so we emit the status
   * change */
  if (conn->status == TP_CONN_STATUS_DISCONNECTED)
    {
      g_debug ("%s: expected; emitting DISCONNECTED", G_STRFUNC);
      g_signal_emit (conn, signals[DISCONNECTED], 0);
    }
  else
    {
      g_debug ("%s: unexpected; calling connection_status_change", G_STRFUNC);
      connection_status_change (conn,
          TP_CONN_STATUS_DISCONNECTED,
          TP_CONN_STATUS_REASON_NETWORK_ERROR);
    }
}


/**
 * connection_status_change:
 * @conn: a #GabbleConnection
 * @status: new status to advertise
 * @reason: reason for new status
 *
 * Compares status with current status. If different, emits a signal
 * for the new status, and updates it in the #GabbleConnection.
 */
static void
connection_status_change (GabbleConnection        *conn,
                          TpConnectionStatus       status,
                          TpConnectionStatusReason reason)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_debug ("%s: status %u reason %u", G_STRFUNC, status, reason);

  if (conn->status != status)
    {
      conn->status = status;

      if (status == TP_CONN_STATUS_DISCONNECTED)
        {
          /* remove the channels so we don't get any race conditions where
           * method calls are delivered to a channel after we've started
           * disconnecting */

          /* trigger close_all on all channel factories */
          g_ptr_array_foreach (priv->channel_factories, (GFunc)
              tp_channel_factory_iface_close_all, NULL);

          /* cancel all queued channel requests */
          if (priv->channel_requests->len > 0)
            {
              g_ptr_array_foreach (priv->channel_requests, (GFunc)
                channel_request_cancel, NULL);
              g_ptr_array_remove_range (priv->channel_requests, 0,
                priv->channel_requests->len);
            }

          /* the old way */
          close_all_channels (conn);
        }

      g_debug ("%s emitting status-changed with status %u reason %u",
               G_STRFUNC, status, reason);

      g_signal_emit (conn, signals[STATUS_CHANGED], 0, status, reason);

      if (status == TP_CONN_STATUS_CONNECTING)
        {
          /* add our callbacks */
          connect_callbacks (conn);

          /* trigger connecting on all channel factories */
          g_ptr_array_foreach (priv->channel_factories, (GFunc)
              tp_channel_factory_iface_connecting, NULL);
        }
      else if (status == TP_CONN_STATUS_CONNECTED)
        {
          /* trigger connected on all channel factories */
          g_ptr_array_foreach (priv->channel_factories, (GFunc)
              tp_channel_factory_iface_connected, NULL);
        }
      else if (status == TP_CONN_STATUS_DISCONNECTED)
        {
          /* remove our callbacks */
          disconnect_callbacks (conn);

          /* trigger disconnected on all channel factories */
          g_ptr_array_foreach (priv->channel_factories, (GFunc)
              tp_channel_factory_iface_disconnected, NULL);

          /* if the connection is open, this function will close it for you.
           * if it's already closed (eg network error) then we're done, so
           * can emit DISCONNECTED and have the connection manager unref us */
          if (lm_connection_is_open (conn->lmconn))
            {
              g_debug ("%s: still open; calling lm_connection_close", G_STRFUNC);
              lm_connection_close (conn->lmconn, NULL);
            }
          else
            {
              g_debug ("%s: closed; emitting DISCONNECTED", G_STRFUNC);
              g_signal_emit (conn, signals[DISCONNECTED], 0);
            }
        }
    }
  else
    {
      g_warning ("%s: attempted to re-emit the current status %u reason %u",
          G_STRFUNC, status, reason);
    }
}

/**
 * close_all_channels:
 * @conn: A #GabbleConnection object
 *
 * Closes all channels owned by @conn.
 */
static void
close_all_channels (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  guint i;

  if (priv->media_channels)
    {
      GPtrArray *tmp = priv->media_channels;
      priv->media_channels = NULL;

      for (i = 0; i < tmp->len; i++)
        {
          GabbleMediaChannel *chan = g_ptr_array_index (tmp, i);

          g_debug ("%s: about to unref channel with ref_count %d",
                   G_STRFUNC, G_OBJECT (chan)->ref_count);

          g_object_unref (chan);
        }

      g_ptr_array_free (tmp, TRUE);
    }

  if (priv->jingle_sessions)
    {
      g_hash_table_destroy (priv->jingle_sessions);
      priv->jingle_sessions = NULL;
    }

  priv->media_channel_index = 0;

  if (priv->roomlist_channel)
    {
      GObject *tmp = G_OBJECT (priv->roomlist_channel);
      priv->roomlist_channel = NULL;
      g_object_unref (tmp);
    }
}


gboolean
_lm_message_node_has_namespace (LmMessageNode *node, const gchar *ns)
{
  const gchar *node_ns = lm_message_node_get_attribute (node, "xmlns");

  if (!node_ns)
    return FALSE;

  return 0 == strcmp (ns, node_ns);
}

static ChannelRequest *
channel_request_new (DBusGMethodInvocation *context,
                     const char *channel_type,
                     guint handle_type,
                     guint handle,
                     gboolean suppress_handler)
{
  ChannelRequest *ret;

  g_assert (NULL != context);
  g_assert (NULL != channel_type);

  ret = g_new0 (ChannelRequest, 1);
  ret->context = context;
  ret->channel_type = g_strdup (channel_type);
  ret->handle_type = handle_type;
  ret->handle = handle;
  ret->suppress_handler = suppress_handler;

  return ret;
}

static void
channel_request_free (ChannelRequest *request)
{
  g_assert (NULL == request->context);
  g_free (request->channel_type);
  g_free (request);
}

static void
channel_request_cancel (gpointer data, gpointer user_data)
{
  ChannelRequest *request = (ChannelRequest *) data;
  GError *error;

  g_debug ("%s: cancelling request for %s/%d/%d", G_STRFUNC,
      request->channel_type, request->handle_type, request->handle);

  error = g_error_new (TELEPATHY_ERRORS, Disconnected, "unable to "
      "service this channel request, we're disconnecting!");

  dbus_g_method_return_error (request->context, error);
  request->context = NULL;

  g_error_free (error);
  channel_request_free (request);
}

static GPtrArray *
find_matching_channel_requests (GabbleConnection *conn,
                                const gchar *channel_type,
                                guint handle_type,
                                guint handle,
                                gboolean *suppress_handler)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GPtrArray *requests;
  guint i;

  requests = g_ptr_array_sized_new (1);

  for (i = 0; i < priv->channel_requests->len; i++)
    {
      ChannelRequest *request = g_ptr_array_index (priv->channel_requests, i);

      if (0 != strcmp (request->channel_type, channel_type))
        continue;

      if (handle_type != request->handle_type)
        continue;

      if (handle != request->handle)
        continue;

      if (request->suppress_handler && suppress_handler)
        *suppress_handler = TRUE;

      g_ptr_array_add (requests, request);
    }

  return requests;
}

static void
connection_new_channel_cb (TpChannelFactoryIface *factory,
                           GObject *chan,
                           gpointer data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  gchar *object_path = NULL, *channel_type = NULL;
  guint handle_type = 0, handle = 0;
  gboolean suppress_handler = priv->suppress_next_handler;
  GPtrArray *tmp;
  guint i;

  g_object_get (chan,
      "object-path", &object_path,
      "channel-type", &channel_type,
      "handle-type", &handle_type,
      "handle", &handle,
      NULL);

  g_debug ("%s: called for %s", G_STRFUNC, object_path);

  tmp = find_matching_channel_requests (conn, channel_type, handle_type,
                                        handle, &suppress_handler);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, channel_type,
                 handle_type, handle,
                 suppress_handler);

  for (i = 0; i < tmp->len; i++)
    {
      ChannelRequest *request = g_ptr_array_index (tmp, i);

      g_debug ("%s: completing queued request, channel_type=%s, handle_type=%u, "
          "handle=%u, suppress_handler=%u", G_STRFUNC, request->channel_type,
          request->handle_type, request->handle, request->suppress_handler);

      dbus_g_method_return (request->context, object_path);
      request->context = NULL;

      g_ptr_array_remove (priv->channel_requests, request);

      channel_request_free (request);
    }

  g_ptr_array_free (tmp, TRUE);

  g_free (object_path);
  g_free (channel_type);
}

static void
connection_channel_error_cb (TpChannelFactoryIface *factory,
                             GObject *chan,
                             GError *error,
                             gpointer data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  gchar *channel_type = NULL;
  guint handle_type = 0, handle = 0;
  GPtrArray *tmp;
  guint i;

  g_debug ("%s: channel_type=%s, handle_type=%u, handle=%u, error_code=%u, "
      "error_message=\"%s\"", G_STRFUNC, channel_type, handle_type, handle,
      error->code, error->message);

  g_object_get (chan,
      "channel-type", &channel_type,
      "handle-type", &handle_type,
      "handle", &handle,
      NULL);

  tmp = find_matching_channel_requests (conn, channel_type, handle_type,
                                        handle, NULL);

  for (i = 0; i < tmp->len; i++)
    {
      ChannelRequest *request = g_ptr_array_index (tmp, i);

      g_debug ("%s: completing queued request %p, channel_type=%s, "
          "handle_type=%u, handle=%u, suppress_handler=%u",
          G_STRFUNC, request, request->channel_type,
          request->handle_type, request->handle, request->suppress_handler);

      dbus_g_method_return_error (request->context, error);
      request->context = NULL;

      g_ptr_array_remove (priv->channel_requests, request);

      channel_request_free (request);
    }

  g_ptr_array_free (tmp, TRUE);
  g_free (channel_type);
}

static void
connection_presence_update_cb (GabblePresenceCache *cache, GabbleHandle handle, gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  emit_one_presence_update (conn, handle);
}

GabbleConnectionAliasSource
_gabble_connection_get_cached_alias (GabbleConnection *conn,
                                     GabbleHandle handle,
                                     gchar **alias)
{
  GabbleConnectionAliasSource ret = GABBLE_CONNECTION_ALIAS_NONE;
  GabblePresence *pres;
  const gchar *tmp;
  gchar *user = NULL, *resource = NULL;

  g_return_val_if_fail (NULL != conn, GABBLE_CONNECTION_ALIAS_NONE);
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), GABBLE_CONNECTION_ALIAS_NONE);
  g_return_val_if_fail (gabble_handle_is_valid (conn->handles,
        TP_HANDLE_TYPE_CONTACT, handle, NULL), GABBLE_CONNECTION_ALIAS_NONE);

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

  /* todo: vcard */

  /* fallback to JID */
  tmp = gabble_handle_inspect (conn->handles, TP_HANDLE_TYPE_CONTACT, handle);
  g_assert (NULL != tmp);

  gabble_handle_decode_jid (tmp, &user, NULL, &resource);

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
                               GabbleHandle handle,
                               gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionAliasSource signal_source, real_source;
  gchar *alias = NULL;
  GPtrArray *aliases;
  GValue entry = { 0, };

  if (object == G_OBJECT (conn->roster))
    signal_source = GABBLE_CONNECTION_ALIAS_FROM_ROSTER;
  else if (object == G_OBJECT (conn->presence_cache))
    signal_source = GABBLE_CONNECTION_ALIAS_FROM_PRESENCE;
/*  else if (object == G_OBJECT (conn->vcard_cache))
 *  signal_source = GABBLE_CONNECTION_ALIAS_FROM_VCARD;
 */
  else
    g_assert_not_reached ();

  real_source = _gabble_connection_get_cached_alias (conn, handle, &alias);

  g_assert (real_source != GABBLE_CONNECTION_ALIAS_NONE);

  /* if the active alias for this handle is already known and from
   * a higher priority, this signal is not interesting so we do
   * nothing */
  if (real_source > signal_source)
    {
      g_debug ("%s: ignoring boring alias change for handle %u, signal from %u "
          "but source %u has alias \"%s\"", G_STRFUNC, handle, signal_source,
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

  g_signal_emit (conn, signals[ALIASES_CHANGED], 0, aliases);

  g_value_unset (&entry);
  g_ptr_array_free (aliases, TRUE);

OUT:
  g_free (alias);
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

  if (gabble_statuses[status].presence_type == TP_CONN_PRESENCE_TYPE_HIDDEN &&
      (conn->features & GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE) == 0)
    return FALSE;
  else
    return TRUE;
}

/**
 * destroy_the_bastard:
 * @data: a GValue to destroy
 *
 * destroys a GValue allocated on the heap
 */
static void
destroy_the_bastard (GValue *value)
{
  g_value_unset (value);
  g_free (value);
}

/**
 * emit_presence_update:
 * @self: A #GabbleConnection
 * @contact_handles: A zero-terminated array of #GabbleHandle for
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
  GValueArray *vals;
  GHashTable *contact_status, *parameters;
  guint timestamp = 0; /* this is never set at the moment*/
  guint i;

  presence_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
                                    (GDestroyNotify) g_value_array_free);

  for (i = 0; i < contact_handles->len; i++)
    {
      GabbleHandle handle = g_array_index (contact_handles, GabbleHandle, i);
      GValue *message;
      GabblePresence *presence = gabble_presence_cache_get (self->presence_cache, handle);
      GabblePresenceId status;
      const gchar *status_message;

      g_assert (gabble_handle_is_valid (self->handles, TP_HANDLE_TYPE_CONTACT, handle, NULL));

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

      message = g_new0 (GValue, 1);
      g_value_init (message, G_TYPE_STRING);
      g_value_set_static_string (message, status_message);

      parameters =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) destroy_the_bastard);

      g_hash_table_insert (parameters, "message", message);

      contact_status =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) g_hash_table_destroy);
      g_hash_table_insert (
        contact_status, (gchar *) gabble_statuses[status].name,
        parameters);

      vals = g_value_array_new (2);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 0), timestamp);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 1),
          dbus_g_type_get_map ("GHashTable", G_TYPE_STRING,
            dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)));
      g_value_take_boxed (g_value_array_get_nth (vals, 1), contact_status);

      g_hash_table_insert (presence_hash, GINT_TO_POINTER (handle),
                           vals);
    }

  g_signal_emit (self, signals[PRESENCE_UPDATE], 0, presence_hash);
  g_hash_table_destroy (presence_hash);
}

/**
 * emit_one_presence_update:
 * Convenience function for calling emit_presence_update with one handle.
 */

static void
emit_one_presence_update (GabbleConnection *self,
                          GabbleHandle handle)
{
  GArray *handles = g_array_sized_new (FALSE, FALSE, sizeof (GabbleHandle), 1);

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
 * Retuns: FALSE if an error occured
 */
static gboolean
signal_own_presence (GabbleConnection *self, GError **error)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);
  GabblePresence *presence = gabble_presence_cache_get (self->presence_cache, self->self_handle);
  LmMessage *message = gabble_presence_as_message (presence, priv->resource);
  LmMessageNode *node = lm_message_get_node (message);
  gboolean ret;

  if (presence->status == GABBLE_PRESENCE_HIDDEN)
    {
      if ((self->features & GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE) != 0)
        lm_message_node_set_attribute (node, "type", "invisible");
    }

  node = lm_message_node_add_child (node, "c", NULL);
  lm_message_node_set_attributes (
    node,
    "xmlns", NS_CAPS,
    "node",  NS_GABBLE_CAPS,
    "ver",   VERSION,
    "ext",   "voice-v1",
    NULL);

  ret = _gabble_connection_send (self, message, error);

  lm_message_unref (message);

  return ret;
}


/**
 * media_channel_closed_cb:
 *
 * Signal callback for when a media channel is closed. Removes the references
 * that #GabbleConnection holds to them.
 */
static void
media_channel_closed_cb (GabbleMediaChannel *chan, gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (priv->media_channels)
    {
      g_debug ("%s: removing media channel %p with ref count %d",
          G_STRFUNC, chan, G_OBJECT (chan)->ref_count);

      g_ptr_array_remove (priv->media_channels, chan);
      g_object_unref (chan);
    }
}

/**
 * new_media_channel
 *
 * Creates a new empty GabbleMediaChannel.
 */
static GabbleMediaChannel *
new_media_channel (GabbleConnection *conn, GabbleHandle creator, gboolean suppress_handler)
{
  GabbleConnectionPrivate *priv;
  GabbleMediaChannel *chan;
  gchar *object_path;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  object_path = g_strdup_printf ("%s/MediaChannel%u", conn->object_path,
                                 priv->media_channel_index);
  priv->media_channel_index += 1;

  chan = g_object_new (GABBLE_TYPE_MEDIA_CHANNEL,
                       "connection", conn,
                       "object-path", object_path,
                       "creator", creator,
                       NULL);

  g_debug ("%s: object path %s", G_STRFUNC, object_path);

  g_signal_connect (chan, "closed", (GCallback) media_channel_closed_cb, conn);

  g_ptr_array_add (priv->media_channels, chan);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
                 0, 0, suppress_handler);

  g_free (object_path);

  return chan;
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
                                  GabbleXmppError error)
{
  const gchar *to, *id;
  LmMessage *msg;
  LmMessageNode *iq_node;

  iq_node = lm_message_get_node (message);
  to = lm_message_node_get_attribute (iq_node, "from");
  id = lm_message_node_get_attribute (iq_node, "id");

  if (id == NULL)
    {
      HANDLER_DEBUG (iq_node, "can't acknowledge IQ with no id");
      return;
    }

  msg = lm_message_new_with_sub_type (to, LM_MESSAGE_TYPE_IQ,
                                      LM_MESSAGE_SUB_TYPE_ERROR);

  lm_message_node_set_attribute (msg->node, "id", id);

  lm_message_node_steal_children (msg->node, iq_node);

  gabble_xmpp_error_to_node (error, msg->node);

  _gabble_connection_send (conn, msg, NULL);

  lm_message_unref (msg);
}

const gchar *
_gabble_connection_jingle_session_allocate (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv;
  guint32 val;
  gchar *sid = NULL;
  gboolean unique = FALSE;

  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  while (!unique)
    {
      gpointer k, v;

      val = g_random_int_range (1000000, G_MAXINT);

      g_free (sid);
      sid = g_strdup_printf ("%u", val);

      unique = !g_hash_table_lookup_extended (priv->jingle_sessions,
                                              sid, &k, &v);
    }

  g_hash_table_insert (priv->jingle_sessions, sid, NULL);

  return (const gchar *) sid;
}

void
_gabble_connection_jingle_session_register (GabbleConnection *conn,
                                            const gchar *sid,
                                            gpointer channel)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_debug ("%s: binding sid %s to %p", G_STRFUNC, sid, channel);

  g_hash_table_insert (priv->jingle_sessions, g_strdup (sid), channel);
}

void
_gabble_connection_jingle_session_unregister (GabbleConnection *conn,
                                              const gchar *sid)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_debug ("%s: unbinding sid %s", G_STRFUNC, sid);

  g_hash_table_insert (priv->jingle_sessions, g_strdup (sid), NULL);
}

/**
 * connection_iq_jingle_cb
 *
 * Called by loudmouth when we get an incoming <iq>. This handler
 * is concerned only with jingle session queries, and allows other
 * handlers to be called for other queries.
 */
static LmHandlerResult
connection_iq_jingle_cb (LmMessageHandler *handler,
                         LmConnection *lmconn,
                         LmMessage *message,
                         gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  LmMessageNode *iq_node, *session_node, *desc_node;
  const gchar *from, *id, *action, *sid;
  gchar *resource;
  GabbleHandle handle;
  GabbleMediaChannel *chan = NULL;
  gpointer k, v;

  g_assert (lmconn == conn->lmconn);

  iq_node = lm_message_get_node (message);
  session_node = lm_message_node_get_child (iq_node, "session");

  /* is it for us? */
  if (!session_node || !_lm_message_node_has_namespace (session_node,
        NS_GOOGLE_SESSION))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  from = lm_message_node_get_attribute (iq_node, "from");
  if (!from)
    {
      HANDLER_DEBUG (iq_node, "'from' attribute not found");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  id = lm_message_node_get_attribute (iq_node, "id");
  if (!id)
    {
      HANDLER_DEBUG (iq_node, "'id' attribute not found");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (LM_MESSAGE_SUB_TYPE_SET != lm_message_get_sub_type (message))
    {
      HANDLER_DEBUG (iq_node, "Jingle message sub type is not \"set\"");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  handle = gabble_handle_for_contact (conn->handles, from, FALSE);
  if (!handle)
    {
      HANDLER_DEBUG (iq_node, "unable to get handle for sender");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  /* determine the jingle action of the request */
  action = lm_message_node_get_attribute (session_node, "type");
  if (!action)
    {
      HANDLER_DEBUG (iq_node, "session 'type' attribute not found");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  /* does the session exist? */
  sid = lm_message_node_get_attribute (session_node, "id");
  if (!sid)
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* is the session new and not a zombie? */
  if (g_hash_table_lookup_extended (priv->jingle_sessions,
                                    g_strdup (sid),
                                    &k, &v))
    {
      chan = (GabbleMediaChannel *) v;
    }
  else
    {
      /* if the session is unknown, the only allowed action is "initiate" */
      if (strcmp (action, "initiate"))
        {
          HANDLER_DEBUG (iq_node, "action is not \"initiate\", ignoring");
          return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
        }

      desc_node = lm_message_node_get_child (session_node, "description");
      if (!desc_node)
        {
          HANDLER_DEBUG (iq_node, "node has no description, ignoring");
          return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
        }

      if (!_lm_message_node_has_namespace (desc_node, NS_GOOGLE_SESSION_PHONE))
        {
          HANDLER_DEBUG (iq_node, "unknown session description, ignoring");
          return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
        }

      g_debug ("%s: creating media channel", G_STRFUNC);

      chan = new_media_channel (conn, handle, FALSE);
    }

  if (chan)
    {
      g_debug ("%s: dispatching to session %s", G_STRFUNC, sid);
      g_object_ref (chan);
      gabble_handle_decode_jid (from, NULL, NULL, &resource);
      _gabble_media_channel_dispatch_session_action (chan, handle, resource,
          sid, message, session_node, action);
      g_object_unref (chan);
    }
  else
    {
      g_debug ("%s: zombie session %s, we should reject this",
          G_STRFUNC, sid);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
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
      HANDLER_DEBUG (iq, "can't acknowledge IQ with no id");
      return NULL;
    }

  from_jid = lm_message_node_get_attribute (iq, "from");

  result = lm_message_new_with_sub_type (from_jid, LM_MESSAGE_TYPE_IQ,
                                         LM_MESSAGE_SUB_TYPE_RESULT);
  result_iq = lm_message_get_node (result);
  lm_message_node_set_attribute (result_iq, "id", id);

  return result;
}

typedef struct _Feature Feature;

struct _Feature
{
  const gchar *bundle;
  const gchar *ns;
};

static Feature *
feature_new (const gchar *bundle, const gchar *ns)
{
  Feature *feature;

  feature = g_new0 (Feature, 1);
  feature->bundle = bundle;
  feature->ns = ns;
  return feature;
}

static GSList *
get_features (void)
{
  static GSList *features = NULL;

  if (NULL == features)
    {
      features = g_slist_append (features,
        feature_new ("voice-v1", NS_GOOGLE_VOICE));

      if (g_getenv ("GABBLE_JINGLE"))
        {
          features = g_slist_append (features,
            feature_new ("jingle", NS_JINGLE));
          features = g_slist_append (features,
            feature_new ("jingle", NS_JINGLE_AUDIO));
        }
    }

  return features;
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
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  LmMessage *result;
  LmMessageNode *iq, *result_iq, *query, *result_query;
  const gchar *node, *suffix;
  GSList *i;

  if (lm_message_get_sub_type (message) != LM_MESSAGE_SUB_TYPE_GET)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  iq = lm_message_get_node (message);
  query = lm_message_node_get_child (iq, "query");

  if (!query)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  if (!_lm_message_node_has_namespace (query, NS_DISCO_INFO))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  node = lm_message_node_get_attribute (query, "node");

  if (node && (
      0 != strncmp (node, NS_GABBLE_CAPS "#", strlen (NS_GABBLE_CAPS) + 1) ||
      strlen(node) < strlen (NS_GABBLE_CAPS) + 2))
    {
      HANDLER_DEBUG (iq, "got iq disco query with unexpected node attribute");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (node == NULL)
    suffix = NULL;
  else
    suffix = node + strlen (NS_GABBLE_CAPS) + 1;

  /* if the suffix is our version number, look up features with a NULL bundle */

  if (suffix != NULL && 0 == strcmp (suffix, VERSION))
    suffix = NULL;

  result = _lm_iq_message_make_result (message);
  result_iq = lm_message_get_node (result);
  result_query = lm_message_node_add_child (result_iq, "query", NULL);
  lm_message_node_set_attribute (result_query, "xmlns", NS_DISCO_INFO);

  for (i = get_features (); NULL != i; i = i->next)
    {
      Feature *feature = (Feature *) i->data;

      if (NULL == node || !g_strdiff (suffix, feature->bundle))
        {
          LmMessageNode *node = lm_message_node_add_child (result_query,
              "feature", NULL);
          lm_message_node_set_attribute (node, "var", feature->ns);
        }
    }

  HANDLER_DEBUG (result_iq, "sending disco response");

  if (!lm_connection_send (conn->lmconn, result, NULL))
    g_debug("sending disco response failed");

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

  HANDLER_DEBUG (message->node, "got unknown iq");

  switch (lm_message_get_sub_type (message))
    {
    case LM_MESSAGE_SUB_TYPE_GET:
    case LM_MESSAGE_SUB_TYPE_SET:
      _gabble_connection_send_iq_error (conn, message,
          XMPP_ERROR_SERVICE_UNAVAILABLE);
      break;
    default:
      break;
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
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
      tp_reason = TP_CONN_STATUS_REASON_CERT_NOT_PROVIDED;
      break;
    case LM_SSL_STATUS_UNTRUSTED_CERT:
      reason = "The certificate can not be trusted.";
      tp_reason = TP_CONN_STATUS_REASON_CERT_UNTRUSTED;
      break;
    case LM_SSL_STATUS_CERT_EXPIRED:
      reason = "The certificate has expired.";
      tp_reason = TP_CONN_STATUS_REASON_CERT_EXPIRED;
      break;
    case LM_SSL_STATUS_CERT_NOT_ACTIVATED:
      reason = "The certificate has not been activated.";
      tp_reason = TP_CONN_STATUS_REASON_CERT_NOT_ACTIVATED;
      break;
    case LM_SSL_STATUS_CERT_HOSTNAME_MISMATCH:
      reason = "The server hostname doesn't match the one in the certificate.";
      tp_reason = TP_CONN_STATUS_REASON_CERT_HOSTNAME_MISMATCH;
      break;
    case LM_SSL_STATUS_CERT_FINGERPRINT_MISMATCH:
      reason = "The fingerprint doesn't match the expected value.";
      tp_reason = TP_CONN_STATUS_REASON_CERT_FINGERPRINT_MISMATCH;
      break;
    case LM_SSL_STATUS_GENERIC_ERROR:
      reason = "An unknown SSL error occured.";
      tp_reason = TP_CONN_STATUS_REASON_CERT_OTHER_ERROR;
      break;
    default:
      g_assert_not_reached();
  }

  g_debug ("%s called: %s", G_STRFUNC, reason);

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

  g_debug ("%s: authenticating with username: %s, password: <hidden>, resource: %s",
           G_STRFUNC, priv->username, priv->resource);

  if (!lm_connection_authenticate (conn->lmconn, priv->username, priv->password,
                                   priv->resource, connection_auth_cb,
                                   conn, NULL, &error))
    {
      g_debug ("%s failed: %s", G_STRFUNC, error->message);
      g_error_free (error);

      /* the reason this function can fail is through network errors,
       * authentication failures are reported to our auth_cb */
      connection_status_change (conn,
          TP_CONN_STATUS_DISCONNECTED,
          TP_CONN_STATUS_REASON_NETWORK_ERROR);
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

  g_debug ("%s: %s", G_STRFUNC, (success) ? "succeeded" : "failed");

  g_object_unref (reg);

  if (success)
    {
      do_auth (conn);
    }
  else
    {
      g_debug ("%s: err_code = %d, err_msg = '%s'",
               G_STRFUNC, err_code, err_msg);

      connection_status_change (conn,
          TP_CONN_STATUS_DISCONNECTED,
          (err_code == InvalidArgument) ? TP_CONN_STATUS_REASON_NAME_IN_USE :
            TP_CONN_STATUS_REASON_AUTHENTICATION_FAILED);
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

  g_assert (priv);
  g_assert (lmconn == conn->lmconn);

  if (!success)
    {
      if (lm_connection_get_proxy (lmconn))
        {
          g_debug ("%s failed, retrying without proxy", G_STRFUNC);

          lm_connection_set_proxy (lmconn, NULL);

          if (do_connect (conn, NULL))
            {
              return;
            }
        }
      else
        {
          g_debug ("%s failed", G_STRFUNC);
        }

      if (priv->ssl_error)
        {
          connection_status_change (conn,
            TP_CONN_STATUS_DISCONNECTED,
            priv->ssl_error);
        }
      else
        {
          connection_status_change (conn,
              TP_CONN_STATUS_DISCONNECTED,
              TP_CONN_STATUS_REASON_NETWORK_ERROR);
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
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GError *error = NULL;

  g_assert (priv);
  g_assert (lmconn == conn->lmconn);

  if (!success)
    {
      g_debug ("%s failed", G_STRFUNC);

      connection_status_change (conn,
          TP_CONN_STATUS_DISCONNECTED,
          TP_CONN_STATUS_REASON_AUTHENTICATION_FAILED);

      return;
    }

  if (!gabble_disco_request_with_timeout (conn->disco, GABBLE_DISCO_TYPE_INFO,
                                          priv->stream_server, NULL, 5000,
                                          connection_disco_cb, conn,
                                          G_OBJECT (conn), &error))
    {
      g_debug ("%s: sending disco request failed: %s",
          G_STRFUNC, error->message);

      g_error_free (error);

      connection_status_change (conn,
          TP_CONN_STATUS_DISCONNECTED,
          TP_CONN_STATUS_REASON_NETWORK_ERROR);
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
  GabbleConnectionPrivate *priv;
  GError *error;

  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (disco_error)
    {
      g_debug ("%s: got disco error, setting no features: %s", G_STRFUNC, disco_error->message);
    }
  else
    {
      LmMessageNode *iter;

      HANDLER_DEBUG (result, "got");

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

      g_debug ("%s: set features flags to %d", G_STRFUNC, conn->features);
    }

  /* send presence to the server to indicate availability */
  if (!signal_own_presence (conn, &error))
    {
      g_debug ("%s: sending initial presence failed: %s", G_STRFUNC,
          error->message);
      goto ERROR;
    }

  /* go go gadget on-line */
  connection_status_change (conn, TP_CONN_STATUS_CONNECTED, TP_CONN_STATUS_REASON_REQUESTED);

  discover_services (conn);

  if (conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_JINGLE_INFO)
    {
      jingle_info_discover_servers (conn);
    }

  return;

ERROR:
  if (error)
    g_error_free (error);

  connection_status_change (conn,
      TP_CONN_STATUS_DISCONNECTED,
      TP_CONN_STATUS_REASON_NETWORK_ERROR);

  return;
}

static void
roomlist_channel_closed_cb (GabbleRoomlistChannel *chan, gpointer data)
{
  GabbleConnection *conn = data;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (priv->roomlist_channel)
    {
      g_object_unref (priv->roomlist_channel);
      priv->roomlist_channel = NULL;
    }
}

static void
make_roomlist_channel (GabbleConnection *conn, gboolean suppress_handler)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (!priv->roomlist_channel)
    {
      gchar *server, *object_path;

      if (priv->conference_servers)
        {
          server = priv->conference_servers->data;
        }
      else if (priv->fallback_conference_server)
        {
          server = priv->fallback_conference_server;
        }
      else
        {
          g_assert_not_reached ();
        }

      object_path = g_strdup_printf ("%s/RoomlistChannel", conn->object_path);

      priv->roomlist_channel = _gabble_roomlist_channel_new (conn, object_path,
          server);

      g_signal_connect (priv->roomlist_channel, "closed",
                        (GCallback) roomlist_channel_closed_cb, conn);

      g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                     object_path, TP_IFACE_CHANNEL_TYPE_ROOM_LIST,
                     0, 0,
                     suppress_handler);

      g_free (object_path);
    }
}

static void
service_info_cb (GabbleDisco *disco,
                 GabbleDiscoRequest *request,
                 const gchar *jid,
                 const gchar *node,
                 LmMessageNode *result,
                 GError *error,
                 gpointer user_data)
{
  LmMessageNode *identity, *feature;
  gboolean is_muc = FALSE;
  const char *category, *type, *var;
  GabbleConnection *conn = user_data;
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (error)
    {
      g_debug ("%s: got error: %s", G_STRFUNC, error->message);
      return;
    }

  identity = lm_message_node_get_child (result, "identity");
  if (identity)
    {
      category = lm_message_node_get_attribute (identity, "category");
      type = lm_message_node_get_attribute (identity, "type");
      g_debug ("%s: got identity, category=%s, type=%s", G_STRFUNC,
               category, type);
      if (category && 0 == strcmp (category, "conference") &&
          type && 0 == strcmp (type, "text"))
        {
          for (feature = result->children; feature; feature = feature->next)
            {
              HANDLER_DEBUG (feature, "got child");

              if (0 == strcmp (feature->name, "feature"))
                {
                  var = lm_message_node_get_attribute (feature, "var");
                  if (var && 0 == strcmp (var, NS_MUC))
                    {
                      is_muc = TRUE;
                      break;
                    }
                }
            }
          if (is_muc)
            {
              g_debug ("%s: Adding conference server %s", G_STRFUNC, jid);
              priv->conference_servers =
                g_list_append (priv->conference_servers, g_strdup (jid));
            }
        }
    }
}

static void
services_discover_cb (GabbleDisco *disco,
                      GabbleDiscoRequest *request,
                      const gchar *jid,
                      const gchar *node,
                      LmMessageNode *result,
                      GError *error,
                      gpointer user_data)
{
  LmMessageNode *iter;
  GabbleConnection *conn = user_data;
  GabbleConnectionPrivate *priv;
  const char *item_jid;
  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (error)
    {
      g_debug ("%s: got error: %s", G_STRFUNC, error->message);
      return;
    }

  iter = result->children;

  for (; iter; iter = iter->next)
    {
      if (0 == strcmp (iter->name, "item"))
        {
          item_jid = lm_message_node_get_attribute (iter, "jid");
          if (item_jid)
            gabble_disco_request (conn->disco, GABBLE_DISCO_TYPE_INFO,
                                  item_jid, NULL,
                                  service_info_cb, conn, G_OBJECT (conn), NULL);
        }
    }
}

static void
discover_services (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv;
  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  gabble_disco_request (conn->disco, GABBLE_DISCO_TYPE_ITEMS,
                        priv->stream_server, NULL,
                        services_discover_cb, conn, G_OBJECT(conn), NULL);
}


static void
destroy_handle_sets (gpointer data)
{
  GabbleHandleSet *handle_set;

  handle_set = (GabbleHandleSet*) data;
  handle_set_destroy (handle_set);
}

/**
 * _gabble_connection_client_hold_handle:
 * @conn: a #GabbleConnection
 * @client_name: DBus bus name of client to hold ahandle for
 * @handle: handle to hold
 * @type: type of handle to hold
 *
 * Marks a handle as held by a given client.
 *
 * Returns: false if client didn't hold this handle
 */
void
_gabble_connection_client_hold_handle (GabbleConnection *conn,
                                       gchar *client_name,
                                       GabbleHandle handle,
                                       TpHandleType type)
{
  GabbleConnectionPrivate *priv;
  GabbleHandleSet *handle_set;
  GData **handle_set_list;
  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  switch (type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      handle_set_list = &priv->client_contact_handle_sets;
      break;
    case TP_HANDLE_TYPE_ROOM:
      handle_set_list = &priv->client_room_handle_sets;
      break;
    case TP_HANDLE_TYPE_LIST:
      handle_set_list = &priv->client_list_handle_sets;
      break;
    default:
      g_critical ("%s: gabble_connection_client_hold_handle called with invalid handle type", G_STRFUNC);
      return;
    }

  handle_set = (GabbleHandleSet*) g_datalist_get_data (handle_set_list, client_name);

  if (!handle_set)
    {
      handle_set = handle_set_new (conn->handles, type);
      g_datalist_set_data_full (handle_set_list, client_name, handle_set, destroy_handle_sets);
    }

  handle_set_add (handle_set, handle);

}

/**
 * _gabble_connection_client_release_handle:
 * @conn: a #GabbleConnection
 * @client_name: DBus bus name of client to release handle for
 * @handle: handle to release
 * @type: type of handle to release
 *
 * Releases a handle held by a given client
 *
 * Returns: false if client didn't hold this handle
 */
gboolean
_gabble_connection_client_release_handle (GabbleConnection *conn,
                                         gchar* client_name,
                                         GabbleHandle handle,
                                         TpHandleType type)
{
  GabbleConnectionPrivate *priv;
  GabbleHandleSet *handle_set;
  GData **handle_set_list;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  switch (type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      handle_set_list = &priv->client_contact_handle_sets;
      break;
    case TP_HANDLE_TYPE_ROOM:
      handle_set_list = &priv->client_room_handle_sets;
      break;
    case TP_HANDLE_TYPE_LIST:
      handle_set_list = &priv->client_list_handle_sets;
      break;
    default:
      g_critical ("%s called with invalid handle type", G_STRFUNC);
      return FALSE;
    }

  handle_set = (GabbleHandleSet *) g_datalist_get_data (handle_set_list,
                                                       client_name);

  if (handle_set)
    return handle_set_remove (handle_set, handle);
  else
    return FALSE;
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
 * Implements DBus method AddStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_add_status (GabbleConnection *obj, const gchar * status, GHashTable * parms, GError **error)
{
  g_assert (GABBLE_IS_CONNECTION (obj));

  ERROR_IF_NOT_CONNECTED (obj, *error);

  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
      "Only one status is possible at a time with this protocol");

  return FALSE;
}


#if 0
/**
 * gabble_connection_advertise_capabilities
 *
 * Implements DBus method AdvertiseCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_advertise_capabilities (GabbleConnection *obj, const gchar ** add, const gchar ** remove, GError **error)
{
  g_assert (GABBLE_IS_CONNECTION (obj));

  ERROR_IF_NOT_CONNECTED (obj, *error);

  add = NULL;
  remove = NULL;

  return TRUE;
}
#endif


/**
 * gabble_connection_clear_status
 *
 * Implements DBus method ClearStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_clear_status (GabbleConnection *obj, GError **error)
{
  GabbleConnectionPrivate *priv;
  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error);

  gabble_presence_cache_update (obj->presence_cache, obj->self_handle, priv->resource, GABBLE_PRESENCE_AVAILABLE, NULL, priv->priority);
  emit_one_presence_update (obj, obj->self_handle);
  return signal_own_presence (obj, error);
}


/**
 * gabble_connection_disconnect
 *
 * Implements DBus method Disconnect
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_disconnect (GabbleConnection *obj, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  connection_status_change (obj,
      TP_CONN_STATUS_DISCONNECTED,
      TP_CONN_STATUS_REASON_REQUESTED);

  return TRUE;
}


#if 0
/**
 * gabble_connection_get_capabilities
 *
 * Implements DBus method GetCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_capabilities (GabbleConnection *obj, guint handle, GPtrArray ** ret, GError **error)
{
  GValue vals ={0.};
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error);

  if (!gabble_handle_is_valid (obj->handles,
                               TP_HANDLE_TYPE_CONTACT,
                               handle,
                               error))
    return FALSE;

  *ret = g_ptr_array_sized_new (1);

  g_value_init (&vals, TP_CAPABILITY_PAIR_TYPE);
  g_value_take_boxed (&vals,
    dbus_g_type_specialized_construct (TP_CAPABILITY_PAIR_TYPE));

  dbus_g_type_struct_set (&vals,
                        0, TP_IFACE_CHANNEL_TYPE_TEXT,
                        1, TP_CONN_CAPABILITY_TYPE_CREATE,
                        G_MAXUINT);

  g_ptr_array_add (*ret, g_value_get_boxed (&vals));

  return TRUE;
}
#endif


/**
 * gabble_connection_get_alias_flags
 *
 * Implements DBus method GetAliasFlags
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_alias_flags (GabbleConnection *obj, guint* ret, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  *ret = TP_CONN_ALIAS_FLAG_USER_SET;

  return TRUE;
}


/**
 * gabble_connection_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_interfaces (GabbleConnection *obj, gchar *** ret, GError **error)
{
  const char *interfaces[] = {
      TP_IFACE_CONN_INTERFACE_ALIASING,
      TP_IFACE_CONN_INTERFACE_PRESENCE,
      TP_IFACE_PROPERTIES,
      NULL };
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_connection_get_properties
 *
 * Implements DBus method GetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_properties (GabbleConnection *obj, const GArray * properties, GPtrArray ** ret, GError **error)
{
  return gabble_properties_mixin_get_properties (G_OBJECT (obj), properties, ret, error);
}


/**
 * gabble_connection_get_protocol
 *
 * Implements DBus method GetProtocol
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_protocol (GabbleConnection *obj, gchar ** ret, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  *ret = g_strdup (priv->protocol);

  return TRUE;
}


/**
 * gabble_connection_get_self_handle
 *
 * Implements DBus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_self_handle (GabbleConnection *obj, guint* ret, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  *ret = obj->self_handle;

  return TRUE;
}


/**
 * gabble_connection_get_status
 *
 * Implements DBus method GetStatus
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_status (GabbleConnection *obj, guint* ret, GError **error)
{
  g_assert (GABBLE_IS_CONNECTION (obj));

  *ret = obj->status;

  return TRUE;
}


/**
 * gabble_connection_get_statuses
 *
 * Implements DBus method GetStatuses
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_get_statuses (GabbleConnection *obj, GHashTable ** ret, GError **error)
{
  GabbleConnectionPrivate *priv;
  GValueArray *status;
  int i;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  g_debug ("%s called.", G_STRFUNC);

  *ret = g_hash_table_new_full (g_str_hash, g_str_equal,
                                NULL, (GDestroyNotify) g_value_array_free);

  for (i=0; i < LAST_GABBLE_PRESENCE; i++)
    {
      /* don't report the invisible presence if the server
       * doesn't have the presence-invisible feature */
      if (!status_is_available (obj, i))
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

      g_hash_table_insert (*ret, (gchar*) gabble_statuses[i].name, status);
    }

  return TRUE;
}


/**
 * gabble_connection_hold_handle
 *
 * Implements DBus method HoldHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean gabble_connection_hold_handle (GabbleConnection *obj, guint handle_type, guint handle, DBusGMethodInvocation *context)
{
  GabbleConnectionPrivate *priv;
  GError *error = NULL;
  gchar *sender;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED_ASYNC (obj, error, context)

  if (!gabble_handle_is_valid (obj->handles,
                               handle_type,
                               handle,
                               &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return FALSE;
    }

  sender = dbus_g_method_get_sender (context);
  _gabble_connection_client_hold_handle (obj, sender, handle, handle_type);
  dbus_g_method_return (context);

  return TRUE;
}


/**
 * gabble_connection_inspect_handle
 *
 * Implements DBus method InspectHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_inspect_handle (GabbleConnection *obj, guint handle_type, guint handle, gchar ** ret, GError **error)
{
  GabbleConnectionPrivate *priv;
  const char *tmp;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  if (!gabble_handle_is_valid (obj->handles,
                               handle_type,
                               handle,
                               error))
    return FALSE;

  tmp = gabble_handle_inspect (obj->handles, handle_type, handle);
  g_assert (tmp != NULL);
  *ret = g_strdup (tmp);

  return TRUE;
}

gboolean gabble_connection_inspect_handles (GabbleConnection *obj, guint handle_type, const GArray *handles, DBusGMethodInvocation *context)
{
  GabbleConnectionPrivate *priv;
  GError *error = NULL;
  const gchar **ret;
  int i;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED_ASYNC (obj, error, context);

  if (!gabble_handles_are_valid (obj->handles,
                                 handle_type,
                                 handles,
                                 FALSE,
                                 &error))
    {
      dbus_g_method_return_error (context, error);

      g_error_free (error);

      return FALSE;
    }

  ret = g_new (const gchar *, handles->len + 1);

  for (i = 0; i < handles->len; i++)
    {
      GabbleHandle handle;
      const gchar *tmp;

      handle = g_array_index (handles, GabbleHandle, i);
      tmp = gabble_handle_inspect (obj->handles, handle_type, handle);
      g_assert (tmp != NULL);

      ret[i] = tmp;
    }

  ret[i] = NULL;

  dbus_g_method_return (context, ret);

  g_free (ret);

  return TRUE;
}

/**
 * list_channel_factory_foreach_one:
 * @key: iterated key
 * @value: iterated value
 * @data: data attached to this key/value pair
 *
 * Called by the exported ListChannels function, this should iterate over
 * the handle/channel pairs in a channel factory, and to the GPtrArray in
 * the data pointer, add a GValueArray containing the following:
 *  a D-Bus object path for the channel object on this service
 *  a D-Bus interface name representing the channel type
 *  an integer representing the handle type this channel communicates with, or zero
 *  an integer handle representing the contact, room or list this channel communicates with, or zero
 */
static void
list_channel_factory_foreach_one (TpChannelIface *chan,
                                  gpointer data)
{
  GObject *channel = G_OBJECT (chan);
  GPtrArray *channels = (GPtrArray *) data;
  gchar *path, *type;
  guint handle_type, handle;
  GValue entry = { 0, };

  g_value_init (&entry, TP_CHANNEL_LIST_ENTRY_TYPE);
  g_value_take_boxed (&entry, dbus_g_type_specialized_construct
      (TP_CHANNEL_LIST_ENTRY_TYPE));

  g_object_get (channel,
      "object-path", &path,
      "channel-type", &type,
      "handle-type", &handle_type,
      "handle", &handle,
      NULL);

  dbus_g_type_struct_set (&entry,
      0, path,
      1, type,
      2, handle_type,
      3, handle,
      G_MAXUINT);

  g_ptr_array_add (channels, g_value_get_boxed (&entry));

  g_free (path);
  g_free (type);
}

/**
 * gabble_connection_list_channels
 *
 * Implements DBus method ListChannels
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_list_channels (GabbleConnection *obj, GPtrArray ** ret, GError **error)
{
  GabbleConnectionPrivate *priv;
  GPtrArray *channels;
  guint i;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  /* I think on average, each factory will have 2 channels :D */
  channels = g_ptr_array_sized_new (priv->channel_factories->len * 2);

  for (i = 0; i < priv->channel_factories->len; i++)
    {
      TpChannelFactoryIface *factory = g_ptr_array_index
        (priv->channel_factories, i);
      tp_channel_factory_iface_foreach (factory,
          list_channel_factory_foreach_one, channels);
    }

  for (i = 0; i < priv->media_channels->len; i++)
    {
      list_channel_factory_foreach_one (TP_CHANNEL_IFACE (g_ptr_array_index
            (priv->media_channels, i)), channels);
    }

  if (priv->roomlist_channel)
    {
      list_channel_factory_foreach_one (TP_CHANNEL_IFACE
          (priv->roomlist_channel), channels);
    }

  *ret = channels;

  return TRUE;
}


/**
 * gabble_connection_list_properties
 *
 * Implements DBus method ListProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_list_properties (GabbleConnection *obj, GPtrArray ** ret, GError **error)
{
  return gabble_properties_mixin_list_properties (G_OBJECT (obj), ret, error);
}


/**
 * gabble_connection_release_handle
 *
 * Implements DBus method ReleaseHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean gabble_connection_release_handle (GabbleConnection *obj, guint handle_type, guint handle, DBusGMethodInvocation *context)
{
  GabbleConnectionPrivate *priv;
  char *sender;
  GError *error = NULL;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED_ASYNC (obj, error, context)

  if (!gabble_handle_is_valid (obj->handles,
                               handle_type,
                               handle,
                               &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return FALSE;
    }

  sender = dbus_g_method_get_sender (context);
  _gabble_connection_client_release_handle (obj, sender, handle, handle_type);
  dbus_g_method_return (context);

  return TRUE;
}


/**
 * gabble_connection_remove_status
 *
 * Implements DBus method RemoveStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_remove_status (GabbleConnection *obj, const gchar * status, GError **error)
{
  GabblePresence *presence;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  g_assert (GABBLE_IS_CONNECTION (obj));

  ERROR_IF_NOT_CONNECTED (obj, *error)

  presence = gabble_presence_cache_get (obj->presence_cache, obj->self_handle);

  if (strcmp (status, gabble_statuses[presence->status].name) == 0)
    {
      gabble_presence_cache_update (obj->presence_cache, obj->self_handle, priv->resource, GABBLE_PRESENCE_AVAILABLE, NULL, priv->priority);
      emit_one_presence_update (obj, obj->self_handle);
      return signal_own_presence (obj, error);
    }
  else
    {
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "Attempting to remove non-existent presence.");
      return FALSE;
    }
}

static GabbleMediaChannel *
find_media_channel_with_handle (GabbleConnection *conn, GabbleHandle handle)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  guint i, j;

  for (i = 0; i < priv->media_channels->len; i++)
    {
      GArray *arr;
      GError *err;

      GabbleMediaChannel *chan = g_ptr_array_index (priv->media_channels, i);

      /* search members */
      if (!gabble_group_mixin_get_members (G_OBJECT (chan), &arr, &err))
        {
          g_debug ("%s: get_members failed: %s", G_STRFUNC, err->message);
          g_error_free (err);
          continue;
        }

      for (j = 0; j < arr->len; j++)
        if (g_array_index (arr, guint32, i) == handle)
          {
            g_array_free (arr, TRUE);
            return chan;
          }

      g_array_free (arr, TRUE);

      /* search local pending */
      if (!gabble_group_mixin_get_local_pending_members (G_OBJECT (chan), &arr, &err))
        {
          g_debug ("%s: get_local_pending_members failed: %s", G_STRFUNC,
              err->message);
          g_error_free (err);
          continue;
        }

      for (j = 0; j < arr->len; j++)
        if (g_array_index (arr, guint32, i) == handle)
          {
            g_array_free (arr, TRUE);
            return chan;
          }

      g_array_free (arr, TRUE);

      /* search remote pending */
      if (!gabble_group_mixin_get_remote_pending_members (G_OBJECT (chan), &arr, &err))
        {
          g_debug ("%s: get_remote_pending_members failed: %s", G_STRFUNC,
              err->message);
          g_error_free (err);
          continue;
        }

      for (j = 0; j < arr->len; j++)
        if (g_array_index (arr, guint32, i) == handle)
          {
            g_array_free (arr, TRUE);
            return chan;
          }

      g_array_free (arr, TRUE);
    }

  return NULL;
}

static gboolean
_gabble_connection_request_channel_deprecated (GabbleConnection *obj, const gchar *type, guint handle_type, guint handle, gboolean suppress_handler, gchar **ret, GError **error)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  if (!strcmp (type, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
    {
      GabbleMediaChannel *chan;

      if (handle_type == 0)
        {
          /* create an empty channel */
          chan = new_media_channel (obj, obj->self_handle, suppress_handler);
        }
      else
        {
          /* have we already got a channel with this handle? */
          chan = find_media_channel_with_handle (obj, handle);

          /* no: create it and add the peer to it */
          if (!chan)
            {
              GArray *members;
              gboolean ret;

              chan = new_media_channel (obj, obj->self_handle, suppress_handler);

              members = g_array_sized_new (FALSE, FALSE, sizeof (GabbleHandle), 1);
              g_array_append_val (members, handle);

              ret = gabble_group_mixin_add_members (G_OBJECT (chan), members, "", error);

              g_array_free (members, TRUE);

              if (!ret)
                {
                  GError *close_err;

                  if (!gabble_media_channel_close (chan, &close_err))
                    {
                      g_error_free (close_err);
                    }

                  return FALSE;
                }
            }
        }

      g_object_get (chan, "object-path", ret, NULL);
    }
  else if (!strcmp (type, TP_IFACE_CHANNEL_TYPE_ROOM_LIST))
    {
      if (NULL == priv->conference_servers &&
          NULL == priv->fallback_conference_server)
        {
          g_debug ("%s: no conference server available for roomlist request",
                   G_STRFUNC);

          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "unable to "
              "list rooms because we have not discovered any local conference "
              "servers and no fallback was provided");

          return FALSE;
        }
      make_roomlist_channel (obj, suppress_handler);
      g_object_get (priv->roomlist_channel, "object-path", ret, NULL);
    }
  else
    {
      return FALSE;
    }

  return TRUE;
}


/**
 * gabble_connection_request_aliases
 *
 * Implements DBus method RequestAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_request_aliases (GabbleConnection *obj, const GArray * contacts, gchar *** ret, GError **error)
{
  int i;
  gchar **aliases;

  g_assert (GABBLE_IS_CONNECTION (obj));

  ERROR_IF_NOT_CONNECTED (obj, *error)

  if (!gabble_handles_are_valid (obj->handles, TP_HANDLE_TYPE_CONTACT,
        contacts, FALSE, error))
    return FALSE;

  aliases = g_new0 (gchar *, contacts->len + 1);

  for (i = 0; i < contacts->len; i++)
    {
      GabbleHandle handle = g_array_index (contacts, GabbleHandle, i);
      GabbleConnectionAliasSource source;
      gchar *alias;

      source = _gabble_connection_get_cached_alias (obj, handle, &alias);

      g_assert (source != GABBLE_CONNECTION_ALIAS_NONE);
      g_assert (NULL != alias);

      aliases[i] = alias;
    }

  *ret = aliases;

  return TRUE;
}


/**
 * gabble_connection_request_channel
 *
 * Implements DBus method RequestChannel
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean gabble_connection_request_channel (GabbleConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean suppress_handler, DBusGMethodInvocation *context)
{
  GabbleConnectionPrivate *priv;
  TpChannelFactoryRequestStatus status = TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
  gchar *object_path = NULL;
  GError *error = NULL;
  int i;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED_ASYNC (obj, error, context);

  for (i = 0; i < priv->channel_factories->len; i++)
    {
      TpChannelFactoryIface *factory = g_ptr_array_index (priv->channel_factories, i);
      TpChannelFactoryRequestStatus cur_status;
      TpChannelIface *chan = NULL;
      ChannelRequest *request = NULL;

      priv->suppress_next_handler = suppress_handler;

      cur_status = tp_channel_factory_iface_request (factory, type,
          (TpHandleType) handle_type, handle, &chan);

      priv->suppress_next_handler = FALSE;

      switch (cur_status)
        {
        case TP_CHANNEL_FACTORY_REQUEST_STATUS_DONE:
          g_assert (NULL != chan);
          g_object_get (chan, "object-path", &object_path, NULL);
          goto OUT;
        case TP_CHANNEL_FACTORY_REQUEST_STATUS_QUEUED:
          g_debug ("%s: queueing request, channel_type=%s, handle_type=%u, "
              "handle=%u, suppress_handler=%u", G_STRFUNC, type, handle_type,
              handle, suppress_handler);
          request = channel_request_new (context, type, handle_type, handle, suppress_handler);
          g_ptr_array_add (priv->channel_requests, request);
          return TRUE;
        default:
          /* always return the most specific error */
          if (cur_status > status)
            status = cur_status;
        }
    }

  /* TODO: delete this bit */
  if (_gabble_connection_request_channel_deprecated (obj, type, handle_type,
        handle, suppress_handler, &object_path, &error))
    {
      g_assert (NULL != object_path);
      goto OUT;
    }
  else if (NULL != error)
    {
      goto OUT;
    }

  switch (status)
    {
      case TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE:
        g_debug ("%s: invalid handle %u", G_STRFUNC, handle);

        error = g_error_new (TELEPATHY_ERRORS, InvalidHandle,
                             "invalid handle %u", handle);

        break;

      case TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE:
        g_debug ("%s: requested channel is unavailable with "
                 "handle type %u", G_STRFUNC, handle_type);

        error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                             "requested channel is not available with "
                             "handle type %u", handle_type);

        break;

      case TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED:
        g_debug ("%s: unsupported channel type %s", G_STRFUNC, type);

        error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                             "unsupported channel type %s", type);

        break;

      default:
        g_assert_not_reached ();
    }

OUT:
  if (NULL != error)
    {
      g_assert (NULL == object_path);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return FALSE;
    }

  g_assert (NULL != object_path);
  dbus_g_method_return (context, object_path);
  g_free (object_path);

  return TRUE;
}


#if 0
static LmHandlerResult
contact_info_got_vcard (GabbleConnection *conn, LmMessage *sent_msg,
                        LmMessage *reply_msg, GObject *object,
                        gpointer user_data)
{
  GabbleConnectionPrivate *priv;
  GabbleHandle contact = GPOINTER_TO_INT (user_data);
  LmMessageNode *node, *child;
  node = lm_message_node_get_child (reply_msg->node, "vCard");
  GString *vcard = g_string_new("");
  gchar *str;

  g_assert (GABBLE_IS_CONNECTION (object));

  priv = GABBLE_CONNECTION_GET_PRIVATE (object);

  if (!node)
    {
      g_debug ("%s: request to %s returned with no contact info",
               G_STRFUNC, gabble_handle_inspect (conn->handles, TP_HANDLE_TYPE_CONTACT, contact));
      g_signal_emit (conn, signals[GOT_CONTACT_INFO], 0, contact, "");
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

 g_debug ("%s: request to %s returned contact info:",
               G_STRFUNC, gabble_handle_inspect (conn->handles, TP_HANDLE_TYPE_CONTACT, contact));
  child = node->children;
  for (;child; child = child->next)
    {
      str = lm_message_node_to_string (child);
      //g_debug ("%s: %s", G_STRFUNC, str);
      if (0 != strcmp (child->name, "PHOTO")
       && 0 != strcmp (child->name, "photo"))
        {
          g_string_append_printf (vcard, "  %s", str);
        }
      g_free (str);
    }

  g_signal_emit (conn, signals[GOT_CONTACT_INFO], 0, contact, vcard->str);

  g_string_free (vcard, TRUE);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/**
 * gabble_connection_request_contact_info
 *
 * Implements DBus method RequestContactInfo
 * on interface org.freedesktop.Telepathy.Connection.Interface.ContactInfo
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_request_contact_info (GabbleConnection *obj, guint contact, GError **error)
{
  GabbleConnectionPrivate *priv;
  LmMessage *msg;
  LmMessageNode *vcard_node;
  const char *contact_jid;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  if (!gabble_handle_is_valid (obj->handles,
                               TP_HANDLE_TYPE_CONTACT,
                               contact,
                               error))
    return FALSE;

  contact_jid = gabble_handle_inspect (obj->handles, TP_HANDLE_TYPE_CONTACT,
                                       contact);

  /* build the message */
  msg = lm_message_new_with_sub_type (contact_jid, LM_MESSAGE_TYPE_IQ,
                                     LM_MESSAGE_SUB_TYPE_GET);

  vcard_node = lm_message_node_add_child (msg->node, "vCard", NULL);

  lm_message_node_set_attribute (vcard_node, "xmlns", "vcard-temp");


  if (!_gabble_connection_send_with_reply (obj, msg, contact_info_got_vcard,
                                           G_OBJECT(obj), GINT_TO_POINTER (contact),
                                           error))
    {
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}
#endif


typedef struct {
    GabbleConnection *conn;
    gchar *jid;
    DBusGMethodInvocation *context;
} RoomVerifyContext;

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
  LmMessageNode *lm_node;
  GabbleConnectionPrivate *priv;

  priv = GABBLE_CONNECTION_GET_PRIVATE (rvctx->conn);

  if (error != NULL)
    {
      g_debug ("%s: disco reply error %s", G_STRFUNC, error->message);
      dbus_g_method_return_error (rvctx->context, error);

      goto OUT;
    }

  for (lm_node = query_result->children; lm_node; lm_node = lm_node->next)
    {
      if (strcmp (lm_node->name, "feature") == 0)
        {
          const gchar *name;

          name = lm_message_node_get_attribute (lm_node, "var");
          if (name != NULL)
            {
              if (strcmp (name, NS_MUC) == 0)
                {
                  gchar *sender;
                  GabbleHandle handle;

                  handle = gabble_handle_for_room (rvctx->conn->handles, rvctx->jid);
                  g_assert (handle != 0);

                  sender = dbus_g_method_get_sender (rvctx->context);
                  _gabble_connection_client_hold_handle (rvctx->conn, sender, handle, TP_HANDLE_TYPE_ROOM);

                  g_debug ("%s: disco reported MUC support for service name in jid %s", G_STRFUNC, rvctx->jid);

                  dbus_g_method_return (rvctx->context, handle);

                  goto OUT;
                }
            }
        }
    }

  error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                      "specified server doesn't support MUC");
  dbus_g_method_return_error (rvctx->context, error);

OUT:
  g_free (rvctx->jid);
  g_free (rvctx);
}

/**
 * room_jid_verify:
 *
 * Utility function that verifies that the service name of
 * the specified jid exists and reports MUC support.
 */
static gboolean
room_jid_verify (GabbleConnection *conn, const gchar *jid,
                 DBusGMethodInvocation *context, GError **error)
{
  GabbleConnectionPrivate *priv;
  gchar *room, *service;
  gboolean ret;
  RoomVerifyContext *rvctx;

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  room = service = NULL;
  gabble_handle_decode_jid (jid, &room, &service, NULL);

  g_assert (room && service);

  rvctx = g_new (RoomVerifyContext, 1);
  rvctx->conn = conn;
  rvctx->jid = g_strdup (jid);
  rvctx->context = context;

  ret = (gabble_disco_request (conn->disco, GABBLE_DISCO_TYPE_INFO, service, NULL,
                               room_jid_disco_cb, rvctx, G_OBJECT (conn), error) != NULL);

  g_free (room);
  g_free (service);

  return ret;
}

static gchar *room_name_to_canonical (GabbleConnection *conn, const gchar *name)
{
  GabbleConnectionPrivate *priv;
  const gchar *server;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (index (name, '@'))
    return g_strdup (name);

  if (priv->conference_servers)
    server = priv->conference_servers->data;
  else if (priv->fallback_conference_server)
    server = priv->fallback_conference_server;
  else
    return NULL;

  return g_strdup_printf ("%s@%s", name, server);
}

/**
 * gabble_connection_request_handle
 *
 * Implements DBus method RequestHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean gabble_connection_request_handle (GabbleConnection *obj, guint handle_type, const gchar * name, DBusGMethodInvocation *context)
{
  GabbleConnectionPrivate *priv;
  GabbleHandle handle;
  gchar *sender, *qualified_name;
  GError *error = NULL;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED_ASYNC (obj, error, context)

  if (!gabble_handle_type_is_valid (handle_type, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return FALSE;
    }

  switch (handle_type)
    {
    case TP_HANDLE_TYPE_CONTACT:
      if (!gabble_handle_jid_is_valid (handle_type, name, &error))
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);

          return FALSE;
        }

      handle = gabble_handle_for_contact (obj->handles, name, FALSE);

      if (handle == 0)
        {
          g_debug ("%s: requested handle %s was invalid", G_STRFUNC, name);

          error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                               "requested handle %s was invalid", name);
          dbus_g_method_return_error (context, error);
          g_error_free (error);

          return FALSE;
        }

      break;
    case TP_HANDLE_TYPE_ROOM:
      qualified_name = room_name_to_canonical (obj, name);

      if (!qualified_name)
        {
          g_debug ("%s: requested handle %s contains no conference server", G_STRFUNC, name);

          error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "requested "
              "room handle %s does not specify a server, but we have not discovered "
              "any local conference servers and no fallback was provided", name);
          dbus_g_method_return_error (context, error);
          g_error_free (error);

          return FALSE;
        }

      /* has the handle been verified before? */
      if (gabble_handle_for_room_exists (obj->handles, qualified_name, FALSE))
        {
          handle = gabble_handle_for_room (obj->handles, qualified_name);

          g_free (qualified_name);
        }
      else
        {
          gboolean success;

          /* verify it */
          success = room_jid_verify (obj, qualified_name, context, &error);

          g_free (qualified_name);

          if (success)
            {
              return TRUE;
            }
          else
            {
              dbus_g_method_return_error (context, error);
              g_error_free (error);

              return FALSE;
            }
        }

      break;
    case TP_HANDLE_TYPE_LIST:
      if (!strcmp (name, "publish"))
        {
          handle = gabble_handle_for_list_publish (obj->handles);
        }
      else if (!strcmp (name, "subscribe"))
        {
          handle = gabble_handle_for_list_subscribe (obj->handles);
        }
      else if (!strcmp (name, "known"))
        {
          handle = gabble_handle_for_list_known (obj->handles);
        }
      else
        {
          g_debug ("%s: requested list channel %s not available", G_STRFUNC, name);

          error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                               "requested list channel %s not available", name);
          dbus_g_method_return_error (context, error);
          g_error_free (error);

          return FALSE;
        }
      break;
    default:
      g_debug ("%s: unimplemented handle type %u", G_STRFUNC, handle_type);

      error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                          "unimplemented handle type %u", handle_type);
      dbus_g_method_return_error (context, error);
      g_error_free (error);

      return FALSE;
    }

  sender = dbus_g_method_get_sender (context);
  _gabble_connection_client_hold_handle (obj, sender, handle, handle_type);
  dbus_g_method_return (context, handle);

  return TRUE;
}


/**
 * gabble_connection_request_presence
 *
 * Implements DBus method RequestPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_request_presence (GabbleConnection *obj, const GArray * contacts, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  if (!gabble_handles_are_valid (obj->handles, TP_HANDLE_TYPE_CONTACT, contacts, FALSE, error))
    return FALSE;

  if (contacts->len)
    emit_presence_update (obj, contacts);

  return TRUE;
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
  GabbleHandle handle = GPOINTER_TO_INT (key);
  gchar *alias = (gchar *) value;
  GError *error = NULL;

  if (!gabble_handle_is_valid (data->conn->handles, TP_HANDLE_TYPE_CONTACT, handle,
        &error))
    {
      data->retval = FALSE;
    }
  else if (!gabble_roster_handle_set_name (data->conn->roster, handle, alias,
        data->error))
    {
      data->retval = FALSE;
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
 * Implements DBus method SetAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_set_aliases (GabbleConnection *obj, GHashTable * aliases, GError **error)
{
  GabbleConnectionPrivate *priv;
  struct _i_hate_g_hash_table_foreach data = { NULL, NULL, TRUE };

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  data.conn = obj;
  data.error = error;

  g_hash_table_foreach (aliases, setaliases_foreach, &data);

  return data.retval;
}


/**
 * gabble_connection_set_last_activity_time
 *
 * Implements DBus method SetLastActivityTime
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_set_last_activity_time (GabbleConnection *obj, guint time, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  return TRUE;
}


static void
setstatuses_foreach (gpointer key, gpointer value, gpointer user_data)
{
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach*) user_data;
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
          g_debug ("%s: requested status %s is not available", G_STRFUNC,
             (const gchar *) key);
          *(data->error) = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                             "requested status '%s' is not available on this connection",
                             (const gchar *) key);
          data->retval = FALSE;
          return;
        }

      if (message)
        {
          if (!G_VALUE_HOLDS_STRING (message))
            {
              g_debug ("%s: got a status message which was not a string", G_STRFUNC);
              *(data->error) = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
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
              g_debug ("%s: got a priority value which was not a signed integer", G_STRFUNC);
              *(data->error) = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                 "Status argument 'priority' requires a signed integer");
              data->retval = FALSE;
              return;
            }
          prio = CLAMP (g_value_get_int (priority), G_MININT8, G_MAXINT8);
        }

      gabble_presence_cache_update (data->conn->presence_cache, data->conn->self_handle, priv->resource, i, status, prio);
      emit_one_presence_update (data->conn, data->conn->self_handle);
      data->retval = signal_own_presence (data->conn, data->error);
    }
  else
    {
      g_debug ("%s: got unknown status identifier %s", G_STRFUNC, (const gchar *) key);
      *(data->error) = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                    "unknown status identifier: %s",
                                    (const gchar *) key);
      data->retval = FALSE;
    }
}

/**
 * gabble_connection_set_properties
 *
 * Implements DBus method SetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @context: The DBUS invocation context to use to return values
 *           or throw an error.
 */
gboolean gabble_connection_set_properties (GabbleConnection *obj, const GPtrArray * properties, DBusGMethodInvocation *context)
{
  return gabble_properties_mixin_set_properties (G_OBJECT (obj), properties, context);
}

/**
 * gabble_connection_set_status
 *
 * Implements DBus method SetStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_set_status (GabbleConnection *obj, GHashTable * statuses, GError **error)
{
  GabbleConnectionPrivate *priv;
  struct _i_hate_g_hash_table_foreach data = { NULL, NULL, TRUE };

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  if (g_hash_table_size (statuses) != 1)
    {
      g_debug ("%s: got more than one status", G_STRFUNC);
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                 "Only one status may be set at a time in this protocol");
      return FALSE;
    }

  data.conn = obj;
  data.error = error;
  g_hash_table_foreach (statuses, setstatuses_foreach, &data);

  return data.retval;
}

