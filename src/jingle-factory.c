/*
 * jingle-factory.c - Support for XEP-0166 (Jingle)
 *
 * Copyright (C) 2006-2008 Collabora Ltd.
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

#include "jingle-factory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include <lib/gibber/gibber-resolver.h>
#include <loudmouth/loudmouth.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "debug.h"
#include "jingle-media-rtp.h"
#include "jingle-session.h"
#include "jingle-transport-google.h"
#include "namespaces.h"
#include "util.h"

G_DEFINE_TYPE(GabbleJingleFactory, gabble_jingle_factory, G_TYPE_OBJECT);

/* signal enum */
enum
{
    NEW_SESSION,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};


struct _GabbleJingleFactoryPrivate
{
  GabbleConnection *conn;
  LmMessageHandler *jingle_cb;
  LmMessageHandler *jingle_info_cb;
  GHashTable *content_types;
  GHashTable *transports;
  GHashTable *sessions;
  GibberResolver *resolver;

  gchar *stun_server;
  guint16 stun_port;
  gchar *relay_token;
  gboolean get_stun_from_jingle;
  gboolean dispose_has_run;
};

#define GABBLE_JINGLE_FACTORY_GET_PRIVATE(o) ((o)->priv)

static LmHandlerResult jingle_cb (LmMessageHandler *handler,
    LmConnection *lmconn, LmMessage *message, gpointer user_data);
static GabbleJingleSession *create_session (GabbleJingleFactory *fac,
    const gchar *sid, TpHandle peer, const gchar *peer_resource);

static void session_terminated_cb (GabbleJingleSession *sess,
    gboolean local_terminator, GabbleJingleFactory *fac);

static void connection_status_changed_cb (GabbleConnection *conn,
    guint status, guint reason, GabbleJingleFactory *self);

static void
gabble_jingle_factory_init (GabbleJingleFactory *obj)
{
  GabbleJingleFactoryPrivate *priv =
     G_TYPE_INSTANCE_GET_PRIVATE (obj, GABBLE_TYPE_JINGLE_FACTORY,
         GabbleJingleFactoryPrivate);
  obj->priv = priv;

  priv->sessions = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);

  priv->transports = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, NULL);

  priv->content_types = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, NULL);

  priv->jingle_cb = NULL;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
  priv->resolver = gibber_resolver_get_resolver ();
}

typedef struct {
    gchar *stun_server;
    guint16 stun_port;
} PendingStunServer;

static void
pending_stun_server_free (gpointer p)
{
  PendingStunServer *data = p;

  g_free (data->stun_server);
  g_slice_free (PendingStunServer, p);
}

static void
stun_server_resolved_cb (GibberResolver *resolver,
                         GList *entries,
                         GError *error,
                         gpointer user_data,
                         GObject *object)
{
  GabbleJingleFactory *self = GABBLE_JINGLE_FACTORY (object);
  PendingStunServer *data = user_data;
  GibberResolverAddrInfo *info;
  GError *e = NULL;
  gchar *stun_server;

  if (error != NULL)
    {
      DEBUG ("Failed to resolve STUN server %s:%u: %s",
          data->stun_server, data->stun_port, error->message);
      return;
    }

  if (entries == NULL)
    {
      DEBUG ("No results for STUN server %s:%u",
          data->stun_server, data->stun_port);
      return;
    }

  info = entries->data;

  if (!gibber_resolver_sockaddr_to_str ((struct sockaddr *) &(info->sockaddr),
        info->sockaddr_len, &stun_server, NULL, &e))
    {
      DEBUG ("Couldn't convert resolved address of %s to string: %s",
          data->stun_server, e->message);
      g_error_free (e);
      return;
    }

  DEBUG ("Resolved STUN server %s:%u to %s:%u", data->stun_server,
      data->stun_port, stun_server, data->stun_port);

  g_free (self->priv->stun_server);
  self->priv->stun_server = stun_server;
  self->priv->stun_port = data->stun_port;
}

static void
take_stun_server (GabbleJingleFactory *self,
                  gchar *stun_server,
                  guint16 stun_port)
{
  PendingStunServer *data = g_slice_new0 (PendingStunServer);

  if (stun_server == NULL)
    return;

  DEBUG ("Resolving STUN server %s:%u", stun_server, stun_port);
  data->stun_server = stun_server;
  data->stun_port = stun_port;

  gibber_resolver_addrinfo (self->priv->resolver, stun_server, NULL,
      AF_INET, SOCK_DGRAM, IPPROTO_UDP, 0,
      stun_server_resolved_cb, data, pending_stun_server_free,
      G_OBJECT (self));
}

/*
 * jingle_info_cb
 *
 * Called by loudmouth when we get an incoming <iq>. This handler
 * is concerned only with Jingle info queries.
 */
static LmHandlerResult
jingle_info_cb (LmMessageHandler *handler,
                LmConnection *lmconn,
                LmMessage *message,
                gpointer user_data)
{
  GabbleJingleFactory *fac = GABBLE_JINGLE_FACTORY (user_data);
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (fac);
  LmMessageSubType sub_type;
  LmMessageNode *query_node, *node;

  query_node = lm_message_node_get_child_with_namespace (message->node,
      "query", NS_GOOGLE_JINGLE_INFO);

  if (query_node == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  sub_type = lm_message_get_sub_type (message);

  if (sub_type == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      GabbleXmppError xmpp_error = XMPP_ERROR_UNDEFINED_CONDITION;

      node = lm_message_node_get_child (message->node, "error");
      if (node != NULL)
        {
          xmpp_error = gabble_xmpp_error_from_node (node, NULL);
        }

      DEBUG ("jingle info error: %s", gabble_xmpp_error_string (xmpp_error));

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  if (sub_type != LM_MESSAGE_SUB_TYPE_RESULT &&
      sub_type != LM_MESSAGE_SUB_TYPE_SET)
    {
      DEBUG ("jingle info: unexpected IQ type, ignoring");

      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (fac->priv->get_stun_from_jingle)
    node = lm_message_node_get_child (query_node, "stun");
  else
    node = NULL;

  if (node != NULL)
    {
      node = lm_message_node_get_child (node, "server");

      if (node != NULL)
        {
          const gchar *server;
          const gchar *port_attr;
          guint port = GABBLE_PARAMS_DEFAULT_STUN_PORT;

          server = lm_message_node_get_attribute (node, "host");
          port_attr = lm_message_node_get_attribute (node, "udp");

          if (port_attr != NULL)
            port = atoi (port_attr);

          if (server != NULL && port > 0 && port <= G_MAXUINT16)
            {
              DEBUG ("jingle info: got stun server %s, port %u", server,
                  port);
              take_stun_server (fac, g_strdup (server), port);
            }
        }
    }

  node = lm_message_node_get_child (query_node, "relay");

  if (node != NULL)
    {
      node = lm_message_node_get_child (node, "token");

      if (node != NULL)
        {
          const gchar *token;

          token = lm_message_node_get_value (node);

          if (token != NULL)
            {
              DEBUG ("jingle info: got relay token %s", token);
              g_free (fac->priv->relay_token);
              fac->priv->relay_token = g_strdup (token);
            }
        }
    }

  if (sub_type == LM_MESSAGE_SUB_TYPE_SET)
    {
      _gabble_connection_acknowledge_set_iq (priv->conn, message);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
jingle_info_send_request (GabbleJingleFactory *fac)
{
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (fac);
  TpBaseConnection *base = (TpBaseConnection *) priv->conn;
  LmMessage *msg;
  LmMessageNode *node;
  const gchar *jid;
  GError *error = NULL;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  jid = tp_handle_inspect (contact_handles, base->self_handle);
  msg = lm_message_new_with_sub_type (jid, LM_MESSAGE_TYPE_IQ,
      LM_MESSAGE_SUB_TYPE_GET);

  node = lm_message_node_add_child (msg->node, "query", NULL);
  lm_message_node_set_attribute (node, "xmlns", NS_GOOGLE_JINGLE_INFO);

  if (!_gabble_connection_send (priv->conn, msg, &error))
    {
      DEBUG ("jingle info send failed: %s\n", error->message);
      g_error_free (error);
    }

  lm_message_unref (msg);
}


static void
gabble_jingle_factory_dispose (GObject *object)
{
  GabbleJingleFactory *fac = GABBLE_JINGLE_FACTORY (object);
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  g_hash_table_destroy (priv->sessions);
  priv->sessions = NULL;

  g_hash_table_destroy (priv->content_types);
  priv->content_types = NULL;

  g_hash_table_destroy (priv->transports);
  priv->transports = NULL;

  g_free (fac->priv->stun_server);
  g_free (fac->priv->relay_token);

  if (G_OBJECT_CLASS (gabble_jingle_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_jingle_factory_parent_class)->dispose (object);
}

static void
gabble_jingle_factory_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  GabbleJingleFactory *chan = GABBLE_JINGLE_FACTORY (object);
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_jingle_factory_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  GabbleJingleFactory *chan = GABBLE_JINGLE_FACTORY (object);
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static GObject *
gabble_jingle_factory_constructor (GType type,
                                   guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabbleJingleFactory *self;
  GabbleJingleFactoryPrivate *priv;

  obj = G_OBJECT_CLASS (gabble_jingle_factory_parent_class)->
      constructor (type, n_props, props);

  self = GABBLE_JINGLE_FACTORY (obj);
  priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (self);

  /* FIXME: why was this in _constructed in media factory? */
  g_signal_connect (priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, self);

  jingle_media_rtp_register (self);
  jingle_transport_google_register (self);

  return obj;
}

static void
gabble_jingle_factory_class_init (GabbleJingleFactoryClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleJingleFactoryPrivate));

  object_class->constructor = gabble_jingle_factory_constructor;
  object_class->get_property = gabble_jingle_factory_get_property;
  object_class->set_property = gabble_jingle_factory_set_property;
  object_class->dispose = gabble_jingle_factory_dispose;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that uses this Jingle Factory object",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  /* signal definitions */

  signals[NEW_SESSION] = g_signal_new ("new-session",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabbleJingleFactory *self)
{
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (self);

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      g_assert (priv->conn != NULL);
      g_assert (priv->conn->lmconn != NULL);

      g_assert (priv->jingle_cb == NULL);
      g_assert (priv->jingle_info_cb == NULL);

      priv->jingle_cb = lm_message_handler_new (jingle_cb,
          self, NULL);
      lm_connection_register_message_handler (priv->conn->lmconn,
          priv->jingle_cb, LM_MESSAGE_TYPE_IQ,
          LM_HANDLER_PRIORITY_NORMAL);

      priv->jingle_info_cb = lm_message_handler_new (
          jingle_info_cb, self, NULL);
      lm_connection_register_message_handler (priv->conn->lmconn,
          priv->jingle_info_cb, LM_MESSAGE_TYPE_IQ,
          LM_HANDLER_PRIORITY_NORMAL);

      break;

    case TP_CONNECTION_STATUS_CONNECTED:
        {
          gchar *stun_server = NULL;
          guint stun_port = 0;

          g_object_get (priv->conn,
              "stun-server", &stun_server,
              "stun-port", &stun_port,
              NULL);

          if (stun_server == NULL)
            {
              self->priv->get_stun_from_jingle = TRUE;
            }
          else
            {
              take_stun_server (self, stun_server, stun_port);
            }

          if (priv->conn->features &
              GABBLE_CONNECTION_FEATURES_GOOGLE_JINGLE_INFO)
            {
              jingle_info_send_request (self);
            }
        }
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      if (priv->jingle_cb != NULL)
        {
          lm_connection_unregister_message_handler (priv->conn->lmconn,
              priv->jingle_cb, LM_MESSAGE_TYPE_IQ);
          lm_connection_unregister_message_handler (priv->conn->lmconn,
              priv->jingle_info_cb, LM_MESSAGE_TYPE_IQ);

          lm_message_handler_unref (priv->jingle_cb);
          priv->jingle_cb = NULL;

          lm_message_handler_unref (priv->jingle_info_cb);
          priv->jingle_info_cb = NULL;
        }
      break;
    }
}


static gboolean
sid_in_use (GabbleJingleFactory *factory, const gchar *sid)
{
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (factory);
  gpointer key, value;

  return g_hash_table_lookup_extended (priv->sessions, sid, &key, &value);
}

static gchar *
get_unique_sid (GabbleJingleFactory *factory)
{
  guint32 val;
  gchar *sid = NULL;
  gboolean unique = FALSE;

  while (!unique)
    {
      val = g_random_int_range (1000000, G_MAXINT);

      g_free (sid);
      sid = g_strdup_printf ("%u", val);

      unique = !sid_in_use (factory, sid);
    }

  return sid;
}

static void
register_session (GabbleJingleFactory *factory,
                  const gchar *sid,
                  GabbleJingleSession *sess)
{
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (factory);
  gchar *sid_copy;

  sid_copy = g_strdup (sid);
  g_assert (g_hash_table_lookup (priv->sessions, sid_copy) == NULL);
  g_hash_table_insert (priv->sessions, sid_copy, sess);
}

void
_jingle_factory_unregister_session (GabbleJingleFactory *factory,
                                    const gchar *sid)
{
  GabbleJingleFactoryPrivate *priv = GABBLE_JINGLE_FACTORY_GET_PRIVATE (factory);
  g_hash_table_remove (priv->sessions, sid);
}

static LmHandlerResult
jingle_cb (LmMessageHandler *handler,
           LmConnection *lmconn,
           LmMessage *msg,
           gpointer user_data)
{
  GabbleJingleFactory *self = GABBLE_JINGLE_FACTORY (user_data);
  GabbleJingleFactoryPrivate *priv =
      GABBLE_JINGLE_FACTORY_GET_PRIVATE (self);
  GError *error = NULL;
  const gchar *sid;
  GabbleJingleSession *sess;
  gboolean new_session = FALSE;
  JingleAction action;
  JingleDialect dialect;

  /* see if it's a jingle message and detect dialect */
  sid = gabble_jingle_session_detect (msg, &action, &dialect);
  if (sid == NULL)
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  sess = g_hash_table_lookup (priv->sessions, sid);
  if (sess == NULL)
    {
      if (action != JINGLE_ACTION_SESSION_INITIATE)
        {
          g_set_error (&error, GABBLE_XMPP_ERROR,
              XMPP_ERROR_JINGLE_OUT_OF_ORDER, "session not initiated yet");
          goto REQUEST_ERROR;
        }
      new_session = TRUE;
      sess = create_session (self, sid, 0, NULL);
      g_object_set (sess, "dialect", dialect, NULL);
    }

  /* now act on the message */
  if (gabble_jingle_session_parse (sess, action, msg, &error))
    {
      if (new_session)
        {
          g_signal_emit (self, signals[NEW_SESSION], 0, sess);
        }

      /* all went well, we can acknowledge the IQ */
      _gabble_connection_acknowledge_set_iq (priv->conn, msg);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  /* on parse error */
  g_assert (error != NULL);

  if (new_session)
      _jingle_factory_unregister_session (self, sid);

REQUEST_ERROR:
  _gabble_connection_send_iq_error (
    priv->conn, msg, error->code, error->message);

  g_error_free (error);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/*
 * If sid is set to NULL a unique sid is generated and
 * the "local-initiator" property of the newly created
 * GabbleJingleSession is set to true.
 */
static GabbleJingleSession *
create_session (GabbleJingleFactory *fac,
    const gchar *sid, TpHandle peer, const gchar *peer_resource)
{
  GabbleJingleFactoryPrivate *priv =
      GABBLE_JINGLE_FACTORY_GET_PRIVATE (fac);
  GabbleJingleSession *sess;
  gboolean local_initiator;

  if (sid != NULL)
    {
      g_assert (NULL == g_hash_table_lookup (priv->sessions, sid));
      local_initiator = FALSE;
    }
  else
    {
      sid = get_unique_sid (fac);
      local_initiator = TRUE;
    }

  sess = gabble_jingle_session_new (priv->conn, sid, local_initiator, peer,
      peer_resource);

  g_signal_connect (sess, "terminated",
    (GCallback) session_terminated_cb, fac);

  DEBUG ("new session %s @ %p created", sid, sess);
  register_session (fac, sid, sess);
  return sess;
}

GabbleJingleSession *
gabble_jingle_factory_create_session (GabbleJingleFactory *fac,
    TpHandle peer, const gchar *peer_resource)
{
  return create_session (fac, NULL, peer, peer_resource);
}

void
gabble_jingle_factory_register_transport (GabbleJingleFactory *self,
                                          gchar *xmlns,
                                          GType transport_type)
{
  g_return_if_fail (g_type_is_a (transport_type,
        GABBLE_TYPE_JINGLE_TRANSPORT_IFACE));

  g_hash_table_insert (self->priv->transports, xmlns,
      GSIZE_TO_POINTER (transport_type));
}

GType
gabble_jingle_factory_lookup_transport (GabbleJingleFactory *self,
                                        const gchar *xmlns)
{
  return GPOINTER_TO_SIZE (g_hash_table_lookup (self->priv->transports,
        xmlns));
}

void
gabble_jingle_factory_register_content_type (GabbleJingleFactory *self,
                                             gchar *xmlns,
                                             GType content_type)
{
  g_return_if_fail (g_type_is_a (content_type, GABBLE_TYPE_JINGLE_CONTENT));

  g_hash_table_insert (self->priv->content_types, xmlns,
      GSIZE_TO_POINTER (content_type));
}

GType
gabble_jingle_factory_lookup_content_type (GabbleJingleFactory *self,
                                           const gchar *xmlns)
{
  return GPOINTER_TO_SIZE (g_hash_table_lookup (self->priv->content_types,
        xmlns));
}

static void
session_terminated_cb (GabbleJingleSession *session,
    gboolean local_terminator, GabbleJingleFactory *factory)
{
  const gchar *sid;
  DEBUG ("removing terminated session");

  g_object_get (session, "session-id", &sid, NULL);

  _jingle_factory_unregister_session (factory, sid);
}

const gchar *
gabble_jingle_factory_get_google_relay_token (GabbleJingleFactory *self)
{
  return self->priv->relay_token;
}

gboolean
gabble_jingle_factory_get_stun_server (GabbleJingleFactory *self,
                                       gchar **stun_server,
                                       guint *stun_port)
{
  if (self->priv->stun_server == NULL || self->priv->stun_port == 0)
    return FALSE;

  if (stun_server != NULL)
    *stun_server = g_strdup (self->priv->stun_server);

  if (stun_port != NULL)
    *stun_port = self->priv->stun_port;

  return TRUE;
}
