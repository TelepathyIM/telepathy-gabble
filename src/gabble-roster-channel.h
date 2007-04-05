/*
 * gabble-roster-channel.h - Header for GabbleRosterChannel
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

#ifndef __GABBLE_ROSTER_CHANNEL_H__
#define __GABBLE_ROSTER_CHANNEL_H__

#include <glib-object.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/intset.h>

#include "gabble-types.h"

G_BEGIN_DECLS

typedef struct _GabbleRosterChannelClass GabbleRosterChannelClass;

struct _GabbleRosterChannelClass {
    GObjectClass parent_class;

    TpGroupMixinClass group_class;
};

struct _GabbleRosterChannel {
    GObject parent;

    TpGroupMixin group;

    gpointer priv;
};

GType gabble_roster_channel_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_ROSTER_CHANNEL \
  (gabble_roster_channel_get_type ())
#define GABBLE_ROSTER_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_ROSTER_CHANNEL, GabbleRosterChannel))
#define GABBLE_ROSTER_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_ROSTER_CHANNEL, GabbleRosterChannelClass))
#define GABBLE_IS_ROSTER_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_ROSTER_CHANNEL))
#define GABBLE_IS_ROSTER_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_ROSTER_CHANNEL))
#define GABBLE_ROSTER_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_ROSTER_CHANNEL, GabbleRosterChannelClass))

G_END_DECLS

#endif /* #ifndef __GABBLE_ROSTER_CHANNEL_H__*/
