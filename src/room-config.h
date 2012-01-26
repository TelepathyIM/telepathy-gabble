/*
 * room-config.h - header for Channel.I.RoomConfig1 implementation
 * Copyright ©2011 Collabora Ltd.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef GABBLE_ROOM_CONFIG_H
#define GABBLE_ROOM_CONFIG_H

#include <glib-object.h>
#include <telepathy-glib/base-room-config.h>

typedef struct _GabbleRoomConfig GabbleRoomConfig;
typedef struct _GabbleRoomConfigClass GabbleRoomConfigClass;
typedef struct _GabbleRoomConfigPrivate GabbleRoomConfigPrivate;

struct _GabbleRoomConfigClass {
    TpBaseRoomConfigClass parent_class;
};

struct _GabbleRoomConfig {
    TpBaseRoomConfig parent;

    GabbleRoomConfigPrivate *priv;
};

GabbleRoomConfig *gabble_room_config_new (
    TpBaseChannel *channel);

/* TYPE MACROS */
GType gabble_room_config_get_type (void);

#define GABBLE_TYPE_ROOM_CONFIG \
  (gabble_room_config_get_type ())
#define GABBLE_ROOM_CONFIG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_ROOM_CONFIG, GabbleRoomConfig))
#define GABBLE_ROOM_CONFIG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_ROOM_CONFIG,\
                           GabbleRoomConfigClass))
#define GABBLE_IS_ROOM_CONFIG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_ROOM_CONFIG))
#define GABBLE_IS_ROOM_CONFIG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_ROOM_CONFIG))
#define GABBLE_ROOM_CONFIG_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_ROOM_CONFIG, \
                              GabbleRoomConfigClass))

#endif /* GABBLE_ROOM_CONFIG_H */
