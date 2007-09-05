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

#include <string.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/enums.h>

#include "conn-aliasing.h"
#include "pubsub.h"
#include "namespaces.h"
#include "util.h"
#include "conn-olpc.h"

static const GabblePubsubEventHandler pubsub_event_handlers[] =
{
    { NS_NICK, gabble_conn_aliasing_pep_nick_event_handler },
    { NS_OLPC_BUDDY_PROPS, olpc_buddy_info_properties_event_handler},
    { NS_OLPC_ACTIVITIES, olpc_buddy_info_activities_event_handler},
    { NS_OLPC_CURRENT_ACTIVITY, olpc_buddy_info_current_activity_event_handler},
    { NS_OLPC_ACTIVITY_PROPS, olpc_activities_properties_event_handler},
    { NULL, NULL}
};

gboolean
gabble_pubsub_event_handler (GabbleConnection *conn, LmMessage *msg,
    TpHandle handle)
{
  const GabblePubsubEventHandler *i;
  LmMessageNode *item_node;
  const gchar *event_ns;

  item_node = lm_message_node_find_child (msg->node, "item");
  if (item_node == NULL)
    {
      return FALSE;
    }

  if (item_node->children == NULL)
    {
      return FALSE;
    }

  /*
   * the namespace of the item is that of the first child of the <item> node
   */
  event_ns = lm_message_node_get_attribute (item_node->children, "xmlns");
  if (event_ns == NULL)
    {
      return FALSE;
    }

  for (i = pubsub_event_handlers; i->ns != NULL; i++)
    {
      if (strcmp (i->ns, event_ns) == 0)
        {
          i->handle_function (conn, msg, handle);
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
  LmMessage *msg;
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
  lm_message_unref (msg);
  return ret;
}

LmMessage *
pubsub_make_publish_msg (
    const gchar *to,
    const gchar *node_name,
    const gchar *item_ns,
    const gchar *item_name,
    LmMessageNode **node)
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
                 LmMessage *message,
                 gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)conn, TP_HANDLE_TYPE_CONTACT);
  LmMessageNode *node;
  TpHandle handle;
  const gchar *event_ns, *from;

  node = lm_message_node_get_child (message->node, "event");

  if (node)
    {
      event_ns = lm_message_node_get_attribute (node, "xmlns");
    }
  else
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  if (event_ns == NULL || !g_str_has_prefix (event_ns, NS_PUBSUB))
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  from = lm_message_node_get_attribute (message->node, "from");
  if (from == NULL)
    {
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  handle = tp_handle_ensure (contact_repo, from, NULL, NULL);
  if (handle == 0)
    {
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  gabble_pubsub_event_handler (conn, message, handle);

  tp_handle_unref (contact_repo, handle);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}
