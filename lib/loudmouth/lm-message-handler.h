/*
 * lm-message-handler.h - Loudmouth-Wocky compatibility layer
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

#ifndef __LM_MESSAGE_HANDLER_H__
#define __LM_MESSAGE_HANDLER_H__

#include "lm-types.h"
#include "lm-connection.h"
#include "lm-message.h"

G_BEGIN_DECLS

typedef enum {
  LM_HANDLER_RESULT_REMOVE_MESSAGE,
  LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS
} LmHandlerResult;

typedef LmHandlerResult (*LmHandleMessageFunction) (LmMessageHandler *handler,
    LmConnection *connection,
    LmMessage *message,
    gpointer user_data);

struct _LmMessageHandler
{
  guint handler_id;
  LmConnection *connection;
  LmHandleMessageFunction function;
  gpointer user_data;
  GDestroyNotify notify;
  guint ref_count;
};

#define LM_HANDLER_PRIORITY_LAST   WOCKY_PORTER_HANDLER_PRIORITY_MIN
#define LM_HANDLER_PRIORITY_NORMAL WOCKY_PORTER_HANDLER_PRIORITY_NORMAL
#define LM_HANDLER_PRIORITY_FIRST  WOCKY_PORTER_HANDLER_PRIORITY_MAX

LmMessageHandler *  lm_message_handler_new (LmHandleMessageFunction function,
    gpointer user_data,
    GDestroyNotify notify);

void lm_message_handler_unref (LmMessageHandler *handler);

LmMessageHandler * lm_message_handler_ref (LmMessageHandler *handler);

G_END_DECLS

#endif /* #ifndef __LM_MESSAGE_HANDLER_H__ */
