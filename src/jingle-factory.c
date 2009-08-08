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
#include <libsoup/soup.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "debug.h"
#include "jingle-media-rtp.h"
#include "jingle-session.h"
#include "jingle-transport-google.h"
#include "jingle-transport-rawudp.h"
#include "jingle-transport-iceudp.h"
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

/* The 'session' map is keyed by:
 * "<peer's handle>\n<peer's resource>\n<session id>"
 */
#define SESSION_MAP_KEY_FORMAT "%u\n%s\n%s"

struct _GabbleJingleFactoryPrivate
{
  GabbleConnection *conn;
  LmMessageHandler *jingle_cb;
  LmMessageHandler *jingle_info_cb;
  GHashTable *content_types;
  GHashTable *transports;

  /* instances of SESSION_MAP_KEY_FORMAT => GabbleJingleSession. */
  GHashTable *sessions;
  GibberResolver *resolver;
  SoupSession *soup;

  gchar *stun_server;
  guint16 stun_port;
  gchar *fallback_stun_server;
  guint16 fallback_stun_port;
  gchar *relay_token;
  gboolean get_stun_from_jingle;
  gchar *relay_server;
  guint16 relay_http_port;
  guint16 relay_udp;
  guint16 relay_tcp;
  guint16 relay_ssltcp;

  gboolean dispose_has_run;
};

static LmHandlerResult jingle_cb (LmMessageHandler *handler,
    LmConnection *lmconn, LmMessage *message, gpointer user_data);
static GabbleJingleSession *create_session (GabbleJingleFactory *fac,
    const gchar *sid,
    TpHandle peer,
    const gchar *peer_resource,
    gboolean local_hold);

static void session_terminated_cb (GabbleJingleSession *sess,
    gboolean local_terminator,
    TpChannelGroupChangeReason reason,
    const gchar *text,
    GabbleJingleFactory *fac);

static void connection_status_changed_cb (GabbleConnection *conn,
    guint status, guint reason, GabbleJingleFactory *self);

#define RELAY_HTTP_TIMEOUT 5

static gboolean test_mode = FALSE;

void
gabble_jingle_factory_set_test_mode (void)
{
  test_mode = TRUE;
}

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
  priv->relay_http_port = 80;
}

typedef struct {
    gchar *stun_server;
    guint16 stun_port;
    gboolean fallback;
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

  if (data->fallback)
    {
      g_free (self->priv->fallback_stun_server);
      self->priv->fallback_stun_server = stun_server;
      self->priv->fallback_stun_port = data->stun_port;
    }
  else
    {
      g_free (self->priv->stun_server);
      self->priv->stun_server = stun_server;
      self->priv->stun_port = data->stun_port;
    }
}

static void
take_stun_server (GabbleJingleFactory *self,
                  gchar *stun_server,
                  guint16 stun_port,
                  gboolean fallback)
{
  PendingStunServer *data = g_slice_new0 (PendingStunServer);

  if (stun_server == NULL)
    return;

  DEBUG ("Resolving %s STUN server %s:%u",
      fallback ? "fallback" : "primary", stun_server, stun_port);
  data->stun_server = stun_server;
  data->stun_port = stun_port;
  data->fallback = fallback;

  gibber_resolver_addrinfo (self->priv->resolver, stun_server, NULL,
      AF_INET, SOCK_DGRAM, IPPROTO_UDP, 0,
      stun_server_resolved_cb, data, pending_stun_server_free,
      G_OBJECT (self));
}


static LmHandlerResult
got_jingle_info_stanza (GabbleJingleFactory *fac,
    LmMessage *message)
{
  GabbleJingleFactoryPrivate *priv = fac->priv;
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
              take_stun_server (fac, g_strdup (server), port, FALSE);
            }
        }
    }

  node = lm_message_node_get_child (query_node, "relay");

  if (node != NULL)
    {
      LmMessageNode *subnode;

      subnode = lm_message_node_get_child (node, "token");

      if (subnode != NULL)
        {
          const gchar *token;

          token = lm_message_node_get_value (subnode);
          if (token != NULL)
            {
              DEBUG ("jingle info: got Google relay token %s", token);
              g_free (fac->priv->relay_token);
              fac->priv->relay_token = g_strdup (token);
            }
        }

      subnode = lm_message_node_get_child (node, "server");

      if (subnode != NULL)
        {
          const gchar *server;
          const gchar *port;

          server = lm_message_node_get_attribute (subnode, "host");

          if (server != NULL)
            {
              DEBUG ("jingle info: got relay server %s", server);
              g_free (fac->priv->relay_server);
              fac->priv->relay_server = g_strdup (server);
            }

          if (test_mode)
            {
              /* this is not part of the real protocol, but we can't listen on
               * port 80 in an unprivileged regression test */
              port = lm_message_node_get_attribute (subnode,
                  "gabble-test-http-port");

              if (port != NULL)
                {
                  DEBUG ("jingle info: diverting 'Google' HTTP requests to "
                      "port %s", port);
                  fac->priv->relay_http_port = atoi (port);
                }
            }

          /* FIXME: these are not really actually used anywhere at
           * the moment, because we get the same info when creating
           * relay session. */
          port = lm_message_node_get_attribute (subnode, "udp");

          if (port != NULL)
            {
              DEBUG ("jingle info: got relay udp port %s", port);
              fac->priv->relay_udp = atoi (port);
            }

          port = lm_message_node_get_attribute (subnode, "tcp");

          if (port != NULL)
            {
              DEBUG ("jingle info: got relay tcp port %s", port);
              fac->priv->relay_tcp = atoi (port);
            }

          port = lm_message_node_get_attribute (subnode, "tcpssl");

          if (port != NULL)
            {
              DEBUG ("jingle info: got relay tcpssl port %s", port);
              fac->priv->relay_ssltcp = atoi (port);
            }

        }

    }

  if (sub_type == LM_MESSAGE_SUB_TYPE_SET)
    {
      _gabble_connection_acknowledge_set_iq (priv->conn, message);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
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

  return got_jingle_info_stanza (fac, message);
}

static LmHandlerResult
jingle_info_reply_cb (GabbleConnection *conn,
    LmMessage *sent_msg,
    LmMessage *reply_msg,
    GObject *factory_obj,
    gpointer user_data)
{
  GabbleJingleFactory *fac = GABBLE_JINGLE_FACTORY (factory_obj);

  return got_jingle_info_stanza (fac, reply_msg);
}

static void
jingle_info_send_request (GabbleJingleFactory *fac)
{
  GabbleJingleFactoryPrivate *priv = fac->priv;
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

  if (!_gabble_connection_send_with_reply (priv->conn, msg,
        jingle_info_reply_cb, G_OBJECT (fac), fac, &error))
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
  GabbleJingleFactoryPrivate *priv = fac->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  if (priv->soup != NULL)
    {
      g_object_unref (priv->soup);
      priv->soup = NULL;
    }

  g_hash_table_destroy (priv->sessions);
  priv->sessions = NULL;

  g_hash_table_destroy (priv->content_types);
  priv->content_types = NULL;

  g_hash_table_destroy (priv->transports);
  priv->transports = NULL;

  g_free (fac->priv->stun_server);
  g_free (fac->priv->fallback_stun_server);
  g_free (fac->priv->relay_token);
  g_free (fac->priv->relay_server);

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
  GabbleJingleFactoryPrivate *priv = chan->priv;

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
  GabbleJingleFactoryPrivate *priv = chan->priv;

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
  priv = self->priv;

  /* FIXME: why was this in _constructed in media factory? */
  g_signal_connect (priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, self);

  jingle_media_rtp_register (self);
  jingle_transport_google_register (self);
  jingle_transport_rawudp_register (self);
  jingle_transport_iceudp_register (self);

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
  GabbleJingleFactoryPrivate *priv = self->priv;

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      g_assert (priv->conn != NULL);

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
              take_stun_server (self, stun_server, stun_port, FALSE);
            }

          g_object_get (priv->conn,
              "fallback-stun-server", &stun_server,
              "fallback-stun-port", &stun_port,
              NULL);

          if (stun_server != NULL)
            {
              take_stun_server (self, stun_server, stun_port, TRUE);
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

static gchar *
make_session_map_key (TpHandle peer,
    const gchar *resource,
    const gchar *sid)
{
  return g_strdup_printf (SESSION_MAP_KEY_FORMAT, peer, resource, sid);
}

static gchar *
get_unique_sid_for (GabbleJingleFactory *factory,
    TpHandle peer,
    const gchar *resource,
    gchar **key)
{
  guint32 val;
  gchar *sid = NULL;
  gchar *key_;

  do
    {
      val = g_random_int_range (1000000, G_MAXINT);

      g_free (sid);
      sid = g_strdup_printf ("%u", val);
      key_ = make_session_map_key (peer, resource, sid);
    }
  while (g_hash_table_lookup (factory->priv->sessions, key_) != NULL);

  *key = key_;
  return sid;
}

static GabbleJingleSession *
ensure_session (GabbleJingleFactory *self,
    const gchar *sid,
    const gchar *from,
    JingleAction action,
    JingleDialect dialect,
    gboolean *new_session,
    GError **error)
{
  GabbleJingleFactoryPrivate *priv = self->priv;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *resource;
  gchar *key;
  GabbleJingleSession *sess;
  TpHandle peer;

  resource = strchr (from, '/');

  if (resource == NULL || *resource == '\0')
    {
      g_set_error (error, GABBLE_XMPP_ERROR,
          XMPP_ERROR_BAD_REQUEST, "IQ sender '%s' has no resource", from);
      return NULL;
    }

  resource++;

  peer = tp_handle_ensure (contact_repo, from, NULL, error);

  if (peer == 0)
    {
      g_prefix_error (error, "Couldn't parse sender '%s': ", from);
      return NULL;
    }

  key = make_session_map_key (peer, resource, sid);
  sess = g_hash_table_lookup (priv->sessions, key);
  g_free (key);

  if (sess == NULL)
    {
      if (action == JINGLE_ACTION_SESSION_INITIATE)
        {
          sess = create_session (self, sid, peer, resource, FALSE);
          g_object_set (sess, "dialect", dialect, NULL);
          *new_session = TRUE;
        }
      else
        {
          g_set_error (error, GABBLE_XMPP_ERROR,
              XMPP_ERROR_JINGLE_UNKNOWN_SESSION, "session %s is unknown", sid);
          return NULL;
        }
    }
  else
    {
      *new_session = FALSE;
    }

  tp_handle_unref (contact_repo, peer);
  return sess;
}

static LmHandlerResult
jingle_cb (LmMessageHandler *handler,
           LmConnection *lmconn,
           LmMessage *msg,
           gpointer user_data)
{
  GabbleJingleFactory *self = GABBLE_JINGLE_FACTORY (user_data);
  GabbleJingleFactoryPrivate *priv = self->priv;
  GError *error = NULL;
  const gchar *sid, *from;
  GabbleJingleSession *sess;
  gboolean new_session = FALSE;
  JingleAction action;
  JingleDialect dialect;

  /* see if it's a jingle message and detect dialect */
  sid = gabble_jingle_session_detect (msg, &action, &dialect);
  from = lm_message_node_get_attribute (lm_message_get_node (msg), "from");

  if (sid == NULL || from == NULL)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  sess = ensure_session (self, sid, from, action, dialect, &new_session,
      &error);

  if (sess == NULL)
    goto REQUEST_ERROR;

  /* now act on the message */
  if (!gabble_jingle_session_parse (sess, action, msg, &error))
    goto REQUEST_ERROR;

  if (new_session)
    g_signal_emit (self, signals[NEW_SESSION], 0, sess);

  /* all went well, we can acknowledge the IQ */
  _gabble_connection_acknowledge_set_iq (priv->conn, msg);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;

REQUEST_ERROR:
  g_assert (error != NULL);

  DEBUG ("NAKing with error: %s", error->message);
  _gabble_connection_send_iq_error (priv->conn, msg, error->code,
      error->message);

  g_error_free (error);

  if (sess != NULL && new_session)
    gabble_jingle_session_terminate (sess, TP_CHANNEL_GROUP_CHANGE_REASON_NONE,
        NULL, NULL);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/*
 * If sid is set to NULL a unique sid is generated and
 * the "local-initiator" property of the newly created
 * GabbleJingleSession is set to true.
 */
static GabbleJingleSession *
create_session (GabbleJingleFactory *fac,
    const gchar *sid,
    TpHandle peer,
    const gchar *peer_resource,
    gboolean local_hold)
{
  GabbleJingleFactoryPrivate *priv = fac->priv;
  GabbleJingleSession *sess;
  gboolean local_initiator;
  gchar *sid_, *key;

  g_assert (peer != 0);
  g_assert (peer_resource != NULL);

  if (sid != NULL)
    {
      key = make_session_map_key (peer, peer_resource, sid);
      sid_ = g_strdup (sid);

      local_initiator = FALSE;
    }
  else
    {
      sid_ = get_unique_sid_for (fac, peer, peer_resource, &key);

      local_initiator = TRUE;
    }

  /* Either we should have found the existing session when the IQ arrived, or
   * get_unique_sid_for should have ensured the key is fresh. */
  g_assert (NULL == g_hash_table_lookup (priv->sessions, key));

  sess = gabble_jingle_session_new (priv->conn, sid_, local_initiator, peer,
      peer_resource, local_hold);
  g_signal_connect (sess, "terminated", (GCallback) session_terminated_cb, fac);

  /* Takes ownership of key */
  g_hash_table_insert (priv->sessions, key, sess);

  DEBUG ("new session (%u, %s, %s) @ %p", peer, peer_resource, sid_, sess);

  g_free (sid_);

  return sess;
}

GabbleJingleSession *
gabble_jingle_factory_create_session (GabbleJingleFactory *fac,
    TpHandle peer,
    const gchar *peer_resource,
    gboolean local_hold)
{
  return create_session (fac, NULL, peer, peer_resource, local_hold);
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
                       gboolean local_terminator,
                       TpChannelGroupChangeReason reason,
                       const gchar *text,
                       GabbleJingleFactory *factory)
{
  gchar *key = make_session_map_key (session->peer,
      gabble_jingle_session_get_peer_resource (session),
      gabble_jingle_session_get_sid (session));

  DEBUG ("removing terminated session with key %s", key);

  g_warn_if_fail (g_hash_table_remove (factory->priv->sessions, key));

  g_free (key);
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
    {
      if (self->priv->fallback_stun_server == NULL ||
          self->priv->fallback_stun_port == 0)
        return FALSE;

      if (stun_server != NULL)
        *stun_server = g_strdup (self->priv->fallback_stun_server);

      if (stun_port != NULL)
        *stun_port = self->priv->fallback_stun_port;

      return TRUE;
    }

  if (stun_server != NULL)
    *stun_server = g_strdup (self->priv->stun_server);

  if (stun_port != NULL)
    *stun_port = self->priv->stun_port;

  return TRUE;
}

typedef struct
{
  GPtrArray *relays;
  guint component;
  guint requests_to_do;
  GabbleJingleFactoryRelaySessionCb callback;
  gpointer user_data;
} RelaySessionData;

static RelaySessionData *
relay_session_data_new (guint requests_to_do,
                        GabbleJingleFactoryRelaySessionCb callback,
                        gpointer user_data)
{
  RelaySessionData *rsd = g_slice_new0 (RelaySessionData);

  rsd->relays = g_ptr_array_sized_new (requests_to_do);
  rsd->component = 1;
  rsd->requests_to_do = requests_to_do;
  rsd->callback = callback;
  rsd->user_data = user_data;

  return rsd;
}

/* This is a GSourceFunc */
static gboolean
relay_session_data_call (gpointer p)
{
  RelaySessionData *rsd = p;

  g_assert (rsd->callback != NULL);

  rsd->callback (rsd->relays, rsd->user_data);

  return FALSE;
}

/* This is a GDestroyNotify */
static void
relay_session_data_destroy (gpointer p)
{
  RelaySessionData *rsd = p;

  g_ptr_array_foreach (rsd->relays, (GFunc) g_hash_table_destroy, NULL);
  g_ptr_array_free (rsd->relays, TRUE);

  g_slice_free (RelaySessionData, rsd);
}

static void
translate_relay_info (GPtrArray *relays,
                      const gchar *relay_ip,
                      const gchar *username,
                      const gchar *password,
                      const gchar *static_type,
                      const gchar *port_string,
                      guint component)
{
  GHashTable *asv;
  guint port = 0;

  if (port_string == NULL)
    {
      DEBUG ("no relay port for %s found", static_type);
      return;
    }

  port = atoi (port_string);

  if (port == 0 || port > G_MAXUINT16)
    {
      DEBUG ("failed to parse relay port '%s' for %s", port_string,
          static_type);
      return;
    }

  DEBUG ("type=%s ip=%s port=%u username=%s password=%s component=%u",
      static_type, relay_ip, port, username, password, component);
  /* keys are static, values are slice-allocated */
  asv = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  g_hash_table_insert (asv, "ip",
      tp_g_value_slice_new_string (relay_ip));
  g_hash_table_insert (asv, "type",
      tp_g_value_slice_new_static_string (static_type));
  g_hash_table_insert (asv, "port",
      tp_g_value_slice_new_uint (port));
  g_hash_table_insert (asv, "username",
      tp_g_value_slice_new_string (username));
  g_hash_table_insert (asv, "password",
      tp_g_value_slice_new_string (password));
  g_hash_table_insert (asv, "component",
      tp_g_value_slice_new_uint (component));

  g_ptr_array_add (relays, asv);
}

static void
on_http_response (SoupSession *soup,
                  SoupMessage *msg,
                  gpointer user_data)
{
  RelaySessionData *rsd = user_data;

  if (msg->status_code != 200)
    {
      DEBUG ("Google session creation failed, relaying not used: %d %s",
          msg->status_code, msg->reason_phrase);
    }
  else
    {
      /* parse a=b lines into GHashTable
       * (key, value both borrowed from items of the strv 'lines') */
      GHashTable *map = g_hash_table_new (g_str_hash, g_str_equal);
      gchar **lines;
      guint i;
      const gchar *relay_ip;
      const gchar *relay_udp_port;
      const gchar *relay_tcp_port;
      const gchar *relay_ssltcp_port;
      const gchar *username;
      const gchar *password;

      DEBUG ("Response from Google:\n====\n%s\n====",
          msg->response_body->data);

      lines = g_strsplit (msg->response_body->data, "\n", 0);

      if (lines != NULL)
        {
          for (i = 0; lines[i] != NULL; i++)
            {
              gchar *delim = strchr (lines[i], '=');
              size_t len;

              if (delim == NULL || delim == lines[i])
                {
                  /* ignore empty keys or lines without '=' */
                  continue;
                }

              len = strlen (lines[i]);

              if (lines[i][len - 1] == '\r')
                {
                  lines[i][len - 1] = '\0';
                }

              *delim = '\0';
              g_hash_table_insert (map, lines[i], delim + 1);
            }
        }

      relay_ip = g_hash_table_lookup (map, "relay.ip");
      relay_udp_port = g_hash_table_lookup (map, "relay.udp_port");
      relay_tcp_port = g_hash_table_lookup (map, "relay.tcp_port");
      relay_ssltcp_port = g_hash_table_lookup (map, "relay.ssltcp_port");
      username = g_hash_table_lookup (map, "username");
      password = g_hash_table_lookup (map, "password");

      if (relay_ip == NULL)
        {
          DEBUG ("No relay.ip found");
        }
      else if (username == NULL)
        {
          DEBUG ("No username found");
        }
      else if (password == NULL)
        {
          DEBUG ("No password found");
        }
      else
        {
          translate_relay_info (rsd->relays, relay_ip, username, password,
              "udp", relay_udp_port, rsd->component);
          translate_relay_info (rsd->relays, relay_ip, username, password,
              "tcp", relay_tcp_port, rsd->component);
          translate_relay_info (rsd->relays, relay_ip, username, password,
              "tls", relay_ssltcp_port, rsd->component);
        }

      g_strfreev (lines);
      g_hash_table_destroy (map);
    }

  rsd->component++;

  if ((--rsd->requests_to_do) == 0)
    {
      relay_session_data_call (rsd);
      relay_session_data_destroy (rsd);
    }
}

void
gabble_jingle_factory_create_google_relay_session (
    GabbleJingleFactory *fac,
    guint components,
    GabbleJingleFactoryRelaySessionCb callback,
    gpointer user_data)
{
  GabbleJingleFactoryPrivate *priv = fac->priv;
  gchar *url;
  guint i;
  RelaySessionData *rsd;

  g_return_if_fail (callback != NULL);

  rsd = relay_session_data_new (components, callback, user_data);

  if (fac->priv->relay_server == NULL)
    {
      DEBUG ("No relay server provided, not creating google relay session");
      g_idle_add_full (G_PRIORITY_DEFAULT, relay_session_data_call, rsd,
          relay_session_data_destroy);
      return;
    }

  if (fac->priv->relay_token == NULL)
    {
      DEBUG ("No relay token provided, not creating google relay session");
      g_idle_add_full (G_PRIORITY_DEFAULT, relay_session_data_call, rsd,
          relay_session_data_destroy);
      return;
    }

  if (priv->soup == NULL)
    {
      priv->soup = soup_session_async_new ();

      /* If we don't get answer in a few seconds, relay won't do
       * us much help anyways. */
      g_object_set (priv->soup, "timeout", RELAY_HTTP_TIMEOUT, NULL);
    }

  url = g_strdup_printf ("http://%s:%d/create_session",
      fac->priv->relay_server, fac->priv->relay_http_port);

  for (i = 0; i < components; i++)
    {
      SoupMessage *msg = soup_message_new ("GET", url);

      DEBUG ("Trying to create a new relay session on %s", url);

      /* libjingle sets both headers, so shall we */
      soup_message_headers_append (msg->request_headers,
          "X-Talk-Google-Relay-Auth", fac->priv->relay_token);
      soup_message_headers_append (msg->request_headers,
          "X-Google-Relay-Auth", fac->priv->relay_token);

      soup_session_queue_message (priv->soup, msg, on_http_response, rsd);
    }

  g_free (url);
}
