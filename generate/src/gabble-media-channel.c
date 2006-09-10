/*
 * gabble-media-channel.c - Source for GabbleMediaChannel
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

#include "gabble-media-channel.h"
#include "gabble-media-channel-signals-marshal.h"

#include "gabble-media-channel-glue.h"

G_DEFINE_TYPE(GabbleMediaChannel, gabble_media_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    GROUP_FLAGS_CHANGED,
    MEMBERS_CHANGED,
    NEW_SESSION_HANDLER,
    STREAM_ADDED,
    STREAM_DIRECTION_CHANGED,
    STREAM_REMOVED,
    STREAM_STATE_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _GabbleMediaChannelPrivate GabbleMediaChannelPrivate;

struct _GabbleMediaChannelPrivate
{
  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_CHANNEL_GET_PRIVATE(obj) \
    ((GabbleMediaChannelPrivate *)obj->priv)

static void
gabble_media_channel_init (GabbleMediaChannel *self)
{
  GabbleMediaChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_MEDIA_CHANNEL, GabbleMediaChannelPrivate);

  self->priv = priv;

  /* allocate any data required by the object here */
}

static void gabble_media_channel_dispose (GObject *object);
static void gabble_media_channel_finalize (GObject *object);

static void
gabble_media_channel_class_init (GabbleMediaChannelClass *gabble_media_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_channel_class);

  g_type_class_add_private (gabble_media_channel_class, sizeof (GabbleMediaChannelPrivate));

  object_class->dispose = gabble_media_channel_dispose;
  object_class->finalize = gabble_media_channel_finalize;

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[GROUP_FLAGS_CHANGED] =
    g_signal_new ("group-flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[MEMBERS_CHANGED] =
    g_signal_new ("members-changed",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__STRING_BOXED_BOXED_BOXED_BOXED_UINT_UINT,
                  G_TYPE_NONE, 7, G_TYPE_STRING, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, G_TYPE_UINT, G_TYPE_UINT);

  signals[NEW_SESSION_HANDLER] =
    g_signal_new ("new-session-handler",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__STRING_STRING,
                  G_TYPE_NONE, 2, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING);

  signals[STREAM_ADDED] =
    g_signal_new ("stream-added",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__UINT_UINT_UINT,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

  signals[STREAM_DIRECTION_CHANGED] =
    g_signal_new ("stream-direction-changed",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__UINT_UINT_UINT,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

  signals[STREAM_REMOVED] =
    g_signal_new ("stream-removed",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[STREAM_STATE_CHANGED] =
    g_signal_new ("stream-state-changed",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_media_channel_class), &dbus_glib_gabble_media_channel_object_info);
}

void
gabble_media_channel_dispose (GObject *object)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_media_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_channel_parent_class)->dispose (object);
}

void
gabble_media_channel_finalize (GObject *object)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gabble_media_channel_parent_class)->finalize (object);
}



/**
 * gabble_media_channel_add_members
 *
 * Implements D-Bus method AddMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_add_members (GabbleMediaChannel *self,
                                  const GArray *contacts,
                                  const gchar *message,
                                  GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_close (GabbleMediaChannel *self,
                            GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_get_all_members
 *
 * Implements D-Bus method GetAllMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_get_all_members (GabbleMediaChannel *self,
                                      GArray **ret,
                                      GArray **ret1,
                                      GArray **ret2,
                                      GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_get_channel_type (GabbleMediaChannel *self,
                                       gchar **ret,
                                       GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_get_group_flags
 *
 * Implements D-Bus method GetGroupFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_get_group_flags (GabbleMediaChannel *self,
                                      guint *ret,
                                      GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_get_handle (GabbleMediaChannel *self,
                                 guint *ret,
                                 guint *ret1,
                                 GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_get_handle_owners
 *
 * Implements D-Bus method GetHandleOwners
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_get_handle_owners (GabbleMediaChannel *self,
                                        const GArray *handles,
                                        GArray **ret,
                                        GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_get_interfaces (GabbleMediaChannel *self,
                                     gchar ***ret,
                                     GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_get_local_pending_members
 *
 * Implements D-Bus method GetLocalPendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_get_local_pending_members (GabbleMediaChannel *self,
                                                GArray **ret,
                                                GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_get_members
 *
 * Implements D-Bus method GetMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_get_members (GabbleMediaChannel *self,
                                  GArray **ret,
                                  GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_get_remote_pending_members
 *
 * Implements D-Bus method GetRemotePendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_get_remote_pending_members (GabbleMediaChannel *self,
                                                 GArray **ret,
                                                 GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_get_self_handle
 *
 * Implements D-Bus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_get_self_handle (GabbleMediaChannel *self,
                                      guint *ret,
                                      GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_get_session_handlers
 *
 * Implements D-Bus method GetSessionHandlers
 * on interface org.freedesktop.Telepathy.Channel.Interface.MediaSignalling
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_get_session_handlers (GabbleMediaChannel *self,
                                           GPtrArray **ret,
                                           GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_list_streams
 *
 * Implements D-Bus method ListStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_list_streams (GabbleMediaChannel *self,
                                   GPtrArray **ret,
                                   GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_remove_members
 *
 * Implements D-Bus method RemoveMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_remove_members (GabbleMediaChannel *self,
                                     const GArray *contacts,
                                     const gchar *message,
                                     GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_request_stream_direction
 *
 * Implements D-Bus method RequestStreamDirection
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_request_stream_direction (GabbleMediaChannel *self,
                                               guint stream_id,
                                               guint stream_direction,
                                               GError **error)
{
  return TRUE;
}


/**
 * gabble_media_channel_request_streams
 *
 * Implements D-Bus method RequestStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_channel_request_streams (GabbleMediaChannel *self,
                                      guint contact_handle,
                                      const GArray *types,
                                      GArray **ret,
                                      GError **error)
{
  return TRUE;
}

