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

#include <loudmouth/loudmouth.h>
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "telepathy-constants.h"
#include "telepathy-errors.h"

#define DEBUG_FLAG GABBLE_DEBUG_IM

#include "debug.h"
#include "gabble-connection.h"
#include "roster.h"
#include "util.h"

#include "text-mixin.h"
#include "text-mixin-signals-marshal.h"

#define TP_TYPE_PENDING_MESSAGE_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_INVALID))

/* allocator */

typedef struct _GabbleAllocator GabbleAllocator;
struct _GabbleAllocator
{
  gulong size;
  guint limit;
  guint count;
};

#define ga_new0(alloc, type) \
    ((type *) gabble_allocator_alloc0 (alloc))

static void
gabble_allocator_init (GabbleAllocator *alloc, gulong size, guint limit)
{
  g_assert (alloc != NULL);
  g_assert (size > 0);
  g_assert (limit > 0);

  alloc->size = size;
  alloc->limit = limit;
}

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

  mixin_cls->lost_message_signal_id = g_signal_new ("lost-message",
                G_OBJECT_CLASS_TYPE (obj_cls),
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                0,
                NULL, NULL,
                text_mixin_marshal_VOID__VOID,
                G_TYPE_NONE, 0);

  mixin_cls->received_signal_id = g_signal_new ("received",
                G_OBJECT_CLASS_TYPE (obj_cls),
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                0,
                NULL, NULL,
                text_mixin_marshal_VOID__INT_INT_INT_INT_INT_STRING,
                G_TYPE_NONE, 6, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  mixin_cls->send_error_signal_id = g_signal_new ("send-error",
                G_OBJECT_CLASS_TYPE (obj_cls),
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                0,
                NULL, NULL,
                text_mixin_marshal_VOID__INT_INT_INT_STRING,
                G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  mixin_cls->sent_signal_id = g_signal_new ("sent",
                G_OBJECT_CLASS_TYPE (obj_cls),
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                0,
                NULL, NULL,
                text_mixin_marshal_VOID__INT_INT_STRING,
                G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);
}

void
gabble_text_mixin_init (GObject *obj,
                        glong offset,
                        GabbleHandleRepo *handle_repo,
                        gboolean send_nick)
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
  mixin->msg_types = g_array_sized_new (FALSE, FALSE, sizeof (guint), 4);

  mixin->message_lost = FALSE;
}

void
gabble_text_mixin_set_message_types (GObject *obj,
                                     ...)
{
  GabbleTextMixin *mixin = GABBLE_TEXT_MIXIN (obj);
  va_list args;
  guint type;

  va_start (args, obj);

  while ((type = va_arg (args, guint)) != G_MAXUINT)
    g_array_append_val (mixin->msg_types, type);

  va_end (args);
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

  g_queue_free (mixin->pending);

  g_array_free (mixin->msg_types, TRUE);
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
  static GabbleAllocator alloc = { 0, };

  if (0 == alloc.size)
    gabble_allocator_init (&alloc, sizeof(GabblePendingMessage), MAX_PENDING_MESSAGES);

  return &alloc;
}

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

  gchar *end;
  GabblePendingMessage *msg;
  gsize len;

  msg = _gabble_pending_new0 ();

  if (msg == NULL)
    {
      DEBUG ("no more pending messages available, giving up");

      if (!mixin->message_lost)
        {
          g_signal_emit (obj, mixin_cls->lost_message_signal_id, 0);
          mixin->message_lost = TRUE;
        }

      return FALSE;
    }

  len = strlen (text);

  if (len > MAX_MESSAGE_SIZE)
    {
      DEBUG ("message exceeds maximum size, truncating");

      /* TODO: add CHANNEL_TEXT_MESSAGE_FLAG_TRUNCATED flag*/

      end = g_utf8_find_prev_char (text, text+MAX_MESSAGE_SIZE);
      if (end)
        len = end-text;
      else
        len = 0;
    }

  msg->text = g_try_malloc (len + 1);

  if (msg->text == NULL)
    {
      DEBUG ("unable to allocate message, giving up");

      if (!mixin->message_lost)
        {
          g_signal_emit (obj, mixin_cls->lost_message_signal_id, 0);
          mixin->message_lost = TRUE;
        }

      _gabble_pending_free (msg);

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
                 0, /* TODO: fill in flags properly */
                 msg->text);

  DEBUG ("queued message %u", msg->id);

  mixin->message_lost = FALSE;

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
 * gabble_text_mixin_acknowledge_pending_messages
 *
 * Implements DBus method AcknowledgePendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_text_mixin_acknowledge_pending_messages (GObject *obj, const GArray * ids, GError **error)
{
  GabbleTextMixin *mixin = GABBLE_TEXT_MIXIN (obj);
  GList **nodes;
  GabblePendingMessage *msg;
  guint i;

  nodes = g_new(GList *, ids->len);

  for (i = 0; i < ids->len; i++)
    {
      guint id = g_array_index(ids, guint, i);

      nodes[i] = g_queue_find_custom (mixin->pending,
                                      GINT_TO_POINTER (id),
                                      compare_pending_message);

      if (nodes[i] == NULL)
        {
          DEBUG ("invalid message id %u", id);

          *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                "invalid message id %u", id);

          return FALSE;
        }
    }

  for (i = 0; i < ids->len; i++)
    {
      guint id = g_array_index(ids, guint, i);

      msg = (GabblePendingMessage *) nodes[i]->data;

      DEBUG ("acknowleding message id %u", id);

      g_queue_remove (mixin->pending, msg);

      gabble_handle_unref (mixin->handle_repo, TP_HANDLE_TYPE_CONTACT, msg->sender);
      _gabble_pending_free (msg);
    }

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
      GValue val = { 0, };

      g_value_init (&val, TP_TYPE_PENDING_MESSAGE_STRUCT);
      g_value_take_boxed (&val,
          dbus_g_type_specialized_construct (TP_TYPE_PENDING_MESSAGE_STRUCT));
      dbus_g_type_struct_set (&val,
          0, msg->id,
          1, msg->timestamp,
          2, msg->sender,
          3, msg->type,
          4, msg->text,
          G_MAXUINT);

      g_ptr_array_add (messages, g_value_get_boxed (&val));
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
gboolean gabble_text_mixin_send (GObject *obj, guint type, guint subtype,
                                 const char *recipient, const gchar *text,
                                 GabbleConnection *conn, GError **error)
{
  GabbleTextMixin *mixin = GABBLE_TEXT_MIXIN (obj);
  GabbleTextMixinClass *mixin_cls = GABBLE_TEXT_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  LmMessage *msg;
  gboolean result;
  time_t timestamp;

  if (type > TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE)
    {
      DEBUG ("invalid message type %u", type);

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

gboolean
gabble_text_mixin_get_message_types (GObject *obj, GArray **ret, GError **error)
{
  GabbleTextMixin *mixin = GABBLE_TEXT_MIXIN (obj);
  guint i;

  *ret = g_array_sized_new (FALSE, FALSE, sizeof (guint),
                            mixin->msg_types->len);

  for (i = 0; i < mixin->msg_types->len; i++)
    {
      g_array_append_val (*ret, g_array_index (mixin->msg_types, guint, i));
    }

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
                        const gchar **body_offset,
                        GabbleTextMixinSendError *send_error)
{
  const gchar *type;
  LmMessageNode *node;

  *send_error = CHANNEL_TEXT_SEND_NO_ERROR;

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
                *send_error = CHANNEL_TEXT_SEND_ERROR_OFFLINE;
                break;

              case XMPP_ERROR_ITEM_NOT_FOUND:
              case XMPP_ERROR_JID_MALFORMED:
              case XMPP_ERROR_REMOTE_SERVER_TIMEOUT:
                *send_error = CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT;
                break;

              case XMPP_ERROR_FORBIDDEN:
                *send_error = CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED;
                break;

              case XMPP_ERROR_RESOURCE_CONSTRAINT:
                *send_error = CHANNEL_TEXT_SEND_ERROR_TOO_LONG;
                break;

              default:
                *send_error = CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
            }
        }
      else
        {
          *send_error = CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
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

void
_gabble_text_mixin_send_error_signal (GObject *obj,
                                      GabbleTextMixinSendError error,
                                      time_t timestamp,
                                      TpChannelTextMessageType type,
                                      const gchar *text)
{
  GabbleTextMixinClass *mixin_cls = GABBLE_TEXT_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));

  g_signal_emit (obj, mixin_cls->send_error_signal_id, 0, error, timestamp, type, text, 0);
}

