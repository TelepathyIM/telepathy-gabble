/*
 * lm-message.h - Loudmouth-Wocky compatibility layer
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

#ifndef __LM_MESSAGE_H__
#define __LM_MESSAGE_H__

#include <wocky/wocky-stanza.h>

#include "lm-message-node.h"

G_BEGIN_DECLS

typedef WockyStanza LmMessage;

typedef WockyStanzaType LmMessageType;

#define LM_MESSAGE_TYPE_MESSAGE         WOCKY_STANZA_TYPE_MESSAGE
#define LM_MESSAGE_TYPE_PRESENCE        WOCKY_STANZA_TYPE_PRESENCE
#define LM_MESSAGE_TYPE_IQ              WOCKY_STANZA_TYPE_IQ
#define LM_MESSAGE_TYPE_STREAM          WOCKY_STANZA_TYPE_STREAM
#define LM_MESSAGE_TYPE_STREAM_ERROR    WOCKY_STANZA_TYPE_STREAM_ERROR
#define LM_MESSAGE_TYPE_STREAM_FEATURES WOCKY_STANZA_TYPE_STREAM_FEATURES
#define LM_MESSAGE_TYPE_AUTH            WOCKY_STANZA_TYPE_AUTH,
#define LM_MESSAGE_TYPE_CHALLENGE       WOCKY_STANZA_TYPE_CHALLENGE
#define LM_MESSAGE_TYPE_RESPONSE        WOCKY_STANZA_TYPE_RESPONSE
#define LM_MESSAGE_TYPE_SUCCESS         WOCKY_STANZA_TYPE_SUCCESS
#define LM_MESSAGE_TYPE_FAILURE         WOCKY_STANZA_TYPE_FAILURE
/*
#define LM_MESSAGE_TYPE_PROCEED
#define LM_MESSAGE_TYPE_STARTTLS
*/
#define LM_MESSAGE_TYPE_UNKNOWN         WOCKY_STANZA_TYPE_UNKNOWN

typedef WockyStanzaSubType LmMessageSubType;

#define LM_MESSAGE_SUB_TYPE_NOT_SET      WOCKY_STANZA_SUB_TYPE_NONE
#define LM_MESSAGE_SUB_TYPE_AVAILABLE    WOCKY_STANZA_SUB_TYPE_AVAILABLE
#define LM_MESSAGE_SUB_TYPE_NORMAL       WOCKY_STANZA_SUB_TYPE_NORMAL
#define LM_MESSAGE_SUB_TYPE_CHAT         WOCKY_STANZA_SUB_TYPE_CHAT
#define LM_MESSAGE_SUB_TYPE_GROUPCHAT    WOCKY_STANZA_SUB_TYPE_GROUPCHAT
#define LM_MESSAGE_SUB_TYPE_HEADLINE     WOCKY_STANZA_SUB_TYPE_HEADLINE
#define LM_MESSAGE_SUB_TYPE_UNAVAILABLE  WOCKY_STANZA_SUB_TYPE_UNAVAILABLE
#define LM_MESSAGE_SUB_TYPE_PROBE        WOCKY_STANZA_SUB_TYPE_PROBE
#define LM_MESSAGE_SUB_TYPE_SUBSCRIBE    WOCKY_STANZA_SUB_TYPE_SUBSCRIBE
#define LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE  WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBE
#define LM_MESSAGE_SUB_TYPE_SUBSCRIBED   WOCKY_STANZA_SUB_TYPE_SUBSCRIBED
#define LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED WOCKY_STANZA_SUB_TYPE_UNSUBSCRIBED
#define LM_MESSAGE_SUB_TYPE_GET          WOCKY_STANZA_SUB_TYPE_GET
#define LM_MESSAGE_SUB_TYPE_SET          WOCKY_STANZA_SUB_TYPE_SET
#define LM_MESSAGE_SUB_TYPE_RESULT       WOCKY_STANZA_SUB_TYPE_RESULT
#define LM_MESSAGE_SUB_TYPE_ERROR        WOCKY_STANZA_SUB_TYPE_ERROR

LmMessage * lm_message_new (const gchar *to,
    LmMessageType type);

LmMessage * lm_message_new_with_sub_type (const gchar *to,
    LmMessageType type,
    LmMessageSubType sub_type);


LmMessage * lm_message_ref (LmMessage *message);
void lm_message_unref (LmMessage *message);

LmMessageType lm_message_get_type (LmMessage *message);
LmMessageSubType lm_message_get_sub_type (LmMessage *message);

LmMessageNode * lm_message_get_node (LmMessage *message);

G_END_DECLS

#endif /* #ifndef __LM_MESSAGE_H__ */
