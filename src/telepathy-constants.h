/*
 * telepathy.h - constants used in telepathy protocol
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

typedef enum
{
    TP_CONNECTION_STATUS_CONNECTED = 0,
    TP_CONNECTION_STATUS_CONNECTING = 1,
    TP_CONNECTION_STATUS_DISCONNECTED = 2
} TpConnectionStatus;

typedef enum
{
    TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED = 0,
    TP_CONNECTION_STATUS_REASON_REQUESTED = 1,
    TP_CONNECTION_STATUS_REASON_NETWORK_ERROR = 2,
    TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED = 3,
    TP_CONNECTION_STATUS_REASON_ENCRYPTION_ERROR = 4
} TpConnectionStatusReason;

#endif /* #ifndef __TELEPATHY_CONSTANTS_H__ */
