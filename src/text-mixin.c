/*
 * text-mixin.c - Source for GabbleTextMixin
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <loudmouth/loudmouth.h>

#include "telepathy-constants.h"
#include "telepathy-errors.h"

#include "text-mixin.h"
#include "text-mixin-signals-marshal.h"

#include "gabble-connection.h"
#include "gabble-im-channel.h"
#include "gabble-muc-channel.h"

/* allocator */

typedef struct _GabbleAllocator GabbleAllocator;
struct _GabbleAllocator
{
  gulong size;
  guint limit;
  guint count;
};

#define ga_new(alloc, type) \
    ((type *) gabble_allocator_alloc (alloc))
#define ga_new0(alloc, type) \
    ((type *) gabble_allocator_alloc0 (alloc))

static GabbleAllocator *
gabble_allocator_new (gulong size, guint limit)
{
  GabbleAllocator *alloc;

  g_assert (size > 0);
  g_assert (limit > 0);

  alloc = g_new0 (GabbleAllocator, 1);

  alloc->size = size;
  alloc->limit = limit;

  return alloc;
}

static void gabble_allocator_destroy (GabbleAllocator *alloc)
{
  g_free (alloc);
}

/* FIXME - warning: defined but not used 
static gpointer gabble_allocator_alloc (GabbleAllocator *alloc)
{
  gpointer ret;

  g_assert (alloc != NULL);
  g_assert (alloc->count <= alloc->limit);

  if (alloc->count == alloc->limit)
    {
      ret = NULL;
    }
  else
    {
      ret = g_malloc (alloc->size);
      alloc->count++;
    }

  return ret;
}
*/

static gpointer gabble_allocator_alloc0 (GabbleAllocator *alloc)
{
  gpointer ret;

  g_assert (alloc != NULL);
  g_assert (alloc->count <= alloc->limit);

  if (alloc->count == alloc->limit)
    {
      ret = NULL;
    }
  else
    {
      ret = g_malloc0 (alloc->size);
      alloc->count++;
    }

  return ret;
}

static void gabble_allocator_free (GabbleAllocator *alloc, gpointer thing)
{
  g_assert (alloc != NULL);
  g_assert (thing != NULL);

  g_free (thing);
  alloc->count--;
}

/* pending message */
#define MAX_PENDING_MESSAGES 256
#define MAX_MESSAGE_SIZE 8*1024 - 1

typedef struct _GabblePendingMessage GabblePendingMessage;
struct _GabblePendingMessage
{
  guint id;
  time_t timestamp;
  GabbleHandle sender;
  TpChannelTextMessageType type;
  char *text;
};

/**
 * gabble_text_mixin_class_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObjectClass
 */
GQuark
gabble_text_mixin_class_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("TextMixinClassOffsetQuark");
  return offset_quark;
}

/**
 * gabble_text_mixin_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObject
 */
GQuark
gabble_text_mixin_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("TextMixinOffsetQuark");
  return offset_quark;
}


/* GabbleTextMixin */
void
gabble_text_mixin_class_init (GObjectClass *obj_cls, glong offset)
{
  GabbleTextMixinClass *mixin_cls;

  g_assert (G_IS_OBJECT_CLASS (obj_cls));

  g_type_set_qdata (G_OBJECT_CLASS_TYPE (obj_cls),
      GABBLE_TEXT_MIXIN_CLASS_OFFSET_QUARK,
      GINT_TO_POINTER (offset));

  mixin_cls = GABBLE_TEXT_MIXIN_CLASS (obj_cls);

  mixin_cls->received_signal_id = g_signal_new ("received",
                G_OBJECT_CLASS_TYPE (obj_cls),
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                0,
                NULL, NULL,
                text_mixin_marshal_VOID__INT_INT_INT_INT_STRING,
                G_TYPE_NONE, 5, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  mixin_cls->sent_signal_id = g_signal_new ("sent",
                G_OBJECT_CLASS_TYPE (obj_cls),
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                0,
                NULL, NULL,
                text_mixin_marshal_VOID__INT_INT_STRING,
                G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  /*
  mixin_cls->lost_message_signal_id = g_signal_new ("lost-message",
                G_OBJECT_CLASS_TYPE (obj_cls),
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                0,
                NULL, NULL,
                text_mixin_marshal_VOID__INT_INT_INT_INT_STRING,
                G_TYPE_NONE, 5, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING); */

}

void gabble_text_mixin_init (GObject *obj,
                             glong offset,
                             GabbleHandleRepo *handle_repo)
{
  GabbleTextMixin *mixin;

  g_assert (G_IS_OBJECT (obj));

  g_type_set_qdata (G_OBJECT_TYPE (obj),
                    GABBLE_TEXT_MIXIN_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin = GABBLE_TEXT_MIXIN (obj);

  mixin->pending = g_queue_new ();
  mixin->handle_repo = handle_repo;
  mixin->recv_id = 0;

}

static void _gabble_pending_free (GabblePendingMessage *msg);
static GabbleAllocator * _gabble_pending_get_alloc ();

void
gabble_text_mixin_finalize (GObject *obj)
{
  GabbleTextMixin *mixin = GABBLE_TEXT_MIXIN (obj);
  GabblePendingMessage *msg;

  /* free any data held directly by the object here */

  while ((msg = g_queue_pop_head(mixin->pending)))
    {
      gabble_handle_unref (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT, msg->sender);
      _gabble_pending_free (msg);
    }

  /* FIXME - this allocates & destroys if there haven't been any messages */
  gabble_allocator_destroy (_gabble_pending_get_alloc ());
  g_queue_free (mixin->pending);
}

/**
 * _gabble_pending_get_alloc
 *
 * Returns a GabbleAllocator for creating up to 256 pending messages, but no
 * more.
 */
static GabbleAllocator *
_gabble_pending_get_alloc ()
{
  static GabbleAllocator *alloc = NULL;

  if (alloc == NULL)
    alloc = gabble_allocator_new (sizeof(GabblePendingMessage), MAX_PENDING_MESSAGES);

  return alloc;
}

#define _gabble_pending_new() \
  (ga_new (_gabble_pending_get_alloc (), GabblePendingMessage))
#define _gabble_pending_new0() \
  (ga_new0 (_gabble_pending_get_alloc (), GabblePendingMessage))

/**
 * _gabble_pending_free
 *
 * Free up a GabblePendingMessage struct.
 */
static void _gabble_pending_free (GabblePendingMessage *msg)
{
  g_free (msg->text);
  gabble_allocator_free (_gabble_pending_get_alloc (), msg);
}

/**
 * _gabble_text_mixin_receive
 *
 */
gboolean gabble_text_mixin_receive (GObject *obj,
                                     TpChannelTextMessageType type,
                                     GabbleHandle sender,
                                     time_t timestamp,
                                     const char *text)
{
  GabbleTextMixin *mixin = GABBLE_TEXT_MIXIN (obj);
  GabbleTextMixinClass *mixin_cls = GABBLE_TEXT_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));

  GabblePendingMessage *msg;
  gsize len;

  msg = _gabble_pending_new0 ();

  if (msg == NULL)
    {
      g_debug ("%s: no more pending messages available, giving up", G_STRFUNC);

      /* TODO - g_signal_emit (obj, mixin_cls->lost_message_signal_id, 0); */

      return FALSE;
    }

  len = strlen (text);

  if (len > MAX_MESSAGE_SIZE)
    {
      g_debug ("%s: message exceeds maximum size, truncating", G_STRFUNC);

      /* TODO: add CHANNEL_TEXT_MESSAGE_FLAG_TRUNCATED flag*/

      len = MAX_MESSAGE_SIZE;
    }

  msg->text = g_try_malloc (len + 1);

  if (msg->text == NULL)
    {
      g_debug ("%s: unable to allocate message, giving up", G_STRFUNC);

      /* TODO - g_signal_emit (obj, mixin_cls->lost_message_signal_id, 0); */

      _gabble_pending_free (msg);

      /* TODO: send LostMessage() signal */

      return FALSE;
    }

  /* TODO: UTF-8 truncation */
  g_strlcpy (msg->text, text, len + 1);

  msg->id = mixin->recv_id++;
  msg->timestamp = timestamp;
  msg->sender = sender;
  msg->type = type;

  gabble_handle_ref (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT, msg->sender);
  g_queue_push_tail (mixin->pending, msg);

  g_signal_emit (obj, mixin_cls->received_signal_id, 0,
                 msg->id,
                 msg->timestamp,
                 msg->sender,
                 msg->type,
                 msg->text);

  g_debug ("%s: queued message %u", G_STRFUNC, msg->id);

  return TRUE;
}

static gint
compare_pending_message (gconstpointer haystack,
                         gconstpointer needle)
{
  GabblePendingMessage *msg = (GabblePendingMessage *) haystack;
  guint id = GPOINTER_TO_INT (needle);

  return (msg->id != id);
}

/**
 * gabble_text_mixin_acknowledge_pending_message
 *
 * Implements DBus method AcknowledgePendingMessage
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_text_mixin_acknowledge_pending_message (GObject *obj, guint id, GError **error)
{
  GabbleTextMixin *mixin = GABBLE_TEXT_MIXIN (obj);
  GList *node;
  GabblePendingMessage *msg;

  node = g_queue_find_custom (mixin->pending,
                              GINT_TO_POINTER (id),
                              compare_pending_message);

  if (node == NULL)
    {
      g_debug ("%s: invalid message id %u", G_STRFUNC, id);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "invalid message id %u", id);

      return FALSE;
    }

  msg = (GabblePendingMessage *) node->data;

  g_debug ("%s: acknowleding message id %u", G_STRFUNC, id);

  g_queue_remove (mixin->pending, msg);

  gabble_handle_unref (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT, msg->sender);
  _gabble_pending_free (msg);

  return TRUE;
}

/**
 * gabble_text_mixin_list_pending_messages
 *
 * Implements DBus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_text_mixin_list_pending_messages (GObject *obj, GPtrArray ** ret, GError **error)
{
  GabbleTextMixin *mixin = GABBLE_TEXT_MIXIN (obj);
  guint count;
  GPtrArray *messages;
  GList *cur;

  count = g_queue_get_length (mixin->pending);
  messages = g_ptr_array_sized_new (count);

  for (cur = g_queue_peek_head_link(mixin->pending);
       cur != NULL;
       cur = cur->next)
    {
      GabblePendingMessage *msg = (GabblePendingMessage *) cur->data;
      GValueArray *vals;

      vals = g_value_array_new (5);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 0), msg->id);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 1), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 1), msg->timestamp);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 2), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 2), msg->sender);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 3), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 3), msg->type);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 4), G_TYPE_STRING);
      g_value_set_string (g_value_array_get_nth (vals, 4), msg->text);

      g_ptr_array_add (messages, vals);
    }

  *ret = messages;

  return TRUE;
}

/**
 * gabble_text_mixin_send
 *
 * Implements DBus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_text_mixin_send (GObject *obj, guint type, guint subtype, const char * recipient, const gchar * text, GabbleConnection *conn, GError **error)
{
  GabbleTextMixinClass *mixin_cls = GABBLE_TEXT_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  LmMessage *msg;
  gboolean result;
  time_t timestamp;

  if (type > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      g_debug ("%s: invalid message type %u", G_STRFUNC, type);

      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
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

  msg = lm_message_new_with_sub_type (recipient, LM_MESSAGE_TYPE_MESSAGE, subtype);

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

  result = _gabble_connection_send (conn, msg, error);
  lm_message_unref (msg);

  if (!result)
    return FALSE;

  timestamp = time (NULL);

  g_signal_emit (obj, mixin_cls->sent_signal_id, 0,
                 timestamp,
                 type,
                 text);

  return TRUE;
}



void
gabble_text_mixin_clear (GObject *obj)
{
  GabbleTextMixin *mixin = GABBLE_TEXT_MIXIN (obj);
  GabblePendingMessage *msg;

  while ((msg = g_queue_pop_head(mixin->pending)))
    {
      gabble_handle_unref (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT, msg->sender);
      _gabble_pending_free (msg);
    }
}

gboolean
gabble_text_mixin_parse_incoming_message (LmMessage *message,
                        const gchar **from,
                        time_t *stamp,
                        TpChannelTextMessageType *msgtype,
                        const gchar **body,
                        const gchar **body_offset)
{
  const gchar *type;
  LmMessageNode *node;

  *from = lm_message_node_get_attribute (message->node, "from");
  if (*from == NULL)
    {
      HANDLER_DEBUG (message->node, "got a message without a from field");
      return FALSE;
    }

  type = lm_message_node_get_attribute (message->node, "type");

  /*
   * Parse timestamp of delayed messages.
   */
  *stamp = 0;

  for (node = message->node->children; node; node = node->next)
    {
      if (strcmp (node->name, "x") == 0)
        {
          const gchar *stamp_str, *p;
          struct tm stamp_tm = { 0, };

          if (!_lm_message_node_has_namespace (node, "jabber:x:delay"))
            continue;

          stamp_str = lm_message_node_get_attribute (node, "stamp");
          if (stamp_str == NULL)
            continue;

          p = strptime (stamp_str, "%Y%m%dT%T", &stamp_tm);
          if (p == NULL || *p != '\0')
            {
              g_warning ("%s: malformed date string '%s' for jabber:x:delay",
                         G_STRFUNC, stamp_str);
              continue;
            }

          *stamp = timegm (&stamp_tm);
        }
    }

  if (*stamp == 0)
    *stamp = time (NULL);


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

