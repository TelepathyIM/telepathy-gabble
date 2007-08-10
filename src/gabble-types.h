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
typedef struct _GabbleTubesFactory GabbleTubesFactory;
typedef struct _GabbleRequestPipeline GabbleRequestPipeline;

typedef enum {
    INITIATOR_INVALID = -1,
    INITIATOR_LOCAL = 0,
    INITIATOR_REMOTE,
} JingleInitiator;

G_END_DECLS

#endif
