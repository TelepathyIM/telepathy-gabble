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

#define DBUS_API_SUBJECT_TO_CHANGE

#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "extensions/extensions.h"

#include "caps-channel-manager.h"
#include "connection.h"
#include "debug.h"
#include "jingle-factory.h"
#include "jingle-media-rtp.h"
#include "jingle-session.h"

#include "call-channel.h"
#include "media-channel.h"
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

static void
media_channel_request_succeeded_cb (MediaChannelRequest *mcr,
    GPtrArray *streams)
{
  tp_channel_manager_emit_new_channel (mcr->self,
      mcr->channel, mcr->request_tokens);

  media_channel_request_free (mcr);
}

static void
media_channel_request_failed_cb (MediaChannelRequest *mcr,
    GError *error)
{
  GSList *l;

  for (l = mcr->request_tokens; l != NULL; l = g_slist_next (l))
    tp_channel_manager_emit_request_failed (mcr->self, l->data,
      error->domain, error->code, error->message);

  media_channel_request_free (mcr);
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

  GList *media_channels;
  GList *call_channels;
  GList *pending_call_channels;
  guint channel_index;

  /* Whether or not we should use call channels for incoming calls */
  gboolean use_call_channels;

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
  g_assert (priv->media_channels == NULL);
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

/**
 * media_channel_closed_cb:
 *
 * Signal callback for when a media channel is closed. Removes the references
 * that #GabbleMediaFactory holds to them.
 */
static void
media_channel_closed_cb (GabbleMediaChannel *chan, gpointer user_data)
{
  GabbleMediaFactory *fac = GABBLE_MEDIA_FACTORY (user_data);
  GabbleMediaFactoryPrivate *priv = fac->priv;

  tp_channel_manager_emit_channel_closed_for_object (fac,
      TP_EXPORTABLE_CHANNEL (chan));

  DEBUG ("removing media channel %p with ref count %d",
      chan, G_OBJECT (chan)->ref_count);

  priv->media_channels = g_list_remove (priv->media_channels, chan);
  g_object_unref (chan);
}

/**
 * new_media_channel
 *
 * Creates a new empty GabbleMediaChannel.
 */
static GabbleMediaChannel *
new_media_channel (GabbleMediaFactory *fac,
                   GabbleJingleSession *sess,
                   TpHandle maybe_peer,
                   gboolean peer_in_rp,
                   gboolean initial_audio,
                   gboolean initial_video)
{
  GabbleMediaFactoryPrivate *priv;
  TpBaseConnection *conn;
  GabbleMediaChannel *chan;
  gchar *object_path;

  g_assert (GABBLE_IS_MEDIA_FACTORY (fac));

  priv = fac->priv;
  conn = (TpBaseConnection *) priv->conn;

  object_path = g_strdup_printf ("%s/MediaChannel%u",
      conn->object_path, priv->channel_index);
  priv->channel_index += 1;

  chan = g_object_new (GABBLE_TYPE_MEDIA_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "session", sess,
                       "initial-peer", maybe_peer,
                       "peer-in-rp", peer_in_rp,
                       "initial-audio", initial_audio,
                       "initial-video", initial_video,
                       NULL);

  DEBUG ("object path %s", object_path);

  g_signal_connect (chan, "closed", (GCallback) media_channel_closed_cb, fac);

  priv->media_channels = g_list_prepend (priv->media_channels, chan);

  g_free (object_path);

  return chan;
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
  GabbleJingleSession *sess,
  TpHandle peer,
  gboolean initial_audio,
  gboolean initial_video,
  gpointer request_token)
{
  GabbleCallChannel *channel;
  MediaChannelRequest *mcr;
  gchar *object_path;
  TpBaseConnection *conn = TP_BASE_CONNECTION (self->priv->conn);
  TpHandle initiator;

  if (sess != NULL)
    initiator = sess->peer;
  else
    initiator = conn->self_handle;

  object_path = g_strdup_printf ("%s/CallChannel%u",
    conn->object_path, self->priv->channel_index);
  self->priv->channel_index++;

  channel = g_object_new (GABBLE_TYPE_CALL_CHANNEL,
    "connection", conn,
    "object-path", object_path,
    "session", sess,
    "handle", peer,
    "initial-audio", initial_audio,
    "initial-video", initial_video,
    "requested", request_token != NULL,
    "initiator-handle", initiator,
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
  while (priv->media_channels != NULL)
    gabble_media_channel_close (
        GABBLE_MEDIA_CHANNEL (priv->media_channels->data));

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
new_jingle_session_cb (GabbleJingleFactory *jf,
    GabbleJingleSession *sess,
    gpointer data)
{
  GabbleMediaFactory *self = GABBLE_MEDIA_FACTORY (data);
  GabbleMediaFactoryPrivate *priv = self->priv;

  if (gabble_jingle_session_get_content_type (sess) !=
      GABBLE_TYPE_JINGLE_MEDIA_RTP)
    return;

  if (gabble_muc_factory_handle_jingle_session (priv->conn->muc_factory, sess))
    {
      /* Muji channel the muc factory is taking care of it */
    }
  else if (self->priv->use_call_channels)
    {
      new_call_channel (self, sess, sess->peer, FALSE, FALSE, NULL);
    }
  else
    {
      GabbleMediaChannel *chan = new_media_channel (self, sess, sess->peer,
          FALSE, FALSE, FALSE);
      GList *cs;

      /* FIXME: we need this detection to properly adjust nat-traversal on
       * the channel. We hope all contents will have the same transport... */
      cs = gabble_jingle_session_get_contents (sess);

      if (cs != NULL)
        {
          GabbleJingleContent *c = cs->data;
          gchar *ns;

          g_object_get (c, "transport-ns", &ns, NULL);

          if (!tp_strdiff (ns, NS_JINGLE_TRANSPORT_ICEUDP))
              g_object_set (chan, "nat-traversal", "ice-udp", NULL);
          else if (!tp_strdiff (ns, NS_JINGLE_TRANSPORT_RAWUDP))
              g_object_set (chan, "nat-traversal", "none", NULL);

          g_free (ns);
          g_list_free (cs);
        }

      tp_channel_manager_emit_new_channel (self,
          TP_EXPORTABLE_CHANNEL (chan), NULL);
    }
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
      g_signal_connect (priv->conn->jingle_factory, "new-session",
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

  for (l = priv->media_channels; l != NULL; l = g_list_next (l))
    foreach (TP_EXPORTABLE_CHANNEL (l->data), user_data);

  for (l = priv->call_channels; l != NULL; l = g_list_next (l))
    foreach (TP_EXPORTABLE_CHANNEL (l->data), user_data);
}

static const gchar * const media_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

/* If you change this at all, you'll probably also need to change both_allowed
 * and video_allowed */
static const gchar * const named_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialAudio",
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialVideo",
    NULL
};

static const gchar * const * both_allowed =
    named_channel_allowed_properties + 2;

static const gchar * const audio_allowed[] = {
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialAudio",
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".ImmutableStreams",
    NULL
};

static const gchar * const video_allowed[] = {
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialVideo",
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".ImmutableStreams",
    NULL
};

static const gchar * const both_allowed_immutable[] = {
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialAudio",
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialVideo",
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".ImmutableStreams",
    NULL
};

static const gchar * const call_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialAudio",
    GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialVideo",
    GABBLE_IFACE_CHANNEL_TYPE_CALL ".MutableContents",
    NULL
};

static const gchar * const call_audio_allowed[] = {
    GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialAudio",
    NULL
};

static const gchar * const call_video_allowed[] = {
    GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialVideo",
    NULL
};

static const gchar * const call_both_allowed[] = {
    GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialAudio",
    GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialVideo",
    GABBLE_IFACE_CHANNEL_TYPE_CALL ".MutableContents",
    NULL
};

static const gchar * const call_both_allowed_immutable[] = {
    GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialAudio",
    GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialVideo",
    NULL
};

/* not advertised in type_foreach_channel_class - can only be requested with
 * RequestChannel, not with CreateChannel/EnsureChannel */
static const gchar * const anon_channel_allowed_properties[] = {
    NULL
};

const gchar * const *
gabble_media_factory_call_channel_allowed_properties (void)
{
  return call_channel_allowed_properties;
}

static GHashTable *
gabble_media_factory_streamed_media_channel_class (void)
{
  GHashTable *table = tp_asv_new (
      TP_IFACE_CHANNEL ".TargetHandleType", G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
      NULL);

  tp_asv_set_static_string (table,
      TP_IFACE_CHANNEL ".ChannelType",
      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);

  return table;
}

static GHashTable *
gabble_media_factory_call_channel_class (void)
{
  GHashTable *table = tp_asv_new (
      TP_IFACE_CHANNEL ".TargetHandleType", G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
      NULL);

  tp_asv_set_static_string (table,
      TP_IFACE_CHANNEL ".ChannelType",
      GABBLE_IFACE_CHANNEL_TYPE_CALL);

  return table;
}

static void
gabble_media_factory_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = gabble_media_factory_streamed_media_channel_class ();

  func (type, table, named_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);

  table = gabble_media_factory_call_channel_class ();

  func (type, table, call_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);
}


typedef enum
{
  METHOD_REQUEST,
  METHOD_CREATE,
  METHOD_ENSURE,
} RequestMethod;

static gboolean
gabble_media_factory_requestotron (TpChannelManager *manager,
                                   gpointer request_token,
                                   GHashTable *request_properties,
                                   RequestMethod method)
{
  GabbleMediaFactory *self = GABBLE_MEDIA_FACTORY (manager);
  GabbleMediaFactoryPrivate *priv = self->priv;
  TpHandleType handle_type;
  TpHandle handle;
  GabbleMediaChannel *channel = NULL;
  GError *error = NULL;
  gboolean require_target_handle, add_peer_to_remote_pending;
  gboolean initial_audio, initial_video;

  /* Supported modes of operation:
   * - RequestChannel(None, 0):
   *     channel is anonymous;
   *     caller may optionally use AddMembers to add the peer to RemotePending
   *     without sending them any XML;
   *     caller uses RequestStreams to set the peer and start the call.
   * - RequestChannel(Contact, n) where n != 0:
   *     channel has TargetHandle=n;
   *     n is in remote pending;
   *     call is started when caller calls RequestStreams.
   * - CreateChannel({THT: Contact, TH: n}):
   *     channel has TargetHandle=n
   *     n is not in the group interface at all
   *     call is started when caller calls RequestStreams.
   * - EnsureChannel({THT: Contact, TH: n}):
   *     look for a channel whose peer is n, and return that if found with
   *       whatever properties and group membership it has;
   *     otherwise the same as into CreateChannel
   */
  switch (method)
    {
    case METHOD_REQUEST:
      require_target_handle = FALSE;
      add_peer_to_remote_pending = TRUE;
      break;
    case METHOD_CREATE:
    case METHOD_ENSURE:
      require_target_handle = TRUE;
      add_peer_to_remote_pending = FALSE;
      break;
    default:
      g_assert_not_reached ();
    }

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
    return FALSE;

  handle_type = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", NULL);

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);

  initial_audio = tp_asv_get_boolean (request_properties,
      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialAudio", NULL);
  initial_video = tp_asv_get_boolean (request_properties,
      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialVideo", NULL);

  switch (handle_type)
    {
    case TP_HANDLE_TYPE_NONE:
      /* already checked by TpBaseConnection */
      g_assert (handle == 0);

      if (require_target_handle)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "A valid Contact handle must be provided when requesting a media "
              "channel");
          goto error;
        }

      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              media_channel_fixed_properties, anon_channel_allowed_properties,
              &error))
        goto error;

      channel = new_media_channel (self, NULL, 0, FALSE, FALSE, FALSE);
      break;

    case TP_HANDLE_TYPE_CONTACT:
      /* validity already checked by TpBaseConnection */
      g_assert (handle != 0);

      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              media_channel_fixed_properties, named_channel_allowed_properties,
              &error))
        goto error;

      if (method == METHOD_ENSURE)
        {
          GList *l;
          TpHandle peer = 0;

          for (l = priv->media_channels; l != NULL; l = g_list_next (l))
            {
              channel = GABBLE_MEDIA_CHANNEL (l->data);
              g_object_get (channel, "peer", &peer, NULL);

              if (peer == handle)
                {
                  /* Per the spec, we ignore InitialAudio and InitialVideo when
                   * looking for an existing channel.
                   */
                  tp_channel_manager_emit_request_already_satisfied (self,
                      request_token, TP_EXPORTABLE_CHANNEL (channel));
                  return TRUE;
                }
            }
        }

      channel = new_media_channel (self, NULL, handle,
          add_peer_to_remote_pending, initial_audio, initial_video);
      break;
    default:
      return FALSE;
    }

  g_assert (channel != NULL);

  gabble_media_channel_request_initial_streams (channel,
      (GFunc) media_channel_request_succeeded_cb,
      (GFunc) media_channel_request_failed_cb,
      media_channel_request_new (self,
        TP_EXPORTABLE_CHANNEL (channel), request_token));

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static gboolean
gabble_media_factory_create_call (TpChannelManager *manager,
    gpointer request_token,
    GHashTable *request_properties,
    RequestMethod method)
{
  GabbleMediaFactory *self = GABBLE_MEDIA_FACTORY (manager);
  TpHandle target;
  TpBaseConnection *conn;
  GError *error = NULL;
  gboolean initial_audio, initial_video;

  conn = (TpBaseConnection *) self->priv->conn;

  DEBUG ("Creating a new call channel");

  if (tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
    return FALSE;

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              media_channel_fixed_properties, call_channel_allowed_properties,
              &error))
        goto error;

  target  = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);

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
      GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialAudio", NULL);
  initial_video = tp_asv_get_boolean (request_properties,
      GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialVideo", NULL);

  if (!initial_audio && !initial_video)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Call channel must contain at least "
          "one of InitialAudio or InitialVideo");
      goto error;
    }

  /* FIXME creating the channel should check and wait for the capabilities
   * FIXME need to cope with disconnecting while channels are setting up
   */

  new_call_channel (self, NULL, target, initial_audio, initial_video,
    request_token);

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static gboolean
gabble_media_factory_request_channel (TpChannelManager *manager,
                                      gpointer request_token,
                                      GHashTable *request_properties)
{
  return gabble_media_factory_requestotron (manager, request_token,
      request_properties, METHOD_REQUEST);
}


static gboolean
gabble_media_factory_create_channel (TpChannelManager *manager,
                                     gpointer request_token,
                                     GHashTable *request_properties)
{
  if (!tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        GABBLE_IFACE_CHANNEL_TYPE_CALL))
    return gabble_media_factory_create_call (manager, request_token,
      request_properties, METHOD_CREATE);
  else
    return gabble_media_factory_requestotron (manager, request_token,
      request_properties, METHOD_CREATE);
}


static gboolean
gabble_media_factory_ensure_channel (TpChannelManager *manager,
                                     gpointer request_token,
                                     GHashTable *request_properties)
{
  if (!tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        GABBLE_IFACE_CHANNEL_TYPE_CALL))
    return gabble_media_factory_create_call (manager, request_token,
        request_properties, METHOD_ENSURE);
  else
    return gabble_media_factory_requestotron (manager, request_token,
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
  iface->request_channel = gabble_media_factory_request_channel;
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

      /* video-v1 implies that we interop with Google Video Chat, i.e. we have
       * gtalk-p2p and H.264 as well as video */
      if (gtalk_p2p && h264)
        gabble_capability_set_add (caps, NS_GOOGLE_FEAT_VIDEO);
    }
}

/* The switch in gabble_media_factory_get_contact_caps needs to be kept in
 * sync with the possible returns from this function. */
TpChannelMediaCapabilities
_gabble_media_factory_caps_to_typeflags (const GabbleCapabilitySet *caps)
{
  TpChannelMediaCapabilities typeflags = 0;
  gboolean has_a_transport, just_google, one_media_type;

  has_a_transport = gabble_capability_set_has_one (caps,
    gabble_capabilities_get_any_transport ());

  if (has_a_transport &&
      gabble_capability_set_has_one (caps,
        gabble_capabilities_get_any_audio ()))
    typeflags |= TP_CHANNEL_MEDIA_CAPABILITY_AUDIO;

  if (has_a_transport &&
      gabble_capability_set_has_one (caps,
        gabble_capabilities_get_any_video ()))
    typeflags |= TP_CHANNEL_MEDIA_CAPABILITY_VIDEO;

  /* The checks below are an intentional asymmetry with the function going the
   * other way - we don't require the other end to advertise the GTalk-P2P
   * transport capability separately because old GTalk clients didn't do that.
   * Having Google voice implied Google session and GTalk-P2P. */

  if (gabble_capability_set_has (caps, NS_GOOGLE_FEAT_VOICE))
    typeflags |= TP_CHANNEL_MEDIA_CAPABILITY_AUDIO;

  if (gabble_capability_set_has (caps, NS_GOOGLE_FEAT_VIDEO))
    typeflags |= TP_CHANNEL_MEDIA_CAPABILITY_VIDEO;

  just_google =
      gabble_capability_set_has_one (caps,
          gabble_capabilities_get_any_google_av ()) &&
      !gabble_capability_set_has_one (caps,
          gabble_capabilities_get_any_jingle_av ());

  one_media_type = (typeflags == TP_CHANNEL_MEDIA_CAPABILITY_AUDIO)
      || (typeflags == TP_CHANNEL_MEDIA_CAPABILITY_VIDEO);

  if (just_google || one_media_type)
    typeflags |= TP_CHANNEL_MEDIA_CAPABILITY_IMMUTABLE_STREAMS;

  return typeflags;
}

void
_gabble_media_factory_typeflags_to_caps (TpChannelMediaCapabilities flags,
    GabbleCapabilitySet *caps)
{
  DEBUG ("adding Jingle caps %u", flags);

  /* The client name just appears in a debug message, so use something that
   * won't, in practice, clash with any client that uses ContactCapabilities */
  gabble_media_factory_add_caps (caps, "<legacy Capabilities>",
      (flags & TP_CHANNEL_MEDIA_CAPABILITY_AUDIO) != 0,
      (flags & TP_CHANNEL_MEDIA_CAPABILITY_VIDEO) != 0,
      (flags & TP_CHANNEL_MEDIA_CAPABILITY_NAT_TRAVERSAL_GTALK_P2P) != 0,
      (flags & TP_CHANNEL_MEDIA_CAPABILITY_NAT_TRAVERSAL_ICE_UDP) != 0,
      TRUE /* assume we have H.264 for now */);
}

static void
gabble_media_factory_reset_caps (GabbleCapsChannelManager *manager)
{
  GabbleMediaFactory *self = GABBLE_MEDIA_FACTORY (manager);

  self->priv->use_call_channels = FALSE;
}

static void
gabble_media_factory_get_contact_caps (GabbleCapsChannelManager *manager,
    TpHandle handle,
    const GabbleCapabilitySet *caps,
    GPtrArray *arr)
{
  TpChannelMediaCapabilities typeflags =
    _gabble_media_factory_caps_to_typeflags (caps);
  GValueArray *va;
  const gchar * const *streamed_media_allowed;
  const gchar * const *call_allowed;

  typeflags &= (TP_CHANNEL_MEDIA_CAPABILITY_AUDIO |
      TP_CHANNEL_MEDIA_CAPABILITY_VIDEO |
      TP_CHANNEL_MEDIA_CAPABILITY_IMMUTABLE_STREAMS);

  /* This switch is over the values of several bits from a
   * bitfield-represented-as-an-enum, simultaneously, which upsets gcc-4.5;
   * the guint cast reassures it that we know what we're doing.
   * _gabble_media_factory_caps_to_typeflags shouldn't return any cases not
   * handled here. */
  switch ((guint) typeflags)
    {
      case 0:
        return;

      case TP_CHANNEL_MEDIA_CAPABILITY_AUDIO
          | TP_CHANNEL_MEDIA_CAPABILITY_IMMUTABLE_STREAMS:
        streamed_media_allowed = audio_allowed;
        call_allowed = call_audio_allowed;
        break;

      case TP_CHANNEL_MEDIA_CAPABILITY_VIDEO
          | TP_CHANNEL_MEDIA_CAPABILITY_IMMUTABLE_STREAMS:
        streamed_media_allowed = video_allowed;
        call_allowed = call_video_allowed;
        break;

      case TP_CHANNEL_MEDIA_CAPABILITY_AUDIO
          | TP_CHANNEL_MEDIA_CAPABILITY_VIDEO: /* both */
        streamed_media_allowed = both_allowed;
        call_allowed = call_both_allowed;
        break;

      case TP_CHANNEL_MEDIA_CAPABILITY_AUDIO /* both but immutable */
          | TP_CHANNEL_MEDIA_CAPABILITY_VIDEO
          | TP_CHANNEL_MEDIA_CAPABILITY_IMMUTABLE_STREAMS:
        streamed_media_allowed = both_allowed_immutable;
        call_allowed = call_both_allowed_immutable;
        break;

      default:
        g_assert_not_reached ();
    }

  /* Streamed Media channel */
  va = g_value_array_new (2);
  g_value_array_append (va, NULL);
  g_value_array_append (va, NULL);
  g_value_init (va->values + 0, TP_HASH_TYPE_CHANNEL_CLASS);
  g_value_init (va->values + 1, G_TYPE_STRV);
  g_value_take_boxed (va->values + 0,
    gabble_media_factory_streamed_media_channel_class ());
  g_value_set_static_boxed (va->values + 1, streamed_media_allowed);

  g_ptr_array_add (arr, va);

  /* Call channel */
  va = g_value_array_new (2);
  g_value_array_append (va, NULL);
  g_value_array_append (va, NULL);
  g_value_init (va->values + 0, TP_HASH_TYPE_CHANNEL_CLASS);
  g_value_init (va->values + 1, G_TYPE_STRV);
  g_value_take_boxed (va->values + 0,
    gabble_media_factory_call_channel_class ());
  g_value_set_static_boxed (va->values + 1, call_allowed);

  g_ptr_array_add (arr, va);
}

static void
gabble_media_factory_represent_client (GabbleCapsChannelManager *manager,
    const gchar *client_name,
    const GPtrArray *filters,
    const gchar * const *cap_tokens,
    GabbleCapabilitySet *cap_set)
{
  static GQuark q_gtalk_p2p = 0, q_ice_udp = 0, q_h264 = 0;
  static GQuark qc_gtalk_p2p = 0, qc_ice_udp = 0, qc_h264 = 0;
  gboolean gtalk_p2p = FALSE, h264 = FALSE, audio = FALSE, video = FALSE,
           ice_udp = FALSE;
  guint i;

  /* One-time initialization - turn the tokens we care about into quarks */
  if (G_UNLIKELY (q_gtalk_p2p == 0))
    {
      q_gtalk_p2p = g_quark_from_static_string (
          TP_IFACE_CHANNEL_INTERFACE_MEDIA_SIGNALLING "/gtalk-p2p");
      qc_gtalk_p2p = g_quark_from_static_string (
          GABBLE_IFACE_CHANNEL_TYPE_CALL "/gtalk-p2p");
      q_ice_udp = g_quark_from_static_string (
          TP_IFACE_CHANNEL_INTERFACE_MEDIA_SIGNALLING "/ice-udp");
      qc_ice_udp = g_quark_from_static_string (
          GABBLE_IFACE_CHANNEL_TYPE_CALL "/ice-udp");
      q_h264 = g_quark_from_static_string (
          TP_IFACE_CHANNEL_INTERFACE_MEDIA_SIGNALLING "/video/h264");
      qc_h264 = g_quark_from_static_string (
          GABBLE_IFACE_CHANNEL_TYPE_CALL "/video/h264");
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
              { q_gtalk_p2p, &gtalk_p2p }, { qc_gtalk_p2p, &gtalk_p2p },
              { q_ice_udp, &ice_udp }, { qc_ice_udp, &ice_udp },
              { q_h264, &h264 }, { qc_h264, &h264 },
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
              TP_IFACE_CHANNEL ".ChannelType"),
            TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA)
          && tp_strdiff (tp_asv_get_string (filter,
              TP_IFACE_CHANNEL ".ChannelType"),
            GABBLE_IFACE_CHANNEL_TYPE_CALL))
        {
          /* not interesting to this channel manager */
          continue;
        }

#ifdef ENABLE_CHANNEL_TYPE_CALL
      /* If there is a handler that can support Call channels, use those for
       * incoming channels */
      if (!tp_strdiff (tp_asv_get_string (filter,
            TP_IFACE_CHANNEL ".ChannelType"),
            GABBLE_IFACE_CHANNEL_TYPE_CALL))
        {
          GabbleMediaFactory *self = GABBLE_MEDIA_FACTORY (manager);
          self->priv->use_call_channels = TRUE;
        }
#endif

      if (tp_asv_lookup (filter, TP_IFACE_CHANNEL ".TargetHandleType")
          != NULL &&
          tp_asv_get_uint32 (filter, TP_IFACE_CHANNEL ".TargetHandleType",
            NULL) != TP_HANDLE_TYPE_CONTACT)
        {
          /* not interesting to this channel manager: we only care about
           * Jingle calls involving contacts (or about clients that support
           * all Jingle calls regardless of handle type) */
          continue;
        }

      if (tp_asv_get_boolean (filter,
            TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialAudio", NULL)
          || tp_asv_get_boolean (filter,
            GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialAudio", NULL))
        audio = TRUE;

      if (tp_asv_get_boolean (filter,
            TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialVideo", NULL)
          || tp_asv_get_boolean (filter,
            GABBLE_IFACE_CHANNEL_TYPE_CALL ".InitialVideo", NULL))
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
  GabbleCapsChannelManagerIface *iface = g_iface;

  iface->reset_caps = gabble_media_factory_reset_caps;
  iface->get_contact_caps = gabble_media_factory_get_contact_caps;
  iface->represent_client = gabble_media_factory_represent_client;
}
