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

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "ansi.h"
#include "debug.h"
#include "gabble-connection.h"
#include "gabble-media-session.h"
#include "gabble-presence.h"
#include "gabble-presence-cache.h"

#include "telepathy-errors.h"
#include "telepathy-helpers.h"
#include "telepathy-interfaces.h"
#include "tp-channel-iface.h"

#include "gabble-media-channel.h"
#include "gabble-media-channel-signals-marshal.h"
#include "gabble-media-channel-glue.h"

#include "gabble-media-session.h"
#include "gabble-media-stream.h"

#include "media-factory.h"

#define TP_SESSION_HANDLER_SET_TYPE (dbus_g_type_get_struct ("GValueArray", \
      DBUS_TYPE_G_OBJECT_PATH, \
      G_TYPE_STRING, \
      G_TYPE_INVALID))

#define TP_CHANNEL_STREAM_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_INVALID))

G_DEFINE_TYPE_WITH_CODE (GabbleMediaChannel, gabble_media_channel,
    G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

/* signal enum */
enum
{
    CLOSED,
    NEW_SESSION_HANDLER,
    STREAM_ADDED,
    STREAM_DIRECTION_CHANGED,
    STREAM_ERROR,
    STREAM_REMOVED,
    STREAM_STATE_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  PROP_CREATOR,
  PROP_FACTORY,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleMediaChannelPrivate GabbleMediaChannelPrivate;

struct _GabbleMediaChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;
  GabbleHandle creator;

  GabbleMediaFactory *factory;
  GabbleMediaSession *session;
  GPtrArray *streams;

  guint next_stream_id;

  gboolean closed;
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

  priv->next_stream_id = 1;
}

static GObject *
gabble_media_channel_constructor (GType type, guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMediaChannelPrivate *priv;
  DBusGConnection *bus;
  GIntSet *empty, *set;

  obj = G_OBJECT_CLASS (gabble_media_channel_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (GABBLE_MEDIA_CHANNEL (obj));

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  gabble_group_mixin_init (obj, G_STRUCT_OFFSET (GabbleMediaChannel, group),
                           priv->conn->handles, priv->conn->self_handle);

  /* automatically add creator to channel */
  empty = g_intset_new ();
  set = g_intset_new ();
  g_intset_add (set, priv->creator);

  gabble_group_mixin_change_members (obj, "", set, empty, empty, empty, 0, 0);

  g_intset_destroy (empty);
  g_intset_destroy (set);

  /* allow member adding */
  gabble_group_mixin_change_flags (obj, TP_CHANNEL_GROUP_FLAG_CAN_ADD, 0);

  return obj;
}

static void session_state_changed_cb (GabbleMediaSession *session, GParamSpec *arg1, GabbleMediaChannel *channel);
static void session_stream_added_cb (GabbleMediaSession *session, GabbleMediaStream  *stream, GabbleMediaChannel *chan);
static void session_terminated_cb (GabbleMediaSession *session, guint terminator, guint reason, gpointer user_data);

/**
 * create_session
 *
 * Creates a GabbleMediaSession object for given peer.
 *
 * If sid is set to NULL a unique sid is generated and
 * the "initiator" property of the newly created
 * GabbleMediaSession is set to our own handle.
 */
static GabbleMediaSession*
create_session (GabbleMediaChannel *channel, GabbleHandle peer, const gchar *peer_resource, const gchar *sid)
{
  GabbleMediaChannelPrivate *priv;
  GabbleMediaSession *session;
  gchar *object_path;
  JingleInitiator initiator;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (channel));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (channel);

  g_assert (priv->session == NULL);

  object_path = g_strdup_printf ("%s/MediaSession%u", priv->object_path, peer);

  if (sid == NULL)
    {
      initiator = INITIATOR_LOCAL;
      sid = _gabble_media_factory_allocate_sid (priv->factory, channel);
    }
  else
    {
      initiator = INITIATOR_REMOTE;
      _gabble_media_factory_register_sid (priv->factory, sid, channel);
    }

  session = g_object_new (GABBLE_TYPE_MEDIA_SESSION,
                          "connection", priv->conn,
                          "media-channel", channel,
                          "object-path", object_path,
                          "session-id", sid,
                          "initiator", initiator,
                          "peer", peer,
                          "peer-resource", peer_resource,
                          NULL);

  g_signal_connect (session, "notify::state",
                    (GCallback) session_state_changed_cb, channel);
  g_signal_connect (session, "stream-added",
                    (GCallback) session_stream_added_cb, channel);
  g_signal_connect (session, "terminated",
                    (GCallback) session_terminated_cb, channel);

  priv->session = session;

  priv->streams = g_ptr_array_sized_new (1);

  g_signal_emit (channel, signals[NEW_SESSION_HANDLER], 0,
                 object_path, "rtp");

  g_free (object_path);

  return session;
}

void
_gabble_media_channel_dispatch_session_action (GabbleMediaChannel *chan,
                                               GabbleHandle peer,
                                               const gchar *peer_resource,
                                               const gchar *sid,
                                               LmMessage *message,
                                               LmMessageNode *session_node,
                                               const gchar *action)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  GabbleMediaSession *session = priv->session;

  if (session == NULL)
    {
      GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (chan);
      GIntSet *empty, *set;

      session = create_session (chan, peer, peer_resource, sid);

      /* make us local pending */
      empty = g_intset_new ();
      set = g_intset_new ();
      g_intset_add (set, mixin->self_handle);

      gabble_group_mixin_change_members (G_OBJECT (chan), "", empty, empty,
          set, empty, 0, 0);

      g_intset_destroy (empty);
      g_intset_destroy (set);

      /* and update flags accordingly */
      gabble_group_mixin_change_flags (G_OBJECT (chan),
                                       TP_CHANNEL_GROUP_FLAG_CAN_ADD |
                                       TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
                                       0);
    }

  g_object_ref (session);
  _gabble_media_session_handle_action (session, message, session_node, action);
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
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_NONE);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, 0);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_CREATOR:
      g_value_set_uint (value, priv->creator);
      break;
    case PROP_FACTORY:
      g_value_set_object (value, priv->factory);
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
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      /* this property is writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      break;
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_CREATOR:
      priv->creator = g_value_get_uint (value);
      break;
    case PROP_FACTORY:
      priv->factory = g_value_get_object (value);
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

  gabble_group_mixin_class_init (object_class,
                                 G_STRUCT_OFFSET (GabbleMediaChannelClass, group_class),
                                 gabble_media_channel_add_member,
                                 gabble_media_channel_remove_member);

  g_object_class_override_property (object_class, PROP_OBJECT_PATH, "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE, "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "media channel object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_uint ("creator", "Channel creator",
                                  "The GabbleHandle representing the contact "
                                  "who created the channel.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CREATOR, param_spec);

  param_spec = g_param_spec_object ("factory", "GabbleMediaFactory object",
                                    "The factory that created this object.",
                                    GABBLE_TYPE_MEDIA_FACTORY,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_FACTORY, param_spec);

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

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

  signals[STREAM_ERROR] =
    g_signal_new ("stream-error",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__UINT_UINT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

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

  /** In this we set the state to ENDED, then the callback unrefs
   * the session
   */

  if (!priv->closed)
    gabble_media_channel_close (self, NULL);

  g_assert (priv->closed);
  g_assert (priv->session == NULL);
  g_assert (priv->streams == NULL);

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
  return gabble_group_mixin_add_members (G_OBJECT (self), contacts, message,
      error);
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
  GabbleMediaChannelPrivate *priv;

  DEBUG ("called on %p", self);

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (priv->closed)
    return TRUE;

  priv->closed = TRUE;

  if (priv->session)
    {
      _gabble_media_session_terminate (priv->session, INITIATOR_LOCAL, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
    }

  g_signal_emit (self, signals[CLOSED], 0);

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
  return gabble_group_mixin_get_all_members (G_OBJECT (self), ret, ret1, ret2,
      error);
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
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);

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
  return gabble_group_mixin_get_group_flags (G_OBJECT (self), ret, error);
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
  GabbleMediaChannelPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  *ret = 0;
  *ret1 = 0;

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
  return gabble_group_mixin_get_handle_owners (G_OBJECT (self), handles, ret,
      error);
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
  const gchar *interfaces[] = {
      TP_IFACE_CHANNEL_INTERFACE_GROUP,
      TP_IFACE_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
      NULL
  };

  *ret = g_strdupv ((gchar **) interfaces);

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
  return gabble_group_mixin_get_local_pending_members (G_OBJECT (self), ret,
      error);
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
  return gabble_group_mixin_get_members (G_OBJECT (self), ret, error);
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
  return gabble_group_mixin_get_remote_pending_members (G_OBJECT (self), ret,
      error);
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
  return gabble_group_mixin_get_self_handle (G_OBJECT (self), ret, error);
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
  GabbleMediaChannelPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (priv->session)
    {
      GValue handler = { 0, };
      GabbleHandle member;
      gchar *path;

      g_value_init (&handler, TP_SESSION_HANDLER_SET_TYPE);
      g_value_take_boxed (&handler,
          dbus_g_type_specialized_construct (TP_SESSION_HANDLER_SET_TYPE));

      g_object_get (priv->session,
                    "peer", &member,
                    "object-path", &path,
                    NULL);

      dbus_g_type_struct_set (&handler,
          0, path,
          1, "rtp",
          G_MAXUINT);

      g_free (path);

      *ret = g_ptr_array_sized_new (1);
      g_ptr_array_add (*ret, g_value_get_boxed (&handler));
    }
  else
    {
      *ret = g_ptr_array_sized_new (0);
    }

  return TRUE;
}


GPtrArray *
make_stream_list (GabbleMediaChannel *self,
                  GPtrArray *streams)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);
  GPtrArray *ret;
  guint i;

  ret = g_ptr_array_sized_new (streams->len);

  for (i = 0; i < streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (streams, i);
      GValue entry = { 0, };
      guint id;
      GabbleHandle peer;
      TpCodecMediaType type;
      TpMediaStreamState connection_state;
      CombinedStreamDirection combined_direction;

      g_object_get (stream,
          "id", &id,
          "media-type", &type,
          "connection-state", &connection_state,
          "combined-direction", &combined_direction,
          NULL);

      g_object_get (priv->session, "peer", &peer, NULL);

      g_value_init (&entry, TP_CHANNEL_STREAM_TYPE);
      g_value_take_boxed (&entry,
          dbus_g_type_specialized_construct (TP_CHANNEL_STREAM_TYPE));

      dbus_g_type_struct_set (&entry,
          0, id,
          1, peer,
          2, type,
          3, connection_state,
          4, COMBINED_DIRECTION_GET_DIRECTION (combined_direction),
          5, COMBINED_DIRECTION_GET_PENDING_SEND (combined_direction),
          G_MAXUINT);

      g_ptr_array_add (ret, g_value_get_boxed (&entry));
    }

  return ret;
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
  GabbleMediaChannelPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  /* no session yet? return an empty array */
  if (priv->session == NULL)
    {
      *ret = g_ptr_array_new ();

      return TRUE;
    }

  *ret = make_stream_list (self, priv->streams);

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
  return gabble_group_mixin_remove_members (G_OBJECT (self), contacts, message,
      error);
}


static GabbleMediaStream *
_find_stream_by_id (GabbleMediaChannel *chan, guint stream_id)
{
  GabbleMediaChannelPrivate *priv;
  guint i;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (chan));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);

  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);
      guint id;

      g_object_get (stream, "id", &id, NULL);
      if (id == stream_id)
        return stream;
    }

  return NULL;
}

/**
 * gabble_media_channel_remove_streams
 *
 * Implements DBus method RemoveStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_channel_remove_streams (GabbleMediaChannel *obj, const GArray * streams, GError **error)
{
  GabbleMediaChannelPrivate *priv;
  GPtrArray *stream_objs;
  guint i;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (obj));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);

  *error = NULL;

  stream_objs = g_ptr_array_sized_new (streams->len);

  /* check that all stream ids are valid and at the same time build an array
   * of stream objects so we don't have to look them up again after verifying
   * all stream identifiers. */
  for (i = 0; i < streams->len; i++)
    {
      guint id = g_array_index (streams, guint, i);
      GabbleMediaStream *stream;
      guint j;

      stream = _find_stream_by_id (obj, id);
      if (stream == NULL)
        {
          g_set_error (error, TELEPATHY_ERRORS, InvalidArgument,
              "given stream id %u does not exist", id);
          goto OUT;
        }

      /* make sure we don't allow the client to repeatedly remove the same stream */
      for (j = 0; j < stream_objs->len; j++)
        {
          GabbleMediaStream *tmp = g_ptr_array_index (stream_objs, j);

          if (tmp == stream)
            {
              stream = NULL;
              break;
            }
        }

      if (stream != NULL)
        g_ptr_array_add (stream_objs, stream);
    }

  /* groovy, it's all good dude, let's remove them */
  if (stream_objs->len > 0)
    _gabble_media_session_remove_streams (priv->session, (GabbleMediaStream **)
        stream_objs->pdata, stream_objs->len);

OUT:
  g_ptr_array_free (stream_objs, TRUE);

  return (*error == NULL);
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
  GabbleMediaChannelPrivate *priv;
  GabbleMediaStream *stream;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (stream_direction > TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL)
    {
      g_set_error (error, TELEPATHY_ERRORS, InvalidArgument,
          "given stream direction %u is not valid", stream_direction);
      return FALSE;
    }

  stream = _find_stream_by_id (self, stream_id);
  if (stream == NULL)
    {
      g_set_error (error, TELEPATHY_ERRORS, InvalidArgument,
          "given stream id %u does not exist", stream_id);
      return FALSE;
    }

  /* streams with no session? I think not... */
  g_assert (priv->session != NULL);

  return _gabble_media_session_request_stream_direction (priv->session, stream,
      stream_direction, error);
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
                                      GPtrArray **ret,
                                      GError **error)
{
  GabbleMediaChannelPrivate *priv;
  GPtrArray *streams;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (!gabble_handle_is_valid (priv->conn->handles, TP_HANDLE_TYPE_CONTACT,
        contact_handle, error))
    return FALSE;

  if (!handle_set_is_member (self->group.members, contact_handle) &&
      !handle_set_is_member (self->group.remote_pending, contact_handle))
    {
      g_set_error (error, TELEPATHY_ERRORS, InvalidArgument,
          "given handle %u is not a member of the channel", contact_handle);
      return FALSE;
    }

  /* if the person is a channel member, we should have a session */
  g_assert (priv->session != NULL);

  if (!_gabble_media_session_request_streams (priv->session, types, &streams,
        error))
    return FALSE;

  *ret = make_stream_list (self, streams);

  g_ptr_array_free (streams, TRUE);

  return TRUE;
}


static gboolean
gabble_media_channel_add_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (obj);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);

  /* did we create this channel? */
  if (priv->creator == mixin->self_handle &&
      handle != mixin->self_handle)
    {
      GabblePresence *presence;

      /* yes: check the peer's capabilities */

      presence = gabble_presence_cache_get (priv->conn->presence_cache, handle);

      if (presence == NULL ||
          !(presence->caps & PRESENCE_CAP_GOOGLE_VOICE ||
            presence->caps & PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO ||
            presence->caps & PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO))
        {
          if (presence == NULL)
            DEBUG ("failed to add contact %d (%s) to media channel: "
                   "no presence available", handle,
                   gabble_handle_inspect (priv->conn->handles,
                     TP_HANDLE_TYPE_CONTACT, handle));
          else
            DEBUG ("failed to add contact %d (%s) to media channel: "
                   "caps %x aren't sufficient", handle,
                   gabble_handle_inspect (priv->conn->handles,
                     TP_HANDLE_TYPE_CONTACT, handle),
                   presence->caps);

          g_set_error (error, TELEPATHY_ERRORS, NotAvailable,
              "handle %u has no media capabilities", handle);
          return FALSE;
        }

      /* yes: invite the peer */

      GIntSet *empty, *set;

      /* create a new session */
      create_session (chan, handle, NULL, NULL);

      /* make the peer remote pending */
      empty = g_intset_new ();
      set = g_intset_new ();
      g_intset_add (set, handle);

      gabble_group_mixin_change_members (obj, "", empty, empty, empty, set, 0, 0);

      g_intset_destroy (empty);
      g_intset_destroy (set);

      /* and update flags accordingly */
      gabble_group_mixin_change_flags (obj,
                                       TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
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

          gabble_group_mixin_change_members (obj, "", set, empty, empty, empty, 0, 0);

          g_intset_destroy (empty);
          g_intset_destroy (set);

          /* update flags */
          gabble_group_mixin_change_flags (obj, 0, TP_CHANNEL_GROUP_FLAG_CAN_ADD);

          /* signal acceptance */
          _gabble_media_session_accept (priv->session);

          return TRUE;
        }
    }

  g_set_error (error, TELEPATHY_ERRORS, NotAvailable,
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
      g_set_error (error, TELEPATHY_ERRORS, NotAvailable,
          "handle %u cannot be removed in the current state", handle);

      return FALSE;
    }

  if (priv->creator != mixin->self_handle &&
      handle != mixin->self_handle)
    {
      g_set_error (error, TELEPATHY_ERRORS, PermissionDenied,
          "handle %u cannot be removed because you are not the creator of the"
          " channel", handle);

      return FALSE;
    }

  _gabble_media_session_terminate (priv->session, INITIATOR_LOCAL, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  /* remove the member */
  empty = g_intset_new ();
  set = g_intset_new ();
  g_intset_add (set, handle);

  gabble_group_mixin_change_members (obj, "", empty, set, empty, empty, 0, 0);

  g_intset_destroy (empty);
  g_intset_destroy (set);

  /* and update flags accordingly */
  gabble_group_mixin_change_flags (obj, TP_CHANNEL_GROUP_FLAG_CAN_ADD,
                                   TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
                                   TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);

  return TRUE;
}


static void
session_terminated_cb (GabbleMediaSession *session,
                       guint terminator,
                       guint reason,
                       gpointer user_data)
{
  GabbleMediaChannel *channel = (GabbleMediaChannel *) user_data;
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (channel);
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (channel);
  GError *error;
  gchar *sid;
  JingleSessionState state;
  GabbleHandle peer;
  GIntSet *empty, *set;

  g_object_get (session,
                "state", &state,
                "peer", &peer,
                NULL);

  empty = g_intset_new ();
  set = g_intset_new ();

  /* remove us and the peer from the member list */
  g_intset_add (set, mixin->self_handle);
  g_intset_add (set, peer);

  gabble_group_mixin_change_members (G_OBJECT (channel), "", empty, set, empty, empty, terminator, reason);

  /* update flags accordingly -- allow adding, deny removal */
  gabble_group_mixin_change_flags (G_OBJECT (channel), TP_CHANNEL_GROUP_FLAG_CAN_ADD,
                                   TP_CHANNEL_GROUP_FLAG_CAN_REMOVE);

  /* free the session ID */
  g_object_get (priv->session, "session-id", &sid, NULL);
  _gabble_media_factory_free_sid (priv->factory, sid);
  g_free (sid);

  /* unref streams */
  if (priv->streams != NULL)
    {
      GPtrArray *tmp = priv->streams;

      /* move priv->streams aside so that the stream_close_cb
       * doesn't double unref */
      priv->streams = NULL;
      g_ptr_array_foreach (tmp, (GFunc) g_object_unref, NULL);
      g_ptr_array_free (tmp, TRUE);
    }

  /* remove the session */
  g_object_unref (priv->session);
  priv->session = NULL;

  /* close the channel */
  if (!gabble_media_channel_close (channel, &error))
    {
      g_warning ("%s: failed to close media channel: %s", G_STRFUNC,
          error->message);
    }
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

  if (state != JS_STATE_ACTIVE)
    return;

  if (priv->creator != mixin->self_handle)
    return;

  empty = g_intset_new ();
  set = g_intset_new ();

  /* add the peer to the member list */
  g_intset_add (set, peer);

  gabble_group_mixin_change_members (G_OBJECT (channel), "", set, empty, empty, empty, 0, 0);

  /* update flags accordingly -- allow removal, deny adding and rescinding */
  gabble_group_mixin_change_flags (G_OBJECT (channel),
      TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
      TP_CHANNEL_GROUP_FLAG_CAN_ADD |
      TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);

  g_intset_destroy (empty);
  g_intset_destroy (set);
}

static void
stream_close_cb (GabbleMediaStream *stream,
                 GabbleMediaChannel *chan)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  guint id;

  g_object_get (stream, "id", &id, NULL);

  g_signal_emit (chan, signals[STREAM_REMOVED], 0, id);

  if (priv->streams != NULL)
    {
      g_ptr_array_remove (priv->streams, stream);
      g_object_unref (stream);
    }
}

static void
stream_error_cb (GabbleMediaStream *stream,
                 TpMediaStreamError errno,
                 const gchar *message,
                 GabbleMediaChannel *chan)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  guint id;

  /* emit signal */
  g_object_get (stream, "id", &id, NULL);
  g_signal_emit (chan, signals[STREAM_ERROR], 0, id, errno, message);

  /* remove stream from session */
  _gabble_media_session_remove_streams (priv->session, &stream, 1);
}

static void
stream_state_changed_cb (GabbleMediaStream *stream,
                         GParamSpec *pspec,
                         GabbleMediaChannel *chan)
{
  guint id;
  TpMediaStreamState connection_state;

  g_object_get (stream, "id", &id, "connection-state", &connection_state, NULL);

  g_signal_emit (chan, signals[STREAM_STATE_CHANGED], 0, id, connection_state);
}

static void
stream_direction_changed_cb (GabbleMediaStream *stream,
                             GParamSpec *pspec,
                             GabbleMediaChannel *chan)
{
  guint id;
  CombinedStreamDirection combined;
  TpMediaStreamDirection direction;
  TpMediaStreamPendingSend pending_send;

  g_object_get (stream,
      "id", &id,
      "combined-direction", &combined,
      NULL);

  direction = COMBINED_DIRECTION_GET_DIRECTION (combined);
  pending_send = COMBINED_DIRECTION_GET_PENDING_SEND (combined);

  g_signal_emit (chan, signals[STREAM_DIRECTION_CHANGED], 0, id, direction,
      pending_send);
}

static void
session_stream_added_cb (GabbleMediaSession *session,
                         GabbleMediaStream  *stream,
                         GabbleMediaChannel *chan)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);

  guint id, handle, type;

  /* keep track of the stream */
  g_object_ref (stream);
  g_ptr_array_add (priv->streams, stream);

  g_signal_connect (stream, "close",
                    (GCallback) stream_close_cb, chan);
  g_signal_connect (stream, "error",
                    (GCallback) stream_error_cb, chan);
  g_signal_connect (stream, "notify::connection-state",
                    (GCallback) stream_state_changed_cb, chan);
  g_signal_connect (stream, "notify::combined-direction",
                    (GCallback) stream_direction_changed_cb, chan);

  /* emit StreamAdded */
  g_object_get (session, "peer", &handle, NULL);
  g_object_get (stream, "id", &id, "media-type", &type, NULL);

  g_signal_emit (chan, signals[STREAM_ADDED], 0, id, handle, type);
}

guint
_gabble_media_channel_get_stream_id (GabbleMediaChannel *chan)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);

  return priv->next_stream_id++;
}

#define AUDIO_CAPS \
  ( PRESENCE_CAP_GOOGLE_VOICE | PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO )

#define VIDEO_CAPS \
  ( PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO )

GabblePresenceCapabilities
_gabble_media_channel_typeflags_to_caps (TpChannelMediaCapabilities flags)
{
  GabblePresenceCapabilities caps = 0;

  if (flags & TP_CHANNEL_MEDIA_CAPABILITY_AUDIO)
    caps |= AUDIO_CAPS;

  if (flags & TP_CHANNEL_MEDIA_CAPABILITY_VIDEO)
    caps |= VIDEO_CAPS;

  return caps;
}

TpChannelMediaCapabilities
_gabble_media_channel_caps_to_typeflags (GabblePresenceCapabilities caps)
{
  TpChannelMediaCapabilities typeflags = 0;

  if (caps & AUDIO_CAPS)
    typeflags |= TP_CHANNEL_MEDIA_CAPABILITY_AUDIO;

  if (caps & VIDEO_CAPS)
    typeflags |= TP_CHANNEL_MEDIA_CAPABILITY_VIDEO;

  return typeflags;
}

