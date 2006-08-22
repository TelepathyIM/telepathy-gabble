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

#include "media-factory.h"

#define TP_SESSION_HANDLER_SET_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      DBUS_TYPE_G_OBJECT_PATH, \
      G_TYPE_STRING, \
      G_TYPE_INVALID))

#define TP_CHANNEL_STREAM_TYPE (dbus_g_type_get_struct ("GValueArray", \
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
    NEW_ICE_SESSION_HANDLER,
    STREAM_ADDED,
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
  GabbleHandle initiator;

  DEBUG ("called");

  g_assert (GABBLE_IS_MEDIA_CHANNEL (channel));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (channel);

  g_assert (priv->session == NULL);

  object_path = g_strdup_printf ("%s/MediaSession%u", priv->object_path, peer);

  if (sid == NULL)
    {
      initiator = priv->conn->self_handle;
      sid = _gabble_media_factory_allocate_sid (priv->factory, channel);
    }
  else
    {
      initiator = peer;
      _gabble_media_factory_register_sid (priv->factory, sid, channel);
    }

  session = g_object_new (GABBLE_TYPE_MEDIA_SESSION,
                          "media-channel", channel,
                          "object-path", object_path,
                          "session-id", sid,
                          "initiator", initiator,
                          "peer", peer,
                          "peer-resource", peer_resource,
                          NULL);

  g_signal_connect (session, "notify::state",
                    (GCallback) session_state_changed_cb, channel);

  priv->session = session;

  g_signal_emit (channel, signals[NEW_ICE_SESSION_HANDLER], 0,
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

      gabble_group_mixin_change_members (G_OBJECT (chan), "", empty, empty, set, empty, 0, 0);

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
                                    "IM channel object.",
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

  signals[NEW_ICE_SESSION_HANDLER] =
    g_signal_new ("new-ice-session-handler",
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
                  gabble_media_channel_marshal_VOID__INT_INT_INT,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

  signals[STREAM_REMOVED] =
    g_signal_new ("stream-removed",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__INT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[STREAM_STATE_CHANGED] =
    g_signal_new ("stream-state-changed",
                  G_OBJECT_CLASS_TYPE (gabble_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_channel_marshal_VOID__INT_INT,
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

  g_assert (priv->closed && priv->session==NULL);

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

  DEBUG ("called on %p", obj);

  g_assert (GABBLE_IS_MEDIA_CHANNEL (obj));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);

  if (priv->closed)
    return TRUE;

  priv->closed = TRUE;

  if (priv->session)
    {
      _gabble_media_session_terminate (priv->session);
    }

  g_signal_emit(obj, signals[CLOSED], 0);

  return TRUE;
}


/**
 * gabble_media_channel_get_all_members
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
gboolean gabble_media_channel_get_all_members (GabbleMediaChannel *obj, GArray ** ret, GArray ** ret1, GArray ** ret2, GError **error)
{
  return gabble_group_mixin_get_all_members (G_OBJECT (obj), ret, ret1, ret2, error);
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
 * gabble_media_channel_get_handle_owners
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
gboolean gabble_media_channel_get_handle_owners (GabbleMediaChannel *obj, const GArray * handles, GArray ** ret, GError **error)
{
  return gabble_group_mixin_get_handle_owners (G_OBJECT (obj), handles, ret, error);
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
  const gchar *interfaces[] = {
      TP_IFACE_CHANNEL_INTERFACE_GROUP,
      TP_IFACE_CHANNEL_INTERFACE_ICE_SIGNALLING,
      NULL
  };

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
 * on interface org.freedesktop.Telepathy.Channel.Interface.IceSignalling
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

  g_assert (GABBLE_IS_MEDIA_CHANNEL (obj));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);

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
          0, member,
          1, path,
          2, "rtp",
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


/**
 * gabble_media_channel_list_streams
 *
 * Implements DBus method ListStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_channel_list_streams (GabbleMediaChannel *obj, GPtrArray ** ret, GError **error)
{
#if 0
  GabbleMediaChannelPrivate *priv;
  GabbleHandle handle, self_handle;
  GArray *array;
  int i;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (obj));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);

  self_handle = obj->group.self_handle;
  array = handle_set_to_array (obj->group.self_handle);

  if (array->len < 2)
    {
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                            "Channel has only one member");
      return FALSE;
    }

  *ret = g_ptr_array_sized_new (array->len - 1);

  for (i = 0; i < array->len; i++)
    {
      handle = g_array_index (array, GabbleHandle, i);
      if (handle != self_handle)
        {
          GValue streams = { 0, };
          g_value_init (&streams, TP_CHANNEL_STREAM_TYPE);
          g_value_take_boxed (&streams,
              dbus_g_type_specialized_construct (TP_CHANNEL_STREAM_TYPE));

          dbus_g_type_struct_set (&streams,
              0, handle,
              1, 1,
              2, TP_CODEC_MEDIA_TYPE_AUDIO,
              3, TP_MEDIA_STREAM_STATE_STOPPED,
              G_MAXUINT);

          g_ptr_array_add (*ret, g_value_get_boxed (&streams));
        }
    }

  g_array_free (array, TRUE);

  return TRUE;
#else
  DEBUG ("not implemented");

  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                        "ListStreams not implemented!");

  return FALSE;
#endif
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


/**
 * gabble_media_channel_request_streams
 *
 * Implements DBus method RequestStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_channel_request_streams (GabbleMediaChannel *obj, guint contact_handle, const GArray * types, GArray ** ret, GError **error)
{
  DEBUG ("not implemented");

  *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                        "RequestStreams not implemented!");

  return FALSE;
}


static gboolean
gabble_media_channel_add_member (GObject *obj, GabbleHandle handle, const gchar *message, GError **error)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (obj);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  GabbleGroupMixin *mixin = GABBLE_GROUP_MIXIN (obj);
  GabblePresence *presence;

  presence = gabble_presence_cache_get (priv->conn->presence_cache, handle);

  if (NULL == presence ||
      0 == (presence->caps & PRESENCE_CAP_GOOGLE_VOICE))
    {
      DEBUG ("handle %u doesn't support voice", handle);

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

          gabble_group_mixin_change_members (G_OBJECT (channel), "", set, empty, empty, empty, 0, 0);

          /* update flags accordingly -- allow removal, deny adding and rescinding */
          gabble_group_mixin_change_flags (G_OBJECT (channel),
              TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
              TP_CHANNEL_GROUP_FLAG_CAN_ADD |
              TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);
        }
    }
  else if (state == JS_STATE_ENDED)
    {
      GError *error;

      /* remove us and the peer from the member list */
      g_intset_add (set, mixin->self_handle);
      g_intset_add (set, peer);

      gabble_group_mixin_change_members (G_OBJECT (channel), "", empty, set, empty, empty, 0, 0);

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

void
_gabble_media_channel_stream_state (GabbleMediaChannel *chan, guint state)
{
  GabbleHandle handle, self_handle;
  GArray *array;
  int i;

  self_handle = chan->group.self_handle;
  array = handle_set_to_array (chan->group.members);

  for (i = 0; i < array->len; i++)
    {
      handle = g_array_index (array, GabbleHandle, i);

      if (handle != self_handle)
        {
          g_signal_emit (chan, signals[STREAM_STATE_CHANGED], 1, handle, i, state);
        }
    }

  g_array_free (array, TRUE);
}
