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

#include "jingle-info.h"

#include <stdlib.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA
#include "debug.h"
#include "google-relay.h"
#include "gabble-signals-marshal.h"
#include "namespaces.h"

static gboolean jingle_info_cb (
    WockyPorter *porter,
    WockyStanza *stanza,
    gpointer user_data);

struct _GabbleJingleInfoPrivate {
    WockyPorter *porter;
    guint jingle_info_handler_id;

    GabbleGoogleRelayResolver *google_resolver;

    gchar *stun_server;
    guint16 stun_port;
    gchar *fallback_stun_server;
    guint16 fallback_stun_port;
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
gabble_jingle_info_set_test_mode (void)
{
  test_mode = TRUE;
}

G_DEFINE_TYPE (GabbleJingleInfo, gabble_jingle_info, G_TYPE_OBJECT)

static void
gabble_jingle_info_init (GabbleJingleInfo *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_JINGLE_INFO,
      GabbleJingleInfoPrivate);

  self->priv->relay_http_port = 80;
  self->priv->get_stun_from_jingle = TRUE;
}

static void
gabble_jingle_info_get_property (
    GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleJingleInfo *self = GABBLE_JINGLE_INFO (object);
  GabbleJingleInfoPrivate *priv = self->priv;

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
gabble_jingle_info_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleJingleInfo *self = GABBLE_JINGLE_INFO (object);
  GabbleJingleInfoPrivate *priv = self->priv;

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
gabble_jingle_info_constructed (GObject *object)
{
  GabbleJingleInfo *self = GABBLE_JINGLE_INFO (object);
  GabbleJingleInfoPrivate *priv = self->priv;
  GObjectClass *parent_class = gabble_jingle_info_parent_class;

  if (parent_class->constructed != NULL)
    parent_class->constructed (object);

  g_assert (priv->porter != NULL);
  priv->jingle_info_handler_id = wocky_c2s_porter_register_handler_from_server (
      WOCKY_C2S_PORTER (priv->porter),
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      WOCKY_PORTER_HANDLER_PRIORITY_NORMAL, jingle_info_cb, self,
      '(', "query", ':', NS_GOOGLE_JINGLE_INFO, ')', NULL);
}

static void
gabble_jingle_info_dispose (GObject *object)
{
  GabbleJingleInfo *self = GABBLE_JINGLE_INFO (object);
  GabbleJingleInfoPrivate *priv = self->priv;
  GObjectClass *parent_class = gabble_jingle_info_parent_class;

  if (priv->porter != NULL)
    {
      g_assert (priv->jingle_info_handler_id != 0);
      wocky_porter_unregister_handler (priv->porter, priv->jingle_info_handler_id);
      g_clear_object (&priv->porter);
    }

  if (priv->google_resolver != NULL)
    {
      gabble_google_relay_resolver_destroy (priv->google_resolver);
      priv->google_resolver = NULL;
    }

  g_free (priv->stun_server);
  priv->stun_server = NULL;
  g_free (priv->fallback_stun_server);
  priv->fallback_stun_server = NULL;
  g_free (priv->relay_token);
  priv->relay_token = NULL;
  g_free (priv->relay_server);
  priv->relay_server = NULL;

  if (parent_class->dispose != NULL)
    parent_class->dispose (object);
}

static void
gabble_jingle_info_class_init (GabbleJingleInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  object_class->get_property = gabble_jingle_info_get_property;
  object_class->set_property = gabble_jingle_info_set_property;
  object_class->constructed = gabble_jingle_info_constructed;
  object_class->dispose = gabble_jingle_info_dispose;

  g_type_class_add_private (klass, sizeof (GabbleJingleInfoPrivate));

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

GabbleJingleInfo *
gabble_jingle_info_new (
    WockyPorter *porter)
{
  return g_object_new (GABBLE_TYPE_JINGLE_INFO,
      "porter", porter,
      NULL);
}

typedef struct {
    GabbleJingleInfo *factory;
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
  GabbleJingleInfo *self = data->factory;
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

void
gabble_jingle_info_take_stun_server (
    GabbleJingleInfo *self,
    gchar *stun_server,
    guint16 stun_port,
    gboolean is_fallback)
{
  GResolver *resolver;
  PendingStunServer *data;

  if (stun_server == NULL)
    return;

  if (!is_fallback)
    self->priv->get_stun_from_jingle = FALSE;

  resolver = g_resolver_get_default ();
  data = g_slice_new0 (PendingStunServer);

  DEBUG ("Resolving %s STUN server %s:%u",
      is_fallback ? "fallback" : "primary", stun_server, stun_port);
  data->factory = self;
  g_object_add_weak_pointer (G_OBJECT (self), (gpointer *) &data->factory);
  data->stun_server = stun_server;
  data->stun_port = stun_port;
  data->fallback = is_fallback;

  data->cancellable = g_cancellable_new ();
  g_object_weak_ref (G_OBJECT (self), (GWeakNotify)g_cancellable_cancel,
      data->cancellable);

  g_resolver_lookup_by_name_async (resolver, stun_server,
      data->cancellable, stun_server_resolved_cb, data);
}

static void
got_jingle_info_stanza (
    GabbleJingleInfo *self,
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
      node = wocky_node_get_child (node, "server");

      if (node != NULL)
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
              gabble_jingle_info_take_stun_server (self,
                  g_strdup (server), port, FALSE);
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
  GabbleJingleInfo *self = GABBLE_JINGLE_INFO (user_data);

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
  GabbleJingleInfo *self = GABBLE_JINGLE_INFO (user_data);
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

void
gabble_jingle_info_send_request (GabbleJingleInfo *self)
{
  GabbleJingleInfoPrivate *priv = self->priv;
  WockyStanza *stanza = wocky_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET, NULL,
      wocky_porter_get_bare_jid (priv->porter),
      '(', "query", ':', NS_GOOGLE_JINGLE_INFO, ')', NULL);

  wocky_porter_send_iq_async (priv->porter, stanza, NULL, jingle_info_reply_cb,
      g_object_ref (self));
  g_object_unref (stanza);
}

gboolean
gabble_jingle_info_get_stun_server (
    GabbleJingleInfo *self,
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

const gchar *
gabble_jingle_info_get_google_relay_token (
    GabbleJingleInfo *self)
{
  return self->priv->relay_token;
}

GabbleJingleRelay *
gabble_jingle_relay_new (
    GabbleJingleRelayType type,
    const gchar *ip,
    guint port,
    const gchar *username,
    const gchar *password,
    guint component)
{
  GabbleJingleRelay ret = { type, g_strdup (ip), port, g_strdup (username),
      g_strdup (password), component };

  return g_slice_dup (GabbleJingleRelay, &ret);
}

void
gabble_jingle_relay_free (GabbleJingleRelay *relay)
{
  g_free (relay->ip);
  g_free (relay->username);
  g_free (relay->password);
  g_slice_free (GabbleJingleRelay, relay);
}

void
gabble_jingle_info_create_google_relay_session (
    GabbleJingleInfo *self,
    guint components,
    GabbleJingleInfoRelaySessionCb callback,
    gpointer user_data)
{
  GabbleJingleInfoPrivate *priv = self->priv;

  g_return_if_fail (callback != NULL);

  if (priv->google_resolver == NULL)
    {
      priv->google_resolver = gabble_google_relay_resolver_new ();
    }

  gabble_google_relay_resolver_resolve (priv->google_resolver,
      components, priv->relay_server, priv->relay_http_port, priv->relay_token,
      callback, user_data);
}
