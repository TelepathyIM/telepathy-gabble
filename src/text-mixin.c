/*
 * text-mixin.c - Gabble-specific bits for TpTextMixin
 * Copyright (C) 2006, 2007 Collabora Ltd.
 * Copyright (C) 2006, 2007 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
 *   @author Senko Rasic <senko@senko.net>
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

#define _GNU_SOURCE /* Needed for strptime (_XOPEN_SOURCE can also be used). */

#include <loudmouth/loudmouth.h>
#include <dbus/dbus-glib.h>
#include <string.h>
#include <time.h>

#include <telepathy-glib/text-mixin.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>

#define DEBUG_FLAG GABBLE_DEBUG_IM

#include "debug.h"
#include "gabble-connection.h"
#include "namespaces.h"
#include "roster.h"
#include "util.h"

#include "text-mixin.h"

/**
 * gabble_text_mixin_send
 *
 * Indirectly, implements D-Bus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text.
 *
 * @param type The Telepathy message type
 * @param subtype The Loudmouth message subtype
 * @param recipient The recipient's JID
 * @param text The text of the message
 * @param conn The Connection
 * @param emit_signal If true, emit Sent; if false, assume we'll get an
 *                    echo of the message and will emit Sent at that point
 * @param context The D-Bus method invocation context
 */
void
gabble_text_mixin_send (GObject *obj,
                        guint type,
                        guint subtype,
                        const char *recipient,
                        const gchar *text,
                        GabbleConnection *conn,
                        gboolean emit_signal,
                        DBusGMethodInvocation *context)
{
  TpTextMixin *mixin = TP_TEXT_MIXIN (obj);
  LmMessage *msg;
  gboolean result;
  time_t timestamp;
  GError *error;

  if (type > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      DEBUG ("invalid message type %u", type);

      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "invalid message type: %u", type);

      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

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

  msg = lm_message_new_with_sub_type (recipient, LM_MESSAGE_TYPE_MESSAGE, subtype);

  if (mixin->send_nick)
    {
      lm_message_node_add_own_nick (msg->node, conn);
      mixin->send_nick = FALSE;
    }

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

  result = _gabble_connection_send (conn, msg, &error);
  lm_message_unref (msg);

  if (!result)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (emit_signal)
    {
      timestamp = time (NULL);

      tp_svc_channel_type_text_emit_sent (obj, timestamp, type, text);
    }

  tp_svc_channel_type_text_return_from_send (context);
}

gboolean
gabble_text_mixin_parse_incoming_message (LmMessage *message,
                        const gchar **from,
                        time_t *stamp,
                        TpChannelTextMessageType *msgtype,
                        const gchar **body,
                        const gchar **body_offset,
                        TpChannelTextSendError *send_error)
{
  const gchar *type;
  LmMessageNode *node;

  *send_error = TP_CHANNEL_SEND_NO_ERROR;

  if (lm_message_get_sub_type (message) == LM_MESSAGE_SUB_TYPE_ERROR)
    {
      LmMessageNode *error_node;

      error_node = lm_message_node_get_child (message->node, "error");
      if (error_node)
        {
          GabbleXmppError err = gabble_xmpp_error_from_node (error_node);
          DEBUG ("got xmpp error: %s: %s", gabble_xmpp_error_string (err),
                 gabble_xmpp_error_description (err));

          /* these are based on descriptions of errors, and some testing */
          switch (err)
            {
              case XMPP_ERROR_SERVICE_UNAVAILABLE:
              case XMPP_ERROR_RECIPIENT_UNAVAILABLE:
                *send_error = TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE;
                break;

              case XMPP_ERROR_ITEM_NOT_FOUND:
              case XMPP_ERROR_JID_MALFORMED:
              case XMPP_ERROR_REMOTE_SERVER_TIMEOUT:
                *send_error = TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT;
                break;

              case XMPP_ERROR_FORBIDDEN:
                *send_error = TP_CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED;
                break;

              case XMPP_ERROR_RESOURCE_CONSTRAINT:
                *send_error = TP_CHANNEL_TEXT_SEND_ERROR_TOO_LONG;
                break;

              case XMPP_ERROR_FEATURE_NOT_IMPLEMENTED:
                *send_error = TP_CHANNEL_TEXT_SEND_ERROR_NOT_IMPLEMENTED;
                break;

              default:
                *send_error = TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
            }
        }
      else
        {
          *send_error = TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
        }
    }

  *from = lm_message_node_get_attribute (message->node, "from");
  if (*from == NULL)
    {
      NODE_DEBUG (message->node, "got a message without a from field");
      return FALSE;
    }

  type = lm_message_node_get_attribute (message->node, "type");

  /*
   * Parse timestamp of delayed messages. For non-delayed, it's
   * 0 and the channel code should set the current timestamp.
   */
  *stamp = 0;

  node = lm_message_node_get_child_with_namespace (message->node, "x",
      NS_X_DELAY);
  if (node != NULL)
    {
      const gchar *stamp_str, *p;
      struct tm stamp_tm = { 0, };

      stamp_str = lm_message_node_get_attribute (node, "stamp");
      if (stamp_str != NULL)
        {
          p = strptime (stamp_str, "%Y%m%dT%T", &stamp_tm);
          if (p == NULL || *p != '\0')
            {
              g_warning ("%s: malformed date string '%s' for jabber:x:delay",
                         G_STRFUNC, stamp_str);
            }
          else
            {
              *stamp = timegm (&stamp_tm);
            }
        }
    }

  /*
   * Parse body if it exists.
   */
  node = lm_message_node_get_child (message->node, "body");

  if (node)
    {
      *body = lm_message_node_get_value (node);
    }
  else
    {
      *body = NULL;
    }

  /* Messages starting with /me are ACTION messages, and the /me should be
   * removed. type="chat" messages are NORMAL.  everything else is
   * something that doesn't necessarily expect a reply or ongoing
   * conversation ("normal") or has been auto-sent, so we make it NOTICE in
   * all other cases. */

  *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE;
  *body_offset = *body;

  if (*body)
    {
      if (0 == strncmp (*body, "/me ", 4))
        {
          *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;
          *body_offset = *body + 4;
        }
      else if (type != NULL && (0 == strcmp (type, "chat") ||
                                0 == strcmp (type, "groupchat")))
        {
          *msgtype = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
          *body_offset = *body;
        }
    }

  return TRUE;
}
