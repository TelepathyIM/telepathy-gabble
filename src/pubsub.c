/*
 * pubsub.c - Gabble Pubsub functions
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
#include "pubsub.h"

#include <string.h>

#include <loudmouth/loudmouth.h>
#include <telepathy-glib/enums.h>

#include "conn-aliasing.h"
#include "namespaces.h"
#include "util.h"
#include "conn-olpc.h"
#include "conn-location.h"

typedef struct
{
    const gchar *ns;
    GabblePubsubEventHandlerFunction handle_function;
} GabblePubsubEventHandler;

static const GabblePubsubEventHandler pubsub_event_handlers[] =
{
    { NS_NICK, gabble_conn_aliasing_pep_nick_event_handler },
    { NS_OLPC_BUDDY_PROPS, olpc_buddy_info_properties_event_handler},
    { NS_OLPC_ACTIVITIES, olpc_buddy_info_activities_event_handler},
    { NS_OLPC_CURRENT_ACTIVITY, olpc_buddy_info_current_activity_event_handler},
    { NS_OLPC_ACTIVITY_PROPS, olpc_activities_properties_event_handler},
    { NS_GEOLOC, geolocation_event_handler},
    { NULL, NULL}
};

static gboolean
gabble_pubsub_event_handler (GabbleConnection *conn,
    WockyXmppStanza *msg,
    const gchar *from,
    WockyXmppNode *item_node)
{
  const GabblePubsubEventHandler *i;
  const gchar *event_ns;

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

  for (i = pubsub_event_handlers; i->ns != NULL; i++)
    {
      if (strcmp (i->ns, event_ns) == 0)
        {
          i->handle_function (conn, msg, from);
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

  msg = lm_message_build (jid, LM_MESSAGE_TYPE_IQ,
      '@', "type", "get",
      '(', "pubsub", "",
        '@', "xmlns", NS_PUBSUB,
        '(', "items", "",
          '@', "node", ns,
        ')',
      ')', NULL);

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
  return lm_message_build (to, LM_MESSAGE_TYPE_IQ,
    '@', "type", "set",
    '(', "pubsub", "",
      '@', "xmlns", NS_PUBSUB,
      '(', "publish", "",
          '@', "node", node_name,
        '(', "item", "",
          '(', item_name, "",
            '*', node,
            '@', "xmlns", item_ns,
          ')',
        ')',
      ')',
    ')', NULL);
}

/**
 * pubsub_msg_event_cb
 *
 * Called by loudmouth when we get an incoming <message>. This handler handles
 * pubsub events.
 */
LmHandlerResult
pubsub_msg_event_cb (LmMessageHandler *handler,
    LmConnection *connection,
    WockyXmppStanza *message,
    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  WockyXmppNode *node;
  const gchar *event_ns, *from;

  node = wocky_xmpp_node_get_child (message->node, "event");

  if (node)
    {
      event_ns = wocky_xmpp_node_get_ns (node);
    }
  else
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (event_ns == NULL || !g_str_has_prefix (event_ns, NS_PUBSUB))
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  from = wocky_xmpp_node_get_attribute (message->node, "from");
  if (from == NULL)
    {
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  node = wocky_xmpp_node_get_child (node, "items");
  if (node == NULL)
    {
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  node = wocky_xmpp_node_get_child (node, "item");
  if (node == NULL)
    {
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  gabble_pubsub_event_handler (conn, message, from, node);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}
