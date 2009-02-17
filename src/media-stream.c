/*
 * gabble-media-stream.c - Source for GabbleMediaStream
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
#include "media-stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/debug-ansi.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/svc-media-interfaces.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "connection.h"
#include "debug.h"
#include "gabble-signals-marshal.h"
#include "jingle-content.h"
#include "jingle-session.h"
#include "jingle-media-rtp.h"
#include "media-channel.h"
#include "namespaces.h"

static void stream_handler_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(GabbleMediaStream,
    gabble_media_stream,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_MEDIA_STREAM_HANDLER,
      stream_handler_iface_init)
    )

/* signal enum */
enum
{
    DESTROY,
    NEW_ACTIVE_CANDIDATE_PAIR,
    NEW_NATIVE_CANDIDATE,
    SUPPORTED_CODECS,
    ERROR,
    UNHOLD_FAILED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_MEDIA_SESSION,
  PROP_OBJECT_PATH,
  PROP_NAME,
  PROP_ID,
  PROP_MEDIA_TYPE,
  PROP_CONNECTION_STATE,
  PROP_READY,
  PROP_PLAYING,
  PROP_COMBINED_DIRECTION,
  PROP_LOCAL_HOLD,
  PROP_CONTENT,
  LAST_PROPERTY
};

/* private structure */

struct _GabbleMediaStreamPrivate
{
  GabbleJingleContent *content;

  GabbleConnection *conn;
  gpointer session;
  GabbleMediaSessionMode mode;
  gchar *object_path;
  guint id;
  guint media_type;

  GValue native_codecs;     /* intersected codec list */
  GValue native_candidates;

  GValue remote_codecs;
  GValue remote_candidates;

  guint remote_candidate_count;

  /* signal handler ID for content REMOVED signal */
  gulong removed_id;

  /* source ID for initial codecs/candidates getter */
  gulong initial_getter_id;

  /* These are really booleans, but gboolean is signed. Thanks, GLib */
  unsigned closed:1;
  unsigned dispose_has_run:1;
  unsigned local_hold:1;
  unsigned ready:1;
  unsigned sending:1;
};

#define GABBLE_MEDIA_STREAM_GET_PRIVATE(obj) ((obj)->priv)

static void push_remote_codecs (GabbleMediaStream *stream);
static void push_remote_candidates (GabbleMediaStream *stream);
static void push_playing (GabbleMediaStream *stream);
static void push_sending (GabbleMediaStream *stream);

static void new_remote_candidates_cb (GabbleJingleContent *content,
    GList *clist, GabbleMediaStream *stream);
static void new_remote_codecs_cb (GabbleJingleContent *content,
    GList *clist, GabbleMediaStream *stream);
static void content_state_changed_cb (GabbleJingleContent *c,
     GParamSpec *pspec, GabbleMediaStream *stream);
static void content_senders_changed_cb (GabbleJingleContent *c,
     GParamSpec *pspec, GabbleMediaStream *stream);
static void content_removed_cb (GabbleJingleContent *content,
      GabbleMediaStream *stream);
static void update_direction (GabbleMediaStream *stream, GabbleJingleContent *c);
static void update_sending (GabbleMediaStream *stream, gboolean start_sending);

static void
gabble_media_stream_init (GabbleMediaStream *self)
{
  GabbleMediaStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_MEDIA_STREAM, GabbleMediaStreamPrivate);
  GType candidate_list_type =
      TP_ARRAY_TYPE_MEDIA_STREAM_HANDLER_CANDIDATE_LIST;
  GType codec_list_type =
      TP_ARRAY_TYPE_MEDIA_STREAM_HANDLER_CODEC_LIST;

  self->priv = priv;

  g_value_init (&priv->native_codecs, codec_list_type);
  g_value_take_boxed (&priv->native_codecs,
      dbus_g_type_specialized_construct (codec_list_type));

  g_value_init (&priv->native_candidates, candidate_list_type);
  g_value_take_boxed (&priv->native_candidates,
      dbus_g_type_specialized_construct (candidate_list_type));

  g_value_init (&priv->remote_codecs, codec_list_type);
  g_value_take_boxed (&priv->remote_codecs,
      dbus_g_type_specialized_construct (codec_list_type));

  g_value_init (&priv->remote_candidates, candidate_list_type);
  g_value_take_boxed (&priv->remote_candidates,
      dbus_g_type_specialized_construct (candidate_list_type));
}

static gboolean
_get_initial_codecs_and_candidates (gpointer user_data)
{
  GabbleMediaStream *stream = GABBLE_MEDIA_STREAM (user_data);
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  priv->initial_getter_id = 0;

  /* we can immediately get the codecs if we're responder */
  new_remote_codecs_cb (priv->content,
      gabble_jingle_media_rtp_get_remote_codecs (GABBLE_JINGLE_MEDIA_RTP (priv->content)),
      stream);

  /* if any candidates arrived before idle loop had the chance to excute
   * us (e.g. specified in session-initiate/content-add), we don't want to
   * miss them */
  new_remote_candidates_cb (priv->content,
      gabble_jingle_content_get_remote_candidates (priv->content), stream);

  return FALSE;
}

static GObject *
gabble_media_stream_constructor (GType type, guint n_props,
                                 GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMediaStream *stream;
  GabbleMediaStreamPrivate *priv;
  DBusGConnection *bus;

  /* call base class constructor */
  obj = G_OBJECT_CLASS (gabble_media_stream_parent_class)->
           constructor (type, n_props, props);
  stream = GABBLE_MEDIA_STREAM (obj);
  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  /* go for the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  if (priv->content != NULL)
    {
      update_direction (stream, priv->content);

      /* MediaStream is created as soon as GabbleJingleContent is
       * created, but we want to let it parse the initiation (if
       * initiated by remote end) before we pick up initial
       * codecs and candidates.
       * FIXME: add API for ordering IQs rather than using g_idle_add.
       */
      priv->initial_getter_id =
          g_idle_add (_get_initial_codecs_and_candidates, stream);
    }

  return obj;
}

static void
gabble_media_stream_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  GabbleMediaStream *stream = GABBLE_MEDIA_STREAM (object);
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_MEDIA_SESSION:
      g_value_set_object (value, priv->session);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_NAME:
      g_value_set_string (value, stream->name);
      break;
    case PROP_ID:
      g_value_set_uint (value, priv->id);
      break;
    case PROP_MEDIA_TYPE:
      g_value_set_uint (value, priv->media_type);
      break;
    case PROP_CONNECTION_STATE:
      g_value_set_uint (value, stream->connection_state);
      break;
    case PROP_READY:
      g_value_set_boolean (value, priv->ready);
      break;
    case PROP_PLAYING:
      g_value_set_boolean (value, stream->playing);
      break;
    case PROP_COMBINED_DIRECTION:
      g_value_set_uint (value, stream->combined_direction);
      break;
    case PROP_LOCAL_HOLD:
      g_value_set_boolean (value, priv->local_hold);
      break;
    case PROP_CONTENT:
      g_value_set_object (value, priv->content);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_media_stream_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GabbleMediaStream *stream = GABBLE_MEDIA_STREAM (object);
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_MEDIA_SESSION:
      priv->session = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_NAME:
      g_free (stream->name);
      stream->name = g_value_dup_string (value);
      break;
    case PROP_ID:
      priv->id = g_value_get_uint (value);
      break;
    case PROP_MEDIA_TYPE:
      priv->media_type = g_value_get_uint (value);
      break;
    case PROP_CONNECTION_STATE:
      DEBUG ("stream %s connection state %d",
          stream->name, stream->connection_state);
      stream->connection_state = g_value_get_uint (value);
      break;
    case PROP_READY:
      priv->ready = g_value_get_boolean (value);
      break;
    case PROP_PLAYING:
        {
          gboolean old = stream->playing;
          stream->playing = g_value_get_boolean (value);
          if (stream->playing != old)
            push_playing (stream);
        }
      break;
    case PROP_COMBINED_DIRECTION:
      stream->combined_direction = g_value_get_uint (value);
      break;
    case PROP_CONTENT:
      g_assert (priv->content == NULL);

      priv->content = g_value_get_object (value);

      DEBUG ("%p: connecting to content %p signals", stream, priv->content);
      g_signal_connect (priv->content, "new-candidates",
          (GCallback) new_remote_candidates_cb, stream);

      /* we need this also, if we're the initiator of the stream
       * (so remote codecs arrive later) */
      g_signal_connect (priv->content, "remote-codecs",
          (GCallback) new_remote_codecs_cb, stream);

      g_signal_connect (priv->content, "notify::state",
          (GCallback) content_state_changed_cb, stream);

      g_signal_connect (priv->content, "notify::senders",
          (GCallback) content_senders_changed_cb, stream);

      priv->removed_id = g_signal_connect (priv->content, "removed",
          (GCallback) content_removed_cb, stream);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_media_stream_dispose (GObject *object);
static void gabble_media_stream_finalize (GObject *object);

static void
gabble_media_stream_class_init (GabbleMediaStreamClass *gabble_media_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_stream_class);
  GParamSpec *param_spec;
  GType transport_list_type =
      TP_ARRAY_TYPE_MEDIA_STREAM_HANDLER_TRANSPORT_LIST;
  GType codec_list_type =
      TP_ARRAY_TYPE_MEDIA_STREAM_HANDLER_CODEC_LIST;

  g_type_class_add_private (gabble_media_stream_class,
      sizeof (GabbleMediaStreamPrivate));

  object_class->constructor = gabble_media_stream_constructor;

  object_class->get_property = gabble_media_stream_get_property;
  object_class->set_property = gabble_media_stream_set_property;

  object_class->dispose = gabble_media_stream_dispose;
  object_class->finalize = gabble_media_stream_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "media stream's channel.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object ("media-session",
      "GabbleMediaSession object",
      "Gabble media session object that owns this media stream object.",
      GABBLE_TYPE_JINGLE_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA_SESSION,
      param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string ("name", "Stream name",
      "An opaque name for the stream used in the signalling.", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);

  param_spec = g_param_spec_uint ("id", "Stream ID",
                                  "A stream number for the stream used in the "
                                  "D-Bus API.",
                                  0, G_MAXUINT, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ID, param_spec);

  param_spec = g_param_spec_uint ("media-type", "Stream media type",
                                  "A constant indicating which media type the "
                                  "stream carries.",
                                  TP_MEDIA_STREAM_TYPE_AUDIO,
                                  TP_MEDIA_STREAM_TYPE_VIDEO,
                                  TP_MEDIA_STREAM_TYPE_AUDIO,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA_TYPE, param_spec);

  param_spec = g_param_spec_uint ("connection-state", "Stream connection state",
                                  "An integer indicating the state of the"
                                  "stream's connection.",
                                  TP_MEDIA_STREAM_STATE_DISCONNECTED,
                                  TP_MEDIA_STREAM_STATE_CONNECTED,
                                  TP_MEDIA_STREAM_STATE_DISCONNECTED,
                                  G_PARAM_CONSTRUCT |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION_STATE,
      param_spec);

  param_spec = g_param_spec_boolean ("ready", "Ready?",
                                     "A boolean signifying whether the user "
                                     "is ready to handle signals from this "
                                     "object.",
                                     FALSE,
                                     G_PARAM_CONSTRUCT |
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_READY, param_spec);

  param_spec = g_param_spec_boolean ("playing", "Set playing",
                                     "A boolean signifying whether the stream "
                                     "has been set playing yet.",
                                     FALSE,
                                     G_PARAM_CONSTRUCT |
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PLAYING, param_spec);

  param_spec = g_param_spec_uint ("combined-direction",
      "Combined direction",
      "An integer indicating the directions the stream currently sends in, "
      "and the peers who have been asked to send.",
      TP_MEDIA_STREAM_DIRECTION_NONE,
      MAKE_COMBINED_DIRECTION (TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
        TP_MEDIA_STREAM_PENDING_LOCAL_SEND |
        TP_MEDIA_STREAM_PENDING_REMOTE_SEND),
      TP_MEDIA_STREAM_DIRECTION_NONE,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_COMBINED_DIRECTION,
      param_spec);

  param_spec = g_param_spec_boolean ("local-hold", "Local hold?",
      "True if resources used for this stream have been freed.", FALSE,
      G_PARAM_READABLE |
      G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NICK);
  g_object_class_install_property (object_class, PROP_LOCAL_HOLD, param_spec);

  param_spec = g_param_spec_object ("content", "GabbleJingleContent object",
                                    "Jingle content signalling this media stream.",
                                    GABBLE_TYPE_JINGLE_CONTENT,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTENT, param_spec);

  /* signals not exported by D-Bus interface */

  signals[DESTROY] =
    g_signal_new ("destroy",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[NEW_ACTIVE_CANDIDATE_PAIR] =
    g_signal_new ("new-active-candidate-pair",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__STRING_STRING,
                  G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  signals[NEW_NATIVE_CANDIDATE] =
    g_signal_new ("new-native-candidate",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__STRING_BOXED,
                  G_TYPE_NONE, 2, G_TYPE_STRING, transport_list_type);

  signals[SUPPORTED_CODECS] =
    g_signal_new ("supported-codecs",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, codec_list_type);

  signals[ERROR] =
    g_signal_new ("error",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__UINT_STRING,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

  signals[UNHOLD_FAILED] = g_signal_new ("unhold-failed",
      G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

void
gabble_media_stream_dispose (GObject *object)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (object);
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  DEBUG ("called");

  if (priv->dispose_has_run)
    return;

  if (priv->initial_getter_id != 0)
    {
      g_source_remove (priv->initial_getter_id);
      priv->initial_getter_id = 0;
    }

  _gabble_media_stream_close (self);

  g_signal_emit (self, signals[DESTROY], 0);

  priv->dispose_has_run = TRUE;

  /* If content wasn't removed already, it will emit REMOVED signal
   * later on. We don't want to catch that. */
  if (priv->removed_id)
    {
      g_signal_handler_disconnect (priv->content, priv->removed_id);
      priv->removed_id = 0;
    }

  if (G_OBJECT_CLASS (gabble_media_stream_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_stream_parent_class)->dispose (object);
}

void
gabble_media_stream_finalize (GObject *object)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (object);
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  g_free (priv->object_path);

  g_value_unset (&priv->native_codecs);
  g_value_unset (&priv->native_candidates);

  g_value_unset (&priv->remote_codecs);
  g_value_unset (&priv->remote_candidates);

  G_OBJECT_CLASS (gabble_media_stream_parent_class)->finalize (object);
}

/**
 * gabble_media_stream_codec_choice
 *
 * Implements D-Bus method CodecChoice
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
gabble_media_stream_codec_choice (TpSvcMediaStreamHandler *iface,
                                  guint codec_id,
                                  DBusGMethodInvocation *context)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (iface);
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  tp_svc_media_stream_handler_return_from_codec_choice (context);
}


gboolean
gabble_media_stream_error (GabbleMediaStream *self,
                           guint errno,
                           const gchar *message,
                           GError **error)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  DEBUG ( "Media.StreamHandler::Error called, error %u (%s) -- emitting signal",
      errno, message);

  g_signal_emit (self, signals[ERROR], 0, errno, message);

  return TRUE;
}


/**
 * gabble_media_stream_error
 *
 * Implements D-Bus method Error
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
gabble_media_stream_error_async (TpSvcMediaStreamHandler *iface,
                                 guint errno,
                                 const gchar *message,
                                 DBusGMethodInvocation *context)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (iface);
  GError *error = NULL;

  if (gabble_media_stream_error (self, errno, message, &error))
    {
      tp_svc_media_stream_handler_return_from_error (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}


/**
 * gabble_media_stream_hold:
 *
 * Tell streaming clients that the stream is going on hold, so they should
 * stop streaming and free up any resources they are currently holding
 * (e.g. close hardware devices); or that the stream is coming off hold,
 * so they should reacquire those resources.
 */
void
gabble_media_stream_hold (GabbleMediaStream *self,
                          gboolean hold)
{
  tp_svc_media_stream_handler_emit_set_stream_held (self, hold);
}


/**
 * gabble_media_stream_hold_state:
 *
 * Called by streaming clients when the stream's hold state has been changed
 * successfully in response to SetStreamHeld.
 */
static void
gabble_media_stream_hold_state (TpSvcMediaStreamHandler *iface,
                                gboolean hold_state,
                                DBusGMethodInvocation *context)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (iface);
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  DEBUG ("%p: %s", self, hold_state ? "held" : "unheld");
  priv->local_hold = hold_state;

  g_object_notify ((GObject *) self, "local-hold");

  tp_svc_media_stream_handler_return_from_hold_state (context);
}


/**
 * gabble_media_stream_unhold_failure:
 *
 * Called by streaming clients when an attempt to reacquire the necessary
 * hardware or software resources to unhold the stream, in response to
 * SetStreamHeld, has failed.
 */
static void
gabble_media_stream_unhold_failure (TpSvcMediaStreamHandler *iface,
                                    DBusGMethodInvocation *context)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (iface);
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  DEBUG ("%p", self);

  priv->local_hold = TRUE;

  g_signal_emit (self, signals[UNHOLD_FAILED], 0);
  g_object_notify ((GObject *) self, "local-hold");

  tp_svc_media_stream_handler_return_from_unhold_failure (context);
}


/**
 * gabble_media_stream_native_candidates_prepared
 *
 * Implements D-Bus method NativeCandidatesPrepared
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
gabble_media_stream_native_candidates_prepared (TpSvcMediaStreamHandler *iface,
                                                DBusGMethodInvocation *context)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (iface);
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  tp_svc_media_stream_handler_return_from_native_candidates_prepared (context);
}


/**
 * gabble_media_stream_new_active_candidate_pair
 *
 * Implements D-Bus method NewActiveCandidatePair
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
gabble_media_stream_new_active_candidate_pair (TpSvcMediaStreamHandler *iface,
                                               const gchar *native_candidate_id,
                                               const gchar *remote_candidate_id,
                                               DBusGMethodInvocation *context)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (iface);
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  g_signal_emit (self, signals[NEW_ACTIVE_CANDIDATE_PAIR], 0,
                 native_candidate_id, remote_candidate_id);

  tp_svc_media_stream_handler_return_from_new_active_candidate_pair (context);
}


/**
 * gabble_media_stream_new_native_candidate
 *
 * Implements D-Bus method NewNativeCandidate
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
gabble_media_stream_new_native_candidate (TpSvcMediaStreamHandler *iface,
                                          const gchar *candidate_id,
                                          const GPtrArray *transports,
                                          DBusGMethodInvocation *context)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (iface);
  GabbleMediaStreamPrivate *priv;
  JingleSessionState state;
  GPtrArray *candidates;
  GValue candidate = { 0, };
  GValueArray *transport;
  guint component_id;
  const gchar *addr;
  GType candidate_struct_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_CANDIDATE;
  JingleCandidate *c;
  GList *li;
  guint i;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  g_object_get (priv->content->session, "state", &state, NULL);

  /* FIXME: maybe this should be an assertion in case the channel
   * isn't closed early enough right now? */
  if (state > JS_STATE_ACTIVE)
    {
      DEBUG ("state > JS_STATE_ACTIVE, doing nothing");
      tp_svc_media_stream_handler_return_from_new_native_candidate (context);
      return;
    }

  candidates = g_value_get_boxed (&priv->native_candidates);

  g_value_init (&candidate, candidate_struct_type);
  g_value_take_boxed (&candidate,
      dbus_g_type_specialized_construct (candidate_struct_type));

  dbus_g_type_struct_set (&candidate,
      0, candidate_id,
      1, transports,
      G_MAXUINT);

  if (transports->len < 1)
    {
      GError only_one = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "google p2p "
          "connections only support the concept of one transport per "
          "candidate" };
      DEBUG ("%s: number of transports was not 1; "
          "rejecting", G_STRFUNC);
      dbus_g_method_return_error (context, &only_one);
      return;
    }

  if (transports->len > 1)
    {
      DEBUG ("google p2p connections only support the concept of one "
          "transport per candidate, ignoring other components");
    }

  for (i = 0; i < transports->len; i++)
    {
      guint component;

      transport = g_ptr_array_index (transports, i);
      component = g_value_get_uint (g_value_array_get_nth (transport, 0));

      /* Accept component 0 because old farsight1 stream-engine didn't set the
       * component */
      if (component == 0 || component == 1)
        {
          break;
        }
      else
        {
          transport = NULL;
        }
    }


  if (transport == NULL)
    {
      GError only_one = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "You need"
          " at least a component 1." };
      DEBUG ("%s: number of transports was not 1; rejecting", G_STRFUNC);
      dbus_g_method_return_error (context, &only_one);
      return;
    }


  addr = g_value_get_string (g_value_array_get_nth (transport, 1));
  if (!strcmp (addr, "127.0.0.1"))
    {
      DEBUG ("ignoring native localhost candidate");
      tp_svc_media_stream_handler_return_from_new_native_candidate (context);
      return;
    }

  component_id = g_value_get_uint (g_value_array_get_nth (transport, 0));
  /* Old farsight1 s-e didn't set the component, make sure it's sane */
  if (component_id == 0)
      component_id = 1;

  g_ptr_array_add (candidates, g_value_get_boxed (&candidate));

  DEBUG ("put 1 native candidate from stream-engine into cache");

  c = jingle_candidate_new (component_id,
      /* address */
      g_value_get_string (g_value_array_get_nth (transport, 1)),
      /* port */
      g_value_get_uint (g_value_array_get_nth (transport, 2)),
      /* protocol */
      g_value_get_uint (g_value_array_get_nth (transport, 3)),
      /* preference */
      g_value_get_double (g_value_array_get_nth (transport, 6)),
      /* candidate type, we're relying on 1:1 candidate type mapping */
      g_value_get_uint (g_value_array_get_nth (transport, 7)),
      /* username */
      g_value_get_string (g_value_array_get_nth (transport, 8)),
      /* password */
      g_value_dup_string (g_value_array_get_nth (transport, 9)),
      /* FIXME: network is hardcoded for now */
      0,
      /* FIXME: generation is also hardcoded for now */
      0);

  li = g_list_prepend (NULL, c);
  gabble_jingle_content_add_candidates (priv->content, li);

  g_signal_emit (self, signals[NEW_NATIVE_CANDIDATE], 0,
                 candidate_id, transports);

  tp_svc_media_stream_handler_return_from_new_native_candidate (context);
}

static void gabble_media_stream_set_local_codecs (TpSvcMediaStreamHandler *,
    const GPtrArray *codecs, DBusGMethodInvocation *);

/**
 * gabble_media_stream_ready
 *
 * Implements D-Bus method Ready
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
gabble_media_stream_ready (TpSvcMediaStreamHandler *iface,
                           const GPtrArray *codecs,
                           DBusGMethodInvocation *context)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (iface);
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  DEBUG ("ready called");

  if (priv->ready == FALSE)
    {
      g_object_set (self, "ready", TRUE, NULL);

      push_remote_codecs (self);
      push_remote_candidates (self);
      push_playing (self);
      push_sending (self);
    }
  else
    {
      DEBUG ("Ready called twice, running plain SetLocalCodecs instead");
    }

  /* set_local_codecs and ready return the same thing, so we can do... */
  gabble_media_stream_set_local_codecs (iface, codecs, context);
}

static void
pass_local_codecs (GabbleMediaStream *stream, const GPtrArray *codecs,
    gboolean intersection)
{
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);
  GList *li = NULL;
  JingleCodec *c;
  guint i;

  /* if content is created by us, we want all the codecs, else we want the
   * intersection. */
  if (gabble_jingle_content_is_created_by_us (priv->content))
    {
      if (intersection)
        {
          DEBUG ("we already sent our codecs, ignoring codec intersection");
          return;
        }
    }
  else
    {
      if (!intersection)
        {
          DEBUG ("ignoring local codecs, waiting for codec intersection");
          return;
        }
    }

  DEBUG ("putting list of %d supported codecs from stream-engine into cache",
      codecs->len);

  g_value_set_boxed (&priv->native_codecs, codecs);

  for (i = 0; i < codecs->len; i++)
    {
      GType codec_struct_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_CODEC;

      GValue codec = { 0, };
      guint id, clock_rate, channels;
      gchar *name;
      GHashTable *params;

      g_value_init (&codec, codec_struct_type);
      g_value_set_static_boxed (&codec, g_ptr_array_index (codecs, i));

      dbus_g_type_struct_get (&codec,
          0, &id,
          1, &name,
          3, &clock_rate,
          4, &channels,
          5, &params,
          G_MAXUINT);

      c = jingle_media_rtp_codec_new (id, name,
          clock_rate, channels, params);

      DEBUG ("adding codec %s (%u %u %u)", c->name, c->id, c->clockrate, c->channels);
      li = g_list_append (li, c);
    }

  jingle_media_rtp_set_local_codecs (GABBLE_JINGLE_MEDIA_RTP (priv->content), li);
}

/**
 * gabble_media_stream_set_local_codecs
 *
 * Implements D-Bus method SetLocalCodecs
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
gabble_media_stream_set_local_codecs (TpSvcMediaStreamHandler *iface,
                                      const GPtrArray *codecs,
                                      DBusGMethodInvocation *context)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (iface);

  pass_local_codecs (self, codecs, FALSE);
  tp_svc_media_stream_handler_return_from_set_local_codecs (context);
}

/**
 * gabble_media_stream_stream_state
 *
 * Implements D-Bus method StreamState
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
gabble_media_stream_stream_state (TpSvcMediaStreamHandler *iface,
                                  guint connection_state,
                                  DBusGMethodInvocation *context)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (iface);
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);
  JingleTransportState ts = JINGLE_TRANSPORT_STATE_DISCONNECTED;

  switch (connection_state) {
    case TP_MEDIA_STREAM_STATE_DISCONNECTED:
      ts = JINGLE_TRANSPORT_STATE_DISCONNECTED;
      break;
    case TP_MEDIA_STREAM_STATE_CONNECTING:
      ts = JINGLE_TRANSPORT_STATE_CONNECTING;
      break;
    case TP_MEDIA_STREAM_STATE_CONNECTED:
      ts = JINGLE_TRANSPORT_STATE_CONNECTED;
      break;
    default:
      DEBUG ("ignoring unknown connection state %u", connection_state);
      goto OUT;
  }

  g_object_set (self, "connection-state", connection_state, NULL);
  gabble_jingle_content_set_transport_state (priv->content, ts);

OUT:
  tp_svc_media_stream_handler_return_from_stream_state (context);
}


/**
 * gabble_media_stream_supported_codecs
 *
 * Implements D-Bus method SupportedCodecs
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
gabble_media_stream_supported_codecs (TpSvcMediaStreamHandler *iface,
                                      const GPtrArray *codecs,
                                      DBusGMethodInvocation *context)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (iface);

  pass_local_codecs (self, codecs, TRUE);
  g_signal_emit (self, signals[SUPPORTED_CODECS], 0, codecs);
  tp_svc_media_stream_handler_return_from_supported_codecs (context);
}

void
_gabble_media_stream_close (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (!priv->closed)
    {
      priv->closed = TRUE;
      tp_svc_media_stream_handler_emit_close (stream);
    }
}

static void
new_remote_codecs_cb (GabbleJingleContent *content,
    GList *clist, GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);
  GList *li;
  GPtrArray *codecs;
  GType codec_struct_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_CODEC;

  DEBUG ("called");

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  codecs = g_value_get_boxed (&priv->remote_codecs);

  g_assert (codecs->len == 0);

  for (li = clist; li; li = li->next)
    {
      GValue codec = { 0, };
      JingleCodec *c = li->data;

      g_value_init (&codec, codec_struct_type);
      g_value_take_boxed (&codec,
          dbus_g_type_specialized_construct (codec_struct_type));

      DEBUG ("new remote %s codec: %u '%s' %u %u %u",
          priv->media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video",
          c->id, c->name, priv->media_type, c->clockrate, c->channels);

      dbus_g_type_struct_set (&codec,
          0, c->id,
          1, c->name,
          2, priv->media_type,
          3, c->clockrate,
          4, c->channels,
          5, c->params,
          G_MAXUINT);

      g_ptr_array_add (codecs, g_value_get_boxed (&codec));
    }

  DEBUG ("pushing remote codecs");

  push_remote_codecs (stream);
}


static void
push_remote_codecs (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;
  GPtrArray *codecs;
  GType codec_list_type =
      TP_ARRAY_TYPE_MEDIA_STREAM_HANDLER_CODEC_LIST;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (!priv->ready)
    return;

  codecs = g_value_get_boxed (&priv->remote_codecs);
  if (codecs->len == 0)
    return;

  DEBUG ("passing %d remote codecs to stream-engine",
                   codecs->len);

  tp_svc_media_stream_handler_emit_set_remote_codecs (stream, codecs);

  g_value_take_boxed (&priv->remote_codecs,
      dbus_g_type_specialized_construct (codec_list_type));
}

static void
new_remote_candidates_cb (GabbleJingleContent *content,
    GList *clist, GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);
  GPtrArray *candidates;
  GList *li;

  candidates = g_value_get_boxed (&priv->remote_candidates);

  DEBUG ("got new remote candidates");

  for (li = clist; li; li = li->next)
    {
      gchar *candidate_id;
      GValue candidate = { 0, };
      GPtrArray *transports;
      GValue transport = { 0, };
      JingleCandidate *c = li->data;
      GType transport_struct_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_TRANSPORT;
      GType candidate_struct_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_CANDIDATE;

      g_value_init (&transport, transport_struct_type);
      g_value_take_boxed (&transport,
          dbus_g_type_specialized_construct (transport_struct_type));

      dbus_g_type_struct_set (&transport,
          0, c->component,
          1, c->address,
          2, c->port,
          3, c->protocol == JINGLE_TRANSPORT_PROTOCOL_UDP ? 0 : 1,
          4, "RTP",
          5, "AVP",
          6, c->preference,
          7, c->type, /* FIXME: we're relying on 1:1 tp/jingle candidate type enums */
          8, c->username,
          9, c->password,
          G_MAXUINT);

      transports = g_ptr_array_sized_new (1);
      g_ptr_array_add (transports, g_value_get_boxed (&transport));

      g_value_init (&candidate, candidate_struct_type);
      g_value_take_boxed (&candidate,
          dbus_g_type_specialized_construct (candidate_struct_type));

      /* FIXME: is this naming scheme sensible? */
      candidate_id = g_strdup_printf ("R%d", ++priv->remote_candidate_count);

      dbus_g_type_struct_set (&candidate,
          0, candidate_id,
          1, transports,
          G_MAXUINT);

      g_free (candidate_id);

      g_ptr_array_add (candidates, g_value_get_boxed (&candidate));
    }

  push_remote_candidates (stream);
}

static void
content_state_changed_cb (GabbleJingleContent *c,
                          GParamSpec *pspec,
                          GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);
  JingleContentState state;

  g_object_get (c, "state", &state, NULL);

  DEBUG ("called");

  switch (state) {
    case JINGLE_CONTENT_STATE_ACKNOWLEDGED:
      /* connected stream means we can play, but sending is determined
       * by content senders (in update_senders) */
      stream->playing = TRUE;
      update_sending (stream, TRUE);
      push_playing (stream);
      push_sending (stream);
      break;
    case JINGLE_CONTENT_STATE_REMOVING:
      stream->playing = FALSE;
      priv->sending = FALSE;
      push_playing (stream);
      break;
    default:
      /* so gcc doesn't cry */
      break;
  }
}

static void
push_remote_candidates (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;
  GPtrArray *candidates;
  guint i;
  GType candidate_list_type =
      TP_ARRAY_TYPE_MEDIA_STREAM_HANDLER_CANDIDATE_LIST;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  candidates = g_value_get_boxed (&priv->remote_candidates);

  if (candidates->len == 0)
    return;

  if (!priv->ready)
    return;

  for (i = 0; i < candidates->len; i++)
    {
      GValueArray *candidate = g_ptr_array_index (candidates, i);
      const gchar *candidate_id;
      const GPtrArray *transports;

      candidate_id = g_value_get_string (g_value_array_get_nth (candidate, 0));
      transports = g_value_get_boxed (g_value_array_get_nth (candidate, 1));

      DEBUG ("passing 1 remote candidate to stream engine: %s", candidate_id);
      tp_svc_media_stream_handler_emit_add_remote_candidate (
          stream, candidate_id, transports);
    }

  g_value_take_boxed (&priv->remote_candidates,
      dbus_g_type_specialized_construct (candidate_list_type));
}

static void
push_playing (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (!priv->ready)
    return;

  DEBUG ("stream %s emitting SetStreamPlaying(%s)",
      stream->name, stream->playing ? "true" : "false");

  tp_svc_media_stream_handler_emit_set_stream_playing (
      stream, stream->playing);
}

static void
push_sending (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (!priv->ready)
    return;

  DEBUG ("stream %s emitting SetStreamSending(%s)",
      stream->name, priv->sending ? "true" : "false");

  tp_svc_media_stream_handler_emit_set_stream_sending (
      stream, priv->sending);
}

static void
update_direction (GabbleMediaStream *stream, GabbleJingleContent *c)
{
  CombinedStreamDirection new_combined_dir;
  TpMediaStreamDirection requested_dir, current_dir;
  TpMediaStreamPendingSend pending_send;
  JingleContentSenders senders;
  gboolean local_initiator;

  DEBUG ("called");

  g_object_get (c, "senders", &senders, NULL);
  g_object_get (c->session, "local-initiator", &local_initiator, NULL);

  switch (senders) {
      case JINGLE_CONTENT_SENDERS_INITIATOR:
        requested_dir = local_initiator ?
          TP_MEDIA_STREAM_DIRECTION_SEND : TP_MEDIA_STREAM_DIRECTION_RECEIVE;
        break;
      case JINGLE_CONTENT_SENDERS_RESPONDER:
        requested_dir = local_initiator ?
          TP_MEDIA_STREAM_DIRECTION_RECEIVE : TP_MEDIA_STREAM_DIRECTION_SEND;
        break;
      case JINGLE_CONTENT_SENDERS_BOTH:
        requested_dir = TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL;
        break;
      default:
        requested_dir = TP_MEDIA_STREAM_DIRECTION_NONE;
  }

  current_dir = COMBINED_DIRECTION_GET_DIRECTION (stream->combined_direction);
  pending_send = COMBINED_DIRECTION_GET_PENDING_SEND
    (stream->combined_direction);

  /* if local sending has been added, remove it,
   * and set the pending local send flag */
  if (((current_dir & TP_MEDIA_STREAM_DIRECTION_SEND) == 0) &&
    ((requested_dir & TP_MEDIA_STREAM_DIRECTION_SEND) != 0))
    {
      DEBUG ("setting pending local send flag");
      requested_dir &= ~TP_MEDIA_STREAM_DIRECTION_SEND;
      pending_send |= TP_MEDIA_STREAM_PENDING_LOCAL_SEND;
    }

  /* make any necessary changes */
  new_combined_dir = MAKE_COMBINED_DIRECTION (requested_dir, pending_send);
  if (new_combined_dir != stream->combined_direction)
    {
      g_object_set (stream, "combined-direction", new_combined_dir, NULL);
      update_sending (stream, FALSE);
    }

}

static void
content_senders_changed_cb (GabbleJingleContent *c,
                            GParamSpec *pspec,
                            GabbleMediaStream *stream)
{
  update_direction (stream, c);
}

static void
content_removed_cb (GabbleJingleContent *content, GabbleMediaStream *stream)
{
  _gabble_media_stream_close (stream);
}


gboolean
gabble_media_stream_change_direction (GabbleMediaStream *stream,
    guint requested_dir, GError **error)
{
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);
  CombinedStreamDirection new_combined_dir;
  TpMediaStreamDirection current_dir;
  TpMediaStreamPendingSend pending_send;
  JingleContentSenders senders;
  gboolean local_initiator;

  current_dir = COMBINED_DIRECTION_GET_DIRECTION (stream->combined_direction);
  pending_send = COMBINED_DIRECTION_GET_PENDING_SEND
    (stream->combined_direction);

  /* if we're awaiting a local decision on sending... */
  if ((pending_send & TP_MEDIA_STREAM_PENDING_LOCAL_SEND) != 0)
    {
      /* clear the flag */
      pending_send &= ~TP_MEDIA_STREAM_PENDING_LOCAL_SEND;

      /* make our current_dir match what other end thinks (he thinks we're
       * bidirectional) so that we send the correct transitions */
      current_dir ^= TP_MEDIA_STREAM_DIRECTION_SEND;
    }

  /* make any necessary changes */
  new_combined_dir = MAKE_COMBINED_DIRECTION (requested_dir, pending_send);
  if (new_combined_dir != stream->combined_direction)
    {
      g_object_set (stream, "combined-direction", new_combined_dir, NULL);
      update_sending (stream, FALSE);
    }

  /* short-circuit sending a request if we're not asking for anything new */
  if (current_dir == requested_dir)
    return TRUE;

  g_object_get (priv->content->session, "local-initiator", &local_initiator, NULL);

  switch (requested_dir)
    {
      case TP_MEDIA_STREAM_DIRECTION_SEND:
        senders = local_initiator ?
          JINGLE_CONTENT_SENDERS_INITIATOR : JINGLE_CONTENT_SENDERS_RESPONDER;
        break;

      case TP_MEDIA_STREAM_DIRECTION_RECEIVE:
        senders = local_initiator ?
          JINGLE_CONTENT_SENDERS_RESPONDER : JINGLE_CONTENT_SENDERS_INITIATOR;
        break;

      case TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL:
        senders = JINGLE_CONTENT_SENDERS_BOTH;
        break;

      default:
        g_assert_not_reached ();
    }

  if (!gabble_jingle_content_change_direction (priv->content, senders))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "stream direction invalid for the Jingle dialect in use");
      return FALSE;
    }

  return TRUE;
}


static void
update_sending (GabbleMediaStream *stream, gboolean start_sending)
{
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);
  gboolean new_sending;

  new_sending =
    ((stream->combined_direction & TP_MEDIA_STREAM_DIRECTION_SEND) != 0);

  if (priv->sending == new_sending)
    return;

  if (new_sending && !start_sending)
    return;

  priv->sending = new_sending;
  push_sending (stream);
}

static void
stream_handler_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcMediaStreamHandlerClass *klass =
    (TpSvcMediaStreamHandlerClass *) g_iface;

#define IMPLEMENT(x,suffix) tp_svc_media_stream_handler_implement_##x (\
    klass, gabble_media_stream_##x##suffix)
  IMPLEMENT(codec_choice,);
  IMPLEMENT(error,_async);
  IMPLEMENT(hold_state,);
  IMPLEMENT(native_candidates_prepared,);
  IMPLEMENT(new_active_candidate_pair,);
  IMPLEMENT(new_native_candidate,);
  IMPLEMENT(ready,);
  IMPLEMENT(set_local_codecs,);
  IMPLEMENT(stream_state,);
  IMPLEMENT(supported_codecs,);
  IMPLEMENT(unhold_failure,);
#undef IMPLEMENT
}
