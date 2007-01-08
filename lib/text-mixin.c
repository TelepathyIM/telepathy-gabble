/*
 * text-mixin.c - Source for TpTextMixin
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

#include <dbus/dbus-glib.h>
#include <string.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>

#define DEBUG_FLAG TP_DEBUG_IM

#include "debug.h"

#include "telepathy-glib/text-mixin.h"
#include "_gen/signals-marshal.h"

#define TP_TYPE_PENDING_MESSAGE_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_INVALID))

/* allocator */

typedef struct
{
  gulong size;
  guint limit;
  guint count;
} _Allocator;

#define _new0(alloc, type) \
    ((type *) _allocator_alloc0 (alloc))

static void
_allocator_init (_Allocator *alloc, gulong size, guint limit)
{
  g_assert (alloc != NULL);
  g_assert (size > 0);
  g_assert (limit > 0);

  alloc->size = size;
  alloc->limit = limit;
}

static gpointer _allocator_alloc0 (_Allocator *alloc)
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

static void _allocator_free (_Allocator *alloc, gpointer thing)
{
  g_assert (alloc != NULL);
  g_assert (thing != NULL);

  g_free (thing);
  alloc->count--;
}

/* pending message */
#define MAX_PENDING_MESSAGES 256
#define MAX_MESSAGE_SIZE 1024 - 1

typedef struct
{
  guint id;
  time_t timestamp;
  TpHandle sender;
  TpChannelTextMessageType type;
  char *text;
  guint flags;
} _PendingMessage;

/**
 * tp_text_mixin_class_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObjectClass
 */
GQuark
tp_text_mixin_class_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("TpTextMixinClassOffsetQuark");
  return offset_quark;
}

/**
 * tp_text_mixin_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObject
 */
GQuark
tp_text_mixin_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("TpTextMixinOffsetQuark");
  return offset_quark;
}


/* TpTextMixin */
void
tp_text_mixin_class_init (GObjectClass *obj_cls, glong offset)
{
  TpTextMixinClass *mixin_cls;

  g_assert (G_IS_OBJECT_CLASS (obj_cls));

  g_type_set_qdata (G_OBJECT_CLASS_TYPE (obj_cls),
      TP_TEXT_MIXIN_CLASS_OFFSET_QUARK,
      GINT_TO_POINTER (offset));

  mixin_cls = TP_TEXT_MIXIN_CLASS (obj_cls);

  mixin_cls->lost_message_signal_id = g_signal_new ("lost-message",
                G_OBJECT_CLASS_TYPE (obj_cls),
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                0,
                NULL, NULL,
                g_cclosure_marshal_VOID__VOID,
                G_TYPE_NONE, 0);

  mixin_cls->received_signal_id = g_signal_new ("received",
                G_OBJECT_CLASS_TYPE (obj_cls),
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                0,
                NULL, NULL,
                _tp_marshal_VOID__UINT_UINT_UINT_UINT_UINT_STRING,
                G_TYPE_NONE, 6, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  mixin_cls->send_error_signal_id = g_signal_new ("send-error",
                G_OBJECT_CLASS_TYPE (obj_cls),
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                0,
                NULL, NULL,
                _tp_marshal_VOID__UINT_UINT_UINT_STRING,
                G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  mixin_cls->sent_signal_id = g_signal_new ("sent",
                G_OBJECT_CLASS_TYPE (obj_cls),
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                0,
                NULL, NULL,
                _tp_marshal_VOID__UINT_UINT_STRING,
                G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);
}

void
tp_text_mixin_init (GObject *obj,
                    glong offset,
                    TpHandleRepoIface *contacts_repo,
                    gboolean send_nick)
{
  TpTextMixin *mixin;

  g_assert (G_IS_OBJECT (obj));

  g_type_set_qdata (G_OBJECT_TYPE (obj),
                    TP_TEXT_MIXIN_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin = TP_TEXT_MIXIN (obj);

  mixin->pending = g_queue_new ();
  mixin->contacts_repo = contacts_repo;
  mixin->recv_id = 0;
  mixin->msg_types = g_array_sized_new (FALSE, FALSE, sizeof (guint), 4);

  mixin->message_lost = FALSE;
}

void
tp_text_mixin_set_message_types (GObject *obj,
                                 ...)
{
  TpTextMixin *mixin = TP_TEXT_MIXIN (obj);
  va_list args;
  guint type;

  va_start (args, obj);

  while ((type = va_arg (args, guint)) != G_MAXUINT)
    g_array_append_val (mixin->msg_types, type);

  va_end (args);
}

static void _pending_free (_PendingMessage *msg);
static _Allocator * _pending_get_alloc ();

void
tp_text_mixin_finalize (GObject *obj)
{
  TpTextMixin *mixin = TP_TEXT_MIXIN (obj);
  _PendingMessage *msg;

  /* free any data held directly by the object here */

  while ((msg = g_queue_pop_head(mixin->pending)))
    {
      tp_handle_unref (mixin->contacts_repo, msg->sender);
      _pending_free (msg);
    }

  g_queue_free (mixin->pending);

  g_array_free (mixin->msg_types, TRUE);
}

/**
 * _pending_get_alloc
 *
 * Returns an Allocator for creating up to 256 pending messages, but no
 * more.
 */
static _Allocator *
_pending_get_alloc ()
{
  static _Allocator alloc = { 0, };

  if (0 == alloc.size)
    _allocator_init (&alloc, sizeof(_PendingMessage), MAX_PENDING_MESSAGES);

  return &alloc;
}

#define _pending_new0() \
  (_new0 (_pending_get_alloc (), _PendingMessage))

/**
 * _pending_free
 *
 * Free up a _PendingMessage struct.
 */
static void _pending_free (_PendingMessage *msg)
{
  g_free (msg->text);
  _allocator_free (_pending_get_alloc (), msg);
}

/**
 * _gabble_text_mixin_receive
 *
 */
gboolean
tp_text_mixin_receive (GObject *obj,
                       TpChannelTextMessageType type,
                       TpHandle sender,
                       time_t timestamp,
                       const char *text)
{
  TpTextMixin *mixin = TP_TEXT_MIXIN (obj);
  TpTextMixinClass *mixin_cls = TP_TEXT_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));

  gchar *end;
  _PendingMessage *msg;
  gsize len;

  msg = _pending_new0 ();

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

      msg->flags |= TP_CHANNEL_TEXT_MESSAGE_FLAG_TRUNCATED;

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

      _pending_free (msg);

      return FALSE;
    }

  g_strlcpy (msg->text, text, len + 1);

  msg->id = mixin->recv_id++;
  msg->timestamp = timestamp;
  msg->sender = sender;
  msg->type = type;

  tp_handle_ref (mixin->contacts_repo, msg->sender);
  g_queue_push_tail (mixin->pending, msg);

  g_signal_emit (obj, mixin_cls->received_signal_id, 0,
                 msg->id,
                 msg->timestamp,
                 msg->sender,
                 msg->type,
                 msg->flags,
                 msg->text);

  DEBUG ("queued message %u", msg->id);

  mixin->message_lost = FALSE;

  return TRUE;
}

static gint
compare_pending_message (gconstpointer haystack,
                         gconstpointer needle)
{
  _PendingMessage *msg = (_PendingMessage *) haystack;
  guint id = GPOINTER_TO_INT (needle);

  return (msg->id != id);
}

/**
 * tp_text_mixin_acknowledge_pending_messages
 *
 * Implements D-Bus method AcknowledgePendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
tp_text_mixin_acknowledge_pending_messages (GObject *obj, const GArray * ids, GError **error)
{
  TpTextMixin *mixin = TP_TEXT_MIXIN (obj);
  GList **nodes;
  _PendingMessage *msg;
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

          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "invalid message id %u", id);

          g_free(nodes);
          return FALSE;
        }
    }

  for (i = 0; i < ids->len; i++)
    {
      msg = (_PendingMessage *) nodes[i]->data;

      DEBUG ("acknowleding message id %u", msg->id);

      g_queue_remove (mixin->pending, msg);

      tp_handle_unref (mixin->contacts_repo, msg->sender);
      _pending_free (msg);
    }

  g_free(nodes);
  return TRUE;
}

/**
 * tp_text_mixin_list_pending_messages
 *
 * Implements D-Bus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
tp_text_mixin_list_pending_messages (GObject *obj, gboolean clear, GPtrArray ** ret, GError **error)
{
  TpTextMixin *mixin = TP_TEXT_MIXIN (obj);
  guint count;
  GPtrArray *messages;
  GList *cur;

  count = g_queue_get_length (mixin->pending);
  messages = g_ptr_array_sized_new (count);

  for (cur = (clear ? g_queue_pop_head_link(mixin->pending)
                    : g_queue_peek_head_link(mixin->pending));
       cur != NULL;
       cur = (clear ? g_queue_pop_head_link(mixin->pending)
                    : cur->next))
    {
      _PendingMessage *msg = (_PendingMessage *) cur->data;
      GValue val = { 0, };

      g_value_init (&val, TP_TYPE_PENDING_MESSAGE_STRUCT);
      g_value_take_boxed (&val,
          dbus_g_type_specialized_construct (TP_TYPE_PENDING_MESSAGE_STRUCT));
      dbus_g_type_struct_set (&val,
          0, msg->id,
          1, msg->timestamp,
          2, msg->sender,
          3, msg->type,
          4, msg->flags,
          5, msg->text,
          G_MAXUINT);

      g_ptr_array_add (messages, g_value_get_boxed (&val));
    }

  *ret = messages;

  return TRUE;
}

void
tp_text_mixin_emit_sent (GObject *obj,
                         time_t timestamp,
                         guint type,
                         const char *text)
{
  TpTextMixinClass *mixin_cls = TP_TEXT_MIXIN_CLASS (G_OBJECT_GET_CLASS
      (obj));

  g_signal_emit (obj, mixin_cls->sent_signal_id, 0,
                 timestamp,
                 type,
                 text);
}

gboolean
tp_text_mixin_get_message_types (GObject *obj, GArray **ret, GError **error)
{
  TpTextMixin *mixin = TP_TEXT_MIXIN (obj);
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
tp_text_mixin_clear (GObject *obj)
{
  TpTextMixin *mixin = TP_TEXT_MIXIN (obj);
  _PendingMessage *msg;

  while ((msg = g_queue_pop_head(mixin->pending)))
    {
      tp_handle_unref (mixin->contacts_repo, msg->sender);
      _pending_free (msg);
    }
}

void
_tp_text_mixin_send_error_signal (GObject *obj,
                                  TpChannelTextSendError error,
                                  time_t timestamp,
                                  TpChannelTextMessageType type,
                                  const gchar *text)
{
  TpTextMixinClass *mixin_cls = TP_TEXT_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));

  g_signal_emit (obj, mixin_cls->send_error_signal_id, 0, error, timestamp, type, text, 0);
}
