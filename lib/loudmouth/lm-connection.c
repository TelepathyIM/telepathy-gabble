/*
 * lm-connection.c - Loudmouth-Wocky compatibility layer
 * Copyright (C) 2009 Collabora Ltd.
 * @author Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
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

#include "lm-connection.h"
#include "lm-message-handler.h"

static gboolean
stanza_cb (WockyPorter *self,
    WockyXmppStanza *stanza,
    gpointer user_data)
{
  LmMessageHandler *handler = (LmMessageHandler *) user_data;
  LmHandlerResult result;

  result = handler->function (handler, handler->connection, stanza,
      handler->user_data);

  if (result == LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS)
    return FALSE;
  else
    return TRUE;
}

GSList *delayed_handlers = NULL;

typedef struct
{
  LmMessageHandler *handler;
  LmMessageType type;
  LmHandlerPriority priority;
} delayed_handler;

static gint
find_handler (gconstpointer a,
    gconstpointer b)
{
  delayed_handler *delayed = (delayed_handler *) a;
  LmMessageHandler *handler = (LmMessageHandler *) b;

  if (delayed->handler == handler)
    return 0;

  return 1;
}

void
lm_connection_register_message_handler (LmConnection *connection,
    LmMessageHandler *handler,
    LmMessageType type,
    LmHandlerPriority priority)
{
  if (connection == NULL)
    {
      /* Loudmouth allows to register handler before the connection is
       * connected. We can't do currently do that with Wocky so we store the
       * handler and will register it once
       * lm_connection_register_previous_handler is called. */
      GSList *found;
      delayed_handler *delayed;

      found = g_slist_find_custom (delayed_handlers, handler, find_handler);
      if (found != NULL)
        return;

      delayed = g_slice_new (delayed_handler);
      delayed->handler = handler;
      delayed->type = type;
      delayed->priority = priority;

      delayed_handlers = g_slist_prepend (delayed_handlers, delayed);
      return;
    }

  g_assert (handler->handler_id == 0);
  g_assert (handler->connection == NULL);

  handler->connection = connection;

  handler->handler_id = wocky_porter_register_handler (connection,
      type, WOCKY_STANZA_SUB_TYPE_NONE, NULL, priority, stanza_cb,
      handler, WOCKY_STANZA_END);
}

void
lm_connection_unregister_message_handler (LmConnection *connection,
    LmMessageHandler *handler,
    LmMessageType type)
{
  if (handler->handler_id == 0)
    return;

  g_assert (handler->connection != NULL);

  wocky_porter_unregister_handler (handler->connection, handler->handler_id);

  handler->handler_id = 0;
  handler->connection = NULL;
}

LmConnection *
lm_connection_ref (LmConnection *connection)
{
  return g_object_ref (connection);
}

void
lm_connection_unref (LmConnection *connection)
{
  g_object_unref (connection);
}

gboolean
lm_connection_send (LmConnection *connection,
    LmMessage *message,
    GError **error)
{
  wocky_porter_send (connection, message);
  return TRUE;
}

static void
iq_reply_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  LmMessageHandler *handler = (LmMessageHandler *) user_data;
  WockyXmppStanza *reply;
  GError *error = NULL;

  reply = wocky_porter_send_iq_finish (WOCKY_PORTER (source), res, &error);
  if (reply == NULL)
    {
      g_debug ("send_iq_async failed: %s", error->message);
      g_error_free (error);
      return;
    }

  handler->function (handler, handler->connection, reply,
      handler->user_data);

  g_object_unref (reply);
  lm_message_handler_unref (handler);
}

GCancellable *iq_reply_cancellable = NULL;

gboolean
lm_connection_send_with_reply (LmConnection *connection,
    LmMessage *message,
    LmMessageHandler *handler,
    GError **error)
{
  handler->connection = connection;
  lm_message_handler_ref (handler);

  if (iq_reply_cancellable == NULL)
    iq_reply_cancellable = g_cancellable_new ();

  wocky_porter_send_iq_async (connection, message, iq_reply_cancellable,
      iq_reply_cb, handler);

  return TRUE;
}

void
lm_connection_register_previous_handler (LmConnection *connection)
{
  GSList *l;

  g_assert (connection != NULL);

  for (l = delayed_handlers; l != NULL; l = g_slist_next (l))
    {
      delayed_handler *delayed = l->data;

      lm_connection_register_message_handler (connection, delayed->handler,
          delayed->type, delayed->priority);

      g_slice_free (delayed_handler, delayed);
    }

  g_slist_free (delayed_handlers);
  delayed_handlers = NULL;
}

void
lm_connection_shutdown (LmConnection *connection)
{
  GSList *l;

  for (l = delayed_handlers; l != NULL; l = g_slist_next (l))
    {
      delayed_handler *delayed = l->data;

      g_slice_free (delayed_handler, delayed);
    }

  g_slist_free (delayed_handlers);
  delayed_handlers = NULL;

  if (iq_reply_cancellable == NULL)
    return;

  g_cancellable_cancel (iq_reply_cancellable);
  g_object_unref (iq_reply_cancellable);
  iq_reply_cancellable = NULL;
}
