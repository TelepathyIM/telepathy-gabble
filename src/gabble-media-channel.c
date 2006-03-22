/*
 * gabble-media-channel.c - Source for GabbleMediaChannel
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"

#include "gabble-media-channel.h"
#include "gabble-media-channel-signals-marshal.h"

#include "gabble-media-channel-glue.h"

#include "gabble-media-session.h"

G_DEFINE_TYPE(GabbleMediaChannel, gabble_media_channel, G_TYPE_OBJECT)

#define TP_SESSION_HANDLER_SET_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      DBUS_TYPE_G_OBJECT_PATH, \
      G_TYPE_STRING, \
      G_TYPE_INVALID))

/* signal enum */
enum
{
    CLOSED,
    NEW_MEDIA_SESSION_HANDLER,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_CREATOR,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleMediaChannelPrivate GabbleMediaChannelPrivate;

struct _GabbleMediaChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;
  GabbleHandle creator;

  GabbleMediaSession *session;

  gboolean closed;
  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MEDIA_CHANNEL, GabbleMediaChannelPrivate))

static void
gabble_media_channel_init (GabbleMediaChannel *obj)
{
  /*GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);*/
}

static GObject *
gabble_media_channel_constructor (GType type, guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMediaChannelPrivate *priv;
  DBusGConnection *bus;
  GabbleHandleRepo *handles;
  gboolean valid;
  GabbleHandle self_handle;
  GError *error;
  GIntSet *empty, *set;

  obj = G_OBJECT_CLASS (gabble_media_channel_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (GABBLE_MEDIA_CHANNEL (obj));

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  /* initialize group mixin */
  valid = gabble_connection_get_self_handle (priv->conn, &self_handle, &error);
  g_assert (valid);

  handles = _gabble_connection_get_handles (priv->conn);

  gabble_group_mixin_init (obj, G_STRUCT_OFFSET (GabbleMediaChannel, group),
                           handles, self_handle);

  /* automatically add creator to channel */
  empty = g_intset_new ();
  set = g_intset_new ();
  g_intset_add (set, priv->creator);

  gabble_group_mixin_change_members (obj, "", set, empty, empty, empty);

  g_intset_destroy (empty);
  g_intset_destroy (set);

  /* allow member adding */
  gabble_group_mixin_change_flags (obj, TP_CHANNEL_GROUP_FLAG_CAN_ADD, 0);

  return obj;
}

static void session_state_changed_cb (GabbleMediaSession *session, GParamSpec *arg1, GabbleMediaChannel *channel);

/**
 * create_session
 *
 * Creates a GabbleMediaSession object for given peer.
 *
 * If sid is set to 0 a unique sid is generated and
 * the "initiator" property of the newly created
 * GabbleMediaSession is set to our own handle.
 */
static GabbleMediaSession *
create_session (GabbleMediaChannel *channel, GabbleHandle peer, guint32 sid)
{
  GabbleMediaChannelPrivate *priv;
  GabbleMediaSession *session;
  gchar *object_path;
  GabbleHandle initiator;

  g_debug ("%s called", G_STRFUNC);

  g_assert (GABBLE_IS_MEDIA_CHANNEL (channel));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (channel);

  g_assert (priv->session == NULL);

  object_path = g_strdup_printf ("%s/MediaSession%u", priv->object_path, peer);

  if (sid == 0)
    {
      GError *err;

      gabble_connection_get_self_handle (priv->conn, &initiator, &err);

      sid = _gabble_connection_jingle_session_allocate (priv->conn);
    }
  else
    {
      initiator = peer;
    }

  session = g_object_new (GABBLE_TYPE_MEDIA_SESSION,
                          "media-channel", channel,
                          "object-path", object_path,
                          "session-id", sid,
                          "initiator", initiator,
                          "peer", peer,
                          NULL);

  g_signal_connect (session, "notify::state",
                    (GCallback) session_state_changed_cb, channel);

  priv->session = session;

  _gabble_connection_jingle_session_register (priv->conn, sid, channel);

  g_signal_emit (channel, signals[NEW_MEDIA_SESSION_HANDLER], 0,
                 peer, object_path, "rtp");

  g_free (object_path);

  return session;
}

void
_gabble_media_channel_dispatch_session_action (GabbleMediaChannel *chan,
                                               GabbleHandle peer,
                                               guint32 sid,
                                               LmMessageNode *iq_node,
                                               LmMessageNode *session_node,
                                               const gchar *action)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  GabbleMediaSession *session = priv->session;

  if (session == NULL)
    {
      GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (chan);
      GIntSet *empty, *set;

      session = create_session (chan, peer, sid);

      /* make us local pending */
      empty = g_intset_new ();
      set = g_intset_new ();
      g_intset_add (set, mixin->self_handle);

      gabble_group_mixin_change_members (G_OBJECT (chan), "", empty, empty, set, empty);

      g_intset_destroy (empty);
      g_intset_destroy (set);

      /* and update flags accordingly */
      gabble_group_mixin_change_flags (G_OBJECT (chan),
                                       TP_CHANNEL_GROUP_FLAG_CAN_ADD ^ TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
                                       0);
    }

  g_object_ref (session);
  _gabble_media_session_handle_action (session, iq_node, session_node, action);
  g_object_unref (session);
}

static void
gabble_media_channel_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_string (value, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
      break;
    case PROP_CREATOR:
      g_value_set_uint (value, priv->creator);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_media_channel_set_property (GObject     *object,
                                   guint        property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_CREATOR:
      priv->creator = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_media_channel_dispose (GObject *object);
static void gabble_media_channel_finalize (GObject *object);
static gboolean gabble_media_channel_add_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);
static gboolean gabble_media_channel_remove_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error);

static void
gabble_media_channel_class_init (GabbleMediaChannelClass *gabble_media_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_media_channel_class, sizeof (GabbleMediaChannelPrivate));

  object_class->constructor = gabble_media_channel_constructor;

  object_class->get_property = gabble_media_channel_get_property;
  object_class->set_property = gabble_media_channel_set_property;

  object_class->dispose = gabble_media_channel_dispose;
  object_class->finalize = gabble_media_channel_finalize;

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

  param_spec = g_param_spec_string ("channel-type", "Telepathy channel type",
                                    "The D-Bus interface representing the "
                                    "type of this channel.",
                                    NULL,
                                    G_PARAM_READABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CHANNEL_TYPE, param_spec);

  param_spec = g_param_spec_uint ("creator", "Channel creator",
                                  "The GabbleHandle representing the contact "
                                  "who created the channel.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CREATOR, param_spec);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[NEW_MEDIA_SESSION_HANDLER] =
    g_signal_new ("new-media-session-handler",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__INT_STRING_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING);

  gabble_group_mixin_class_init (object_class,
                                 G_STRUCT_OFFSET (GabbleMediaChannelClass, group_class),
                                 gabble_media_channel_add_member,
                                 gabble_media_channel_remove_member);

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

  if (priv->session)
    g_object_unref (priv->session);

  if (!priv->closed)
    g_signal_emit (self, signals[CLOSED], 0);

  if (G_OBJECT_CLASS (gabble_media_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_channel_parent_class)->dispose (object);
}

void
gabble_media_channel_finalize (GObject *object)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  g_free (priv->object_path);

  gabble_group_mixin_finalize (object);

  G_OBJECT_CLASS (gabble_media_channel_parent_class)->finalize (object);
}



/**
 * gabble_media_channel_add_members
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
gboolean gabble_media_channel_add_members (GabbleMediaChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return gabble_group_mixin_add_members (G_OBJECT (obj), contacts, message, error);
}


/**
 * gabble_media_channel_close
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
gboolean gabble_media_channel_close (GabbleMediaChannel *obj, GError **error)
{
  GabbleMediaChannelPrivate *priv;

  g_debug ("%s called on %p", G_STRFUNC, obj);

  g_assert (GABBLE_IS_MEDIA_CHANNEL (obj));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);
  priv->closed = TRUE;

  if (priv->session)
    {
      _gabble_media_session_terminate (priv->session);
    }

  g_signal_emit(obj, signals[CLOSED], 0);

  return TRUE;
}


/**
 * gabble_media_channel_get_channel_type
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
gboolean gabble_media_channel_get_channel_type (GabbleMediaChannel *obj, gchar ** ret, GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);

  return TRUE;
}


/**
 * gabble_media_channel_get_group_flags
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
gboolean gabble_media_channel_get_group_flags (GabbleMediaChannel *obj, guint* ret, GError **error)
{
  return gabble_group_mixin_get_group_flags (G_OBJECT (obj), ret, error);
}


/**
 * gabble_media_channel_get_handle
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
gboolean gabble_media_channel_get_handle (GabbleMediaChannel *obj, guint* ret, guint* ret1, GError **error)
{
  GabbleMediaChannelPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (obj));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);

  *ret = 0;
  *ret1 = 0;

  return TRUE;
}


/**
 * gabble_media_channel_get_interfaces
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
gboolean gabble_media_channel_get_interfaces (GabbleMediaChannel *obj, gchar *** ret, GError **error)
{
  const gchar *interfaces[] = { TP_IFACE_CHANNEL_INTERFACE_GROUP, NULL };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_media_channel_get_local_pending_members
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
gboolean gabble_media_channel_get_local_pending_members (GabbleMediaChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_local_pending_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_media_channel_get_members
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
gboolean gabble_media_channel_get_members (GabbleMediaChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_media_channel_get_remote_pending_members
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
gboolean gabble_media_channel_get_remote_pending_members (GabbleMediaChannel *obj, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_remote_pending_members (G_OBJECT (obj), ret, error);
}


/**
 * gabble_media_channel_get_self_handle
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
gboolean gabble_media_channel_get_self_handle (GabbleMediaChannel *obj, guint* ret, GError **error)
{
  return gabble_group_mixin_get_self_handle (G_OBJECT (obj), ret, error);
}


/**
 * gabble_media_channel_get_session_handlers
 *
 * Implements DBus method GetSessionHandlers
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_channel_get_session_handlers (GabbleMediaChannel *obj, GPtrArray ** ret, GError **error)
{
  GabbleMediaChannelPrivate *priv;
  GValue handler = { 0, };
  GabbleHandle member;
  gchar *path;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (obj));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);

  g_value_init (&handler, TP_SESSION_HANDLER_SET_TYPE);
  g_value_set_static_boxed (&handler,
      dbus_g_type_specialized_construct (TP_SESSION_HANDLER_SET_TYPE));

  g_object_get (priv->session,
                "peer", &member,
                "object-path", &path,
                NULL);

  dbus_g_type_struct_set (&handler,
      0, member,
      1, path,
      2, "rtp",
      G_MAXUINT);

  g_free (path);

  *ret = g_ptr_array_sized_new (1);
  g_ptr_array_add (*ret, g_value_get_boxed (&handler));

  return TRUE;
}


/**
 * gabble_media_channel_remove_members
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
gboolean gabble_media_channel_remove_members (GabbleMediaChannel *obj, const GArray * contacts, const gchar * message, GError **error)
{
  return gabble_group_mixin_remove_members (G_OBJECT (obj), contacts, message, error);
}

static gboolean
gabble_media_channel_add_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (obj);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);

  if (!_gabble_connection_contact_supports_voice (priv->conn, handle))
    {
      g_debug ("%s: handle %u doesn't support voice", G_STRFUNC, handle);

      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            "handle %u doesn't support voice", handle);

      return FALSE;
    }

  /* did we create this channel? */
  if (priv->creator == mixin->self_handle &&
      handle != mixin->self_handle)
    {
      /* yes: invite the peer */

      GIntSet *empty, *set;

      /* create a new session */
      create_session (chan, handle, 0);

      /* make the peer remote pending */
      empty = g_intset_new ();
      set = g_intset_new ();
      g_intset_add (set, handle);

      gabble_group_mixin_change_members (obj, "", empty, empty, empty, set);

      g_intset_destroy (empty);
      g_intset_destroy (set);

      /* and update flags accordingly */
      gabble_group_mixin_change_flags (obj,
                                       TP_CHANNEL_GROUP_FLAG_CAN_REMOVE ^
                                       TP_CHANNEL_GROUP_FLAG_CAN_RESCIND,
                                       TP_CHANNEL_GROUP_FLAG_CAN_ADD);

      return TRUE;
    }
  else
    {
      /* no: has a session been created, is the handle being added ours,
       *     and are we in local pending? */

      if (priv->session &&
          handle == mixin->self_handle &&
          handle_set_is_member (mixin->local_pending, handle))
        {
          /* yes: accept the request */

          GIntSet *empty, *set;

          /* make us a member */
          empty = g_intset_new ();
          set = g_intset_new ();
          g_intset_add (set, handle);

          gabble_group_mixin_change_members (obj, "", set, empty, empty, empty);

          g_intset_destroy (empty);
          g_intset_destroy (set);

          /* update flags */
          gabble_group_mixin_change_flags (obj, 0, TP_CHANNEL_GROUP_FLAG_CAN_ADD);

          /* signal acceptance */
          _gabble_media_session_accept (priv->session);

          return TRUE;
        }
    }

  *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                        "handle %u cannot be added in the current state", handle);
  return FALSE;
}

static gboolean
gabble_media_channel_remove_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (obj);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  GIntSet *empty, *set;

  if (priv->session == NULL)
    {
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            "handle %u cannot be removed in the current state",
                            handle);

      return FALSE;
    }

  if (priv->creator != mixin->self_handle &&
      handle != mixin->self_handle)
    {
      *error = g_error_new (TELEPATHY_ERRORS, PermissionDenied,
                            "handle %u cannot be removed because you are "
                            "not the creator of the channel",
                            handle);

      return FALSE;
    }

  _gabble_media_session_terminate (priv->session);

  /* remove the member */
  empty = g_intset_new ();
  set = g_intset_new ();
  g_intset_add (set, handle);

  gabble_group_mixin_change_members (obj, "", empty, set, empty, empty);

  g_intset_destroy (empty);
  g_intset_destroy (set);

  /* and update flags accordingly */
  gabble_group_mixin_change_flags (obj, TP_CHANNEL_GROUP_FLAG_CAN_ADD,
                                   TP_CHANNEL_GROUP_FLAG_CAN_REMOVE ^
                                   TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);

  return TRUE;
}

static void
session_state_changed_cb (GabbleMediaSession *session,
                          GParamSpec *arg1,
                          GabbleMediaChannel *channel)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (channel);
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (channel);
  JingleSessionState state;
  GabbleHandle peer;
  GIntSet *empty, *set;

  g_object_get (session,
                "state", &state,
                "peer", &peer,
                NULL);

  empty = g_intset_new ();
  set = g_intset_new ();

  if (state == JS_STATE_ACTIVE)
    {
      if (priv->creator == mixin->self_handle)
        {
          /* add the peer to the member list */
          g_intset_add (set, peer);

          gabble_group_mixin_change_members (G_OBJECT (channel), "", set, empty, empty, empty);

          /* update flags accordingly -- allow removal, deny adding and rescinding */
          gabble_group_mixin_change_flags (G_OBJECT (channel), TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
                                           TP_CHANNEL_GROUP_FLAG_CAN_ADD ^ TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);
        }
    }
  else if (state == JS_STATE_ENDED)
    {
      GError *error;

      /* remove us and the peer from the member list */
      g_intset_add (set, mixin->self_handle);
      g_intset_add (set, peer);

      gabble_group_mixin_change_members (G_OBJECT (channel), "", empty, set, empty, empty);

      /* update flags accordingly -- allow adding, deny removal */
      gabble_group_mixin_change_flags (G_OBJECT (channel), TP_CHANNEL_GROUP_FLAG_CAN_ADD,
                                       TP_CHANNEL_GROUP_FLAG_CAN_REMOVE);

      /* remove the session */
      g_object_unref (priv->session);
      priv->session = NULL;

      /* close the channel */
      if (!gabble_media_channel_close (channel, &error))
        {
          g_warning ("%s: failed to close media channel: %s", G_STRFUNC, error->message);
        }
    }

    g_intset_destroy (empty);
    g_intset_destroy (set);

}

