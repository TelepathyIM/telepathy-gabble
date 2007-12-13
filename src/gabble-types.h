/*
 * gabble-types.h - Header for Gabble type definitions
 *
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#ifndef __GABBLE_TYPES_H__
#define __GABBLE_TYPES_H__

#include <config.h>

#include <telepathy-glib/handle.h>

G_BEGIN_DECLS

typedef struct _GabbleConnection GabbleConnection;
typedef struct _GabbleDisco GabbleDisco;
typedef struct _GabbleMucChannel GabbleMucChannel;
typedef struct _GabblePresence GabblePresence;
typedef struct _GabblePresenceCache GabblePresenceCache;
typedef struct _GabbleRoster GabbleRoster;
typedef struct _GabbleRosterChannel GabbleRosterChannel;
typedef struct _GabbleVCardManager GabbleVCardManager;
typedef struct _GabbleBytestreamFactory GabbleBytestreamFactory;
typedef struct _GabblePrivateTubesFactory GabblePrivateTubesFactory;
typedef struct _GabbleRequestPipeline GabbleRequestPipeline;

typedef enum {
    INITIATOR_INVALID = -1,
    INITIATOR_LOCAL = 0,
    INITIATOR_REMOTE,
} JingleInitiator;

typedef enum {
    PRESENCE_CAP_NONE = 0,
    PRESENCE_CAP_GOOGLE_TRANSPORT_P2P = 1 << 0,
    PRESENCE_CAP_GOOGLE_VOICE = 1 << 1,
    PRESENCE_CAP_JINGLE = 1 << 2,
    PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO = 1 << 3,
    PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO = 1 << 4,
    PRESENCE_CAP_JINGLE_H263_N800_HACK = 1 << 5,
    PRESENCE_CAP_CHAT_STATES = 1 << 6,
    PRESENCE_CAP_SI = 1 << 7,
    PRESENCE_CAP_BYTESTREAMS = 1 << 8,
    PRESENCE_CAP_IBB = 1 << 9,
    PRESENCE_CAP_SI_TUBES = 1 << 10,
    PRESENCE_CAP_FILE_TRANSFER = 1 << 11,
    PRESENCE_CAP_OLPC_1 = 1 << 12
} GabblePresenceCapabilities;

G_END_DECLS

#endif
