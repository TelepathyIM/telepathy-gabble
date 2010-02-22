/*
 * lm-message-handler.c - Loudmouth-Wocky compatibility layer
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

#include "lm-message-handler.h"

LmMessageHandler *
lm_message_handler_new (LmHandleMessageFunction function,
    gpointer user_data,
    GDestroyNotify notify)
{
  LmMessageHandler *handler = g_slice_new0 (LmMessageHandler);
  handler->function = function;
  handler->user_data = user_data;
  handler->notify = notify;
  handler->ref_count = 1;

  return handler;
}

void
lm_message_handler_unref (LmMessageHandler *handler)
{
  handler->ref_count--;

  if (handler->ref_count == 0)
    {
      if (handler->notify != NULL)
        handler->notify (handler->user_data);
      g_slice_free (LmMessageHandler, handler);
    }
}

LmMessageHandler *
lm_message_handler_ref (LmMessageHandler *handler)
{
  handler->ref_count++;

  return handler;
}
