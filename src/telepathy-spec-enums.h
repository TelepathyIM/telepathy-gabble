/* Generated from the Telepathy spec, version 0.14.0.1

Copyright (C) 2005, 2006 Collabora Limited
Copyright (C) 2005, 2006 Nokia Corporation
Copyright (C) 2006 INdT

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


*/

#ifdef __cplusplus
extern "C" {
#endif


/* TpConnMgrParamFlags (bitfield/set of flags, 0 for none) */

typedef enum {
    TP_CONN_MGR_PARAM_FLAG_REQUIRED = 1,
    TP_CONN_MGR_PARAM_FLAG_REGISTER = 2,
    TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT = 4,
} TpConnMgrParamFlags;


/* TpHandleType (enum) */

typedef enum {
    TP_HANDLE_TYPE_NONE = 0,
    TP_HANDLE_TYPE_CONTACT = 1,
    TP_HANDLE_TYPE_ROOM = 2,
    TP_HANDLE_TYPE_LIST = 3,
    TP_HANDLE_TYPE_GROUP = 4,
    LAST_TP_HANDLE_TYPE = 4
} TpHandleType;


/* TpConnectionStatus (enum) */

typedef enum {
    TP_CONNECTION_STATUS_CONNECTED = 0,
    TP_CONNECTION_STATUS_CONNECTING = 1,
    TP_CONNECTION_STATUS_DISCONNECTED = 2,
    LAST_TP_CONNECTION_STATUS = 2
} TpConnectionStatus;


/* TpConnectionStatusReason (enum) */

typedef enum {
    TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED = 0,
    TP_CONNECTION_STATUS_REASON_REQUESTED = 1,
    TP_CONNECTION_STATUS_REASON_NETWORK_ERROR = 2,
    TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED = 3,
    TP_CONNECTION_STATUS_REASON_ENCRYPTION_ERROR = 4,
    TP_CONNECTION_STATUS_REASON_NAME_IN_USE = 5,
    TP_CONNECTION_STATUS_REASON_CERT_NOT_PROVIDED = 6,
    TP_CONNECTION_STATUS_REASON_CERT_UNTRUSTED = 7,
    TP_CONNECTION_STATUS_REASON_CERT_EXPIRED = 8,
    TP_CONNECTION_STATUS_REASON_CERT_NOT_ACTIVATED = 9,
    TP_CONNECTION_STATUS_REASON_CERT_HOSTNAME_MISMATCH = 10,
    TP_CONNECTION_STATUS_REASON_CERT_FINGERPRINT_MISMATCH = 11,
    TP_CONNECTION_STATUS_REASON_CERT_SELF_SIGNED = 12,
    TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR = 13,
    LAST_TP_CONNECTION_STATUS_REASON = 13
} TpConnectionStatusReason;


/* TpConnectionAliasFlags (bitfield/set of flags, 0 for none) */

typedef enum {
    TP_CONNECTION_ALIAS_FLAG_USER_SET = 1,
} TpConnectionAliasFlags;


/* TpConnectionCapabilityFlags (bitfield/set of flags, 0 for none) */

typedef enum {
    TP_CONNECTION_CAPABILITY_FLAG_CREATE = 1,
    TP_CONNECTION_CAPABILITY_FLAG_INVITE = 2,
} TpConnectionCapabilityFlags;


/* TpConnectionPresenceType (enum) */

typedef enum {
    TP_CONNECTION_PRESENCE_TYPE_UNSET = 0,
    TP_CONNECTION_PRESENCE_TYPE_OFFLINE = 1,
    TP_CONNECTION_PRESENCE_TYPE_AVAILABLE = 2,
    TP_CONNECTION_PRESENCE_TYPE_AWAY = 3,
    TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY = 4,
    TP_CONNECTION_PRESENCE_TYPE_HIDDEN = 5,
} TpConnectionPresenceType;


/* TpChannelContactSearchState (enum) */

typedef enum {
    TP_CHANNEL_CONTACT_SEARCH_STATE_BEFORE = 0,
    TP_CHANNEL_CONTACT_SEARCH_STATE_DURING = 1,
    TP_CHANNEL_CONTACT_SEARCH_STATE_AFTER = 2,
} TpChannelContactSearchState;


/* TpMediaStreamType (enum) */

typedef enum {
    TP_MEDIA_STREAM_TYPE_AUDIO = 0,
    TP_MEDIA_STREAM_TYPE_VIDEO = 1,
} TpMediaStreamType;


/* TpMediaStreamState (enum) */

typedef enum {
    TP_MEDIA_STREAM_STATE_DISCONNECTED = 0,
    TP_MEDIA_STREAM_STATE_CONNECTING = 1,
    TP_MEDIA_STREAM_STATE_CONNECTED = 2,
} TpMediaStreamState;


/* TpMediaStreamDirection (enum) */

typedef enum {
    TP_MEDIA_STREAM_DIRECTION_NONE = 0,
    TP_MEDIA_STREAM_DIRECTION_SEND = 1,
    TP_MEDIA_STREAM_DIRECTION_RECEIVE = 2,
    TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL = 3,
} TpMediaStreamDirection;


/* TpMediaStreamPendingSend (bitfield/set of flags, 0 for none) */

typedef enum {
    TP_MEDIA_STREAM_PENDING_LOCAL_SEND = 1,
    TP_MEDIA_STREAM_PENDING_REMOTE_SEND = 2,
} TpMediaStreamPendingSend;


/* TpChannelMediaCapabilities (bitfield/set of flags, 0 for none) */
/* 
        The channel-type-specific capability flags used for
        Channel.Type.StreamedMedia in the Connection.Interface.Capabilities
        interface.
       */
typedef enum {
    TP_CHANNEL_MEDIA_CAPABILITY_AUDIO = 1,
    TP_CHANNEL_MEDIA_CAPABILITY_VIDEO = 2,
} TpChannelMediaCapabilities;


/* TpChannelTextSendError (enum) */

typedef enum {
    TP_CHANNEL_SEND_ERROR_UNKNOWN = 0,
    TP_CHANNEL_SEND_ERROR_OFFLINE = 1,
    TP_CHANNEL_SEND_ERROR_INVALID_CONTACT = 2,
    TP_CHANNEL_SEND_ERROR_PERMISSION_DENIED = 3,
    TP_CHANNEL_SEND_ERROR_TOO_LONG = 4,
    TP_CHANNEL_SEND_ERROR_NOT_IMPLEMENTED = 5,
} TpChannelTextSendError;


/* TpChannelTextMessageType (enum) */

typedef enum {
    TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL = 0,
    TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION = 1,
    TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE = 2,
    TP_CHANNEL_TEXT_MESSAGE_TYPE_AUTO_REPLY = 3,
} TpChannelTextMessageType;


/* TpChannelTextMessageFlags (bitfield/set of flags, 0 for none) */

typedef enum {
    TP_CHANNEL_TEXT_MESSAGE_FLAG_TRUNCATED = 1,
} TpChannelTextMessageFlags;


/* TpChannelGroupFlags (bitfield/set of flags, 0 for none) */

typedef enum {
    TP_CHANNEL_GROUP_FLAG_CAN_ADD = 1,
    TP_CHANNEL_GROUP_FLAG_CAN_REMOVE = 2,
    TP_CHANNEL_GROUP_FLAG_CAN_RESCIND = 4,
    TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD = 8,
    TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE = 16,
    TP_CHANNEL_GROUP_FLAG_MESSAGE_ACCEPT = 32,
    TP_CHANNEL_GROUP_FLAG_MESSAGE_REJECT = 64,
    TP_CHANNEL_GROUP_FLAG_MESSAGE_RESCIND = 128,
    TP_CHANNEL_GROUP_FLAG_CHANNEL_SPECIFIC_HANDLES = 256,
    TP_CHANNEL_GROUP_FLAG_ONLY_ONE_GROUP = 512,
} TpChannelGroupFlags;


/* TpChannelGroupChangeReason (enum) */

typedef enum {
    TP_CHANNEL_GROUP_CHANGE_REASON_NONE = 0,
    TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE = 1,
    TP_CHANNEL_GROUP_CHANGE_REASON_KICKED = 2,
    TP_CHANNEL_GROUP_CHANGE_REASON_BUSY = 3,
    TP_CHANNEL_GROUP_CHANGE_REASON_INVITED = 4,
    TP_CHANNEL_GROUP_CHANGE_REASON_BANNED = 5,
    TP_CHANNEL_GROUP_CHANGE_REASON_ERROR = 6,
} TpChannelGroupChangeReason;


/* TpChannelHoldState (enum) */

typedef enum {
    TP_CHANNEL_HOLD_STATE_NONE = 0,
    TP_CHANNEL_HOLD_STATE_SEND_ONLY = 1,
    TP_CHANNEL_HOLD_STATE_RECV_ONLY = 2,
    TP_CHANNEL_HOLD_STATE_BOTH = 3,
} TpChannelHoldState;


/* TpChannelPasswordFlags (bitfield/set of flags, 0 for none) */

typedef enum {
    TP_CHANNEL_PASSWORD_FLAG_PROVIDE = 8,
} TpChannelPasswordFlags;


/* TpMediaStreamError (enum) */

typedef enum {
    TP_MEDIA_STREAM_ERROR_UNKNOWN = 0,
    TP_MEDIA_STREAM_ERROR_EOS = 1,
} TpMediaStreamError;


/* TpMediaStreamBaseProto (enum) */

typedef enum {
    TP_MEDIA_STREAM_BASE_PROTO_UDP = 0,
    TP_MEDIA_STREAM_BASE_PROTO_TCP = 1,
} TpMediaStreamBaseProto;


/* TpMediaStreamTransportType (enum) */

typedef enum {
    TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL = 0,
    TP_MEDIA_STREAM_TRANSPORT_TYPE_DERIVED = 1,
    TP_MEDIA_STREAM_TRANSPORT_TYPE_RELAY = 2,
} TpMediaStreamTransportType;


/* TpPropertyFlags (bitfield/set of flags, 0 for none) */

typedef enum {
    TP_PROPERTY_FLAG_READ = 1,
    TP_PROPERTY_FLAG_WRITE = 2,
} TpPropertyFlags;



#ifdef __cplusplus
}
#endif

