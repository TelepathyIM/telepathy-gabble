/*
 * jingle-mint.c - creates and configures a GabbleJingleFactory
 * Copyright ©2012 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 *
 *
 * "Mint" is intended in the manufacturing sense: a mint is a factory which
 * produces coins. <http://en.wikipedia.org/wiki/Mint_(coin)>. It was chosen
 * in favour of "factory" because this is a "factory factory"; and in favour of
 * "foundry" to make JingleFactory and this class have different initials.
 */

#include "jingle-mint.h"

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA
#include "debug.h"

#include "connection.h"
#include "conn-presence.h"
#include "jingle-factory.h"
#include "jingle-session.h"
#include "presence-cache.h"

struct _GabbleJingleMintPrivate {
    GabbleConnection *conn;

    GabbleJingleFactory *factory;
};

enum {
    INCOMING_SESSION = 0,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

enum
{
  PROP_CONNECTION = 1,
};

static void connection_status_changed_cb (
    GabbleConnection *conn,
    guint status,
    guint reason,
    gpointer user_data);
static void connection_porter_available_cb (
    GabbleConnection *conn,
    WockyPorter *porter,
    gpointer user_data);

static void factory_new_session_cb (
    GabbleJingleFactory *factory,
    GabbleJingleSession *session,
    gboolean initiated_locally,
    gpointer user_data);
static gboolean factory_query_cap_cb (
    GabbleJingleFactory *factory,
    WockyContact *contact,
    const gchar *cap_or_quirk,
    gpointer user_data);

G_DEFINE_TYPE (GabbleJingleMint, gabble_jingle_mint, G_TYPE_OBJECT)

static void
gabble_jingle_mint_init (GabbleJingleMint *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_JINGLE_MINT,
      GabbleJingleMintPrivate);
}

static void
gabble_jingle_mint_get_property (
    GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleJingleMint *self = GABBLE_JINGLE_MINT (object);
  GabbleJingleMintPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, priv->conn);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gabble_jingle_mint_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleJingleMint *self = GABBLE_JINGLE_MINT (object);
  GabbleJingleMintPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_CONNECTION:
        priv->conn = g_value_get_object (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gabble_jingle_mint_constructed (GObject *object)
{
  GabbleJingleMint *self = GABBLE_JINGLE_MINT (object);
  GabbleJingleMintPrivate *priv = self->priv;
  GObjectClass *parent_class = gabble_jingle_mint_parent_class;

  if (parent_class->constructed != NULL)
    parent_class->constructed (object);

  tp_g_signal_connect_object (priv->conn, "status-changed",
      (GCallback) connection_status_changed_cb, self, 0);
  tp_g_signal_connect_object (priv->conn, "porter-available",
      (GCallback) connection_porter_available_cb, self, 0);
}

static void
gabble_jingle_mint_dispose (GObject *object)
{
  GabbleJingleMint *self = GABBLE_JINGLE_MINT (object);
  GabbleJingleMintPrivate *priv = self->priv;
  GObjectClass *parent_class = gabble_jingle_mint_parent_class;

  g_clear_object (&priv->factory);

  if (parent_class->dispose != NULL)
    parent_class->dispose (object);
}

static void
gabble_jingle_mint_class_init (GabbleJingleMintClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  object_class->get_property = gabble_jingle_mint_get_property;
  object_class->set_property = gabble_jingle_mint_set_property;
  object_class->constructed = gabble_jingle_mint_constructed;
  object_class->dispose = gabble_jingle_mint_dispose;

  g_type_class_add_private (klass, sizeof (GabbleJingleMintPrivate));

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that uses this JingleMint object",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  signals[INCOMING_SESSION] = g_signal_new ("incoming-session",
        G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
        G_TYPE_NONE, 1, GABBLE_TYPE_JINGLE_SESSION);
}

GabbleJingleMint *
gabble_jingle_mint_new (
    GabbleConnection *connection)
{
  return g_object_new (GABBLE_TYPE_JINGLE_MINT,
      "connection", connection,
      NULL);
}

static void
connection_status_changed_cb (
    GabbleConnection *conn,
    guint status,
    guint reason,
    gpointer user_data)
{
  GabbleJingleMint *self = GABBLE_JINGLE_MINT (user_data);
  GabbleJingleMintPrivate *priv = self->priv;

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      g_assert (priv->conn != NULL);
      break;

    case TP_CONNECTION_STATUS_CONNECTED:
        {
          GabbleJingleInfo *info = gabble_jingle_mint_get_info (self);
          gchar *stun_server = NULL;
          guint stun_port = 0;

          g_object_get (priv->conn,
              "stun-server", &stun_server,
              "stun-port", &stun_port,
              NULL);

          if (stun_server != NULL)
            gabble_jingle_info_take_stun_server (info,
                stun_server, stun_port, FALSE);

          g_object_get (priv->conn,
              "fallback-stun-server", &stun_server,
              "fallback-stun-port", &stun_port,
              NULL);

          if (stun_server != NULL)
            gabble_jingle_info_take_stun_server (info,
                stun_server, stun_port, TRUE);

          if (priv->conn->features &
              GABBLE_CONNECTION_FEATURES_GOOGLE_JINGLE_INFO)
            {
              gabble_jingle_info_send_request (info);
            }
        }
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      if (priv->factory != NULL)
        gabble_jingle_factory_stop (priv->factory);
      break;
    }
}

static void
connection_porter_available_cb (
    GabbleConnection *conn,
    WockyPorter *porter,
    gpointer user_data)
{
  GabbleJingleMint *self = GABBLE_JINGLE_MINT (user_data);
  GabbleJingleMintPrivate *priv = self->priv;

  /* If we have a WockyPorter, we should definitely have a WockySession */
  g_assert (conn->session != NULL);

  g_assert (priv->factory == NULL);
  priv->factory = gabble_jingle_factory_new (conn->session);

  tp_g_signal_connect_object (priv->factory, "new-session",
      (GCallback) factory_new_session_cb, self, 0);
  tp_g_signal_connect_object (priv->factory, "query-cap",
      (GCallback) factory_query_cap_cb, self, 0);
}

static void
session_about_to_initiate_cb (
    GabbleJingleSession *session,
    gpointer user_data)
{
  GabbleJingleMint *self = GABBLE_JINGLE_MINT (user_data);
  GabbleJingleMintPrivate *priv = self->priv;
  const gchar *peer_jid = gabble_jingle_session_get_peer_jid (session);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle peer = tp_handle_ensure (contact_repo, peer_jid, NULL, NULL);

  /* send directed presence (including our own caps, avatar etc.) to
   * the peer, if we aren't already visible to them */
  if (!conn_presence_visible_to (priv->conn, peer))
    conn_presence_signal_own_presence (priv->conn, peer_jid, NULL);
}

static void
factory_new_session_cb (
    GabbleJingleFactory *factory,
    GabbleJingleSession *session,
    gboolean initiated_locally,
    gpointer user_data)
{
  GabbleJingleMint *self = GABBLE_JINGLE_MINT (user_data);

  if (initiated_locally)
    tp_g_signal_connect_object (session, "about-to-initiate",
        (GCallback) session_about_to_initiate_cb, self, 0);

  /* Proxy the signal outwards if this is a new incoming session. */
  if (!initiated_locally)
    g_signal_emit (self, signals[INCOMING_SESSION], 0, session);
}

static gboolean
factory_query_cap_cb (
    GabbleJingleFactory *factory,
    WockyContact *contact,
    const gchar *cap_or_quirk,
    gpointer user_data)
{
  GabbleJingleMint *self = GABBLE_JINGLE_MINT (user_data);
  GabbleJingleMintPrivate *priv = self->priv;
  GabblePresence *presence = gabble_presence_cache_get_for_contact (
      priv->conn->presence_cache, contact);

  if (presence == NULL)
    return FALSE;

  if (WOCKY_IS_RESOURCE_CONTACT (contact))
    {
      const gchar *peer_resource = wocky_resource_contact_get_resource (
          WOCKY_RESOURCE_CONTACT (contact));

      return gabble_presence_resource_has_caps (presence, peer_resource,
          gabble_capability_set_predicate_has, cap_or_quirk);
    }
  else
    {
      return gabble_presence_has_cap (presence, cap_or_quirk);
    }
}

GabbleJingleFactory *
gabble_jingle_mint_get_factory (
    GabbleJingleMint *self)
{
  return self->priv->factory;
}

GabbleJingleInfo *
gabble_jingle_mint_get_info (
    GabbleJingleMint *self)
{
  return gabble_jingle_factory_get_jingle_info (self->priv->factory);
}
