/*
 * gabble-connection.c - Source for GabbleConnection
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

#include "gabble-connection.h"
#include "gabble-connection-signals-marshal.h"

#include "gabble-connection-glue.h"

G_DEFINE_TYPE(GabbleConnection, gabble_connection, G_TYPE_OBJECT)

/* signal enum */
enum
{
    ALIASES_CHANGED,
    AVATAR_UPDATED,
    CAPABILITIES_CHANGED,
    NEW_CHANNEL,
    PRESENCE_UPDATE,
    PROPERTIES_CHANGED,
    PROPERTY_FLAGS_CHANGED,
    STATUS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _GabbleConnectionPrivate GabbleConnectionPrivate;

struct _GabbleConnectionPrivate
{
  gboolean dispose_has_run;
};

#define GABBLE_CONNECTION_GET_PRIVATE(obj) \
    ((GabbleConnectionPrivate *)obj->priv)

static void
gabble_connection_init (GabbleConnection *self)
{
  GabbleConnectionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_CONNECTION, GabbleConnectionPrivate);

  self->priv = priv;

  /* allocate any data required by the object here */
}

static void gabble_connection_dispose (GObject *object);
static void gabble_connection_finalize (GObject *object);

static void
gabble_connection_class_init (GabbleConnectionClass *gabble_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_connection_class);

  g_type_class_add_private (gabble_connection_class, sizeof (GabbleConnectionPrivate));

  object_class->dispose = gabble_connection_dispose;
  object_class->finalize = gabble_connection_finalize;

  signals[ALIASES_CHANGED] =
    g_signal_new ("aliases-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID)))));

  signals[AVATAR_UPDATED] =
    g_signal_new ("avatar-updated",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__UINT_STRING,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

  signals[CAPABILITIES_CHANGED] =
    g_signal_new ("capabilities-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID)))));

  signals[NEW_CHANNEL] =
    g_signal_new ("new-channel",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__STRING_STRING_UINT_UINT_BOOLEAN,
                  G_TYPE_NONE, 5, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN);

  signals[PRESENCE_UPDATE] =
    g_signal_new ("presence-update",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_map ("GHashTable", G_TYPE_UINT, (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)))), G_TYPE_INVALID)))));

  signals[PROPERTIES_CHANGED] =
    g_signal_new ("properties-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_VALUE, G_TYPE_INVALID)))));

  signals[PROPERTY_FLAGS_CHANGED] =
    g_signal_new ("property-flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID)))));

  signals[STATUS_CHANGED] =
    g_signal_new ("status-changed",
                  G_OBJECT_CLASS_TYPE (gabble_connection_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_connection_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_connection_class), &dbus_glib_gabble_connection_object_info);
}

void
gabble_connection_dispose (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_connection_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_connection_parent_class)->dispose (object);
}

void
gabble_connection_finalize (GObject *object)
{
  GabbleConnection *self = GABBLE_CONNECTION (object);
  GabbleConnectionPrivate *priv = GABBLE_CONNECTION_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gabble_connection_parent_class)->finalize (object);
}



/**
 * gabble_connection_add_status
 *
 * Implements D-Bus method AddStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_add_status (GabbleConnection *self,
                              const gchar *status,
                              GHashTable *parms,
                              GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_advertise_capabilities
 *
 * Implements D-Bus method AdvertiseCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_advertise_capabilities (GabbleConnection *self,
                                          const GPtrArray *add,
                                          const gchar **remove,
                                          GPtrArray **ret,
                                          GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_clear_status
 *
 * Implements D-Bus method ClearStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_clear_status (GabbleConnection *self,
                                GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_connect
 *
 * Implements D-Bus method Connect
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_connect (GabbleConnection *self,
                           GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_disconnect
 *
 * Implements D-Bus method Disconnect
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_disconnect (GabbleConnection *self,
                              GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_get_alias_flags
 *
 * Implements D-Bus method GetAliasFlags
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_get_alias_flags (GabbleConnection *self,
                                   guint *ret,
                                   GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_get_avatar_requirements
 *
 * Implements D-Bus method GetAvatarRequirements
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_get_avatar_requirements (GabbleConnection *self,
                                           gchar ***ret,
                                           guint *ret1,
                                           guint *ret2,
                                           guint *ret3,
                                           guint *ret4,
                                           guint *ret5,
                                           GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_get_avatar_tokens
 *
 * Implements D-Bus method GetAvatarTokens
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_get_avatar_tokens (GabbleConnection *self,
                                     const GArray *contacts,
                                     DBusGMethodInvocation *context)
{
  return;
}


/**
 * gabble_connection_get_capabilities
 *
 * Implements D-Bus method GetCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_get_capabilities (GabbleConnection *self,
                                    const GArray *handles,
                                    GPtrArray **ret,
                                    GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_get_interfaces (GabbleConnection *self,
                                  gchar ***ret,
                                  GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_get_presence
 *
 * Implements D-Bus method GetPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_get_presence (GabbleConnection *self,
                                const GArray *contacts,
                                DBusGMethodInvocation *context)
{
  return;
}


/**
 * gabble_connection_get_properties
 *
 * Implements D-Bus method GetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_get_properties (GabbleConnection *self,
                                  const GArray *properties,
                                  GPtrArray **ret,
                                  GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_get_protocol
 *
 * Implements D-Bus method GetProtocol
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_get_protocol (GabbleConnection *self,
                                gchar **ret,
                                GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_get_self_handle
 *
 * Implements D-Bus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_get_self_handle (GabbleConnection *self,
                                   guint *ret,
                                   GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_get_status
 *
 * Implements D-Bus method GetStatus
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_get_status (GabbleConnection *self,
                              guint *ret,
                              GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_get_statuses
 *
 * Implements D-Bus method GetStatuses
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_get_statuses (GabbleConnection *self,
                                GHashTable **ret,
                                GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_hold_handles
 *
 * Implements D-Bus method HoldHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_hold_handles (GabbleConnection *self,
                                guint handle_type,
                                const GArray *handles,
                                DBusGMethodInvocation *context)
{
  return;
}


/**
 * gabble_connection_inspect_handles
 *
 * Implements D-Bus method InspectHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_inspect_handles (GabbleConnection *self,
                                   guint handle_type,
                                   const GArray *handles,
                                   DBusGMethodInvocation *context)
{
  return;
}


/**
 * gabble_connection_list_channels
 *
 * Implements D-Bus method ListChannels
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_list_channels (GabbleConnection *self,
                                 GPtrArray **ret,
                                 GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_list_properties
 *
 * Implements D-Bus method ListProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_list_properties (GabbleConnection *self,
                                   GPtrArray **ret,
                                   GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_release_handles
 *
 * Implements D-Bus method ReleaseHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_release_handles (GabbleConnection *self,
                                   guint handle_type,
                                   const GArray *handles,
                                   DBusGMethodInvocation *context)
{
  return;
}


/**
 * gabble_connection_remove_status
 *
 * Implements D-Bus method RemoveStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_remove_status (GabbleConnection *self,
                                 const gchar *status,
                                 GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_request_aliases
 *
 * Implements D-Bus method RequestAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_request_aliases (GabbleConnection *self,
                                   const GArray *contacts,
                                   DBusGMethodInvocation *context)
{
  return;
}


/**
 * gabble_connection_request_avatar
 *
 * Implements D-Bus method RequestAvatar
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_request_avatar (GabbleConnection *self,
                                  guint contact,
                                  DBusGMethodInvocation *context)
{
  return;
}


/**
 * gabble_connection_request_channel
 *
 * Implements D-Bus method RequestChannel
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_request_channel (GabbleConnection *self,
                                   const gchar *type,
                                   guint handle_type,
                                   guint handle,
                                   gboolean suppress_handler,
                                   DBusGMethodInvocation *context)
{
  return;
}


/**
 * gabble_connection_request_handles
 *
 * Implements D-Bus method RequestHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_request_handles (GabbleConnection *self,
                                   guint handle_type,
                                   const gchar **names,
                                   DBusGMethodInvocation *context)
{
  return;
}


/**
 * gabble_connection_request_presence
 *
 * Implements D-Bus method RequestPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_request_presence (GabbleConnection *self,
                                    const GArray *contacts,
                                    GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_set_aliases
 *
 * Implements D-Bus method SetAliases
 * on interface org.freedesktop.Telepathy.Connection.Interface.Aliasing
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_set_aliases (GabbleConnection *self,
                               GHashTable *aliases,
                               GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_set_avatar
 *
 * Implements D-Bus method SetAvatar
 * on interface org.freedesktop.Telepathy.Connection.Interface.Avatars
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_set_avatar (GabbleConnection *self,
                              const GArray *avatar,
                              const gchar *mime_type,
                              gchar **ret,
                              GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_set_last_activity_time
 *
 * Implements D-Bus method SetLastActivityTime
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_set_last_activity_time (GabbleConnection *self,
                                          guint time,
                                          GError **error)
{
  return TRUE;
}


/**
 * gabble_connection_set_properties
 *
 * Implements D-Bus method SetProperties
 * on interface org.freedesktop.Telepathy.Properties
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
void
gabble_connection_set_properties (GabbleConnection *self,
                                  const GPtrArray *properties,
                                  DBusGMethodInvocation *context)
{
  return;
}


/**
 * gabble_connection_set_status
 *
 * Implements D-Bus method SetStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_connection_set_status (GabbleConnection *self,
                              GHashTable *statuses,
                              GError **error)
{
  return TRUE;
}

