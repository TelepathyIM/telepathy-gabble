/*
 * base-channel.h - Header for GabbleBaseChannel
 *
 * Copyright (C) 2009 Collabora Ltd.
 * Copyright (C) 2009 Nokia Corporation
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

#ifndef __GABBLE_BASE_CHANNEL_H__
#define __GABBLE_BASE_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/dbus-properties-mixin.h>

#include "connection.h"

G_BEGIN_DECLS

typedef struct _GabbleBaseChannel GabbleBaseChannel;
typedef struct _GabbleBaseChannelClass GabbleBaseChannelClass;
typedef struct _GabbleBaseChannelPrivate GabbleBaseChannelPrivate;

struct _GabbleBaseChannelClass {
    GObjectClass parent_class;

    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleBaseChannel {
    GObject parent;

    GabbleConnection *conn;

    char *object_path;

    const gchar *channel_type;
    const gchar **interfaces;
    TpHandleType target_type;
    TpHandle target;
    TpHandle initiator;

    gboolean closed;

    GabbleBaseChannelPrivate *priv;
};

void gabble_base_channel_register (GabbleBaseChannel *chan);

GType gabble_base_channel_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_BASE_CHANNEL \
  (gabble_base_channel_get_type ())
#define GABBLE_BASE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_BASE_CHANNEL, \
                              GabbleBaseChannel))
#define GABBLE_BASE_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_BASE_CHANNEL, \
                           GabbleBaseChannelClass))
#define GABBLE_IS_BASE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_BASE_CHANNEL))
#define GABBLE_IS_BASE_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_BASE_CHANNEL))
#define GABBLE_BASE_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_BASE_CHANNEL, \
                              GabbleBaseChannelClass))

G_END_DECLS

#endif /* #ifndef __GABBLE_BASE_CHANNEL_H__*/
