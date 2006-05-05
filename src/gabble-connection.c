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

#define DBUS_API_SUBJECT_TO_CHANGE
#define _GNU_SOURCE /* Needed for strptime (_XOPEN_SOURCE can also be used). */

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <loudmouth/loudmouth.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib-object.h>

#include "handles.h"
#include "handle-set.h"
#include "telepathy-constants.h"
#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"

#include "gabble-connection.h"
#include "gabble-connection-glue.h"
#include "gabble-connection-signals-marshal.h"

#include "gabble-register.h"
#include "gabble-im-channel.h"
#include "gabble-muc-channel.h"
#include "gabble-media-channel.h"
#include "gabble-roster-channel.h"
#include "gabble-disco.h"
#include "gabble-roomlist-channel.h"
#include "gabble-presence.h"
#include "gabble-presence-cache.h"

#define BUS_NAME        "org.freedesktop.Telepathy.Connection.gabble"
#define OBJECT_PATH     "/org/freedesktop/Telepathy/Connection/gabble"

#define NS_PRESENCE_INVISIBLE "presence-invisible"
#define NS_PRIVACY            "jabber:iq:privacy"
#define NS_ROSTER             "jabber:iq:roster"

#define TP_CAPABILITY_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID))

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

#define DEFAULT_CONFERENCE_SERVER "conference.jabber.org"

typedef struct _StatusInfo StatusInfo;

struct _StatusInfo
{
  const gchar *name;
  TpConnectionPresenceType presence_type;
  const gboolean self;
  const gboolean exclusive;
};

static const StatusInfo gabble_statuses[LAST_GABBLE_PRESENCE] = {
 { "available", TP_CONN_PRESENCE_TYPE_AVAILABLE,     TRUE, TRUE },
 { "away",      TP_CONN_PRESENCE_TYPE_AWAY,          TRUE, TRUE },
 { "chat",      TP_CONN_PRESENCE_TYPE_AVAILABLE,     TRUE, TRUE },
 { "dnd",       TP_CONN_PRESENCE_TYPE_AWAY,          TRUE, TRUE },
 { "xa",        TP_CONN_PRESENCE_TYPE_EXTENDED_AWAY, TRUE, TRUE },
 { "offline",   TP_CONN_PRESENCE_TYPE_OFFLINE,       TRUE, TRUE },
 { "hidden",    TP_CONN_PRESENCE_TYPE_HIDDEN,        TRUE, TRUE }
};

/* signal enum */
enum
{
    CAPABILITIES_CHANGED,
    GOT_CONTACT_INFO,
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

    LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleConnectionPrivate GabbleConnectionPrivate;

struct _GabbleConnectionPrivate
{
  LmMessageHandler *message_im_cb;
  LmMessageHandler *message_muc_cb;
  LmMessageHandler *presence_muc_cb;
  LmMessageHandler *presence_roster_cb;
  LmMessageHandler *iq_roster_cb;
  LmMessageHandler *iq_jingle_cb;
  LmMessageHandler *iq_disco_cb;
  LmMessageHandler *iq_unknown_cb;

  /* telepathy properties */
  gchar *protocol;

  /* connection properties */
  gchar *connect_server;
  guint port;
  gboolean old_ssl;

  gboolean do_register;

  gboolean low_bandwidth;

  gchar *https_proxy_server;
  guint https_proxy_port;

  gchar *fallback_conference_server;

  gchar *stun_server;
  guint stun_port;
  gchar *stun_relay_magic_cookie;
  gchar *stun_relay_server;
  guint stun_relay_udp_port;
  guint stun_relay_tcp_port;
  guint stun_relay_ssltcp_port;
  gchar *stun_relay_username;
  gchar *stun_relay_password;

  /* authentication properties */
  gchar *stream_server;
  gchar *username;
  gchar *password;
  gchar *resource;

  /* jingle sessions */
  GHashTable *jingle_sessions;

  /* channels */
  GHashTable *im_channels;

  GHashTable *muc_channels;

  GPtrArray *media_channels;
  guint media_channel_index;

  GabbleRosterChannel *publish_channel;
  GabbleRosterChannel *subscribe_channel;

  GabbleRoomlistChannel *roomlist_channel;

  /* clients */
  GData *client_contact_handle_sets;
  GData *client_room_handle_sets;
  GData *client_list_handle_sets;

  /* server services */
  GList *conference_servers;

  /* gobject housekeeping */
  gboolean dispose_has_run;
};

#define GABBLE_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_CONNECTION, GabbleConnectionPrivate))

static void
gabble_connection_init (GabbleConnection *obj)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  obj->status = TP_CONN_STATUS_CONNECTING;
  obj->handles = gabble_handle_repo_new ();
  obj->disco = gabble_disco_new (obj);
  obj->presence_cache = NULL;

  priv->jingle_sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, NULL);

  priv->im_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                             NULL, g_object_unref);

  priv->muc_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                              NULL, g_object_unref);

  priv->media_channels = g_ptr_array_sized_new (1);
  priv->media_channel_index = 0;

  g_datalist_init (&priv->client_contact_handle_sets);
  g_datalist_init (&priv->client_room_handle_sets);
  g_datalist_init (&priv->client_list_handle_sets);

  /* Set default parameters for optional parameters */
  priv->resource = g_strdup (GABBLE_PARAMS_DEFAULT_RESOURCE);
  priv->port = GABBLE_PARAMS_DEFAULT_PORT;
  priv->old_ssl = GABBLE_PARAMS_DEFAULT_OLD_SSL;
  priv->do_register = FALSE;
  priv->https_proxy_server = g_strdup (GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_SERVER);
  priv->https_proxy_port = GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT;
}

static void
gabble_connection_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleConnection *self = (GabbleConnection *) object;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

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
    case PROP_HTTPS_PROXY_SERVER:
      g_value_set_string (value, priv->https_proxy_server);
      break;
    case PROP_HTTPS_PROXY_PORT:
      g_value_set_uint (value, priv->https_proxy_port);
      break;
    case PROP_FALLBACK_CONFERENCE_SERVER:
      g_value_set_string (value, priv->fallback_conference_server);
      break;
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
    default:
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
    default:
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

  /** signal definitions */

  signals[CAPABILITIES_CHANGED] =
    g_signal_new ("capabilities-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__INT_BOXED_BOXED,
                  G_TYPE_NONE, 3, G_TYPE_UINT, (dbus_g_type_get_collection ("GPtrArray", G_TYPE_VALUE_ARRAY)), (dbus_g_type_get_collection ("GPtrArray", G_TYPE_VALUE_ARRAY)));

  signals[GOT_CONTACT_INFO] =
    g_signal_new ("got-contact-info",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__INT_STRING,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

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

  if (priv->im_channels)
    {
      g_assert (g_hash_table_size (priv->im_channels) == 0);
      g_hash_table_destroy (priv->im_channels);
      priv->im_channels = NULL;
    }

  if (priv->muc_channels)
    {
      g_assert (g_hash_table_size (priv->muc_channels) == 0);
      g_hash_table_destroy (priv->muc_channels);
      priv->muc_channels = NULL;
    }

  if (priv->media_channels)
    {
      g_assert (priv->media_channels->len == 0);
      g_ptr_array_free (priv->media_channels, TRUE);
      priv->media_channels = NULL;
    }

  g_object_unref (self->disco);
  self->disco = NULL;

  if (self->lmconn)
    {
      if (lm_connection_is_open (self->lmconn))
        {
          g_warning ("%s: connection was open when the object was deleted, it'll probably crash now...", G_STRFUNC);
          lm_connection_close (self->lmconn, NULL);
        }

      lm_connection_unregister_message_handler (self->lmconn, priv->message_im_cb,
                                                LM_MESSAGE_TYPE_MESSAGE);
      lm_message_handler_unref (priv->message_im_cb);

      lm_connection_unregister_message_handler (self->lmconn, priv->message_muc_cb,
                                                LM_MESSAGE_TYPE_MESSAGE);
      lm_message_handler_unref (priv->message_muc_cb);

      lm_connection_unregister_message_handler (self->lmconn, priv->presence_muc_cb,
                                                LM_MESSAGE_TYPE_PRESENCE);
      lm_message_handler_unref (priv->presence_muc_cb);

      lm_connection_unregister_message_handler (self->lmconn, priv->presence_roster_cb,
                                                LM_MESSAGE_TYPE_PRESENCE);
      lm_message_handler_unref (priv->presence_roster_cb);

      lm_connection_unregister_message_handler (self->lmconn, priv->iq_roster_cb,
                                                LM_MESSAGE_TYPE_IQ);
      lm_message_handler_unref (priv->iq_roster_cb);

      lm_connection_unregister_message_handler (self->lmconn, priv->iq_jingle_cb,
                                                LM_MESSAGE_TYPE_IQ);
      lm_message_handler_unref (priv->iq_jingle_cb);

      lm_connection_unregister_message_handler (self->lmconn, priv->iq_disco_cb,
                                                LM_MESSAGE_TYPE_IQ);
      lm_message_handler_unref (priv->iq_disco_cb);

      lm_connection_unregister_message_handler (self->lmconn, priv->iq_unknown_cb,
                                                LM_MESSAGE_TYPE_IQ);
      lm_message_handler_unref (priv->iq_unknown_cb);
    }

  g_object_unref (self->presence_cache);

  dbus_g_proxy_call_no_reply (bus_proxy, "ReleaseName",
                              G_TYPE_STRING, self->bus_name,
                              G_TYPE_INVALID);

  if (G_OBJECT_CLASS (gabble_connection_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_connection_parent_class)->dispose (object);
}

static gboolean
_unref_lm_connection (gpointer data)
{
  LmConnection *conn = (LmConnection *) data;

  lm_connection_unref (conn);
  return FALSE;
}

void
gabble_connection_finalize (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  g_debug ("%s called with %p", G_STRFUNC, object);

  /*
   * The Loudmouth connection can't be unref'd immediately because this
   * function might (indirectly) return into Loudmouth code which expects the
   * connection to always be there.
   */
  if (self->lmconn)
    g_idle_add (_unref_lm_connection, self->lmconn);

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
  g_free (priv->stun_server);
  g_free (priv->stun_relay_magic_cookie);
  g_free (priv->stun_relay_server);
  g_free (priv->stun_relay_username);
  g_free (priv->stun_relay_password);

  g_list_free (priv->conference_servers);

  g_datalist_clear (&priv->client_room_handle_sets);
  g_datalist_clear (&priv->client_contact_handle_sets);
  g_datalist_clear (&priv->client_list_handle_sets);

  gabble_handle_repo_destroy (self->handles);

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

  /* only set the connect server if one hasn't already been specified */
  if (!priv->connect_server)
    g_object_set (G_OBJECT (conn), "connect-server", server, NULL);

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
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            request_error->message);
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

      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            "Error acquiring bus name %s, %s",
                             conn->bus_name, msg);
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

static LmHandlerResult connection_message_im_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_message_muc_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_presence_muc_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_presence_roster_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_iq_roster_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_iq_jingle_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_iq_disco_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmHandlerResult connection_iq_unknown_cb (LmMessageHandler*, LmConnection*, LmMessage*, gpointer);
static LmSSLResponse connection_ssl_cb (LmSSL*, LmSSLStatus, gpointer);
static void connection_open_cb (LmConnection*, gboolean, gpointer);
static void connection_auth_cb (LmConnection*, gboolean, gpointer);
static void connection_disco_cb (GabbleDisco *disco, const gchar *jid,
                                 const gchar *node, LmMessageNode *result,
                                 GError *disco_error, gpointer user_data);
static void connection_disconnected_cb (LmConnection *connection, LmDisconnectReason lm_reason, gpointer user_data);
static void connection_status_change (GabbleConnection *, TpConnectionStatus, TpConnectionStatusReason);

static void close_all_channels (GabbleConnection *conn);
static GabbleIMChannel *new_im_channel (GabbleConnection *conn, GabbleHandle handle, gboolean suppress_handler);
static void make_roster_channels (GabbleConnection *conn);
static void discover_services (GabbleConnection *conn);
static void emit_one_presence_update (GabbleConnection *self, GabbleHandle handle);

static void
presence_update_cb (GabblePresenceCache *cache, GabbleHandle handle, gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  if (handle == conn->self_handle)
    g_debug ("ignoring presence from ourselves on another resource");
  else
    emit_one_presence_update (conn, handle);
}

static gboolean
do_connect (GabbleConnection *conn, GError **error)
{
  GError *lmerror = NULL;

  g_debug ("%s: calling lm_connection_open", G_STRFUNC);
  if (!lm_connection_open (conn->lmconn, connection_open_cb,
                           conn, NULL, &lmerror))
    {
      g_debug ("%s: %s", G_STRFUNC, lmerror->message);

      *error = g_error_new (TELEPATHY_ERRORS, NetworkError,
                            "lm_connection_open_failed: %s", lmerror->message);

      g_error_free (lmerror);

      return FALSE;
    }

  return TRUE;
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
 * Stage 2 is connection_open_cb calling lm_connection_auth
 * Stage 3 is connection_auth_cb initiating service discovery
 * Stage 4 is connection_disco_cb advertising initial presence, requesting
 *   the roster and setting the CONNECTED state
 */
gboolean
_gabble_connection_connect (GabbleConnection *conn,
                            GError          **error)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_assert (priv->connect_server != NULL);
  g_assert (priv->port > 0 && priv->port <= G_MAXUINT16);
  g_assert (priv->stream_server != NULL);
  g_assert (priv->username != NULL);
  g_assert (priv->password != NULL);
  g_assert (priv->resource != NULL);

  if (conn->lmconn == NULL)
    {
      char *jid;
      gboolean valid;
      GabblePresence *presence;

      conn->lmconn = lm_connection_new (priv->connect_server);
      conn->presence_cache = gabble_presence_cache_new (conn->lmconn, conn->handles);
      g_signal_connect (conn->presence_cache, "presence-update", (GCallback) presence_update_cb, conn);

      if (priv->https_proxy_server)
        {
          LmProxy *proxy;

          proxy = lm_proxy_new_with_server (LM_PROXY_TYPE_HTTP,
              priv->https_proxy_server, priv->https_proxy_port);

          lm_connection_set_proxy (conn->lmconn, proxy);

          lm_proxy_unref (proxy);
        }

      lm_connection_set_port (conn->lmconn, priv->port);

      /* send whitespace to the server every 30 seconds */
      lm_connection_set_keep_alive_rate (conn->lmconn, 30);

      jid = g_strdup_printf ("%s@%s", priv->username, priv->stream_server);
      lm_connection_set_jid (conn->lmconn, jid);

      lm_connection_set_disconnect_function (conn->lmconn,
                                             connection_disconnected_cb,
                                             conn,
                                             NULL);

      conn->self_handle = gabble_handle_for_contact (conn->handles,
                                                     jid, FALSE);

      if (conn->self_handle == 0)
        {
          /* FIXME: check this sooner and return an error to the user
           * this will be when we implement Connect() in spec 0.13 */
          g_error ("%s: invalid jid %s", G_STRFUNC, jid);
          return FALSE;
        }

      valid = gabble_handle_ref (conn->handles,
                                 TP_HANDLE_TYPE_CONTACT,
                                 conn->self_handle);
      g_assert (valid);

      /* set initial presence. TODO: some way for the user to set this */
      gabble_presence_cache_update (conn->presence_cache, conn->self_handle, priv->resource, GABBLE_PRESENCE_AVAILABLE, NULL, 0);
      emit_one_presence_update (conn, conn->self_handle);
      presence = gabble_presence_cache_get (conn->presence_cache, conn->self_handle);
      g_assert (presence);
      gabble_presence_set_capabilities (presence, priv->resource,
          PRESENCE_CAP_JINGLE_VOICE | PRESENCE_CAP_GOOGLE_VOICE);

      g_free (jid);

      if (priv->old_ssl)
        {
          LmSSL *ssl = lm_ssl_new (NULL, connection_ssl_cb, conn, NULL);
          lm_connection_set_ssl (conn->lmconn, ssl);
          lm_ssl_unref (ssl);
        }

      priv->message_im_cb = lm_message_handler_new (connection_message_im_cb,
                                                    conn, NULL);
      lm_connection_register_message_handler (conn->lmconn, priv->message_im_cb,
                                              LM_MESSAGE_TYPE_MESSAGE,
                                              LM_HANDLER_PRIORITY_LAST);

      priv->message_muc_cb = lm_message_handler_new (connection_message_muc_cb,
                                                     conn, NULL);
      lm_connection_register_message_handler (conn->lmconn, priv->message_muc_cb,
                                              LM_MESSAGE_TYPE_MESSAGE,
                                              LM_HANDLER_PRIORITY_FIRST);

      priv->presence_muc_cb = lm_message_handler_new (connection_presence_muc_cb,
                                                  conn, NULL);
      lm_connection_register_message_handler (conn->lmconn, priv->presence_muc_cb,
                                              LM_MESSAGE_TYPE_PRESENCE,
                                              LM_HANDLER_PRIORITY_FIRST);

      priv->presence_roster_cb = lm_message_handler_new (connection_presence_roster_cb,
                                                  conn, NULL);
      lm_connection_register_message_handler (conn->lmconn, priv->presence_roster_cb,
                                              LM_MESSAGE_TYPE_PRESENCE,
                                              LM_HANDLER_PRIORITY_LAST);

      priv->iq_roster_cb = lm_message_handler_new (connection_iq_roster_cb,
                                                   conn, NULL);
      lm_connection_register_message_handler (conn->lmconn, priv->iq_roster_cb,
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
  else
    {
      g_assert (lm_connection_is_open (conn->lmconn) == FALSE);
    }

  do_connect (conn, error);

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
          /* remove the channels so we don't get any race conditions
           * where method calls are delivered to a channel after we've started
           * disconnecting */
          close_all_channels (conn);
        }

      g_debug ("%s emitting status-changed with status %u reason %u",
               G_STRFUNC, status, reason);

      g_signal_emit (conn, signals[STATUS_CHANGED], 0, status, reason);

      if (status == TP_CONN_STATUS_DISCONNECTED)
        {
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


static void im_channel_closed_cb (GabbleIMChannel *chan, gpointer user_data);

gboolean hash_foreach_close_im_channel (gpointer key,
                                    gpointer value,
                                    gpointer user_data)
{
  GabbleIMChannel *chan = GABBLE_IM_CHANNEL (value);
  GError *error = NULL;

  g_signal_handlers_disconnect_by_func (chan, (GCallback) im_channel_closed_cb,
                                       user_data);
  g_debug ("%s calling gabble_im_channel_close on %p", G_STRFUNC, chan);
  gabble_im_channel_close (chan, &error);
  g_debug ("%s removing channel %p", G_STRFUNC, chan);
  return TRUE;
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

  if (priv->im_channels)
    {
      g_hash_table_destroy (priv->im_channels);
      priv->im_channels = NULL;
    }

  if (priv->muc_channels)
    {
      g_hash_table_destroy (priv->muc_channels);
      priv->muc_channels = NULL;
    }

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

  if (priv->publish_channel)
    {
      g_object_unref (priv->publish_channel);
      priv->publish_channel = NULL;
    }

  if (priv->subscribe_channel)
    {
      g_object_unref (priv->subscribe_channel);
      priv->subscribe_channel = NULL;
    }

  priv->media_channel_index = 0;
}


/**
 * muc_channel_closed_cb:
 *
 * Signal callback for when a MUC channel is closed. Removes the references
 * that #GabbleConnection holds to them.
 */
static void
muc_channel_closed_cb (GabbleMucChannel *chan, gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GabbleHandle room_handle;

  g_object_get (chan, "handle", &room_handle, NULL);

  g_debug ("%s: removing MUC channel with handle %d", G_STRFUNC, room_handle);
  g_hash_table_remove (priv->muc_channels, GINT_TO_POINTER (room_handle));
}

/**
 * new_muc_channel
 */
static GabbleMucChannel *
new_muc_channel (GabbleConnection *conn, GabbleHandle handle, gboolean suppress_handler)
{
  GabbleConnectionPrivate *priv;
  GabbleMucChannel *chan;
  char *object_path;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_assert (g_hash_table_lookup (priv->muc_channels, GINT_TO_POINTER (handle)) == NULL);

  object_path = g_strdup_printf ("%s/MucChannel%u", conn->object_path, handle);

  chan = g_object_new (GABBLE_TYPE_MUC_CHANNEL,
                       "connection", conn,
                       "object-path", object_path,
                       "handle", handle,
                       NULL);

  g_debug ("new_muc_channel: object path %s", object_path);

  g_signal_connect (chan, "closed", (GCallback) muc_channel_closed_cb, conn);

  g_hash_table_insert (priv->muc_channels, GINT_TO_POINTER (handle), chan);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, TP_IFACE_CHANNEL_TYPE_TEXT,
                 TP_HANDLE_TYPE_ROOM, handle,
                 suppress_handler);

  g_free (object_path);

  return chan;
}

LmMessageNode *
_get_muc_node (LmMessageNode *toplevel_node)
{
  LmMessageNode *node;

  for (node = toplevel_node->children; node; node = node->next)
    if (strcmp (node->name, "x") == 0)
      {
        const gchar *xmlns = lm_message_node_get_attribute (node, "xmlns");

        if (xmlns && strcmp (xmlns, MUC_XMLNS_USER) == 0)
          return node;
      }

  return NULL;
}

static GabbleMucChannel *
get_muc_from_jid (GabbleConnection *conn, const gchar *jid)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GabbleHandle handle;
  GabbleMucChannel *chan = NULL;

  if (gabble_handle_for_room_exists (conn->handles, jid, TRUE))
    {
      handle = gabble_handle_for_room (conn->handles, jid);

      chan = g_hash_table_lookup (priv->muc_channels,
                                  GUINT_TO_POINTER (handle));
    }

  return chan;
}

static gboolean
parse_incoming_message (LmMessage *message,
                        const gchar **from,
                        time_t *stamp,
                        TpChannelTextMessageType *msgtype,
                        const gchar **body,
                        const gchar **body_offset)
{
  const gchar *type;
  LmMessageNode *node;

  *from = lm_message_node_get_attribute (message->node, "from");
  if (*from == NULL)
    {
      HANDLER_DEBUG (message->node, "got a message without a from field");
      return FALSE;
    }

  type = lm_message_node_get_attribute (message->node, "type");

  /*
   * Parse timestamp of delayed messages.
   */
  *stamp = 0;

  for (node = message->node->children; node; node = node->next)
    {
      if (strcmp (node->name, "x") == 0)
        {
          const gchar *xmlns, *stamp_str, *p;
          struct tm stamp_tm = { 0, };

          xmlns = lm_message_node_get_attribute (node, "xmlns");
          if (xmlns == NULL)
            continue;

          if (strcmp (xmlns, "jabber:x:delay") != 0)
            continue;

          stamp_str = lm_message_node_get_attribute (node, "stamp");
          if (stamp_str == NULL)
            continue;

          p = strptime (stamp_str, "%Y%m%dT%T", &stamp_tm);
          if (p == NULL || *p != '\0')
            {
              g_warning ("%s: malformed date string '%s' for jabber:x:delay",
                         G_STRFUNC, stamp_str);
              continue;
            }

          *stamp = timegm (&stamp_tm);
        }
    }

  if (*stamp == 0)
    *stamp = time (NULL);


  /*
   * Parse body if it exists.
   */
  node = lm_message_node_get_child (message->node, "body");

  if (node)
    {
      *body = lm_message_node_get_value (node);
    }
  else
    {
      *body = NULL;
    }

  /* Messages starting with /me are ACTION messages, and the /me should be
   * removed. type="chat" messages are NORMAL.  everything else is
   * something that doesn't necessarily expect a reply or ongoing
   * conversation ("normal") or has been auto-sent, so we make it NOTICE in
   * all other cases. */

  *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
  *body_offset = *body;

  if (*body)
    {
      if (0 == strncmp (*body, "/me ", 4))
        {
          *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;
          *body_offset = *body + 4;
        }
      else if (type != NULL && (0 == strcmp (type, "chat") ||
                                0 == strcmp (type, "groupchat")))
        {
          *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
          *body_offset = *body;
        }
    }

  return TRUE;
}

/**
 * connection_message_im_cb:
 *
 * Called by loudmouth when we get an incoming <message>.
 */
static LmHandlerResult
connection_message_im_cb (LmMessageHandler *handler,
                          LmConnection *lmconn,
                          LmMessage *message,
                          gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  const gchar *from, *body, *body_offset;
  time_t stamp;
  TpChannelTextMessageType msgtype;
  GabbleHandle handle;
  GabbleIMChannel *chan;

  g_assert (lmconn == conn->lmconn);

  if (!parse_incoming_message (message, &from, &stamp, &msgtype, &body,
                               &body_offset))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  if (body == NULL)
    {
      HANDLER_DEBUG (message->node, "got a message without a body field, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  handle = gabble_handle_for_contact (conn->handles, from, FALSE);
  if (handle == 0)
    {
      HANDLER_DEBUG (message->node, "ignoring message node from malformed jid");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  g_debug ("%s: message from %s (handle %u), msgtype %d, body:\n%s",
           G_STRFUNC, from, handle, msgtype, body_offset);

  chan = g_hash_table_lookup (priv->im_channels, GINT_TO_POINTER (handle));

  if (chan == NULL)
    {
      g_debug ("%s: found no IM channel, creating one", G_STRFUNC);

      chan = new_im_channel (conn, handle, FALSE);
    }

  if (_gabble_im_channel_receive (chan, msgtype, handle,
                                  stamp, body_offset))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

/**
 * connection_message_muc_cb:
 *
 * Called by loudmouth when we get an incoming <message>,
 * which might be for a MUC.
 */
static LmHandlerResult
connection_message_muc_cb (LmMessageHandler *handler,
                           LmConnection *lmconn,
                           LmMessage *message,
                           gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  const gchar *from, *body, *body_offset;
  time_t stamp;
  TpChannelTextMessageType msgtype;
  LmMessageNode *node;
  GabbleHandle room_handle, handle;
  GabbleMucChannel *chan;

  g_assert (lmconn == conn->lmconn);

  if (!parse_incoming_message (message, &from, &stamp, &msgtype, &body,
                               &body_offset))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* does it have a muc subnode? */
  node = _get_muc_node (message->node);
  if (node)
    {
      /* and an invitation? */
      node = lm_message_node_get_child (node, "invite");
      if (node)
        {
          LmMessageNode *reason_node;
          const gchar *invite_from, *reason;
          GabbleHandle inviter_handle;

          invite_from = lm_message_node_get_attribute (node, "from");
          if (invite_from == NULL)
            {
              HANDLER_DEBUG (message->node, "got a MUC invitation message "
                             "without a from field on the invite node, "
                             "ignoring");

              return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
            }

          inviter_handle = gabble_handle_for_contact (conn->handles,
                                                      invite_from, FALSE);

          reason_node = lm_message_node_get_child (node, "reason");
          if (reason_node)
            {
              reason = lm_message_node_get_value (reason_node);
            }
          else
            {
              reason = "";
              HANDLER_DEBUG (message->node, "no MUC invite reason specified");
            }

          /* create the channel */
          handle = gabble_handle_for_room (conn->handles, from);

          if (g_hash_table_lookup (priv->muc_channels, GINT_TO_POINTER (handle)) == NULL)
            {
              chan = new_muc_channel (conn, handle, FALSE);
              _gabble_muc_channel_handle_invited (chan, inviter_handle, reason);
            }
          else
            {
              HANDLER_DEBUG (message->node, "ignoring invite to a room we're already in");
            }

          return LM_HANDLER_RESULT_REMOVE_MESSAGE;
        }
    }

  /* check if a room with the jid exists */
  if (!gabble_handle_for_room_exists (conn->handles, from, TRUE))
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  room_handle = gabble_handle_for_room (conn->handles, from);

  /* find the MUC channel */
  chan = g_hash_table_lookup (priv->muc_channels,
                              GUINT_TO_POINTER (room_handle));

  if (!chan)
    {
      g_warning ("%s: ignoring groupchat message from known handle with "
                 "no MUC channel", G_STRFUNC);

      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  /* get the handle of the sender, which is either the room
   * itself or one of its members */
  if (gabble_handle_for_room_exists (conn->handles, from, FALSE))
    {
      handle = room_handle;
    }
  else
    {
      handle = gabble_handle_for_contact (conn->handles, from, TRUE);
    }

  if (_gabble_muc_channel_receive (chan, msgtype, handle, stamp,
                                   body_offset, message))
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

/**
 * _get_contact_presence_quark:
 *
 * Returns: the quark used for storing presence information on
 *          a GabbleHandle
 */
GQuark
_get_contact_presence_quark()
{
  static GQuark presence_quark = 0;
  if (!presence_quark)
    presence_quark = g_quark_from_static_string("ContactPresenceQuark");
  return presence_quark;
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

      g_assert (gabble_handle_is_valid (self->handles, TP_HANDLE_TYPE_CONTACT, handle, NULL));

      /* WTF */
      if (!presence)
        {
          g_debug ("no presence in cache for %d", handle);
          continue;
        }

      message = g_new0 (GValue, 1);
      g_value_init (message, G_TYPE_STRING);
      g_value_set_static_string (message, presence->status_message);

      parameters =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) destroy_the_bastard);

      g_hash_table_insert (parameters, "message", message);

      contact_status =
        g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) g_hash_table_destroy);
      g_hash_table_insert (
        contact_status, (gchar *) gabble_statuses[presence->status].name,
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
  GabblePresence *presence = gabble_presence_cache_get (self->presence_cache, self->self_handle);
  LmMessage *message = NULL;
  LmMessageNode *node;
  LmMessageSubType subtype;

  if (presence->status == GABBLE_PRESENCE_OFFLINE)
    subtype = LM_MESSAGE_SUB_TYPE_UNAVAILABLE;
  else
    subtype = LM_MESSAGE_SUB_TYPE_AVAILABLE;

  message = lm_message_new_with_sub_type (NULL, LM_MESSAGE_TYPE_PRESENCE,
              subtype);

  if (presence->status == GABBLE_PRESENCE_HIDDEN)
    {
      if ((self->features & GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE) != 0)
        lm_message_node_set_attribute (message->node, "type", "invisible");
    }

  node = lm_message_get_node (message);

  if (presence->status_message)
    {
      lm_message_node_add_child (node, "status", presence->status_message);
    }

  switch (presence->status)
    {
    case GABBLE_PRESENCE_AVAILABLE:
    case GABBLE_PRESENCE_OFFLINE:
    case GABBLE_PRESENCE_HIDDEN:
      break;
    case GABBLE_PRESENCE_AWAY:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_AWAY);
      break;
    case GABBLE_PRESENCE_CHAT:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_CHAT);
      break;
    case GABBLE_PRESENCE_DND:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_DND);
      break;
    case GABBLE_PRESENCE_XA:
      lm_message_node_add_child (node, "show", JABBER_PRESENCE_SHOW_XA);
      break;
    default:
      g_critical ("%s: Unexpected Telepathy presence type", G_STRFUNC);
      break;
    }

  /* FIXME: use constants from libloudmouth and libjingle here */
  node = lm_message_node_add_child (node,
                                    "c", NULL);
  lm_message_node_set_attributes (node,
                                  "node",  "http://telepathy.freedesktop.org/caps",
                                  "ver",   GABBLE_VERSION,
                                  "ext",   "voice-v1",
                                  "xmlns", "http://jabber.org/protocol/caps",
                                  NULL);

  if (!_gabble_connection_send (self, message, error))
    goto ERROR;

  lm_message_unref (message);
  return TRUE;

ERROR:
  if (message)
    lm_message_unref(message);

  return FALSE;
}


/**
 * connection_presence_muc_cb:
 * @handler: #LmMessageHandler for this message
 * @connection: #LmConnection that originated the message
 * @message: the presence message
 * @user_data: callback data
 *
 * Called by loudmouth when we get an incoming <presence>.
 */
static LmHandlerResult
connection_presence_muc_cb (LmMessageHandler *handler,
                            LmConnection *lmconn,
                            LmMessage *msg,
                            gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  const char *from;
  LmMessageSubType sub_type;
  GabbleMucChannel *muc_chan;
  LmMessageNode *x_node;

  g_assert (lmconn == conn->lmconn);

  from = lm_message_node_get_attribute (msg->node, "from");

  if (from == NULL)
    {
      HANDLER_DEBUG (msg->node, "presence stanza without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  sub_type = lm_message_get_sub_type (msg);

  muc_chan = get_muc_from_jid (conn, from);

  /* is it an error and for a MUC? */
  if (sub_type == LM_MESSAGE_SUB_TYPE_ERROR
      && muc_chan != NULL)
    {
      _gabble_muc_channel_presence_error (muc_chan, from, msg->node);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  x_node = _get_muc_node (msg->node);

  /* is it a MUC member presence? */
  if (x_node)
    {
      if (muc_chan != NULL)
        {
          GabbleHandle handle;

          handle = gabble_handle_for_contact (conn->handles, from, TRUE);

          _gabble_muc_channel_member_presence_updated (muc_chan, handle,
                                                       msg, x_node);
        }
      else
        {
          HANDLER_DEBUG (msg->node, "unexpected MUC member presence");
        }
    }

  /* intentionally do not remove the message, so that the
   * normal presence handler gets called */
  return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}



/**
 * connection_presence_roster_cb:
 * @handler: #LmMessageHandler for this message
 * @connection: #LmConnection that originated the message
 * @message: the presence message
 * @user_data: callback data
 *
 * Called by loudmouth when we get an incoming <presence>.
 */
static LmHandlerResult
connection_presence_roster_cb (LmMessageHandler *handler,
                               LmConnection *lmconn,
                               LmMessage *message,
                               gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  LmMessageNode *pres_node, *child_node;
  const char *from;
  LmMessageSubType sub_type;
  GIntSet *empty, *tmp;
  GabbleHandle handle;
  LmMessage *reply = NULL;
  const gchar *status_message = NULL;

  g_assert (lmconn == conn->lmconn);

  pres_node = lm_message_get_node (message);

  from = lm_message_node_get_attribute (pres_node, "from");

  if (from == NULL)
    {
      HANDLER_DEBUG (pres_node, "presence stanza without from attribute, ignoring");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  sub_type = lm_message_get_sub_type (message);

  if (_get_muc_node (pres_node))
    {
      HANDLER_DEBUG (pres_node, "ignoring MUC presence");

      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  handle = gabble_handle_for_contact (conn->handles, from, FALSE);

  if (handle == 0)
    {
      HANDLER_DEBUG (pres_node, "ignoring presence from malformed jid");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (handle == conn->self_handle)
    {
      HANDLER_DEBUG (pres_node, "ignoring presence from ourselves on another resource");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  g_assert (handle != 0);

  child_node = lm_message_node_get_child (pres_node, "status");
  if (child_node)
    status_message = lm_message_node_get_value (child_node);

  switch (sub_type)
    {
    case LM_MESSAGE_SUB_TYPE_SUBSCRIBE:
      empty = g_intset_new ();
      tmp = g_intset_new ();

      g_debug ("%s: making %s (handle %u) local pending on the publish channel",
          G_STRFUNC, from, handle);

      /* make the contact local pending on the publish channel */
      g_intset_add (tmp, handle);
      gabble_group_mixin_change_members (G_OBJECT (priv->publish_channel),
          status_message, empty, empty, tmp, empty);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE:
      empty = g_intset_new ();
      tmp = g_intset_new ();

      g_debug ("%s: removing %s (handle %u) from the publish channel",
          G_STRFUNC, from, handle);

      /* remove the contact from the publish channel */
      g_intset_add (tmp, handle);
      gabble_group_mixin_change_members (G_OBJECT (priv->publish_channel),
          status_message, empty, tmp, empty, empty);

      /* acknowledge the change */
      reply = lm_message_new_with_sub_type (from,
                LM_MESSAGE_TYPE_PRESENCE,
                LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED);
      _gabble_connection_send (conn, reply, NULL);
      lm_message_unref (reply);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    case LM_MESSAGE_SUB_TYPE_SUBSCRIBED:
      empty = g_intset_new ();
      tmp = g_intset_new ();

      g_debug ("%s: adding %s (handle %u) to the subscribe channel",
          G_STRFUNC, from, handle);

      /* add the contact to the subscribe channel */
      g_intset_add (tmp, handle);
      gabble_group_mixin_change_members (G_OBJECT (priv->subscribe_channel),
          status_message, tmp, empty, empty, empty);

      /* acknowledge the change */
      reply = lm_message_new_with_sub_type (from,
                LM_MESSAGE_TYPE_PRESENCE,
                LM_MESSAGE_SUB_TYPE_SUBSCRIBE);
      _gabble_connection_send (conn, reply, NULL);
      lm_message_unref (reply);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    case LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED:
      empty = g_intset_new ();
      tmp = g_intset_new ();

      g_debug ("%s: removing %s (handle %u) from the subscribe channel",
          G_STRFUNC, from, handle);

      /* remove the contact from the subscribe channel */
      g_intset_add (tmp, handle);
      gabble_group_mixin_change_members (G_OBJECT (priv->subscribe_channel),
          status_message, empty, tmp, empty, empty);

      /* acknowledge the change */
      reply = lm_message_new_with_sub_type (from,
                LM_MESSAGE_TYPE_PRESENCE,
                LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE);
      _gabble_connection_send (conn, reply, NULL);
      lm_message_unref (reply);

      g_intset_destroy (empty);
      g_intset_destroy (tmp);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    default:
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }
}


/**
 * connection_iq_roster_cb
 *
 * Called by loudmouth when we get an incoming <iq>. This handler
 * is concerned only with roster queries, and allows other handlers
 * if queries other than rosters are received.
 */
static LmHandlerResult
connection_iq_roster_cb (LmMessageHandler *handler,
                         LmConnection *lmconn,
                         LmMessage *message,
                         gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  LmMessageNode *iq_node, *query_node;

  g_assert (lmconn == conn->lmconn);

  iq_node = lm_message_get_node (message);
  query_node = lm_message_node_get_child (iq_node, "query");

  if (!query_node || strcmp (NS_ROSTER,
        lm_message_node_get_attribute (query_node, "xmlns")))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  /* if this is a result, it's from our initial query. if it's a set,
   * it's a roster push. either way, parse the items. */
  if (lm_message_get_sub_type (message) == LM_MESSAGE_SUB_TYPE_RESULT ||
      lm_message_get_sub_type (message) == LM_MESSAGE_SUB_TYPE_SET)
    {
      LmMessageNode *item_node;
      GIntSet *empty, *pub_add, *pub_rem,
              *sub_add, *sub_rem, *sub_rp;

      /* asymmetry is because we don't get locally pending subscription
       * requests via <roster>, we get it via <presence> */
      empty = g_intset_new ();
      pub_add = g_intset_new ();
      pub_rem = g_intset_new ();
      sub_add = g_intset_new ();
      sub_rem = g_intset_new ();
      sub_rp = g_intset_new ();

      /* iterate every sub-node, which we expect to be <item>s */
      for (item_node = query_node->children;
           item_node;
           item_node = item_node->next)
        {
          const char *jid, *subscription, *ask;
          GabbleHandle handle;

          if (strcmp (item_node->name, "item"))
            {
              HANDLER_DEBUG (item_node, "query sub-node is not item, skipping");
              continue;
            }

          jid = lm_message_node_get_attribute (item_node, "jid");
          if (!jid)
            {
              HANDLER_DEBUG (item_node, "item node has no jid, skipping");
              continue;
            }

          handle = gabble_handle_for_contact (conn->handles, jid, FALSE);
          if (handle == 0)
            {
              HANDLER_DEBUG (item_node, "item jid is malformed, skipping");
              continue;
            }

          subscription = lm_message_node_get_attribute (item_node, "subscription");
          if (!subscription)
            {
              HANDLER_DEBUG (item_node, "item node has no subscription, skipping");
              continue;
            }

          ask = lm_message_node_get_attribute (item_node, "ask");

          if (!strcmp (subscription, "both"))
            {
              g_intset_add (pub_add, handle);
              g_intset_add (sub_add, handle);
            }
          else if (!strcmp (subscription, "from"))
            {
              g_intset_add (pub_add, handle);
              if (ask != NULL && !strcmp (ask, "subscribe"))
                g_intset_add (sub_rp, handle);
              else
                g_intset_add (sub_rem, handle);
            }
          else if (!strcmp (subscription, "none"))
            {
              g_intset_add (pub_rem, handle);
              if (ask != NULL && !strcmp (ask, "subscribe"))
                g_intset_add (sub_rp, handle);
              else
                g_intset_add (sub_rem, handle);
            }
          else if (!strcmp (subscription, "remove"))
            {
              g_intset_add (pub_rem, handle);
              g_intset_add (sub_rem, handle);
            }
          else if (!strcmp (subscription, "to"))
            {
              g_intset_add (pub_rem, handle);
              g_intset_add (sub_add, handle);
            }
          else
            {
              HANDLER_DEBUG (item_node, "got unexpected subscription value");
            }
        }

      if (g_intset_size (pub_add) > 0 ||
          g_intset_size (pub_rem) > 0)
        {
          g_debug ("%s: calling change members on publish channel", G_STRFUNC);
          gabble_group_mixin_change_members (G_OBJECT (priv->publish_channel),
              "", pub_add, pub_rem, empty, empty);
        }

      if (g_intset_size (sub_add) > 0 ||
          g_intset_size (sub_rem) > 0 ||
          g_intset_size (sub_rp) > 0)
        {
          g_debug ("%s: calling change members on subscribe channel", G_STRFUNC);
          gabble_group_mixin_change_members (G_OBJECT (priv->subscribe_channel),
              "", sub_add, sub_rem, empty, sub_rp);
        }

      g_intset_destroy (empty);
      g_intset_destroy (pub_add);
      g_intset_destroy (pub_rem);
      g_intset_destroy (sub_add);
      g_intset_destroy (sub_rem);
      g_intset_destroy (sub_rp);
    }
  else
    {
      HANDLER_DEBUG (iq_node, "unhandled roster IQ");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  /* if this is a SET, it's a roster push, so we need to send an
   * acknowledgement */
  if (lm_message_get_sub_type (message) == LM_MESSAGE_SUB_TYPE_SET)
    {
      const char *id;

      id = lm_message_node_get_attribute (iq_node, "id");
      if (id == NULL)
        {
          HANDLER_DEBUG (iq_node, "got roster iq set with no id, not replying");
        }
      else
        {
          LmMessage *reply;

          HANDLER_DEBUG (iq_node, "acknowledging roster push");

          reply = lm_message_new_with_sub_type (NULL,
              LM_MESSAGE_TYPE_IQ,
              LM_MESSAGE_SUB_TYPE_RESULT);
          lm_message_node_set_attribute (reply->node, "id", id);
          _gabble_connection_send (conn, reply, NULL);
          lm_message_unref (reply);
        }
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
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

/**
 * _gabble_connection_send_iq_ack
 *
 * Function used to acknowledge an IQ stanza.
 */
void
_gabble_connection_send_iq_ack (GabbleConnection *conn, LmMessageNode *iq_node, LmMessageSubType type)
{
  const gchar *to, *id;
  LmMessage *msg;

  to = lm_message_node_get_attribute (iq_node, "from");
  id = lm_message_node_get_attribute (iq_node, "id");

  msg = lm_message_new_with_sub_type (to, LM_MESSAGE_TYPE_IQ, type);
  lm_message_node_set_attribute (msg->node, "id", id);
  if (!_gabble_connection_send (conn, msg, NULL)) {
      g_warning ("%s: _gabble_connection_send failed", G_STRFUNC);
  }
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
  const gchar *from, *id, *type, *action, *sid;
  GabbleHandle handle;
  GabbleMediaChannel *chan = NULL;
  gpointer k, v;

  g_assert (lmconn == conn->lmconn);

  iq_node = lm_message_get_node (message);
  session_node = lm_message_node_get_child (iq_node, "session");

  /* is it for us? */
  if (!session_node || strcmp (lm_message_node_get_attribute (session_node, "xmlns"),
        "http://www.google.com/session"))
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

  type = lm_message_node_get_attribute (iq_node, "type");
  if (!type)
    {
      HANDLER_DEBUG (iq_node, "'type' attribute not found");
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (strcmp (type, "set") != 0)
    {
      HANDLER_DEBUG (iq_node, "'type' is not \"set\"");
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

      if (strcmp (lm_message_node_get_attribute (desc_node, "xmlns"),
                  "http://www.google.com/session/phone"))
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
      _gabble_media_channel_dispatch_session_action (chan, handle, sid,
          iq_node, session_node, action);
      g_object_unref (chan);
    }
  else
    {
      g_debug ("%s: zombie session %s, we should reject this",
          G_STRFUNC, sid);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
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
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  LmMessage *result;
  LmMessageNode *node, *query, *feature;
  gchar *to_jid;
  const gchar *xmlns, *from_jid;

  if (lm_message_get_sub_type (message) != LM_MESSAGE_SUB_TYPE_GET)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  node = lm_message_get_node (message);

  to_jid = g_strdup_printf ("%s/%s", gabble_handle_inspect (conn->handles, TP_HANDLE_TYPE_CONTACT, conn->self_handle), priv->resource);
  g_assert (0 == strcmp (lm_message_node_get_attribute (node, "to"), to_jid));
  g_free (to_jid);

  from_jid = lm_message_node_get_attribute (node, "from");
  g_assert (from_jid);

  node = lm_message_node_get_child (node, "query");

  if (!node)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  xmlns = lm_message_node_get_attribute (node, "xmlns");

  if (!xmlns)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  if (0 != strcmp (xmlns, "http://jabber.org/protocol/disco#info"))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  result = lm_message_new_with_sub_type (from_jid, LM_MESSAGE_TYPE_IQ,
                                         LM_MESSAGE_SUB_TYPE_RESULT);
  lm_message_node_set_attribute (result->node, "id",
      lm_message_node_get_attribute (message->node, "id"));

  query = lm_message_node_add_child (result->node, "query", NULL);
  lm_message_node_set_attribute (query, "xmlns", xmlns);

  feature = lm_message_node_add_child (query, "feature", NULL);
  lm_message_node_set_attribute (feature, "var",
      "http://jabber.org/protocol/jingle");

  feature = lm_message_node_add_child (query, "feature", NULL);
  lm_message_node_set_attribute (feature, "var",
      "http://jabber.org/protocol/jingle/media/audio");

  feature = lm_message_node_add_child (query, "feature", NULL);
  lm_message_node_set_attribute (feature, "var",
      "http://www.google.com/session");

  feature = lm_message_node_add_child (query, "feature", NULL);
  lm_message_node_set_attribute (feature, "var",
      "http://www.google.com/session/phone");

  HANDLER_DEBUG (result->node, "sending disco response");

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
  LmMessageNode *iq_node;

  g_assert (connection == conn->lmconn);

  iq_node = lm_message_get_node (message);
  HANDLER_DEBUG (iq_node, "got unknown iq");

  /* TODO: return an IQ error for unknown get/set */

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
  const char *reason;
  LmSSLResponse response = LM_SSL_RESPONSE_STOP;

  switch (status) {
    case LM_SSL_STATUS_NO_CERT_FOUND:
      reason = "The server doesn't provide a certificate.";
      response = LM_SSL_RESPONSE_CONTINUE;
      break;
    case LM_SSL_STATUS_UNTRUSTED_CERT:
      reason = "The certificate can not be trusted.";
      response = LM_SSL_RESPONSE_CONTINUE;
      break;
    case LM_SSL_STATUS_CERT_EXPIRED:
      reason = "The certificate has expired.";
      break;
    case LM_SSL_STATUS_CERT_NOT_ACTIVATED:
      reason = "The certificate has not been activated.";
      break;
    case LM_SSL_STATUS_CERT_HOSTNAME_MISMATCH:
      reason = "The server hostname doesn't match the one in the certificate.";
      break;
    case LM_SSL_STATUS_CERT_FINGERPRINT_MISMATCH:
      reason = "The fingerprint doesn't match the expected value.";
      break;
    case LM_SSL_STATUS_GENERIC_ERROR:
      reason = "An unknown SSL error occured.";
      break;
    default:
      g_assert_not_reached();
  }

  g_debug ("%s called: %s", G_STRFUNC, reason);

  if (response == LM_SSL_RESPONSE_CONTINUE)
    g_debug ("proceeding anyway!");
  else
    connection_status_change (conn,
        TP_CONN_STATUS_DISCONNECTED,
        TP_CONN_STATUS_REASON_ENCRYPTION_ERROR);

  return response;
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
          GError *error;

          g_debug ("%s failed, retrying without proxy", G_STRFUNC);
          lm_connection_set_proxy (lmconn, NULL);

          if (do_connect (conn, &error))
            {
              return;
            }
          else
            {
              g_error_free (error);
            }
        }
      else
        {
          g_debug ("%s failed", G_STRFUNC);
        }

      connection_status_change (conn,
          TP_CONN_STATUS_DISCONNECTED,
          TP_CONN_STATUS_REASON_NETWORK_ERROR);

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
 * result of the non-blocking lm_connection_auth call is known. It sends
 * a discovery request to find the server's features.
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
connection_disco_cb (GabbleDisco *disco, const gchar *jid,
                     const gchar *node, LmMessageNode *result,
                     GError *disco_error, gpointer user_data)
{
  GabbleConnection *conn = user_data;
  GabbleConnectionPrivate *priv;
  LmMessage *message = NULL;
  LmMessageNode *msgnode;
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

              if (0 == strcmp (var, NS_PRESENCE_INVISIBLE))
                conn->features |= GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE;
              else if (0 == strcmp (var, NS_PRIVACY))
                conn->features |= GABBLE_CONNECTION_FEATURES_PRIVACY;
            }
        }

      g_debug ("%s: set features flags to %d", G_STRFUNC, conn->features);
    }

  /* go go gadget on-line */
  connection_status_change (conn, TP_CONN_STATUS_CONNECTED, TP_CONN_STATUS_REASON_REQUESTED);

  /* send presence to the server to indicate availability */
  if (!signal_own_presence (conn, &error))
    {
      g_debug ("%s: sending initial presence failed: %s", G_STRFUNC,
          error->message);
      goto ERROR;
    }

  /* send <iq type="get"><query xmnls="jabber:iq:roster" /></iq> to
   * request the roster */
  message = lm_message_new_with_sub_type (NULL,
                                          LM_MESSAGE_TYPE_IQ,
                                          LM_MESSAGE_SUB_TYPE_GET);
  msgnode = lm_message_node_add_child (lm_message_get_node (message),
                                    "query", NULL);
  lm_message_node_set_attribute (msgnode, "xmlns", NS_ROSTER);

  if (!lm_connection_send (conn->lmconn, message, &error))
    {
      g_debug ("%s: initial roster request failed: %s",
               G_STRFUNC, error->message);

      goto ERROR;
    }

  lm_message_unref (message);

  make_roster_channels (conn);

  discover_services (conn);

  return;

ERROR:
  if (error)
    g_error_free (error);

  if (message)
    lm_message_unref (message);

  connection_status_change (conn,
      TP_CONN_STATUS_DISCONNECTED,
      TP_CONN_STATUS_REASON_NETWORK_ERROR);

  return;
}

/**
 * make_roster_channels
 */
static void
make_roster_channels (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv;
  GabbleHandle handle;
  char *object_path;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_assert (priv->publish_channel == NULL);
  g_assert (priv->subscribe_channel == NULL);

  /* make publish list channel */
  handle = gabble_handle_for_list_publish (conn->handles);
  object_path = g_strdup_printf ("%s/RosterChannelPublish", conn->object_path);

  priv->publish_channel = g_object_new (GABBLE_TYPE_ROSTER_CHANNEL,
                                        "connection", conn,
                                        "object-path", object_path,
                                        "handle", handle,
                                        NULL);

  g_debug ("%s: created %s", G_STRFUNC, object_path);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
                 TP_HANDLE_TYPE_LIST, handle,
                 /* suppress handler: */ FALSE);

  g_free (object_path);

  /* make subscribe list channel */
  handle = gabble_handle_for_list_subscribe (conn->handles);
  object_path = g_strdup_printf ("%s/RosterChannelSubscribe", conn->object_path);

  priv->subscribe_channel = g_object_new (GABBLE_TYPE_ROSTER_CHANNEL,
                                          "connection", conn,
                                          "object-path", object_path,
                                          "handle", handle,
                                          NULL);

  g_debug ("%s: created %s", G_STRFUNC, object_path);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
                 TP_HANDLE_TYPE_LIST, handle,
                 /* supress handler: */ FALSE);

  g_free (object_path);
}

static void
roomlist_channel_closed_cb (GabbleRoomlistChannel *chan, gpointer data)
{
  GabbleConnection *conn = data;
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  g_object_unref (priv->roomlist_channel);
  priv->roomlist_channel = NULL;
}

static void
make_roomlist_channel (GabbleConnection *conn, gboolean suppress_handler)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  if (!priv->roomlist_channel)
    {
      gchar *object_path;

      object_path =
        g_strdup_printf ("%s/RoomlistChannel", conn->object_path);
      priv->roomlist_channel =
        _gabble_roomlist_channel_new (conn, object_path,
            priv->conference_servers->data);

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
service_info_cb (GabbleDisco *disco, const gchar *jid, const gchar *node,
                 LmMessageNode *result, GError *error,
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

  HANDLER_DEBUG (result, "got");

  identity = lm_message_node_get_child (result, "identity");
  if (identity)
    {
      category = lm_message_node_get_attribute (identity, "category");
      type = lm_message_node_get_attribute (identity, "type");
      g_debug ("%s: got identity, category=%s, type=%s", G_STRFUNC,
               category, type);
      if (category && 0 == strcmp (category, "conference")
                   && 0 == strcmp (type, "text"))
        {
          for (feature = result->children; feature; feature = feature->next)
            {
              HANDLER_DEBUG (feature, "got child");

              if (0 == strcmp (feature->name, "feature"))
                {
                  var = lm_message_node_get_attribute (feature, "var");
                  if (var &&
                      0 == strcmp (var, "http://jabber.org/protocol/muc"))
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
services_discover_cb (GabbleDisco *disco, const gchar *jid, const gchar *node,
                      LmMessageNode *result, GError *error,
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
  HANDLER_DEBUG (result, "got");

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


/**
 * im_channel_closed_cb:
 *
 * Signal callback for when an IM channel is closed. Removes the references
 * that #GabbleConnection holds to them.
 */
static void
im_channel_closed_cb (GabbleIMChannel *chan, gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GabbleHandle contact_handle;

  g_object_get (chan, "handle", &contact_handle, NULL);

  g_debug ("%s: removing channel with handle %d", G_STRFUNC, contact_handle);
  g_hash_table_remove (priv->im_channels, GINT_TO_POINTER (contact_handle));
}

/**
 * new_im_channel
 */
static GabbleIMChannel *
new_im_channel (GabbleConnection *conn, GabbleHandle handle, gboolean suppress_handler)
{
  GabbleConnectionPrivate *priv;
  GabbleIMChannel *chan;
  char *object_path;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  object_path = g_strdup_printf ("%s/ImChannel%u", conn->object_path, handle);

  chan = g_object_new (GABBLE_TYPE_IM_CHANNEL,
                       "connection", conn,
                       "object-path", object_path,
                       "handle", handle,
                       NULL);

  g_debug ("new_im_channel: object path %s", object_path);

  g_signal_connect (chan, "closed", (GCallback) im_channel_closed_cb, conn);

  g_hash_table_insert (priv->im_channels, GINT_TO_POINTER (handle), chan);

  g_signal_emit (conn, signals[NEW_CHANNEL], 0,
                 object_path, TP_IFACE_CHANNEL_TYPE_TEXT,
                 TP_HANDLE_TYPE_CONTACT, handle,
                 suppress_handler);

  g_free (object_path);

  return chan;
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

  gabble_presence_cache_update (obj->presence_cache, obj->self_handle, priv->resource, GABBLE_PRESENCE_AVAILABLE, NULL, 0);
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
  g_value_set_static_boxed (&vals,
    dbus_g_type_specialized_construct (TP_CAPABILITY_PAIR_TYPE));

  dbus_g_type_struct_set (&vals,
                        0, TP_IFACE_CHANNEL_TYPE_TEXT,
                        1, TP_CONN_CAPABILITY_TYPE_CREATE,
                        G_MAXUINT);

  g_ptr_array_add (*ret, g_value_get_boxed (&vals));

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
      TP_IFACE_CONN_INTERFACE_PRESENCE,
      TP_IFACE_CONN_INTERFACE_CAPABILITIES,
      TP_IFACE_CONN_INTERFACE_CONTACT_INFO,
      NULL };
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
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


/**
 * list_channel_hash_foreach:
 * @key: iterated key
 * @value: iterated value
 * @data: data attached to this key/value pair
 *
 * Called by the exported ListChannels function, this should iterate over
 * the handle/channel pairs in a hash, and to the GPtrArray in the
 * ListChannelInfo struct, add a GValueArray containing the following:
 *  a D-Bus object path for the channel object on this service
 *  a D-Bus interface name representing the channel type
 *  an integer representing the handle type this channel communicates with, or zero
 *  an integer handle representing the contact, room or list this channel communicates with, or zero
 */
static void
list_channel_hash_foreach (gpointer key,
                           gpointer value,
                           gpointer data)
{
  GObject *channel = G_OBJECT (value);
  GPtrArray *channels = (GPtrArray *) data;
  char *path, *type;
  guint handle_type, handle;
  GValueArray *vals;

  g_object_get (channel, "object-path", &path,
                         "channel-type", &type,
                         "handle-type", &handle_type,
                         "handle", &handle, NULL);

  g_debug ("list_channels_foreach_hash: adding path %s, type %s, "
           "handle type %u, handle %u", path, type, handle_type, handle);

  vals = g_value_array_new (4);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 0), DBUS_TYPE_G_OBJECT_PATH);
  g_value_set_boxed (g_value_array_get_nth (vals, 0), path);
  g_free (path);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 1), G_TYPE_STRING);
  g_value_set_string (g_value_array_get_nth (vals, 1), type);
  g_free (type);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 2), G_TYPE_UINT);
  g_value_set_uint (g_value_array_get_nth (vals, 2), handle_type);

  g_value_array_append (vals, NULL);
  g_value_init (g_value_array_get_nth (vals, 3), G_TYPE_UINT);
  g_value_set_uint (g_value_array_get_nth (vals, 3), handle);

  g_ptr_array_add (channels, vals);
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
  guint count;
  GPtrArray *channels;
  guint i;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  count = g_hash_table_size (priv->im_channels);
  count += g_hash_table_size (priv->muc_channels);
  count += priv->media_channels->len;

  channels = g_ptr_array_sized_new (count);

  g_hash_table_foreach (priv->im_channels, list_channel_hash_foreach, channels);

  g_hash_table_foreach (priv->muc_channels, list_channel_hash_foreach, channels);

  for (i = 0; i < priv->media_channels->len; i++)
    {
      list_channel_hash_foreach (NULL, g_ptr_array_index (priv->media_channels, i), channels);
    }

  if (priv->publish_channel)
    list_channel_hash_foreach (NULL, priv->publish_channel, channels);

  if (priv->subscribe_channel)
    list_channel_hash_foreach (NULL, priv->subscribe_channel, channels);

  *ret = channels;

  return TRUE;
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
      gabble_presence_cache_update (obj->presence_cache, obj->self_handle, priv->resource, GABBLE_PRESENCE_AVAILABLE, NULL, 0);
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
          g_debug ("%s: get_members failed", G_STRFUNC);
          g_free (err);
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
          g_debug ("%s: get_local_pending_members failed", G_STRFUNC);
          g_free (err);
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
          g_debug ("%s: get_remote_pending_members failed", G_STRFUNC);
          g_free (err);
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

/**
 * gabble_connection_request_channel
 *
 * Implements DBus method RequestChannel
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_connection_request_channel (GabbleConnection *obj, const gchar * type, guint handle_type, guint handle, gboolean suppress_handler, gchar ** ret, GError **error)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (obj));

  priv = GABBLE_CONNECTION_GET_PRIVATE (obj);

  ERROR_IF_NOT_CONNECTED (obj, *error)

  if (!strcmp (type, TP_IFACE_CHANNEL_TYPE_TEXT))
    {
      GObject *chan;

      if (!gabble_handle_is_valid (obj->handles,
                                   handle_type,
                                   handle,
                                   error))
        return FALSE;

      if (handle_type == TP_HANDLE_TYPE_CONTACT)
        {
          chan = g_hash_table_lookup (priv->im_channels, GINT_TO_POINTER (handle));

          if (chan == NULL)
            {
              chan = G_OBJECT (new_im_channel (obj, handle, suppress_handler));
            }
        }
      else if (handle_type == TP_HANDLE_TYPE_ROOM)
        {
          chan = g_hash_table_lookup (priv->muc_channels, GINT_TO_POINTER (handle));

          if (chan == NULL)
            {
              GArray *members;
              gboolean ret;

              chan = G_OBJECT (new_muc_channel (obj, handle, suppress_handler));

              members = g_array_sized_new (FALSE, FALSE, sizeof (GabbleHandle), 1);
              g_array_append_val (members, obj->self_handle);

              ret = gabble_group_mixin_add_members (chan, members, "", error);

              g_array_free (members, TRUE);

              if (!ret)
                {
                  GError *close_err;

                  if (!gabble_muc_channel_close (GABBLE_MUC_CHANNEL (chan),
                                                 &close_err))
                    {
                      g_error_free (close_err);
                    }

                  return FALSE;
                }
            }
        }
      else
        {
          goto NOT_AVAILABLE;
        }

      if (chan)
        {
          g_object_get (chan, "object-path", ret, NULL);
        }
      else
        {
          goto NOT_AVAILABLE;
        }
    }
  else if (!strcmp (type, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST))
    {
      GabbleRosterChannel *chan;

      if (handle_type != TP_HANDLE_TYPE_LIST)
        goto NOT_AVAILABLE;

      if (!gabble_handle_is_valid (obj->handles,
                                   handle_type,
                                   handle,
                                   error))
        return FALSE;

      if (handle == gabble_handle_for_list_publish (obj->handles))
        chan = priv->publish_channel;
      else if (handle == gabble_handle_for_list_subscribe (obj->handles))
        chan = priv->subscribe_channel;
      else
        g_assert_not_reached ();

      g_object_get (chan, "object-path", ret, NULL);
    }
  else if (!strcmp (type, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
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
      if (!priv->conference_servers)
        {
          g_debug ("%s: no conference server found when requesting roomlist",
                   G_STRFUNC);

          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                                "no conference server found when "
                                "requesting roomlist");
          return FALSE;
        }
      make_roomlist_channel (obj, suppress_handler);
      g_object_get (priv->roomlist_channel, "object-path", ret, NULL);
    }
  else
    {
      goto NOT_IMPLEMENTED;
    }

  return TRUE;

NOT_AVAILABLE:
  g_debug ("request_channel: requested channel is unavailable with "
           "handle type %u", handle_type);

  *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                        "requested channel is not available with "
                        "handle type %u", handle_type);

  return FALSE;

NOT_IMPLEMENTED:
  g_debug ("request_channel: unsupported channel type %s", type);

  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                        "unsupported channel type %s", type);

  return FALSE;
}


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

typedef struct {
    GabbleConnection *conn;
    gchar *jid;
    DBusGMethodInvocation *context;
} RoomVerifyContext;

static void
room_jid_disco_cb (GabbleDisco *disco, const gchar *jid, const gchar *node,
                   LmMessageNode *query_result, GError *error,
                   gpointer user_data)
{
  RoomVerifyContext *rvctx = user_data;
  LmMessageNode *lm_node;
  GabbleConnectionPrivate *priv;

  priv = GABBLE_CONNECTION_GET_PRIVATE (rvctx->conn);

  if (error != NULL)
    {
      g_debug ("%s: DISCO reply error %s", G_STRFUNC, error->message);
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
              if (strcmp (name, MUC_XMLNS) == 0)
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
    {
      return g_strdup (name);
    }

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
      server = DEFAULT_CONFERENCE_SERVER;
    }

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

  /*TODO; what do we do about requests for non-rostered contacts?*/

  if (contacts->len)
    emit_presence_update (obj, contacts);

  return TRUE;
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

struct _i_hate_g_hash_table_foreach
{
  GabbleConnection *conn;
  GError **error;
  gboolean retval;
};

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
      gint8 prio = 0;

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
          if (!G_VALUE_HOLDS_UINT (priority))
            {
              g_debug ("%s: got a priority value which was not an unsigned int", G_STRFUNC);
              *(data->error) = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                 "Status argument 'priority' requires an unsigned int");
              data->retval = FALSE;
              return;
            }
          prio = CLAMP (g_value_get_uint (priority), G_MININT8, G_MAXINT8);
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

