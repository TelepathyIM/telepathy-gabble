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

#include <loudmouth/loudmouth.h>
#include <wocky/wocky-c2s-porter.h>
#include <libsoup/soup.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "conn-util.h"
#include "debug.h"
#include "gabble-signals-marshal.h"
#include "jingle-share.h"
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
    STUN_SERVER_CHANGED,
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
  guint jingle_handler_id;
  guint jingle_info_handler_id;
  GHashTable *content_types;
  GHashTable *transports;

  /* instances of SESSION_MAP_KEY_FORMAT => GabbleJingleSession. */
  GHashTable *sessions;
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

static gboolean jingle_cb (
    WockyPorter *porter,
    WockyStanza *msg,
    gpointer user_data);
static GabbleJingleSession *create_session (GabbleJingleFactory *fac,
    const gchar *sid,
    TpHandle peer,
    const gchar *jid,
    gboolean local_hold);

static void session_terminated_cb (GabbleJingleSession *sess,
    gboolean local_terminator,
    TpChannelGroupChangeReason reason,
    const gchar *text,
    GabbleJingleFactory *fac);

static void connection_status_changed_cb (GabbleConnection *conn,
    guint status, guint reason, GabbleJingleFactory *self);
static void connection_porter_available_cb (
    GabbleConnection *conn,
    WockyPorter *porter,
    gpointer user_data);

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

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
  priv->relay_http_port = 80;
}

typedef struct {
    GabbleJingleFactory *factory;
    gchar *stun_server;
    guint16 stun_port;
    gboolean fallback;
    GCancellable *cancellable;
} PendingStunServer;

static void
pending_stun_server_free (gpointer p)
{
  PendingStunServer *data = p;

  if (data->factory != NULL)
    g_object_remove_weak_pointer (G_OBJECT (data->factory),
        (gpointer)&data->factory);

  g_object_unref (data->cancellable);
  g_free (data->stun_server);
  g_slice_free (PendingStunServer, p);
}

static void
stun_server_resolved_cb (GObject *resolver,
                         GAsyncResult *result,
                         gpointer user_data)
{
  PendingStunServer *data = user_data;
  GabbleJingleFactory *self = data->factory;
  GError *e = NULL;
  gchar *stun_server;
  GList *entries;

  if (self != NULL)
      g_object_weak_unref (G_OBJECT (self),
          (GWeakNotify)g_cancellable_cancel, data->cancellable);

  entries = g_resolver_lookup_by_name_finish (
      G_RESOLVER (resolver), result, &e);

  if (entries == NULL)
    {
      DEBUG ("Failed to resolve STUN server %s:%u: %s",
          data->stun_server, data->stun_port, e->message);
      g_error_free (e);
      goto out;
    }

  stun_server = g_inet_address_to_string (entries->data);
  g_resolver_free_addresses (entries);

  DEBUG ("Resolved STUN server %s:%u to %s:%u", data->stun_server,
      data->stun_port, stun_server, data->stun_port);

  if (self == NULL)
    {
      g_free (stun_server);
      goto out;
    }

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

      g_signal_emit (self, signals[STUN_SERVER_CHANGED], 0,
          stun_server, data->stun_port);
    }

out:
  pending_stun_server_free (data);
  g_object_unref (resolver);
}

static void
take_stun_server (GabbleJingleFactory *self,
                  gchar *stun_server,
                  guint16 stun_port,
                  gboolean fallback)
{
  GResolver *resolver;
  PendingStunServer *data;

  if (stun_server == NULL)
    return;

  resolver = g_resolver_get_default ();
  data = g_slice_new0 (PendingStunServer);

  DEBUG ("Resolving %s STUN server %s:%u",
      fallback ? "fallback" : "primary", stun_server, stun_port);
  data->factory = self;
  g_object_add_weak_pointer (G_OBJECT (self), (gpointer *) &data->factory);
  data->stun_server = stun_server;
  data->stun_port = stun_port;
  data->fallback = fallback;

  data->cancellable = g_cancellable_new ();
  g_object_weak_ref (G_OBJECT (self), (GWeakNotify)g_cancellable_cancel,
      data->cancellable);

  g_resolver_lookup_by_name_async (resolver, stun_server,
      data->cancellable, stun_server_resolved_cb, data);
}


static void
got_jingle_info_stanza (
    GabbleJingleFactory *fac,
    WockyStanza *stanza)
{
  WockyNode *node, *query_node;

  query_node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (stanza), "query", NS_GOOGLE_JINGLE_INFO);

  if (query_node == NULL)
    return;

  if (fac->priv->get_stun_from_jingle)
    node = wocky_node_get_child (query_node, "stun");
  else
    node = NULL;

  if (node != NULL)
    {
      node = wocky_node_get_child (node, "server");

      if (node != NULL)
        {
          const gchar *server;
          const gchar *port_attr;
          guint port = GABBLE_PARAMS_DEFAULT_STUN_PORT;

          server = wocky_node_get_attribute (node, "host");
          port_attr = wocky_node_get_attribute (node, "udp");

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

  node = wocky_node_get_child (query_node, "relay");

  if (node != NULL)
    {
      WockyNode *subnode = wocky_node_get_child (node, "token");

      if (subnode != NULL)
        {
          const gchar *token = subnode->content;

          if (token != NULL)
            {
              DEBUG ("jingle info: got Google relay token %s", token);
              g_free (fac->priv->relay_token);
              fac->priv->relay_token = g_strdup (token);
            }
        }

      subnode = wocky_node_get_child (node, "server");

      if (subnode != NULL)
        {
          const gchar *server;
          const gchar *port;

          server = wocky_node_get_attribute (subnode, "host");

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
              port = wocky_node_get_attribute (subnode,
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
          port = wocky_node_get_attribute (subnode, "udp");

          if (port != NULL)
            {
              DEBUG ("jingle info: got relay udp port %s", port);
              fac->priv->relay_udp = atoi (port);
            }

          port = wocky_node_get_attribute (subnode, "tcp");

          if (port != NULL)
            {
              DEBUG ("jingle info: got relay tcp port %s", port);
              fac->priv->relay_tcp = atoi (port);
            }

          port = wocky_node_get_attribute (subnode, "tcpssl");

          if (port != NULL)
            {
              DEBUG ("jingle info: got relay tcpssl port %s", port);
              fac->priv->relay_ssltcp = atoi (port);
            }

        }

    }
}

static gboolean
jingle_info_cb (
    WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  GabbleJingleFactory *fac = GABBLE_JINGLE_FACTORY (user_data);

  got_jingle_info_stanza (fac, stanza);
  wocky_porter_acknowledge_iq (porter, stanza, NULL);

  return TRUE;
}

static void
jingle_info_reply_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleJingleFactory *fac = GABBLE_JINGLE_FACTORY (user_data);
  WockyStanza *reply = NULL;
  GError *error = NULL;

  if (conn_util_send_iq_finish (GABBLE_CONNECTION (source), result, &reply,
          &error))
    {
      got_jingle_info_stanza (fac, reply);
    }
  else
    {
      DEBUG ("jingle info request failed: %s", error->message);
      g_clear_error (&error);
    }

  tp_clear_object (&reply);
}

static void
jingle_info_send_request (GabbleJingleFactory *fac)
{
  GabbleJingleFactoryPrivate *priv = fac->priv;
  const gchar *jid = conn_util_get_bare_self_jid (priv->conn);
  WockyStanza *stanza = wocky_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET, NULL, jid,
      '(', "query", ':', NS_GOOGLE_JINGLE_INFO, ')', NULL);

  conn_util_send_iq_async (priv->conn, stanza, NULL, jingle_info_reply_cb, fac);

  g_object_unref (stanza);
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

  tp_clear_object (&priv->soup);
  tp_clear_pointer (&priv->sessions, g_hash_table_destroy);
  tp_clear_pointer (&priv->content_types, g_hash_table_destroy);
  tp_clear_pointer (&priv->transports, g_hash_table_destroy);
  tp_clear_pointer (&priv->stun_server, g_free);
  tp_clear_pointer (&priv->fallback_stun_server, g_free);
  tp_clear_pointer (&priv->relay_token, g_free);
  tp_clear_pointer (&priv->relay_server, g_free);

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

static void
gabble_jingle_factory_constructed (GObject *obj)
{
  GabbleJingleFactory *self = GABBLE_JINGLE_FACTORY (obj);
  GabbleJingleFactoryPrivate *priv = self->priv;
  GObjectClass *parent = G_OBJECT_CLASS (gabble_jingle_factory_parent_class);

  if (parent->constructed != NULL)
    parent->constructed (obj);

  gabble_signal_connect_weak (priv->conn, "status-changed",
      (GCallback) connection_status_changed_cb, G_OBJECT (self));
  gabble_signal_connect_weak (priv->conn, "porter-available",
      (GCallback) connection_porter_available_cb, G_OBJECT (self));

  jingle_share_register (self);
  jingle_media_rtp_register (self);
  jingle_transport_google_register (self);
  jingle_transport_rawudp_register (self);
  jingle_transport_iceudp_register (self);
}

static void
gabble_jingle_factory_class_init (GabbleJingleFactoryClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);
  GParamSpec *param_spec;

  g_type_class_add_private (cls, sizeof (GabbleJingleFactoryPrivate));

  object_class->constructed = gabble_jingle_factory_constructed;
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

  signals[STUN_SERVER_CHANGED] = g_signal_new ("stun-server-changed",
        G_TYPE_FROM_CLASS (cls), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, gabble_marshal_VOID__STRING_UINT,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);
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
      if (priv->jingle_handler_id != 0)
        {
          WockyPorter *p = wocky_session_get_porter (priv->conn->session);

          wocky_porter_unregister_handler (p, priv->jingle_handler_id);
          wocky_porter_unregister_handler (p, priv->jingle_info_handler_id);
          priv->jingle_handler_id = 0;
          priv->jingle_info_handler_id = 0;
        }

      break;
    }
}

static void
connection_porter_available_cb (
    GabbleConnection *conn,
    WockyPorter *porter,
    gpointer user_data)
{
  GabbleJingleFactory *self = GABBLE_JINGLE_FACTORY (user_data);
  GabbleJingleFactoryPrivate *priv = self->priv;

  g_assert (priv->jingle_handler_id == 0);

  /* TODO: we could match different dialects here maybe? */
  priv->jingle_handler_id = wocky_porter_register_handler_from_anyone (porter,
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, jingle_cb, self,
      NULL);

  priv->jingle_info_handler_id = wocky_c2s_porter_register_handler_from_server (
      WOCKY_C2S_PORTER (porter),
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, jingle_info_cb, self,
      '(', "query", ':', NS_GOOGLE_JINGLE_INFO, ')', NULL);
}

/* The 'session' map is keyed by:
 * "<peer's handle>\n<peer's jid>\n<session id>"
 */
#define SESSION_MAP_KEY_FORMAT "%u\n%s\n%s"

static gchar *
make_session_map_key (TpHandle peer,
    const gchar *jid,
    const gchar *sid)
{
  return g_strdup_printf (SESSION_MAP_KEY_FORMAT, peer, jid, sid);
}

static gchar *
get_unique_sid_for (GabbleJingleFactory *factory,
    TpHandle peer,
    const gchar *jid,
    gchar **key)
{
  guint32 val;
  gchar *sid = NULL;
  gchar *key_ = NULL;

  do
    {
      val = g_random_int_range (1000000, G_MAXINT);

      g_free (sid);
      g_free (key_);
      sid = g_strdup_printf ("%u", val);
      key_ = make_session_map_key (peer, jid, sid);
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
  gchar *key;
  GabbleJingleSession *sess;
  TpHandle peer;

  peer = tp_handle_ensure (contact_repo, from, NULL, error);

  if (peer == 0)
    {
      g_prefix_error (error, "Couldn't parse sender '%s': ", from);
      return NULL;
    }

  /* If we can ensure the handle, we can decode the jid */
  key = make_session_map_key (peer, from, sid);
  sess = g_hash_table_lookup (priv->sessions, key);
  g_free (key);

  if (sess == NULL)
    {
      if (action == JINGLE_ACTION_SESSION_INITIATE)
        {
          sess = create_session (self, sid, peer, from, FALSE);
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

static gboolean
jingle_cb (
    WockyPorter *porter,
    WockyStanza *msg,
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
  from = wocky_stanza_get_from (msg);

  if (sid == NULL || from == NULL)
    return FALSE;

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

  return TRUE;

REQUEST_ERROR:
  g_assert (error != NULL);

  DEBUG ("NAKing with error: %s", error->message);
  _gabble_connection_send_iq_error (priv->conn, msg, error->code,
      error->message);

  g_error_free (error);

  if (sess != NULL && new_session)
    gabble_jingle_session_terminate (sess, JINGLE_REASON_UNKNOWN, NULL, NULL);

  return TRUE;
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
    const gchar *jid,
    gboolean local_hold)
{
  GabbleJingleFactoryPrivate *priv = fac->priv;
  GabbleJingleSession *sess;
  gboolean local_initiator;
  gchar *sid_, *key;

  g_assert (jid != NULL);

  if (sid != NULL)
    {
      key = make_session_map_key (peer, jid, sid);
      sid_ = g_strdup (sid);

      local_initiator = FALSE;
    }
  else
    {
      sid_ = get_unique_sid_for (fac, peer, jid, &key);

      local_initiator = TRUE;
    }

  /* Either we should have found the existing session when the IQ arrived, or
   * get_unique_sid_for should have ensured the key is fresh. */
  g_assert (NULL == g_hash_table_lookup (priv->sessions, key));

  sess = gabble_jingle_session_new (priv->conn, sid_, local_initiator, jid,
      local_hold);
  g_signal_connect (sess, "terminated",
    (GCallback) session_terminated_cb, fac);

  /* Takes ownership of key */
  g_hash_table_insert (priv->sessions, key, sess);

  DEBUG ("new session (%s, %s) @ %p", jid, sid_, sess);

  g_free (sid_);

  return sess;
}

GabbleJingleSession *
gabble_jingle_factory_create_session (GabbleJingleFactory *fac,
    TpHandle peer,
    const gchar *jid,
    gboolean local_hold)
{
  return create_session (fac, NULL, peer, jid, local_hold);
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
      gabble_jingle_session_get_peer_jid (session),
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
      gchar *escaped_str;

      escaped_str = g_strescape (msg->response_body->data, "\r\n");
      DEBUG ("Response from Google:\n====\n%s\n====", escaped_str);
      g_free (escaped_str);

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
