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
#include <telepathy-glib/base-channel.h>

typedef struct _GabbleRoomConfig GabbleRoomConfig;
typedef struct _GabbleRoomConfigClass GabbleRoomConfigClass;
typedef struct _GabbleRoomConfigPrivate GabbleRoomConfigPrivate;

typedef void (*GabbleRoomConfigUpdateAsync) (
    TpBaseChannel *channel,
    GHashTable *validated_properties,
    GAsyncReadyCallback callback,
    gpointer user_data);
typedef gboolean (*GabbleRoomConfigUpdateFinish) (
    TpBaseChannel *channel,
    GAsyncResult *result,
    GError **error);

struct _GabbleRoomConfigClass {
    /*< private >*/
    GObjectClass parent_class;

    /*< public >*/
    GabbleRoomConfigUpdateAsync update_async;
    GabbleRoomConfigUpdateFinish update_finish;
};

struct _GabbleRoomConfig {
    /*< private >*/
    GObject parent;
    GabbleRoomConfigPrivate *priv;
};

/* By an astonishing coincidence, the nicknames for this enum are the names of
 * corresponding D-Bus properties.
 */
typedef enum {
    GABBLE_ROOM_CONFIG_ANONYMOUS = 0, /*< nick=Anonymous >*/
    GABBLE_ROOM_CONFIG_INVITE_ONLY, /*< nick=InviteOnly >*/
    GABBLE_ROOM_CONFIG_LIMIT, /*< nick=Limit >*/
    GABBLE_ROOM_CONFIG_MODERATED, /*< nick=Moderated >*/
    GABBLE_ROOM_CONFIG_TITLE, /*< nick=Title >*/
    GABBLE_ROOM_CONFIG_DESCRIPTION, /*< nick=Description >*/
    GABBLE_ROOM_CONFIG_PERSISTENT, /*< nick=Persistent >*/
    GABBLE_ROOM_CONFIG_PRIVATE, /*< nick=Private >*/
    GABBLE_ROOM_CONFIG_PASSWORD_PROTECTED, /*< nick=PasswordProtected >*/
    GABBLE_ROOM_CONFIG_PASSWORD, /*< nick=Password >*/

    GABBLE_NUM_ROOM_CONFIG_PROPERTIES /*< skip >*/
} GabbleRoomConfigProperty;

void gabble_room_config_register_class (
    TpBaseChannelClass *base_channel_class);
void gabble_room_config_iface_init (
    gpointer g_iface,
    gpointer iface_data);

GabbleRoomConfig *gabble_room_config_new (
    TpBaseChannel *channel);

void gabble_room_config_set_can_update_configuration (
    GabbleRoomConfig *self,
    gboolean can_update_configuration);

void gabble_room_config_set_property_mutable (
    GabbleRoomConfig *self,
    GabbleRoomConfigProperty property_id,
    gboolean is_mutable);

void gabble_room_config_emit_properties_changed (
    GabbleRoomConfig *self);

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
