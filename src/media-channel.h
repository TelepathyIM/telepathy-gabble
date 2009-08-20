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

#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/properties-mixin.h>

#include "presence.h"

G_BEGIN_DECLS

typedef struct _GabbleMediaChannel GabbleMediaChannel;
typedef struct _GabbleMediaChannelPrivate GabbleMediaChannelPrivate;
typedef struct _GabbleMediaChannelClass GabbleMediaChannelClass;

struct _GabbleMediaChannelClass {
    GObjectClass parent_class;

    TpGroupMixinClass group_class;
    TpPropertiesMixinClass properties_class;
    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleMediaChannel {
    GObject parent;

    TpGroupMixin group;
    TpPropertiesMixin properties;

    GabbleMediaChannelPrivate *priv;
};

GType gabble_media_channel_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_MEDIA_CHANNEL \
  (gabble_media_channel_get_type ())
#define GABBLE_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MEDIA_CHANNEL,\
                              GabbleMediaChannel))
#define GABBLE_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MEDIA_CHANNEL,\
                           GabbleMediaChannelClass))
#define GABBLE_IS_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MEDIA_CHANNEL))
#define GABBLE_IS_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MEDIA_CHANNEL))
#define GABBLE_MEDIA_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MEDIA_CHANNEL, \
                              GabbleMediaChannelClass))

GabblePresenceCapabilities
_gabble_media_channel_typeflags_to_caps (TpChannelMediaCapabilities flags);

TpChannelMediaCapabilities
_gabble_media_channel_caps_to_typeflags (GabblePresenceCapabilities caps);

void gabble_media_channel_request_initial_streams (GabbleMediaChannel *chan,
    GFunc succeeded_cb,
    GFunc failed_cb,
    gpointer user_data);

void gabble_media_channel_close (GabbleMediaChannel *self);

G_END_DECLS

#endif /* #ifndef __GABBLE_MEDIA_CHANNEL_H__*/
