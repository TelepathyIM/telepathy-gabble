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

#include <string.h>
#include <time.h>

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
  WockyNode *n = wocky_stanza_get_top_node (msg);

  switch (state)
    {
      case TP_CHANNEL_CHAT_STATE_GONE:
        node = lm_message_node_add_child (n, "gone", NULL);
        break;
      case TP_CHANNEL_CHAT_STATE_INACTIVE:
        node = lm_message_node_add_child (n, "inactive", NULL);
        break;
      case TP_CHANNEL_CHAT_STATE_ACTIVE:
        node = lm_message_node_add_child (n, "active", NULL);
        break;
      case TP_CHANNEL_CHAT_STATE_PAUSED:
        node = lm_message_node_add_child (n, "paused", NULL);
        break;
      case TP_CHANNEL_CHAT_STATE_COMPOSING:
        node = lm_message_node_add_child (n, "composing", NULL);
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
 * @flags: the flags used if sending is successful
 * @subtype: the Loudmouth message subtype
 * @state: the Telepathy chat state, or -1 if unknown or not applicable
 * @recipient: the recipient's JID
 * @send_nick: whether to include our own nick in the message
 */
void
gabble_message_util_send_message (GObject *obj,
                                  GabbleConnection *conn,
                                  TpMessage *message,
                                  TpMessageSendingFlags flags,
                                  LmMessageSubType subtype,
                                  TpChannelChatState state,
                                  const char *recipient,
                                  gboolean send_nick)
{
  GError *error = NULL;
  const GHashTable *part;
  LmMessage *msg;
  WockyNode *node;
  guint type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
  gboolean result = TRUE;
  const gchar *content_type, *text;
  gchar *id = NULL;
  guint n_parts;

#define INVALID_ARGUMENT(msg, ...) \
  G_STMT_START { \
    DEBUG (msg , ## __VA_ARGS__); \
    g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, \
        msg , ## __VA_ARGS__); \
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
  content_type = tp_asv_get_string (part, "content-type");
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
  node = wocky_stanza_get_top_node (msg);
  /* Generate a UUID for the message */
  id = gabble_generate_id ();
  lm_message_node_set_attribute (node, "id", id);
  tp_message_set_string (message, 0, "message-token", id);

  if (send_nick)
    lm_message_node_add_own_nick (node, conn);

  if (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION)
    {
      gchar *tmp;
      tmp = g_strconcat ("/me ", text, NULL);
      lm_message_node_add_child (node, "body", tmp);
      g_free (tmp);
    }
  else
    {
      lm_message_node_add_child (node, "body", text);
    }

  _add_chat_state (msg, state);

  result = _gabble_connection_send (conn, msg, &error);
  lm_message_unref (msg);

  if (!result)
    goto despair_island;

  tp_message_mixin_sent (obj, message, flags, id, NULL);
  g_free (id);

  return;

despair_island:
  g_assert (error != NULL);
  tp_message_mixin_sent (obj, message, 0, NULL, error);
  g_error_free (error);
  g_free (id);
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

TpChannelTextSendError
gabble_tp_send_error_from_wocky_xmpp_error (WockyXmppError err)
{
  switch (err)
    {
      /* Note: Google replies with <service-unavailable/> if you send a
       * message to someone you're not subscribed to. But
       * http://xmpp.org/rfcs/rfc3921.html#rules explicitly says that means
       * the user is offline and doesn't have offline storage. I think Google
       * should be returning <forbidden/> or <not-authorized/>. --wjt
       */
      case WOCKY_XMPP_ERROR_SERVICE_UNAVAILABLE:
      case WOCKY_XMPP_ERROR_RECIPIENT_UNAVAILABLE:
        return TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE;

      case WOCKY_XMPP_ERROR_ITEM_NOT_FOUND:
      case WOCKY_XMPP_ERROR_JID_MALFORMED:
      case WOCKY_XMPP_ERROR_REMOTE_SERVER_TIMEOUT:
        return TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT;

      case WOCKY_XMPP_ERROR_FORBIDDEN:
      case WOCKY_XMPP_ERROR_NOT_AUTHORIZED:
        return TP_CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED;

      case WOCKY_XMPP_ERROR_RESOURCE_CONSTRAINT:
        return TP_CHANNEL_TEXT_SEND_ERROR_TOO_LONG;

      case WOCKY_XMPP_ERROR_FEATURE_NOT_IMPLEMENTED:
        return TP_CHANNEL_TEXT_SEND_ERROR_NOT_IMPLEMENTED;

      default:
        return TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
    }
}

static TpChannelTextSendError
_tp_send_error_from_error_node (LmMessageNode *error_node,
                                TpDeliveryStatus *delivery_status)
{
  if (error_node != NULL)
    {
      GabbleXmppErrorType err_type = XMPP_ERROR_TYPE_UNDEFINED;
      GabbleXmppError err = gabble_xmpp_error_from_node (error_node, &err_type);

      DEBUG ("got xmpp error: %s (type=%u): %s", gabble_xmpp_error_string (err),
          err_type, gabble_xmpp_error_description (err));

      if (err_type == XMPP_ERROR_TYPE_WAIT)
        *delivery_status = TP_DELIVERY_STATUS_TEMPORARILY_FAILED;
      else
        *delivery_status = TP_DELIVERY_STATUS_PERMANENTLY_FAILED;

      /* these are based on descriptions of errors, and some testing */
      switch (err)
        {
        /* Note: Google replies with <service-unavailable/> if you send a
         * message to someone you're not subscribed to. But
         * http://xmpp.org/rfcs/rfc3921.html#rules explicitly says that means
         * the user is offline and doesn't have offline storage. I think Google
         * should be returning <forbidden/> or <not-authorized/>. --wjt
         */
        case XMPP_ERROR_SERVICE_UNAVAILABLE:
        case XMPP_ERROR_RECIPIENT_UNAVAILABLE:
          return TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE;

        case XMPP_ERROR_ITEM_NOT_FOUND:
        case XMPP_ERROR_JID_MALFORMED:
        case XMPP_ERROR_REMOTE_SERVER_TIMEOUT:
          return TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT;

        case XMPP_ERROR_FORBIDDEN:
        case XMPP_ERROR_NOT_AUTHORIZED:
          return TP_CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED;

        case XMPP_ERROR_RESOURCE_CONSTRAINT:
          return TP_CHANNEL_TEXT_SEND_ERROR_TOO_LONG;

        case XMPP_ERROR_FEATURE_NOT_IMPLEMENTED:
          return TP_CHANNEL_TEXT_SEND_ERROR_NOT_IMPLEMENTED;

        default:
          return TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
        }
    }
  else
    {
      return TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
    }
}


static gint
_tp_chat_state_from_message (LmMessage *message)
{
  LmMessageNode *node;

#define MAP_TO(str, state) \
  node = lm_message_node_get_child_with_namespace ( \
      wocky_stanza_get_top_node (message), str, \
      NS_CHAT_STATES); \
  if (node != NULL) \
    return state;

  MAP_TO ("active",    TP_CHANNEL_CHAT_STATE_ACTIVE);
  MAP_TO ("composing", TP_CHANNEL_CHAT_STATE_COMPOSING);
  MAP_TO ("inactive",  TP_CHANNEL_CHAT_STATE_INACTIVE);
  MAP_TO ("paused",    TP_CHANNEL_CHAT_STATE_PAUSED);
  MAP_TO ("gone",      TP_CHANNEL_CHAT_STATE_GONE);

#undef MAP_TO

  return -1;
}


/**
 * gabble_message_util_parse_incoming_message:
 * @message: an incoming XMPP message
 * @from: will be set to the message sender's jid.
 * @stamp: will be set to the message's timestamp if it's a delayed message, or
 *         to 0 otherwise.
 * @msgtype: will be set to the message's type.
 * @body_ret: will be set to the contents of the message's body, or %NULL if it
 *            had no body.
 * @state: will be set to the %TpChannelChatState of the message, or -1 if
 *         there was no chat state in the message.
 * @send_error: set to the relevant send error if the message contained an
 *              error node, or to %GABBLE_TEXT_CHANNEL_SEND_NO_ERROR otherwise.
 * @delivery_status: set to TemporarilyFailed if an <error type="wait"/> is
 *                   encountered, to PermanentlyFailed if any other <error/> is
 *                   encountered, and to Unknown otherwise.
 *
 * Parses an incoming <message> stanza, producing various bits of the message
 * as various out parameters.
 *
 * Returns: %TRUE if the <message> was successfully parsed, even if it
 *  contained no body, chat state or send error; %FALSE otherwise.
 */
gboolean
gabble_message_util_parse_incoming_message (LmMessage *message,
                                            const gchar **from,
                                            time_t *stamp,
                                            TpChannelTextMessageType *msgtype,
                                            const gchar **id,
                                            const gchar **body_ret,
                                            gint *state,
                                            TpChannelTextSendError *send_error,
                                            TpDeliveryStatus *delivery_status)
{
  const gchar *type, *body;
  LmMessageNode *node;

  *send_error = GABBLE_TEXT_CHANNEL_SEND_NO_ERROR;
  *delivery_status = TP_DELIVERY_STATUS_UNKNOWN;

  if (lm_message_get_sub_type (message) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      LmMessageNode *error_node;

      error_node = lm_message_node_get_child (
        wocky_stanza_get_top_node (message), "error");

      *send_error = _tp_send_error_from_error_node (error_node,
          delivery_status);
    }

  *id = lm_message_node_get_attribute (wocky_stanza_get_top_node (message),
      "id");

  *from = lm_message_node_get_attribute (wocky_stanza_get_top_node (message),
      "from");
  if (*from == NULL)
    {
      STANZA_DEBUG (message, "got a message without a from field");
      return FALSE;
    }

  type = lm_message_node_get_attribute (wocky_stanza_get_top_node (message),
    "type");

  /*
   * Parse timestamp of delayed messages. For non-delayed, it's
   * 0 and the channel code should set the current timestamp.
   */
  *stamp = 0;

  node = lm_message_node_get_child_with_namespace (
      wocky_stanza_get_top_node (message), "x", NS_X_DELAY);
  if (node != NULL)
    {
      const gchar *stamp_str;

      /* These timestamps do not contain a timezone, but are understood to be
       * in GMT. They're in the format yyyymmddThhmmss, so if we append 'Z'
       * we'll get (one of the many valid syntaxes for) an ISO-8601 timestamp.
       */
      stamp_str = lm_message_node_get_attribute (node, "stamp");

      if (stamp_str != NULL)
        {
          GTimeVal timeval = { 0, 0 };
          gchar *stamp_dup = g_strdup_printf ("%sZ", stamp_str);

          if (g_time_val_from_iso8601 (stamp_dup, &timeval))
            {
              *stamp = timeval.tv_sec;
            }
          else
            {
              DEBUG ("%s: malformed date string '%s' for jabber:x:delay",
                  G_STRFUNC, stamp_str);
            }

          g_free (stamp_dup);
        }
    }

  /*
   * Parse body if it exists.
   */
  node = lm_message_node_get_child (wocky_stanza_get_top_node (message),
      "body");

  if (node)
    {
      body = lm_message_node_get_value (node);
    }
  else
    {
      body = NULL;
    }

  /* Messages starting with /me are ACTION messages, and the /me should be
   * removed. type="chat" messages are NORMAL.  everything else is
   * something that doesn't necessarily expect a reply or ongoing
   * conversation ("normal") or has been auto-sent, so we make it NOTICE in
   * all other cases. */

  *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
  *body_ret = body;

  if (body != NULL)
    {
      if (type == NULL &&
          lm_message_node_get_child_with_namespace (
              wocky_stanza_get_top_node (message),
              "time", "google:timestamp") != NULL &&
          lm_message_node_get_child_with_namespace (
              wocky_stanza_get_top_node (message),
              "x", "jabber:x:delay") != NULL)
        {
          /* Google servers send offline messages without a type. Work around
           * this. */
          *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
        }
      else if (0 == strncmp (body, "/me ", 4))
        {
          *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;
          *body_ret = body + 4;
        }
      else if (type != NULL && (0 == strcmp (type, "chat") ||
                                0 == strcmp (type, "groupchat")))
        {
          *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
          *body_ret = body;
        }
    }

  /* Parse chat state if it exists. */
  *state = _tp_chat_state_from_message (message);

  return TRUE;
}
