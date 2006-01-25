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

#include "gintset.h"

G_BEGIN_DECLS

typedef struct _GabbleRosterChannel GabbleRosterChannel;
typedef struct _GabbleRosterChannelClass GabbleRosterChannelClass;

struct _GabbleRosterChannelClass {
    GObjectClass parent_class;
};

struct _GabbleRosterChannel {
    GObject parent;
};

GType gabble_roster_channel_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_ROSTER_CHANNEL \
  (gabble_roster_channel_get_type())
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


void
_gabble_roster_channel_change_members (GabbleRosterChannel *chan,
                                       const char *message,
                                       GIntSet *add,
                                       GIntSet *remove,
                                       GIntSet *local_pending,
                                       GIntSet *remote_pending);


gboolean gabble_roster_channel_add_members (GabbleRosterChannel *obj, const GArray * contacts, const gchar * message, GError **error);
gboolean gabble_roster_channel_close (GabbleRosterChannel *obj, GError **error);
gboolean gabble_roster_channel_get_channel_type (GabbleRosterChannel *obj, gchar ** ret, GError **error);
gboolean gabble_roster_channel_get_group_flags (GabbleRosterChannel *obj, guint* ret, GError **error);
gboolean gabble_roster_channel_get_handle (GabbleRosterChannel *obj, guint* ret, guint* ret1, GError **error);
gboolean gabble_roster_channel_get_interfaces (GabbleRosterChannel *obj, gchar *** ret, GError **error);
gboolean gabble_roster_channel_get_local_pending_members (GabbleRosterChannel *obj, GArray ** ret, GError **error);
gboolean gabble_roster_channel_get_members (GabbleRosterChannel *obj, GArray ** ret, GError **error);
gboolean gabble_roster_channel_get_remote_pending_members (GabbleRosterChannel *obj, GArray ** ret, GError **error);
gboolean gabble_roster_channel_get_self_handle (GabbleRosterChannel *obj, guint* ret, GError **error);
gboolean gabble_roster_channel_remove_members (GabbleRosterChannel *obj, const GArray * contacts, const gchar * message, GError **error);


G_END_DECLS

#endif /* #ifndef __GABBLE_ROSTER_CHANNEL_H__*/
