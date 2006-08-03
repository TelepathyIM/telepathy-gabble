/*
 * gabble-roster-channel.c - Source for GabbleRosterChannel
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

#include "gabble-roster-channel.h"
#include "gabble-roster-channel-signals-marshal.h"

#include "gabble-roster-channel-glue.h"

G_DEFINE_TYPE(GabbleRosterChannel, gabble_roster_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    GROUP_FLAGS_CHANGED,
    MEMBERS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _GabbleRosterChannelPrivate GabbleRosterChannelPrivate;

struct _GabbleRosterChannelPrivate
{
  gboolean dispose_has_run;
};

#define GABBLE_ROSTER_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_ROSTER_CHANNEL, GabbleRosterChannelPrivate))

static void
gabble_roster_channel_init (GabbleRosterChannel *obj)
{
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
}

static void gabble_roster_channel_dispose (GObject *object);
static void gabble_roster_channel_finalize (GObject *object);

static void
gabble_roster_channel_class_init (GabbleRosterChannelClass *gabble_roster_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_roster_channel_class);

  g_type_class_add_private (gabble_roster_channel_class, sizeof (GabbleRosterChannelPrivate));

  object_class->dispose = gabble_roster_channel_dispose;
  object_class->finalize = gabble_roster_channel_finalize;

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_roster_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_roster_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[GROUP_FLAGS_CHANGED] =
    g_signal_new ("group-flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_roster_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_roster_channel_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[MEMBERS_CHANGED] =
    g_signal_new ("members-changed",
                  G_OBJECT_CLASS_TYPE (gabble_roster_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_roster_channel_marshal_VOID__STRING_BOXED_BOXED_BOXED_BOXED_INT_INT,
                  G_TYPE_NONE, 7, G_TYPE_STRING, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, DBUS_TYPE_G_UINT_ARRAY, G_TYPE_UINT, G_TYPE_UINT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_roster_channel_class), &dbus_glib_gabble_roster_channel_object_info);
}

void
gabble_roster_channel_dispose (GObject *object)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_roster_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roster_channel_parent_class)->dispose (object);
}

void
gabble_roster_channel_finalize (GObject *object)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gabble_roster_channel_parent_class)->finalize (object);
}



/**
 * gabble_roster_channel_add_members
 *
 * Implements DBus method AddMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_add_members (GabbleRosterChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_close (GabbleRosterChannel *obj, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_get_all_members
 *
 * Implements DBus method GetAllMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_all_members (GabbleRosterChannel *obj, GArray ** ret, GArray ** ret1, GArray ** ret2, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_channel_type (GabbleRosterChannel *obj, gchar ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_get_group_flags
 *
 * Implements DBus method GetGroupFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_group_flags (GabbleRosterChannel *obj, guint* ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_handle (GabbleRosterChannel *obj, guint* ret, guint* ret1, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_get_handle_owners
 *
 * Implements DBus method GetHandleOwners
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_handle_owners (GabbleRosterChannel *obj, const GArray * handles, GArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_interfaces (GabbleRosterChannel *obj, gchar *** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_get_local_pending_members
 *
 * Implements DBus method GetLocalPendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_local_pending_members (GabbleRosterChannel *obj, GArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_get_members
 *
 * Implements DBus method GetMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_members (GabbleRosterChannel *obj, GArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_get_remote_pending_members
 *
 * Implements DBus method GetRemotePendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_remote_pending_members (GabbleRosterChannel *obj, GArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_get_self_handle
 *
 * Implements DBus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_get_self_handle (GabbleRosterChannel *obj, guint* ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_roster_channel_remove_members
 *
 * Implements DBus method RemoveMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_roster_channel_remove_members (GabbleRosterChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return TRUE;
}

