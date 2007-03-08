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

#include "debug.h"
#include "gabble-connection.h"
#include "gabble-media-session.h"
#include "presence.h"
#include "presence-cache.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-properties-interface.h>

#include "gabble-media-channel.h"

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

static void channel_iface_init (gpointer, gpointer);
static void media_signalling_iface_init (gpointer, gpointer);
static void streamed_media_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleMediaChannel, gabble_media_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
      media_signalling_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_STREAMED_MEDIA,
      streamed_media_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_PROPERTIES_INTERFACE,
      tp_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

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
  /* TP properties (see also below) */
  PROP_NAT_TRAVERSAL,
  PROP_STUN_SERVER,
  PROP_STUN_PORT,
  PROP_GTALK_P2P_RELAY_TOKEN,
  LAST_PROPERTY
};

/* TP properties */
enum
{
  CHAN_PROP_NAT_TRAVERSAL = 0,
  CHAN_PROP_STUN_SERVER,
  CHAN_PROP_STUN_PORT,
  CHAN_PROP_GTALK_P2P_RELAY_TOKEN,
  NUM_CHAN_PROPS,
  INVALID_CHAN_PROP
};

const TpPropertySignature channel_property_signatures[NUM_CHAN_PROPS] = {
      { "nat-traversal",          G_TYPE_STRING },
      { "stun-server",            G_TYPE_STRING },
      { "stun-port",              G_TYPE_UINT   },
      { "gtalk-p2p-relay-token",  G_TYPE_STRING }
};

/* private structure */
typedef struct _GabbleMediaChannelPrivate GabbleMediaChannelPrivate;

struct _GabbleMediaChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;
  TpHandle creator;

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

  /* initialize properties mixin */
  tp_properties_mixin_init (G_OBJECT (self), G_STRUCT_OFFSET (
        GabbleMediaChannel, properties));
}

static GObject *
gabble_media_channel_constructor (GType type, guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMediaChannelPrivate *priv;
  TpBaseConnection *conn;
  DBusGConnection *bus;
  TpIntSet *set;
  TpHandleRepoIface *contact_handles;

  obj = G_OBJECT_CLASS (gabble_media_channel_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (GABBLE_MEDIA_CHANNEL (obj));
  conn = (TpBaseConnection *)priv->conn;
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  tp_group_mixin_init ((TpSvcChannelInterfaceGroup *)obj,
      G_STRUCT_OFFSET (GabbleMediaChannel, group),
      contact_handles, conn->self_handle);

  /* automatically add creator to channel */
  set = tp_intset_new ();
  tp_intset_add (set, priv->creator);

  tp_group_mixin_change_members ((TpSvcChannelInterfaceGroup *)obj,
      "", set, NULL, NULL, NULL, 0, 0);

  tp_intset_destroy (set);

  /* allow member adding */
  tp_group_mixin_change_flags ((TpSvcChannelInterfaceGroup *)obj,
      TP_CHANNEL_GROUP_FLAG_CAN_ADD, 0);

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
create_session (GabbleMediaChannel *channel,
                TpHandle peer,
                const gchar *peer_resource,
                const gchar *sid)
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

  tp_svc_channel_interface_media_signalling_emit_new_session_handler (
      channel, object_path, "rtp");

  g_free (object_path);

  return session;
}

gboolean
_gabble_media_channel_dispatch_session_action (GabbleMediaChannel *chan,
                                               TpHandle peer,
                                               const gchar *peer_resource,
                                               const gchar *sid,
                                               LmMessage *message,
                                               LmMessageNode *session_node,
                                               const gchar *action,
                                               GError **error)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  GabbleMediaSession *session = priv->session;
  gboolean session_is_new = FALSE;

  if (session == NULL)
    {
      TpGroupMixin *mixin = TP_GROUP_MIXIN (chan);
      TpIntSet *set;

      session = create_session (chan, peer, peer_resource, sid);
      session_is_new = TRUE;

      /* make us local pending */
      set = tp_intset_new ();
      tp_intset_add (set, mixin->self_handle);

      tp_group_mixin_change_members ((TpSvcChannelInterfaceGroup *)chan,
          "", NULL, NULL, set, NULL, 0, 0);

      tp_intset_destroy (set);

      /* and update flags accordingly */
      tp_group_mixin_change_flags ((TpSvcChannelInterfaceGroup *)chan,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD | TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
          0);
    }

  g_object_ref (session);

  if (_gabble_media_session_handle_action (session, message, session_node,
        action, error))
    {
      g_object_unref (session);
      return TRUE;
    }
  else
    {
      if (session_is_new)
        _gabble_media_session_terminate (session, INITIATOR_LOCAL,
            TP_CHANNEL_GROUP_CHANGE_REASON_ERROR);

      g_object_unref (session);
      return FALSE;
    }
}

static void
gabble_media_channel_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  const gchar *param_name;
  guint tp_property_id;

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
      param_name = g_param_spec_get_name (pspec);

      if (tp_properties_mixin_has_property (object, param_name,
            &tp_property_id))
        {
          GValue *tp_property_value =
            chan->properties.properties[tp_property_id].value;

          if (tp_property_value)
            {
              g_value_copy (tp_property_value, value);
              return;
            }
        }

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
  const gchar *param_name;
  guint tp_property_id;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      /* this property is writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      break;
    case PROP_HANDLE_TYPE:
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
      param_name = g_param_spec_get_name (pspec);

      if (tp_properties_mixin_has_property (object, param_name,
            &tp_property_id))
        {
          tp_properties_mixin_change_value (object, tp_property_id, value,
                                                NULL);
          tp_properties_mixin_change_flags (object, tp_property_id,
                                                TP_PROPERTY_FLAG_READ,
                                                0, NULL);

          return;
        }

      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_media_channel_dispose (GObject *object);
static void gabble_media_channel_finalize (GObject *object);
static gboolean gabble_media_channel_remove_member (TpSvcChannelInterfaceGroup *obj, TpHandle handle, const gchar *message, GError **error);

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

  tp_group_mixin_class_init ((TpSvcChannelInterfaceGroupClass *)object_class,
                                 G_STRUCT_OFFSET (GabbleMediaChannelClass, group_class),
                                 _gabble_media_channel_add_member,
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
                                  "The TpHandle representing the contact "
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

  param_spec = g_param_spec_string ("nat-traversal",
                                    "NAT traversal",
                                    "NAT traversal mechanism.",
                                    "gtalk-p2p",
                                    G_PARAM_CONSTRUCT |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NAT_TRAVERSAL, param_spec);

  param_spec = g_param_spec_string ("stun-server",
                                    "STUN server",
                                    "IP or address of STUN server.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_SERVER, param_spec);

  param_spec = g_param_spec_uint ("stun-port",
                                  "STUN port",
                                  "UDP port of STUN server.",
                                  0, G_MAXUINT16, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_PORT, param_spec);

  param_spec = g_param_spec_string ("gtalk-p2p-relay-token",
                                    "GTalk P2P Relay Token",
                                    "Magic token to authenticate with the "
                                    "Google Talk relay server.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_GTALK_P2P_RELAY_TOKEN, param_spec);

  tp_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleMediaChannelClass, properties_class),
      channel_property_signatures, NUM_CHAN_PROPS, NULL);
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
    gabble_media_channel_close (self);

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

  tp_group_mixin_finalize ((TpSvcChannelInterfaceGroup *)object);

  G_OBJECT_CLASS (gabble_media_channel_parent_class)->finalize (object);
}


/**
 * gabble_media_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_media_channel_close_async (TpSvcChannel *iface,
                                  DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (iface);

  gabble_media_channel_close (self);
  tp_svc_channel_return_from_close (context);
}

void
gabble_media_channel_close (GabbleMediaChannel *self)
{
  GabbleMediaChannelPrivate *priv;

  DEBUG ("called on %p", self);

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (priv->closed)
    {
      return;
    }

  priv->closed = TRUE;

  if (priv->session)
    {
      _gabble_media_session_terminate (priv->session, INITIATOR_LOCAL, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
    }

  tp_svc_channel_emit_closed (self);
}


/**
 * gabble_media_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_media_channel_get_channel_type (TpSvcChannel *iface,
                                       DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
}


/**
 * gabble_media_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_media_channel_get_handle (TpSvcChannel *iface,
                                 DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_handle (context, 0, 0);
}


/**
 * gabble_media_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
gabble_media_channel_get_interfaces (TpSvcChannel *iface,
                                     DBusGMethodInvocation *context)
{
  const gchar *interfaces[] = {
      TP_IFACE_CHANNEL_INTERFACE_GROUP,
      TP_IFACE_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
      TP_IFACE_PROPERTIES_INTERFACE,
      NULL
  };

  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}


/**
 * gabble_media_channel_get_session_handlers
 *
 * Implements D-Bus method GetSessionHandlers
 * on interface org.freedesktop.Telepathy.Channel.Interface.MediaSignalling
 */
static void
gabble_media_channel_get_session_handlers (TpSvcChannelInterfaceMediaSignalling *iface,
                                           DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (iface);
  GabbleMediaChannelPrivate *priv;
  GPtrArray *ret;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (priv->session)
    {
      GValue handler = { 0, };
      TpHandle member;
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

      ret = g_ptr_array_sized_new (1);
      g_ptr_array_add (ret, g_value_get_boxed (&handler));
    }
  else
    {
      ret = g_ptr_array_sized_new (0);
    }

  tp_svc_channel_interface_media_signalling_return_from_get_session_handlers (
      context, ret);
  g_ptr_array_free (ret, TRUE);
}


static GPtrArray *
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
      TpHandle peer;
      TpMediaStreamType type;
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
 */
static void
gabble_media_channel_list_streams (TpSvcChannelTypeStreamedMedia *iface,
                                   DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (iface);
  GabbleMediaChannelPrivate *priv;
  GPtrArray *ret;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  /* no session yet? return an empty array */
  if (priv->session == NULL)
    {
      ret = g_ptr_array_new ();
    }
  else
    {
      ret = make_stream_list (self, priv->streams);
    }

  tp_svc_channel_type_streamed_media_return_from_list_streams (context, ret);
  g_ptr_array_free (ret, TRUE);
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
 */
static void
gabble_media_channel_remove_streams (TpSvcChannelTypeStreamedMedia *iface,
                                     const GArray * streams,
                                     DBusGMethodInvocation *context)
{
  GabbleMediaChannel *obj = GABBLE_MEDIA_CHANNEL (iface);
  GabbleMediaChannelPrivate *priv;
  GPtrArray *stream_objs;
  GError *error = NULL;
  guint i;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (obj));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (obj);

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
          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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

  if (error)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
  else
    {
      tp_svc_channel_type_streamed_media_return_from_remove_streams (context);
    }
}


/**
 * gabble_media_channel_request_stream_direction
 *
 * Implements D-Bus method RequestStreamDirection
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
gabble_media_channel_request_stream_direction (TpSvcChannelTypeStreamedMedia *iface,
                                               guint stream_id,
                                               guint stream_direction,
                                               DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (iface);
  GabbleMediaChannelPrivate *priv;
  GabbleMediaStream *stream;
  GError *error = NULL;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (stream_direction > TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "given stream direction %u is not valid", stream_direction);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  stream = _find_stream_by_id (self, stream_id);
  if (stream == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "given stream id %u does not exist", stream_id);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  /* streams with no session? I think not... */
  g_assert (priv->session != NULL);

  if (_gabble_media_session_request_stream_direction (priv->session, stream,
        stream_direction, &error))
    {
      tp_svc_channel_type_streamed_media_return_from_request_stream_direction (
          context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}


/**
 * gabble_media_channel_request_streams
 *
 * Implements D-Bus method RequestStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
gabble_media_channel_request_streams (TpSvcChannelTypeStreamedMedia *iface,
                                      guint contact_handle,
                                      const GArray *types,
                                      DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (iface);
  GabbleMediaChannelPrivate *priv;
  TpBaseConnection *conn;
  GPtrArray *streams;
  GError *error = NULL;
  GPtrArray *ret;
  TpHandleRepoIface *contact_handles;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (self);
  conn = (TpBaseConnection *)priv->conn;
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handle_is_valid (contact_handles, contact_handle, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (!tp_handle_set_is_member (self->group.members, contact_handle) &&
      !tp_handle_set_is_member (self->group.remote_pending, contact_handle))
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "given handle %u is not a member of the channel", contact_handle);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  /* if the person is a channel member, we should have a session */
  g_assert (priv->session != NULL);

  if (!_gabble_media_session_request_streams (priv->session, types, &streams,
        &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = make_stream_list (self, streams);

  g_ptr_array_free (streams, TRUE);

  tp_svc_channel_type_streamed_media_return_from_request_streams (context, ret);
  g_ptr_array_free (ret, TRUE);
}


gboolean
_gabble_media_channel_add_member (TpSvcChannelInterfaceGroup *obj,
                                  TpHandle handle,
                                  const gchar *message,
                                  GError **error)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (obj);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
#ifdef ENABLE_DEBUG
  TpBaseConnection *conn = (TpBaseConnection *)priv->conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
#endif

  /* did we create this channel? */
  if (priv->creator == mixin->self_handle)
    {
      GabblePresence *presence;
      TpIntSet *set;

      /* yes: check the peer's capabilities */

      presence = gabble_presence_cache_get (priv->conn->presence_cache, handle);

      if (presence == NULL)
        {
          DEBUG ("failed to add contact %d (%s) to media channel: "
              "no presence available", handle,
              tp_handle_inspect (contact_handles, handle));
          goto NO_CAPS;
        }

      if (!(presence->caps & PRESENCE_CAP_GOOGLE_VOICE ||
            presence->caps & PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO ||
            presence->caps & PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO))
        {
          DEBUG ("failed to add contact %d (%s) to media channel: "
              "caps %x aren't sufficient", handle,
              tp_handle_inspect (contact_handles, handle),
              presence->caps);
          goto NO_CAPS;
        }

      /* yes: invite the peer */

      /* create a new session */
      create_session (chan, handle, NULL, NULL);

      /* make the peer remote pending */
      set = tp_intset_new ();
      tp_intset_add (set, handle);

      tp_group_mixin_change_members (obj, "", NULL, NULL, NULL, set, 0, 0);

      tp_intset_destroy (set);

      /* and update flags accordingly */
      tp_group_mixin_change_flags (obj,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE | TP_CHANNEL_GROUP_FLAG_CAN_RESCIND,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD);

      return TRUE;
    }
  else
    {
      /* no: has a session been created, is the handle being added ours,
       *     and are we in local pending? */

      if (priv->session &&
          handle == mixin->self_handle &&
          tp_handle_set_is_member (mixin->local_pending, handle))
        {
          /* yes: accept the request */

          TpIntSet *set;

          /* make us a member */
          set = tp_intset_new ();
          tp_intset_add (set, handle);

          tp_group_mixin_change_members (obj,
              "", set, NULL, NULL, NULL, 0, 0);

          tp_intset_destroy (set);

          /* update flags */
          tp_group_mixin_change_flags (obj,
              0, TP_CHANNEL_GROUP_FLAG_CAN_ADD);

          /* signal acceptance */
          _gabble_media_session_accept (priv->session);

          return TRUE;
        }
    }

  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
      "handle %u cannot be added in the current state", handle);
  return FALSE;

NO_CAPS:
  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
      "handle %u has no media capabilities", handle);
  return FALSE;
}

static gboolean
gabble_media_channel_remove_member (TpSvcChannelInterfaceGroup *obj, TpHandle handle, const gchar *message, GError **error)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (obj);
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpIntSet *set;

  if (priv->session == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "handle %u cannot be removed in the current state", handle);

      return FALSE;
    }

  if (priv->creator != mixin->self_handle &&
      handle != mixin->self_handle)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
          "handle %u cannot be removed because you are not the creator of the"
          " channel", handle);

      return FALSE;
    }

  _gabble_media_session_terminate (priv->session, INITIATOR_LOCAL, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  /* remove the member */
  set = tp_intset_new ();
  tp_intset_add (set, handle);

  tp_group_mixin_change_members (obj, "", NULL, set, NULL, NULL, 0, 0);

  tp_intset_destroy (set);

  /* and update flags accordingly */
  tp_group_mixin_change_flags (obj, TP_CHANNEL_GROUP_FLAG_CAN_ADD,
      TP_CHANNEL_GROUP_FLAG_CAN_REMOVE | TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);

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
  TpGroupMixin *mixin = TP_GROUP_MIXIN (channel);
  gchar *sid;
  JingleSessionState state;
  TpHandle peer;
  TpIntSet *set;

  g_object_get (session,
                "state", &state,
                "peer", &peer,
                NULL);

  set = tp_intset_new ();

  /* remove us and the peer from the member list */
  tp_intset_add (set, mixin->self_handle);
  tp_intset_add (set, peer);

  tp_group_mixin_change_members ((TpSvcChannelInterfaceGroup *)channel,
      "", NULL, set, NULL, NULL, terminator, reason);

  /* update flags accordingly -- allow adding, deny removal */
  tp_group_mixin_change_flags ((TpSvcChannelInterfaceGroup *)channel,
      TP_CHANNEL_GROUP_FLAG_CAN_ADD,
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
  gabble_media_channel_close (channel);
}


static void
session_state_changed_cb (GabbleMediaSession *session,
                          GParamSpec *arg1,
                          GabbleMediaChannel *channel)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (channel);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (channel);
  JingleSessionState state;
  TpHandle peer;
  TpIntSet *set;

  g_object_get (session,
                "state", &state,
                "peer", &peer,
                NULL);

  if (state != JS_STATE_ACTIVE)
    return;

  if (priv->creator != mixin->self_handle)
    return;

  set = tp_intset_new ();

  /* add the peer to the member list */
  tp_intset_add (set, peer);

  tp_group_mixin_change_members ((TpSvcChannelInterfaceGroup *)channel,
      "", set, NULL, NULL, NULL, 0, 0);

  /* update flags accordingly -- allow removal, deny adding and rescinding */
  tp_group_mixin_change_flags ((TpSvcChannelInterfaceGroup *)channel,
      TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
      TP_CHANNEL_GROUP_FLAG_CAN_ADD | TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);

  tp_intset_destroy (set);
}

static void
stream_close_cb (GabbleMediaStream *stream,
                 GabbleMediaChannel *chan)
{
  GabbleMediaChannelPrivate *priv = GABBLE_MEDIA_CHANNEL_GET_PRIVATE (chan);
  guint id;

  g_object_get (stream, "id", &id, NULL);

  tp_svc_channel_type_streamed_media_emit_stream_removed (chan, id);

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
  tp_svc_channel_type_streamed_media_emit_stream_error (chan, id, errno,
      message);

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

  tp_svc_channel_type_streamed_media_emit_stream_state_changed (chan,
      id, connection_state);
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

  tp_svc_channel_type_streamed_media_emit_stream_direction_changed (
      chan, id, direction, pending_send);
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

  tp_svc_channel_type_streamed_media_emit_stream_added (
      chan, id, handle, type);
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


static void
channel_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, gabble_media_channel_##x##suffix)
  IMPLEMENT(close,_async);
  IMPLEMENT(get_channel_type,);
  IMPLEMENT(get_handle,);
  IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}

static void
streamed_media_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelTypeStreamedMediaClass *klass = (TpSvcChannelTypeStreamedMediaClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_streamed_media_implement_##x (\
    klass, gabble_media_channel_##x)
  IMPLEMENT(list_streams);
  IMPLEMENT(remove_streams);
  IMPLEMENT(request_stream_direction);
  IMPLEMENT(request_streams);
#undef IMPLEMENT
}

static void
media_signalling_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceMediaSignallingClass *klass = (TpSvcChannelInterfaceMediaSignallingClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_media_signalling_implement_##x (\
    klass, gabble_media_channel_##x)
  IMPLEMENT(get_session_handlers);
#undef IMPLEMENT
}
