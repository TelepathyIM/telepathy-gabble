/*
 * wocky-pubsub.c - Wocky Pubsub
 * Copyright (C) 2007 Collabora Ltd.
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
#include "wocky-pubsub.h"

#include <string.h>

#include <wocky/wocky-porter.h>
#include <wocky/wocky-utils.h>
#include <wocky/wocky-namespaces.h>

#include "conn-aliasing.h"
#include "namespaces.h"
#include "util.h"
#include "conn-olpc.h"
#include "conn-location.h"

G_DEFINE_TYPE (WockyPubsub, wocky_pubsub, G_TYPE_OBJECT)

typedef struct
{
    gchar *ns;
    WockyPubsubEventHandlerFunction handle_function;
    gpointer user_data;
    guint id;
} PubsubEventHandler;

static PubsubEventHandler *
pubsub_event_handler_new (const gchar *ns,
    WockyPubsubEventHandlerFunction func,
    gpointer user_data,
    guint id)
{
  PubsubEventHandler *handler = g_slice_new (PubsubEventHandler);
  handler->ns = g_strdup (ns);
  handler->handle_function = func;
  handler->user_data = user_data;
  handler->id = id;

  return handler;
}

static void
pubsub_event_handler_free (PubsubEventHandler *handler)
{
  g_free (handler->ns);
  g_slice_free (PubsubEventHandler, handler);
}

/* signal enum */
enum
{
  LAST_SIGNAL,
};

/*
static guint signals[LAST_SIGNAL] = {0};
*/

/* private structure */
typedef struct _WockyPubsubPrivate WockyPubsubPrivate;

struct _WockyPubsubPrivate
{
  WockySession *session;
  WockyPorter *porter;

  guint handler_id;

  /* list of owned (PubsubEventHandler *) */
  GSList *handlers;
  guint last_id_used;

  gboolean dispose_has_run;
};

#define WOCKY_PUBSUB_GET_PRIVATE(o)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), WOCKY_TYPE_PUBSUB, \
    WockyPubsubPrivate))

static void
wocky_pubsub_init (WockyPubsub *obj)
{
  /*
  WockyPubsub *self = WOCKY_PUBSUB (obj);
  WockyPubsubPrivate *priv = WOCKY_PUBSUB_GET_PRIVATE (self);
  */
}

static void
wocky_pubsub_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  switch (property_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
wocky_pubsub_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  switch (property_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}


static void
wocky_pubsub_dispose (GObject *object)
{
  WockyPubsub *self = WOCKY_PUBSUB (object);
  WockyPubsubPrivate *priv = WOCKY_PUBSUB_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->handler_id != 0)
    {
      wocky_porter_unregister_handler (priv->porter, priv->handler_id);
      priv->handler_id = 0;
    }

  if (priv->porter != NULL)
    g_object_unref (priv->porter);

  if (G_OBJECT_CLASS (wocky_pubsub_parent_class)->dispose)
    G_OBJECT_CLASS (wocky_pubsub_parent_class)->dispose (object);
}

static void
wocky_pubsub_finalize (GObject *object)
{
  WockyPubsub *self = WOCKY_PUBSUB (object);
  WockyPubsubPrivate *priv = WOCKY_PUBSUB_GET_PRIVATE (self);
  GSList *l;

  for (l = priv->handlers; l != NULL; l = g_slist_next (l))
    {
      PubsubEventHandler *handler = (PubsubEventHandler *) l->data;

      pubsub_event_handler_free (handler);
    }
  g_slist_free (priv->handlers);

  G_OBJECT_CLASS (wocky_pubsub_parent_class)->finalize (object);
}

static void
wocky_pubsub_class_init (WockyPubsubClass *wocky_pubsub_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (wocky_pubsub_class);

  g_type_class_add_private (wocky_pubsub_class,
      sizeof (WockyPubsubPrivate));

  object_class->set_property = wocky_pubsub_set_property;
  object_class->get_property = wocky_pubsub_get_property;
  object_class->dispose = wocky_pubsub_dispose;
  object_class->finalize = wocky_pubsub_finalize;
}

static gboolean
gabble_pubsub_event_handler (WockyPubsub *self,
    WockyXmppStanza *msg,
    const gchar *from,
    WockyXmppNode *item_node)
{
  WockyPubsubPrivate *priv = WOCKY_PUBSUB_GET_PRIVATE (self);
  const gchar *event_ns;
  GSList *l;

  if (node_iter (item_node) == NULL)
    {
      return FALSE;
    }

  /*
   * the namespace of the item is that of the first child of the <item> node
   */
  event_ns = wocky_xmpp_node_get_ns (node_iter_data (node_iter (item_node)));

  if (event_ns == NULL)
    {
      return FALSE;
    }

  for (l = priv->handlers; l != NULL; l = g_slist_next (l))
    {
      PubsubEventHandler *handler = l->data;

      if (!wocky_strdiff (handler->ns, event_ns))
        {
          handler->handle_function (self, msg, from, handler->user_data);
          return TRUE;
        }
    }

  return FALSE;
}

gboolean
pubsub_query (
    GabbleConnection *conn,
    const gchar *jid,
    const gchar *ns,
    GabbleConnectionMsgReplyFunc reply_func,
    gpointer user_data)
{
  WockyXmppStanza *msg;
  gboolean ret;

  msg = wocky_xmpp_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_GET,
      NULL, jid,
      WOCKY_NODE, "pubsub",
        WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_PUBSUB,
        WOCKY_NODE, "items",
          WOCKY_NODE_ATTRIBUTE, "node", ns,
        WOCKY_NODE_END,
      WOCKY_NODE_END, WOCKY_STANZA_END);

  ret = _gabble_connection_send_with_reply (conn, msg, reply_func, NULL,
      user_data, NULL);
  g_object_unref (msg);
  return ret;
}

WockyXmppStanza *
pubsub_make_publish_msg (
    const gchar *to,
    const gchar *node_name,
    const gchar *item_ns,
    const gchar *item_name,
    WockyXmppNode **node)
{
  return wocky_xmpp_stanza_build (
      WOCKY_STANZA_TYPE_IQ, WOCKY_STANZA_SUB_TYPE_SET,
      NULL, to,
      WOCKY_NODE, "pubsub",
        WOCKY_NODE_XMLNS, WOCKY_XMPP_NS_PUBSUB,
        WOCKY_NODE, "publish",
          WOCKY_NODE_ATTRIBUTE, "node", node_name,
          WOCKY_NODE, "item",
            WOCKY_NODE, item_name,
              WOCKY_NODE_ASSIGN_TO, node,
              WOCKY_NODE_XMLNS, item_ns,
            WOCKY_NODE_END,
          WOCKY_NODE_END,
        WOCKY_NODE_END,
      WOCKY_NODE_END, WOCKY_STANZA_END);
}

/**
 * pubsub_msg_event_cb
 *
 * Called by Wocky when we get an incoming <message>. This handler handles
 * pubsub events.
 */
static gboolean
pubsub_msg_event_cb (WockyPorter *porter,
    WockyXmppStanza *message,
    gpointer user_data)
{
  WockyPubsub *self = WOCKY_PUBSUB (user_data);
  WockyXmppNode *node;
  const gchar *event_ns, *from;

  node = wocky_xmpp_node_get_child (message->node, "event");

  if (node)
    {
      event_ns = wocky_xmpp_node_get_ns (node);
    }
  else
    {
      return FALSE;
    }

  if (event_ns == NULL || !g_str_has_prefix (event_ns, WOCKY_XMPP_NS_PUBSUB))
    {
      return FALSE;
    }

  from = wocky_xmpp_node_get_attribute (message->node, "from");
  if (from == NULL)
    {
      return TRUE;
    }

  node = wocky_xmpp_node_get_child (node, "items");
  if (node == NULL)
    {
      return TRUE;
    }

  node = wocky_xmpp_node_get_child (node, "item");
  if (node == NULL)
    {
      return TRUE;
    }

  gabble_pubsub_event_handler (self, message, from, node);

  return TRUE;
}

WockyPubsub *
wocky_pubsub_new (void)
{
  return g_object_new (WOCKY_TYPE_PUBSUB,
      NULL);
}

void
wocky_pubsub_start (WockyPubsub *self,
    WockySession *session)
{
  WockyPubsubPrivate *priv = WOCKY_PUBSUB_GET_PRIVATE (self);

  g_assert (priv->session == NULL);
  priv->session = session;

  priv->porter = wocky_session_get_porter (priv->session);
  g_object_ref (priv->porter);

  /* Register message handler */
  priv->handler_id = wocky_porter_register_handler (priv->porter,
      WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE, NULL,
      WOCKY_PORTER_HANDLER_PRIORITY_MAX,
      pubsub_msg_event_cb, self, WOCKY_STANZA_END);
}

guint
wocky_pubsub_register_event_handler (WockyPubsub *self,
    const gchar *ns,
    WockyPubsubEventHandlerFunction func,
    gpointer user_data)
{
  WockyPubsubPrivate *priv = WOCKY_PUBSUB_GET_PRIVATE (self);
  PubsubEventHandler *handler;

  priv->last_id_used++;
  handler = pubsub_event_handler_new (ns, func, user_data, priv->last_id_used);

  priv->handlers = g_slist_append (priv->handlers, handler);

  return priv->last_id_used;
}
