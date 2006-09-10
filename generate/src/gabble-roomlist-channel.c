/*
 * gabble-roomlist-channel.c - Source for GabbleRoomlistChannel
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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "gabble-roomlist-channel.h"
#include "gabble-roomlist-channel-signals-marshal.h"

#include "gabble-roomlist-channel-glue.h"

G_DEFINE_TYPE(GabbleRoomlistChannel, gabble_roomlist_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    GOT_ROOMS,
    LISTING_ROOMS,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _GabbleRoomlistChannelPrivate GabbleRoomlistChannelPrivate;

struct _GabbleRoomlistChannelPrivate
{
  gboolean dispose_has_run;
};

#define GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE(obj) \
    ((GabbleRoomlistChannelPrivate *)obj->priv)

static void
gabble_roomlist_channel_init (GabbleRoomlistChannel *self)
{
  GabbleRoomlistChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_ROOMLIST_CHANNEL, GabbleRoomlistChannelPrivate);

  self->priv = priv;

  /* allocate any data required by the object here */
}

static void gabble_roomlist_channel_dispose (GObject *object);
static void gabble_roomlist_channel_finalize (GObject *object);

static void
gabble_roomlist_channel_class_init (GabbleRoomlistChannelClass *gabble_roomlist_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_roomlist_channel_class);

  g_type_class_add_private (gabble_roomlist_channel_class, sizeof (GabbleRoomlistChannelPrivate));

  object_class->dispose = gabble_roomlist_channel_dispose;
  object_class->finalize = gabble_roomlist_channel_finalize;

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_roomlist_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[GOT_ROOMS] =
    g_signal_new ("got-rooms",
                  G_OBJECT_CLASS_TYPE (gabble_roomlist_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)), G_TYPE_INVALID)))));

  signals[LISTING_ROOMS] =
    g_signal_new ("listing-rooms",
                  G_OBJECT_CLASS_TYPE (gabble_roomlist_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_roomlist_channel_class), &dbus_glib_gabble_roomlist_channel_object_info);
}

void
gabble_roomlist_channel_dispose (GObject *object)
{
  GabbleRoomlistChannel *self = GABBLE_ROOMLIST_CHANNEL (object);
  GabbleRoomlistChannelPrivate *priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->dispose (object);
}

void
gabble_roomlist_channel_finalize (GObject *object)
{
  GabbleRoomlistChannel *self = GABBLE_ROOMLIST_CHANNEL (object);
  GabbleRoomlistChannelPrivate *priv = GABBLE_ROOMLIST_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gabble_roomlist_channel_parent_class)->finalize (object);
}



/**
 * gabble_roomlist_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_close (GabbleRoomlistChannel *self,
                               GError **error)
{
  return TRUE;
}


/**
 * gabble_roomlist_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_get_channel_type (GabbleRoomlistChannel *self,
                                          gchar **ret,
                                          GError **error)
{
  return TRUE;
}


/**
 * gabble_roomlist_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_get_handle (GabbleRoomlistChannel *self,
                                    guint *ret,
                                    guint *ret1,
                                    GError **error)
{
  return TRUE;
}


/**
 * gabble_roomlist_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_get_interfaces (GabbleRoomlistChannel *self,
                                        gchar ***ret,
                                        GError **error)
{
  return TRUE;
}


/**
 * gabble_roomlist_channel_get_listing_rooms
 *
 * Implements D-Bus method GetListingRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_get_listing_rooms (GabbleRoomlistChannel *self,
                                           gboolean *ret,
                                           GError **error)
{
  return TRUE;
}


/**
 * gabble_roomlist_channel_list_rooms
 *
 * Implements D-Bus method ListRooms
 * on interface org.freedesktop.Telepathy.Channel.Type.RoomList
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roomlist_channel_list_rooms (GabbleRoomlistChannel *self,
                                    GError **error)
{
  return TRUE;
}

