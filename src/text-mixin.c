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

#include "config.h"
#include "text-mixin.h"

#include <string.h>

#include <dbus/dbus-glib.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/text-mixin.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>

#define DEBUG_FLAG GABBLE_DEBUG_IM

#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "roster.h"
#include "util.h"

/**
 * gabble_text_mixin_init:
 * @obj_cls: The class of the implementation that uses this mixin
 * @offset: The offset of the GabbleTextMixinClass within the class structure
 * @send_nick: %TRUE if the user's nick should be included in messages
 *             sent through this channel
 *
 * Initialize the text mixin. Should be called instead of #tp_text_mixin_init
 * from the implementation's init function.
 */
void
gabble_text_mixin_init (GObject *obj,
                        glong offset,
                        TpHandleRepoIface *contacts_repo,
                        gboolean send_nick)
{
  GabbleTextMixin *mixin;

  tp_text_mixin_init (obj, offset, contacts_repo);

  mixin = GABBLE_TEXT_MIXIN (obj);

  mixin->send_nick = send_nick;
}

/**
 * gabble_text_mixin_send
 *
 * Indirectly, implements D-Bus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text.
 *
 * @param type The Telepathy message type
 * @param subtype The Loudmouth message subtype
 * @param state The Telepathy chat state type
 * @param recipient The recipient's JID
 * @param text The text of the message (if NULL, the message won't have body)
 * @param conn The Connection
 * @param emit_signal If true, emit Sent; if false, assume we'll get an
 *                    echo of the message and will emit Sent at that point
 * @param error The GError
 */
gboolean
gabble_text_mixin_send (GObject *obj,
                        guint type,
                        guint subtype,
                        gint state,
                        const char *recipient,
                        const gchar *text,
                        GabbleConnection *conn,
                        gboolean emit_signal,
                        GError **error)
{
  GabbleTextMixin *mixin = GABBLE_TEXT_MIXIN (obj);
  LmMessage *msg;
  LmMessageNode *node;
  gboolean result;
  time_t timestamp;

  if (type >= NUM_TP_CHANNEL_TEXT_MESSAGE_TYPES)
    {
      DEBUG ("invalid message type %u", type);

      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "invalid message type: %u", type);

      return FALSE;
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

  msg = lm_message_new_with_sub_type (recipient, LM_MESSAGE_TYPE_MESSAGE,
      subtype);

  if (mixin->send_nick)
    {
      lm_message_node_add_own_nick (msg->node, conn);
      mixin->send_nick = FALSE;
    }

  if (text != NULL)
    {
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
    }

  node = NULL;

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

  result = _gabble_connection_send (conn, msg, error);
  lm_message_unref (msg);

  if (!result)
    {
      return FALSE;
    }

  if (emit_signal && text != NULL)
    {
      timestamp = time (NULL);

      tp_svc_channel_type_text_emit_sent (obj, timestamp, type, text);
    }
  return TRUE;
}

