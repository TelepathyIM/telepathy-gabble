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

#include "config.h"
#include "media-channel.h"


#include <dbus/dbus-glib.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-properties-interface.h>
#include <telepathy-glib/svc-media-interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "debug.h"
#include "jingle-content.h"
#include "jingle-factory.h"
#include "jingle-media-rtp.h"
#include "jingle-session.h"
#include "media-factory.h"
#include "media-stream.h"
#include "namespaces.h"
#include "presence-cache.h"
#include "presence.h"
#include "util.h"

#define MAX_STREAMS 99

static void call_state_iface_init (gpointer, gpointer);
static void channel_iface_init (gpointer, gpointer);
static void hold_iface_init (gpointer, gpointer);
static void media_signalling_iface_init (gpointer, gpointer);
static void streamed_media_iface_init (gpointer, gpointer);
static void session_handler_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleMediaChannel, gabble_media_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CALL_STATE,
      call_state_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_HOLD,
      hold_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
      media_signalling_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_STREAMED_MEDIA,
      streamed_media_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_PROPERTIES_INTERFACE,
      tp_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_MEDIA_SESSION_HANDLER,
      session_handler_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

static const gchar *gabble_media_channel_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_CALL_STATE,
    TP_IFACE_CHANNEL_INTERFACE_GROUP,
    TP_IFACE_CHANNEL_INTERFACE_HOLD,
    TP_IFACE_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
    TP_IFACE_PROPERTIES_INTERFACE,
    TP_IFACE_MEDIA_SESSION_HANDLER,
    NULL
};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_TARGET_ID,
  PROP_INITIAL_PEER,
  PROP_PEER,
  PROP_REQUESTED,
  PROP_CONNECTION,
  PROP_CREATOR,
  PROP_CREATOR_ID,
  PROP_FACTORY,
  PROP_INTERFACES,
  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,
  /* TP properties (see also below) */
  PROP_NAT_TRAVERSAL,
  PROP_STUN_SERVER,
  PROP_STUN_PORT,
  PROP_GTALK_P2P_RELAY_TOKEN,
  PROP_SESSION,
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

struct _GabbleMediaChannelPrivate
{
  GabbleConnection *conn;
  gchar *object_path;
  TpHandle creator;
  TpHandle initial_peer;

  GabbleMediaFactory *factory;
  GabbleJingleSession *session;

  /* array of referenced GabbleMediaStream* */
  GPtrArray *streams;
  /* list of PendingStreamRequest* in no particular order */
  GList *pending_stream_requests;

  /* list of StreamCreationData* in no particular order */
  GList *stream_creation_datas;

  guint next_stream_id;

  TpLocalHoldState hold_state;
  TpLocalHoldStateReason hold_state_reason;

  /* The "most held" of all associated contents' current states, which is what
   * we present on CallState.
   */
  JingleRtpRemoteState remote_state;

  GPtrArray *delayed_request_streams;

  /* These are really booleans, but gboolean is signed. Thanks, GLib */
  unsigned ready:1;
  unsigned closed:1;
  unsigned dispose_has_run:1;
};

struct _delayed_request_streams_ctx {
  GabbleMediaChannel *chan;
  gulong caps_disco_id;
  guint timeout_id;
  guint contact_handle;
  GArray *types;
  DBusGMethodInvocation *context;
};

static void destroy_request (struct _delayed_request_streams_ctx *ctx,
    gpointer user_data);

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

static void session_state_changed_cb (GabbleJingleSession *session,
    GParamSpec *arg1, GabbleMediaChannel *channel);
static void session_terminated_cb (GabbleJingleSession *session,
    gboolean local_terminator, TpChannelGroupChangeReason reason,
    gpointer user_data);
static void session_new_content_cb (GabbleJingleSession *session,
    GabbleJingleContent *c, gpointer user_data);
static void create_stream_from_content (GabbleMediaChannel *chan,
    GabbleJingleContent *c);
static gboolean contact_is_media_capable (GabbleMediaChannel *chan, TpHandle peer,
    gboolean *wait);
static void stream_creation_data_cancel (gpointer p, gpointer unused);

static void
_create_streams (GabbleMediaChannel *chan)
{
  GabbleMediaChannelPrivate *priv = chan->priv;
  GList *contents, *li;

  contents = gabble_jingle_session_get_contents (priv->session);
  for (li = contents; li; li = li->next)
    {
      create_stream_from_content (chan, GABBLE_JINGLE_CONTENT (li->data));
    }

  g_list_free (contents);
}

static void
_latch_to_session (GabbleMediaChannel *chan)
{
  GabbleMediaChannelPrivate *priv = chan->priv;

  g_assert (priv->session != NULL);

  DEBUG ("%p: Latching onto session %p", chan, priv->session);

  g_signal_connect (priv->session, "notify::state",
                    (GCallback) session_state_changed_cb, chan);

  g_signal_connect (priv->session, "new-content",
                    (GCallback) session_new_content_cb, chan);

  g_signal_connect (priv->session, "terminated",
                    (GCallback) session_terminated_cb, chan);

  g_assert (priv->streams == NULL);

  priv->streams = g_ptr_array_sized_new (1);

  tp_svc_channel_interface_media_signalling_emit_new_session_handler (
      G_OBJECT (chan), priv->object_path, "rtp");
}

static void
create_session (GabbleMediaChannel *chan, TpHandle peer)
{
  GabbleMediaChannelPrivate *priv = chan->priv;

  g_assert (priv->session == NULL);

  DEBUG ("%p: Creating new outgoing session", chan);

  priv->session = g_object_ref (
      gabble_jingle_factory_create_session (priv->conn->jingle_factory,
          peer, NULL));

  _latch_to_session (chan);
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
  GabbleJingleFactory *jf;
  const gchar *relay_token;
  gchar *stun_server;
  guint stun_port;

  obj = G_OBJECT_CLASS (gabble_media_channel_parent_class)->
           constructor (type, n_props, props);

  priv = GABBLE_MEDIA_CHANNEL (obj)->priv;
  conn = (TpBaseConnection *) priv->conn;
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  tp_group_mixin_init (obj, G_STRUCT_OFFSET (GabbleMediaChannel, group),
      contact_handles, conn->self_handle);

  if (priv->session != NULL)
      priv->creator = priv->session->peer;
  else
      priv->creator = conn->self_handle;

  /* automatically add creator to channel, but also ref them again (because
   * priv->creator is the InitiatorHandle) */
  g_assert (priv->creator != 0);
  tp_handle_ref (contact_handles, priv->creator);

  set = tp_intset_new ();
  tp_intset_add (set, priv->creator);

  tp_group_mixin_change_members (obj, "", set, NULL, NULL, NULL, 0,
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  tp_intset_destroy (set);

  /* We implement the 0.17.6 properties correctly */
  tp_group_mixin_change_flags (obj, TP_CHANNEL_GROUP_FLAG_PROPERTIES, 0);

  /* Set up Google relay related properties */
  jf = priv->conn->jingle_factory;

  if (gabble_jingle_factory_get_stun_server (jf, &stun_server,
        &stun_port))
    {
      g_object_set (obj,
          "stun-server", stun_server,
          "stun-port", stun_port,
          NULL);
      g_free (stun_server);
    }

  relay_token = gabble_jingle_factory_get_google_relay_token (jf);

  if (relay_token != NULL)
    {
      g_object_set (obj,
          "gtalk-p2p-relay-token", relay_token,
          NULL);
    }

  if (priv->session != NULL)
    {
      /* This is an incoming call; make us local pending and don't set any
       * group flags (all we can do is add or remove ourselves, which is always
       * valid per the spec)
       */
      set = tp_intset_new ();
      tp_intset_add (set, ((TpBaseConnection *) priv->conn)->self_handle);

      tp_group_mixin_change_members (obj, "", NULL, NULL, set, NULL,
          priv->session->peer, TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);

      tp_intset_destroy (set);

      /* Set up signal callbacks, emit session handler, initialize streams */
      _latch_to_session (GABBLE_MEDIA_CHANNEL (obj));
      _create_streams (GABBLE_MEDIA_CHANNEL (obj));
    }
  else
    {
      /* This is an outgoing call. We'll set CanAdd here, in case the UI is
       * using the "RequestChannel(StreamedMedia, HandleTypeNone, 0);
       * AddMembers([h], ""); RequestStreams(h, [...])" legacy API. If the
       * channel request came via one of the APIs where the peer is added
       * immediately, that'll happen in media-factory.c before the channel is
       * returned, and CanAdd will be cleared.
       *
       * If this channel was made with Create or Ensure, the CanAdd flag will
       * stick around, but it shouldn't.
       *
       * FIXME: refactor this so we know which calling convention is in use
       *        here, rather than poking it from media-factory.c.
       */
      tp_group_mixin_change_flags (obj, TP_CHANNEL_GROUP_FLAG_CAN_ADD, 0);
    }

  return obj;
}

static void
gabble_media_channel_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = chan->priv;
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;
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
      /* This is used to implement TargetHandleType, which is immutable.  If
       * the peer was known at channel-creation time, this will be Contact;
       * otherwise, it must be None even if we subsequently learn who the peer
       * is.
       */
      if (priv->initial_peer != 0)
        g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
      else
        g_value_set_uint (value, TP_HANDLE_TYPE_NONE);
      break;
    case PROP_INITIAL_PEER:
    case PROP_HANDLE:
      /* As above: TargetHandle is immutable, so non-0 only if the peer handle
       * was known at creation time.
       */
      g_value_set_uint (value, priv->initial_peer);
      break;
    case PROP_TARGET_ID:
      /* As above. */
      if (priv->initial_peer != 0)
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, TP_HANDLE_TYPE_CONTACT);
          const gchar *target_id = tp_handle_inspect (repo, priv->initial_peer);

          g_value_set_string (value, target_id);
        }
      else
        {
          g_value_set_static_string (value, "");
        }

      break;
    case PROP_PEER:
      {
        TpHandle peer = 0;

        if (priv->initial_peer != 0)
          peer = priv->initial_peer;
        else if (priv->session != NULL)
          g_object_get (priv->session,
              "peer", &peer,
              NULL);

        g_value_set_uint (value, peer);
        break;
      }
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_CREATOR:
      g_value_set_uint (value, priv->creator);
      break;
    case PROP_CREATOR_ID:
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, TP_HANDLE_TYPE_CONTACT);

          g_value_set_string (value, tp_handle_inspect (repo, priv->creator));
        }
      break;
    case PROP_REQUESTED:
      g_value_set_boolean (value, (priv->creator == base_conn->self_handle));
      break;
    case PROP_FACTORY:
      g_value_set_object (value, priv->factory);
      break;
    case PROP_INTERFACES:
      g_value_set_boxed (value, gabble_media_channel_interfaces);
      break;
    case PROP_CHANNEL_DESTROYED:
      g_value_set_boolean (value, priv->closed);
      break;
    case PROP_CHANNEL_PROPERTIES:
      g_value_take_boxed (value,
          tp_dbus_properties_mixin_make_properties_hash (object,
              TP_IFACE_CHANNEL, "TargetHandle",
              TP_IFACE_CHANNEL, "TargetHandleType",
              TP_IFACE_CHANNEL, "ChannelType",
              TP_IFACE_CHANNEL, "TargetID",
              TP_IFACE_CHANNEL, "InitiatorHandle",
              TP_IFACE_CHANNEL, "InitiatorID",
              TP_IFACE_CHANNEL, "Requested",
              TP_IFACE_CHANNEL, "Interfaces",
              NULL));
      break;
    case PROP_SESSION:
      g_value_set_object (value, priv->session);
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
  GabbleMediaChannelPrivate *priv = chan->priv;
  const gchar *param_name;
  guint tp_property_id;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE_TYPE:
    case PROP_HANDLE:
    case PROP_CHANNEL_TYPE:
      /* these properties are writable in the interface, but not actually
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
    case PROP_INITIAL_PEER:
      priv->initial_peer = g_value_get_uint (value);

      if (priv->initial_peer != 0)
        {
          TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;
          TpHandleRepoIface *repo = tp_base_connection_get_handles (base_conn,
              TP_HANDLE_TYPE_CONTACT);
          tp_handle_ref (repo, priv->initial_peer);
        }

      break;
    case PROP_SESSION:
      g_assert (priv->session == NULL);
      priv->session = g_value_dup_object (value);
      if (priv->session != NULL)
        {

        }
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
static gboolean gabble_media_channel_remove_member (GObject *obj,
    TpHandle handle, const gchar *message, guint reason, GError **error);

static void
gabble_media_channel_class_init (GabbleMediaChannelClass *gabble_media_channel_class)
{
  static TpDBusPropertiesMixinPropImpl channel_props[] = {
      { "TargetHandleType", "handle-type", NULL },
      { "TargetHandle", "handle", NULL },
      { "TargetID", "target-id", NULL },
      { "ChannelType", "channel-type", NULL },
      { "Interfaces", "interfaces", NULL },
      { "Requested", "requested", NULL },
      { "InitiatorHandle", "creator", NULL },
      { "InitiatorID", "creator-id", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        channel_props,
      },
      { NULL }
  };
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_media_channel_class,
      sizeof (GabbleMediaChannelPrivate));

  object_class->constructor = gabble_media_channel_constructor;

  object_class->get_property = gabble_media_channel_get_property;
  object_class->set_property = gabble_media_channel_set_property;

  object_class->dispose = gabble_media_channel_dispose;
  object_class->finalize = gabble_media_channel_finalize;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_string ("target-id", "Target JID",
      "Currently empty, because this channel always has handle 0.",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_uint ("initial-peer", "Other participant",
      "The TpHandle representing the other participant in the channel if known "
      "at construct-time; 0 if the other participant was unknown at the time "
      "of channel creation",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_PEER, param_spec);

  param_spec = g_param_spec_uint ("peer", "Other participant",
      "The TpHandle representing the other participant in the channel if "
      "currently known; 0 if this is an anonymous channel on which "
      "RequestStreams  has not yet been called.",
      0, G_MAXUINT32, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PEER, param_spec);

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this media channel object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_uint ("creator", "Channel creator",
      "The TpHandle representing the contact who created the channel.",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CREATOR, param_spec);

  param_spec = g_param_spec_string ("creator-id", "Creator bare JID",
      "The bare JID obtained by inspecting the creator handle.",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CREATOR_ID, param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  param_spec = g_param_spec_object ("factory", "GabbleMediaFactory object",
      "The factory that created this object.",
      GABBLE_TYPE_MEDIA_FACTORY,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_FACTORY, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_string ("nat-traversal", "NAT traversal",
      "NAT traversal mechanism.",
      "gtalk-p2p",
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NAT_TRAVERSAL,
      param_spec);

  param_spec = g_param_spec_string ("stun-server", "STUN server",
      "IP or address of STUN server.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_SERVER, param_spec);

  param_spec = g_param_spec_uint ("stun-port", "STUN port",
      "UDP port of STUN server.",
      0, G_MAXUINT16, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_PORT, param_spec);

  param_spec = g_param_spec_string ("gtalk-p2p-relay-token",
      "GTalk P2P Relay Token",
      "Magic token to authenticate with the Google Talk relay server.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_GTALK_P2P_RELAY_TOKEN,
      param_spec);

  param_spec = g_param_spec_object ("session", "GabbleJingleSession object",
      "Jingle session associated with this media channel object.",
      GABBLE_TYPE_JINGLE_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SESSION, param_spec);

  tp_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleMediaChannelClass, properties_class),
      channel_property_signatures, NUM_CHAN_PROPS, NULL);

  gabble_media_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleMediaChannelClass, dbus_props_class));

  tp_group_mixin_class_init (object_class,
      G_STRUCT_OFFSET (GabbleMediaChannelClass, group_class),
      _gabble_media_channel_add_member, NULL);
  tp_group_mixin_class_set_remove_with_reason_func (object_class,
      gabble_media_channel_remove_member);
  tp_group_mixin_init_dbus_properties (object_class);
}

void
gabble_media_channel_dispose (GObject *object)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (object);
  GabbleMediaChannelPrivate *priv = self->priv;
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (
      conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  DEBUG ("called");

  priv->dispose_has_run = TRUE;

  /* StreamCreationData * holds a reference to the media channel; thus, we
   * shouldn't be disposed till they've all gone away.
   */
  g_assert (priv->stream_creation_datas == NULL);

  if (priv->delayed_request_streams != NULL)
    {
      g_ptr_array_foreach (priv->delayed_request_streams,
          (GFunc) destroy_request, NULL);
      g_assert (priv->delayed_request_streams == NULL);
    }

  tp_handle_unref (contact_handles, priv->creator);
  priv->creator = 0;

  if (priv->initial_peer != 0)
    {
      tp_handle_unref (contact_handles, priv->initial_peer);
      priv->initial_peer = 0;
    }

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
  GabbleMediaChannelPrivate *priv = self->priv;

  g_free (priv->object_path);

  tp_group_mixin_finalize (object);
  tp_properties_mixin_finalize (object);

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

  DEBUG ("called");
  gabble_media_channel_close (self);
  tp_svc_channel_return_from_close (context);
}

void
gabble_media_channel_close (GabbleMediaChannel *self)
{
  GabbleMediaChannelPrivate *priv;

  DEBUG ("called on %p", self);

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = self->priv;

  if (priv->closed)
    {
      return;
    }

  priv->closed = TRUE;

  if (priv->session)
    {
      gabble_jingle_session_terminate (priv->session,
          TP_CHANNEL_GROUP_CHANGE_REASON_NONE, NULL);
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
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (iface);

  if (self->priv->initial_peer == 0)
    tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_NONE, 0);
  else
    tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
        self->priv->initial_peer);
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
  tp_svc_channel_return_from_get_interfaces (context,
      gabble_media_channel_interfaces);
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
  GType info_type = TP_STRUCT_TYPE_MEDIA_SESSION_HANDLER_INFO;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = self->priv;

  if (priv->session)
    {
      GValue handler = { 0, };
      TpHandle member;

      g_value_init (&handler, info_type);
      g_value_take_boxed (&handler,
          dbus_g_type_specialized_construct (info_type));

      g_object_get (priv->session,
                    "peer", &member,
                    NULL);

      dbus_g_type_struct_set (&handler,
          0, priv->object_path,
          1, "rtp",
          G_MAXUINT);

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
                  guint len,
                  GabbleMediaStream **streams)
{
  GabbleMediaChannelPrivate *priv = self->priv;
  GPtrArray *ret;
  guint i;
  GType info_type = TP_STRUCT_TYPE_MEDIA_STREAM_INFO;

  ret = g_ptr_array_sized_new (len);

  for (i = 0; i < len; i++)
    {
      GValue entry = { 0, };
      guint id;
      TpHandle peer;
      TpMediaStreamType type;
      TpMediaStreamState connection_state;
      CombinedStreamDirection combined_direction;

      g_object_get (streams[i],
          "id", &id,
          "media-type", &type,
          "connection-state", &connection_state,
          "combined-direction", &combined_direction,
          NULL);

      g_object_get (priv->session, "peer", &peer, NULL);

      g_value_init (&entry, info_type);
      g_value_take_boxed (&entry,
          dbus_g_type_specialized_construct (info_type));

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

  priv = self->priv;

  /* no session yet? return an empty array */
  if (priv->session == NULL)
    {
      ret = g_ptr_array_new ();
    }
  else
    {
      ret = make_stream_list (self, priv->streams->len,
          (GabbleMediaStream **) priv->streams->pdata);
    }

  tp_svc_channel_type_streamed_media_return_from_list_streams (context, ret);
  g_ptr_array_foreach (ret, (GFunc) g_value_array_free, NULL);
  g_ptr_array_free (ret, TRUE);
}


static GabbleMediaStream *
_find_stream_by_id (GabbleMediaChannel *chan, guint stream_id)
{
  GabbleMediaChannelPrivate *priv;
  guint i;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (chan));

  priv = chan->priv;

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

  priv = obj->priv;

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

      /* make sure we don't allow the client to repeatedly remove the same
      stream */
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
    {
      GabbleJingleContent *c;

      for (i = 0; i < stream_objs->len; i++)
        {
          g_object_get (g_ptr_array_index (stream_objs, i), "content", &c, NULL);

          /* FIXME: make sure session emits content-removed, on which we can
           * delete it from the list */
          gabble_jingle_session_remove_content (priv->session, c);
        }
    }

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

  priv = self->priv;

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

  DEBUG ("called (stream %s, direction %u)", stream->name, stream_direction);

  /* streams with no session? I think not... */
  g_assert (priv->session != NULL);

  if (stream_direction == TP_MEDIA_STREAM_DIRECTION_NONE)
    {
      GabbleJingleContent *c;

      DEBUG ("request for NONE direction; removing stream");

      g_object_get (stream, "content", &c, NULL);
      gabble_jingle_session_remove_content (priv->session, c);

      tp_svc_channel_type_streamed_media_return_from_request_stream_direction (
          context);

      return;
    }

  if (gabble_media_stream_change_direction (stream, stream_direction, &error))
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

static const gchar *
_pick_best_content_type (GabbleMediaChannel *chan, TpHandle peer,
  const gchar *resource, JingleMediaType type)
{
  GabbleMediaChannelPrivate *priv = chan->priv;
  GabblePresence *presence;

  presence = gabble_presence_cache_get (priv->conn->presence_cache, peer);

  if (presence == NULL)
    {
      DEBUG ("contact %d has no presence available", peer);
      return NULL;
    }

  if (gabble_presence_resource_has_caps (presence, resource,
          PRESENCE_CAP_JINGLE_RTP))
    {
      return NS_JINGLE_RTP;
    }

  if ((type == JINGLE_MEDIA_TYPE_VIDEO) &&
      gabble_presence_resource_has_caps (presence, resource,
          PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO))
    {
      return NS_JINGLE_DESCRIPTION_VIDEO;
    }

  if ((type == JINGLE_MEDIA_TYPE_AUDIO) &&
      gabble_presence_resource_has_caps (presence, resource,
          PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO))
    {
      return NS_JINGLE_DESCRIPTION_AUDIO;
    }
  if ((type == JINGLE_MEDIA_TYPE_AUDIO) &&
      gabble_presence_resource_has_caps (presence, resource,
          PRESENCE_CAP_GOOGLE_VOICE))
    {
      return NS_GOOGLE_SESSION_PHONE;
    }

  return NULL;
}


static const gchar *
_pick_best_resource (GabbleMediaChannel *chan,
  TpHandle peer, gboolean want_audio, gboolean want_video,
  const char **transport_ns, JingleDialect *dialect)
{
  GabbleMediaChannelPrivate *priv = chan->priv;
  GabblePresence *presence;
  GabblePresenceCapabilities caps;
  const gchar *resource = NULL;

  presence = gabble_presence_cache_get (priv->conn->presence_cache, peer);

  if (presence == NULL)
    {
      DEBUG ("contact %d has no presence available", peer);
      return NULL;
    }

  *dialect = JINGLE_DIALECT_ERROR;
  *transport_ns = NULL;

  g_return_val_if_fail (want_audio || want_video, NULL);

  /* Try newest Jingle standard */
  caps = PRESENCE_CAP_JINGLE_RTP;
  resource = gabble_presence_pick_resource_by_caps (presence, caps);

  if (resource != NULL)
    {
      *dialect = JINGLE_DIALECT_V032;
      goto CHOOSE_TRANSPORT;
    }

  /* Else try older Jingle draft, audio + video */
  caps = PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO |
      PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO;
  resource = gabble_presence_pick_resource_by_caps (presence, caps);

  if (resource != NULL)
    {
      *dialect = JINGLE_DIALECT_V015;
      goto CHOOSE_TRANSPORT;
    }

  /* In this unlikely case, we can get by with just video */
  if (!want_audio)
    {
      caps = PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO;
      resource = gabble_presence_pick_resource_by_caps (presence, caps);

      if (resource != NULL)
        {
          *dialect = JINGLE_DIALECT_V015;
          goto CHOOSE_TRANSPORT;
        }
    }

  /* Uh, huh, we can't provide what's requested. */
  if (want_video)
      return NULL;

  /* Ok, try just older Jingle draft, audio */
  caps = PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO;
  resource = gabble_presence_pick_resource_by_caps (presence, caps);

  if (resource != NULL)
    {
      *dialect = JINGLE_DIALECT_V015;
      goto CHOOSE_TRANSPORT;
    }

  /* There is still hope, try GTalk */
  caps = PRESENCE_CAP_GOOGLE_VOICE;
  resource = gabble_presence_pick_resource_by_caps (presence, caps);

  if (resource != NULL)
    {
      *dialect = JINGLE_DIALECT_GTALK4;
      goto CHOOSE_TRANSPORT;
    }

  /* Nope, nothing we can do. */
  return NULL;

CHOOSE_TRANSPORT:
  /* We prefer ICE, Google-P2P, then raw UDP */

  if (gabble_presence_resource_has_caps (presence, resource,
        PRESENCE_CAP_JINGLE_TRANSPORT_ICE))
    {
      *transport_ns = NS_JINGLE_TRANSPORT_ICE;
    }
  else if (gabble_presence_resource_has_caps (presence, resource,
        PRESENCE_CAP_GOOGLE_TRANSPORT_P2P))
    {
      *transport_ns = NS_GOOGLE_TRANSPORT_P2P;
    }
  else if (gabble_presence_resource_has_caps (presence, resource,
        PRESENCE_CAP_JINGLE_TRANSPORT_RAWUDP))
    {
      *transport_ns = NS_JINGLE_TRANSPORT_RAWUDP;
    }
  else if (*dialect == JINGLE_DIALECT_GTALK4)
    {
      /* (Some) GTalk clients don't advertise gtalk-p2p, though
       * they support it. If we know it's GTalk and there's no
       * transport, we can assume it also. */
      *transport_ns = NS_GOOGLE_TRANSPORT_P2P;
    }

  if (*transport_ns == NULL)
      return NULL;

  return resource;
}

typedef struct {
    /* number of streams requested == number of content objects */
    guint len;
    /* array of @len borrowed pointers */
    GabbleJingleContent **contents;
    /* accumulates borrowed pointers to streams. Initially @len NULL pointers;
     * when the stream for contents[i] is created, it is stored at streams[i].
     */
    GabbleMediaStream **streams;
    /* number of non-NULL elements in streams (0 <= satisfied <= contents) */
    guint satisfied;
    DBusGMethodInvocation *context;
} PendingStreamRequest;

static PendingStreamRequest *
pending_stream_request_new (GPtrArray *contents,
                            DBusGMethodInvocation *context)
{
  PendingStreamRequest *p = g_slice_new0 (PendingStreamRequest);

  p->len = contents->len;
  p->contents = g_memdup (contents->pdata, contents->len * sizeof (gpointer));
  p->streams = g_new0 (GabbleMediaStream *, contents->len);
  p->satisfied = 0;
  p->context = context;

  return p;
}

static gboolean
pending_stream_request_maybe_satisfy (PendingStreamRequest *p,
                                      GabbleMediaChannel *channel,
                                      GabbleJingleContent *content,
                                      GabbleMediaStream *stream)
{
  guint i;

  for (i = 0; i < p->len; i++)
    {
      if (p->contents[i] == content)
        {
          g_assert (p->streams[i] == NULL);
          p->streams[i] = stream;

          if (++p->satisfied == p->len && p->context != NULL)
            {
              GPtrArray *ret = make_stream_list (channel, p->len, p->streams);

              tp_svc_channel_type_streamed_media_return_from_request_streams (
                  p->context, ret);
              g_ptr_array_foreach (ret, (GFunc) g_value_array_free, NULL);
              g_ptr_array_free (ret, TRUE);
              p->context = NULL;
              return TRUE;
            }
        }
    }

  return FALSE;
}

static gboolean
pending_stream_request_maybe_fail (PendingStreamRequest *p,
                                   GabbleMediaChannel *channel,
                                   GabbleJingleContent *content)
{
  guint i;

  for (i = 0; i < p->len; i++)
    {
      if (content == p->contents[i])
        {
          GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "A stream was removed before it could be fully set up" };

          /* return early */
          dbus_g_method_return_error (p->context, &e);
          p->context = NULL;
          return TRUE;
        }
    }

  return FALSE;
}

static void
pending_stream_request_free (gpointer data)
{
  PendingStreamRequest *p = data;

  if (p->context != NULL)
    {
      GError e = { TP_ERRORS, TP_ERROR_CANCELLED,
          "The session terminated before the requested streams could be added"
      };

      dbus_g_method_return_error (p->context, &e);
    }

  g_free (p->contents);
  g_free (p->streams);

  g_slice_free (PendingStreamRequest, p);
}

static gboolean
_gabble_media_channel_request_contents (GabbleMediaChannel *chan,
                                        const GArray *media_types,
                                        GPtrArray **ret,
                                        GError **error)
{
  GabbleMediaChannelPrivate *priv = chan->priv;
  gboolean want_audio, want_video;
  JingleDialect dialect;
  guint idx;
  TpHandle peer;
  const gchar *peer_resource;
  const gchar *transport_ns = NULL;

  DEBUG ("called");

  g_object_get (priv->session, "peer", &peer,
      "peer-resource", &peer_resource, NULL);

  if (!contact_is_media_capable (chan, peer, NULL))
    {
      DEBUG ("peer has no a/v capabilities");
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "member has no audio/video capabilities");

      return FALSE;
    }

  want_audio = want_video = FALSE;

  for (idx = 0; idx < media_types->len; idx++)
    {
      guint media_type = g_array_index (media_types, guint, idx);

      if (media_type == TP_MEDIA_STREAM_TYPE_AUDIO)
        {
          want_audio = TRUE;
        }
      else if (media_type == TP_MEDIA_STREAM_TYPE_VIDEO)
        {
          want_video = TRUE;
        }
      else
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "given media type %u is invalid", media_type);
          return FALSE;
        }
    }

  g_object_get (priv->session, "dialect", &dialect, NULL);

  /* existing call; the recipient and the mode has already been decided */
  if (dialect != JINGLE_DIALECT_ERROR)
    {
      /* is a google call... we have no other option */
      if (JINGLE_IS_GOOGLE_DIALECT (dialect))
        {
          DEBUG ("already in Google mode; can't add new stream");

          g_assert (priv->streams->len == 1);

          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "Google Talk calls may only contain one stream");

          return FALSE;
        }

      /* check if the resource supports it; FIXME - we assume only
       * one channel type (video or audio) will be added later */
      if (NULL == _pick_best_content_type (chan, peer, peer_resource,
          want_audio ? JINGLE_MEDIA_TYPE_AUDIO : JINGLE_MEDIA_TYPE_VIDEO))
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "member does not have the desired audio/video capabilities");

          return FALSE;
        }

      /* We assume we already picked the best possible transport ns for the
       * previous streams, so we just reuse that one */
        {
          GList *contents = gabble_jingle_session_get_contents (priv->session);
          GabbleJingleContent *c = contents->data;
          g_list_free (contents);

          g_object_get (c, "transport-ns", &transport_ns, NULL);
        }
    }
  /* no existing call; we should choose a recipient and a mode */
  else
    {
      DEBUG ("picking the best resource (want audio: %u, want video: %u",
            want_audio, want_video);

      g_assert (priv->streams->len == 0);

      peer_resource = _pick_best_resource (chan, peer, want_audio, want_video,
          &transport_ns, &dialect);

      if (peer_resource == NULL)
        {
          DEBUG ("contact doesn't have a resource with suitable capabilities");

          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "member does not have the desired audio/video capabilities");

          return FALSE;
        }

      DEBUG ("Picking resource '%s' (transport: %s, dialect: %u)",
          peer_resource, transport_ns, dialect);

      g_object_set (priv->session, "dialect", dialect,
          "peer-resource", peer_resource, NULL);
    }

  /* check it's not a ridiculous number of streams */
  if ((priv->streams->len + media_types->len) > MAX_STREAMS)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "I think that's quite enough streams already");
      return FALSE;
    }

  /* if we've got here, we're good to make the Jingle contents */

  *ret = g_ptr_array_sized_new (media_types->len);

  for (idx = 0; idx < media_types->len; idx++)
    {
      guint media_type = g_array_index (media_types, guint, idx);
      GabbleJingleContent *c;
      const gchar *content_ns;

      content_ns = _pick_best_content_type (chan, peer, peer_resource,
          media_type == TP_MEDIA_STREAM_TYPE_AUDIO ?
            JINGLE_MEDIA_TYPE_AUDIO : JINGLE_MEDIA_TYPE_VIDEO);

      /* if we got this far, resource should be capable enough, so we
       * should not fail in choosing ns */
      g_assert (content_ns != NULL);
      g_assert (transport_ns != NULL);

      DEBUG ("Creating new jingle content with ns %s : %s", content_ns, transport_ns);

      c = gabble_jingle_session_add_content (priv->session,
          media_type == TP_MEDIA_STREAM_TYPE_AUDIO ?
            JINGLE_MEDIA_TYPE_AUDIO : JINGLE_MEDIA_TYPE_VIDEO,
            content_ns, transport_ns);

      /* The stream is created in "new-content" callback, and appended to
       * priv->streams. This is now guaranteed to happen asynchronously (adding
       * streams can take time due to the relay info lookup, and if it doesn't,
       * we use an idle so it does). */
      g_assert (c != NULL);
      g_ptr_array_add (*ret, c);
    }

  return TRUE;
}

/* user_data param is here so we match the GFunc prototype */
static void
destroy_request (struct _delayed_request_streams_ctx *ctx,
    gpointer user_data G_GNUC_UNUSED)
{
  GabbleMediaChannelPrivate *priv = ctx->chan->priv;

  if (ctx->timeout_id)
    g_source_remove (ctx->timeout_id);

  if (ctx->caps_disco_id)
    g_signal_handler_disconnect (priv->conn->presence_cache,
        ctx->caps_disco_id);

  if (ctx->context != NULL)
    {
      GError *error = NULL;
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "cannot add streams: peer has insufficient caps");
      dbus_g_method_return_error (ctx->context, error);
      g_error_free (error);
    }

  g_array_free (ctx->types, TRUE);
  g_slice_free (struct _delayed_request_streams_ctx, ctx);
  g_ptr_array_remove_fast (priv->delayed_request_streams, ctx);

  if (priv->delayed_request_streams->len == 0)
    {
      g_ptr_array_free (priv->delayed_request_streams, TRUE);
      priv->delayed_request_streams = NULL;
    }
}

static void gabble_media_channel_request_streams (TpSvcChannelTypeStreamedMedia *iface,
    guint contact_handle, const GArray *types, DBusGMethodInvocation *context);

static gboolean
repeat_request (struct _delayed_request_streams_ctx *ctx)
{
  gabble_media_channel_request_streams (
      TP_SVC_CHANNEL_TYPE_STREAMED_MEDIA (ctx->chan),
      ctx->contact_handle, ctx->types, ctx->context);

  ctx->timeout_id = 0;
  ctx->context = NULL;
  destroy_request (ctx, NULL);
  return FALSE;
}

static void
capabilities_discovered_cb (GabblePresenceCache *cache,
                            TpHandle handle,
                            struct _delayed_request_streams_ctx *ctx)
{
  /* If there are more cache caps pending, wait for them. */
  if (gabble_presence_cache_caps_pending (cache, handle))
    return;

  repeat_request (ctx);
}

static void
delay_stream_request (GabbleMediaChannel *chan,
                      TpSvcChannelTypeStreamedMedia *iface,
                      guint contact_handle,
                      const GArray *types,
                      DBusGMethodInvocation *context,
                      gboolean disco_in_progress)
{
  GabbleMediaChannelPrivate *priv = chan->priv;
  struct _delayed_request_streams_ctx *ctx =
    g_slice_new0 (struct _delayed_request_streams_ctx);

  ctx->chan = chan;
  ctx->contact_handle = contact_handle;
  ctx->context = context;
  ctx->types = g_array_sized_new (FALSE, FALSE, sizeof (guint), types->len);
  g_array_append_vals (ctx->types, types->data, types->len);

  if (disco_in_progress)
    {
      ctx->caps_disco_id = g_signal_connect (priv->conn->presence_cache,
          "capabilities-discovered", G_CALLBACK (capabilities_discovered_cb),
          ctx);
      ctx->timeout_id = 0;
    }
  else
    {
      ctx->caps_disco_id = 0;
      ctx->timeout_id = g_timeout_add_seconds (5,
          (GSourceFunc) repeat_request, ctx);
    }

  if (priv->delayed_request_streams == NULL)
      priv->delayed_request_streams = g_ptr_array_sized_new (1);

  g_ptr_array_add (priv->delayed_request_streams, ctx);
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
  GPtrArray *contents;
  GError *error = NULL;
  TpHandleRepoIface *contact_handles;
  gboolean wait;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  /* FIXME: disallow this if we've put the other guy on hold? */

  priv = self->priv;
  conn = (TpBaseConnection *) priv->conn;
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handle_is_valid (contact_handles, contact_handle, &error))
    goto error;

  /* If we know the caps haven't arrived yet, delay stream creation
   * and check again later */
  if (!contact_is_media_capable (self, contact_handle, &wait))
    {
      if (wait)
        {
          DEBUG ("Delaying RequestStreams until we get all caps from contact");
          delay_stream_request (self, iface, contact_handle, types, context,
              TRUE);
          return;
        }

      /* if we're unsure about the offlineness of the contact, wait a bit */
      if (gabble_presence_cache_is_unsure (priv->conn->presence_cache))
        {
          DEBUG ("Delaying RequestStreams because we're unsure about them");
          delay_stream_request (self, iface, contact_handle, types, context,
              FALSE);
          return;
        }
    }


  if (priv->session == NULL)
    {
      create_session (self, contact_handle);
    }
  else
    {
      TpHandle peer;

      g_object_get (priv->session,
          "peer", &peer,
          NULL);

      if (peer != contact_handle)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "cannot add streams for %u: this channel's peer is %u",
              contact_handle, peer);
          goto error;
        }
    }

  if (!_gabble_media_channel_request_contents (self, types, &contents,
        &error))
    goto error;

  priv->pending_stream_requests = g_list_prepend (
      priv->pending_stream_requests,
      pending_stream_request_new (contents, context));
  g_ptr_array_free (contents, TRUE);
  return;

error:
  dbus_g_method_return_error (context, error);
  g_error_free (error);
}


static gboolean
contact_is_media_capable (GabbleMediaChannel *chan, TpHandle peer, gboolean *wait)
{
  GabbleMediaChannelPrivate *priv = chan->priv;
  GabblePresence *presence;
  GabblePresenceCapabilities caps;
#ifdef ENABLE_DEBUG
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (
      conn, TP_HANDLE_TYPE_CONTACT);
#endif

  if (wait != NULL)
    *wait = FALSE;

  if (gabble_presence_cache_caps_pending (priv->conn->presence_cache, peer))
    {
      DEBUG ("caps are pending for peer %u", peer);
      if (wait != NULL)
        *wait = TRUE;
    }

  caps = PRESENCE_CAP_GOOGLE_VOICE | PRESENCE_CAP_JINGLE_RTP |
    PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO | PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO;

  presence = gabble_presence_cache_get (priv->conn->presence_cache, peer);

  if (presence == NULL)
    {
      DEBUG ("contact %d (%s) has no presence available", peer,
          tp_handle_inspect (contact_handles, peer));
      return FALSE;
    }

  if ((presence->caps & caps) == 0)
    {
      DEBUG ("contact %d (%s) doesn't have sufficient media caps", peer,
          tp_handle_inspect (contact_handles, peer));
      return FALSE;
    }

  return TRUE;
}

gboolean
_gabble_media_channel_add_member (GObject *obj,
                                  TpHandle handle,
                                  const gchar *message,
                                  GError **error)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (obj);
  GabbleMediaChannelPrivate *priv = chan->priv;
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  /* did we create this channel? */
  if (priv->creator == mixin->self_handle)
    {
      TpIntSet *set;
      gboolean wait;

      /* yes: check we don't have a peer already, invite this onis one */

      if (priv->session != NULL)
        {
          TpHandle peer;

          g_object_get (priv->session, "peer", &peer, NULL);

          if (peer != handle)
            {
              g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                  "handle %u cannot be added: this channel's peer is %u",
                  handle, peer);
              return FALSE;
            }
        }

      /* We can't delay the request at this time, but if there's a chance
       * the caps might be available later, we'll add the contact and
       * hope for the best. */
      if (!contact_is_media_capable (chan, handle, &wait))
        {
          if (wait ||
              gabble_presence_cache_is_unsure (priv->conn->presence_cache))
            {
              DEBUG ("contact %u caps still pending, adding anyways", handle);
            }
          else
            {
              g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                  "handle %u cannot be added: has no media capabilities",
                  handle);
              return FALSE;
            }
        }

      /* make the peer remote pending */
      set = tp_intset_new ();
      tp_intset_add (set, handle);

      tp_group_mixin_change_members (obj, "", NULL, NULL, NULL, set,
          mixin->self_handle, TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);

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
       *     and are we in local pending? (call answer) */

      if (priv->session &&
          handle == mixin->self_handle &&
          tp_handle_set_is_member (mixin->local_pending, handle))
        {
          /* yes: accept the request */

          TpIntSet *set;

          /* make us a member */
          set = tp_intset_new ();
          tp_intset_add (set, handle);

          tp_group_mixin_change_members (obj, "", set, NULL, NULL, NULL,
              handle, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

          tp_intset_destroy (set);

          /* update flags */
          tp_group_mixin_change_flags (obj,
              0, TP_CHANNEL_GROUP_FLAG_CAN_ADD);

          /* accept any local pending sends */
          g_ptr_array_foreach (priv->streams,
              (GFunc) gabble_media_stream_accept_pending_local_send, NULL);

          /* signal acceptance */
          gabble_jingle_session_accept (priv->session);

          return TRUE;
        }
    }

  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
      "handle %u cannot be added in the current state", handle);
  return FALSE;
}

static gboolean
gabble_media_channel_remove_member (GObject *obj,
                                    TpHandle handle,
                                    const gchar *message,
                                    guint reason,
                                    GError **error)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (obj);
  GabbleMediaChannelPrivate *priv = chan->priv;
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

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

  if (!gabble_jingle_session_terminate (priv->session, reason, error))
    return FALSE;

  /* We're terminating the session, any further changes to the members are
   * meaningless, since the channel will go away RSN. */
  tp_group_mixin_change_flags (obj, 0,
      TP_CHANNEL_GROUP_FLAG_CAN_REMOVE | TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);

  return TRUE;
}

static void
session_terminated_cb (GabbleJingleSession *session,
                       gboolean local_terminator,
                       TpChannelGroupChangeReason reason,
                       gpointer user_data)
{
  GabbleMediaChannel *channel = (GabbleMediaChannel *) user_data;
  GabbleMediaChannelPrivate *priv = channel->priv;
  TpGroupMixin *mixin = TP_GROUP_MIXIN (channel);
  guint terminator;
  JingleSessionState state;
  TpHandle peer;
  TpIntSet *set;

  DEBUG ("called");

  g_object_get (session,
                "state", &state,
                "peer", &peer,
                NULL);

  if (local_terminator)
      terminator = mixin->self_handle;
  else
      terminator = peer;

  set = tp_intset_new ();

  /* remove us and the peer from the member list */
  tp_intset_add (set, mixin->self_handle);
  tp_intset_add (set, peer);

  tp_group_mixin_change_members ((GObject *) channel,
      "", NULL, set, NULL, NULL, terminator, reason);

  /* update flags accordingly -- no operations are meaningful any more, because
   * the channel is about to close.
   */
  tp_group_mixin_change_flags ((GObject *) channel, 0,
      TP_CHANNEL_GROUP_FLAG_CAN_ADD |
      TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
      TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);

  /* Ignore any Google relay session responses we're waiting for. */
  g_list_foreach (priv->stream_creation_datas, stream_creation_data_cancel,
      NULL);

  /* any contents that we were waiting for have now lost */
  g_list_foreach (priv->pending_stream_requests,
      (GFunc) pending_stream_request_free, NULL);
  g_list_free (priv->pending_stream_requests);
  priv->pending_stream_requests = NULL;

  /* unref streams */
  if (priv->streams != NULL)
    {
      GPtrArray *tmp = priv->streams;

      DEBUG ("unreffing streams");

      /* move priv->streams aside so that the stream_close_cb
       * doesn't double unref */
      priv->streams = NULL;
      g_ptr_array_foreach (tmp, (GFunc) g_object_unref, NULL);
      g_ptr_array_free (tmp, TRUE);
    }

  /* remove the session */
  g_object_unref (priv->session);
  priv->session = NULL;

  /* close us if we aren't already closed */
  if (!priv->closed)
    {
      DEBUG ("calling media channel close from session terminated cb");
      gabble_media_channel_close (channel);
    }
}


static void
session_state_changed_cb (GabbleJingleSession *session,
                          GParamSpec *arg1,
                          GabbleMediaChannel *channel)
{
  GabbleMediaChannelPrivate *priv = channel->priv;
  TpGroupMixin *mixin = TP_GROUP_MIXIN (channel);
  JingleSessionState state;
  TpHandle peer;
  TpIntSet *set;

  DEBUG ("called");

  g_object_get (session,
                "state", &state,
                "peer", &peer,
                NULL);

  set = tp_intset_new ();

  tp_intset_add (set, peer);

  if (state >= JS_STATE_PENDING_INITIATE_SENT &&
      state < JS_STATE_ACTIVE &&
      !tp_handle_set_is_member (mixin->members, peer))
    {
      /* The first time we send anything to the other user, they materialise
       * in remote-pending if necessary */

      tp_group_mixin_change_members ((GObject *) channel, "", NULL, NULL, NULL,
          set, mixin->self_handle, TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);

      tp_group_mixin_change_flags ((GObject *) channel,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE | TP_CHANNEL_GROUP_FLAG_CAN_RESCIND,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD);
    }

  if (state == JS_STATE_ACTIVE &&
      priv->creator == mixin->self_handle)
    {

      DEBUG ("adding peer to the member list and updating flags");

      /* add the peer to the member list */
      tp_group_mixin_change_members ((GObject *) channel,
          "", set, NULL, NULL, NULL, peer,
          TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

      /* update flags accordingly -- allow removal, deny adding and
       * rescinding */
      tp_group_mixin_change_flags ((GObject *) channel,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD | TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);

    }

  tp_intset_destroy (set);
}


static void
inform_peer_of_unhold (GabbleMediaChannel *self)
{
  gabble_jingle_session_send_held (self->priv->session, FALSE);
}


static void
inform_peer_of_hold (GabbleMediaChannel *self)
{
  gabble_jingle_session_send_held (self->priv->session, TRUE);
}


static void
stream_hold_state_changed (GabbleMediaStream *stream G_GNUC_UNUSED,
                           GParamSpec *unused G_GNUC_UNUSED,
                           gpointer data)
{
  GabbleMediaChannel *self = data;
  GabbleMediaChannelPrivate *priv = self->priv;
  gboolean all_held = TRUE, any_held = FALSE;
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      gboolean its_hold;

      g_object_get (g_ptr_array_index (priv->streams, i),
          "local-hold", &its_hold,
          NULL);

      DEBUG ("Stream at index %u has local-hold=%u", i, (guint) its_hold);

      all_held = all_held && its_hold;
      any_held = any_held || its_hold;
    }

  DEBUG ("all_held=%u, any_held=%u", (guint) all_held, (guint) any_held);

  if (all_held)
    {
      /* Move to state HELD */

      if (priv->hold_state == TP_LOCAL_HOLD_STATE_HELD)
        {
          /* nothing changed */
          return;
        }
      else if (priv->hold_state == TP_LOCAL_HOLD_STATE_PENDING_UNHOLD)
        {
          /* This can happen if the user asks us to hold, then changes their
           * mind. We make no particular guarantees about stream states when
           * in PENDING_UNHOLD state, so keep claiming to be in that state */
          return;
        }
      else if (priv->hold_state == TP_LOCAL_HOLD_STATE_PENDING_HOLD)
        {
          /* We wanted to hold, and indeed we have. Yay! Keep whatever
           * reason code we used for going to PENDING_HOLD */
          priv->hold_state = TP_LOCAL_HOLD_STATE_HELD;
        }
      else
        {
          /* We were previously UNHELD. So why have we gone on hold now? */
          DEBUG ("Unexpectedly entered HELD state!");
          priv->hold_state = TP_LOCAL_HOLD_STATE_HELD;
          priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;
        }
    }
  else if (any_held)
    {
      if (priv->hold_state == TP_LOCAL_HOLD_STATE_UNHELD)
        {
          /* The streaming client has spontaneously changed its stream
           * state. Why? We just don't know */
          DEBUG ("Unexpectedly entered PENDING_UNHOLD state!");
          priv->hold_state = TP_LOCAL_HOLD_STATE_PENDING_UNHOLD;
          priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;
        }
      else if (priv->hold_state == TP_LOCAL_HOLD_STATE_HELD)
        {
          /* Likewise */
          DEBUG ("Unexpectedly entered PENDING_HOLD state!");
          priv->hold_state = TP_LOCAL_HOLD_STATE_PENDING_HOLD;
          priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;
        }
      else
        {
          /* nothing particularly interesting - we're trying to change hold
           * state already, so nothing to signal */
          return;
        }
    }
  else
    {
      /* Move to state UNHELD */

      if (priv->hold_state == TP_LOCAL_HOLD_STATE_UNHELD)
        {
          /* nothing changed */
          return;
        }
      else if (priv->hold_state == TP_LOCAL_HOLD_STATE_PENDING_HOLD)
        {
          /* This can happen if the user asks us to unhold, then changes their
           * mind. We make no particular guarantees about stream states when
           * in PENDING_HOLD state, so keep claiming to be in that state */
          return;
        }
      else if (priv->hold_state == TP_LOCAL_HOLD_STATE_PENDING_UNHOLD)
        {
          /* We wanted to hold, and indeed we have. Yay! Keep whatever
           * reason code we used for going to PENDING_UNHOLD */
          priv->hold_state = TP_LOCAL_HOLD_STATE_UNHELD;
        }
      else
        {
          /* We were previously HELD. So why have we gone off hold now? */
          DEBUG ("Unexpectedly entered UNHELD state!");
          priv->hold_state = TP_LOCAL_HOLD_STATE_UNHELD;
          priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;
        }

      /* Tell the peer what's happened */
      inform_peer_of_unhold (self);
    }

  tp_svc_channel_interface_hold_emit_hold_state_changed (self,
      priv->hold_state, priv->hold_state_reason);
}


static void
stream_unhold_failed (GabbleMediaStream *stream,
                      gpointer data)
{
  GabbleMediaChannel *self = data;
  GabbleMediaChannelPrivate *priv = self->priv;
  guint i;

  DEBUG ("%p: %p", self, stream);

  /* Unholding failed - let's roll back to Hold state */
  priv->hold_state = TP_LOCAL_HOLD_STATE_PENDING_HOLD;
  priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_RESOURCE_NOT_AVAILABLE;
  tp_svc_channel_interface_hold_emit_hold_state_changed (self,
      priv->hold_state, priv->hold_state_reason);

  /* The stream's state may have changed from unheld to held, so re-poll.
   * It's possible that all streams are now held, in which case we can stop. */
  stream_hold_state_changed (stream, NULL, self);

  if (priv->hold_state == TP_LOCAL_HOLD_STATE_HELD)
    return;

  /* There should be no need to notify the peer, who already thinks they're
   * on hold, so just tell the streaming client what to do. */

  for (i = 0; i < priv->streams->len; i++)
    {
      gabble_media_stream_hold (g_ptr_array_index (priv->streams, i),
          TRUE);
    }
}


static void
stream_close_cb (GabbleMediaStream *stream,
                 GabbleMediaChannel *chan)
{
  GabbleMediaChannelPrivate *priv = chan->priv;
  guint id;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (chan));

  g_object_get (stream,
      "id", &id,
      NULL);

  tp_svc_channel_type_streamed_media_emit_stream_removed (chan, id);

  if (priv->streams != NULL)
    {
      g_ptr_array_remove (priv->streams, stream);

      /* A stream closing might cause the "total" hold state to change:
       * if there's one held and one unheld, and the unheld one closes,
       * then our state changes from indeterminate to held. */
      stream_hold_state_changed (stream, NULL, chan);

      g_object_unref (stream);
    }
}

static void
stream_error_cb (GabbleMediaStream *stream,
                 TpMediaStreamError errno,
                 const gchar *message,
                 GabbleMediaChannel *chan)
{
  GabbleMediaChannelPrivate *priv = chan->priv;
  GabbleJingleContent *c;
  guint id;

  /* emit signal */
  g_object_get (stream, "id", &id, "content", &c, NULL);
  tp_svc_channel_type_streamed_media_emit_stream_error (chan, id, errno,
      message);

  /* remove stream from session (removal will be signalled
   * so we can dispose of the stream) */
  gabble_jingle_session_remove_content (priv->session, c);
}

static void
stream_state_changed_cb (GabbleMediaStream *stream,
                         GParamSpec *pspec,
                         GabbleMediaChannel *chan)
{
  guint id;
  TpMediaStreamState connection_state;

  g_object_get (stream,
      "id", &id,
      "connection-state", &connection_state,
      NULL);

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

  DEBUG ("direction: %u, pending_send: %u", direction, pending_send);

  tp_svc_channel_type_streamed_media_emit_stream_direction_changed (
      chan, id, direction, pending_send);
}

static TpChannelCallStateFlags
jingle_remote_state_to_csf (JingleRtpRemoteState state)
{
  switch (state)
    {
    case JINGLE_RTP_REMOTE_STATE_ACTIVE:
    /* FIXME: we should be able to expose <mute/> through CallState */
    case JINGLE_RTP_REMOTE_STATE_MUTE:
      return 0;
    case JINGLE_RTP_REMOTE_STATE_RINGING:
      return TP_CHANNEL_CALL_STATE_RINGING;
    case JINGLE_RTP_REMOTE_STATE_HOLD:
      return TP_CHANNEL_CALL_STATE_HELD;
    default:
      g_assert_not_reached ();
    }
}

static void
remote_state_changed_cb (GabbleJingleMediaRtp *rtp,
    GParamSpec *pspec G_GNUC_UNUSED,
    GabbleMediaChannel *self)
{
  GabbleMediaChannelPrivate *priv = self->priv;
  JingleRtpRemoteState state = gabble_jingle_media_rtp_get_remote_state (rtp);
  TpChannelCallStateFlags csf = 0;

  DEBUG ("Content %p's state changed to %u (current channel state: %u)", rtp,
      state, priv->remote_state);

  if (state == priv->remote_state)
    {
      DEBUG ("already in that state");
      return;
    }

  if (state > priv->remote_state)
    {
      /* If this content's state is "more held" than the current aggregated level,
       * move up to it.
       */
      DEBUG ("%u is more held than %u, moving up", state, priv->remote_state);
      priv->remote_state = state;
    }
  else
    {
      /* This content is now less held than the current aggregated level; we
       * need to recalculate the highest hold level and see if it's changed.
       */
      guint i = 0;

      DEBUG ("%u less held than %u; recalculating", state, priv->remote_state);
      state = JINGLE_RTP_REMOTE_STATE_ACTIVE;

      for (i = 0; i < priv->streams->len; i++)
        {
          GabbleJingleMediaRtp *c = gabble_media_stream_get_content (
                g_ptr_array_index (priv->streams, i));
          JingleRtpRemoteState s = gabble_jingle_media_rtp_get_remote_state (c);

          state = MAX (state, s);
          DEBUG ("%p in state %u; high water mark %u", c, s, state);
        }

      if (priv->remote_state == state)
        {
          DEBUG ("no change");
          return;
        }

      priv->remote_state = state;
    }

  csf = jingle_remote_state_to_csf (priv->remote_state);
  DEBUG ("emitting CallStateChanged(%u, %u) (JingleRtpRemoteState %u)",
      priv->session->peer, csf, priv->remote_state);
  tp_svc_channel_interface_call_state_emit_call_state_changed (self,
      priv->session->peer, csf);
}

#define GTALK_CAPS \
  ( PRESENCE_CAP_GOOGLE_VOICE )

#define JINGLE_CAPS \
  ( PRESENCE_CAP_JINGLE015 | PRESENCE_CAP_JINGLE032 \
  | PRESENCE_CAP_GOOGLE_TRANSPORT_P2P )

#define JINGLE_AUDIO_CAPS \
  ( PRESENCE_CAP_JINGLE_RTP | PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO )

#define JINGLE_VIDEO_CAPS \
  ( PRESENCE_CAP_JINGLE_RTP | PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO )

GabblePresenceCapabilities
_gabble_media_channel_typeflags_to_caps (TpChannelMediaCapabilities flags)
{
  GabblePresenceCapabilities caps = 0;

  /* currently we can only signal any (GTalk or Jingle calls) using
   * the GTalk-P2P transport */
  if (flags & TP_CHANNEL_MEDIA_CAPABILITY_NAT_TRAVERSAL_GTALK_P2P)
    {
      DEBUG ("adding jingle caps");

      caps |= JINGLE_CAPS;

      if (flags & TP_CHANNEL_MEDIA_CAPABILITY_AUDIO)
        caps |= GTALK_CAPS | JINGLE_AUDIO_CAPS;

      if (flags & TP_CHANNEL_MEDIA_CAPABILITY_VIDEO)
        caps |= JINGLE_VIDEO_CAPS;
    }

  return caps;
}

static void
construct_stream (GabbleMediaChannel *chan,
                  GabbleJingleContent *c,
                  const gchar *name,
                  const gchar *nat_traversal,
                  const GPtrArray *relays)
{
  GObject *chan_o = (GObject *) chan;
  GabbleMediaChannelPrivate *priv = chan->priv;
  GabbleMediaStream *stream;
  TpMediaStreamType mtype;
  guint id;
  gchar *object_path;

  id = priv->next_stream_id++;

  object_path = g_strdup_printf ("%s/MediaStream%u",
      priv->object_path, id);

  stream = gabble_media_stream_new (object_path, c, name, id,
      nat_traversal, relays);

  DEBUG ("%p: created new MediaStream %p for content '%s'", chan, stream, name);

  g_ptr_array_add (priv->streams, stream);

  /* if any RequestStreams call was waiting for a stream to be created for
   * that content, return from it successfully */
    {
      GList *link = priv->pending_stream_requests;

      while (link != NULL)
        {
          if (pending_stream_request_maybe_satisfy (link->data,
                chan, c, stream))
            {
              GList *dead = link;

              pending_stream_request_free (dead->data);

              link = dead->next;
              priv->pending_stream_requests = g_list_delete_link (
                  priv->pending_stream_requests, dead);
            }
          else
            {
              link = link->next;
            }
        }
    }

  gabble_signal_connect_weak (stream, "close", (GCallback) stream_close_cb,
      chan_o);
  gabble_signal_connect_weak (stream, "error", (GCallback) stream_error_cb,
      chan_o);
  gabble_signal_connect_weak (stream, "unhold-failed",
      (GCallback) stream_unhold_failed, chan_o);
  gabble_signal_connect_weak (stream, "notify::connection-state",
      (GCallback) stream_state_changed_cb, chan_o);
  gabble_signal_connect_weak (stream, "notify::combined-direction",
      (GCallback) stream_direction_changed_cb, chan_o);
  gabble_signal_connect_weak (stream, "notify::local-hold",
      (GCallback) stream_hold_state_changed, chan_o);

  /* While we're here, watch the active/mute/held state of the corresponding
   * content so we can keep the call state up to date, and call the callback
   * once to pick up the current state of this content.
   */
  gabble_signal_connect_weak (c, "notify::remote-state",
      (GCallback) remote_state_changed_cb, chan_o);
  remote_state_changed_cb (GABBLE_JINGLE_MEDIA_RTP (c), NULL, chan);

  /* emit StreamAdded */
  mtype = gabble_media_stream_get_media_type (stream);

  DEBUG ("emitting StreamAdded with type '%s'",
    mtype == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video");

  tp_svc_channel_type_streamed_media_emit_stream_added (
      chan, id, priv->session->peer, mtype);

  /* A stream being added might cause the "total" hold state to change */
  stream_hold_state_changed (stream, NULL, chan);

  /* Initial stream direction was changed before we had time to hook up
   * signal handler, so we call the handler manually to pick it up. */
  stream_direction_changed_cb (stream, NULL, chan);

  if (priv->ready)
    {
      /* all of the streams are bidirectional from farsight's point of view, it's
       * just in the signalling they change */
      DEBUG ("emitting MediaSessionHandler:NewStreamHandler signal for stream %d", id);
      tp_svc_media_session_handler_emit_new_stream_handler (chan,
        object_path, id, mtype, TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL);
    }

  g_free (object_path);
}

typedef struct {
    GabbleMediaChannel *self;
    GabbleJingleContent *content;
    gulong removed_id;
    gchar *nat_traversal;
    gchar *name;
} StreamCreationData;

static void
stream_creation_data_cancel (gpointer p,
                             gpointer unused)
{
  StreamCreationData *d = p;

  d->content = NULL;
}

static void
stream_creation_data_free (gpointer p)
{
  StreamCreationData *d = p;
  GabbleMediaChannelPrivate *priv = d->self->priv;

  g_free (d->name);
  g_free (d->nat_traversal);

  if (d->content != NULL)
    {
      g_signal_handler_disconnect (d->content, d->removed_id);
      g_object_unref (d->content);
    }

  priv->stream_creation_datas = g_list_remove (priv->stream_creation_datas, d);

  g_object_unref (d->self);
  g_slice_free (StreamCreationData, d);
}

static gboolean
construct_stream_later_cb (gpointer user_data)
{
  StreamCreationData *d = user_data;

  if (d->content != NULL)
    construct_stream (d->self, d->content, d->name, d->nat_traversal, NULL);

  return FALSE;
}

static void
google_relay_session_cb (GPtrArray *relays,
                         gpointer user_data)
{
  StreamCreationData *d = user_data;

  if (d->content != NULL)
    construct_stream (d->self, d->content, d->name, d->nat_traversal, relays);

  stream_creation_data_free (d);
}

static void
content_removed_cb (GabbleJingleContent *content,
                    StreamCreationData *d)
{
  if (d->content != NULL)
    {
      GList *link = d->self->priv->pending_stream_requests;

      g_signal_handler_disconnect (d->content, d->removed_id);

      /* if any RequestStreams call was waiting for a stream to be created for
       * that content, return from it unsuccessfully */
      while (link != NULL)
        {
          if (pending_stream_request_maybe_fail (link->data,
                d->self, d->content))
            {
              GList *dead = link;

              pending_stream_request_free (dead->data);

              link = dead->next;
              d->self->priv->pending_stream_requests = g_list_delete_link (
                  d->self->priv->pending_stream_requests, dead);
            }
          else
            {
              link = link->next;
            }
        }

      g_object_unref (d->content);
      d->content = NULL;
    }
}

static void
create_stream_from_content (GabbleMediaChannel *self,
                            GabbleJingleContent *c)
{
  gchar *name, *nat_traversal;
  StreamCreationData *d;

  g_object_get (c,
      "name", &name,
      NULL);

  if (G_OBJECT_TYPE (c) != GABBLE_TYPE_JINGLE_MEDIA_RTP)
    {
      DEBUG ("ignoring non MediaRtp content '%s'", name);
      g_free (name);
      return;
    }

  g_object_get (self,
      "nat-traversal", &nat_traversal,
      NULL);

  d = g_slice_new0 (StreamCreationData);

  d->self = g_object_ref (self);
  d->nat_traversal = nat_traversal;
  d->name = name;
  d->content = g_object_ref (c);

  /* If the content gets removed before we've finished looking up its
   * relay (can this happen?) we need to cancel the creation of the stream,
   * and make any PendingStreamRequests fail */
  d->removed_id = g_signal_connect (c, "removed",
      G_CALLBACK (content_removed_cb), d);

  if (!tp_strdiff (nat_traversal, "gtalk-p2p"))
    {
      /* See if our server is Google, and if it is, ask them for a relay.
       * We ask for enough relays for 2 components (RTP and RTCP) since we
       * don't yet know whether there will be RTCP. */
      DEBUG ("Attempting to create Google relay session");
      gabble_jingle_factory_create_google_relay_session (
          self->priv->conn->jingle_factory, 2, google_relay_session_cb, d);
    }
  else
    {
      /* just create the stream (do it asynchronously so that the behaviour
       * is the same in each case) */
      g_idle_add_full (G_PRIORITY_DEFAULT, construct_stream_later_cb,
          d, stream_creation_data_free);
    }

  self->priv->stream_creation_datas = g_list_prepend (
      self->priv->stream_creation_datas, d);
}

static void
session_new_content_cb (GabbleJingleSession *session,
    GabbleJingleContent *c, gpointer user_data)
{
  GabbleMediaChannel *chan = GABBLE_MEDIA_CHANNEL (user_data);

  DEBUG ("called");

  create_stream_from_content (chan, c);
}

TpChannelMediaCapabilities
_gabble_media_channel_caps_to_typeflags (GabblePresenceCapabilities caps)
{
  TpChannelMediaCapabilities typeflags = 0;

  /* this is intentionally asymmetric to the previous function - we don't
   * require the other end to advertise the GTalk-P2P transport capability
   * separately because old GTalk clients didn't do that - having Google voice
   * implied Google session and GTalk-P2P */

  /* TODO: we should use RTP_AUDIO and RTP_VIDEO instead of just RTP */

  if ((caps & PRESENCE_CAP_GOOGLE_VOICE) ||
      ((caps & PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO) &&
       (caps & PRESENCE_CAP_GOOGLE_TRANSPORT_P2P)))
        typeflags |= TP_CHANNEL_MEDIA_CAPABILITY_AUDIO;

  if ((caps & PRESENCE_CAP_JINGLE_RTP) ||
      ((caps & PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO) &&
       (caps & PRESENCE_CAP_GOOGLE_TRANSPORT_P2P)))
        typeflags |= TP_CHANNEL_MEDIA_CAPABILITY_VIDEO;

  return typeflags;
}

static void
gabble_media_channel_get_call_states (TpSvcChannelInterfaceCallState *iface,
                                      DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = (GabbleMediaChannel *) iface;
  GabbleMediaChannelPrivate *priv = self->priv;
  JingleRtpRemoteState state = priv->remote_state;
  GHashTable *states = g_hash_table_new (g_direct_hash, g_direct_equal);

  if (state != JINGLE_RTP_REMOTE_STATE_ACTIVE)
    g_hash_table_insert (states, GUINT_TO_POINTER (priv->session->peer),
        GUINT_TO_POINTER (jingle_remote_state_to_csf (state)));

  tp_svc_channel_interface_call_state_return_from_get_call_states (context,
      states);

  g_hash_table_destroy (states);
}

static void
gabble_media_channel_get_hold_state (TpSvcChannelInterfaceHold *iface,
                                     DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = (GabbleMediaChannel *) iface;
  GabbleMediaChannelPrivate *priv = self->priv;

  tp_svc_channel_interface_hold_return_from_get_hold_state (context,
      priv->hold_state, priv->hold_state_reason);
}


static void
gabble_media_channel_request_hold (TpSvcChannelInterfaceHold *iface,
                                   gboolean hold,
                                   DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (iface);
  GabbleMediaChannelPrivate *priv = self->priv;
  guint i;
  TpLocalHoldState old_state = priv->hold_state;

  DEBUG ("%p: RequestHold(%u)", self, !!hold);

  if (hold)
    {
      if (priv->hold_state == TP_LOCAL_HOLD_STATE_HELD)
        {
          DEBUG ("No-op");
          tp_svc_channel_interface_hold_return_from_request_hold (context);
          return;
        }

      if (priv->hold_state == TP_LOCAL_HOLD_STATE_UNHELD)
        {
          inform_peer_of_hold (self);
        }

      priv->hold_state = TP_LOCAL_HOLD_STATE_PENDING_HOLD;
    }
  else
    {
      if (priv->hold_state == TP_LOCAL_HOLD_STATE_UNHELD)
        {
          DEBUG ("No-op");
          tp_svc_channel_interface_hold_return_from_request_hold (context);
          return;
        }

      priv->hold_state = TP_LOCAL_HOLD_STATE_PENDING_UNHOLD;
    }

  if (old_state != priv->hold_state ||
      priv->hold_state_reason != TP_LOCAL_HOLD_STATE_REASON_REQUESTED)
    {
      tp_svc_channel_interface_hold_emit_hold_state_changed (self,
          priv->hold_state, TP_LOCAL_HOLD_STATE_REASON_REQUESTED);
      priv->hold_state_reason = TP_LOCAL_HOLD_STATE_REASON_REQUESTED;
    }

  /* Tell streaming client to release or reacquire resources */

  for (i = 0; i < priv->streams->len; i++)
    {
      gabble_media_stream_hold (g_ptr_array_index (priv->streams, i), hold);
    }

  tp_svc_channel_interface_hold_return_from_request_hold (context);
}

static void
_emit_new_stream (GabbleMediaChannel *chan,
                  GabbleMediaStream *stream)
{
  gchar *object_path;
  guint id, media_type;

  g_object_get (stream,
                "object-path", &object_path,
                "id", &id,
                "media-type", &media_type,
                NULL);

  /* all of the streams are bidirectional from farsight's point of view, it's
   * just in the signalling they change */
  DEBUG ("emitting MediaSessionHandler:NewStreamHandler signal for %s stream %d ",
      media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video", id);
  tp_svc_media_session_handler_emit_new_stream_handler (chan,
      object_path, id, media_type, TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL);

  g_free (object_path);
}


static void
gabble_media_channel_ready (TpSvcMediaSessionHandler *iface,
                            DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (iface);
  GabbleMediaChannelPrivate *priv = self->priv;

  if (!priv->ready)
    {
      guint i;

      priv->ready = TRUE;

      for (i = 0; i < priv->streams->len; i++)
        _emit_new_stream (self, g_ptr_array_index (priv->streams, i));
    }

  tp_svc_media_session_handler_return_from_ready (context);
}

static void
gabble_media_channel_error (TpSvcMediaSessionHandler *iface,
                            guint errno,
                            const gchar *message,
                            DBusGMethodInvocation *context)
{
  GabbleMediaChannel *self = GABBLE_MEDIA_CHANNEL (iface);
  GabbleMediaChannelPrivate *priv;
  GPtrArray *tmp;
  guint i;
  JingleSessionState state;

  g_assert (GABBLE_IS_MEDIA_CHANNEL (self));

  priv = self->priv;

  DEBUG ("Media.SessionHandler::Error called, error %u (%s) -- "
      "emitting error on each stream", errno, message);

  /* priv->session should be valid throghout SessionHandle D-Bus object life */
  g_assert (priv->session != NULL);

  g_object_get (priv->session, "state", &state, NULL);

  if (state == JS_STATE_ENDED)
    {
      tp_svc_media_session_handler_return_from_error (context);
      return;
    }
  else if (state == JS_STATE_PENDING_CREATED)
    {
      /* shortcut to prevent sending remove actions if we haven't sent an
       * initiate yet */
      g_object_set (self, "state", JS_STATE_ENDED, NULL);
      tp_svc_media_session_handler_return_from_error (context);
      return;
    }

  g_assert (priv->streams != NULL);

  tmp = priv->streams;
  priv->streams = NULL;

  for (i = 0; i < tmp->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (tmp, i);

      gabble_media_stream_error (stream, errno, message, NULL);
    }

  g_ptr_array_free (tmp, TRUE);

  tp_svc_media_session_handler_return_from_error (context);
}


static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, gabble_media_channel_##x##suffix)
  IMPLEMENT(close,_async);
  IMPLEMENT(get_channel_type,);
  IMPLEMENT(get_handle,);
  IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}

static void
streamed_media_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelTypeStreamedMediaClass *klass =
    (TpSvcChannelTypeStreamedMediaClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_streamed_media_implement_##x (\
    klass, gabble_media_channel_##x)
  IMPLEMENT(list_streams);
  IMPLEMENT(remove_streams);
  IMPLEMENT(request_stream_direction);
  IMPLEMENT(request_streams);
#undef IMPLEMENT
}

static void
media_signalling_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceMediaSignallingClass *klass =
    (TpSvcChannelInterfaceMediaSignallingClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_media_signalling_implement_##x (\
    klass, gabble_media_channel_##x)
  IMPLEMENT(get_session_handlers);
#undef IMPLEMENT
}

static void
call_state_iface_init (gpointer g_iface,
                       gpointer iface_data G_GNUC_UNUSED)
{
  TpSvcChannelInterfaceCallStateClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_call_state_implement_##x (\
    klass, gabble_media_channel_##x)
  IMPLEMENT(get_call_states);
#undef IMPLEMENT
}

static void
hold_iface_init (gpointer g_iface,
                 gpointer iface_data G_GNUC_UNUSED)
{
  TpSvcChannelInterfaceHoldClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_hold_implement_##x (\
    klass, gabble_media_channel_##x)
  IMPLEMENT(get_hold_state);
  IMPLEMENT(request_hold);
#undef IMPLEMENT
}

static void
session_handler_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcMediaSessionHandlerClass *klass =
    (TpSvcMediaSessionHandlerClass *) g_iface;

#define IMPLEMENT(x) tp_svc_media_session_handler_implement_##x (\
    klass, gabble_media_channel_##x)
  IMPLEMENT(error);
  IMPLEMENT(ready);
#undef IMPLEMENT
}
