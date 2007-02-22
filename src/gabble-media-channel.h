/*
 * gabble-media-channel.h - Header for GabbleMediaChannel
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

#ifndef __GABBLE_MEDIA_CHANNEL_H__
#define __GABBLE_MEDIA_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/group-mixin.h>

#include "gabble-media-session.h"
#include "handles.h"
#include "gabble-presence.h"

G_BEGIN_DECLS

typedef struct _GabbleMediaChannel GabbleMediaChannel;
typedef struct _GabbleMediaChannelClass GabbleMediaChannelClass;

struct _GabbleMediaChannelClass {
    GObjectClass parent_class;

    TpGroupMixinClass group_class;
    TpPropertiesMixinClass properties_class;
};

struct _GabbleMediaChannel {
    GObject parent;

    TpGroupMixin group;
    TpPropertiesMixin properties;

    gpointer priv;
};

GType gabble_media_channel_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_MEDIA_CHANNEL \
  (gabble_media_channel_get_type())
#define GABBLE_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MEDIA_CHANNEL, GabbleMediaChannel))
#define GABBLE_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MEDIA_CHANNEL, GabbleMediaChannelClass))
#define GABBLE_IS_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MEDIA_CHANNEL))
#define GABBLE_IS_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MEDIA_CHANNEL))
#define GABBLE_MEDIA_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MEDIA_CHANNEL, GabbleMediaChannelClass))

gboolean
_gabble_media_channel_add_member (TpSvcChannelInterfaceGroup *obj,
                                  TpHandle handle,
                                  const gchar *message,
                                  GError **error);

gboolean
_gabble_media_channel_dispatch_session_action (GabbleMediaChannel *chan,
                                               TpHandle peer,
                                               const gchar *peer_resource,
                                               const gchar *sid,
                                               LmMessage *message,
                                               LmMessageNode *session_node,
                                               const gchar *action,
                                               GError **error);

void
_gabble_media_channel_stream_state (GabbleMediaChannel *chan,
                                    guint state);

guint
_gabble_media_channel_get_stream_id (GabbleMediaChannel *chan);

GabblePresenceCapabilities
_gabble_media_channel_typeflags_to_caps (TpChannelMediaCapabilities flags);

TpChannelMediaCapabilities
_gabble_media_channel_caps_to_typeflags (GabblePresenceCapabilities caps);

void gabble_media_channel_close (GabbleMediaChannel *);

G_END_DECLS

#endif /* #ifndef __GABBLE_MEDIA_CHANNEL_H__*/
