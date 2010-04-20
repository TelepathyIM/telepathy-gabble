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
    WockyStanza *stanza,
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

typedef struct
{
  LmMessageHandler *handler;
  LmMessageType type;
  LmHandlerPriority priority;
} delayed_handler;

void
lm_connection_register_message_handler (LmConnection *connection,
    LmMessageHandler *handler,
    LmMessageType type,
    LmHandlerPriority priority)
{
  if (connection->porter == NULL)
    {
      /* Loudmouth lets you register handlers before the connection is
       * connected. We can't do currently do that with Wocky so we store the
       * handler and will register it once lm_connection_set_porter is called.*/
      delayed_handler *delayed;

      delayed = g_slice_new (delayed_handler);
      delayed->handler = handler;
      delayed->type = type;
      delayed->priority = priority;

      connection->delayed_handlers = g_slist_prepend (
          connection->delayed_handlers, delayed);
      return;
    }

  /* Genuine Loudmouth lets you register the same handler once per message
   * type, but this compatibility shim only lets you register each
   * LmMessageHandler once. */
  g_assert (handler->handler_id == 0);
  g_assert (handler->connection == NULL);

  handler->connection = connection;

  handler->handler_id = wocky_porter_register_handler (connection->porter,
      type, WOCKY_STANZA_SUB_TYPE_NONE, NULL, priority, stanza_cb,
      handler, NULL);
}

void
lm_connection_unregister_message_handler (LmConnection *connection,
    LmMessageHandler *handler,
    LmMessageType type)
{
  if (handler->handler_id == 0)
    return;

  g_assert (handler->connection != NULL);

  wocky_porter_unregister_handler (handler->connection->porter,
      handler->handler_id);

  handler->handler_id = 0;
  handler->connection = NULL;
}

void
lm_connection_unref (LmConnection *connection)
{
  GSList *l;

  for (l = connection->delayed_handlers; l != NULL; l = g_slist_next (l))
    {
      delayed_handler *delayed = l->data;

      g_slice_free (delayed_handler, delayed);
    }

  g_slist_free (connection->delayed_handlers);
  connection->delayed_handlers = NULL;

  g_cancellable_cancel (connection->iq_reply_cancellable);
  g_object_unref (connection->iq_reply_cancellable);
  connection->iq_reply_cancellable = NULL;

  if (connection->porter != NULL)
    {
      g_object_unref (connection->porter);
      connection->porter = NULL;
    }

  g_free (connection);
}

gboolean
lm_connection_send (LmConnection *connection,
    LmMessage *message,
    GError **error)
{
  g_assert (connection != NULL);
  g_assert (connection->porter != NULL);

  wocky_porter_send (connection->porter, message);
  return TRUE;
}

static void
iq_reply_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  LmMessageHandler *handler = (LmMessageHandler *) user_data;
  WockyStanza *reply;
  GError *error = NULL;

  reply = wocky_porter_send_iq_finish (WOCKY_PORTER (source), res, &error);
  if (reply == NULL)
    {
      g_debug ("send_iq_async failed: %s", error->message);
      g_error_free (error);
      goto out;
    }

  handler->function (handler, handler->connection, reply,
      handler->user_data);

  g_object_unref (reply);

out:
  lm_message_handler_unref (handler);
}

gboolean
lm_connection_send_with_reply (LmConnection *connection,
    LmMessage *message,
    LmMessageHandler *handler,
    GError **error)
{
  g_assert (connection != NULL);
  g_assert (connection->porter != NULL);

  handler->connection = connection;
  lm_message_handler_ref (handler);

  wocky_porter_send_iq_async (connection->porter, message,
      connection->iq_reply_cancellable, iq_reply_cb, handler);

  return TRUE;
}

LmConnection *
lm_connection_new (void)
{
  LmConnection *connection;

  connection = g_malloc (sizeof (LmConnection));
  connection->porter = NULL;
  connection->delayed_handlers = NULL;
  connection->iq_reply_cancellable = g_cancellable_new ();

  return connection;
}

void
lm_connection_set_porter (LmConnection *connection,
    WockyPorter *porter)
{
  GSList *l;

  g_assert (connection != NULL);
  g_assert (connection->porter == NULL);
  connection->porter = g_object_ref (porter);

  /* Now that we have a porter we can register the delayed handlers */
  for (l = connection->delayed_handlers; l != NULL; l = g_slist_next (l))
    {
      delayed_handler *delayed = l->data;

      lm_connection_register_message_handler (connection, delayed->handler,
          delayed->type, delayed->priority);

      g_slice_free (delayed_handler, delayed);
    }

  g_slist_free (connection->delayed_handlers);
  connection->delayed_handlers = NULL;
}
