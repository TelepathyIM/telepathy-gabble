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

#include "gabble-connection.h"

#define DBUS_API_SUBJECT_TO_CHANGE

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <glib-object.h>
#include <loudmouth/loudmouth.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/channel-factory-iface.h>

#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>

#include <telepathy-glib/svc-connection.h>

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "capabilities.h"
#include "conn-aliasing.h"
#include "conn-avatars.h"
#include "conn-presence.h"
#include "conn-olpc.h"
#include "debug.h"
#include "disco.h"
#include "presence-cache.h"
#include "presence.h"
#include "gabble-register.h"
#include "im-factory.h"
#include "media-factory.h"
#include "muc-factory.h"
#include "namespaces.h"
#include "pubsub.h"
#include "roster.h"
#include "util.h"
#include "vcard-manager.h"

#include "gabble-media-channel.h"
#include "gabble-roomlist-channel.h"

#define GABBLE_TP_ALIAS_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID))
#define GABBLE_TP_CAPABILITY_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID))
#define GABBLE_TP_CAPABILITIES_CHANGED_MONSTER_TYPE (dbus_g_type_get_struct \
    ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, \
                    G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID))
#define GABBLE_TP_GET_CAPABILITIES_MONSTER_TYPE (dbus_g_type_get_struct \
    ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, \
                    G_TYPE_INVALID))

static void conn_service_iface_init (gpointer, gpointer);
static void capabilities_service_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(GabbleConnection,
    gabble_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION,
      conn_service_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
      conn_aliasing_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS,
      conn_avatars_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CAPABILITIES,
      capabilities_service_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE,
      conn_presence_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_OLPC_BUDDY_INFO,
      olpc_buddy_info_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_OLPC_ACTIVITY_PROPERTIES,
      olpc_activity_properties_iface_init);
    )

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
    PROP_IGNORE_SSL_ERRORS,
    PROP_ALIAS,
    PROP_AUTH_MAC,
    PROP_AUTH_BTID,

    LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleConnectionPrivate GabbleConnectionPrivate;

struct _GabbleConnectionPrivate
{
  LmMessageHandler *iq_disco_cb;
  LmMessageHandler *iq_unknown_cb;
  LmMessageHandler *stream_error_cb;
  LmMessageHandler *msg_cb;

  /* connection properties */
  gchar *connect_server;
  guint port;
  gboolean old_ssl;

  gboolean ignore_ssl_errors;
  TpConnectionStatusReason ssl_error;

  gboolean do_register;

  gboolean low_bandwidth;

  gchar *https_proxy_server;
  guint16 https_proxy_port;

  gchar *stun_server;
  guint16 stun_port;

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

  /* serial number of current advertised caps */
  guint caps_serial;

  /* gobject housekeeping */
  gboolean dispose_has_run;
};

#define GABBLE_CONNECTION_GET_PRIVATE(obj) \
    ((GabbleConnectionPrivate *)obj->priv)

static void connection_nickname_update_cb (GObject *, TpHandle, gpointer);
static void connection_capabilities_update_cb (GabblePresenceCache *,
    TpHandle, GabblePresenceCapabilities, GabblePresenceCapabilities,
    gpointer);

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

  self->presence_cache = gabble_presence_cache_new (self);
  g_signal_connect (self->presence_cache, "nickname-update", G_CALLBACK
      (connection_nickname_update_cb), self);
  g_signal_connect (self->presence_cache, "capabilities-update", G_CALLBACK
      (connection_capabilities_update_cb), self);

  capabilities_fill_cache (self->presence_cache);

  conn_avatars_init (self);
  conn_presence_init (self);
  conn_olpc_activity_properties_init (self);

  return (GObject *)self;
}

static void
gabble_connection_init (GabbleConnection *self)
{
  GabbleConnectionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CONNECTION, GabbleConnectionPrivate);

  DEBUG("Initializing (GabbleConnection *)%p", self);

  self->priv = priv;
  self->lmconn = lm_connection_new (NULL);

  /* Set default parameters for optional parameters */
  priv->resource = g_strdup (GABBLE_PARAMS_DEFAULT_RESOURCE);

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
      g_value_set_char (value, priv->priority);
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
    case PROP_STUN_SERVER:
      g_value_set_string (value, priv->stun_server);
      break;
    case PROP_STUN_PORT:
      g_value_set_uint (value, priv->stun_port);
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
      priv->priority = g_value_get_char (value);
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
    case PROP_STUN_SERVER:
      g_free (priv->stun_server);
      priv->stun_server = g_value_dup_string (value);
      break;
    case PROP_STUN_PORT:
      priv->stun_port = g_value_get_uint (value);
      break;
    default:
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

/* must be in the same order as GabbleListHandle in gabble-connection.h */
static const char *list_handle_strings[] =
{
    "publish",      /* GABBLE_LIST_HANDLE_PUBLISH */
    "subscribe",    /* GABBLE_LIST_HANDLE_SUBSCRIBE */
    "known",        /* GABBLE_LIST_HANDLE_KNOWN */
    "deny",         /* GABBLE_LIST_HANDLE_DENY */
    NULL
};

/* For the benefit of the unit tests, this will allow the connection to
 * be NULL
 */
void
_gabble_connection_create_handle_repos (TpBaseConnection *conn,
    TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES])
{
  repos[TP_HANDLE_TYPE_CONTACT] =
      tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_CONTACT,
          gabble_normalize_contact, GUINT_TO_POINTER (GABBLE_JID_ANY));
  repos[TP_HANDLE_TYPE_ROOM] =
      tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_ROOM, gabble_normalize_room,
          NULL);
  repos[TP_HANDLE_TYPE_GROUP] =
      tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_GROUP, NULL, NULL);
  repos[TP_HANDLE_TYPE_LIST] =
      tp_static_handle_repo_new (TP_HANDLE_TYPE_LIST, list_handle_strings);
}

static void
gabble_connection_class_init (GabbleConnectionClass *gabble_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_connection_class);
  TpBaseConnectionClass *parent_class = TP_BASE_CONNECTION_CLASS (
      gabble_connection_class);
  GParamSpec *param_spec;
  static const gchar *interfaces_always_present[] = {
      TP_IFACE_CONNECTION_INTERFACE_ALIASING,
      TP_IFACE_CONNECTION_INTERFACE_CAPABILITIES,
      TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
      TP_IFACE_CONNECTION_INTERFACE_AVATARS,
      NULL };

  DEBUG("Initializing (GabbleConnectionClass *)%p", gabble_connection_class);

  object_class->get_property = gabble_connection_get_property;
  object_class->set_property = gabble_connection_set_property;
  object_class->constructor = gabble_connection_constructor;

  parent_class->create_handle_repos = _gabble_connection_create_handle_repos;
  parent_class->get_unique_connection_name = gabble_connection_get_unique_name;
  parent_class->create_channel_factories =
    _gabble_connection_create_channel_factories;
  parent_class->connecting = connect_callbacks;
  parent_class->disconnected = disconnect_callbacks;
  parent_class->shut_down = connection_shut_down;
  parent_class->start_connecting = _gabble_connection_connect;
  parent_class->interfaces_always_present = interfaces_always_present;

  g_type_class_add_private (gabble_connection_class,
      sizeof (GabbleConnectionPrivate));

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
                                  G_PARAM_CONSTRUCT |
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

  param_spec = g_param_spec_char ("priority", "Jabber presence priority",
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
                                  0, G_MAXUINT16, GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT,
                                  G_PARAM_CONSTRUCT |
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
                                  0, G_MAXUINT16, GABBLE_PARAMS_DEFAULT_STUN_PORT,
                                  G_PARAM_CONSTRUCT |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_PORT, param_spec);

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

  g_hash_table_destroy (self->olpc_activities_info);
  g_hash_table_destroy (self->olpc_contacts_activities);

  /* if this is not already the case, we'll crash anyway */
  g_assert (!lm_connection_is_open (self->lmconn));

  g_assert (priv->iq_disco_cb == NULL);
  g_assert (priv->iq_unknown_cb == NULL);
  g_assert (priv->stream_error_cb == NULL);
  g_assert (priv->msg_cb == NULL);

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
  g_free (priv->stun_server);
  g_free (priv->fallback_conference_server);

  g_free (priv->alias);

  g_free (priv->auth_mac);
  g_free (priv->auth_btid);

  G_OBJECT_CLASS (gabble_connection_parent_class)->finalize (object);
}

/**
 * _gabble_connection_set_properties_from_account
 *
 * Parses an account string which may be one of the following forms:
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

static LmHandlerResult connection_iq_disco_cb (LmMessageHandler *,
    LmConnection *, LmMessage *, gpointer);
static LmHandlerResult connection_iq_unknown_cb (LmMessageHandler *,
    LmConnection *, LmMessage *, gpointer);
static LmHandlerResult connection_stream_error_cb (LmMessageHandler *,
    LmConnection *, LmMessage *, gpointer);
static LmSSLResponse connection_ssl_cb (LmSSL *, LmSSLStatus, gpointer);
static void connection_open_cb (LmConnection *, gboolean, gpointer);
static void connection_auth_cb (LmConnection *, gboolean, gpointer);
static void connection_disco_cb (GabbleDisco *, GabbleDiscoRequest *,
    const gchar *, const gchar *, LmMessageNode *, GError *, gpointer);
static void connection_disconnected_cb (LmConnection *, LmDisconnectReason,
    gpointer);


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
  g_assert (priv->msg_cb == NULL);

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

  priv->msg_cb = lm_message_handler_new (pubsub_msg_event_cb,
                                            conn, NULL);
  lm_connection_register_message_handler (conn->lmconn, priv->msg_cb,
                                          LM_MESSAGE_TYPE_MESSAGE,
                                          LM_HANDLER_PRIORITY_FIRST);
}

static void
disconnect_callbacks (TpBaseConnection *base)
{
  GabbleConnection *conn = GABBLE_CONNECTION (base);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);

  g_assert (priv->iq_disco_cb != NULL);
  g_assert (priv->iq_unknown_cb != NULL);
  g_assert (priv->stream_error_cb != NULL);
  g_assert (priv->msg_cb != NULL);

  lm_connection_unregister_message_handler (conn->lmconn, priv->iq_disco_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_disco_cb);
  priv->iq_disco_cb = NULL;

  lm_connection_unregister_message_handler (conn->lmconn, priv->iq_unknown_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_unknown_cb);
  priv->iq_unknown_cb = NULL;

  lm_connection_unregister_message_handler (conn->lmconn,
      priv->stream_error_cb, LM_MESSAGE_TYPE_STREAM_ERROR);
  lm_message_handler_unref (priv->stream_error_cb);
  priv->stream_error_cb = NULL;

  lm_connection_unregister_message_handler (conn->lmconn, priv->msg_cb,
                                            LM_MESSAGE_TYPE_MESSAGE);
  lm_message_handler_unref (priv->msg_cb);
  priv->msg_cb = NULL;
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
  g_free (jid);

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

GabbleConnectionAliasSource
_gabble_connection_get_cached_alias (GabbleConnection *conn,
                                     TpHandle handle,
                                     gchar **alias)
{
  TpBaseConnection *base = (TpBaseConnection *)conn;
  GabbleConnectionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (conn,
      GABBLE_TYPE_CONNECTION, GabbleConnectionPrivate);
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  GabbleConnectionAliasSource ret = GABBLE_CONNECTION_ALIAS_NONE;
  GabblePresence *pres;
  const gchar *tmp;
  gchar *user = NULL, *resource = NULL;

  g_return_val_if_fail (NULL != conn, GABBLE_CONNECTION_ALIAS_NONE);
  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), GABBLE_CONNECTION_ALIAS_NONE);
  g_return_val_if_fail (tp_handle_is_valid (contact_handles, handle, NULL),
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
  tmp = tp_handle_inspect (contact_handles, handle);
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
  GabbleConnectionAliasSource signal_source, current_source;
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

  current_source = _gabble_connection_get_cached_alias (conn, handle, &alias);

  g_assert (current_source != GABBLE_CONNECTION_ALIAS_NONE);

  /* if the active alias for this handle is already known and from
   * a higher priority, this signal is not interesting so we do
   * nothing */
  if (signal_source < current_source)
    {
      DEBUG ("ignoring boring alias change for handle %u, signal from %u "
          "but source %u has alias \"%s\"", handle, signal_source,
          current_source, alias);
      goto OUT;
    }

  g_value_init (&entry, GABBLE_TP_ALIAS_PAIR_TYPE);
  g_value_take_boxed (&entry, dbus_g_type_specialized_construct
      (GABBLE_TP_ALIAS_PAIR_TYPE));

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

/**
 * _gabble_connection_signal_own_presence:
 * @self: A #GabbleConnection
 * @error: pointer in which to return a GError in case of failure.
 *
 * Signal the user's stored presence to the jabber server
 *
 * Retuns: FALSE if an error occurred
 */
gboolean
_gabble_connection_signal_own_presence (GabbleConnection *self, GError **error)
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
      strlen (node) < strlen (NS_GABBLE_CAPS) + 2))
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

  DEBUG ("got disco request for bundle %s, caps are %x", node,
      self->self_presence->caps);
  features = capabilities_get_features (self->self_presence->caps);

  g_debug ("%s: caps now %u", G_STRFUNC, self->self_presence->caps);

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

  lm_message_unref (result);
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
      g_assert_not_reached ();
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

  if (!lm_connection_authenticate (conn->lmconn, priv->username,
        priv->password, priv->resource, connection_auth_cb, conn, NULL,
        &error))
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
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (conn);
  GError *error = NULL;
  const gchar *jid;

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


  jid = lm_connection_get_jid (lmconn);

  base->self_handle = tp_handle_ensure (contact_handles, jid, NULL, &error);

  if (base->self_handle == 0)
    {
      DEBUG ("couldn't get our self handle: %s", error->message);

      g_error_free (error);

      tp_base_connection_change_status ((TpBaseConnection *)conn,
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

      return;
    }

  /* update priv->resource and priv->stream_server from the server's JID */
  if (!_gabble_connection_set_properties_from_account (conn, jid, &error))
    {
      DEBUG ("couldn't parse our own JID: %s", error->message);

      g_error_free (error);

      tp_base_connection_change_status ((TpBaseConnection *)conn,
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

      return;
    }

  DEBUG ("Created self handle %d, our JID is %s", base->self_handle, jid);

  /* set initial presence */
  conn->self_presence = gabble_presence_new ();
  gabble_presence_update (conn->self_presence, priv->resource,
      GABBLE_PRESENCE_AVAILABLE, NULL, priv->priority);

  /* set initial capabilities */
  gabble_presence_set_capabilities (conn->self_presence, priv->resource,
      capabilities_get_initial_caps (), priv->caps_serial++);

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
  GError *error = NULL;

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
          if (0 == strcmp (iter->name, "identity"))
            {
              const gchar *category = lm_message_node_get_attribute (iter,
                  "category");
              const gchar *type = lm_message_node_get_attribute (iter, "type");

              if (!tp_strdiff (category, "pubsub") &&
                  !tp_strdiff (type, "pep"))
                /* XXX: should we also check for specific PubSub <feature>s? */
                conn->features |= GABBLE_CONNECTION_FEATURES_PEP;
            }
          else if (0 == strcmp (iter->name, "feature"))
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

  if (conn->features && GABBLE_CONNECTION_FEATURES_PEP)
    {
      const gchar *ifaces[] = { GABBLE_IFACE_OLPC_BUDDY_INFO,
          GABBLE_IFACE_OLPC_ACTIVITY_PROPERTIES, NULL };

      tp_base_connection_add_interfaces ((TpBaseConnection *) conn, ifaces);
    }

  /* send presence to the server to indicate availability */
  /* TODO: some way for the user to set this */
  if (!_gabble_connection_signal_own_presence (conn, &error))
    {
      DEBUG ("sending initial presence failed: %s", error->message);
      goto ERROR;
    }

  /* go go gadget on-line */
  tp_base_connection_change_status (base,
      TP_CONNECTION_STATUS_CONNECTED, TP_CONNECTION_STATUS_REASON_REQUESTED);

  return;

ERROR:
  if (error)
    g_error_free (error);

  tp_base_connection_change_status (base,
      TP_CONNECTION_STATUS_DISCONNECTED,
      TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

  return;
}


/****************************************************************************
 *                          D-BUS EXPORTED METHODS                          *
 ****************************************************************************/


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
              GABBLE_TP_CAPABILITIES_CHANGED_MONSTER_TYPE);
          g_value_take_boxed (&caps_monster_struct,
              dbus_g_type_specialized_construct
                (GABBLE_TP_CAPABILITIES_CHANGED_MONSTER_TYPE));

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
      g_boxed_free (GABBLE_TP_CAPABILITIES_CHANGED_MONSTER_TYPE,
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
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  DEBUG ("caps before: %x", pres->caps);

  for (i = 0; i < add->len; i++)
    {
      GValue iface_flags_pair = {0, };
      gchar *iface;
      guint flags;

      g_value_init (&iface_flags_pair, GABBLE_TP_CAPABILITY_PAIR_TYPE);
      g_value_set_static_boxed (&iface_flags_pair, g_ptr_array_index (add, i));

      dbus_g_type_struct_get (&iface_flags_pair,
                              0, &iface,
                              1, &flags,
                              G_MAXUINT);

      for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
          if (g_str_equal (iface, ccd->iface))
            add_caps |= ccd->tf2c_fn (flags);

      g_free (iface);
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
      gabble_presence_set_capabilities (pres, priv->resource, caps,
          priv->caps_serial++);
      DEBUG ("set caps: %x", pres->caps);
    }

  ret = g_ptr_array_new ();

  for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
    {
      if (ccd->c2tf_fn (pres->caps))
        {
          GValue iface_flags_pair = {0, };

          g_value_init (&iface_flags_pair, GABBLE_TP_CAPABILITY_PAIR_TYPE);
          g_value_take_boxed (&iface_flags_pair,
              dbus_g_type_specialized_construct (GABBLE_TP_CAPABILITY_PAIR_TYPE));

          dbus_g_type_struct_set (&iface_flags_pair,
                                  0, ccd->iface,
                                  1, ccd->c2tf_fn (pres->caps),
                                  G_MAXUINT);

          g_ptr_array_add (ret, g_value_get_boxed (&iface_flags_pair));
        }
    }

  if (caps ^ save_caps)
    {
      if (!_gabble_connection_signal_own_presence (self, &error))
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
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  guint i;
  GPtrArray *ret;
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handles_are_valid (contact_handles, handles, TRUE, &error))
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
          /* FIXME report the magical channel types available on the
           * connection itself */
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

                g_value_init (&monster, GABBLE_TP_GET_CAPABILITIES_MONSTER_TYPE);
                g_value_take_boxed (&monster,
                    dbus_g_type_specialized_construct (
                      GABBLE_TP_GET_CAPABILITIES_MONSTER_TYPE));

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

          g_value_init (&monster, GABBLE_TP_GET_CAPABILITIES_MONSTER_TYPE);
          g_value_take_boxed (&monster,
              dbus_g_type_specialized_construct (GABBLE_TP_GET_CAPABILITIES_MONSTER_TYPE));

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
  TpBaseConnection *base = (TpBaseConnection *)(batch->conn);
  TpHandleRepoIface *room_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_ROOM);
  guint i;

  tp_handles_unref (room_handles, batch->handles);
  g_array_free (batch->handles, TRUE);
  for (i = 0; i < batch->count; i++)
    {
      g_free (batch->contexts[i].jid);
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
          gabble_disco_cancel_request (batch->conn->disco,
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
  TpHandleRepoIface *room_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_ROOM);
  RoomVerifyBatch *batch = g_slice_new (RoomVerifyBatch);
  guint i;

  batch->errored = FALSE;
  batch->conn = conn;
  batch->invocation = invocation;
  batch->count = count;
  batch->handles = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), count);
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
          GError *error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "requested room handle %s does not specify a server, but we "
              "have not discovered any local conference servers and no "
              "fallback was provided", name);
          DEBUG ("%s", error->message);
          room_verify_batch_raise_error (batch, error);
          return NULL;
        }

      batch->contexts[i].jid = qualified_name;

      /* has the handle been verified before? */
      handle = tp_handle_lookup (room_handles, qualified_name, NULL, NULL);
      if (handle)
        tp_handle_ref (room_handles, handle);
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
  TpHandleRepoIface *room_handles = tp_base_connection_get_handles (
      (TpBaseConnection *)batch->conn, TP_HANDLE_TYPE_ROOM);
  gchar *sender;
  GError *error = NULL;

  for (i = 0; i < batch->count; i++)
    {
      if (!g_array_index (batch->handles, TpHandle, i))
        {
          /* we're not ready yet */
          return FALSE;
        }
    }

  sender = dbus_g_method_get_sender (batch->invocation);
  if (!tp_handles_client_hold (room_handles, sender, batch->handles, &error))
    {
      g_assert (error != NULL);
    }
  g_free (sender);

  if (error == NULL)
    {
      tp_svc_connection_return_from_request_handles (batch->invocation,
          batch->handles);
    }
  else
    {
      dbus_g_method_return_error (batch->invocation, error);
      g_error_free (error);
    }

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
  TpHandleRepoIface *room_handles = tp_base_connection_get_handles (
      (TpBaseConnection *)batch->conn, TP_HANDLE_TYPE_ROOM);
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

      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "specified server doesn't support MUC");

      room_verify_batch_raise_error (batch, error);

      return;
    }

  /* this refs the handle, so we're putting a ref in batch->handles */
  handle = tp_handle_ensure (room_handles, rvctx->jid, NULL, &error);
  if (handle == 0)
    {
      room_verify_batch_raise_error (batch, error);
    }

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
  GError *error = NULL;

  room = service = NULL;
  gabble_decode_jid (batch->contexts[index].jid, &room, &service, NULL);

  if (room == NULL || *room == '\0' || service == NULL || *service == '\0')
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "unable to get room name and service from JID %s",
          batch->contexts[index].jid);

      ret = FALSE;

      goto out;
    }

  ret = (gabble_disco_request (batch->conn->disco, GABBLE_DISCO_TYPE_INFO,
                               service, NULL, room_jid_disco_cb,
                               batch->contexts + index,
                               G_OBJECT (batch->conn), &error) != NULL);

out:
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

  g_assert (GABBLE_IS_CONNECTION (self));

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (handle_type == TP_HANDLE_TYPE_ROOM)
    {
      RoomVerifyBatch *batch = NULL;
      guint count = 0, i;
      const gchar **cur_name;

      for (cur_name = names; *cur_name != NULL; cur_name++)
        {
          count++;
        }

      batch = room_verify_batch_new (self, context, count, names);
      if (!batch)
        {
          /* an error occurred while setting up the batch, and we returned
          error to dbus */
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
      return;
    }

  /* else it's either an invalid type, or a type we can verify immediately -
   * in either case, let the superclass do it */
  tp_base_connection_dbus_request_handles (iface, handle_type, names, context);
}


/* We reimplement RequestHandles to be able to do async validation on
 * room handles */
static void
conn_service_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionClass *klass = (TpSvcConnectionClass *)g_iface;

#define IMPLEMENT(x) tp_svc_connection_implement_##x (klass, \
    gabble_connection_##x)
  IMPLEMENT(request_handles);
#undef IMPLEMENT
}

static void
capabilities_service_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceCapabilitiesClass *klass =
    (TpSvcConnectionInterfaceCapabilitiesClass *)g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_capabilities_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(advertise_capabilities);
  IMPLEMENT(get_capabilities);
#undef IMPLEMENT
}

