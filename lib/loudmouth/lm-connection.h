/*
 * lm-connection.h - Loudmouth-Wocky compatibility layer
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

#ifndef __LM_CONNECTION_H__
#define __LM_CONNECTION_H__

#include <wocky/wocky-stanza.h>
#include <wocky/wocky-porter.h>

#include "lm-types.h"
#include "lm-message-handler.h"

G_BEGIN_DECLS

struct _LmConnection
{
  WockyPorter *porter;
  GSList *delayed_handlers;
  GCancellable *iq_reply_cancellable;
};

typedef guint LmHandlerPriority;

LmConnection * lm_connection_new (void);

void lm_connection_register_message_handler (LmConnection *connection,
    LmMessageHandler *handler,
    WockyStanzaType type,
    LmHandlerPriority priority);

void lm_connection_unregister_message_handler (LmConnection *connection,
    LmMessageHandler *handler,
    WockyStanzaType type);

void lm_connection_unref (LmConnection *connection);

/* Fake API. This is not part of loudmouth */

void lm_connection_set_porter (LmConnection *connection,
    WockyPorter *porter);

G_END_DECLS

#endif /* #ifndef __LM_CONNECTION_H__ */
