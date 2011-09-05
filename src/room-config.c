/*
 * room-config.c - Channel.Interface.RoomConfig1 implementation
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

#include "room-config.h"

#define DEBUG_FLAG GABBLE_DEBUG_MUC
#include "debug.h"

struct _GabbleRoomConfigPrivate {
    gpointer hi_dere;
};

G_DEFINE_TYPE (GabbleRoomConfig, gabble_room_config, TP_TYPE_BASE_ROOM_CONFIG)

static void
gabble_room_config_init (GabbleRoomConfig *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_ROOM_CONFIG,
      GabbleRoomConfigPrivate);
}

static void
gabble_room_config_class_init (GabbleRoomConfigClass *klass)
{
  g_type_class_add_private (klass, sizeof (GabbleRoomConfigPrivate));
}

GabbleRoomConfig *
gabble_room_config_new (
    TpBaseChannel *channel)
{
  g_return_val_if_fail (TP_IS_BASE_CHANNEL (channel), NULL);

  return g_object_new (GABBLE_TYPE_ROOM_CONFIG,
      "channel", channel,
      NULL);
}
