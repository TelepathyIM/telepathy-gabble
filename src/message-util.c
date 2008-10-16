/*
 * message-util.c - Messages interface utility functions
 * Copyright (C) 2006-2008 Collabora Ltd.
 * Copyright (C) 2006-2008 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
 *   @author Senko Rasic <senko@senko.net>
 *   @author Will Thompson <will.thompson@collabora.co.uk>
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

#include "message-util.h"

#include <loudmouth/loudmouth.h>
#include <telepathy-glib/dbus.h>

#define DEBUG_FLAG GABBLE_DEBUG_IM
#include "debug.h"
#include "namespaces.h"
#include "util.h"


static void
_add_chat_state (LmMessage *msg,
                 TpChannelChatState state)
{
  LmMessageNode *node = NULL;

  switch (state)
    {
      case TP_CHANNEL_CHAT_STATE_GONE:
        node = lm_message_node_add_child (msg->node, "gone", NULL);
        break;
      case TP_CHANNEL_CHAT_STATE_INACTIVE:
        node = lm_message_node_add_child (msg->node, "inactive", NULL);
        break;
      case TP_CHANNEL_CHAT_STATE_ACTIVE:
        node = lm_message_node_add_child (msg->node, "active", NULL);
        break;
      case TP_CHANNEL_CHAT_STATE_PAUSED:
        node = lm_message_node_add_child (msg->node, "paused", NULL);
        break;
      case TP_CHANNEL_CHAT_STATE_COMPOSING:
        node = lm_message_node_add_child (msg->node, "composing", NULL);
        break;
    }

  if (node != NULL)
    {
      lm_message_node_set_attributes (node, "xmlns", NS_CHAT_STATES, NULL);
    }
}


/**
 * gabble_message_util_send_message:
 * @obj: a channel implementation featuring TpMessageMixin
 * @conn: the connection owning this channel
 * @message: the message to be sent
 * @subtype: the Loudmouth message subtype
 * @state: the Telepathy chat state, or -1 if unknown or not applicable
 * @recipient: the recipient's JID
 * @send_nick: whether to include our own nick in the message
 */
void
gabble_message_util_send_message (GObject *obj,
                                  GabbleConnection *conn,
                                  TpMessage *message,
                                  LmMessageSubType subtype,
                                  TpChannelChatState state,
                                  const char *recipient,
                                  gboolean send_nick)
{
  GError *error = NULL;
  const GHashTable *part;
  LmMessage *msg;
  guint type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
  gboolean result = TRUE;
  const gchar *content_type, *text;
  guint n_parts;

#define INVALID_ARGUMENT(msg, args...) \
  G_STMT_START { \
    DEBUG (msg , ## args); \
    g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, \
        msg , ## args); \
    goto despair_island; \
  } G_STMT_END

  part = tp_message_peek (message, 0);

  if (tp_asv_lookup (part, "message-type") != NULL)
    type = tp_asv_get_uint32 (part, "message-type", &result);

  if (!result)
    INVALID_ARGUMENT ("message-type must be a 32-bit unsigned integer");

  if (type >= NUM_TP_CHANNEL_TEXT_MESSAGE_TYPES)
    INVALID_ARGUMENT ("invalid message type: %u", type);

  n_parts = tp_message_count_parts (message);

  if (n_parts != 2)
    INVALID_ARGUMENT ("message must contain exactly 1 part, not %u",
        (n_parts - 1));

  part = tp_message_peek (message, 1);
  content_type = tp_asv_get_string (part, "type");
  text = tp_asv_get_string (part, "content");

  if (content_type == NULL || tp_strdiff (content_type, "text/plain"))
    INVALID_ARGUMENT ("message must be text/plain");

  if (text == NULL)
    INVALID_ARGUMENT ("content must be a UTF-8 string");

  /* Okay, it's valid. Let's send it. */

  if (!subtype)
    {
      switch (type)
        {
        case TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL:
        case TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION:
          subtype = LM_MESSAGE_SUB_TYPE_CHAT;
          break;
        case TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE:
          subtype = LM_MESSAGE_SUB_TYPE_NORMAL;
          break;
        }
    }

  msg = lm_message_new_with_sub_type (recipient, LM_MESSAGE_TYPE_MESSAGE,
      subtype);

  if (send_nick)
    lm_message_node_add_own_nick (msg->node, conn);

  if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION)
    {
      gchar *tmp;
      tmp = g_strconcat ("/me ", text, NULL);
      lm_message_node_add_child (msg->node, "body", tmp);
      g_free (tmp);
    }
  else
    {
      lm_message_node_add_child (msg->node, "body", text);
    }

  _add_chat_state (msg, state);

  result = _gabble_connection_send (conn, msg, &error);
  lm_message_unref (msg);

  if (!result)
    goto despair_island;

  tp_message_mixin_sent (obj, message, "", NULL);

  return;

despair_island:
  g_assert (error != NULL);
  tp_message_mixin_sent (obj, message, NULL, error);
  g_error_free (error);
}


/**
 * gabble_message_util_send_chat_state:
 * @obj: a channel implementation featuring TpMessageMixin
 * @conn: the connection owning this channel
 * @subtype: the Loudmouth message subtype
 * @state: the Telepathy chat state
 * @recipient: the recipient's JID
 * @error: pointer in which to return a GError in case of failure.
 *
 * Returns: %TRUE if the message was sent successfully; %FALSE otherwise.
 */
gboolean
gabble_message_util_send_chat_state (GObject *obj,
                                     GabbleConnection *conn,
                                     LmMessageSubType subtype,
                                     TpChannelChatState state,
                                     const char *recipient,
                                     GError **error)
{
  LmMessage *msg = lm_message_new_with_sub_type (recipient,
      LM_MESSAGE_TYPE_MESSAGE, subtype);
  gboolean result;

  _add_chat_state (msg, state);

  result = _gabble_connection_send (conn, msg, error);
  lm_message_unref (msg);

  return result;
}
