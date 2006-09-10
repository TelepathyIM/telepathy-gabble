/*
 * gabble-roomlist-channel.h - Header for GabbleRoomlistChannel
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

#ifndef __GABBLE_ROOMLIST_CHANNEL_H__
#define __GABBLE_ROOMLIST_CHANNEL_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GabbleRoomlistChannel GabbleRoomlistChannel;
typedef struct _GabbleRoomlistChannelClass GabbleRoomlistChannelClass;

struct _GabbleRoomlistChannelClass {
    GObjectClass parent_class;
};

struct _GabbleRoomlistChannel {
    GObject parent;

    gpointer priv;
};

GType gabble_roomlist_channel_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_ROOMLIST_CHANNEL \
  (gabble_roomlist_channel_get_type())
#define GABBLE_ROOMLIST_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_ROOMLIST_CHANNEL, GabbleRoomlistChannel))
#define GABBLE_ROOMLIST_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_ROOMLIST_CHANNEL, GabbleRoomlistChannelClass))
#define GABBLE_IS_ROOMLIST_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_ROOMLIST_CHANNEL))
#define GABBLE_IS_ROOMLIST_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_ROOMLIST_CHANNEL))
#define GABBLE_ROOMLIST_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_ROOMLIST_CHANNEL, GabbleRoomlistChannelClass))


gboolean
gabble_roomlist_channel_close (GabbleRoomlistChannel *self,
                               GError **error);

gboolean
gabble_roomlist_channel_get_channel_type (GabbleRoomlistChannel *self,
                                          gchar **ret,
                                          GError **error);

gboolean
gabble_roomlist_channel_get_handle (GabbleRoomlistChannel *self,
                                    guint *ret,
                                    guint *ret1,
                                    GError **error);

gboolean
gabble_roomlist_channel_get_interfaces (GabbleRoomlistChannel *self,
                                        gchar ***ret,
                                        GError **error);

gboolean
gabble_roomlist_channel_get_listing_rooms (GabbleRoomlistChannel *self,
                                           gboolean *ret,
                                           GError **error);

gboolean
gabble_roomlist_channel_list_rooms (GabbleRoomlistChannel *self,
                                    GError **error);



G_END_DECLS

#endif /* #ifndef __GABBLE_ROOMLIST_CHANNEL_H__*/
