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

#include "config.h"

#include "room-config.h"
#include "muc-channel.h"

#define DEBUG_FLAG GABBLE_DEBUG_MUC
#include "debug.h"

static void gabble_room_config_update_configuration_async (
    TpBaseRoomConfig *base_config,
    GHashTable *validated_properties,
    GAsyncReadyCallback callback,
    gpointer user_data);

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
  TpBaseRoomConfigClass *parent_class = TP_BASE_ROOM_CONFIG_CLASS (klass);

  parent_class->update_async = gabble_room_config_update_configuration_async;
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

static void
updated_configuration_cb (
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GabbleMucChannel *channel = GABBLE_MUC_CHANNEL (source);
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;

  if (!gabble_muc_channel_update_configuration_finish (channel, result, &error))
    {
      g_simple_async_result_set_from_error (simple, error);
      g_clear_error (&error);
    }

  g_simple_async_result_complete (simple);
  g_object_unref (simple);
}

static void
gabble_room_config_update_configuration_async (
    TpBaseRoomConfig *base_config,
    GHashTable *validated_properties,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  TpBaseChannel *base_channel = tp_base_room_config_dup_channel (base_config);
  GSimpleAsyncResult *simple = g_simple_async_result_new (
      G_OBJECT (base_config), callback, user_data,
      gabble_room_config_update_configuration_async);

  gabble_muc_channel_update_configuration_async (
      GABBLE_MUC_CHANNEL (base_channel), validated_properties,
      updated_configuration_cb, simple);
  g_object_unref (base_channel);
}
