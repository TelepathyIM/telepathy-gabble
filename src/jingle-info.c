/*
 * jingle-info.c - exciting times with Google's jingleinfo extension
 * Copyright © 2008–2012 Collabora Ltd.
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
 */

#include "config.h"

#include "jingle-info.h"
#include "jingle-info-internal.h"

#include <stdlib.h>
#include <telepathy-glib/telepathy-glib.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA
#include "debug.h"
#include "google-relay.h"
#include "gabble-enumtypes.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"

static gboolean jingle_info_cb (
    WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data);

struct _WockyJingleInfoPrivate {
    WockyPorter *porter;
    guint jingle_info_handler_id;
    gchar *jid_domain;

    WockyGoogleRelayResolver *google_resolver;

    WockyStunServer *stun_server;
    WockyStunServer *fallback_stun_server;

    gchar *relay_token;

    /* TRUE if the user has not explicitly specified a STUN server, and hence
     * we should ask the XMPP server for one; FALSE if not.
     */
    gboolean get_stun_from_jingle;

    gchar *relay_server;
    guint16 relay_http_port;
    guint16 relay_udp;
    guint16 relay_tcp;
    guint16 relay_ssltcp;

};

enum {
    PROP_PORTER = 1,
};

enum {
    STUN_SERVER_CHANGED = 0,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static gboolean test_mode = FALSE;

void
wocky_jingle_info_set_test_mode (void)
{
  test_mode = TRUE;
}

static WockyStunServer *
wocky_stun_server_new (
    gchar *address,
    guint16 port)
{
  WockyStunServer stun_server = { address, port };

  return g_slice_dup (WockyStunServer, &stun_server);
}

static void
wocky_stun_server_free (WockyStunServer *stun_server)
{
  if (stun_server != NULL)
    {
      g_free (stun_server->address);
      g_slice_free (WockyStunServer, stun_server);
    }
}

G_DEFINE_TYPE (WockyJingleInfo, wocky_jingle_info, G_TYPE_OBJECT)

static void
wocky_jingle_info_init (WockyJingleInfo *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, WOCKY_TYPE_JINGLE_INFO,
      WockyJingleInfoPrivate);

  self->priv->relay_http_port = 80;
  self->priv->get_stun_from_jingle = TRUE;
}

static void
wocky_jingle_info_get_property (
    GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  WockyJingleInfo *self = WOCKY_JINGLE_INFO (object);
  WockyJingleInfoPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_PORTER:
        g_value_set_object (value, priv->porter);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
wocky_jingle_info_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  WockyJingleInfo *self = WOCKY_JINGLE_INFO (object);
  WockyJingleInfoPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_PORTER:
        g_assert (priv->porter == NULL);
        priv->porter = g_value_dup_object (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
wocky_jingle_info_constructed (GObject *object)
{
  WockyJingleInfo *self = WOCKY_JINGLE_INFO (object);
  WockyJingleInfoPrivate *priv = self->priv;
  GObjectClass *parent_class = wocky_jingle_info_parent_class;

  if (parent_class->constructed != NULL)
    parent_class->constructed (object);

  g_assert (priv->porter != NULL);

  if (!wocky_decode_jid (wocky_porter_get_bare_jid (priv->porter), NULL,
          &priv->jid_domain, NULL))
    g_assert_not_reached ();
}

static void
wocky_jingle_info_dispose (GObject *object)
{
  WockyJingleInfo *self = WOCKY_JINGLE_INFO (object);
  WockyJingleInfoPrivate *priv = self->priv;
  GObjectClass *parent_class = wocky_jingle_info_parent_class;

  if (priv->porter != NULL)
    {
      if (priv->jingle_info_handler_id != 0)
        wocky_porter_unregister_handler (priv->porter,
            priv->jingle_info_handler_id);

      g_clear_object (&priv->porter);
    }

  if (priv->google_resolver != NULL)
    {
      wocky_google_relay_resolver_destroy (priv->google_resolver);
      priv->google_resolver = NULL;
    }

  g_free (priv->jid_domain);
  priv->jid_domain = NULL;
  wocky_stun_server_free (priv->stun_server);
  priv->stun_server = NULL;
  wocky_stun_server_free (priv->fallback_stun_server);
  priv->fallback_stun_server = NULL;
  g_free (priv->relay_token);
  priv->relay_token = NULL;
  g_free (priv->relay_server);
  priv->relay_server = NULL;

  if (parent_class->dispose != NULL)
    parent_class->dispose (object);
}

static void
wocky_jingle_info_class_init (WockyJingleInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  object_class->get_property = wocky_jingle_info_get_property;
  object_class->set_property = wocky_jingle_info_set_property;
  object_class->constructed = wocky_jingle_info_constructed;
  object_class->dispose = wocky_jingle_info_dispose;

  g_type_class_add_private (klass, sizeof (WockyJingleInfoPrivate));

  param_spec = g_param_spec_object ("porter", "WockyC2SPorter",
      "Porter for the current connection",
      WOCKY_TYPE_C2S_PORTER,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PORTER, param_spec);

  signals[STUN_SERVER_CHANGED] = g_signal_new ("stun-server-changed",
      G_OBJECT_CLASS_TYPE (object_class), G_SIGNAL_RUN_LAST,
      0, NULL, NULL, gabble_marshal_VOID__STRING_UINT,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);
}

WockyJingleInfo *
wocky_jingle_info_new (
    WockyPorter *porter)
{
  return g_object_new (WOCKY_TYPE_JINGLE_INFO,
      "porter", porter,
      NULL);
}

typedef struct {
    WockyJingleInfo *factory;
    gchar *stun_server;
    guint16 stun_port;
    WockyStunServerSource source;
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
  WockyJingleInfo *self = data->factory;
  WockyJingleInfoPrivate *priv = self->priv;
  GError *e = NULL;
  WockyStunServer *stun_server;
  gchar *address;
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

  address = g_inet_address_to_string (entries->data);
  g_resolver_free_addresses (entries);

  DEBUG ("Resolved STUN server %s:%u to %s:%u", data->stun_server,
      data->stun_port, address, data->stun_port);

  if (self == NULL)
    {
      g_free (address);
      goto out;
    }

  stun_server = wocky_stun_server_new (address, data->stun_port);

  if (data->source == WOCKY_STUN_SERVER_FALLBACK)
    {
      wocky_stun_server_free (priv->fallback_stun_server);
      priv->fallback_stun_server = stun_server;
    }
  else
    {
      wocky_stun_server_free (priv->stun_server);
      priv->stun_server = stun_server;

      g_signal_emit (self, signals[STUN_SERVER_CHANGED], 0,
          stun_server, data->stun_port);
    }

out:
  pending_stun_server_free (data);
  g_object_unref (resolver);
}

static void
wocky_jingle_info_take_stun_server_internal (
    WockyJingleInfo *self,
    gchar *stun_server,
    guint16 stun_port,
    WockyStunServerSource source)
{
  GResolver *resolver;
  PendingStunServer *data;

  if (stun_server == NULL)
    return;

  if (source == WOCKY_STUN_SERVER_USER_SPECIFIED)
    self->priv->get_stun_from_jingle = FALSE;

  resolver = g_resolver_get_default ();
  data = g_slice_new0 (PendingStunServer);

  DEBUG ("Resolving %s STUN server %s:%u",
      wocky_enum_to_nick (GABBLE_TYPE_STUN_SERVER_SOURCE, data->source),
      stun_server, stun_port);
  data->factory = self;
  g_object_add_weak_pointer (G_OBJECT (self), (gpointer *) &data->factory);
  data->stun_server = stun_server;
  data->stun_port = stun_port;
  data->source = source;

  data->cancellable = g_cancellable_new ();
  g_object_weak_ref (G_OBJECT (self), (GWeakNotify)g_cancellable_cancel,
      data->cancellable);

  g_resolver_lookup_by_name_async (resolver, stun_server,
      data->cancellable, stun_server_resolved_cb, data);
}

/*
 * wocky_jingle_info_take_stun_server:
 * @self: a #WockyJingleInfo object
 * @stun_server: (transfer full): the STUN server's address
 * @stun_port: the STUN server's port
 * @is_fallback: %TRUE if this is a last resort; %FALSE if this STUN server was
 *  provided by the user (whether by explicitly setting one, or by asking the
 *  user's XMPP server).
 */
void
wocky_jingle_info_take_stun_server (
    WockyJingleInfo *self,
    gchar *stun_server,
    guint16 stun_port,
    gboolean is_fallback)
{
  WockyStunServerSource source = is_fallback
      ? WOCKY_STUN_SERVER_FALLBACK
      : WOCKY_STUN_SERVER_USER_SPECIFIED;

  wocky_jingle_info_take_stun_server_internal (self, stun_server, stun_port,
      source);
}

static void
got_jingle_info_stanza (
    WockyJingleInfo *self,
    WockyStanza *stanza)
{
  WockyNode *node, *query_node;

  query_node = wocky_node_get_child_ns (
      wocky_stanza_get_top_node (stanza), "query", NS_GOOGLE_JINGLE_INFO);

  if (query_node == NULL)
    return;

  if (self->priv->get_stun_from_jingle)
    node = wocky_node_get_child (query_node, "stun");
  else
    node = NULL;

  if (node != NULL)
    {
      WockyNodeIter iter;

      /* TODO: use more than just the first stun server returned. */
      wocky_node_iter_init (&iter, node, "server", NULL);
      if (wocky_node_iter_next (&iter, &node))
        {
          const gchar *server;
          const gchar *port_attr;
          guint port = 0;

          server = wocky_node_get_attribute (node, "host");
          port_attr = wocky_node_get_attribute (node, "udp");

          if (port_attr != NULL)
            port = atoi (port_attr);

          if (server != NULL &&
              port_attr != NULL && port > 0 && port <= G_MAXUINT16)
            {
              DEBUG ("jingle info: got stun server %s, port %u", server,
                  port);
              wocky_jingle_info_take_stun_server_internal (self,
                  g_strdup (server), port, WOCKY_STUN_SERVER_DISCOVERED);
            }
        }
    }

#ifdef ENABLE_GOOGLE_RELAY
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
              g_free (self->priv->relay_token);
              self->priv->relay_token = g_strdup (token);
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
              g_free (self->priv->relay_server);
              self->priv->relay_server = g_strdup (server);
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
                  self->priv->relay_http_port = atoi (port);
                }
            }

          /* FIXME: these are not really actually used anywhere at
           * the moment, because we get the same info when creating
           * relay session. */
          port = wocky_node_get_attribute (subnode, "udp");

          if (port != NULL)
            {
              DEBUG ("jingle info: got relay udp port %s", port);
              self->priv->relay_udp = atoi (port);
            }

          port = wocky_node_get_attribute (subnode, "tcp");

          if (port != NULL)
            {
              DEBUG ("jingle info: got relay tcp port %s", port);
              self->priv->relay_tcp = atoi (port);
            }

          port = wocky_node_get_attribute (subnode, "tcpssl");

          if (port != NULL)
            {
              DEBUG ("jingle info: got relay tcpssl port %s", port);
              self->priv->relay_ssltcp = atoi (port);
            }

        }

    }
#endif  /* ENABLE_GOOGLE_RELAY */
}

static gboolean
jingle_info_cb (
    WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data)
{
  WockyJingleInfo *self = WOCKY_JINGLE_INFO (user_data);

  got_jingle_info_stanza (self, stanza);
  wocky_porter_acknowledge_iq (porter, stanza, NULL);

  return TRUE;
}

static void
jingle_info_reply_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyPorter *porter = WOCKY_PORTER (source);
  WockyJingleInfo *self = WOCKY_JINGLE_INFO (user_data);
  WockyStanza *reply = NULL;
  GError *error = NULL;

  reply = wocky_porter_send_iq_finish (porter, result, &error);
  if (reply != NULL &&
      !wocky_stanza_extract_errors (reply, NULL, &error, NULL, NULL))
    {
      got_jingle_info_stanza (self, reply);
    }
  else
    {
      DEBUG ("jingle info request failed: %s", error->message);
      g_clear_error (&error);
    }

  g_clear_object (&reply);
  g_object_unref (self);
}

static void
wocky_jingle_info_send_google_request (
    WockyJingleInfo *self)
{
  WockyJingleInfoPrivate *priv = self->priv;
  WockyStanza *stanza = wocky_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET, NULL,
      wocky_porter_get_bare_jid (priv->porter),
      '(', "query", ':', NS_GOOGLE_JINGLE_INFO, ')', NULL);

  wocky_porter_send_iq_async (priv->porter, stanza, NULL, jingle_info_reply_cb,
      g_object_ref (self));
  g_object_unref (stanza);

  priv->jingle_info_handler_id = wocky_c2s_porter_register_handler_from_server (
      WOCKY_C2S_PORTER (priv->porter),
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, jingle_info_cb, self,
      '(', "query", ':', NS_GOOGLE_JINGLE_INFO, ')', NULL);
}

static void
discover_stun_servers_cb (GObject *resolver,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyJingleInfo *self = WOCKY_JINGLE_INFO (user_data);
  WockyJingleInfoPrivate *priv = self->priv;
  GError *error = NULL;
  GList *targets;

  targets = g_resolver_lookup_service_finish (G_RESOLVER (resolver),
      result, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to discover STUN servers on %s: %s",
          priv->jid_domain, error->message);
      g_clear_error (&error);
    }
  else
    {
      DEBUG ("Discovered %d STUN servers on %s", g_list_length (targets),
          priv->jid_domain);

      /* TODO: use more than just the first. */
      if (targets != NULL)
        {
          GSrvTarget *target = targets->data;
          const gchar *hostname = g_srv_target_get_hostname (target);
          guint16 port = g_srv_target_get_port (target);

          DEBUG ("Found STUN server: %s:%d", hostname, port);

          wocky_jingle_info_take_stun_server (self, g_strdup (hostname), port,
              FALSE);
        }

      g_resolver_free_targets (targets);
    }

  g_object_unref (resolver);
  g_object_unref (self);
}

static void
wocky_jingle_info_lookup_srv (
    WockyJingleInfo *self)
{
  WockyJingleInfoPrivate *priv = self->priv;
  GResolver *resolver;

  g_assert (priv->jid_domain != NULL);
  DEBUG ("Discovering STUN servers on %s", priv->jid_domain);

  resolver = g_resolver_get_default ();
  g_resolver_lookup_service_async (resolver, "stun", "udp", priv->jid_domain,
      NULL, discover_stun_servers_cb, g_object_ref (self));
}

void
wocky_jingle_info_send_request (
    WockyJingleInfo *self,
    gboolean google_jingleinfo_supported)
{
  /* FIXME: we probably don't want to send either query if the user specified a
   * stun server (that is, get_stun_from_jingle is FALSE).
   */
  if (google_jingleinfo_supported)
    wocky_jingle_info_send_google_request (self);
  else
    wocky_jingle_info_lookup_srv (self);
}

/*
 * wocky_jingle_info_get_stun_servers:
 *
 * Grabs the currently known and resolved stun servers.
 *
 * Returns: (transfer container): a list of WockyJingleInfo structs
 */
GList *
wocky_jingle_info_get_stun_servers (
    WockyJingleInfo *self)
{
  WockyJingleInfoPrivate *priv = self->priv;
  GQueue stun_servers = G_QUEUE_INIT;

  if (priv->stun_server != NULL)
    g_queue_push_head (&stun_servers, priv->stun_server);

  /* Only add the fallback server as a last resort. */
  if (stun_servers.length == 0 &&
      priv->fallback_stun_server != NULL)
    g_queue_push_tail (&stun_servers, priv->fallback_stun_server);

  return stun_servers.head;
}

const gchar *
wocky_jingle_info_get_google_relay_token (
    WockyJingleInfo *self)
{
  return self->priv->relay_token;
}

WockyJingleRelay *
wocky_jingle_relay_new (
    WockyJingleRelayType type,
    const gchar *ip,
    guint port,
    const gchar *username,
    const gchar *password,
    guint component)
{
  WockyJingleRelay ret = { type, g_strdup (ip), port, g_strdup (username),
      g_strdup (password), component };

  return g_slice_dup (WockyJingleRelay, &ret);
}

void
wocky_jingle_relay_free (WockyJingleRelay *relay)
{
  g_free (relay->ip);
  g_free (relay->username);
  g_free (relay->password);
  g_slice_free (WockyJingleRelay, relay);
}

void
wocky_jingle_info_create_google_relay_session (
    WockyJingleInfo *self,
    guint components,
    WockyJingleInfoRelaySessionCb callback,
    gpointer user_data)
{
  WockyJingleInfoPrivate *priv = self->priv;

  g_return_if_fail (callback != NULL);

  if (priv->google_resolver == NULL)
    {
      priv->google_resolver = wocky_google_relay_resolver_new ();
    }

  wocky_google_relay_resolver_resolve (priv->google_resolver,
      components, priv->relay_server, priv->relay_http_port, priv->relay_token,
      callback, user_data);
}
