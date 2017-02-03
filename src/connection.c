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

#include <stdio.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <glib-object.h>
#include <wocky/wocky.h>
#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "extensions/extensions.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include <gabble/error.h>
#include "bytestream-factory.h"
#include "gabble/capabilities.h"
#include "gabble/caps-channel-manager.h"
#include "gabble/plugin-connection.h"
#include "caps-hash.h"
#include "auth-manager.h"
#include "conn-aliasing.h"
#include "conn-avatars.h"
#include "conn-client-types.h"
#include "conn-contact-info.h"
#include "conn-location.h"
#include "conn-presence.h"
#include "conn-sidecars.h"
#include "conn-mail-notif.h"
#include "conn-olpc.h"
#include "conn-power-saving.h"
#include "debug.h"
#include "disco.h"
#include "im-factory.h"
#include "legacy-caps.h"
#include "muc-factory.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "presence.h"
#include "request-pipeline.h"
#include "roomlist-manager.h"
#include "roster.h"
#include "search-manager.h"
#include "server-tls-channel.h"
#include "server-tls-manager.h"
#include "plugin-loader.h"
#include "private-tubes-factory.h"
#include "util.h"
#include "vcard-manager.h"
#include "conn-util.h"
#include "conn-addressing.h"

#ifdef ENABLE_VOIP
#include "media-channel.h"
#include "media-factory.h"
#endif

static guint disco_reply_timeout = 5;

#define DISCONNECT_TIMEOUT 5

static void capabilities_service_iface_init (gpointer, gpointer);
static void gabble_conn_contact_caps_iface_init (gpointer, gpointer);
static void conn_capabilities_fill_contact_attributes (GObject *obj,
  const GArray *contacts, GHashTable *attributes_hash);
static void conn_contact_capabilities_fill_contact_attributes (GObject *obj,
  const GArray *contacts, GHashTable *attributes_hash);
static void gabble_plugin_connection_iface_init (
    GabblePluginConnectionInterface *iface,
    gpointer conn);
static gchar *_gabble_plugin_connection_get_full_jid (
    GabblePluginConnection *conn);
static TpBaseContactList *_gabble_plugin_connection_get_contact_list (
    GabblePluginConnection *conn);

G_DEFINE_TYPE_WITH_CODE(GabbleConnection,
    gabble_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
      conn_aliasing_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS,
      conn_avatars_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_INFO,
      conn_contact_info_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CAPABILITIES,
      capabilities_service_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
       tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACTS,
      tp_contacts_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_LIST,
      tp_base_contact_list_mixin_list_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_GROUPS,
      tp_base_contact_list_mixin_groups_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_BLOCKING,
      tp_base_contact_list_mixin_blocking_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
      tp_presence_mixin_simple_presence_iface_init);
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
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CONNECTION_FUTURE,
      conn_future_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_MAIL_NOTIFICATION,
      conn_mail_notif_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CLIENT_TYPES,
      conn_client_types_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_POWER_SAVING,
      conn_power_saving_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_SVC_CONNECTION_INTERFACE_ADDRESSING,
      conn_addressing_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_PLUGIN_CONNECTION,
      gabble_plugin_connection_iface_init);
    )

/* properties */
enum
{
    PROP_CONNECT_SERVER = 1,
    PROP_EXPLICIT_SERVER,
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
    PROP_FALLBACK_SERVERS,
    PROP_EXTRA_CERTIFICATE_IDENTITIES,
    PROP_POWER_SAVING,
    PROP_DOWNLOAD_AT_CONNECTION,
    PROP_MESSAGE_CARBONS,
    PROP_SEND_CHAT_MARKERS,
    PROP_FORCE_CHAT_MARKERS,
    PROP_FORCE_RECEIPTS,

    LAST_PROPERTY
};

/* private structure */

struct _GabbleConnectionPrivate
{
  WockyConnector *connector;
  WockyPorter *porter;
  WockyPing *pinger;

  GCancellable *cancellable;

  /* connection properties */
  gchar *connect_server;
  gchar *explicit_server;
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

  GStrv fallback_servers;
  guint fallback_server_index;

  GStrv extra_certificate_identities;

  gboolean power_saving;

  gboolean message_carbons;

  gboolean send_chat_markers;
  gboolean force_chat_markers;
  gboolean force_receipts;

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
  /* Last activity time for XEP-0012 purposes, where "activity" is defined to
   * mean "sending a message".
   */
  time_t last_activity_time;

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
  /* sidecar caps set by gabble_connection_update_sidecar_capabilities */
  GabbleCapabilitySet *sidecar_caps;
  /* caps provided via ContactCapabilities.UpdateCapabilities ()
   * gchar * (client name) => GabbleCapabilitySet * */
  GHashTable *client_caps;
  /* the union of the above */
  GabbleCapabilitySet *all_caps;

  /* data forms provided via UpdateCapabilities()
   * gchar * (client name) => GPtrArray<owned WockyDataForm> */
  GHashTable *client_data_forms;

  /* auth manager */
  GabbleAuthManager *auth_manager;

  /* server TLS manager */
  GabbleServerTLSManager *server_tls_manager;

  /* IM factory; we need this to add text caps for everyone (even
   * offline contacts) which is done through the IM factory's
   * GabbleCapsChannelManagerInterface->get_contact_caps function. */
  GabbleImFactory *im_factory;

  /* stream id returned by the connector */
  gchar *stream_id;

  /* timer used when trying to properly disconnect */
  guint disconnect_timer;

  /* Number of things we are waiting for before changing the connection status
   * to connected */
  guint waiting_connected;

  /* Used to cancel pending calls to _gabble_connection_send_with_reply(). It
   * should not be necessary because by the time we get to cancelling this (in
   * our dispose()) the porter should be long dead and have called back for all
   * outstanding requests. However... call this paranoia, and a desire to
   * delete the Loudmouth compatibility layer without spending any more hours
   * untangling even more old code.
   */
  GCancellable *iq_reply_cancellable;

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

static void
add_to_array (gpointer data,
    gpointer user_data)
{
  g_ptr_array_add (user_data, data);
}

static GPtrArray *
_gabble_connection_create_channel_managers (TpBaseConnection *conn)
{
  GabbleConnection *self = GABBLE_CONNECTION (conn);
  GabblePluginConnection *plugin_connection = GABBLE_PLUGIN_CONNECTION (self);
  GPtrArray *channel_managers = g_ptr_array_sized_new (10);
  GabblePluginLoader *loader;
  GPtrArray *tmp;

  self->roster = gabble_roster_new (self);
  g_signal_connect (self->roster, "nicknames-update", G_CALLBACK
      (gabble_conn_aliasing_nicknames_updated), self);
  g_ptr_array_add (channel_managers, self->roster);

  self->priv->im_factory = g_object_new (GABBLE_TYPE_IM_FACTORY,
      "connection", self,
      NULL);
  g_ptr_array_add (channel_managers, self->priv->im_factory);

  g_ptr_array_add (channel_managers,
      g_object_new (GABBLE_TYPE_ROOMLIST_MANAGER,
        "connection", self,
        NULL));

  g_ptr_array_add (channel_managers,
      g_object_new (GABBLE_TYPE_SEARCH_MANAGER,
        "connection", self,
        NULL));

  self->priv->auth_manager = g_object_new (GABBLE_TYPE_AUTH_MANAGER,
      "connection", self, NULL);
  g_ptr_array_add (channel_managers, self->priv->auth_manager);

  self->priv->server_tls_manager =
    g_object_new (GABBLE_TYPE_SERVER_TLS_MANAGER,
        "connection", self,
        NULL);
  g_ptr_array_add (channel_managers, self->priv->server_tls_manager);

  self->muc_factory = g_object_new (GABBLE_TYPE_MUC_FACTORY,
      "connection", self,
      NULL);
  g_ptr_array_add (channel_managers, self->muc_factory);

  self->private_tubes_factory = gabble_private_tubes_factory_new (self);
  g_ptr_array_add (channel_managers, self->private_tubes_factory);

#ifdef ENABLE_VOIP
  self->jingle_mint = gabble_jingle_mint_new (self);

  g_ptr_array_add (channel_managers,
      g_object_new (GABBLE_TYPE_MEDIA_FACTORY,
        "connection", self,
        NULL));
#endif

#ifdef ENABLE_FILE_TRANSFER
  self->ft_manager = gabble_ft_manager_new (self);
  g_ptr_array_add (channel_managers, self->ft_manager);
#endif

  /* plugin channel managers */
  loader = gabble_plugin_loader_dup ();
  tmp = gabble_plugin_loader_create_channel_managers (loader,
      plugin_connection);
  g_object_unref (loader);

  g_ptr_array_foreach (tmp, add_to_array, channel_managers);
  g_ptr_array_unref (tmp);

  return channel_managers;
}

static void
gabble_plugin_connection_iface_init (
    GabblePluginConnectionInterface *iface,
    gpointer conn)
{
  iface->add_sidecar_own_caps = gabble_connection_add_sidecar_own_caps;
  iface->add_sidecar_own_caps_full=
    gabble_connection_add_sidecar_own_caps_full;
  iface->get_session = gabble_connection_get_session;
  iface->get_full_jid = _gabble_plugin_connection_get_full_jid;
  iface->get_jid_for_caps = gabble_connection_get_jid_for_caps;
  iface->pick_best_resource_for_caps =
    gabble_connection_pick_best_resource_for_caps;
  iface->get_contact_list = _gabble_plugin_connection_get_contact_list;
  iface->get_caps = gabble_connection_get_caps;
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
  TpBaseConnection *base = TP_BASE_CONNECTION (self);

  DEBUG("Post-construction: (GabbleConnection *)%p", self);

  tp_base_connection_add_possible_client_interest (base,
      TP_IFACE_QUARK_CONNECTION_INTERFACE_MAIL_NOTIFICATION);

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

  tp_contacts_mixin_init (G_OBJECT (self),
      G_STRUCT_OFFSET (GabbleConnection, contacts));

  tp_base_connection_register_with_contacts_mixin (base);
  tp_base_contact_list_mixin_register_with_contacts_mixin (base);

  conn_aliasing_init (self);
  conn_avatars_init (self);
  conn_contact_info_init (self);
  conn_presence_init (self);
  conn_olpc_activity_properties_init (self);
  conn_location_init (self);
  conn_sidecars_init (self);
  conn_mail_notif_init (self);
  conn_client_types_init (self);
  conn_addressing_init (self);

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
  priv->sidecar_caps = gabble_capability_set_new ();
  priv->client_caps = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) gabble_capability_set_free);

  priv->client_data_forms = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) g_ptr_array_unref);

  /* Historically, the optional Jingle transports were in our initial
   * presence, but could be removed by AdvertiseCapabilities(). Emulate
   * that here for now. */
  priv->bonus_caps = gabble_capability_set_new ();
#ifdef ENABLE_VOIP
  gabble_capability_set_add (priv->bonus_caps, NS_GOOGLE_TRANSPORT_P2P);
  gabble_capability_set_add (priv->bonus_caps, NS_JINGLE_TRANSPORT_ICEUDP);
#endif

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
      GABBLE_PRESENCE_AVAILABLE, NULL, priv->priority, NULL, time (NULL));
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
  priv->iq_reply_cancellable = g_cancellable_new ();

  priv->caps_serial = 1;
  priv->last_activity_time = time (NULL);
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
    case PROP_EXPLICIT_SERVER:
      g_value_set_string (value, priv->explicit_server);
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
      g_value_set_schar (value, priv->priority);
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

    case PROP_FALLBACK_SERVERS:
      g_value_set_boxed (value, priv->fallback_servers);
      break;

    case PROP_EXTRA_CERTIFICATE_IDENTITIES:
      g_value_set_boxed (value, priv->extra_certificate_identities);
      break;

    case PROP_POWER_SAVING:
      g_value_set_boolean (value, priv->power_saving);
      break;

    case PROP_DOWNLOAD_AT_CONNECTION:
      {
        gboolean download_at_connection = TRUE;
        if (self->roster != NULL)
          {
            g_object_get (self->roster,
                "download-at-connection", &download_at_connection,
                NULL);
          }
        else
          {
            g_warn_if_reached ();
          }
        g_value_set_boolean (value, download_at_connection);
        break;
      }

    case PROP_MESSAGE_CARBONS:
      g_value_set_boolean (value, priv->message_carbons);
      break;

    case PROP_SEND_CHAT_MARKERS:
      g_value_set_boolean (value, priv->send_chat_markers);
      break;

    case PROP_FORCE_CHAT_MARKERS:
      g_value_set_boolean (value, priv->force_chat_markers);
      break;

    case PROP_FORCE_RECEIPTS:
      g_value_set_boolean (value, priv->force_receipts);
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
    case PROP_EXPLICIT_SERVER:
      g_free (priv->explicit_server);
      priv->explicit_server = g_value_dup_string (value);
      if (priv->connect_server == NULL)
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
          time_t now = time (NULL);

          priv->resource = new_resource;

          /* Add self presence for new resource... */
          gabble_presence_update (self->self_presence, new_resource,
              self->self_presence->status, self->self_presence->status_message,
              priv->priority, NULL, now);
          /* ...and remove it for the old one. */
          gabble_presence_update (self->self_presence, old_resource,
              GABBLE_PRESENCE_OFFLINE, NULL, 0, NULL, now);

          g_free (old_resource);
        }
      break;
    case PROP_PRIORITY:
      priv->priority = g_value_get_schar (value);
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
      if (priv->pinger != NULL)
        g_object_set (priv->pinger, "ping-interval",
            priv->keepalive_interval, NULL);
      break;

    case PROP_DECLOAK_AUTOMATICALLY:
      priv->decloak_automatically = g_value_get_boolean (value);
      break;

    case PROP_FALLBACK_SERVERS:
      if (priv->fallback_servers != NULL)
        g_strfreev (priv->fallback_servers);
      priv->fallback_servers = g_value_dup_boxed (value);
      priv->fallback_server_index = 0;
      break;

    case PROP_EXTRA_CERTIFICATE_IDENTITIES:
      if (priv->extra_certificate_identities != NULL)
        g_strfreev (priv->extra_certificate_identities);
      priv->extra_certificate_identities = g_value_dup_boxed (value);
      break;

    case PROP_POWER_SAVING:
      priv->power_saving = g_value_get_boolean (value);
      break;

    case PROP_DOWNLOAD_AT_CONNECTION:
      /* Connection parameters are set by TpBaseProtocolClass->new_connection
       * after the channel managers are created. So at this step self->roster
       * is not NULL and we pass the property.
       */
      if (self->roster != NULL)
        g_object_set (self->roster, "download-at-connection",
            g_value_get_boolean (value), NULL);
      break;

    case PROP_MESSAGE_CARBONS:
      priv->message_carbons = g_value_get_boolean (value);
      break;

    case PROP_SEND_CHAT_MARKERS:
      priv->send_chat_markers = g_value_get_boolean (value);
      break;

    case PROP_FORCE_CHAT_MARKERS:
      priv->force_chat_markers = g_value_get_boolean (value);
      break;

    case PROP_FORCE_RECEIPTS:
      priv->force_receipts = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_connection_dispose (GObject *object);
static void gabble_connection_finalize (GObject *object);
static void connection_shut_down (TpBaseConnection *base);
static gboolean _gabble_connection_connect (TpBaseConnection *base,
    GError **error);

static gchar *
gabble_connection_get_unique_name (TpBaseConnection *self)
{
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION (self)->priv;
  gchar *unique_name = gabble_encode_jid (priv->username, priv->stream_server,
      priv->resource);

  if (priv->username == NULL)
    {
      gchar *tmp = unique_name;
      unique_name = g_strdup_printf ("%s_%p", tmp, self);
      g_free (tmp);
    }

  return unique_name;
}

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
}

#define TWICE(x) (x), (x)

static const gchar *implemented_interfaces[] = {
    /* conditionally present interfaces */
    TP_IFACE_CONNECTION_INTERFACE_MAIL_NOTIFICATION,
    GABBLE_IFACE_OLPC_ACTIVITY_PROPERTIES,
    GABBLE_IFACE_OLPC_BUDDY_INFO,

    /* always present interfaces */
    TP_IFACE_CONNECTION_INTERFACE_POWER_SAVING,
    TP_IFACE_CONNECTION_INTERFACE_ALIASING,
    TP_IFACE_CONNECTION_INTERFACE_CAPABILITIES,
    TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
    TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
    TP_IFACE_CONNECTION_INTERFACE_AVATARS,
    TP_IFACE_CONNECTION_INTERFACE_CONTACT_INFO,
    TP_IFACE_CONNECTION_INTERFACE_CONTACTS,
    TP_IFACE_CONNECTION_INTERFACE_CONTACT_LIST,
    TP_IFACE_CONNECTION_INTERFACE_CONTACT_GROUPS,
    TP_IFACE_CONNECTION_INTERFACE_REQUESTS,
    TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES,
    TP_IFACE_CONNECTION_INTERFACE_LOCATION,
    GABBLE_IFACE_CONNECTION_INTERFACE_GABBLE_DECLOAK,
    GABBLE_IFACE_CONNECTION_FUTURE,
    TP_IFACE_CONNECTION_INTERFACE_CLIENT_TYPES,
    GABBLE_IFACE_CONNECTION_INTERFACE_ADDRESSING,
    NULL
};
static const gchar **interfaces_always_present = implemented_interfaces + 3;

const gchar **
gabble_connection_get_implemented_interfaces (void)
{
    return implemented_interfaces;
}

const gchar **
gabble_connection_get_guaranteed_interfaces (void)
{
    return interfaces_always_present;
}

static GPtrArray *
_gabble_connection_get_interfaces_always_present (TpBaseConnection *base)
{
  GPtrArray *interfaces;
  const gchar **iter;

  interfaces = TP_BASE_CONNECTION_CLASS (
      gabble_connection_parent_class)->get_interfaces_always_present (base);

  for (iter = interfaces_always_present; *iter != NULL; iter++)
    g_ptr_array_add (interfaces, (gchar *) *iter);

  return interfaces;
}

static void
gabble_connection_class_init (GabbleConnectionClass *gabble_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_connection_class);
  TpBaseConnectionClass *parent_class = TP_BASE_CONNECTION_CLASS (
      gabble_connection_class);
  static TpDBusPropertiesMixinPropImpl location_props[] = {
        { "LocationAccessControlTypes", NULL, NULL },
        { "LocationAccessControl", NULL, NULL },
        { "SupportedLocationFeatures", NULL, NULL },
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
  static TpDBusPropertiesMixinPropImpl power_saving_props[] = {
        { "PowerSavingActive", "power-saving", NULL },
        { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
        /* 0 */ { TP_IFACE_CONNECTION_INTERFACE_LOCATION,
          conn_location_properties_getter,
          conn_location_properties_setter,
          location_props,
        },
        /* 1 */ { TP_IFACE_CONNECTION_INTERFACE_AVATARS,
          conn_avatars_properties_getter,
          NULL,
          NULL,
        },
        /* 2 */ { TP_IFACE_CONNECTION_INTERFACE_CONTACT_INFO,
          conn_contact_info_properties_getter,
          NULL,
          NULL,
        },
        /* 3 */ { GABBLE_IFACE_CONNECTION_INTERFACE_GABBLE_DECLOAK,
          tp_dbus_properties_mixin_getter_gobject_properties,
          tp_dbus_properties_mixin_setter_gobject_properties,
          decloak_props,
        },
        { TP_IFACE_CONNECTION_INTERFACE_MAIL_NOTIFICATION,
          conn_mail_notif_properties_getter,
          NULL,
          mail_notif_props,
        },
        { TP_IFACE_CONNECTION_INTERFACE_POWER_SAVING,
          tp_dbus_properties_mixin_getter_gobject_properties,
          NULL,
          power_saving_props,
        },
        { NULL }
  };

  prop_interfaces[1].props = conn_avatars_properties;
  prop_interfaces[2].props = conn_contact_info_properties;

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
  parent_class->shut_down = connection_shut_down;
  parent_class->start_connecting = _gabble_connection_connect;
  parent_class->get_interfaces_always_present =
      _gabble_connection_get_interfaces_always_present;

  g_type_class_add_private (gabble_connection_class,
      sizeof (GabbleConnectionPrivate));

  object_class->dispose = gabble_connection_dispose;
  object_class->finalize = gabble_connection_finalize;

  g_object_class_install_property (object_class, PROP_CONNECT_SERVER,
      g_param_spec_string (
          "connect-server", "Hostname or IP of Jabber server",
          "The server used when establishing a connection.",
          NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /*
   * The explicit-server property can be used for verification of a
   * server certificate. It's important that it comes from the user
   * and is not the result of any other outside lookup, unlike the
   * connect-server property.
   */

  g_object_class_install_property (object_class, PROP_EXPLICIT_SERVER,
      g_param_spec_string (
          "explicit-server", "Explicit Hostname or IP of Jabber server",
          "Server explicitly specified by the user to connect to.",
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

  g_object_class_install_property (
      object_class, PROP_DOWNLOAD_AT_CONNECTION,
      g_param_spec_boolean (
          "download-roster-at-connection",
          "Download the contact list at connection?",
          "Download the contact list at connection?",
          TRUE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FALLBACK_SERVERS,
      g_param_spec_boxed (
        "fallback-servers", "Fallback servers",
        "List of servers to fallback to (syntax server[:port][,oldssl]",
        G_TYPE_STRV,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_EXTRA_CERTIFICATE_IDENTITIES,
      g_param_spec_boxed (
        "extra-certificate-identities",
        "Extra Certificate Reference Identities",
        "Extra identities to check certificate against. These are present as a "
        "result of a user choice or configuration.",
        G_TYPE_STRV,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (
      object_class, PROP_POWER_SAVING,
      g_param_spec_boolean (
          "power-saving", "Power saving active?",
          "Queue remote presence updates server-side for less network chatter",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (
      object_class, PROP_MESSAGE_CARBONS,
      g_param_spec_boolean (
          "message-carbons", "Message carbons enabled?",
          "Client will receive other active resources messages",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (
      object_class, PROP_SEND_CHAT_MARKERS,
      g_param_spec_boolean (
          "send-chat-markers", "Send message read markers?",
          "Client will send read markers to sender",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (
      object_class, PROP_FORCE_CHAT_MARKERS,
      g_param_spec_boolean (
          "force-chat-markers", "Always send message markers?",
          "Client will send read markers and requests regardless of support",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (
      object_class, PROP_FORCE_RECEIPTS,
      g_param_spec_boolean (
          "force-receipts", "Always send message receipts?",
          "Client will send receipts and requests regardless of support",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gabble_connection_class->properties_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleConnectionClass, properties_class));

  tp_contacts_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleConnectionClass, contacts_class));

  conn_presence_class_init (gabble_connection_class);

  conn_contact_info_class_init (gabble_connection_class);

  tp_base_contact_list_mixin_class_init (parent_class);
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

  g_assert ((tp_base_connection_get_status (base) ==
          TP_CONNECTION_STATUS_DISCONNECTED) ||
      (tp_base_connection_get_status (base) ==
          TP_INTERNAL_CONNECTION_STATUS_NEW));

  tp_clear_object (&self->bytestream_factory);
  tp_clear_object (&self->disco);
  tp_clear_object (&self->req_pipeline);
  tp_clear_object (&self->vcard_manager);
#ifdef ENABLE_VOIP
  tp_clear_object (&self->jingle_mint);
#endif

  /* remove borrowed references before TpBaseConnection unrefs the channel
   * factories */
  self->roster = NULL;
  self->muc_factory = NULL;
  self->private_tubes_factory = NULL;
  priv->auth_manager = NULL;

  tp_clear_object (&self->self_presence);
  tp_clear_object (&self->presence_cache);

  conn_olpc_activity_properties_dispose (self);

  g_hash_table_unref (self->avatar_requests);
  g_hash_table_unref (self->vcard_requests);

  conn_presence_dispose (self);

  conn_mail_notif_dispose (self);

  tp_clear_object (&priv->connector);
  tp_clear_object (&self->session);

  /* The porter was borrowed from the session. */
  priv->porter = NULL;
  /* Cancel any outstanding _gabble_connection_send_with_reply() requests. */
  g_cancellable_cancel (priv->iq_reply_cancellable);
  tp_clear_object (&priv->iq_reply_cancellable);

  g_hash_table_unref (priv->client_caps);
  gabble_capability_set_free (priv->all_caps);
  gabble_capability_set_free (priv->notify_caps);
  gabble_capability_set_free (priv->legacy_caps);
  gabble_capability_set_free (priv->sidecar_caps);
  gabble_capability_set_free (priv->bonus_caps);

  g_hash_table_unref (priv->client_data_forms);

  if (priv->disconnect_timer != 0)
    {
      g_source_remove (priv->disconnect_timer);
      priv->disconnect_timer = 0;
    }

  tp_clear_object (&self->pep_location);
  tp_clear_object (&self->pep_nick);
  tp_clear_object (&self->pep_olpc_buddy_props);
  tp_clear_object (&self->pep_olpc_activities);
  tp_clear_object (&self->pep_olpc_current_act);
  tp_clear_object (&self->pep_olpc_act_props);

  conn_sidecars_dispose (self);

  tp_clear_object (&self->daemon);

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
  g_free (priv->explicit_server);
  g_free (priv->stream_server);
  g_free (priv->username);
  g_free (priv->password);
  g_free (priv->resource);

  g_free (priv->https_proxy_server);
  g_free (priv->stun_server);
  g_free (priv->fallback_stun_server);
  g_free (priv->fallback_conference_server);
  g_strfreev (priv->fallback_socks5_proxies);
  g_strfreev (priv->fallback_servers);
  g_strfreev (priv->extra_certificate_identities);

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
  char *username, *server, *resource;
  gboolean result;

  g_assert (GABBLE_IS_CONNECTION (conn));
  g_assert (account != NULL);

  username = server = resource = NULL;
  result = TRUE;

  if (!wocky_decode_jid (account, &username, &server, &resource))
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "unable to extract JID from account name");
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
  const gchar *bare_jid = conn_util_get_bare_self_jid (conn);

  return g_strconcat (bare_jid, "/", conn->priv->resource, NULL);
}

static gchar *
_gabble_plugin_connection_get_full_jid (GabblePluginConnection *plugin_conn)
{
  GabbleConnection *conn = GABBLE_CONNECTION (plugin_conn);
  return gabble_connection_get_full_jid (conn);
}

/**
 * gabble_connection_dup_porter:
 *
 * Returns: (transfer full): the #WockyPorter instance driving this connection.
 */

WockyPorter *gabble_connection_dup_porter (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv;

  g_assert (GABBLE_IS_CONNECTION (conn));

  priv = conn->priv;

  if (priv->porter != NULL)
    return g_object_ref (priv->porter);

  return NULL;
}

WockySession *
gabble_connection_get_session (GabblePluginConnection *plugin_connection)
{
  GabbleConnection *connection = GABBLE_CONNECTION (plugin_connection);

  g_return_val_if_fail (GABBLE_IS_CONNECTION (connection), NULL);

  return connection->session;
}

/**
 * _gabble_connection_send
 *
 * Send an WockyStanza and trap network errors appropriately.
 */
gboolean
_gabble_connection_send (GabbleConnection *conn, WockyStanza *msg, GError **error)
{
  g_assert (GABBLE_IS_CONNECTION (conn));

  if (conn->priv->porter == NULL)
    {
      g_set_error_literal (error, TP_ERROR, TP_ERROR_NETWORK_ERROR,
              "connection is disconnected");
      return FALSE;
    }

  wocky_porter_send (conn->priv->porter, msg);
  return TRUE;
}

void
gabble_connection_update_last_use (GabbleConnection *conn)
{
  conn->priv->last_activity_time = time (NULL);
}

static gdouble
gabble_connection_get_last_use (GabbleConnection *conn)
{
  return difftime (time (NULL), conn->priv->last_activity_time);
}

typedef struct {
    GabbleConnectionMsgReplyFunc reply_func;

    GabbleConnection *conn;
    WockyStanza *sent_msg;
    gpointer user_data;

    GObject *object;
    gboolean object_alive;
} GabbleMsgHandlerData;

static void
message_send_object_destroy_notify_cb (gpointer data,
                                       GObject *where_the_object_was)
{
  GabbleMsgHandlerData *handler_data = data;

  handler_data->object = NULL;
  handler_data->object_alive = FALSE;
}

static void
msg_handler_data_free (GabbleMsgHandlerData *handler_data)
{
  g_object_unref (handler_data->sent_msg);

  if (handler_data->object != NULL)
    {
      g_object_weak_unref (handler_data->object,
                           message_send_object_destroy_notify_cb,
                           handler_data);
    }

  g_slice_free (GabbleMsgHandlerData, handler_data);
}

static void
message_send_reply_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleMsgHandlerData *handler_data = user_data;

  if (handler_data->object_alive && handler_data->reply_func != NULL)
    {
      WockyPorter *porter = WOCKY_PORTER (source);
      GError *error = NULL;
      WockyStanza *stanza = wocky_porter_send_iq_finish (porter, result, &error);

      if (stanza != NULL)
        {
          handler_data->reply_func (handler_data->conn,
                                    handler_data->sent_msg,
                                    stanza,
                                    handler_data->object,
                                    handler_data->user_data);
          g_object_unref (stanza);
        }
      else
        {
          DEBUG ("send_iq_async failed: %s", error->message);
          g_error_free (error);
        }
    }

  msg_handler_data_free (handler_data);
}

/**
 * _gabble_connection_send_with_reply
 *
 * Send a tracked WockyStanza and trap network errors appropriately.
 *
 * If object is non-NULL the handler will follow the lifetime of that object,
 * which means that if the object is destroyed the callback will not be invoked.
 *
 * if reply_func is NULL the reply will be ignored.
 */
gboolean
_gabble_connection_send_with_reply (GabbleConnection *conn,
                                    WockyStanza *msg,
                                    GabbleConnectionMsgReplyFunc reply_func,
                                    GObject *object,
                                    gpointer user_data,
                                    GError **error)
{
  GabbleConnectionPrivate *priv;
  GabbleMsgHandlerData *handler_data;

  g_assert (GABBLE_IS_CONNECTION (conn));
  priv = conn->priv;

  if (priv->porter == NULL)
    {
      g_set_error_literal (error, TP_ERROR, TP_ERROR_NETWORK_ERROR,
              "connection is disconnected");
      return FALSE;
    }

  g_object_ref (msg);

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

  wocky_porter_send_iq_async (priv->porter, msg,
      priv->iq_reply_cancellable, message_send_reply_cb, handler_data);

  return TRUE;
}

static void connect_iq_callbacks (GabbleConnection *conn);
static gboolean iq_disco_cb (WockyPorter *, WockyStanza *, gpointer);
static gboolean iq_version_cb (WockyPorter *, WockyStanza *, gpointer);
static void connection_disco_cb (GabbleDisco *, GabbleDiscoRequest *,
    const gchar *, const gchar *, WockyNode *, GError *, gpointer);
static void decrement_waiting_connected (GabbleConnection *connection);
static void connection_initial_presence_cb (GObject *, GAsyncResult *,
    gpointer);

static void
gabble_connection_disconnect_with_tp_error (GabbleConnection *self,
    const GError *tp_error,
    TpConnectionStatusReason reason)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  GHashTable *details = tp_asv_new (
      "debug-message", G_TYPE_STRING, tp_error->message,
      NULL);

  g_assert (tp_error->domain == TP_ERROR);
  tp_base_connection_disconnect_with_dbus_error (base,
      tp_error_get_dbus_name (tp_error->code), details, reason);
  g_hash_table_unref (details);
}

static void
remote_closed_cb (WockyPorter *porter,
    GabbleConnection *self)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  GError e = { TP_ERROR, TP_ERROR_CONNECTION_LOST,
      "server closed its XMPP stream" };

  if (tp_base_connection_get_status (base) == TP_CONNECTION_STATUS_DISCONNECTED)
    /* Ignore if we are already disconnecting/disconnected */
    return;

  DEBUG ("server closed its XMPP stream; close ours");

  /* Changing the state to Disconnect will call connection_shut_down which
   * will properly close the porter. */
  gabble_connection_disconnect_with_tp_error (self, &e,
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
  GError e = { domain, code, msg };
  GError *error = NULL;

  if (tp_base_connection_get_status (base) == TP_CONNECTION_STATUS_DISCONNECTED)
    /* Ignore if we are already disconnecting/disconnected */
    return;

  gabble_set_tp_conn_error_from_wocky (&e, tp_base_connection_get_status (base),
      &reason, &error);
  g_assert (error->domain == TP_ERROR);

  DEBUG ("Force closing of the connection %p", self);
  priv->closing = TRUE;
  wocky_porter_force_close_async (priv->porter, NULL, force_close_cb,
      self);

  gabble_connection_disconnect_with_tp_error (self, error, reason);
  g_error_free (error);
}

static void
connector_error_disconnect (GabbleConnection *self,
    GError *error)
{
  GError *tp_error = NULL;
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpConnectionStatusReason reason = TP_CONNECTION_STATUS_REASON_NETWORK_ERROR;
  gchar *dbus_error = NULL;
  GHashTable *details = NULL;

  if (error->domain == GABBLE_SERVER_TLS_ERROR)
    {
      gabble_server_tls_manager_get_rejection_details (
          self->priv->server_tls_manager, &dbus_error, &details, &reason);

      /* new-style interactive TLS verification error */
      DEBUG ("New-style TLS verification error, reason %u, dbus error %s",
          reason, dbus_error);

      tp_base_connection_disconnect_with_dbus_error (base, dbus_error,
          details, reason);

      tp_clear_pointer (&details, g_hash_table_unref);
      g_free (dbus_error);

      return;
    }

  if (error->domain == WOCKY_AUTH_ERROR &&
      gabble_auth_manager_get_failure_details (self->priv->auth_manager,
        &dbus_error, &details, &reason))
    {
      DEBUG ("Interactive authentication error, reason %u, dbus error %s",
          reason, dbus_error);

      tp_base_connection_disconnect_with_dbus_error (base, dbus_error,
          details, reason);

      tp_clear_pointer (&details, g_hash_table_unref);
      g_free (dbus_error);
      return;
    }

  gabble_set_tp_conn_error_from_wocky (error,
      tp_base_connection_get_status (base), &reason, &tp_error);
  DEBUG ("connection failed: %s", tp_error->message);
  g_assert (tp_error->domain == TP_ERROR);

  gabble_connection_disconnect_with_tp_error (self, tp_error, reason);
  g_error_free (tp_error);
}

static void
bare_jid_disco_cb (GabbleDisco *disco,
    GabbleDiscoRequest *request,
    const gchar *jid,
    const gchar *node,
    WockyNode *result,
    GError *disco_error,
    gpointer user_data)
{
  GabbleConnection *conn = user_data;

  if (disco_error != NULL)
    {
      DEBUG ("Got disco error on bare jid: %s", disco_error->message);
    }
  else
    {
      WockyNodeIter i;
      WockyNode *child;

      wocky_node_iter_init (&i, result, "identity", NULL);
      while (wocky_node_iter_next (&i, &child))
        {
          const gchar *category = wocky_node_get_attribute (child,
              "category");
          const gchar *type = wocky_node_get_attribute (child, "type");

          if (!tp_strdiff (category, "pubsub") &&
              !tp_strdiff (type, "pep"))
            {
              DEBUG ("Server advertises PEP support in our jid features");
              conn->features |= GABBLE_CONNECTION_FEATURES_PEP;
            }
        }
    }

  decrement_waiting_connected (conn);
}

/**
 * next_fallback_server
 *
 * Launch connection using next fallback server if any, return
 * FALSE if they all have been tried or the error is not
 * network related (e.g. login failed).
 */
static gboolean
next_fallback_server (GabbleConnection *self,
    const GError *error)
{
  GabbleConnectionPrivate *priv = self->priv;
  const gchar *server;
  gchar **split;
  guint port = 5222;
  gboolean old_ssl = FALSE;
  GNetworkAddress *addr;
  GError *tmp_error = NULL;

  /* Only retry on connection failures */
  if (error->domain != G_IO_ERROR)
    return FALSE;

  if (priv->fallback_servers == NULL
      || priv->fallback_servers[priv->fallback_server_index] == NULL)
    return FALSE;

  server = priv->fallback_servers[priv->fallback_server_index++];
  split = g_strsplit (server, ",", 2);

  if (split[0] == NULL)
    {
      g_strfreev (split);
      return FALSE;
    }
  else if (split[1] != NULL && !tp_strdiff (split[1], "oldssl"))
    {
      old_ssl = TRUE;
      port = 5223;
    }

  addr = G_NETWORK_ADDRESS (
      g_network_address_parse (split[0], port, &tmp_error));

  if (!addr)
    {
      g_warning ("%s", tmp_error->message);
      g_error_free (tmp_error);
      return FALSE;
    }

  g_free (priv->connect_server);
  priv->connect_server = g_strdup (g_network_address_get_hostname (addr));
  priv->port = g_network_address_get_port (addr);
  priv->old_ssl = old_ssl;
  g_object_notify (G_OBJECT (self), "connect-server");
  g_object_notify (G_OBJECT (self), "port");
  g_object_notify (G_OBJECT (self), "old-ssl");

  g_object_unref (addr);

  _gabble_connection_connect (TP_BASE_CONNECTION (self), NULL);

  g_strfreev (split);
  return TRUE;
}

/**
 * connector_connected
 *
 * Stage 2 of connecting, this function is called once the connect operation
 * has finished. It checks if the connection succeeded, creates and starts
 * the WockyPorter, then sends two discovery requests to find the
 * server's features (one to the server and one to our bare jid).
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

  /* cleanup the cancellable */
  tp_clear_object (&priv->cancellable);

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
  tp_clear_object (&priv->connector);

  if (conn == NULL)
    {
      if (!next_fallback_server (self, error))
        connector_error_disconnect (self, error);
      g_error_free (error);
      return;
    }

  DEBUG ("connected (jid: %s)", jid);

  self->session = wocky_session_new_with_connection (conn, jid);
  priv->porter = wocky_session_get_porter (self->session);

  g_assert (WOCKY_IS_C2S_PORTER (priv->porter));
  priv->pinger = wocky_ping_new (WOCKY_C2S_PORTER (priv->porter),
      priv->keepalive_interval);

  g_signal_connect (priv->porter, "remote-closed",
      G_CALLBACK (remote_closed_cb), self);
  g_signal_connect (priv->porter, "remote-error",
      G_CALLBACK (remote_error_cb), self);

  g_signal_emit_by_name (self, "porter-available", priv->porter);
  connect_iq_callbacks (self);

  wocky_pep_service_start (self->pep_location, self->session);
  wocky_pep_service_start (self->pep_nick, self->session);
  wocky_pep_service_start (self->pep_olpc_buddy_props, self->session);
  wocky_pep_service_start (self->pep_olpc_activities, self->session);
  wocky_pep_service_start (self->pep_olpc_current_act, self->session);
  wocky_pep_service_start (self->pep_olpc_act_props, self->session);

  /* Don't use wocky_session_start as we don't want to start all the
   * components (Roster, presence-manager, etc) for now */
  wocky_porter_start (priv->porter);

  tp_base_connection_set_self_handle (base,
      tp_handle_ensure (contact_handles, jid, NULL, &error));

  if (tp_base_connection_get_self_handle (base) == 0)
    {
      DEBUG ("couldn't get our self handle: %s", error->message);

      if (error->domain != TP_ERROR)
        {
          error->domain = TP_ERROR;
          error->code = TP_ERROR_INVALID_HANDLE;
        }

      gabble_connection_disconnect_with_tp_error (self, error,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
      g_error_free (error);

      return;
    }

  /* update priv->resource and priv->stream_server from the server's JID */
  if (!_gabble_connection_set_properties_from_account (self, jid, &error))
    {
      DEBUG ("couldn't parse our own JID: %s", error->message);

      if (error->domain != TP_ERROR)
        {
          error->domain = TP_ERROR;
          error->code = TP_ERROR_INVALID_ARGUMENT;
        }

      gabble_connection_disconnect_with_tp_error (self, error,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
      g_error_free (error);

      return;
    }

  DEBUG ("Created self handle %d, our JID is %s",
      tp_base_connection_get_self_handle (base), jid);

  /* set initial capabilities */
  gabble_connection_refresh_capabilities (self, NULL);

  /* Disco server features */
  if (!gabble_disco_request_with_timeout (self->disco, GABBLE_DISCO_TYPE_INFO,
                                          priv->stream_server, NULL,
                                          disco_reply_timeout,
                                          connection_disco_cb, self,
                                          G_OBJECT (self), &error))
    {
      DEBUG ("sending disco request failed: %s",
          error->message);

      if (error->domain != TP_ERROR)
        {
          error->domain = TP_ERROR;
          error->code = TP_ERROR_NETWORK_ERROR;
        }

      gabble_connection_disconnect_with_tp_error (self, error,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
      g_error_free (error);
    }

  /* Disco our own bare jid to check if PEP is supported */
  if (!gabble_disco_request_with_timeout (self->disco, GABBLE_DISCO_TYPE_INFO,
          conn_util_get_bare_self_jid (self), NULL, disco_reply_timeout,
          bare_jid_disco_cb, self, G_OBJECT (self), &error))
    {
      DEBUG ("Sending disco request to our own bare jid failed: %s",
          error->message);

      if (error->domain != TP_ERROR)
        {
          error->domain = TP_ERROR;
          error->code = TP_ERROR_NETWORK_ERROR;
        }

      gabble_connection_disconnect_with_tp_error (self, error,
          TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
      g_error_free (error);
    }

  self->priv->waiting_connected = 2;
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

  conn = wocky_connector_connect_finish (WOCKY_CONNECTOR (source), res, &jid,
      &(priv->stream_id), &error);

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

  conn = wocky_connector_register_finish (WOCKY_CONNECTOR (source), res,
      &jid, &(priv->stream_id), &error);

  connector_connected (self, conn, jid, error);
  g_free (jid);
}

static gboolean
connection_iq_last_cb (
    WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (user_data);
  const gchar *from = wocky_stanza_get_from (stanza);
  /* Aside from 21 being an appropriate number, 2 ^ 64 is 20 digits long. */
  char seconds[21];

  /* Check if the peer, if any, is authorized to receive our presence. */
  if (from != NULL)
    {
      TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) self, TP_HANDLE_TYPE_CONTACT);
      TpHandle handle = tp_handle_lookup (contact_repo, from, NULL, NULL);

      /* If there's no handle for them, they're certainly not on the roster. */
      if (handle == 0 ||
          !gabble_roster_handle_gets_presence_from_us (self->roster, handle))
        {
          wocky_porter_send_iq_error (porter, stanza,
              WOCKY_XMPP_ERROR_FORBIDDEN, NULL);
          return TRUE;
        }
    }

  sprintf (seconds, "%.0f", gabble_connection_get_last_use (self));
  wocky_porter_acknowledge_iq (porter, stanza,
      '(', "query", ':', NS_LAST, '@', "seconds", seconds, ')',
      NULL);
  return TRUE;
}

static void
connect_iq_callbacks (GabbleConnection *conn)
{
  GabbleConnectionPrivate *priv = conn->priv;

  wocky_porter_register_handler_from_anyone (priv->porter,
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
      iq_disco_cb, conn,
      '(', "query", ':', NS_DISCO_INFO, ')', NULL);

  wocky_porter_register_handler_from_anyone (priv->porter,
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
      iq_version_cb, conn,
      '(', "query", ':', NS_VERSION, ')', NULL);

  wocky_porter_register_handler_from_anyone (priv->porter,
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL,
      connection_iq_last_cb, conn,
      '(', "query", ':', NS_LAST, ')', NULL);
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
 * Stage 3 is connection_disco_cb processing the server's features, and setting
 *            initial presence
 * Stage 4 is set_status_to_connected setting the CONNECTED state.
 */
static gboolean
_gabble_connection_connect (TpBaseConnection *base,
                            GError **error)
{
  GabbleConnection *conn = GABBLE_CONNECTION (base);
  GabbleConnectionPrivate *priv = conn->priv;
  WockyTLSHandler *tls_handler;
  char *jid;
  gboolean interactive_tls;
  gchar *user_certs_dir;

  g_assert (priv->connector == NULL);
  g_assert (priv->port <= G_MAXUINT16);
  g_assert (priv->stream_server != NULL);
  g_assert (priv->resource != NULL);

  jid = gabble_encode_jid (priv->username, priv->stream_server, NULL);
  tls_handler = WOCKY_TLS_HANDLER (priv->server_tls_manager);
  priv->connector = wocky_connector_new (jid, priv->password, priv->resource,
      WOCKY_AUTH_REGISTRY (priv->auth_manager),
      tls_handler);
  g_free (jid);

#ifdef GTLS_SYSTEM_CA_CERTIFICATES
  /* system certs */
  wocky_tls_handler_add_ca (tls_handler, GTLS_SYSTEM_CA_CERTIFICATES);
#endif

  /* user certs */
  user_certs_dir = g_build_filename (g_get_user_config_dir (),
      "telepathy", "certs", NULL);
  wocky_tls_handler_add_ca (tls_handler, user_certs_dir);
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

  /* We want to enable interactive TLS verification also in
   * case encryption is not required, and we don't ignore SSL errors.
   */
  interactive_tls = !conn->priv->ignore_ssl_errors;

  if (!conn->priv->require_encryption && !conn->priv->ignore_ssl_errors)
    {
      DEBUG ("require-encryption is False; flipping ignore_ssl_errors to True");
      conn->priv->ignore_ssl_errors = TRUE;
    }

  g_object_set (priv->connector,
      "old-ssl", priv->old_ssl,
      /* We always wants to support old servers */
      "legacy", TRUE,
      NULL);

  g_object_set (tls_handler,
      "interactive-tls", interactive_tls,
      "ignore-ssl-errors", priv->ignore_ssl_errors,
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

  priv->cancellable = g_cancellable_new ();

  if (priv->do_register)
    {
      DEBUG ("Start registering");

      wocky_connector_register_async (priv->connector, priv->cancellable,
          connector_register_cb, conn);
    }
  else
    {
      DEBUG ("Start connecting");

      wocky_connector_connect_async (priv->connector, priv->cancellable,
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

  tp_clear_object (&priv->pinger);

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
      DEBUG ("Cancelling the porter connection");
      g_cancellable_cancel (priv->cancellable);

      return;
    }

  DEBUG ("neither porter nor connector is alive: clean up the base connection");
  tp_base_connection_finish_shutdown (base);
}

void
gabble_connection_fill_in_caps (GabbleConnection *self,
    WockyStanza *presence_message)
{
  GabblePresence *presence = self->self_presence;
  WockyNode *node = wocky_stanza_get_top_node (presence_message);
  gchar *caps_hash;
  gboolean share_v1, voice_v1, video_v1;
  GString *ext = g_string_new ("");

  /* XEP-0115 version 1.5 uses a verification string in the 'ver' attribute */
  caps_hash = caps_hash_compute_from_self_presence (self);
  node = wocky_node_add_child_ns (node, "c", NS_CAPS);
  wocky_node_set_attributes (
    node,
    "hash",  "sha-1",
    "node",  NS_GABBLE_CAPS,
    "ver",   caps_hash,
    NULL);

  /* Ensure this set of capabilities is in the cache. */
  gabble_presence_cache_add_own_caps (self->presence_cache, caps_hash,
      gabble_presence_peek_caps (presence), NULL,
      gabble_presence_peek_data_forms (presence));

  /* XEP-0115 deprecates 'ext' feature bundles. But we still need
   * BUNDLE_VOICE_V1 it for backward-compatibility with Gabble 0.2 */

  g_string_append (ext, BUNDLE_PMUC_V1);

  share_v1 = gabble_presence_has_cap (presence, NS_GOOGLE_FEAT_SHARE);
  voice_v1 = gabble_presence_has_cap (presence, NS_GOOGLE_FEAT_VOICE);
  video_v1 = gabble_presence_has_cap (presence, NS_GOOGLE_FEAT_VIDEO);

  if (share_v1)
    g_string_append (ext, " " BUNDLE_SHARE_V1);

  if (voice_v1)
    g_string_append (ext, " " BUNDLE_VOICE_V1);

  if (video_v1)
    g_string_append (ext, " " BUNDLE_VIDEO_V1);

  if (gabble_presence_has_cap (presence, NS_GOOGLE_FEAT_CAMERA))
    g_string_append (ext, " " BUNDLE_CAMERA_V1);

  wocky_node_set_attribute (node, "ext", ext->str);
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
  WockyStanza *message;
  gboolean ret;
  TpHandle handle;

  /* if we don't have a handle allocated for the recipient, they clearly aren't
   * getting our presence... */
  handle = tp_handle_lookup (contact_repo, recipient, NULL, NULL);

  if (handle != 0 && conn_presence_visible_to (self, handle))
    {
      /* nothing to do, they should already have had our presence */
      return TRUE;
    }

  /* We deliberately don't include anything except the caps here */
  message = wocky_stanza_build (
      WOCKY_STANZA_TYPE_PRESENCE, WOCKY_STANZA_SUB_TYPE_AVAILABLE,
      NULL, recipient,
      NULL);

  gabble_connection_fill_in_caps (self, message);

  ret = _gabble_connection_send (self, message, error);

  g_object_unref (message);

  return ret;
}

gboolean
gabble_connection_request_decloak (GabbleConnection *self,
    const gchar *to,
    const gchar *reason,
    GError **error)
{
  GabblePresence *presence = self->self_presence;
  WockyStanza *message = gabble_presence_as_message (presence, to);
  WockyNode *decloak;
  gboolean ret;

  gabble_connection_fill_in_caps (self, message);

  decloak = wocky_node_add_child_ns (wocky_stanza_get_top_node (message),
      "temppres", NS_TEMPPRES);

  if (reason != NULL && *reason != '\0')
    {
      wocky_node_set_attribute (decloak, "reason", reason);
    }

  ret = _gabble_connection_send (self, message, error);
  g_object_unref (message);

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
  GPtrArray *data_forms;

  save_set = self->priv->all_caps;
  self->priv->all_caps = gabble_capability_set_new ();

  data_forms = g_ptr_array_new ();

  gabble_capability_set_update (self->priv->all_caps,
      gabble_capabilities_get_fixed_caps ());
  gabble_capability_set_update (self->priv->all_caps, self->priv->notify_caps);
  gabble_capability_set_update (self->priv->all_caps, self->priv->legacy_caps);
  gabble_capability_set_update (self->priv->all_caps, self->priv->sidecar_caps);
  gabble_capability_set_update (self->priv->all_caps, self->priv->bonus_caps);

  /* first, normal caps */
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

  /* now data forms */
  g_hash_table_iter_init (&iter, self->priv->client_data_forms);

  /* just borrow the ref, data_forms doesn't have a free func */
  while (g_hash_table_iter_next (&iter, &k, &v))
    tp_g_ptr_array_extend (data_forms, v);

  if (self->self_presence != NULL)
    gabble_presence_set_capabilities (self->self_presence,
        self->priv->resource, self->priv->all_caps, data_forms,
        self->priv->caps_serial++);

  if (gabble_capability_set_equals (self->priv->all_caps, save_set))
    {
      gabble_capability_set_free (save_set);
      g_ptr_array_unref (data_forms);
      DEBUG ("nothing to do");
      return FALSE;
    }

  /* don't signal presence unless we're already CONNECTED */
  if (tp_base_connection_get_status (base) != TP_CONNECTION_STATUS_CONNECTED)
    {
      gabble_capability_set_free (save_set);
      g_ptr_array_unref (data_forms);
      DEBUG ("not emitting self-presence stanza: not connected yet");
      return FALSE;
    }

  if (!conn_presence_signal_own_presence (self, NULL, &error))
    {
      gabble_capability_set_free (save_set);
      g_ptr_array_unref (data_forms);
      DEBUG ("error sending presence: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  if (old_out == NULL)
    gabble_capability_set_free (save_set);
  else
    *old_out = save_set;

  g_ptr_array_unref (data_forms);

  return TRUE;
}

/**
 * _gabble_connection_send_iq_result
 *
 * Function used to acknowledge an IQ stanza.
 */
void
_gabble_connection_acknowledge_set_iq (GabbleConnection *conn,
                                       WockyStanza *iq)
{
  wocky_porter_acknowledge_iq (wocky_session_get_porter (conn->session),
      iq, NULL);
}

static void
add_feature_node (gpointer namespace,
    gpointer result_query)
{
  WockyNode *feature_node;

  feature_node = wocky_node_add_child_with_content (result_query, "feature",
      NULL);
  wocky_node_set_attribute (feature_node, "var", namespace);
}

static void
add_identity_node (const GabbleDiscoIdentity *identity,
    gpointer result_query)
{
  WockyNode *identity_node;

  identity_node = wocky_node_add_child_with_content
      (result_query, "identity", NULL);
  wocky_node_set_attribute (identity_node, "category", identity->category);
  wocky_node_set_attribute (identity_node, "type", identity->type);
  if (identity->lang)
    wocky_node_set_attribute (identity_node, "lang", identity->lang);
  if (identity->name)
    wocky_node_set_attribute (identity_node, "name", identity->name);
}

/**
 * iq_disco_cb
 *
 * Called by Wocky when we get an incoming <iq> with a <query xmlns="disco#info">
 * node. This handler handles disco-related IQs.
 */
static gboolean
iq_disco_cb (WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (user_data);
  WockyStanza *result;
  WockyNode *query, *result_query;
  const gchar *node, *suffix;
  const GabbleCapabilityInfo *info = NULL;
  const GabbleCapabilitySet *features = NULL;
  const GPtrArray *identities = NULL;
  const GPtrArray *data_forms = NULL;

  /* query's existence is checked by WockyPorter before this function is called */
  query = wocky_node_get_child (wocky_stanza_get_top_node (stanza), "query");
  node = wocky_node_get_attribute (query, "node");

  if (node && (
      0 != strncmp (node, NS_GABBLE_CAPS "#", strlen (NS_GABBLE_CAPS) + 1) ||
      strlen (node) < strlen (NS_GABBLE_CAPS) + 2))
    {
      STANZA_DEBUG (stanza, "got iq disco query with unexpected node attribute");
      return FALSE;
    }

  if (node == NULL)
    suffix = NULL;
  else
    suffix = node + strlen (NS_GABBLE_CAPS) + 1;

  result = wocky_stanza_build_iq_result (stanza,
      '(', "query", ':', NS_DISCO_INFO, '*', &result_query, ')', NULL);

  /* If we get an IQ without an id='', there's not much we can do. */
  if (result == NULL)
    return FALSE;

  if (node)
    wocky_node_set_attribute (result_query, "node", node);

  if (node == NULL)
    {
      features = gabble_presence_peek_caps (self->self_presence);
      data_forms = gabble_presence_peek_data_forms (self->self_presence);
    }
  else
    {
      /* If node is not NULL, it can be either a caps bundle as defined in the
       * legacy XEP-0115 version 1.3 or an hash as defined in XEP-0115 version
       * 1.5. Let's see if it's a verification string we've told the cache about.
       */
      info = gabble_presence_cache_peek_own_caps (self->presence_cache,
          suffix);
    }

  if (info)
    {
      features = info->cap_set;
      identities = info->identities;
      data_forms = info->data_forms;
    }

  if (identities && identities->len != 0)
    {
      g_ptr_array_foreach ((GPtrArray *) identities,
          (GFunc) add_identity_node, result_query);
    }
  else
    {
      /* Every entity MUST have at least one identity (XEP-0030). Gabble publishes
       * one identity. If you change the identity here, you also need to change
       * caps_hash_compute_from_self_presence(). */
      wocky_node_add_build (result_query,
        '(', "identity",
          '@', "category", "client",
          '@', "name", PACKAGE_STRING,
          '@', "type", CLIENT_TYPE,
        ')', NULL);
    }

  if (features == NULL)
    {
      /* Otherwise, is it one of the caps bundles we advertise? These are not
       * just shoved into the cache with gabble_presence_cache_add_own_caps()
       * because capabilities_get_features() always includes a few bonus
       * features...
       */

      if (!tp_strdiff (suffix, BUNDLE_SHARE_V1))
        features = gabble_capabilities_get_bundle_share_v1 ();

      if (!tp_strdiff (suffix, BUNDLE_VOICE_V1))
        features = gabble_capabilities_get_bundle_voice_v1 ();

      if (!tp_strdiff (suffix, BUNDLE_VIDEO_V1))
        features = gabble_capabilities_get_bundle_video_v1 ();

      if (!tp_strdiff (suffix, BUNDLE_CAMERA_V1))
        features = gabble_capabilities_get_bundle_camera_v1 ();
    }

  if (data_forms != NULL)
    {
      guint i;

      for (i = 0; i < data_forms->len; i++)
        {
          WockyDataForm *form = g_ptr_array_index (data_forms, i);

          wocky_data_form_add_to_node (form, result_query);
        }
    }

  if (features == NULL && tp_strdiff (suffix, BUNDLE_PMUC_V1))
    {
      wocky_porter_send_iq_error (porter, stanza,
          WOCKY_XMPP_ERROR_ITEM_NOT_FOUND, NULL);
    }
  else
    {
      /* Send an empty reply for a pmuc-v1 disco, matching Google's behaviour. */
      if (features != NULL)
        {
          gabble_capability_set_foreach (features, add_feature_node,
              result_query);
        }

      wocky_porter_send (self->priv->porter, result);
    }

  g_object_unref (result);

  return TRUE;
}

static gboolean
iq_version_cb (WockyPorter *porter, WockyStanza *stanza, gpointer user_data)
{
  WockyStanza *result;

  result = wocky_stanza_build_iq_result (stanza,
      '(', "query", ':', NS_VERSION,
        '(', "name", '$', PACKAGE_NAME, ')',
        '(', "version", '$', PACKAGE_VERSION, ')',
      ')', NULL);

  if (result == NULL)
    return FALSE;

  wocky_porter_send (porter, result);

  g_object_unref ((GObject *) result);

  return TRUE;
}

/**
 * set_status_to_connected
 *
 * Stage 4 of connecting, this function is called once all the events we were
 * waiting for happened.
 *
 */
static void
set_status_to_connected (GabbleConnection *conn)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;

  if (tp_base_connection_get_status (base) == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      /* We already failed to connect, but at the time an async thing was
       * still pending, and now it has finished. Do nothing special. */
      return;
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
         { TP_IFACE_CONNECTION_INTERFACE_MAIL_NOTIFICATION, NULL };

      tp_base_connection_add_interfaces ((TpBaseConnection *) conn, ifaces);
    }

  if (tp_base_contact_list_can_block (gabble_connection_get_contact_list (conn)))
    {
      const gchar *ifaces[] =
        { TP_IFACE_CONNECTION_INTERFACE_CONTACT_BLOCKING, NULL };

      tp_base_connection_add_interfaces ((TpBaseConnection *) conn, ifaces);
    }

  /* go go gadget on-line */
  tp_base_connection_change_status (base,
      TP_CONNECTION_STATUS_CONNECTED, TP_CONNECTION_STATUS_REASON_REQUESTED);
}

static void
decrement_waiting_connected (GabbleConnection *conn)
{
  conn->priv->waiting_connected--;

  if (conn->priv->waiting_connected == 0)
    set_status_to_connected (conn);
}

static void
conn_wlm_jid_lookup_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleConnection *conn = (GabbleConnection *) source;
  GSimpleAsyncResult *my_result = user_data;
  WockyStanza *iq = NULL;
  WockyNode *node;
  const gchar *jid = NULL;
  GError *error = NULL;

  if (!conn_util_send_iq_finish (conn, result, &iq, &error))
    {
      g_simple_async_result_take_error (my_result, error);
      goto out;
    }

  node = wocky_node_get_child_ns (wocky_stanza_get_top_node (iq),
      "getjid", NS_WLM_JID_LOOKUP);

  if (node != NULL)
    {
      jid = wocky_node_get_content_from_child (node, "jid");
    }

  if (!tp_str_empty (jid))
    {
      g_simple_async_result_set_op_res_gpointer (my_result,
          g_strdup (jid), g_free);
    }
  else
    {
      g_simple_async_result_set_error (my_result,
          TP_ERROR, TP_ERROR_INVALID_HANDLE,
          "jid not found in getjid reply");
    }

out:
  g_simple_async_result_complete (my_result);
  g_object_unref (my_result);
}

static void
conn_wlm_jid_lookup_async (TpHandleRepoIface *repo,
    TpBaseConnection *base,
    const gchar *id,
    gpointer context,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabbleConnection *self = GABBLE_CONNECTION (base);
  WockyStanza *iq;
  GSimpleAsyncResult *result;
  gchar *normal_id;
  GError *error = NULL;

  result = g_simple_async_result_new ((GObject *) repo, callback, user_data,
      conn_wlm_jid_lookup_async);

  /* First, do local normalization */
  normal_id = gabble_normalize_contact (repo, id, context, &error);
  if (normal_id == NULL)
    {
      g_simple_async_result_take_error (result, error);
      g_simple_async_result_complete_in_idle (result);
      g_object_unref (result);
      return;
    }

  /* If it is already a WLM jid, return it */
  if (g_str_has_suffix (normal_id, "@messenger.live.com"))
    {
      g_simple_async_result_set_op_res_gpointer (result, normal_id, g_free);
      g_simple_async_result_complete_in_idle (result);
      g_object_unref (result);
      return;
    }

  /* Ask the server to give a WLM jid */
  iq = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET,
      conn_util_get_bare_self_jid (self), normal_id,
      '(', "getjid",
        ':', NS_WLM_JID_LOOKUP,
      ')',
      NULL);

  conn_util_send_iq_async (self, iq, NULL, conn_wlm_jid_lookup_cb, result);

  g_object_unref (iq);
  g_free (normal_id);
}

static void
carbons_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GError *error = NULL;
  WockyPorter *porter = WOCKY_PORTER (source_object);
  WockyStanza *reply = wocky_porter_send_iq_finish (porter, res, &error);

  if (reply == NULL ||
      wocky_stanza_extract_errors (reply, NULL, &error, NULL, NULL))
    {
      DEBUG ("Failed to set carbons: %s", error->message);
      g_error_free (error);
    }

  tp_clear_object (&reply);
}

static gchar *
conn_wlm_jid_lookup_finish (TpHandleRepoIface *repo,
    GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple = (GSimpleAsyncResult *) result;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
      G_OBJECT (repo), conn_wlm_jid_lookup_async), NULL);

  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;

  return g_strdup (g_simple_async_result_get_op_res_gpointer (simple));
}

static void
connection_disco_cb (GabbleDisco *disco,
                     GabbleDiscoRequest *request,
                     const gchar *jid,
                     const gchar *node,
                     WockyNode *result,
                     GError *disco_error,
                     gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  TpBaseConnection *base = (TpBaseConnection *) conn;

  if (tp_base_connection_get_status (base) != TP_CONNECTION_STATUS_CONNECTING)
    {
      g_assert (tp_base_connection_get_status (base) ==
          TP_CONNECTION_STATUS_DISCONNECTED);
      return;
    }

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
      WockyNodeIter i;
      WockyNode *child;

      NODE_DEBUG (result, "got");

      wocky_node_iter_init (&i, result, NULL, NULL);
      while (wocky_node_iter_next (&i, &child))
        {
          if (0 == strcmp (child->name, "identity"))
            {
              const gchar *category = wocky_node_get_attribute (child,
                  "category");
              const gchar *type = wocky_node_get_attribute (child, "type");

              if (!tp_strdiff (category, "pubsub") &&
                  !tp_strdiff (type, "pep"))
                {
                  DEBUG ("Server advertises PEP support in its features");
                  conn->features |= GABBLE_CONNECTION_FEATURES_PEP;
                }
            }
          else if (0 == strcmp (child->name, "feature"))
            {
              const gchar *var = wocky_node_get_attribute (child, "var");

              if (var == NULL)
                continue;

              if (0 == strcmp (var, NS_GOOGLE_ROSTER))
                conn->features |= GABBLE_CONNECTION_FEATURES_GOOGLE_ROSTER;
#ifdef ENABLE_VOIP
              else if (0 == strcmp (var, NS_GOOGLE_JINGLE_INFO))
                conn->features |= GABBLE_CONNECTION_FEATURES_GOOGLE_JINGLE_INFO;
#endif
              else if (0 == strcmp (var, NS_PRESENCE_INVISIBLE))
                conn->features |= GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE;
              else if (0 == strcmp (var, NS_PRIVACY))
                conn->features |= GABBLE_CONNECTION_FEATURES_PRIVACY;
              else if (0 == strcmp (var, NS_INVISIBLE))
                conn->features |= GABBLE_CONNECTION_FEATURES_INVISIBLE;
              else if (0 == strcmp (var, NS_GOOGLE_MAIL_NOTIFY))
                conn->features |= GABBLE_CONNECTION_FEATURES_GOOGLE_MAIL_NOTIFY;
              else if (0 == strcmp (var, NS_GOOGLE_SHARED_STATUS))
                conn->features |= GABBLE_CONNECTION_FEATURES_GOOGLE_SHARED_STATUS;
              else if (0 == strcmp (var, NS_GOOGLE_QUEUE))
                conn->features |= GABBLE_CONNECTION_FEATURES_GOOGLE_QUEUE;
              else if (0 == strcmp (var, NS_GOOGLE_SETTING))
                conn->features |= GABBLE_CONNECTION_FEATURES_GOOGLE_SETTING;
              else if (0 == strcmp (var, NS_WLM_JID_LOOKUP))
                conn->features |= GABBLE_CONNECTION_FEATURES_WLM_JID_LOOKUP;
              else if (0 == strcmp (var, NS_CARBONS))
                conn->features |= GABBLE_CONNECTION_FEATURES_CARBONS;
            }
        }

      DEBUG ("set features flags to %d", conn->features);
    }

  if ((conn->features & GABBLE_CONNECTION_FEATURES_WLM_JID_LOOKUP) != 0)
    {
      TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base,
          TP_HANDLE_TYPE_CONTACT);

      tp_dynamic_handle_repo_set_normalize_async (
          (TpDynamicHandleRepo *) contact_repo,
          conn_wlm_jid_lookup_async,
          conn_wlm_jid_lookup_finish);
    }

  if ((conn->features & GABBLE_CONNECTION_FEATURES_CARBONS)
        && (conn->priv->message_carbons))
    {
      WockyStanza *query;
      WockyPorter *porter;
      gchar *full_jid;

      full_jid = gabble_connection_get_full_jid (conn);
      porter = wocky_session_get_porter (conn->session);
      query = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
                                  WOCKY_STANZA_SUB_TYPE_SET, full_jid, NULL,
                                  '(', "enable",
                                    ':', NS_CARBONS,
                                  ')',
                                  NULL);
      wocky_porter_send_iq_async (porter, query, NULL,
                                  carbons_cb, conn);

      g_object_unref (query);
      g_free (full_jid);
    }

  conn_presence_set_initial_presence_async (conn,
      connection_initial_presence_cb, NULL);

  return;

ERROR:
  tp_base_connection_change_status (base,
      TP_CONNECTION_STATUS_DISCONNECTED,
      TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

  return;
}

/**
 * connection_initial_presence_cb
 *
 * Stage 4 of connecting, this function is called after our initial presence
 * has been set (or has failed unrecoverably). It marks the connection as
 * online (or failed, as appropriate); the former triggers the roster being
 * retrieved.
 */
static void
connection_initial_presence_cb (GObject *source_object,
    GAsyncResult *res,
    gpointer user_data)
{
  GabbleConnection *self = (GabbleConnection *) source_object;
  TpBaseConnection *base = (TpBaseConnection *) self;
  GError *error = NULL;

  if (!conn_presence_set_initial_presence_finish (self, res, &error))
    {
      DEBUG ("error setting up initial presence: %s", error->message);

      if (tp_base_connection_get_status (base) !=
              TP_CONNECTION_STATUS_DISCONNECTED)
        tp_base_connection_change_status (base,
            TP_CONNECTION_STATUS_DISCONNECTED,
            TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);

      g_error_free (error);
    }
  else
    {
      decrement_waiting_connected (self);
    }
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
      if (GABBLE_IS_CAPS_CHANNEL_MANAGER (manager))
        {
          gabble_caps_channel_manager_get_contact_capabilities (
              GABBLE_CAPS_CHANNEL_MANAGER (manager), handle, caps, ret);
        }
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
  g_ptr_array_unref (caps_arr);

  /* o.f.T.C.ContactCapabilities */
  caps_arr = gabble_connection_build_contact_caps (conn, handle, new_set);

  hash = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) gabble_free_rcc_list);
  g_hash_table_insert (hash, GUINT_TO_POINTER (handle), caps_arr);

  tp_svc_connection_interface_contact_capabilities_emit_contact_capabilities_changed (
      conn, hash);

  g_hash_table_unref (hash);
}

static const GabbleCapabilitySet *
empty_caps_set (void)
{
  static GabbleCapabilitySet *empty = NULL;

  if (G_UNLIKELY (empty == NULL))
    empty = gabble_capability_set_new ();

  return empty;
}

/*
 * gabble_connection_get_handle_contact_capabilities:
 *
 * Returns: an array of channel classes representing @handle's capabilities
 */
static GPtrArray *
gabble_connection_get_handle_contact_capabilities (
    GabbleConnection *self,
    TpHandle handle)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  GabblePresence *p;
  const GabbleCapabilitySet *caps;

  if (handle == tp_base_connection_get_self_handle (base))
    p = self->self_presence;
  else
    p = gabble_presence_cache_get (self->presence_cache, handle);

  if (p == NULL)
    caps = empty_caps_set ();
  else
    caps = gabble_presence_peek_caps (p);

  return gabble_connection_build_contact_caps (self, handle, caps);
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
      _emit_capabilities_changed (self,
          tp_base_connection_get_self_handle (base), save_set, priv->all_caps);
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
  g_ptr_array_unref (ret);
}

static const gchar *
get_form_type (WockyDataForm *form)
{
  WockyDataFormField *field;

  field = g_hash_table_lookup (form->fields,
      "FORM_TYPE");
  g_assert (field != NULL);

  return field->raw_value_contents[0];
}

static const gchar *
check_form_is_unique (GabbleConnection *self,
    const gchar *form_type)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, self->priv->client_data_forms);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const gchar *manager = key;
      GPtrArray *data_forms = value;
      guint i;

      if (data_forms == NULL || data_forms->len == 0)
        continue;

      for (i = 0; i < data_forms->len; i++)
        {
          WockyDataForm *form = g_ptr_array_index (data_forms, i);

          if (!tp_strdiff (get_form_type (form), form_type))
            return manager;
        }
    }

  return NULL;
}

static gboolean
check_data_form_in_list (GPtrArray *array,
    const gchar *form_type)
{
  guint i;

  for (i = 0; i < array->len; i++)
    {
      WockyDataForm *form = g_ptr_array_index (array, i);

      if (!tp_strdiff (get_form_type (form), form_type))
        return TRUE;
    }

  return FALSE;
}

static gboolean
check_data_form_is_valid (GabbleConnection *self,
    WockyDataForm *form,
    GPtrArray *existing_forms)
{
  WockyDataFormField *field;
  const gchar *form_type, *other_client;

  /* We want rid of forms with no FORM_TYPE quickly. */
  field = g_hash_table_lookup (form->fields, "FORM_TYPE");

  if (field == NULL || tp_str_empty (field->raw_value_contents[0]))
    {
      WARNING ("data form with no FORM_TYPE field; ignoring");
      return FALSE;
    }

  form_type = field->raw_value_contents[0];

  /* We'll get warnings (potentially bad) if two clients cause a
   * channel manager to create two data forms with the same FORM_TYPE,
   * or if multiple channel managers create two data forms with the
   * same FORM_TYPE, for the same client. This is probably not a
   * problem in practice given hardly anyone uses data forms in entity
   * capabilities anyway.  */

  /* We don't want the same data form from another caps channel
   * manager for this client either */
  if (check_data_form_in_list (existing_forms, form_type))
    {
      WARNING ("duplicate data form '%s' from another channel "
          "manager; ignoring", form_type);
      return FALSE;
    }

  /* And lastly we don't want a form we're already advertising. */
  other_client = check_form_is_unique (self, form_type);
  if (other_client != NULL)
    {
      WARNING ("Data form '%s' already provided by client "
          "%s; ignoring", form_type, other_client);
      return FALSE;
    }

  return TRUE;
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
  GabbleCapabilitySet *old_caps = NULL;
  guint i;

  /* Now that someone has told us our *actual* capabilities, we can stop
   * advertising spurious caps in initial presence */
  gabble_capability_set_clear (self->priv->bonus_caps);

  DEBUG ("enter");

  for (i = 0; i < clients->len; i++)
    {
      GValueArray *va = g_ptr_array_index (clients, i);
      const gchar *client_name = g_value_get_string (va->values + 0);
      const GPtrArray *filters = g_value_get_boxed (va->values + 1);
      const gchar * const * cap_tokens = g_value_get_boxed (va->values + 2);
      GabbleCapabilitySet *cap_set;
      GPtrArray *data_forms;
      TpChannelManagerIter iter;
      TpChannelManager *manager;

      g_hash_table_remove (self->priv->client_caps, client_name);
      g_hash_table_remove (self->priv->client_data_forms, client_name);

      if ((cap_tokens == NULL || cap_tokens[0] == NULL) &&
          filters->len == 0)
        {
          /* no capabilities */
          DEBUG ("client %s can't do anything", client_name);
          continue;
        }

      cap_set = gabble_capability_set_new ();
      data_forms = g_ptr_array_new_with_free_func (
          (GDestroyNotify) g_object_unref);

      tp_base_connection_channel_manager_iter_init (&iter, base);

      while (tp_base_connection_channel_manager_iter_next (&iter, &manager))
        {
          if (GABBLE_IS_CAPS_CHANNEL_MANAGER (manager))
            {
              GPtrArray *forms = g_ptr_array_new_with_free_func (
                  (GDestroyNotify) g_object_unref);
              guint j;

              /* First, represent the client... */
              gabble_caps_channel_manager_represent_client (
                  GABBLE_CAPS_CHANNEL_MANAGER (manager), client_name, filters,
                  cap_tokens, cap_set, forms);

              /* Now check the forms... */
              for (j = 0; j < forms->len; j++)
                {
                  WockyDataForm *form = g_ptr_array_index (forms, j);

                  if (check_data_form_is_valid (self, form, data_forms))
                    g_ptr_array_add (data_forms, g_object_ref (form));
                }

              g_ptr_array_unref (forms);
            }
        }

      /* first deal with normal caps */
      if (gabble_capability_set_size (cap_set) > 0)
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
      else
        {
          DEBUG ("client %s has no interesting capabilities", client_name);
          gabble_capability_set_free (cap_set);
        }

      /* now data forms */
      if (data_forms->len > 0)
        {
          guint j;

          /* now print out what forms we have here */
          DEBUG ("client %s contributes %u data form%s:", client_name,
              data_forms->len,
              data_forms->len > 1 ? "s" : "");

          for (j = 0; j < data_forms->len; j++)
            DEBUG (" - %s", get_form_type (g_ptr_array_index (data_forms, j)));

          g_hash_table_insert (self->priv->client_data_forms,
              g_strdup (client_name), data_forms);
        }
      else
        {
          DEBUG ("client %s has no interesting data forms", client_name);
          g_ptr_array_unref (data_forms);
        }
    }

  if (gabble_connection_refresh_capabilities (self, &old_caps))
    {
      _emit_capabilities_changed (self,
          tp_base_connection_get_self_handle (base), old_caps,
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

  if (handle == tp_base_connection_get_self_handle (base))
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
      g_ptr_array_unref (array);
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
      GValue *val = tp_g_value_slice_new_take_boxed (
          TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST,
          gabble_connection_get_handle_contact_capabilities (self, handle));

      tp_contacts_mixin_set_contact_attribute (attributes_hash,
          handle,
          TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES"/capabilities",
          val);
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

  g_ptr_array_unref (ret);
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
      TpHandle handle = g_array_index (handles, TpHandle, i);
      GPtrArray *arr;

      arr = gabble_connection_get_handle_contact_capabilities (self, handle);
      g_hash_table_insert (ret, GUINT_TO_POINTER (handle), arr);
    }

  tp_svc_connection_interface_contact_capabilities_return_from_get_contact_capabilities
      (context, ret);

  g_hash_table_unref (ret);
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
                                 WockyStanzaSubType sub_type,
                                 const gchar *contact,
                                 const gchar *status,
                                 GError **error)
{
  WockyStanza *message;
  gboolean result;

  message = wocky_stanza_build (WOCKY_STANZA_TYPE_PRESENCE, sub_type,
      NULL, contact,
      NULL);

  if (WOCKY_STANZA_SUB_TYPE_SUBSCRIBE == sub_type)
    lm_message_node_add_own_nick (
        wocky_stanza_get_top_node (message), conn);

  if (!tp_str_empty (status))
    wocky_node_add_child_with_content (
        wocky_stanza_get_top_node (message), "status", status);

  result = _gabble_connection_send (conn, message, error);

  g_object_unref (message);

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

void
gabble_connection_update_sidecar_capabilities (GabbleConnection *self,
                                               const GabbleCapabilitySet *add_set,
                                               const GabbleCapabilitySet *remove_set)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (self);
  GabbleConnectionPrivate *priv = self->priv;
  GabbleCapabilitySet *save_set;

  if (add_set == NULL && remove_set == NULL)
    return;

  if (add_set != NULL)
    gabble_capability_set_update (priv->sidecar_caps, add_set);
  if (remove_set != NULL)
    gabble_capability_set_exclude (priv->sidecar_caps, remove_set);

  if (DEBUGGING)
    {
      if (add_set != NULL)
        {
          gchar *add_str = gabble_capability_set_dump (add_set, "  ");

          DEBUG ("sidecar caps to add:\n%s", add_str);
          g_free (add_str);
        }

      if (remove_set != NULL)
        {
          gchar *remove_str = gabble_capability_set_dump (remove_set, "  ");

          DEBUG ("sidecar caps to remove:\n%s", remove_str);
          g_free (remove_str);
        }
    }

  if (gabble_connection_refresh_capabilities (self, &save_set))
    {
      _emit_capabilities_changed (self,
          tp_base_connection_get_self_handle (base), save_set, priv->all_caps);
      gabble_capability_set_free (save_set);
    }
}

/* identities is actually a WockyDiscoIdentityArray */
gchar *
gabble_connection_add_sidecar_own_caps_full (GabblePluginConnection *self,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities,
    GPtrArray *data_forms)
{
  GabbleConnection *conn = GABBLE_CONNECTION (self);
  GPtrArray *identities_copy = ((identities == NULL) ?
      wocky_disco_identity_array_new () :
      wocky_disco_identity_array_copy (identities));
  gchar *ver;

  /* XEP-0030 requires at least 1 identity. We don't need more. */
  if (identities_copy->len == 0)
    g_ptr_array_add (identities_copy,
        wocky_disco_identity_new ("client", CLIENT_TYPE,
            NULL, PACKAGE_STRING));

  ver = gabble_caps_hash_compute_full (cap_set, identities_copy, data_forms);

  gabble_presence_cache_add_own_caps (conn->presence_cache, ver,
      cap_set, identities_copy, data_forms);

  wocky_disco_identity_array_free (identities_copy);

  return ver;
}

gchar *
gabble_connection_add_sidecar_own_caps (GabblePluginConnection *self,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities)
{
  return gabble_connection_add_sidecar_own_caps_full (self, cap_set,
      identities, NULL);
}

const gchar *
gabble_connection_get_jid_for_caps (GabblePluginConnection *plugin_conn,
    WockyXep0115Capabilities *caps)
{
  TpHandle handle;
  TpBaseConnection *base;
  TpHandleRepoIface *contact_handles;
  GabbleConnection *conn = GABBLE_CONNECTION (plugin_conn);

  g_return_val_if_fail (GABBLE_IS_CONNECTION (conn), NULL);
  g_return_val_if_fail (GABBLE_IS_PRESENCE (caps), NULL);

  base = (TpBaseConnection *) conn;

  if ((GabblePresence *) caps == conn->self_presence)
    {
      handle = tp_base_connection_get_self_handle (base);
    }
  else
    {
      handle = gabble_presence_cache_get_handle (conn->presence_cache,
          (GabblePresence *) caps);
    }

  contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  return tp_handle_inspect (contact_handles, handle);
}

const gchar *
gabble_connection_pick_best_resource_for_caps (
    GabblePluginConnection *plugin_connection,
    const gchar *jid,
    GabbleCapabilitySetPredicate predicate,
    gconstpointer user_data)
{
  TpBaseConnection *base;
  TpHandleRepoIface *contact_handles;
  TpHandle handle;
  GabbleConnection *connection = GABBLE_CONNECTION (plugin_connection);
  GabblePresence *presence;

  g_return_val_if_fail (GABBLE_IS_CONNECTION (connection), NULL);
  g_return_val_if_fail (!tp_str_empty (jid), NULL);

  base = (TpBaseConnection *) connection;
  contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  handle = tp_handle_ensure (contact_handles, jid,
      NULL, NULL);

  if (handle == 0)
    return NULL;

  presence = gabble_presence_cache_get (connection->presence_cache,
      handle);

  if (presence == NULL)
    return NULL;

  return gabble_presence_pick_resource_by_caps (presence, 0,
      predicate, user_data);
}

TpBaseContactList *
gabble_connection_get_contact_list (GabbleConnection *connection)
{
  g_return_val_if_fail (GABBLE_IS_CONNECTION (connection), NULL);

  return (TpBaseContactList *) connection->roster;
}

static TpBaseContactList *
_gabble_plugin_connection_get_contact_list (
    GabblePluginConnection *plugin_connection)
{
  GabbleConnection *connection = GABBLE_CONNECTION (plugin_connection);
  return gabble_connection_get_contact_list (connection);
}

WockyXep0115Capabilities *
gabble_connection_get_caps (GabblePluginConnection *plugin_connection,
    TpHandle handle)
{
  GabblePresence *presence;
  GabbleConnection *connection = GABBLE_CONNECTION (plugin_connection);

  g_return_val_if_fail (GABBLE_IS_CONNECTION (connection), NULL);
  g_return_val_if_fail (handle > 0, NULL);

  presence = gabble_presence_cache_get (connection->presence_cache,
      handle);

  return (WockyXep0115Capabilities *) presence;
}
