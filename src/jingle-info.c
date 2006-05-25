/*
 * jingle-info.c - Source for Jingle info discovery
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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
#include <stdlib.h>

#include "jingle-info.h"
#include "gabble-error.h"


/**
 * jingle_info_discover_servers:
 *
 * Discover available Jingle servers.
 *
 * @conn: The GabbleConnection# object initiating the discovery.
 */
void
jingle_info_discover_servers (GabbleConnection *conn)
{
  LmMessage *msg = NULL;
  LmMessageNode *node;
  GError *error = NULL;
  GabbleHandle handle;
  gchar *jid = NULL;

  if (!gabble_connection_get_self_handle (conn, &handle, &error))
    {
      g_warning ("%s: get_self_handle failed: %s\n", G_STRFUNC,
                 error->message);
      goto OUT;
    }

  if (!gabble_connection_inspect_handle (conn, TP_HANDLE_TYPE_CONTACT, handle,
                                         &jid, &error))
    {
      g_warning ("%s: get_self_handle failed: %s\n", G_STRFUNC,
                 error->message);
      goto OUT;
    }

  msg = lm_message_new_with_sub_type (jid, LM_MESSAGE_TYPE_IQ,
                                      LM_MESSAGE_SUB_TYPE_GET);

  node = lm_message_node_add_child (msg->node, "query", NULL);
  lm_message_node_set_attribute (node, "xmlns", NS_GOOGLE_JINGLE_INFO);

  if (!_gabble_connection_send (conn, msg, &error))
    {
      g_warning ("%s: send failed: %s\n", G_STRFUNC, error->message);
      goto OUT;
    }

OUT:
  if (jid)
    g_free (jid);

  if (msg)
    lm_message_unref (msg);

  if (error)
    g_error_free (error);
}


/**
 * jingle_info_iq_callback
 *
 * Called by loudmouth when we get an incoming <iq>. This handler
 * is concerned only with Jingle info queries.
 */
LmHandlerResult
jingle_info_iq_callback (LmMessageHandler *handler,
                         LmConnection *lmconn,
                         LmMessage *message,
                         gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  LmMessageSubType sub_type;
  LmMessageNode *query_node, *parent_node, *node;
  const gchar *str;
  guint port;

  query_node = lm_message_node_get_child (message->node, "query");

  if (!query_node || !_lm_message_node_has_namespace (query_node,
        NS_GOOGLE_JINGLE_INFO))
    {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  HANDLER_DEBUG (message->node, "got");

  sub_type = lm_message_get_sub_type (message);

  if (sub_type == LM_MESSAGE_SUB_TYPE_RESULT ||
      sub_type == LM_MESSAGE_SUB_TYPE_SET)
    {
      parent_node = lm_message_node_get_child (query_node, "stun");
      if (parent_node)
        {
          for (node = parent_node->children; node; node = node->next)
            {
              if (strcmp (node->name, "server") == 0)
                {
                  str = lm_message_node_get_attribute (node, "host");
                  if (str)
                    {
                      g_debug ("%s: setting 'stun-server' to '%s'", G_STRFUNC,
                               str);

                      g_object_set (conn, "stun-server", str, NULL);
                    }

                  str = lm_message_node_get_attribute (node, "udp");
                  if (str)
                    {
                      port = atoi (str);

                      g_debug ("%s: setting 'stun-port' to %d", G_STRFUNC, port);

                      g_object_set (conn, "stun-port", port, NULL);
                    }

                  /* only grab the first one for now */
                  break;
                }
            }
        }

      parent_node = lm_message_node_get_child (query_node, "relay");
      if (parent_node)
        {
          gboolean found_server = FALSE;

          for (node = parent_node->children; node; node = node->next)
            {
              if (!found_server && strcmp (node->name, "server") == 0)
                {
                  str = lm_message_node_get_attribute (node, "host");
                  if (str)
                    {
                      g_debug ("%s: setting 'stun-relay-server' to '%s'",
                               G_STRFUNC, str);

                      g_object_set (conn, "stun-relay-server", str, NULL);
                    }

                  str = lm_message_node_get_attribute (node, "udp");
                  if (str)
                    {
                      port = atoi (str);

                      g_debug ("%s: setting 'stun-relay-udp-port' to %d",
                               G_STRFUNC, port);

                      g_object_set (conn, "stun-relay-udp-port", port, NULL);
                    }

                  str = lm_message_node_get_attribute (node, "tcp");
                  if (str)
                    {
                      port = atoi (str);

                      g_debug ("%s: setting 'stun-relay-tcp-port' to %d",
                               G_STRFUNC, port);

                      g_object_set (conn, "stun-relay-tcp-port", port, NULL);
                    }

                  str = lm_message_node_get_attribute (node, "tcpssl");
                  if (str)
                    {
                      port = atoi (str);

                      g_debug ("%s: setting 'stun-relay-ssltcp-port' to %d",
                               G_STRFUNC, port);

                      g_object_set (conn, "stun-relay-ssltcp-port", port, NULL);
                    }

                  found_server = TRUE;
                }
              else if (strcmp (node->name, "token") == 0)
                {
                  str = lm_message_node_get_value (node);
                  if (str)
                    {
                      g_debug ("%s: setting 'stun-relay-magic-cookie' to '%s'",
                               G_STRFUNC, str);

                      g_object_set (conn, "stun-relay-magic-cookie", str, NULL);
                    }
                }
            }
        }

      if (sub_type == LM_MESSAGE_SUB_TYPE_SET)
        {
          _gabble_connection_send_iq_result (conn, message->node);
        }
    }
  else if (sub_type == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      GabbleXmppError xmpp_error = INVALID_XMPP_ERROR;

      node = lm_message_node_get_child (message->node, "error");
      if (node)
        {
          xmpp_error = gabble_xmpp_error_from_node (node);
        }

      str = gabble_xmpp_error_string (xmpp_error);

      g_warning ("%s: jingle info error: %s", G_STRFUNC,
          (str) ? str : "unknown error");
    }
  else
    {
      HANDLER_DEBUG (message->node, "unknown message sub type");
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

