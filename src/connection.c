/*
 * gabble-connection.c - Source for GabbleConnection
 * Copyright (C) 2005-2010 Collabora Ltd.
 * Copyright (C) 2005-2010 Nokia Corporation
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
#include "connection.h"
#include "gabble.h"

#include <string.h>

#define DBUS_API_SUBJECT_TO_CHANGE

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <glib-object.h>
#include <loudmouth/loudmouth.h>
#include <wocky/wocky-connector.h>
#include <wocky/wocky-xmpp-error.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-generic.h>

#include "extensions/extensions.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "bytestream-factory.h"
#include "capabilities.h"
#include "caps-channel-manager.h"
#include "caps-hash.h"
#include "conn-aliasing.h"
#include "conn-avatars.h"
#include "conn-contact-info.h"
#include "conn-location.h"
#include "conn-presence.h"
#include "conn-sidecars.h"
#include "conn-mail-notif.h"
#include "conn-olpc.h"
#include "debug.h"
#include "disco.h"
#include "media-channel.h"
#include "im-factory.h"
#include "jingle-factory.h"
#include "media-factory.h"
#include "muc-factory.h"
#include "namespaces.h"
#include "olpc-gadget-manager.h"
#include "presence-cache.h"
#include "presence.h"
#include "request-pipeline.h"
#include "roomlist-manager.h"
#include "roster.h"
#include "search-manager.h"
#include "private-tubes-factory.h"
#include "util.h"
#include "vcard-manager.h"

static guint disco_reply_timeout = 5;

#define DISCONNECT_TIMEOUT 5

static void capabilities_service_iface_init (gpointer, gpointer);
static void gabble_conn_contact_caps_iface_init (gpointer, gpointer);
static void conn_capabilities_fill_contact_attributes (GObject *obj,
  const GArray *contacts, GHashTable *attributes_hash);
static void conn_contact_capabilities_fill_contact_attributes (GObject *obj,
  const GArray *contacts, GHashTable *attributes_hash);

G_DEFINE_TYPE_WITH_CODE(GabbleConnection,
    gabble_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
      conn_aliasing_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS,
      conn_avatars_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_INFO,
      conn_contact_info_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CAPABILITIES,
      capabilities_service_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
       tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACTS,
      tp_contacts_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
      tp_presence_mixin_simple_presence_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE,
      conn_presence_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CONNECTION_INTERFACE_GABBLE_DECLOAK,
      conn_decloak_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_LOCATION,
      location_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_OLPC_BUDDY_INFO,
      olpc_buddy_info_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_OLPC_ACTIVITY_PROPERTIES,
      olpc_activity_properties_iface_init);
    G_IMPLEMENT_INTERFACE
      (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_CAPABILITIES,
      gabble_conn_contact_caps_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_OLPC_GADGET,
      olpc_gadget_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CONNECTION_FUTURE,
      conn_future_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CONNECTION_INTERFACE_MAIL_NOTIFICATION,
      conn_mail_notif_iface_init);
    )

/* properties */
enum
{
    PROP_CONNECT_SERVER = 1,
    PROP_PORT,
    PROP_OLD_SSL,
    PROP_REQUIRE_ENCRYPTION,
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
    PROP_FALLBACK_STUN_SERVER,
    PROP_FALLBACK_STUN_PORT,
    PROP_IGNORE_SSL_ERRORS,
    PROP_ALIAS,
    PROP_FALLBACK_SOCKS5_PROXIES,
    PROP_KEEPALIVE_INTERVAL,
    PROP_DECLOAK_AUTOMATICALLY,

    LAST_PROPERTY
};

/* private structure */

struct _GabbleConnectionPrivate
{
  WockyConnector *connector;
  WockyPorter *porter;

  LmMessageHandler *iq_disco_cb;
  LmMessageHandler *iq_unknown_cb;
  LmMessageHandler *olpc_msg_cb;
  LmMessageHandler *olpc_presence_cb;

  /* connection properties */
  gchar *connect_server;
  guint port;
  gboolean old_ssl;
  gboolean require_encryption;

  gboolean ignore_ssl_errors;
  TpConnectionStatusReason ssl_error;

  gboolean do_register;

  gboolean low_bandwidth;

  guint keepalive_interval;

  gchar *https_proxy_server;
  guint16 https_proxy_port;

  gchar *stun_server;
  guint16 stun_port;

  gchar *fallback_stun_server;
  guint16 fallback_stun_port;

  gchar *fallback_conference_server;

  GStrv fallback_socks5_proxies;

  gboolean decloak_automatically;

  /* authentication properties */
  gchar *stream_server;
  gchar *username;
  gchar *password;
  gchar *resource;
  gint8 priority;
  gchar *alias;

  /* reference to conference server name */
  const gchar *conference_server;

  /* serial number of current advertised caps */
  guint caps_serial;

  /* capabilities from various sources: */
  /* subscriptions on behalf of the Connection, like PEP "+notify"
   * namespaces (this one is add-only) */
  GabbleCapabilitySet *notify_caps;
  /* caps provided by Capabilities.AdvertiseCapabilities (tp-spec 0.16) */
  GabbleCapabilitySet *legacy_caps;
  /* additional caps that we advertise until the first call to
   * AdvertiseCapabilities or UpdateCapabilities, for vague historical
   * reasons */
  GabbleCapabilitySet *bonus_caps;
  /* caps provided via ContactCapabilities.UpdateCapabilities ()
   * gchar * (client name) => GabbleCapabilitySet * */
  GHashTable *client_caps;
  /* the union of the above */
  GabbleCapabilitySet *all_caps;

  /* stream id returned by the connector */
  gchar *stream_id;

  /* timer used when trying to properly disconnect */
  guint disconnect_timer;

  gboolean closing;
  /* gobject housekeeping */
  gboolean dispose_has_run;
};

static void connection_capabilities_update_cb (GabblePresenceCache *cache,
    TpHandle handle,
    const GabbleCapabilitySet *old_cap_set,
    const GabbleCapabilitySet *new_cap_set,
    gpointer user_data);

static gboolean gabble_connection_refresh_capabilities (GabbleConnection *self,
    GabbleCapabilitySet **old_out);

static GPtrArray *
_gabble_connection_create_channel_managers (TpBaseConnection *conn)
{
  GabbleConnection *self = GABBLE_CONNECTION (conn);
  GPtrArray *channel_managers = g_ptr_array_sized_new (5);

  self->roster = gabble_roster_new (self);
  g_signal_connect (self->roster, "nickname-update", G_CALLBACK
      (gabble_conn_aliasing_nickname_updated), self);
  g_ptr_array_add (channel_managers, self->roster);

  g_ptr_array_add (channel_managers,
      g_object_new (GABBLE_TYPE_IM_FACTORY,
        "connection", self,
        NULL));

  g_ptr_array_add (channel_managers,
      g_object_new (GABBLE_TYPE_ROOMLIST_MANAGER,
        "connection", self,
        NULL));

  g_ptr_array_add (channel_managers,
      g_object_new (GABBLE_TYPE_SEARCH_MANAGER,
        "connection", self,
        NULL));

  self->muc_factory = g_object_new (GABBLE_TYPE_MUC_FACTORY,
      "connection", self,
      NULL);
  g_ptr_array_add (channel_managers, self->muc_factory);

  self->private_tubes_factory = gabble_private_tubes_factory_new (self);
  g_ptr_array_add (channel_managers, self->private_tubes_factory);

  self->jingle_factory = g_object_new (GABBLE_TYPE_JINGLE_FACTORY,
    "connection", self, NULL);

  g_ptr_array_add (channel_managers,
      g_object_new (GABBLE_TYPE_MEDIA_FACTORY,
        "connection", self,
        NULL));

  self->olpc_gadget_manager = g_object_new (GABBLE_TYPE_OLPC_GADGET_MANAGER,
      "connection", self,
      NULL);
  g_ptr_array_add (channel_managers, self->olpc_gadget_manager);

  self->ft_manager = gabble_ft_manager_new (self);
  g_ptr_array_add (channel_managers, self->ft_manager);

  return channel_managers;
}

static GObject *
gabble_connection_constructor (GType type,
                               guint n_construct_properties,
                               GObjectConstructParam *construct_params)
{
  GabbleConnection *self = GABBLE_CONNECTION (
      G_OBJECT_CLASS (gabble_connection_parent_class)->constructor (
        type, n_construct_properties, construct_params));
  GabbleConnectionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CONNECTION, GabbleConnectionPrivate);

  DEBUG("Post-construction: (GabbleConnection *)%p", self);

  self->req_pipeline = gabble_request_pipeline_new (self);
  self->disco = gabble_disco_new (self);
  self->vcard_manager = gabble_vcard_manager_new (self);
  g_signal_connect (self->vcard_manager, "nickname-update", G_CALLBACK
      (gabble_conn_aliasing_nickname_updated), self);

  self->presence_cache = gabble_presence_cache_new (self);
  g_signal_connect (self->presence_cache, "nickname-update", G_CALLBACK
      (gabble_conn_aliasing_nickname_updated), self);
  g_signal_connect (self->presence_cache, "capabilities-update", G_CALLBACK
      (connection_capabilities_update_cb), self);

  capabilities_fill_cache (self->presence_cache);

  tp_contacts_mixin_init (G_OBJECT (self),
      G_STRUCT_OFFSET (GabbleConnection, contacts));

  tp_base_connection_register_with_contacts_mixin (TP_BASE_CONNECTION (self));

  conn_aliasing_init (self);
  conn_avatars_init (self);
  conn_contact_info_init (self);
  conn_presence_init (self);
  conn_olpc_activity_properties_init (self);
  conn_location_init (self);
  conn_sidecars_init (self);
  conn_mail_notif_init (self);

  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (self),
      TP_IFACE_CONNECTION_INTERFACE_CAPABILITIES,
          conn_capabilities_fill_contact_attributes);

  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (self),
      TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES,
          conn_contact_capabilities_fill_contact_attributes);

  self->bytestream_factory = gabble_bytestream_factory_new (self);

  self->avatar_requests = g_hash_table_new (NULL, NULL);
  self->vcard_requests = g_hash_table_new (NULL, NULL);

  if (priv->fallback_socks5_proxies == NULL)
    {
      /* No proxies have been defined, set the default ones */
      gchar *default_socks5_proxies[] = GABBLE_PARAMS_DEFAULT_SOCKS5_PROXIES;

      g_object_set (self, "fallback-socks5-proxies", default_socks5_proxies,
          NULL);
    }

  priv->all_caps = gabble_capability_set_new ();
  priv->notify_caps = gabble_capability_set_new ();
  priv->legacy_caps = gabble_capability_set_new ();
  priv->client_caps = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) gabble_capability_set_free);

  /* Historically, the optional Jingle transports were in our initial
   * presence, but could be removed by AdvertiseCapabilities(). Emulate
   * that here for now. */
  priv->bonus_caps = gabble_capability_set_new ();
  gabble_capability_set_add (priv->bonus_caps, NS_GOOGLE_TRANSPORT_P2P);
  gabble_capability_set_add (priv->bonus_caps, NS_JINGLE_TRANSPORT_ICEUDP);

  return (GObject *) self;
}

static gchar *
dup_default_resource (void)
{
  /* This is a once-per-process leak. */
  static gchar *default_resource = NULL;

  if (G_UNLIKELY (default_resource == NULL))
    {
      char *local_machine_id = dbus_get_local_machine_id ();

      if (local_machine_id == NULL)
        g_error ("Out of memory getting local machine ID");

      default_resource = sha1_hex (local_machine_id, strlen (local_machine_id));
      /* Let's keep the resource a maneagable length. */
      default_resource[8] = '\0';

      dbus_free (local_machine_id);
    }

  return g_strdup (default_resource);
}

static void
gabble_connection_constructed (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = self->priv;
  void (*chain_up)(GObject *) =
      G_OBJECT_CLASS (gabble_connection_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

  if (priv->resource == NULL)
    {
      priv->resource = dup_default_resource ();
      DEBUG ("defaulted resource to %s", priv->resource);
    }

  /* set initial presence */
  self->self_presence = gabble_presence_new ();
  g_assert (priv->resource);
  gabble_presence_update (self->self_presence, priv->resource,
      GABBLE_PRESENCE_AVAILABLE, NULL, priv->priority);
}

static void
gabble_connection_init (GabbleConnection *self)
{
  GError *error = NULL;
  GabbleConnectionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CONNECTION, GabbleConnectionPrivate);

  DEBUG("Initializing (GabbleConnection *)%p", self);

  self->daemon = tp_dbus_daemon_dup (&error);

  if (self->daemon == NULL)
    {
      g_error ("Failed to connect to dbus daemon: %s", error->message);
    }

  self->priv = priv;
  self->lmconn = lm_connection_new ();

  priv->caps_serial = 1;
  priv->port = 5222;

  gabble_capabilities_init (self);
}

static void
gabble_connection_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GabbleConnection *self = (GabbleConnection *) object;
  GabbleConnectionPrivate *priv = self->priv;

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
    case PROP_REQUIRE_ENCRYPTION:
      g_value_set_boolean (value, priv->require_encryption);
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
    case PROP_STUN_SERVER:
      g_value_set_string (value, priv->stun_server);
      break;
    case PROP_STUN_PORT:
      g_value_set_uint (value, priv->stun_port);
      break;
    case PROP_FALLBACK_STUN_SERVER:
      g_value_set_string (value, priv->fallback_stun_server);
      break;
    case PROP_FALLBACK_STUN_PORT:
      g_value_set_uint (value, priv->fallback_stun_port);
      break;
    case PROP_FALLBACK_SOCKS5_PROXIES:
      g_value_set_boxed (value, priv->fallback_socks5_proxies);
      break;
    case PROP_KEEPALIVE_INTERVAL:
      g_value_set_uint (value, priv->keepalive_interval);
      break;

    case PROP_DECLOAK_AUTOMATICALLY:
      g_value_set_boolean (value, priv->decloak_automatically);
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
  GabbleConnectionPrivate *priv = self->priv;

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
    case PROP_REQUIRE_ENCRYPTION:
      priv->require_encryption = g_value_get_boolean (value);
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
      if (tp_strdiff (priv->resource, g_value_get_string (value)))
        {
          gchar *old_resource = priv->resource;
          gchar *new_resource = g_value_dup_string (value);

          priv->resource = new_resource;

          /* Add self presence for new resource... */
          gabble_presence_update (self->self_presence, new_resource,
              self->self_presence->status, self->self_presence->status_message,
              priv->priority);
          /* ...and remove it for the old one. */
          gabble_presence_update (self->self_presence, old_resource,
              GABBLE_PRESENCE_OFFLINE, NULL, 0);

          g_free (old_resource);
        }
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
    case PROP_STUN_SERVER:
      g_free (priv->stun_server);
      priv->stun_server = g_value_dup_string (value);
      break;
    case PROP_STUN_PORT:
      priv->stun_port = g_value_get_uint (value);
      break;
    case PROP_FALLBACK_STUN_SERVER:
      g_free (priv->fallback_stun_server);
      priv->fallback_stun_server = g_value_dup_string (value);
      break;
    case PROP_FALLBACK_STUN_PORT:
      priv->fallback_stun_port = g_value_get_uint (value);
      break;
    case PROP_FALLBACK_SOCKS5_PROXIES:
      if (priv->fallback_socks5_proxies != NULL)
        g_strfreev (priv->fallback_socks5_proxies);
      priv->fallback_socks5_proxies = g_value_dup_boxed (value);
      break;
    case PROP_KEEPALIVE_INTERVAL:
      priv->keepalive_interval = g_value_get_uint (value);
      break;

    case PROP_DECLOAK_AUTOMATICALLY:
      priv->decloak_automatically = g_value_get_boolean (value);
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
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION (self)->priv;

  return gabble_encode_jid (
      priv->username, priv->stream_server, priv->resource);
}

/* must be in the same order as GabbleListHandle in connection.h */
static const char *list_handle_strings[] =
{
    "publish",      /* GABBLE_LIST_HANDLE_PUBLISH */
    "subscribe",    /* GABBLE_LIST_HANDLE_SUBSCRIBE */
    "stored",       /* GABBLE_LIST_HANDLE_STORED */
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
          conn);
  repos[TP_HANDLE_TYPE_GROUP] =
      tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_GROUP, NULL, NULL);
  repos[TP_HANDLE_TYPE_LIST] =
      tp_static_handle_repo_new (TP_HANDLE_TYPE_LIST, list_handle_strings);
}

static void
base_connected_cb (TpBaseConnection *base_conn)
{
  GabbleConnection *conn = GABBLE_CONNECTION (base_conn);

  gabble_connection_connected_olpc (conn);
}

#define TWICE(x) (x), (x)

static void
gabble_connection_class_init (GabbleConnectionClass *gabble_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_connection_class);
  TpBaseConnectionClass *parent_class = TP_BASE_CONNECTION_CLASS (
      gabble_connection_class);
  static const gchar *interfaces_always_present[] = {
      TP_IFACE_CONNECTION_INTERFACE_ALIASING,
      TP_IFACE_CONNECTION_INTERFACE_CAPABILITIES,
      TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
      TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
      TP_IFACE_CONNECTION_INTERFACE_AVATARS,
      GABBLE_IFACE_CONNECTION_INTERFACE_CONTACT_INFO,
      TP_IFACE_CONNECTION_INTERFACE_CONTACTS,
      TP_IFACE_CONNECTION_INTERFACE_REQUESTS,
      GABBLE_IFACE_OLPC_GADGET,
      TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES,
      TP_IFACE_CONNECTION_INTERFACE_LOCATION,
      GABBLE_IFACE_CONNECTION_INTERFACE_GABBLE_DECLOAK,
      NULL };
  static TpDBusPropertiesMixinPropImpl olpc_gadget_props[] = {
        { "GadgetAvailable", NULL, NULL },
        { NULL }
  };
  static TpDBusPropertiesMixinPropImpl location_props[] = {
        { "LocationAccessControlTypes", NULL, NULL },
        { "LocationAccessControl", NULL, NULL },
        { NULL }
  };
  static TpDBusPropertiesMixinPropImpl decloak_props[] = {
        { "DecloakAutomatically", TWICE ("decloak-automatically") },
        { NULL }
  };
  static TpDBusPropertiesMixinPropImpl mail_notif_props[] = {
        { "MailNotificationFlags", NULL, NULL },
        { "UnreadMailCount", NULL, NULL },
        { "UnreadMails", NULL, NULL },
        { "MailAddress", NULL, NULL },
        { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
        /* 0 */ { GABBLE_IFACE_OLPC_GADGET,
          conn_olpc_gadget_properties_getter,
          NULL,
          olpc_gadget_props,
        },
        /* 1 */ { TP_IFACE_CONNECTION_INTERFACE_LOCATION,
          conn_location_properties_getter,
          conn_location_properties_setter,
          location_props,
        },
        /* 2 */ { TP_IFACE_CONNECTION_INTERFACE_AVATARS,
          conn_avatars_properties_getter,
          NULL,
          NULL,
        },
        /* 3 */ { GABBLE_IFACE_CONNECTION_INTERFACE_CONTACT_INFO,
          conn_contact_info_properties_getter,
          NULL,
          NULL,
        },
        /* 4 */ { GABBLE_IFACE_CONNECTION_INTERFACE_GABBLE_DECLOAK,
          tp_dbus_properties_mixin_getter_gobject_properties,
          tp_dbus_properties_mixin_setter_gobject_properties,
          decloak_props,
        },
        { GABBLE_IFACE_CONNECTION_INTERFACE_MAIL_NOTIFICATION,
          conn_mail_notif_properties_getter,
          NULL,
          mail_notif_props,
        },
        { NULL }
  };

  prop_interfaces[2].props = conn_avatars_properties;
  prop_interfaces[3].props = conn_contact_info_properties;

  DEBUG("Initializing (GabbleConnectionClass *)%p", gabble_connection_class);

  object_class->get_property = gabble_connection_get_property;
  object_class->set_property = gabble_connection_set_property;
  object_class->constructor = gabble_connection_constructor;
  object_class->constructed = gabble_connection_constructed;

  parent_class->create_handle_repos = _gabble_connection_create_handle_repos;
  parent_class->get_unique_connection_name = gabble_connection_get_unique_name;
  parent_class->create_channel_factories = NULL;
  parent_class->create_channel_managers =
    _gabble_connection_create_channel_managers;
  parent_class->connecting = connect_callbacks;
  parent_class->connected = base_connected_cb;
  parent_class->disconnected = disconnect_callbacks;
  parent_class->shut_down = connection_shut_down;
  parent_class->start_connecting = _gabble_connection_connect;
  parent_class->interfaces_always_present = interfaces_always_present;

  g_type_class_add_private (gabble_connection_class,
      sizeof (GabbleConnectionPrivate));

  object_class->dispose = gabble_connection_dispose;
  object_class->finalize = gabble_connection_finalize;

  g_object_class_install_property (object_class, PROP_CONNECT_SERVER,
      g_param_spec_string (
          "connect-server", "Hostname or IP of Jabber server",
          "The server used when establishing a connection.",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PORT,
      g_param_spec_uint (
          "port", "Jabber server port",
          "The port used when establishing a connection.",
          1, G_MAXUINT16, 5222,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_OLD_SSL,
      g_param_spec_boolean (
          "old-ssl", "Old-style SSL tunneled connection",
          "Establish the entire connection to the server within an "
          "SSL-encrypted tunnel. Note that this is not the same as connecting "
          "with TLS, which is not yet supported.",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (
      object_class, PROP_REQUIRE_ENCRYPTION,
      g_param_spec_boolean (
          "require-encryption", "Require encryption",
          "Require the connection to be encrypted, either via old-style SSL, "
          "or StartTLS mechanisms.",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_REGISTER,
      g_param_spec_boolean (
          "register", "Register account on server",
          "Register a new account on server.",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_LOW_BANDWIDTH,
      g_param_spec_boolean (
          "low-bandwidth", "Low bandwidth mode",
          "Determines whether we are in low bandwidth mode. This influences "
          "polling behaviour.",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_STREAM_SERVER,
      g_param_spec_string (
          "stream-server", "The server name used to initialise the stream.",
          "The server name used when initialising the stream, which is "
          "usually the part after the @ in the user's JID.",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_USERNAME,
      g_param_spec_string (
          "username", "Jabber username",
          "The username used when authenticating.",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PASSWORD,
      g_param_spec_string (
          "password", "Jabber password",
          "The password used when authenticating.",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_RESOURCE,
      g_param_spec_string (
          "resource", "Jabber resource",
          "The Jabber resource used when authenticating.",
          "Telepathy",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_char (
          "priority", "Jabber presence priority",
          "The default priority used when reporting our presence.",
          G_MININT8, G_MAXINT8, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_HTTPS_PROXY_SERVER,
      g_param_spec_string (
          "https-proxy-server",
          "The server name used as an HTTPS proxy server",
          "The server name used as an HTTPS proxy server.",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_HTTPS_PROXY_PORT,
      g_param_spec_uint (
          "https-proxy-port", "The HTTP proxy server port",
          "The HTTP proxy server port.",
          0, G_MAXUINT16, GABBLE_PARAMS_DEFAULT_HTTPS_PROXY_PORT,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_FALLBACK_CONFERENCE_SERVER, g_param_spec_string (
          "fallback-conference-server",
          "The conference server used as fallback",
          "The conference server used as fallback when everything else fails.",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_STUN_SERVER,
      g_param_spec_string (
          "stun-server", "STUN server",
          "STUN server.",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_STUN_PORT,
      g_param_spec_uint (
          "stun-port", "STUN port",
          "STUN port.",
          0, G_MAXUINT16, GABBLE_PARAMS_DEFAULT_STUN_PORT,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FALLBACK_STUN_SERVER,
      g_param_spec_string (
          "fallback-stun-server", "fallback STUN server",
          "Fallback STUN server.",
          GABBLE_PARAMS_DEFAULT_FALLBACK_STUN_SERVER,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FALLBACK_STUN_PORT,
      g_param_spec_uint (
          "fallback-stun-port", "fallback STUN port",
          "Fallback STUN port.",
          0, G_MAXUINT16, GABBLE_PARAMS_DEFAULT_STUN_PORT,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_IGNORE_SSL_ERRORS,
      g_param_spec_boolean (
          "ignore-ssl-errors", "Ignore SSL errors",
          "Continue connecting even if the server's "
          "SSL certificate is invalid or missing.",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ALIAS,
      g_param_spec_string (
          "alias", "Alias/nick for local user",
          "Alias/nick for local user",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FALLBACK_SOCKS5_PROXIES,
      g_param_spec_boxed (
          "fallback-socks5-proxies", "fallback SOCKS5 proxies",
          "Fallback SOCKS5 proxies.",
          G_TYPE_STRV,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_KEEPALIVE_INTERVAL,
      g_param_spec_uint (
          "keepalive-interval", "keepalive interval",
          "Seconds between keepalive packets, or 0 to disable",
          0, G_MAXUINT, 30,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (
      object_class, PROP_DECLOAK_AUTOMATICALLY,
      g_param_spec_boolean (
          "decloak-automatically", "Decloak automatically?",
          "Leak presence and capabilities when requested",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gabble_connection_class->properties_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleConnectionClass, properties_class));

  tp_contacts_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleConnectionClass, contacts_class));

  conn_presence_class_init (gabble_connection_class);

  conn_contact_info_class_init (gabble_connection_class);
}

static void
gabble_connection_dispose (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  TpBaseConnection *base = (TpBaseConnection *) self;
  GabbleConnectionPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  DEBUG ("called");

  g_assert ((base->status == TP_CONNECTION_STATUS_DISCONNECTED) ||
            (base->status == TP_INTERNAL_CONNECTION_STATUS_NEW));

  g_object_unref (self->bytestream_factory);
  self->bytestream_factory = NULL;

  g_object_unref (self->disco);
  self->disco = NULL;

  g_object_unref (self->req_pipeline);
  self->req_pipeline = NULL;

  g_object_unref (self->vcard_manager);
  self->vcard_manager = NULL;

  g_object_unref (self->jingle_factory);
  self->jingle_factory = NULL;

  /* remove borrowed references before TpBaseConnection unrefs the channel
   * factories */
  self->roster = NULL;
  self->muc_factory = NULL;
  self->private_tubes_factory = NULL;

  if (self->self_presence != NULL)
    g_object_unref (self->self_presence);
  self->self_presence = NULL;

  g_object_unref (self->presence_cache);
  self->presence_cache = NULL;

  conn_olpc_activity_properties_dispose (self);

  g_hash_table_destroy (self->avatar_requests);
  g_hash_table_destroy (self->vcard_requests);

  conn_mail_notif_dispose (self);

  g_assert (priv->iq_disco_cb == NULL);
  g_assert (priv->iq_unknown_cb == NULL);
  g_assert (priv->olpc_msg_cb == NULL);
  g_assert (priv->olpc_presence_cb == NULL);

  if (priv->connector != NULL)
    {
      g_object_unref (priv->connector);
      priv->connector = NULL;
    }

  if (self->session != NULL)
    {
      g_object_unref (self->session);
      self->session = NULL;
    }

  if (self->lmconn != NULL)
    {
      lm_connection_unref (self->lmconn);
      self->lmconn = NULL;
    }

  g_hash_table_destroy (priv->client_caps);
  gabble_capability_set_free (priv->all_caps);
  gabble_capability_set_free (priv->notify_caps);
  gabble_capability_set_free (priv->legacy_caps);
  gabble_capability_set_free (priv->bonus_caps);

  if (priv->disconnect_timer != 0)
    {
      g_source_remove (priv->disconnect_timer);
      priv->disconnect_timer = 0;
    }

  if (self->pep_location != NULL)
    {
      g_object_unref (self->pep_location);
      self->pep_location = NULL;
    }

  if (self->pep_nick != NULL)
    {
      g_object_unref (self->pep_nick);
      self->pep_nick = NULL;
    }

  if (self->pep_olpc_buddy_props != NULL)
    {
      g_object_unref (self->pep_olpc_buddy_props);
      self->pep_olpc_buddy_props = NULL;
    }

  if (self->pep_olpc_activities != NULL)
    {
      g_object_unref (self->pep_olpc_activities);
      self->pep_olpc_activities = NULL;
    }

  if (self->pep_olpc_current_act != NULL)
    {
      g_object_unref (self->pep_olpc_current_act);
      self->pep_olpc_current_act = NULL;
    }

  if (self->pep_olpc_act_props != NULL)
    {
      g_object_unref (self->pep_olpc_act_props);
      self->pep_olpc_act_props = NULL;
    }

  conn_sidecars_dispose (self);

  if (self->daemon != NULL)
    {
      g_object_unref (self->daemon);
      self->daemon = NULL;
    }

  if (G_OBJECT_CLASS (gabble_connection_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_connection_parent_class)->dispose (object);
}

static void
gabble_connection_finalize (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = self->priv;

  DEBUG ("called with %p", object);

  g_free (priv->connect_server);
  g_free (priv->stream_server);
  g_free (priv->username);
  g_free (priv->password);
  g_free (priv->resource);

  g_free (priv->https_proxy_server);
  g_free (priv->stun_server);
  g_free (priv->fallback_stun_server);
  g_free (priv->fallback_conference_server);
  g_strfreev (priv->fallback_socks5_proxies);

  g_free (priv->alias);
  g_free (priv->stream_id);

  tp_contacts_mixin_finalize (G_OBJECT(self));

  conn_presence_finalize (self);
  conn_contact_info_finalize (self);

  gabble_capabilities_finalize (self);

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

  priv = conn->priv;

  username = server = resource = NULL;
  result = TRUE;

  if (!gabble_decode_jid (account, &username, &server, &resource) ||
      username == NULL)
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
 * gabble_connection_get_full_jid:
 *
 * Returns: the full jid (including resource) of this connection, which must be
 *          freed by the caller.
 */
gchar *
gabble_connection_get_full_jid (GabbleConnection *conn)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (conn);
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  TpHandle self = tp_base_connection_get_self_handle (base);
  const gchar *bare_jid = tp_handle_inspect (contact_handles, self);

  return g_strconcat (bare_jid, "/", conn->priv->resource, NULL);
}

/**
 * gabble_connection_get_porter:
 *
 * Returns: the #WockyPorter instance driving this connection.
 */

WockyPorter *gabble_connection_get_porter (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = conn->priv;

  if (priv->porter != NULL)
    return g_object_ref (priv->porter);

  return NULL;
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

  if (conn->lmconn == NULL)
    {
      g_set_error_literal (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "connection is disconnected");
      return FALSE;
    }

  priv = conn->priv;

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

  if (handler_data->object_alive && handler_data->reply_func != NULL)
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
 *
 * if reply_func is NULL the reply will be ignored but connection_iq_unknown_cb
 * won't be called.
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

  if (conn->lmconn == NULL)
    {
      g_set_error_literal (error, TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "connection is disconnected");
      return FALSE;
    }

  priv = conn->priv;

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
static void connection_disco_cb (GabbleDisco *, GabbleDiscoRequest *,
    const gchar *, const gchar *, LmMessageNode *, GError *, gpointer);

static void
remote_closed_cb (WockyPorter *porter,
    GabbleConnection *self)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (self);

  if (base->status == TP_CONNECTION_STATUS_DISCONNECTED)
    /* Ignore if we are already disconnecting/disconnected */
    return;

  DEBUG ("server closed its XMPP stream; close ours");

  /* Changing the state to Disconnect will call connection_shut_down which
   * will properly close the porter. */
  tp_base_connection_change_status ((TpBaseConnection *) self,
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
}

static void
force_close_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (user_data);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  GError *error = NULL;

  if (!wocky_porter_force_close_finish (WOCKY_PORTER (source), res, &error))
    {
      DEBUG ("force close failed: %s", error->message);
      g_error_free (error);
    }
  else
    {
      DEBUG ("connection properly closed (forced)");
    }

  tp_base_connection_finish_shutdown (base);
}

static void
remote_error_cb (WockyPorter *porter,
    GQuark domain,
    gint code,
    gchar *msg,
    GabbleConnection *self)
{
  TpConnectionStatusReason reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;
  TpBaseConnection *base = (TpBaseConnection *) self;
  GabbleConnectionPrivate *priv = self->priv;

  if (base->status == TP_CONNECTION_STATUS_DISCONNECTED)
    /* Ignore if we are already disconnecting/disconnected */
    return;

  if (domain == WOCKY_XMPP_STREAM_ERROR)
    {
      /* stream error */
      DEBUG ("Received stream error (%u): %s", code, msg);

      if (code == WOCKY_XMPP_STREAM_ERROR_CONFLICT)
        {
          /* Another client with the same resource just appeared, we're going
           * down. */
          DEBUG ("Another client appeared with the same resource");
          reason = TP_CONNECTION_STATUS_REASON_NAME_IN_USE;
        }
    }
  else
    {
      DEBUG ("remote error: %s", msg);
    }

  DEBUG ("Force closing of the connection %p", self);
  priv->closing = TRUE;
  wocky_porter_force_close_async (priv->porter, NULL, force_close_cb,
      self);

  tp_base_connection_change_status ((TpBaseConnection *) self,
      TP_CONNECTION_STATUS_DISCONNECTED, reason);
}

static void
connector_error_disconnect (GabbleConnection *self,
    GError *error)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpConnectionStatusReason reason = \
    TP_CONNECTION_STATUS_REASON_NETWORK_ERROR;

  DEBUG ("connection failed: %s", error->message);

  if (error->domain == WOCKY_CONNECTOR_ERROR)
    {
      /* Connector error */
      switch (error->code)
        {
          case WOCKY_CONNECTOR_ERROR_SESSION_DENIED:
            reason = TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED;
            break;

          case WOCKY_CONNECTOR_ERROR_REGISTRATION_CONFLICT:
            DEBUG ("Registration failed; jid is already used");
            reason = TP_CONNECTION_STATUS_REASON_NAME_IN_USE;
            break;

          case WOCKY_CONNECTOR_ERROR_REGISTRATION_REJECTED:
          case WOCKY_CONNECTOR_ERROR_REGISTRATION_UNSUPPORTED:
            /* AuthenticationFailed is the closest ConnectionStatusReason to
             * "I tried but couldn't register you an account." */
            DEBUG ("Registration rejected");
            reason = TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED;
            break;

          default:
            break;
        }
    }

  else if (error->domain == WOCKY_XMPP_STREAM_ERROR)
    {
      /* Stream error */
      switch (error->code)
        {
          case WOCKY_XMPP_STREAM_ERROR_HOST_UNKNOWN:
            /* If we get this while we're logging in, it's because we're trying
             * to connect to foo@bar.com but the server doesn't know about
             * bar.com, probably because the user entered a non-GTalk JID into
             * a GTalk profile that forces the server. */
            DEBUG ("got <host-unknown> while connecting");
            reason = TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED;
            break;

          default:
            break;
        }
    }
  else if (error->domain == WOCKY_TLS_CERT_ERROR)
    {
      /* certificate error */
      switch (error->code)
        {
          case WOCKY_TLS_CERT_NO_CERTIFICATE:
            DEBUG ("The server doesn't provide a certificate.");
            reason = TP_CONNECTION_STATUS_REASON_CERT_NOT_PROVIDED;
            break;
          case WOCKY_TLS_CERT_INSECURE:
          case WOCKY_TLS_CERT_SIGNER_UNKNOWN:
          case WOCKY_TLS_CERT_SIGNER_UNAUTHORISED:
          case WOCKY_TLS_CERT_REVOKED:
          case WOCKY_TLS_CERT_MAYBE_DOS:
            DEBUG ("The certificate cannot be trusted.");
            reason = TP_CONNECTION_STATUS_REASON_CERT_UNTRUSTED;
            break;
          case WOCKY_TLS_CERT_EXPIRED:
            DEBUG ("The certificate has expired.");
            reason = TP_CONNECTION_STATUS_REASON_CERT_EXPIRED;
            break;
          case WOCKY_TLS_CERT_NOT_ACTIVE:
            DEBUG ("The certificate has not been activated.");
            reason = TP_CONNECTION_STATUS_REASON_CERT_NOT_ACTIVATED;
            break;
          case WOCKY_TLS_CERT_NAME_MISMATCH:
            DEBUG ("The server hostname doesn't match the one in the"
                " certificate.");
            reason = TP_CONNECTION_STATUS_REASON_CERT_HOSTNAME_MISMATCH;
            break;
          case WOCKY_TLS_CERT_INTERNAL_ERROR:
          case WOCKY_TLS_CERT_UNKNOWN_ERROR:
          default:
            DEBUG ("Unknown certificate error: %s", error->message);
            reason = TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR;
            break;
        }
    }

  tp_base_connection_change_status (base,
      TP_CONNECTION_STATUS_DISCONNECTED, reason);
}

/**
 * connector_connected
 *
 * Stage 2 of connecting, this function is called once the connect operation
 * has finished. It checks if the connection succeeded, creates and starts
 * the WockyPorter, then sends a discovery request to find the
 * server's features.
 */
static void
connector_connected (GabbleConnection *self,
    WockyXmppConnection *conn,
    const gchar *jid,
    GError *error)
{
  GabbleConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  /* We went to closing while we were connecting... drop the connection and
   * finish the shutdown */
  if (priv->closing)
    {
      if (conn != NULL)
        g_object_unref (conn);
      else
        g_error_free (error);

      tp_base_connection_finish_shutdown (base);
      return;
    }

  /* We don't need the connector any more */
  g_object_unref (priv->connector);
  priv->connector = NULL;

  if (conn == NULL)
    {
      connector_error_disconnect (self, error);
      g_error_free (error);
      return;
    }

  DEBUG ("connected (jid: %s)", jid);

  self->session = wocky_session_new (conn);
  priv->porter = wocky_session_get_porter (self->session);

  g_signal_connect (priv->porter, "remote-closed",
      G_CALLBACK (remote_closed_cb), self);
  g_signal_connect (priv->porter, "remote-error",
      G_CALLBACK (remote_error_cb), self);

  lm_connection_set_porter (self->lmconn, priv->porter);

  wocky_pep_service_start (self->pep_location, self->session);
  wocky_pep_service_start (self->pep_nick, self->session);
  wocky_pep_service_start (self->pep_olpc_buddy_props, self->session);
  wocky_pep_service_start (self->pep_olpc_activities, self->session);
  wocky_pep_service_start (self->pep_olpc_current_act, self->session);
  wocky_pep_service_start (self->pep_olpc_act_props, self->session);

  /* Don't use wocky_session_start as we don't want to start all the
   * components (Roster, presence-manager, etc) for now */
  wocky_porter_start (priv->porter);

  base->self_handle = tp_handle_ensure (contact_handles, jid, NULL, &error);

  if (base->self_handle == 0)
    {
      DEBUG ("couldn't get our self handle: %s", error->message);

      g_error_free (error);

      tp_base_connection_change_status ((TpBaseConnection *) self,
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

      return;
    }

  /* update priv->resource and priv->stream_server from the server's JID */
  if (!_gabble_connection_set_properties_from_account (self, jid, &error))
    {
      DEBUG ("couldn't parse our own JID: %s", error->message);

      g_error_free (error);

      tp_base_connection_change_status ((TpBaseConnection *) self,
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

      return;
    }

  DEBUG ("Created self handle %d, our JID is %s", base->self_handle, jid);

  /* set initial capabilities */
  gabble_connection_refresh_capabilities (self, NULL);

  if (!gabble_disco_request_with_timeout (self->disco, GABBLE_DISCO_TYPE_INFO,
                                          priv->stream_server, NULL,
                                          disco_reply_timeout,
                                          connection_disco_cb, self,
                                          G_OBJECT (self), &error))
    {
      DEBUG ("sending disco request failed: %s",
          error->message);

      g_error_free (error);

      tp_base_connection_change_status ((TpBaseConnection *) self,
          TP_CONNECTION_STATUS_DISCONNECTED,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
    }
}

static void
connector_connect_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = self->priv;
  WockyXmppConnection *conn;
  GError *error = NULL;
  gchar *jid = NULL;

  conn = wocky_connector_connect_finish (WOCKY_CONNECTOR (source), res, &error,
      &jid, &(priv->stream_id));

  connector_connected (self, conn, jid, error);
  g_free (jid);
}

static void
connector_register_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = self->priv;
  WockyXmppConnection *conn;
  GError *error = NULL;
  gchar *jid = NULL;

  conn = wocky_connector_register_finish (WOCKY_CONNECTOR (source), res, &error,
      &jid, &(priv->stream_id));

  connector_connected (self, conn, jid, error);
  g_free (jid);
}

static void
connect_callbacks (TpBaseConnection *base)
{
  GabbleConnection *conn = GABBLE_CONNECTION (base);
  GabbleConnectionPrivate *priv = conn->priv;

  g_assert (priv->iq_disco_cb == NULL);
  g_assert (priv->iq_unknown_cb == NULL);
  g_assert (priv->olpc_msg_cb == NULL);
  g_assert (priv->olpc_presence_cb == NULL);

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

  priv->olpc_msg_cb = lm_message_handler_new (conn_olpc_msg_cb,
                                            conn, NULL);
  lm_connection_register_message_handler (conn->lmconn, priv->olpc_msg_cb,
                                          LM_MESSAGE_TYPE_MESSAGE,
                                          LM_HANDLER_PRIORITY_FIRST);

  priv->olpc_presence_cb = lm_message_handler_new (conn_olpc_presence_cb,
      conn, NULL);
  lm_connection_register_message_handler (conn->lmconn, priv->olpc_presence_cb,
                                          LM_MESSAGE_TYPE_PRESENCE,
                                          LM_HANDLER_PRIORITY_NORMAL);
}

static void
disconnect_callbacks (TpBaseConnection *base)
{
  GabbleConnection *conn = GABBLE_CONNECTION (base);
  GabbleConnectionPrivate *priv = conn->priv;

  g_assert (priv->iq_disco_cb != NULL);
  g_assert (priv->iq_unknown_cb != NULL);
  g_assert (priv->olpc_msg_cb != NULL);
  g_assert (priv->olpc_presence_cb != NULL);

  lm_connection_unregister_message_handler (conn->lmconn, priv->iq_disco_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_disco_cb);
  priv->iq_disco_cb = NULL;

  lm_connection_unregister_message_handler (conn->lmconn, priv->iq_unknown_cb,
                                            LM_MESSAGE_TYPE_IQ);
  lm_message_handler_unref (priv->iq_unknown_cb);
  priv->iq_unknown_cb = NULL;

  lm_connection_unregister_message_handler (conn->lmconn, priv->olpc_msg_cb,
                                            LM_MESSAGE_TYPE_MESSAGE);
  lm_message_handler_unref (priv->olpc_msg_cb);
  priv->olpc_msg_cb = NULL;

  lm_connection_unregister_message_handler (conn->lmconn,
      priv->olpc_presence_cb, LM_MESSAGE_TYPE_MESSAGE);
  lm_message_handler_unref (priv->olpc_presence_cb);
  priv->olpc_presence_cb = NULL;
}

/**
 * _gabble_connection_connect
 *
 * Use the stored server & authentication details to commence
 * the stages for connecting to the server and authenticating.
 * Will create a WockyConnector.
 *
 * Stage 1 is _gabble_connection_connect calling wocky_connector_connect_async
 * Stage 2 is connector_connected initiating service discovery
 * Stage 3 is connection_disco_cb advertising initial presence, requesting
 *   the roster and setting the CONNECTED state
 */
static gboolean
_gabble_connection_connect (TpBaseConnection *base,
                            GError **error)
{
  GabbleConnection *conn = GABBLE_CONNECTION (base);
  GabbleConnectionPrivate *priv = conn->priv;
  char *jid;
  gchar *user_certs_dir;

  g_assert (priv->connector == NULL);
  g_assert (priv->port <= G_MAXUINT16);
  g_assert (priv->stream_server != NULL);
  g_assert (priv->username != NULL);
  g_assert (priv->password != NULL);
  g_assert (priv->resource != NULL);

  jid = gabble_encode_jid (priv->username, priv->stream_server, NULL);
  priv->connector = wocky_connector_new (jid, priv->password, priv->resource);
  g_free (jid);

  /* system certs */
  wocky_connector_add_ca (priv->connector,
      "/etc/ssl/certs/ca-certificates.crt");

  /* user certs */
  user_certs_dir = g_build_filename (g_get_user_config_dir (),
      "telepathy", "certs", NULL);
  wocky_connector_add_ca (priv->connector, user_certs_dir);
  g_free (user_certs_dir);

  /* If the UI explicitly specified a port or a server, pass them to Loudmouth
   * rather than letting it do an SRV lookup.
   *
   * If the port is 5222 (the default) then unless the UI also specified a
   * server or old-style SSL, we ignore it and do an SRV lookup anyway. This
   * means that UIs that blindly pass the default back to Gabble work
   * correctly. If the user really did mean 5222, then when the SRV lookup
   * fails we fall back to that anyway.
   */
  if (priv->port != 5222 || priv->connect_server != NULL || priv->old_ssl)
    {
      gchar *server;

      if (priv->connect_server != NULL)
        server = priv->connect_server;
      else
        server = priv->stream_server;

      DEBUG ("disabling SRV because \"server\" or \"old-ssl\" was specified "
          "or port was not 5222, will connect to %s", server);

      g_object_set (priv->connector,
          "xmpp-server", server,
          "xmpp-port", priv->port,
          NULL);
    }
  else
    {
      DEBUG ("letting SRV lookup decide server and port");
    }

  if (!conn->priv->require_encryption && !conn->priv->ignore_ssl_errors)
    {
      DEBUG ("require-encryption is False; flipping ignore_ssl_errors to True");
      conn->priv->ignore_ssl_errors = TRUE;
    }

  g_object_set (priv->connector,
      "ignore-ssl-errors", priv->ignore_ssl_errors,
      "old-ssl", priv->old_ssl,
      /* We always wants to support old servers */
      "legacy", TRUE,
      NULL);

  if (priv->old_ssl)
    {
      g_object_set (priv->connector,
          "tls-required", FALSE,
          NULL);
    }
  else
    {
      g_object_set (priv->connector,
          "tls-required", priv->require_encryption,
          "plaintext-auth-allowed", !priv->require_encryption,
          NULL);
    }

  /* FIXME: support proxy server */
  /* FIXME: support keep alive */

  if (priv->do_register)
    {
      DEBUG ("Start registering");

      wocky_connector_register_async (priv->connector,
          connector_register_cb, conn);
    }
  else
    {
      DEBUG ("Start connecting");

      wocky_connector_connect_async (priv->connector,
          connector_connect_cb, conn);
    }

  return TRUE;
}

static void
closed_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (user_data);
  GabbleConnectionPrivate *priv = self->priv;
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  GError *error = NULL;

  if (priv->disconnect_timer != 0)
    {
      /* stop the timer */
      g_source_remove (priv->disconnect_timer);
      priv->disconnect_timer = 0;
    }

  if (!wocky_porter_close_finish (WOCKY_PORTER (source), res, &error))
    {
      DEBUG ("close failed: %s", error->message);

      if (g_error_matches (error, WOCKY_PORTER_ERROR,
            WOCKY_PORTER_ERROR_FORCIBLY_CLOSED))
        {
          /* Close operation has been aborted because a force_close operation
           * has been started. tp_base_connection_finish_shutdown will be
           * called once this force_close operation is completed so we don't
           * do it here. */

          g_error_free (error);
          return;
        }

      g_error_free (error);
    }
  else
    {
      DEBUG ("connection properly closed");
    }

  tp_base_connection_finish_shutdown (base);
}

static gboolean
disconnect_timeout_cb (gpointer data)
{
  GabbleConnection *self = GABBLE_CONNECTION (data);
  GabbleConnectionPrivate *priv = self->priv;

  DEBUG ("Close operation timed out. Force closing");
  priv->disconnect_timer = 0;

  wocky_porter_force_close_async (priv->porter, NULL, force_close_cb, self);
  return FALSE;
}

static void
connection_shut_down (TpBaseConnection *base)
{
  GabbleConnection *self = GABBLE_CONNECTION (base);
  GabbleConnectionPrivate *priv = self->priv;

  if (priv->closing)
    return;

  priv->closing = TRUE;

  if (priv->porter != NULL)
    {
      DEBUG ("connection may still be open; closing it: %p", base);

      g_assert (priv->disconnect_timer == 0);
      priv->disconnect_timer = g_timeout_add_seconds (DISCONNECT_TIMEOUT,
          disconnect_timeout_cb, self);

      wocky_porter_close_async (priv->porter, NULL, closed_cb, self);
      return;
    }
  else if (priv->connector != NULL)
    {
      /* FIXME: cancel connecting if we are connecting, for now we wait *
       * until the connection is finished and then drop it directly     *
       * wocky connector does not support gcancellables yet             */
      DEBUG ("wait for connector to finish before closing: %p", base);
      return;
    }

  DEBUG ("neither porter nor connector is alive: clean up the base connection");
  tp_base_connection_finish_shutdown (base);
}

gboolean
gabble_connection_visible_to (GabbleConnection *self,
    TpHandle recipient)
{
  if (self->self_presence->status == GABBLE_PRESENCE_HIDDEN)
    return FALSE;

  if ((gabble_roster_handle_get_subscription (self->roster, recipient)
      & GABBLE_ROSTER_SUBSCRIPTION_FROM) == 0)
    return FALSE;

  /* FIXME: other reasons they might not be able to see our presence? */

  return TRUE;
}

static void
gabble_connection_fill_in_caps (GabbleConnection *self,
    LmMessage *presence_message)
{
  GabblePresence *presence = self->self_presence;
  LmMessageNode *node = lm_message_get_node (presence_message);
  gchar *caps_hash;
  gboolean voice_v1, video_v1;
  GString *ext = g_string_new ("");

  /* XEP-0115 version 1.5 uses a verification string in the 'ver' attribute */
  caps_hash = caps_hash_compute_from_self_presence (self);
  node = lm_message_node_add_child (node, "c", NULL);
  lm_message_node_set_attributes (
    node,
    "xmlns", NS_CAPS,
    "hash",  "sha-1",
    "node",  NS_GABBLE_CAPS,
    "ver",   caps_hash,
    NULL);

  /* Ensure this set of capabilities is in the cache. */
  gabble_presence_cache_add_own_caps (self->presence_cache, caps_hash,
      gabble_presence_peek_caps (presence));

  /* XEP-0115 deprecates 'ext' feature bundles. But we still need
   * BUNDLE_VOICE_V1 it for backward-compatibility with Gabble 0.2 */

  g_string_append (ext, BUNDLE_PMUC_V1);

  voice_v1 = gabble_presence_has_cap (presence, NS_GOOGLE_FEAT_VOICE);
  video_v1 = gabble_presence_has_cap (presence, NS_GOOGLE_FEAT_VIDEO);

  if (voice_v1)
    g_string_append (ext, " " BUNDLE_VOICE_V1);

  if (video_v1)
    g_string_append (ext, " " BUNDLE_VIDEO_V1);

  lm_message_node_set_attribute (node, "ext", ext->str);
  g_string_free (ext, TRUE);
  g_free (caps_hash);
}

gboolean
gabble_connection_send_capabilities (GabbleConnection *self,
    const gchar *recipient,
    GError **error)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) self, TP_HANDLE_TYPE_CONTACT);
  LmMessage *message;
  gboolean ret;
  TpHandle handle;

  /* if we don't have a handle allocated for the recipient, they clearly aren't
   * getting our presence... */
  handle = tp_handle_lookup (contact_repo, recipient, NULL, NULL);

  if (handle != 0 && gabble_connection_visible_to (self, handle))
    {
      /* nothing to do, they should already have had our presence */
      return TRUE;
    }

  /* We deliberately don't include anything except the caps here */
  message = lm_message_new_with_sub_type (recipient, LM_MESSAGE_TYPE_PRESENCE,
      LM_MESSAGE_SUB_TYPE_AVAILABLE);

  gabble_connection_fill_in_caps (self, message);

  ret = _gabble_connection_send (self, message, error);

  lm_message_unref (message);

  return ret;
}

/**
 * _gabble_connection_signal_own_presence:
 * @self: A #GabbleConnection
 * @to: bare or full JID for directed presence, or NULL
 * @error: pointer in which to return a GError in case of failure.
 *
 * Signal the user's stored presence to @to, or to the jabber server
 *
 * Retuns: FALSE if an error occurred
 */
gboolean
_gabble_connection_signal_own_presence (GabbleConnection *self,
    const gchar *to,
    GError **error)
{
  GabblePresence *presence = self->self_presence;
  LmMessage *message = gabble_presence_as_message (presence, to);
  gboolean ret;

  if (presence->status == GABBLE_PRESENCE_HIDDEN)
    {
      if ((self->features & GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE) != 0
          && to == NULL)
        lm_message_node_set_attribute (lm_message_get_node (message),
            "type", "invisible");
      /* FIXME: or if sending directed presence, should we add
       * <show>away</show>? */
    }

  gabble_connection_fill_in_caps (self, message);

  ret = _gabble_connection_send (self, message, error);

  lm_message_unref (message);

  /* FIXME: if sending broadcast presence, should we echo it to everyone we
   * previously sent directed presence to? (Perhaps also GC them after a
   * while?) */

  if (to == NULL && !self->priv->closing)
    gabble_muc_factory_broadcast_presence (self->muc_factory);

  return ret;
}

gboolean
gabble_connection_request_decloak (GabbleConnection *self,
    const gchar *to,
    const gchar *reason,
    GError **error)
{
  GabblePresence *presence = self->self_presence;
  LmMessage *message = gabble_presence_as_message (presence, to);
  LmMessageNode *decloak;
  gboolean ret;

  gabble_connection_fill_in_caps (self, message);

  decloak = lm_message_node_add_child (lm_message_get_node (message),
      "temppres", NULL);
  lm_message_node_set_attribute (decloak, "xmlns", NS_TEMPPRES);

  if (reason != NULL && *reason != '\0')
    {
      lm_message_node_set_attribute (decloak, "reason", reason);
    }

  ret = _gabble_connection_send (self, message, error);
  lm_message_unref (message);

  return ret;
}

static gboolean
gabble_connection_refresh_capabilities (GabbleConnection *self,
    GabbleCapabilitySet **old_out)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  GError *error = NULL;
  GHashTableIter iter;
  gpointer k, v;
  GabbleCapabilitySet *save_set;

  save_set = self->priv->all_caps;
  self->priv->all_caps = gabble_capability_set_new ();

  gabble_capability_set_update (self->priv->all_caps,
      gabble_capabilities_get_fixed_caps ());
  gabble_capability_set_update (self->priv->all_caps, self->priv->notify_caps);
  gabble_capability_set_update (self->priv->all_caps, self->priv->legacy_caps);
  gabble_capability_set_update (self->priv->all_caps, self->priv->bonus_caps);

  g_hash_table_iter_init (&iter, self->priv->client_caps);

  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      if (DEBUGGING)
        {
          gchar *s = gabble_capability_set_dump (v, "  ");

          DEBUG ("incorporating caps for %s:\n%s", (const gchar *) k, s);
          g_free (s);
        }

      gabble_capability_set_update (self->priv->all_caps, v);
    }

  if (self->self_presence != NULL)
    gabble_presence_set_capabilities (self->self_presence,
        self->priv->resource, self->priv->all_caps, self->priv->caps_serial++);

  if (gabble_capability_set_equals (self->priv->all_caps, save_set))
    {
      gabble_capability_set_free (save_set);
      DEBUG ("nothing to do");
      return FALSE;
    }

  /* don't signal presence unless we're already CONNECTED */
  if (base->status != TP_CONNECTION_STATUS_CONNECTED)
    {
      gabble_capability_set_free (save_set);
      DEBUG ("not emitting self-presence stanza: not connected yet");
      return FALSE;
    }

  if (!_gabble_connection_signal_own_presence (self, NULL, &error))
    {
      gabble_capability_set_free (save_set);
      DEBUG ("error sending presence: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  if (old_out == NULL)
    gabble_capability_set_free (save_set);
  else
    *old_out = save_set;

  return TRUE;
}

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

  result = lm_iq_message_make_result (iq);

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

static void
add_feature_node (gpointer namespace,
    gpointer result_query)
{
  LmMessageNode *feature_node;

  feature_node = lm_message_node_add_child (result_query, "feature",
      NULL);
  lm_message_node_set_attribute (feature_node, "var", namespace);
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
  LmMessageNode *iq, *result_iq, *query, *result_query, *identity;
  const gchar *node, *suffix;
  const GabbleCapabilitySet *features;

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

  result = lm_iq_message_make_result (message);

  /* If we get an IQ without an id='', there's not much we can do. */
  if (result == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  result_iq = lm_message_get_node (result);
  result_query = lm_message_node_add_child (result_iq, "query", NULL);
  lm_message_node_set_attribute (result_query, "xmlns", NS_DISCO_INFO);

  if (node)
    lm_message_node_set_attribute (result_query, "node", node);

  DEBUG ("got disco request for node %s", node);

  /* Every entity MUST have at least one identity (XEP-0030). Gabble publishs
   * one identity. If you change the identity here, you also need to change
   * caps_hash_compute_from_self_presence(). */
  identity = lm_message_node_add_child
      (result_query, "identity", NULL);
  lm_message_node_set_attribute (identity, "category", "client");
  lm_message_node_set_attribute (identity, "name", PACKAGE_STRING);
  lm_message_node_set_attribute (identity, "type", CLIENT_TYPE);

  if (node == NULL)
    features = gabble_presence_peek_caps (self->self_presence);
  /* If node is not NULL, it can be either a caps bundle as defined in the
   * legacy XEP-0115 version 1.3 or an hash as defined in XEP-0115 version
   * 1.5. Let's see if it's a verification string we've told the cache about.
   */
  else
    features = gabble_presence_cache_peek_own_caps (self->presence_cache,
        suffix);

  if (features == NULL)
    {
      /* Otherwise, is it one of the caps bundles we advertise? These are not
       * just shoved into the cache with gabble_presence_cache_add_own_caps()
       * because capabilities_get_features() always includes a few bonus
       * features...
       */
      if (!tp_strdiff (suffix, BUNDLE_VOICE_V1))
        features = gabble_capabilities_get_bundle_voice_v1 ();

      if (!tp_strdiff (suffix, BUNDLE_VIDEO_V1))
        features = gabble_capabilities_get_bundle_video_v1 ();
    }

  if (features == NULL && tp_strdiff (suffix, BUNDLE_PMUC_V1))
    {
      _gabble_connection_send_iq_error (self, message,
          XMPP_ERROR_ITEM_NOT_FOUND, NULL);
    }
  else
    {
      /* Send an empty reply for a pmuc-v1 disco, matching Google's behaviour. */
      if (features != NULL)
        {
          gabble_capability_set_foreach (features, add_feature_node,
              result_query);
        }

      NODE_DEBUG (result_iq, "sending disco response");

      if (!lm_connection_send (self->lmconn, result, NULL))
        {
          DEBUG ("sending disco response failed");
        }
    }

  lm_message_unref (result);

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
 * connection_disco_cb
 *
 * Stage 3 of connecting, this function is called by GabbleDisco after the
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
  TpBaseConnection *base = (TpBaseConnection *) conn;
  GabbleConnectionPrivate *priv;
  GError *error = NULL;

  if (base->status != TP_CONNECTION_STATUS_CONNECTING)
    {
      g_assert (base->status == TP_CONNECTION_STATUS_DISCONNECTED);
      return;
    }

  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = conn->priv;

  if (disco_error)
    {
      DEBUG ("got disco error, setting no features: %s", disco_error->message);
      if (disco_error->code == GABBLE_DISCO_ERROR_TIMEOUT)
        {
          DEBUG ("didn't receive a response to our disco request: disconnect");
          goto ERROR;
        }
    }
  else
    {
      NodeIter i;

      NODE_DEBUG (result, "got");

      for (i = node_iter (result); i; i = node_iter_next (i))
        {
          LmMessageNode *child = node_iter_data (i);

          if (0 == strcmp (child->name, "identity"))
            {
              const gchar *category = lm_message_node_get_attribute (child,
                  "category");
              const gchar *type = lm_message_node_get_attribute (child, "type");

              if (!tp_strdiff (category, "pubsub") &&
                  !tp_strdiff (type, "pep"))
                /* XXX: should we also check for specific PubSub <feature>s? */
                conn->features |= GABBLE_CONNECTION_FEATURES_PEP;
            }
          else if (0 == strcmp (child->name, "feature"))
            {
              const gchar *var = lm_message_node_get_attribute (child, "var");

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
              else if (0 == strcmp (var, NS_GOOGLE_MAIL_NOTIFY))
                conn->features |= GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY;
            }
        }

      DEBUG ("set features flags to %d", conn->features);
    }

  if (conn->features & GABBLE_CONNECTION_FEATURES_PEP)
    {
      const gchar *ifaces[] = { GABBLE_IFACE_OLPC_BUDDY_INFO,
          GABBLE_IFACE_OLPC_ACTIVITY_PROPERTIES,
          NULL };

      tp_base_connection_add_interfaces ((TpBaseConnection *) conn, ifaces);
    }

  if (conn->features & GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY)
    {
       const gchar *ifaces[] =
         { GABBLE_IFACE_CONNECTION_INTERFACE_MAIL_NOTIFICATION, NULL };

      tp_base_connection_add_interfaces ((TpBaseConnection *) conn, ifaces);
    }

  /* send presence to the server to indicate availability */
  /* TODO: some way for the user to set this */
  if (!_gabble_connection_signal_own_presence (conn, NULL, &error))
    {
      DEBUG ("sending initial presence failed: %s", error->message);
      goto ERROR;
    }

  /* go go gadget on-line */
  tp_base_connection_change_status (base,
      TP_CONNECTION_STATUS_CONNECTED, TP_CONNECTION_STATUS_REASON_REQUESTED);

  return;

ERROR:
  if (error != NULL)
    g_error_free (error);

  tp_base_connection_change_status (base,
      TP_CONNECTION_STATUS_DISCONNECTED,
      TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

  return;
}


/****************************************************************************
 *                          D-BUS EXPORTED METHODS                          *
 ****************************************************************************/

static void gabble_free_rcc_list (GPtrArray *rccs)
{
  g_boxed_free (TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST, rccs);
}

/**
 * gabble_connection_build_contact_caps:
 * @handle: a contact
 * @caps: @handle's XMPP capabilities
 *
 * Returns: an array containing the channel classes corresponding to @caps.
 */
static GPtrArray *
gabble_connection_build_contact_caps (
    GabbleConnection *self,
    TpHandle handle,
    const GabbleCapabilitySet *caps)
{
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self);
  TpChannelManagerIter iter;
  TpChannelManager *manager;
  GPtrArray *ret = g_ptr_array_new ();

  tp_base_connection_channel_manager_iter_init (&iter, base_conn);

  while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
    {
      /* all channel managers must implement the capability interface */
      g_assert (GABBLE_IS_CAPS_CHANNEL_MANAGER (manager));

      gabble_caps_channel_manager_get_contact_capabilities (
          GABBLE_CAPS_CHANNEL_MANAGER (manager), handle, caps, ret);
    }

  return ret;
}

static void
_emit_capabilities_changed (GabbleConnection *conn,
                            TpHandle handle,
                            const GabbleCapabilitySet *old_set,
                            const GabbleCapabilitySet *new_set)
{
  GPtrArray *caps_arr;
  const CapabilityConversionData *ccd;
  GHashTable *hash;
  guint i;

  if (gabble_capability_set_equals (old_set, new_set))
    return;

  /* o.f.T.C.Capabilities */

  caps_arr = g_ptr_array_new ();

  for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
    {
      guint old_specific = ccd->c2tf_fn (old_set);
      guint new_specific = ccd->c2tf_fn (new_set);

      if (old_specific != 0 || new_specific != 0)
        {
          GValue caps_monster_struct = {0, };
          guint old_generic = old_specific ?
            TP_CONNECTION_CAPABILITY_FLAG_CREATE |
            TP_CONNECTION_CAPABILITY_FLAG_INVITE : 0;
          guint new_generic = new_specific ?
            TP_CONNECTION_CAPABILITY_FLAG_CREATE |
            TP_CONNECTION_CAPABILITY_FLAG_INVITE : 0;

          if (0 == (old_specific ^ new_specific))
            continue;

          g_value_init (&caps_monster_struct,
              TP_STRUCT_TYPE_CAPABILITY_CHANGE);
          g_value_take_boxed (&caps_monster_struct,
              dbus_g_type_specialized_construct
                (TP_STRUCT_TYPE_CAPABILITY_CHANGE));

          dbus_g_type_struct_set (&caps_monster_struct,
              0, handle,
              1, ccd->iface,
              2, old_generic,
              3, new_generic,
              4, old_specific,
              5, new_specific,
              G_MAXUINT);

          g_ptr_array_add (caps_arr, g_value_get_boxed (&caps_monster_struct));
        }
    }

  if (caps_arr->len)
    tp_svc_connection_interface_capabilities_emit_capabilities_changed (
        conn, caps_arr);


  for (i = 0; i < caps_arr->len; i++)
    {
      g_boxed_free (TP_STRUCT_TYPE_CAPABILITY_CHANGE,
          g_ptr_array_index (caps_arr, i));
    }
  g_ptr_array_free (caps_arr, TRUE);

  /* o.f.T.C.ContactCapabilities */
  caps_arr = gabble_connection_build_contact_caps (conn, handle, new_set);

  hash = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) gabble_free_rcc_list);
  g_hash_table_insert (hash, GUINT_TO_POINTER (handle), caps_arr);

  tp_svc_connection_interface_contact_capabilities_emit_contact_capabilities_changed (
      conn, hash);

  g_hash_table_destroy (hash);
}

/**
 * gabble_connection_get_handle_contact_capabilities:
 *
 * Returns: a set of channel classes representing @handle's capabilities, or
 *          %NULL if unknown.
 */
static GPtrArray *
gabble_connection_get_handle_contact_capabilities (
    GabbleConnection *self,
    TpHandle handle)
{
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self);
  GabblePresence *p;
  const GabbleCapabilitySet *caps;
  GPtrArray *arr;

  if (handle == base_conn->self_handle)
    p = self->self_presence;
  else
    p = gabble_presence_cache_get (self->presence_cache, handle);

  if (p == NULL)
    {
      DEBUG ("don't know %u's presence; no caps for them.", handle);
      return NULL;
    }

  caps = gabble_presence_peek_caps (p);
  arr = gabble_connection_build_contact_caps (self, handle, caps);
  return arr;
}

static void
connection_capabilities_update_cb (GabblePresenceCache *cache,
    TpHandle handle,
    const GabbleCapabilitySet *old_cap_set,
    const GabbleCapabilitySet *new_cap_set,
    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  _emit_capabilities_changed (conn, handle, old_cap_set, new_cap_set);
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
                                          const gchar **del,
                                          DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  guint i;
  GabbleConnectionPrivate *priv = self->priv;
  const CapabilityConversionData *ccd;
  GPtrArray *ret;
  GabbleCapabilitySet *save_set;
  GabbleCapabilitySet *add_set, *remove_set;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  /* Now that someone has told us our *actual* capabilities, we can stop
   * advertising spurious caps in initial presence */
  gabble_capability_set_clear (self->priv->bonus_caps);

  add_set = gabble_capability_set_new ();
  remove_set = gabble_capability_set_new ();

  for (i = 0; i < add->len; i++)
    {
      GValue iface_flags_pair = {0, };
      gchar *channel_type;
      guint flags;

      g_value_init (&iface_flags_pair, TP_STRUCT_TYPE_CAPABILITY_PAIR);
      g_value_set_static_boxed (&iface_flags_pair, g_ptr_array_index (add, i));

      dbus_g_type_struct_get (&iface_flags_pair,
                              0, &channel_type,
                              1, &flags,
                              G_MAXUINT);

      for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
          if (g_str_equal (channel_type, ccd->iface))
            ccd->tf2c_fn (flags, add_set);

      g_free (channel_type);
    }

  for (i = 0; NULL != del[i]; i++)
    {
      for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
          if (g_str_equal (del[i], ccd->iface))
            ccd->tf2c_fn (~0, remove_set);
    }

  gabble_capability_set_update (priv->legacy_caps, add_set);
  gabble_capability_set_exclude (priv->legacy_caps, remove_set);

  if (DEBUGGING)
    {
      gchar *add_str = gabble_capability_set_dump (add_set, "  ");
      gchar *remove_str = gabble_capability_set_dump (remove_set, "  ");

      DEBUG ("caps to add:\n%s", add_str);
      DEBUG ("caps to remove:\n%s", remove_str);
      g_free (add_str);
      g_free (remove_str);
    }

  gabble_capability_set_free (add_set);
  gabble_capability_set_free (remove_set);

  if (gabble_connection_refresh_capabilities (self, &save_set))
    {
      _emit_capabilities_changed (self, base->self_handle, save_set,
          priv->all_caps);
      gabble_capability_set_free (save_set);
    }

  ret = g_ptr_array_new ();

  for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
    {
      guint tp_caps = ccd->c2tf_fn (self->priv->all_caps);

      if (tp_caps != 0)
        {
          GValue iface_flags_pair = {0, };

          g_value_init (&iface_flags_pair, TP_STRUCT_TYPE_CAPABILITY_PAIR);
          g_value_take_boxed (&iface_flags_pair,
              dbus_g_type_specialized_construct (
                  TP_STRUCT_TYPE_CAPABILITY_PAIR));

          dbus_g_type_struct_set (&iface_flags_pair,
                                  0, ccd->iface,
                                  1, tp_caps,
                                  G_MAXUINT);

          g_ptr_array_add (ret, g_value_get_boxed (&iface_flags_pair));
        }
    }

  tp_svc_connection_interface_capabilities_return_from_advertise_capabilities (
      context, ret);

  g_ptr_array_foreach (ret, (GFunc) g_value_array_free, NULL);
  g_ptr_array_free (ret, TRUE);
}

/**
 * gabble_connection_update_capabilities
 *
 * Implements D-Bus method UpdateCapabilities
 * on interface
 * org.freedesktop.Telepathy.Connection.Interface.ContactCapabilities
 */
static void
gabble_connection_update_capabilities (
    TpSvcConnectionInterfaceContactCapabilities *iface,
    const GPtrArray *clients,
    DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  GabbleCapabilitySet *old_caps;
  TpChannelManagerIter iter;
  TpChannelManager *manager;
  guint i;

  old_caps = gabble_capability_set_copy (self->priv->all_caps);

  /* Now that someone has told us our *actual* capabilities, we can stop
   * advertising spurious caps in initial presence */
  gabble_capability_set_clear (self->priv->bonus_caps);

  tp_base_connection_channel_manager_iter_init (&iter, base);

  while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
    {
      /* all channel managers must implement the capability interface */
      g_assert (GABBLE_IS_CAPS_CHANNEL_MANAGER (manager));

      gabble_caps_channel_manager_reset_capabilities (
          GABBLE_CAPS_CHANNEL_MANAGER (manager));
    }

  DEBUG ("enter");

  for (i = 0; i < clients->len; i++)
    {
      GValueArray *va = g_ptr_array_index (clients, i);
      const gchar *client_name = g_value_get_string (va->values + 0);
      const GPtrArray *filters = g_value_get_boxed (va->values + 1);
      const gchar * const * cap_tokens = g_value_get_boxed (va->values + 2);
      GabbleCapabilitySet *cap_set;

      g_hash_table_remove (self->priv->client_caps, client_name);

      if ((cap_tokens == NULL || cap_tokens[0] != NULL) &&
          filters->len == 0)
        {
          /* no capabilities */
          DEBUG ("client %s can't do anything", client_name);
          continue;
        }

      cap_set = gabble_capability_set_new ();

      tp_base_connection_channel_manager_iter_init (&iter, base);

      while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
        {
          /* all channel managers must implement the capability interface */
          g_assert (GABBLE_IS_CAPS_CHANNEL_MANAGER (manager));

          gabble_caps_channel_manager_represent_client (
              GABBLE_CAPS_CHANNEL_MANAGER (manager), client_name, filters,
              cap_tokens, cap_set);
        }

      if (gabble_capability_set_is_empty (cap_set))
        {
          DEBUG ("client %s has no interesting capabilities", client_name);
          gabble_capability_set_free (cap_set);
        }
      else
        {
          if (DEBUGGING)
            {
              gchar *s = gabble_capability_set_dump (cap_set, "  ");

              DEBUG ("client %s contributes:\n%s", client_name, s);
              g_free (s);
            }

          g_hash_table_insert (self->priv->client_caps, g_strdup (client_name),
              cap_set);
        }
    }

  if (gabble_connection_refresh_capabilities (self, &old_caps))
    {
      _emit_capabilities_changed (self, base->self_handle, old_caps,
          self->priv->all_caps);
      gabble_capability_set_free (old_caps);
    }

  tp_svc_connection_interface_contact_capabilities_return_from_update_capabilities (
      context);
}

static const gchar *assumed_caps[] =
{
  TP_IFACE_CHANNEL_TYPE_TEXT,
  NULL
};


/**
 * gabble_connection_get_handle_capabilities
 *
 * Add capabilities of handle to the given GPtrArray
 */
static void
gabble_connection_get_handle_capabilities (GabbleConnection *self,
  TpHandle handle, GPtrArray *arr)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  GabblePresence *pres;
  const CapabilityConversionData *ccd;
  guint typeflags;
  const gchar **assumed;

  if (0 == handle)
    {
      /* obsolete request for the connection's capabilities, do nothing */
      return;
    }

  if (handle == base->self_handle)
    pres = self->self_presence;
  else
    pres = gabble_presence_cache_get (self->presence_cache, handle);

  if (NULL != pres)
    {
      const GabbleCapabilitySet *cap_set = gabble_presence_peek_caps (pres);

      for (ccd = capabilities_conversions; NULL != ccd->iface; ccd++)
        {
          typeflags = ccd->c2tf_fn (cap_set);

          if (typeflags)
            {
              GValue monster = {0, };

              g_value_init (&monster, TP_STRUCT_TYPE_CONTACT_CAPABILITY);
              g_value_take_boxed (&monster,
                  dbus_g_type_specialized_construct (
                    TP_STRUCT_TYPE_CONTACT_CAPABILITY));

              dbus_g_type_struct_set (&monster,
                  0, handle,
                  1, ccd->iface,
                  2, TP_CONNECTION_CAPABILITY_FLAG_CREATE |
                      TP_CONNECTION_CAPABILITY_FLAG_INVITE,
                  3, typeflags,
                  G_MAXUINT);

              g_ptr_array_add (arr, g_value_get_boxed (&monster));
            }
        }
    }

  for (assumed = assumed_caps; NULL != *assumed; assumed++)
    {
      GValue monster = {0, };

      g_value_init (&monster, TP_STRUCT_TYPE_CONTACT_CAPABILITY);
      g_value_take_boxed (&monster,
          dbus_g_type_specialized_construct (
              TP_STRUCT_TYPE_CONTACT_CAPABILITY));

      dbus_g_type_struct_set (&monster,
          0, handle,
          1, *assumed,
          2, TP_CONNECTION_CAPABILITY_FLAG_CREATE |
              TP_CONNECTION_CAPABILITY_FLAG_INVITE,
          3, 0,
          G_MAXUINT);

      g_ptr_array_add (arr, g_value_get_boxed (&monster));
    }
}


static void
conn_capabilities_fill_contact_attributes (GObject *obj,
  const GArray *contacts, GHashTable *attributes_hash)
{
  GabbleConnection *self = GABBLE_CONNECTION (obj);
  guint i;
  GPtrArray *array = NULL;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);

      if (array == NULL)
        array = g_ptr_array_new ();

      gabble_connection_get_handle_capabilities (self, handle, array);

      if (array->len > 0)
        {
          GValue *val =  tp_g_value_slice_new (
            TP_ARRAY_TYPE_CONTACT_CAPABILITY_LIST);

          g_value_take_boxed (val, array);
          tp_contacts_mixin_set_contact_attribute (attributes_hash,
            handle, TP_IFACE_CONNECTION_INTERFACE_CAPABILITIES"/caps",
            val);

          array = NULL;
        }
    }

    if (array != NULL)
      g_ptr_array_free (array, TRUE);
}

static void
conn_contact_capabilities_fill_contact_attributes (GObject *obj,
  const GArray *contacts, GHashTable *attributes_hash)
{
  GabbleConnection *self = GABBLE_CONNECTION (obj);
  guint i;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GPtrArray *array;

      array = gabble_connection_get_handle_contact_capabilities (self, handle);

      if (array != NULL)
        {
          GValue *val =  tp_g_value_slice_new (
            TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST);

          g_value_take_boxed (val, array);
          tp_contacts_mixin_set_contact_attribute (attributes_hash,
              handle,
              TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES"/capabilities",
              val);
        }
    }
}

/**
 * gabble_connection_get_capabilities
 *
 * Implements D-Bus method GetCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 */
static void
gabble_connection_get_capabilities (TpSvcConnectionInterfaceCapabilities *iface,
                                    const GArray *handles,
                                    DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
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
      return;
    }

  ret = g_ptr_array_new ();

  for (i = 0; i < handles->len; i++)
    {
      TpHandle handle = g_array_index (handles, TpHandle, i);

      gabble_connection_get_handle_capabilities (self, handle, ret);
    }

  tp_svc_connection_interface_capabilities_return_from_get_capabilities (
      context, ret);

  for (i = 0; i < ret->len; i++)
    {
      g_value_array_free (g_ptr_array_index (ret, i));
    }

  g_ptr_array_free (ret, TRUE);
}

/**
 * gabble_connection_get_contact_capabilities
 *
 * Implements D-Bus method GetContactCapabilities
 * on interface
 * org.freedesktop.Telepathy.Connection.Interface.ContactCapabilities
 */
static void
gabble_connection_get_contact_capabilities (
    TpSvcConnectionInterfaceContactCapabilities *iface,
    const GArray *handles,
    DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  guint i;
  GHashTable *ret;
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handles_are_valid (contact_handles, handles, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) gabble_free_rcc_list);

  for (i = 0; i < handles->len; i++)
    {
      GPtrArray *arr;
      TpHandle handle = g_array_index (handles, TpHandle, i);

      arr = gabble_connection_get_handle_contact_capabilities (self, handle);

      if (arr != NULL)
        g_hash_table_insert (ret, GUINT_TO_POINTER (handle), arr);
    }

  tp_svc_connection_interface_contact_capabilities_return_from_get_contact_capabilities
      (context, ret);

  g_hash_table_destroy (ret);
}


const char *
_gabble_connection_find_conference_server (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = conn->priv;

  if (priv->conference_server == NULL)
    {
      /* Find first server that has NS_MUC feature */
      const GabbleDiscoItem *item = gabble_disco_service_find (conn->disco,
          "conference", "text", NS_MUC);
      if (item != NULL)
        priv->conference_server = item->jid;
    }

  if (priv->conference_server == NULL)
    priv->conference_server = priv->fallback_conference_server;

  return priv->conference_server;
}


gchar *
gabble_connection_get_canonical_room_name (GabbleConnection *conn,
                                           const gchar *name)
{
  const gchar *server;

  g_assert (GABBLE_IS_CONNECTION (conn));

  if (strchr (name, '@'))
    return g_strdup (name);

  server = _gabble_connection_find_conference_server (conn);

  if (server == NULL)
    return NULL;

  return gabble_encode_jid (name, server, NULL);
}

void
gabble_connection_ensure_capabilities (GabbleConnection *self,
    const GabbleCapabilitySet *ensured)
{
  gabble_capability_set_update (self->priv->notify_caps, ensured);
  gabble_connection_refresh_capabilities (self, NULL);
}

gboolean
gabble_connection_send_presence (GabbleConnection *conn,
                                 LmMessageSubType sub_type,
                                 const gchar *contact,
                                 const gchar *status,
                                 GError **error)
{
  LmMessage *message;
  gboolean result;

  message = lm_message_new_with_sub_type (contact,
      LM_MESSAGE_TYPE_PRESENCE,
      sub_type);

  if (LM_MESSAGE_SUB_TYPE_SUBSCRIBE == sub_type)
    lm_message_node_add_own_nick (message->node, conn);

  if (!CHECK_STR_EMPTY(status))
    lm_message_node_add_child (message->node, "status", status);

  result = _gabble_connection_send (conn, message, error);

  lm_message_unref (message);

  return result;
}

static void
capabilities_service_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceCapabilitiesClass *klass =
    (TpSvcConnectionInterfaceCapabilitiesClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_capabilities_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(advertise_capabilities);
  IMPLEMENT(get_capabilities);
#undef IMPLEMENT
}

static void
gabble_conn_contact_caps_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceContactCapabilitiesClass *klass = g_iface;

#define IMPLEMENT(x) \
    tp_svc_connection_interface_contact_capabilities_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(get_contact_capabilities);
  IMPLEMENT(update_capabilities);
#undef IMPLEMENT
}


/* For unit tests only */
void
gabble_connection_set_disco_reply_timeout (guint timeout)
{
  disco_reply_timeout = timeout;
}
