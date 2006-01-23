/*
 * telepathy-constants.h - constants used in telepathy 
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __TELEPATHY_CONSTANTS_H__
#define __TELEPATHY_CONSTANTS_H__ 

#include <glib.h>
G_BEGIN_DECLS

typedef enum {
TP_CONN_ALIAS_FLAG_USER_SET = 1
} TpConnAliasFlags;

typedef enum {
TP_CONN_CAPABILITY_TYPE_CREATE = 0,
TP_CONN_CAPABILITY_TYPE_INVITE = 1
} TpConnectionCapabilityType;

typedef enum {
TP_HANDLE_TYPE_NONE = 0,
TP_HANDLE_TYPE_CONTACT = 1,
TP_HANDLE_TYPE_ROOM = 2,
TP_HANDLE_TYPE_LIST = 3
} TpHandleType;

typedef enum {
TP_CONN_PRESENCE_TYPE_OFFLINE = 0,
TP_CONN_PRESENCE_TYPE_AVAILABLE = 1,
TP_CONN_PRESENCE_TYPE_AWAY = 2,
TP_CONN_PRESENCE_TYPE_EXTENDED_AWAY = 3,
TP_CONN_PRESENCE_TYPE_HIDDEN = 4
} TpConnectionPresenceType;

typedef enum {
TP_CONN_STATUS_CONNECTED = 0,
TP_CONN_STATUS_CONNECTING = 1,
TP_CONN_STATUS_DISCONNECTED = 2
} TpConnectionStatus;

typedef enum {
TP_CONN_STATUS_REASON_NONE_SPECIFIED = 0,
TP_CONN_STATUS_REASON_REQUESTED = 1,
TP_CONN_STATUS_REASON_NETWORK_ERROR = 2,
TP_CONN_STATUS_REASON_AUTHENTICATION_FAILED = 3,
TP_CONN_STATUS_REASON_ENCRYPTION_ERROR = 4
} TpConnectionStatusReason;

typedef enum {
TP_CHANNEL_CONTACT_SEARCH_STATE_BEFORE = 0,
TP_CHANNEL_CONTACT_SEARCH_STATE_DURING = 1,
TP_CHANNEL_CONTACT_SEARCH_STATE_AFTER = 2
} TpChannelContactSearchState;

typedef enum {
TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL = 0,
TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION = 1,
TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE = 2
} TpChannelTextMessageType;

typedef enum {
TP_CHANNEL_GROUP_FLAG_CAN_ADD = 1,
TP_CHANNEL_GROUP_FLAG_CAN_REMOVE = 2,
TP_CHANNEL_GROUP_FLAG_CAN_RESCIND = 4,
TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD = 8,
TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE = 16,
TP_CHANNEL_GROUP_FLAG_MESSAGE_ACCEPT = 32,
TP_CHANNEL_GROUP_FLAG_MESSAGE_REJECT = 64,
TP_CHANNEL_GROUP_FLAG_MESSAGE_RESCIND = 128
} TpChannelGroupFlags;

typedef enum {
TP_CHANNEL_HOLD_STATE_NONE = 0,
TP_CHANNEL_HOLD_STATE_SEND_ONLY = 1,
TP_CHANNEL_HOLD_STATE_RECV_ONLY = 2,
TP_CHANNEL_HOLD_STATE_BOTH = 3
} TpChannelHoldState;

typedef enum {
TP_CHANNEL_PASSWORD_FLAG_REQUIRED = 1,
TP_CHANNEL_PASSWORD_FLAG_VISIBLE = 2,
TP_CHANNEL_PASSWORD_FLAG_MODIFIABLE = 4,
TP_CHANNEL_PASSWORD_FLAG_PROVIDE = 8
} TpChannelPasswordFlags;

typedef enum {
TP_CHANNEL_SUBJECT_FLAG_PRESENT = 1,
TP_CHANNEL_SUBJECT_FLAG_MODIFIABLE = 2
} TpChannelSubjectFlags;

typedef enum {
TP_MEDIA_STREAM_TYPE_AUDIO = 0,
TP_MEDIA_STREAM_TYPE_VIDEO = 1
} TpMediaStreamType;

typedef enum {
TP_MEDIA_STREAM_DIRECTION_NONE = 0,
TP_MEDIA_STREAM_DIRECTION_SEND = 1,
TP_MEDIA_STREAM_DIRECTION_RECIEVE = 2,
TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL = 3
} TpMediaStreamDirection;

typedef enum {
TP_MEDIA_STREAM_ERROR_UNKNOWN = 0,
TP_MEDIA_STREAM_ERROR_EOS = 1
} TpMediaStreamError;

typedef enum {
TP_MEDIA_STREAM_PROTO_UDP = 0,
TP_MEDIA_STREAM_PROTO_TCP = 1
} TpMediaStreamProto;

typedef enum {
TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL = 0,
TP_MEDIA_STREAM_TRANSPORT_TYPE_DERIVED = 1,
TP_MEDIA_STREAM_TRANSPORT_TYPE_RELAY = 2
} TpMediaStreamTransportType;

G_END_DECLS


#endif
