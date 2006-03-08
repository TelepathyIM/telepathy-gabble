/*
 * gabble-muc-channel.c - Source for GabbleMucChannel
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#include "telepathy-interfaces.h"
#include "telepathy-helpers.h"

#include "gabble-muc-channel.h"
#include "gabble-muc-channel-signals-marshal.h"

#include "gabble-muc-channel-glue.h"

G_DEFINE_TYPE(GabbleMucChannel, gabble_muc_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    PASSWORD_FLAGS_CHANGED,
    RECEIVED,
    SENT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_HANDLE,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleMucChannelPrivate GabbleMucChannelPrivate;

struct _GabbleMucChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;
  GabbleHandle handle;

  gboolean closed;
  gboolean dispose_has_run;
};

#define GABBLE_MUC_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MUC_CHANNEL, GabbleMucChannelPrivate))

static void
gabble_muc_channel_init (GabbleMucChannel *obj)
{
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  g_assert (priv);

  /* allocate any data required by the object here */
}

static GObject *
gabble_muc_channel_constructor (GType type, guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMucChannelPrivate *priv;
  DBusGConnection *bus;
  GabbleHandleRepo *handles;
  gboolean valid;
  GabbleHandle self_handle;
  GError *error;
  /*GIntSet *empty, *set;*/

  obj = G_OBJECT_CLASS (gabble_muc_channel_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (GABBLE_MUC_CHANNEL (obj));

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  /* initialize group mixin */
  valid = gabble_connection_get_self_handle (priv->conn, &self_handle, &error);
  g_assert (valid);

  handles = _gabble_connection_get_handles (priv->conn);

  gabble_group_mixin_init (obj, G_STRUCT_OFFSET (GabbleMucChannel, group),
                           handles, self_handle);

  /* automatically add ourself to remote pending */
  /*
  empty = g_intset_new ();
  set = g_intset_new ();
  g_intset_add (set, priv->creator);

  gabble_group_mixin_change_members (obj, "", set, empty, empty, empty);

  g_intset_destroy (empty);
  g_intset_destroy (set);
  */

  /* allow member adding */
  /*gabble_group_mixin_change_flags (obj, TP_CHANNEL_GROUP_FLAG_CAN_ADD, 0);*/

  return obj;
}

static void
gabble_muc_channel_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_muc_channel_set_property (GObject     *object,
                                 guint        property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GabbleMucChannel *chan = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      if (priv->object_path)
        g_free (priv->object_path);

      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_muc_channel_dispose (GObject *object);
static void gabble_muc_channel_finalize (GObject *object);
static gboolean gabble_muc_channel_add_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);
static gboolean gabble_muc_channel_remove_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);

static void
gabble_muc_channel_class_init (GabbleMucChannelClass *gabble_muc_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_muc_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_muc_channel_class, sizeof (GabbleMucChannelPrivate));

  object_class->constructor = gabble_muc_channel_constructor;

  object_class->get_property = gabble_muc_channel_get_property;
  object_class->set_property = gabble_muc_channel_set_property;

  object_class->dispose = gabble_muc_channel_dispose;
  object_class->finalize = gabble_muc_channel_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "IM channel object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_uint ("handle", "Room handle",
                                  "The GabbleHandle representing the room "
                                  "with whom this channel communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_HANDLE, param_spec);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[PASSWORD_FLAGS_CHANGED] =
    g_signal_new ("password-flags-changed",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[RECEIVED] =
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__INT_INT_INT_INT_STRING,
                  G_TYPE_NONE, 5, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SENT] =
    g_signal_new ("sent",
                  G_OBJECT_CLASS_TYPE (gabble_muc_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_muc_channel_marshal_VOID__INT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  gabble_group_mixin_class_init (object_class,
                                 G_STRUCT_OFFSET (GabbleMucChannelClass, group_class),
                                 gabble_muc_channel_add_member,
                                 gabble_muc_channel_remove_member);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_muc_channel_class), &dbus_glib_gabble_muc_channel_object_info);
}

void
gabble_muc_channel_dispose (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_muc_channel_parent_class)->dispose (object);
}

void
gabble_muc_channel_finalize (GObject *object)
{
  GabbleMucChannel *self = GABBLE_MUC_CHANNEL (object);
  GabbleMucChannelPrivate *priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (self);

  g_assert (priv);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gabble_muc_channel_parent_class)->finalize (object);
}



/**
 * gabble_muc_channel_acknowledge_pending_message
 *
 * Implements DBus method AcknowledgePendingMessage
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_acknowledge_pending_message (GabbleMucChannel *obj, guint id, GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_add_members
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
gboolean gabble_muc_channel_add_members (GabbleMucChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return gabble_group_mixin_add_members (G_OBJECT (obj), contacts, message, error);
}


/**
 * gabble_muc_channel_close
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
gboolean gabble_muc_channel_close (GabbleMucChannel *obj, GError **error)
{
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  priv->closed = TRUE;

  g_debug ("%s called on %p", G_STRFUNC, obj);
  g_signal_emit(obj, signals[CLOSED], 0);

  return TRUE;
}


/**
 * gabble_muc_channel_get_channel_type
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
gboolean gabble_muc_channel_get_channel_type (GabbleMucChannel *obj, gchar ** ret, GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_TEXT);

  return TRUE;
}


/**
 * gabble_muc_channel_get_group_flags
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
gboolean gabble_muc_channel_get_group_flags (GabbleMucChannel *obj, guint* ret, GError **error)
{
  return gabble_group_mixin_get_group_flags (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_get_handle
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
gboolean gabble_muc_channel_get_handle (GabbleMucChannel *obj, guint* ret, guint* ret1, GError **error)
{
  GabbleMucChannelPrivate *priv;

  g_assert (GABBLE_IS_MUC_CHANNEL (obj));

  priv = GABBLE_MUC_CHANNEL_GET_PRIVATE (obj);

  *ret = TP_HANDLE_TYPE_ROOM;
  *ret1 = priv->handle;

  return TRUE;
}


/**
 * gabble_muc_channel_get_interfaces
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
gboolean gabble_muc_channel_get_interfaces (GabbleMucChannel *obj, gchar *** ret, GError **error)
{
  const gchar *interfaces[] = {
      TP_IFACE_CHANNEL_INTERFACE_GROUP,
      TP_IFACE_CHANNEL_INTERFACE_PASSWORD,
      NULL
  };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_muc_channel_get_local_pending_members
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
gboolean gabble_muc_channel_get_local_pending_members (GabbleMucChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_local_pending_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_get_members
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
gboolean gabble_muc_channel_get_members (GabbleMucChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_get_password
 *
 * Implements DBus method GetPassword
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_password (GabbleMucChannel *obj, gchar ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_password_flags
 *
 * Implements DBus method GetPasswordFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_get_password_flags (GabbleMucChannel *obj, guint* ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_get_remote_pending_members
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
gboolean gabble_muc_channel_get_remote_pending_members (GabbleMucChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_remote_pending_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_get_self_handle
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
gboolean gabble_muc_channel_get_self_handle (GabbleMucChannel *obj, guint* ret, GError **error)
{
  return gabble_group_mixin_get_self_handle (G_OBJECT (obj), ret, error);
}


/**
 * gabble_muc_channel_list_pending_messages
 *
 * Implements DBus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_list_pending_messages (GabbleMucChannel *obj, GPtrArray ** ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_provide_password
 *
 * Implements DBus method ProvidePassword
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_provide_password (GabbleMucChannel *obj, const gchar * password, gboolean* ret, GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_remove_members
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
gboolean gabble_muc_channel_remove_members (GabbleMucChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return gabble_group_mixin_remove_members (G_OBJECT (obj), contacts, message, error);
}


/**
 * gabble_muc_channel_send
 *
 * Implements DBus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_send (GabbleMucChannel *obj, guint type, const gchar * text, GError **error)
{
  return TRUE;
}


/**
 * gabble_muc_channel_set_password
 *
 * Implements DBus method SetPassword
 * on interface org.freedesktop.Telepathy.Channel.Interface.Password
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_muc_channel_set_password (GabbleMucChannel *obj, const gchar * password, GError **error)
{
  return TRUE;
}


static gboolean
gabble_muc_channel_add_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error)
{
  return TRUE;
}

static gboolean
gabble_muc_channel_remove_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error)
{
  return TRUE;
}

