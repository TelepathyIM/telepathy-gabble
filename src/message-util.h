/*
 * message-util.h - Header for Messages interface utility functions
 * Copyright (C) 2008 Collabora Ltd.
 * Copyright (C) 2008 Nokia Corporation
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

#ifndef __GABBLE_MESSAGE_UTIL_H__
#define __GABBLE_MESSAGE_UTIL_H__

#include <telepathy-glib/telepathy-glib.h>

#include <wocky/wocky.h>
#include <wocky/wocky.h>

#include "connection.h"

G_BEGIN_DECLS

void gabble_message_util_add_chat_state (WockyStanza *stanza,
    TpChannelChatState state);

WockyStanza * gabble_message_util_build_stanza (TpMessage *message,
    GabbleConnection *conn, WockyStanzaSubType subtype,
    TpChannelChatState state, const char *recipient, gboolean send_nick,
    gchar **token, GError **error);

gboolean gabble_message_util_send_chat_state (GObject *obj,
    GabbleConnection *conn, WockyStanzaSubType subtype, TpChannelChatState state,
    const char *recipient, GError **error);


#define GABBLE_TEXT_CHANNEL_SEND_NO_ERROR ((TpChannelTextSendError)-1)

gboolean gabble_message_util_parse_incoming_message (WockyStanza *message,
    const gchar **from, const gchar **to, time_t *stamp,
    TpChannelTextMessageType *msgtype, const gchar **id,
    const gchar **body_ret, gint *state, TpChannelTextSendError *send_error,
    TpDeliveryStatus *delivery_status, gboolean *sent);

TpChannelTextSendError
gabble_tp_send_error_from_wocky_xmpp_error (WockyXmppError err);

G_END_DECLS

#endif /* #ifndef __GABBLE_MESSAGE_UTIL_H__ */
