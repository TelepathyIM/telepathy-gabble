/*
 * media-factory.c - Source for GabbleMediaFactory
 * Copyright (C) 2006 Collabora Ltd.
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
#include "media-factory.h"

#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <wocky/wocky.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "gabble/caps-channel-manager.h"
#include "connection.h"
#include "debug.h"

#include "call-channel.h"
#include "namespaces.h"
#include "util.h"

typedef struct
{
    GabbleMediaFactory *self;
    TpExportableChannel *channel;
    GSList *request_tokens;
} MediaChannelRequest;

static MediaChannelRequest *
media_channel_request_new (GabbleMediaFactory *self,
    TpExportableChannel *channel,
    gpointer request_token)
{
  MediaChannelRequest *mcr = g_slice_new0 (MediaChannelRequest);

  mcr->self = self;
  mcr->channel = g_object_ref (channel);
  if (request_token != NULL)
    mcr->request_tokens = g_slist_prepend (mcr->request_tokens, request_token);

  return mcr;
}

static void
media_channel_request_free (MediaChannelRequest *mcr)
{
  g_object_unref (mcr->channel);
  g_slist_free (mcr->request_tokens);
  g_slice_free (MediaChannelRequest, mcr);
}

static void channel_manager_iface_init (gpointer, gpointer);
static void caps_channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleMediaFactory, gabble_media_factory,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init);
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER,
      caps_channel_manager_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

struct _GabbleMediaFactoryPrivate
{
  GabbleConnection *conn;
  gulong status_changed_id;

  GList *call_channels;
  GList *pending_call_channels;
  guint channel_index;

  gboolean dispose_has_run;
};

static void
gabble_media_factory_init (GabbleMediaFactory *fac)
{
  GabbleMediaFactoryPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (fac,
      GABBLE_TYPE_MEDIA_FACTORY, GabbleMediaFactoryPrivate);

  fac->priv = priv;

  priv->channel_index = 0;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}


static void gabble_media_factory_close_all (GabbleMediaFactory *fac);


static void
gabble_media_factory_dispose (GObject *object)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (object);
  GabbleMediaFactoryPrivate *priv = fac->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  gabble_media_factory_close_all (fac);
  g_assert (priv->call_channels == NULL);

  if (G_OBJECT_CLASS (gabble_media_factory_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_factory_parent_class)->dispose (object);
}

static void
gabble_media_factory_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (object);
  GabbleMediaFactoryPrivate *priv = fac->priv;

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_media_factory_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (object);
  GabbleMediaFactoryPrivate *priv = fac->priv;

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void gabble_media_factory_constructed (GObject *object);


static void
gabble_media_factory_class_init (GabbleMediaFactoryClass *gabble_media_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_factory_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_media_factory_class,
      sizeof (GabbleMediaFactoryPrivate));

  object_class->constructed = gabble_media_factory_constructed;
  object_class->dispose = gabble_media_factory_dispose;

  object_class->get_property = gabble_media_factory_get_property;
  object_class->set_property = gabble_media_factory_set_property;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
      "Gabble connection object that owns this media channel manager object.",
      GABBLE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

}

static void
call_channel_closed_cb (GabbleCallChannel *chan, gpointer user_data)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (user_data);
  GabbleMediaFactoryPrivate *priv = fac->priv;

  tp_channel_manager_emit_channel_closed_for_object (fac,
      TP_EXPORTABLE_CHANNEL (chan));

  DEBUG ("removing media channel %p with ref count %d",
      chan, G_OBJECT (chan)->ref_count);

  priv->call_channels = g_list_remove (priv->call_channels, chan);
  g_object_unref (chan);
}

static void
call_channel_initialized (GObject *source,
  GAsyncResult *res,
  gpointer user_data)
{
  MediaChannelRequest *mcr = user_data;
  GabbleMediaFactoryPrivate *priv = mcr->self->priv;
  GError *error = NULL;

  priv->pending_call_channels =
    g_list_remove (priv->pending_call_channels, mcr);

  if (g_async_initable_init_finish (G_ASYNC_INITABLE (source),
      res, &error))
    {
      priv->call_channels = g_list_prepend (priv->call_channels,
          g_object_ref (mcr->channel));

      tp_channel_manager_emit_new_channel (mcr->self,
        mcr->channel, mcr->request_tokens);

      g_signal_connect (mcr->channel, "closed",
        G_CALLBACK (call_channel_closed_cb), mcr->self);
    }
  else
    {
      GSList *l;
      for (l = mcr->request_tokens; l != NULL; l = g_slist_next (l))
        tp_channel_manager_emit_request_failed (mcr->self, l->data,
          error->domain, error->code, error->message);
    }

  media_channel_request_free (mcr);
}

/**
 * new_call_channel
 *
 * Creates and triggers initialisation of a new empty GabbleCallChannel.
 */
static void
new_call_channel (GabbleMediaFactory *self,
  WockyJingleSession *sess,
  TpHandle peer,
  gboolean initial_audio,
  const gchar *initial_audio_name,
  gboolean initial_video,
  const gchar *initial_video_name,
  gpointer request_token)
{
  GabbleCallChannel *channel;
  MediaChannelRequest *mcr;
  gchar *object_path;
  TpBaseConnection *conn = TP_BASE_CONNECTION (self->priv->conn);
  TpHandle initiator;

  if (sess != NULL)
    initiator = peer;
  else
    initiator = tp_base_connection_get_self_handle (conn);

  object_path = g_strdup_printf ("%s/CallChannel%u",
    tp_base_connection_get_object_path (conn), self->priv->channel_index);
  self->priv->channel_index++;

  channel = g_object_new (GABBLE_TYPE_CALL_CHANNEL,
    "connection", conn,
    "object-path", object_path,
    "session", sess,
    "handle", peer,
    "initial-audio", initial_audio,
    "initial-audio-name",
        initial_audio_name != NULL ? initial_audio_name : "audio",
    "initial-video", initial_video,
    "initial-video-name",
        initial_video_name != NULL ? initial_video_name : "video",
    "requested", request_token != NULL,
    "initiator-handle", initiator,
    "mutable-contents", TRUE,
    NULL);

  g_free (object_path);

  mcr = media_channel_request_new (self,
    TP_EXPORTABLE_CHANNEL (channel), request_token);

  g_async_initable_init_async (G_ASYNC_INITABLE (channel),
    G_PRIORITY_DEFAULT,
    NULL, /* FIXME support cancelling the channel creation */
    call_channel_initialized,
    mcr);

  self->priv->pending_call_channels
    = g_list_prepend (self->priv->pending_call_channels, mcr);

  g_object_unref (channel);
}

static void
gabble_media_factory_close_all (GabbleMediaFactory *fac)
{
  GabbleMediaFactoryPrivate *priv = fac->priv;

  DEBUG ("closing channels");

  /* Close will cause the channel to be removed from the list indirectly..*/
  while (priv->call_channels != NULL)
    tp_base_channel_close (TP_BASE_CHANNEL (priv->call_channels->data));

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->conn,
          priv->status_changed_id);
      priv->status_changed_id = 0;
    }
}

static void
new_jingle_session_cb (GabbleJingleMint *jm,
    WockyJingleSession *sess,
    gpointer data)
{
  GabbleMediaFactory *self = GABBLE_MEDIA_FACTORY (data);
  GabbleMediaFactoryPrivate *priv = self->priv;
  TpHandleRepoIface *contacts;
  TpHandle peer;

  if (wocky_jingle_session_get_content_type (sess) !=
      WOCKY_TYPE_JINGLE_MEDIA_RTP)
    return;

  if (gabble_muc_factory_handle_jingle_session (priv->conn->muc_factory, sess))
    {
      /* Muji channel the muc factory is taking care of it */
      return;
    }

  contacts = tp_base_connection_get_handles (TP_BASE_CONNECTION (priv->conn),
      TP_HANDLE_TYPE_CONTACT);
  peer = tp_handle_ensure (contacts, wocky_jingle_session_get_peer_jid (sess),
      NULL, NULL);

  new_call_channel (self, sess, peer,
      FALSE, NULL,
      FALSE, NULL,
      NULL);
}

static void
connection_status_changed_cb (GabbleConnection *conn,
                              guint status,
                              guint reason,
                              GabbleMediaFactory *self)
{
  GabbleMediaFactoryPrivate *priv = self->priv;

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      g_signal_connect (priv->conn->jingle_mint, "incoming-session",
          G_CALLBACK (new_jingle_session_cb), self);
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      gabble_media_factory_close_all (self);
      break;
    }
}


static void
gabble_media_factory_constructed (GObject *object)
{
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (gabble_media_factory_parent_class)->constructed;
  GabbleMediaFactory *self = GABBLE_MEDIA_FACTORY (object);
  GabbleMediaFactoryPrivate *priv = self->priv;

  if (chain_up != NULL)
    chain_up (object);

  priv->status_changed_id = g_signal_connect (priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, object);
}


static void
gabble_media_factory_foreach_channel (TpChannelManager *manager,
                                      TpExportableChannelFunc foreach,
                                      gpointer user_data)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (manager);
  GabbleMediaFactoryPrivate *priv = fac->priv;
  GList *l;

  for (l = priv->call_channels; l != NULL; l = g_list_next (l))
    foreach (TP_EXPORTABLE_CHANNEL (l->data), user_data);
}

static const gchar * const media_channel_fixed_properties[] = {
    TP_PROP_CHANNEL_CHANNEL_TYPE,
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
    NULL
};

static const gchar * const call_channel_allowed_properties[] = {
    TP_PROP_CHANNEL_TARGET_HANDLE,
    TP_PROP_CHANNEL_TARGET_ID,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO_NAME,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO_NAME,
    TP_PROP_CHANNEL_TYPE_CALL_MUTABLE_CONTENTS,
    NULL
};

static const gchar * const call_audio_allowed[] = {
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO_NAME,
    NULL
};

static const gchar * const call_video_allowed[] = {
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO_NAME,
    NULL
};

static const gchar * const call_both_allowed[] = {
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO_NAME,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO_NAME,
    TP_PROP_CHANNEL_TYPE_CALL_MUTABLE_CONTENTS,
    NULL
};

static const gchar * const call_both_allowed_immutable[] = {
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO_NAME,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO_NAME,
    NULL
};

const gchar * const *
gabble_media_factory_call_channel_allowed_properties (void)
{
  return call_channel_allowed_properties;
}

static GHashTable *
gabble_media_factory_call_channel_class (void)
{
  GHashTable *table = tp_asv_new (
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
      NULL);

  tp_asv_set_static_string (table, TP_PROP_CHANNEL_CHANNEL_TYPE,
      TP_IFACE_CHANNEL_TYPE_CALL);

  return table;
}

static void
gabble_media_factory_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = gabble_media_factory_call_channel_class ();

  func (type, table, call_channel_allowed_properties, user_data);

  g_hash_table_unref (table);
}


typedef enum
{
  METHOD_CREATE,
  METHOD_ENSURE,
} RequestMethod;

static gboolean
gabble_media_factory_create_call (TpChannelManager *manager,
    gpointer request_token,
    GHashTable *request_properties,
    RequestMethod method)
{
  GabbleMediaFactory *self = GABBLE_MEDIA_FACTORY (manager);
  TpHandle target;
  GError *error = NULL;
  gboolean initial_audio, initial_video;
  const gchar *initial_audio_name, *initial_video_name;

  g_return_val_if_fail (request_properties != NULL, FALSE);

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_PROP_CHANNEL_CHANNEL_TYPE),
        TP_IFACE_CHANNEL_TYPE_CALL))
    return FALSE;

  DEBUG ("Creating a new call channel");

  if (tp_asv_get_uint32 (request_properties,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL) != TP_HANDLE_TYPE_CONTACT)
    return FALSE;

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              media_channel_fixed_properties, call_channel_allowed_properties,
              &error))
        goto error;

  target  = tp_asv_get_uint32 (request_properties,
      TP_PROP_CHANNEL_TARGET_HANDLE, NULL);

  if (method == METHOD_ENSURE)
    {
      GList *l;
      TpHandle handle = 0;

      for (l = self->priv->call_channels; l != NULL; l = g_list_next (l))
        {
          GabbleCallChannel *channel = GABBLE_CALL_CHANNEL (l->data);
          g_object_get (channel, "handle", &handle, NULL);

          if (handle == target)
            {
              /* Per the spec, we ignore InitialAudio and InitialVideo when
               * looking for an existing channel.
               */
              tp_channel_manager_emit_request_already_satisfied (self,
                  request_token, TP_EXPORTABLE_CHANNEL (channel));
              return TRUE;
            }
        }

      for (l = self->priv->pending_call_channels;
          l != NULL; l = g_list_next (l))
        {
          MediaChannelRequest *mcr = (MediaChannelRequest *) l->data;
          g_object_get (mcr->channel, "handle", &handle, NULL);

          if (handle == target)
            {
              /* Per the spec, we ignore InitialAudio and InitialVideo when
               * looking for an existing channel.
               */
              mcr->request_tokens = g_slist_prepend (mcr->request_tokens,
                  request_token);
              return TRUE;
            }
        }
    }

  initial_audio = tp_asv_get_boolean (request_properties,
      TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO, NULL);
  initial_video = tp_asv_get_boolean (request_properties,
      TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO, NULL);

  if (!initial_audio && !initial_video)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Call channel must contain at least "
          "one of InitialAudio or InitialVideo");
      goto error;
    }

  /* FIXME creating the channel should check and wait for the capabilities
   * FIXME need to cope with disconnecting while channels are setting up
   */

  initial_audio_name = tp_asv_get_string (request_properties,
      TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO_NAME);
  initial_video_name = tp_asv_get_string (request_properties,
      TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO_NAME);

  new_call_channel (self, NULL, target,
    initial_audio, initial_audio_name,
    initial_video, initial_video_name,
    request_token);

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static gboolean
gabble_media_factory_create_channel (TpChannelManager *manager,
                                     gpointer request_token,
                                     GHashTable *request_properties)
{
  return gabble_media_factory_create_call (manager, request_token,
      request_properties, METHOD_CREATE);
}


static gboolean
gabble_media_factory_ensure_channel (TpChannelManager *manager,
                                     gpointer request_token,
                                     GHashTable *request_properties)
{
  return gabble_media_factory_create_call (manager, request_token,
      request_properties, METHOD_ENSURE);
}


static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = gabble_media_factory_foreach_channel;
  iface->type_foreach_channel_class =
      gabble_media_factory_type_foreach_channel_class;
  iface->create_channel = gabble_media_factory_create_channel;
  iface->ensure_channel = gabble_media_factory_ensure_channel;
}

static void
gabble_media_factory_add_caps (GabbleCapabilitySet *caps,
    const gchar *client_name,
    gboolean audio,
    gboolean video,
    gboolean gtalk_p2p,
    gboolean ice_udp,
    gboolean h264)
{
  gboolean any_content = audio || video;

  DEBUG ("Client %s media capabilities:%s%s%s%s%s",
      client_name,
      audio ? " audio" : "",
      video ? " video" : "",
      gtalk_p2p ? " gtalk-p2p" : "",
      ice_udp ? " ice-udp" : "",
      h264 ? " H.264" : "");

  if (gtalk_p2p && any_content)
    gabble_capability_set_add (caps, NS_GOOGLE_TRANSPORT_P2P);

  if (ice_udp && any_content)
    gabble_capability_set_add (caps, NS_JINGLE_TRANSPORT_ICEUDP);

  if (audio)
    {
      gabble_capability_set_add (caps, NS_JINGLE_RTP);
      gabble_capability_set_add (caps, NS_JINGLE_RTP_AUDIO);
      gabble_capability_set_add (caps, NS_JINGLE_DESCRIPTION_AUDIO);
      gabble_capability_set_add (caps, NS_JINGLE_RTP_HDREXT);
      gabble_capability_set_add (caps, NS_JINGLE_RTCP_FB);

      /* voice-v1 implies that we interop with GTalk, i.e. we have gtalk-p2p
       * as well as audio */
      if (gtalk_p2p)
        gabble_capability_set_add (caps, NS_GOOGLE_FEAT_VOICE);
    }

  if (video)
    {
      gabble_capability_set_add (caps, NS_JINGLE_RTP);
      gabble_capability_set_add (caps, NS_JINGLE_RTP_VIDEO);
      gabble_capability_set_add (caps, NS_JINGLE_DESCRIPTION_VIDEO);
      gabble_capability_set_add (caps, NS_JINGLE_RTP_HDREXT);
      gabble_capability_set_add (caps, NS_JINGLE_RTCP_FB);

      /* video-v1 implies that we interop with Google Video Chat, i.e. we have
       * gtalk-p2p and H.264 as well as video */
      if (gtalk_p2p && h264)
        {
          gabble_capability_set_add (caps, NS_GOOGLE_FEAT_VIDEO);
          gabble_capability_set_add (caps, NS_GOOGLE_FEAT_CAMERA);
        }
    }
}

typedef enum {
  MEDIA_CAPABILITY_AUDIO = 1,
  MEDIA_CAPABILITY_VIDEO = 2,
  MEDIA_CAPABILITY_NAT_TRAVERSAL_STUN = 4,
  MEDIA_CAPABILITY_NAT_TRAVERSAL_GTALK_P2P = 8,
  MEDIA_CAPABILITY_NAT_TRAVERSAL_ICE_UDP = 16,
  MEDIA_CAPABILITY_IMMUTABLE_STREAMS = 32,
} MediaCapabilities;

/* The switch in gabble_media_factory_get_contact_caps needs to be kept in
 * sync with the possible returns from this function. */
static MediaCapabilities
_gabble_media_factory_caps_to_typeflags (const GabbleCapabilitySet *caps)
{
  MediaCapabilities typeflags = 0;
  gboolean has_a_transport, just_google, one_media_type;

  has_a_transport = gabble_capability_set_has_one (caps,
    gabble_capabilities_get_any_transport ());

  if (has_a_transport &&
      gabble_capability_set_has_one (caps,
        gabble_capabilities_get_any_audio ()))
    typeflags |= MEDIA_CAPABILITY_AUDIO;

  if (has_a_transport &&
      gabble_capability_set_has_one (caps,
        gabble_capabilities_get_any_video ()))
    typeflags |= MEDIA_CAPABILITY_VIDEO;

  /* The checks below are an intentional asymmetry with the function going the
   * other way - we don't require the other end to advertise the GTalk-P2P
   * transport capability separately because old GTalk clients didn't do that.
   * Having Google voice implied Google session and GTalk-P2P. */

  if (gabble_capability_set_has (caps, NS_GOOGLE_FEAT_VOICE))
    typeflags |= MEDIA_CAPABILITY_AUDIO;

  if (gabble_capability_set_has (caps, NS_GOOGLE_FEAT_VIDEO))
    typeflags |= MEDIA_CAPABILITY_VIDEO;

  just_google =
      gabble_capability_set_has_one (caps,
          gabble_capabilities_get_any_google_av ()) &&
      !gabble_capability_set_has_one (caps,
          gabble_capabilities_get_any_jingle_av ());

  one_media_type = (typeflags == MEDIA_CAPABILITY_AUDIO)
      || (typeflags == MEDIA_CAPABILITY_VIDEO);

  if (just_google || one_media_type)
    typeflags |= MEDIA_CAPABILITY_IMMUTABLE_STREAMS;

  return typeflags;
}

static void
gabble_media_factory_get_contact_caps (GabbleCapsChannelManager *manager,
    TpHandle handle,
    const GabbleCapabilitySet *caps,
    GPtrArray *arr)
{
  MediaCapabilities typeflags =
    _gabble_media_factory_caps_to_typeflags (caps);
  const gchar * const *call_allowed;

  typeflags &= (MEDIA_CAPABILITY_AUDIO |
      MEDIA_CAPABILITY_VIDEO |
      MEDIA_CAPABILITY_IMMUTABLE_STREAMS);

  /* This switch is over the values of several bits from a
   * bitfield-represented-as-an-enum, simultaneously, which upsets gcc-4.5;
   * the guint cast reassures it that we know what we're doing.
   * _gabble_media_factory_caps_to_typeflags shouldn't return any cases not
   * handled here. */
  switch ((guint) typeflags)
    {
      case 0:
        return;

      case MEDIA_CAPABILITY_AUDIO
          | MEDIA_CAPABILITY_IMMUTABLE_STREAMS:
        call_allowed = call_audio_allowed;
        break;

      case MEDIA_CAPABILITY_VIDEO
          | MEDIA_CAPABILITY_IMMUTABLE_STREAMS:
        call_allowed = call_video_allowed;
        break;

      case MEDIA_CAPABILITY_AUDIO
          | MEDIA_CAPABILITY_VIDEO: /* both */
        call_allowed = call_both_allowed;
        break;

      case MEDIA_CAPABILITY_AUDIO /* both but immutable */
          | MEDIA_CAPABILITY_VIDEO
          | MEDIA_CAPABILITY_IMMUTABLE_STREAMS:
        call_allowed = call_both_allowed_immutable;
        break;

      default:
        g_assert_not_reached ();
    }

  /* Call channel */
  g_ptr_array_add (arr,
      tp_value_array_build (2,
        TP_HASH_TYPE_CHANNEL_CLASS, gabble_media_factory_call_channel_class (),
        G_TYPE_STRV, call_allowed,
        G_TYPE_INVALID));
}

static void
gabble_media_factory_represent_client (GabbleCapsChannelManager *manager,
    const gchar *client_name,
    const GPtrArray *filters,
    const gchar * const *cap_tokens,
    GabbleCapabilitySet *cap_set,
    GPtrArray *data_forms)
{
  static GQuark qc_gtalk_p2p = 0, qc_ice_udp = 0, qc_h264 = 0, qc_ice = 0;
  gboolean gtalk_p2p = FALSE, h264 = FALSE, audio = FALSE, video = FALSE,
           ice_udp = FALSE;
  guint i;

  /* One-time initialization - turn the tokens we care about into quarks */
  if (G_UNLIKELY (qc_gtalk_p2p == 0))
    {
      qc_gtalk_p2p = g_quark_from_static_string (
          TP_TOKEN_CHANNEL_TYPE_CALL_GTALK_P2P);
      qc_ice = g_quark_from_static_string (
          TP_TOKEN_CHANNEL_TYPE_CALL_ICE);
      /* 'ice-udp' isn't the proper cap name, 'ice' is. We keep supporting
       * 'ice-udp' for now to not break existing clients. */
      qc_ice_udp = g_quark_from_static_string (
          TP_IFACE_CHANNEL_TYPE_CALL "/ice-udp");
      qc_h264 = g_quark_from_static_string (
          TP_IFACE_CHANNEL_TYPE_CALL "/video/h264");
    }

  if (cap_tokens != NULL)
    {
      const gchar * const *token;

      for (token = cap_tokens; *token != NULL; token++)
        {
          GQuark quark = g_quark_try_string (*token);
          struct {
            GQuark quark;
            gboolean *cap;
          } q2cap[] = {
              { qc_gtalk_p2p, &gtalk_p2p },
              { qc_ice_udp, &ice_udp },
              { qc_ice, &ice_udp },
              { qc_h264, &h264 },
              { 0, NULL },
          };

          for (i = 0; q2cap[i].quark != 0; i++)
            {
              if (quark == q2cap[i].quark)
                *(q2cap[i].cap) = TRUE;
            }
        }
    }

  for (i = 0; i < filters->len; i++)
    {
      GHashTable *filter = g_ptr_array_index (filters, i);

      if (tp_asv_size (filter) == 0)
        {
          /* a client that claims to be able to do absolutely everything can
           * presumably do audio, video, smell, etc. etc. */
          audio = TRUE;
          video = TRUE;
          continue;
        }

      if (tp_strdiff (tp_asv_get_string (filter,
              TP_PROP_CHANNEL_CHANNEL_TYPE),
            TP_IFACE_CHANNEL_TYPE_CALL))
        {
          /* not interesting to this channel manager */
          continue;
        }

      if (tp_asv_lookup (filter, TP_PROP_CHANNEL_TARGET_HANDLE_TYPE)
          != NULL &&
          tp_asv_get_uint32 (filter, TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
            NULL) != TP_HANDLE_TYPE_CONTACT)
        {
          /* not interesting to this channel manager: we only care about
           * Jingle calls involving contacts (or about clients that support
           * all Jingle calls regardless of handle type) */
          continue;
        }

      if (tp_asv_get_boolean (filter,
            TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO, NULL))
        audio = TRUE;

      if (tp_asv_get_boolean (filter,
            TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO, NULL))
        video = TRUE;

      /* If we've picked up all the capabilities we're ever going to, then
       * we don't need to look at the rest of the filters */
      if (audio && video)
        break;
    }

  gabble_media_factory_add_caps (cap_set, client_name, audio, video, gtalk_p2p,
      ice_udp, h264);
}

static void
caps_channel_manager_iface_init (gpointer g_iface,
                                 gpointer iface_data)
{
  GabbleCapsChannelManagerInterface *iface = g_iface;

  iface->get_contact_caps = gabble_media_factory_get_contact_caps;
  iface->represent_client = gabble_media_factory_represent_client;
}
