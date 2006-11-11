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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "ansi.h"
#include "debug.h"
#include "handles.h"
#include "namespaces.h"

#include "gabble-connection.h"
#include "gabble-media-channel.h"
#include "gabble-media-session.h"
#include "gabble-media-session-enumtypes.h"

#include "telepathy-helpers.h"
#include "telepathy-constants.h"

#include "gabble-media-stream.h"
#include "gabble-media-stream-signals-marshal.h"
#include "gabble-media-stream-glue.h"

G_DEFINE_TYPE(GabbleMediaStream, gabble_media_stream, G_TYPE_OBJECT)

/* signal enum */
enum
{
    DESTROY,

    ADD_REMOTE_CANDIDATE,
    CLOSE,
    REMOVE_REMOTE_CANDIDATE,
    SET_ACTIVE_CANDIDATE_PAIR,
    SET_REMOTE_CANDIDATE_LIST,
    SET_REMOTE_CODECS,
    SET_STREAM_PLAYING,
    SET_STREAM_SENDING,

    NEW_ACTIVE_CANDIDATE_PAIR,
    NEW_NATIVE_CANDIDATE,
    SUPPORTED_CODECS,
    ERROR,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_MEDIA_SESSION,
  PROP_OBJECT_PATH,
  PROP_MODE,
  PROP_NAME,
  PROP_ID,
  PROP_INITIATOR,
  PROP_MEDIA_TYPE,
  PROP_CONNECTION_STATE,
  PROP_READY,
  PROP_GOT_LOCAL_CODECS,
  PROP_SIGNALLING_STATE,
  PROP_PLAYING,
  PROP_COMBINED_DIRECTION,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleMediaStreamPrivate GabbleMediaStreamPrivate;

struct _GabbleMediaStreamPrivate
{
  GabbleConnection *conn;
  GabbleMediaSession *session;
  GabbleMediaSessionMode mode;
  gchar *object_path;
  guint id;
  guint media_type;

  gboolean ready;
  gboolean sending;

  GValue native_codecs;     /* intersected codec list */
  GValue native_candidates;

  GValue remote_codecs;
  GValue remote_candidates;

  guint remote_candidate_count;

  gboolean closed;
  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_STREAM_GET_PRIVATE(obj) \
    ((GabbleMediaStreamPrivate *)obj->priv)

#ifdef ENABLE_DEBUG
#if _GMS_DEBUG_LEVEL > 1
static const char *tp_protocols[] = {
  "TP_MEDIA_STREAM_PROTO_UDP (0)",
  "TP_MEDIA_STREAM_PROTO_TCP (1)"
};

static const char *tp_transports[] = {
  "TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL (0)",
  "TP_MEDIA_STREAM_TRANSPORT_TYPE_DERIVED (1)",
  "TP_MEDIA_STREAM_TRANSPORT_TYPE_RELAY (2)"
};
#endif
#endif

static void push_native_candidates (GabbleMediaStream *stream);
static void push_remote_codecs (GabbleMediaStream *stream);
static void push_remote_candidates (GabbleMediaStream *stream);
static void push_playing (GabbleMediaStream *stream);
static void push_sending (GabbleMediaStream *stream);

static void
gabble_media_stream_init (GabbleMediaStream *self)
{
  GabbleMediaStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_MEDIA_STREAM, GabbleMediaStreamPrivate);

  self->priv = priv;

  g_value_init (&priv->native_codecs, TP_TYPE_CODEC_LIST);
  g_value_take_boxed (&priv->native_codecs,
      dbus_g_type_specialized_construct (TP_TYPE_CODEC_LIST));

  g_value_init (&priv->native_candidates, TP_TYPE_CANDIDATE_LIST);
  g_value_take_boxed (&priv->native_candidates,
      dbus_g_type_specialized_construct (TP_TYPE_CANDIDATE_LIST));

  g_value_init (&priv->remote_codecs, TP_TYPE_CODEC_LIST);
  g_value_take_boxed (&priv->remote_codecs,
      dbus_g_type_specialized_construct (TP_TYPE_CODEC_LIST));

  g_value_init (&priv->remote_candidates, TP_TYPE_CANDIDATE_LIST);
  g_value_take_boxed (&priv->remote_candidates,
      dbus_g_type_specialized_construct (TP_TYPE_CANDIDATE_LIST));
}

static GObject *
gabble_media_stream_constructor (GType type, guint n_props,
                                 GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMediaStreamPrivate *priv;
  DBusGConnection *bus;

  /* call base class constructor */
  obj = G_OBJECT_CLASS (gabble_media_stream_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (GABBLE_MEDIA_STREAM (obj));

  /* go for the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

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
    case PROP_MODE:
      g_value_set_enum (value, priv->mode);
      break;
    case PROP_NAME:
      g_value_set_string (value, stream->name);
      break;
    case PROP_ID:
      g_value_set_uint (value, priv->id);
      break;
    case PROP_INITIATOR:
      g_value_set_uint (value, stream->initiator);
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
    case PROP_GOT_LOCAL_CODECS:
      g_value_set_boolean (value, stream->got_local_codecs);
      break;
    case PROP_SIGNALLING_STATE:
      g_value_set_uint (value, stream->signalling_state);
      break;
    case PROP_PLAYING:
      g_value_set_boolean (value, stream->playing);
      break;
    case PROP_COMBINED_DIRECTION:
      g_value_set_uint (value, stream->combined_direction);
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
    case PROP_MODE:
      priv->mode = g_value_get_enum (value);
      break;
    case PROP_NAME:
      g_free ((gchar *) stream->name);
      stream->name = g_value_dup_string (value);
      break;
    case PROP_ID:
      priv->id = g_value_get_uint (value);
      break;
    case PROP_INITIATOR:
      stream->initiator = g_value_get_uint (value);
      break;
    case PROP_MEDIA_TYPE:
      priv->media_type = g_value_get_uint (value);
      break;
    case PROP_CONNECTION_STATE:
      GMS_DEBUG_INFO (priv->session, "stream %s connection state %d",
          stream->name, stream->connection_state);
      stream->connection_state = g_value_get_uint (value);
      break;
    case PROP_READY:
      priv->ready = g_value_get_boolean (value);
      break;
    case PROP_GOT_LOCAL_CODECS:
      stream->got_local_codecs = g_value_get_boolean (value);
      break;
    case PROP_SIGNALLING_STATE:
        {
          StreamSignallingState old = stream->signalling_state;
          stream->signalling_state = g_value_get_uint (value);
          GMS_DEBUG_INFO (priv->session, "stream %s sig_state %d->%d",
              stream->name, old, stream->signalling_state);
          if (stream->signalling_state != old)
            push_native_candidates (stream);
        }
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
        {
          gboolean new_sending;
          stream->combined_direction = g_value_get_uint (value);
          new_sending = ((stream->combined_direction &
                TP_MEDIA_STREAM_DIRECTION_SEND) != 0);
          if (priv->sending != new_sending)
            {
              priv->sending = new_sending;
              push_sending (stream);
            }
        }
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

  g_type_class_add_private (gabble_media_stream_class, sizeof (GabbleMediaStreamPrivate));

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

  param_spec = g_param_spec_object ("media-session", "GabbleMediaSession object",
                                    "Gabble media session object that owns this "
                                    "media stream object.",
                                    GABBLE_TYPE_MEDIA_SESSION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA_SESSION, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_enum ("mode", "Signalling mode",
                                  "Which signalling mode used to control the "
                                  "stream.",
                                  gabble_media_session_mode_get_type(),
                                  MODE_JINGLE,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MODE, param_spec);

  param_spec = g_param_spec_string ("name", "Stream name",
                                    "An opaque name for the stream used in the "
                                    "signalling.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
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

  param_spec = g_param_spec_uint ("initiator", "Stream initiator",
                                  "An enum signifying which end initiated "
                                  "the stream.",
                                  INITIATOR_LOCAL,
                                  INITIATOR_REMOTE,
                                  INITIATOR_LOCAL,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_INITIATOR, param_spec);

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
  g_object_class_install_property (object_class, PROP_CONNECTION_STATE, param_spec);

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

  param_spec = g_param_spec_boolean ("got-local-codecs", "Got local codecs?",
                                     "A boolean signifying whether we've got "
                                     "the locally supported codecs from the user.",
                                     FALSE,
                                     G_PARAM_CONSTRUCT |
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_GOT_LOCAL_CODECS, param_spec);

  param_spec = g_param_spec_uint ("signalling-state", "Signalling state",
                                  "Whether the stream is newly created, "
                                  "sent to the peer, or acknowledged.",
                                  STREAM_SIG_STATE_NEW,
                                  STREAM_SIG_STATE_REMOVING,
                                  STREAM_SIG_STATE_NEW,
                                  G_PARAM_CONSTRUCT |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SIGNALLING_STATE, param_spec);

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
      TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_COMBINED_DIRECTION,
      param_spec);

  /* signals exported by D-Bus interface */
  signals[DESTROY] =
    g_signal_new ("destroy",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[ADD_REMOTE_CANDIDATE] =
    g_signal_new ("add-remote-candidate",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_stream_marshal_VOID__STRING_BOXED,
                  G_TYPE_NONE, 2, G_TYPE_STRING, TP_TYPE_TRANSPORT_LIST);

  signals[CLOSE] =
    g_signal_new ("close",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[REMOVE_REMOTE_CANDIDATE] =
    g_signal_new ("remove-remote-candidate",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__STRING,
                  G_TYPE_NONE, 1, G_TYPE_STRING);

  signals[SET_ACTIVE_CANDIDATE_PAIR] =
    g_signal_new ("set-active-candidate-pair",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_stream_marshal_VOID__STRING_STRING,
                  G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  signals[SET_REMOTE_CANDIDATE_LIST] =
    g_signal_new ("set-remote-candidate-list",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, TP_TYPE_CANDIDATE_LIST);

  signals[SET_REMOTE_CODECS] =
    g_signal_new ("set-remote-codecs",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, TP_TYPE_CODEC_LIST);

  /* signals not exported by D-Bus interface */
  signals[NEW_ACTIVE_CANDIDATE_PAIR] =
    g_signal_new ("new-active-candidate-pair",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_stream_marshal_VOID__STRING_STRING,
                  G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  signals[NEW_NATIVE_CANDIDATE] =
    g_signal_new ("new-native-candidate",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_stream_marshal_VOID__STRING_BOXED,
                  G_TYPE_NONE, 2, G_TYPE_STRING, TP_TYPE_TRANSPORT_LIST);

  signals[SUPPORTED_CODECS] =
    g_signal_new ("supported-codecs",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, TP_TYPE_CODEC_LIST);

  signals[SET_STREAM_PLAYING] =
    g_signal_new ("set-stream-playing",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  signals[SET_STREAM_SENDING] =
    g_signal_new ("set-stream-sending",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  signals[ERROR] =
    g_signal_new ("error",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_stream_marshal_VOID__UINT_STRING,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_media_stream_class), &dbus_glib_gabble_media_stream_object_info);
}

void
gabble_media_stream_dispose (GObject *object)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (object);
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  _gabble_media_stream_close (self);

  g_signal_emit (self, signals[DESTROY], 0);

  priv->dispose_has_run = TRUE;

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
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_stream_codec_choice (GabbleMediaStream *self,
                                  guint codec_id,
                                  GError **error)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  return TRUE;
}


/**
 * gabble_media_stream_error
 *
 * Implements D-Bus method Error
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_stream_error (GabbleMediaStream *self,
                           guint errno,
                           const gchar *message,
                           GError **error)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  GMS_DEBUG_WARNING (priv->session, "Media.StreamHandler::Error called, error %u (%s) -- emitting signal", errno, message);

  g_signal_emit (self, signals[ERROR], 0, errno, message);

  return TRUE;
}


/**
 * gabble_media_stream_native_candidates_prepared
 *
 * Implements D-Bus method NativeCandidatesPrepared
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_stream_native_candidates_prepared (GabbleMediaStream *self,
                                                GError **error)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  return TRUE;
}


/**
 * gabble_media_stream_new_active_candidate_pair
 *
 * Implements D-Bus method NewActiveCandidatePair
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_stream_new_active_candidate_pair (GabbleMediaStream *self,
                                               const gchar *native_candidate_id,
                                               const gchar *remote_candidate_id,
                                               GError **error)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  g_signal_emit (self, signals[NEW_ACTIVE_CANDIDATE_PAIR], 0,
                 native_candidate_id, remote_candidate_id);

  return TRUE;
}


/**
 * gabble_media_stream_new_native_candidate
 *
 * Implements D-Bus method NewNativeCandidate
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_stream_new_native_candidate (GabbleMediaStream *self,
                                          const gchar *candidate_id,
                                          const GPtrArray *transports,
                                          GError **error)
{
  GabbleMediaStreamPrivate *priv;
  JingleSessionState state;
  GPtrArray *candidates;
  GValue candidate = { 0, };
  GValueArray *transport;
  const gchar *addr;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  g_object_get (priv->session, "state", &state, NULL);

  /* FIXME: maybe this should be an assertion in case the channel
   * isn't closed early enough right now? */
  if (state > JS_STATE_ACTIVE)
    {
      DEBUG ("state > JS_STATE_ACTIVE, doing nothing");
      return TRUE;
    }

  candidates = g_value_get_boxed (&priv->native_candidates);

  g_value_init (&candidate, TP_TYPE_CANDIDATE_STRUCT);
  g_value_take_boxed (&candidate,
      dbus_g_type_specialized_construct (TP_TYPE_CANDIDATE_STRUCT));

  dbus_g_type_struct_set (&candidate,
      0, candidate_id,
      1, transports,
      G_MAXUINT);

  transport = g_ptr_array_index (transports, 0);
  addr = g_value_get_string (g_value_array_get_nth (transport, 1));
  if (!strcmp (addr, "127.0.0.1"))
    {
      GMS_DEBUG_WARNING (priv->session, "%s: ignoring native localhost candidate",
                         G_STRFUNC);
      return TRUE;
    }

  g_ptr_array_add (candidates, g_value_get_boxed (&candidate));

  GMS_DEBUG_INFO (priv->session, "put 1 native candidate from stream-engine into cache");

  push_native_candidates (self);

  g_signal_emit (self, signals[NEW_NATIVE_CANDIDATE], 0,
                 candidate_id, transports);

  return TRUE;
}


/**
 * gabble_media_stream_ready
 *
 * Implements D-Bus method Ready
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_stream_ready (GabbleMediaStream *self,
                           const GPtrArray *codecs,
                           GError **error)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  GMS_DEBUG_INFO (priv->session, "ready called");

  g_object_set (self, "ready", TRUE, NULL);

  push_remote_codecs (self);
  push_remote_candidates (self);
  push_playing (self);
  push_sending (self);

  return gabble_media_stream_set_local_codecs (self, codecs, error);
}


/**
 * gabble_media_stream_set_local_codecs
 *
 * Implements D-Bus method SetLocalCodecs
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_stream_set_local_codecs (GabbleMediaStream *self,
                                      const GPtrArray *codecs,
                                      GError **error)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  GMS_DEBUG_INFO (priv->session, "putting list of all %d locally supported "
                  "codecs from stream-engine into cache", codecs->len);

  g_value_set_boxed (&priv->native_codecs, codecs);

  g_object_set (self, "got-local-codecs", TRUE, NULL);

  return TRUE;
}


/**
 * gabble_media_stream_stream_state
 *
 * Implements D-Bus method StreamState
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_stream_stream_state (GabbleMediaStream *self,
                                  guint connection_state,
                                  GError **error)
{
  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  g_object_set (self, "connection-state", connection_state, NULL);

  return TRUE;
}


/**
 * gabble_media_stream_supported_codecs
 *
 * Implements D-Bus method SupportedCodecs
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_media_stream_supported_codecs (GabbleMediaStream *self,
                                      const GPtrArray *codecs,
                                      GError **error)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (self));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  GMS_DEBUG_INFO (priv->session, "got codec intersection containing %d "
                  "codecs from stream-engine", codecs->len);

  /* store the intersection for later on */
  g_value_set_boxed (&priv->native_codecs, codecs);

  g_signal_emit (self, signals[SUPPORTED_CODECS], 0, codecs);

  return TRUE;
}

static LmHandlerResult
candidates_msg_reply_cb (GabbleConnection *conn,
                         LmMessage *sent_msg,
                         LmMessage *reply_msg,
                         GObject *object,
                         gpointer user_data)
{
  GabbleMediaStream *stream = GABBLE_MEDIA_STREAM (object);
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  MSG_REPLY_CB_END_SESSION_IF_NOT_SUCCESSFUL (priv->session, "candidates failed");

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

void
_add_rtp_candidate_node (GabbleMediaSession *session, LmMessageNode *parent,
                         GValueArray *candidate)
{
  gchar *addr;
  gchar *user;
  gchar *pass;
  gchar *port_str;
  gchar *pref_str;
  gchar *xml;
  const gchar *type_str;
  const gchar *candidate_id;
  guint port;
  gdouble pref;
  TpMediaStreamProto proto;
  TpMediaStreamTransportType type;
  const GPtrArray *transports;
  GValue transport = { 0, };
  LmMessageNode *cand_node;

  candidate_id = g_value_get_string (g_value_array_get_nth (candidate, 0));
  transports = g_value_get_boxed (g_value_array_get_nth (candidate, 1));

  /* jingle audio only supports the concept of one transport per candidate */
  g_assert (transports->len == 1);

  g_value_init (&transport, TP_TYPE_TRANSPORT_STRUCT);
  g_value_set_static_boxed (&transport, g_ptr_array_index (transports, 0));

  dbus_g_type_struct_get (&transport,
      1, &addr,
      2, &port,
      3, &proto,
      6, &pref,
      7, &type,
      8, &user,
      9, &pass,
      G_MAXUINT);

  port_str = g_strdup_printf ("%d", port);
  pref_str = g_strdup_printf ("%f", pref);

  switch (type) {
    case TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL:
      type_str = "local";
      break;
    case TP_MEDIA_STREAM_TRANSPORT_TYPE_DERIVED:
      type_str = "stun";
      break;
    case TP_MEDIA_STREAM_TRANSPORT_TYPE_RELAY:
      type_str = "relay";
      break;
    default:
      g_error ("%s: TpMediaStreamTransportType has an invalid value",
        G_STRFUNC);
      return;
  }

  cand_node = lm_message_node_add_child (parent, "candidate", NULL);
  lm_message_node_set_attributes (cand_node,
      "name", "rtp",
      "address", addr,
      "port", port_str,
      "username", user,
      "password", pass,
      "preference", pref_str,
      "protocol", (proto == TP_MEDIA_STREAM_PROTO_UDP) ? "udp" : "tcp",
      "type", type_str,
      "network", "0",
      "generation", "0",
      NULL);

  xml = lm_message_node_to_string (cand_node);
  GMS_DEBUG_DUMP (session,
    "  from Telepathy D-Bus struct: [%s\"%s\", %s[%s1, \"%s\", %d, %s, "
    "\"%s\", \"%s\", %f, %s, \"%s\", \"%s\"%s]]",
    ANSI_BOLD_OFF, candidate_id, ANSI_BOLD_ON, ANSI_BOLD_OFF, addr, port,
    tp_protocols[proto], "RTP", "AVP", pref, tp_transports[type], user, pass,
    ANSI_BOLD_ON);
  GMS_DEBUG_DUMP (session,
    "  to Jingle XML: [%s%s%s]", ANSI_BOLD_OFF, xml, ANSI_BOLD_ON);
  g_free (xml);

  g_free (addr);
  g_free (user);
  g_free (pass);
  g_free (port_str);
  g_free (pref_str);
}

static LmMessage *
_gabble_media_stream_message_new (GabbleMediaStream *stream,
                                  const gchar *action,
                                  LmMessageNode **content_node)
{
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);
  LmMessage *msg;

  /* construct a session message */
  msg = _gabble_media_session_message_new (priv->session, action,
                                           content_node);

  /* add our content node to it if in jingle mode */
  if (priv->mode == MODE_JINGLE)
    {
      LmMessageNode *node;

      node = lm_message_node_add_child (*content_node, "content", NULL);
      lm_message_node_set_attribute (node, "name", stream->name);

      *content_node = node;
    }

  return msg;
}

static void
push_candidate (GabbleMediaStream *stream, GValueArray *candidate)
{
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);
  LmMessage *msg;
  LmMessageNode *content_node, *transport_node;
  const gchar *action;

  if (priv->mode == MODE_GOOGLE)
    action = "candidates";
  else
    action = "transport-info";

  /* construct a base message */
  msg = _gabble_media_stream_message_new (stream, action, &content_node);

  /* for jingle, add a transport */
  transport_node = _gabble_media_stream_content_node_add_transport (stream,
      content_node);

  /* add transport info to it */
  _add_rtp_candidate_node (priv->session, transport_node, candidate);

  GMS_DEBUG_INFO (priv->session, "sending jingle session action \"%s\" to "
      "peer", action);

  /* send it */
  _gabble_connection_send_with_reply (priv->conn, msg, candidates_msg_reply_cb,
      G_OBJECT (stream), NULL, NULL);

  /* clean up */
  lm_message_unref (msg);
}

static void
push_native_candidates (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;
  GPtrArray *candidates;
  guint i;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (stream->signalling_state == STREAM_SIG_STATE_NEW ||
      stream->signalling_state == STREAM_SIG_STATE_REMOVING)
    return;

  candidates = g_value_get_boxed (&priv->native_candidates);

  for (i = 0; i < candidates->len; i++)
    push_candidate (stream, g_ptr_array_index (candidates, i));

  g_value_take_boxed (&priv->native_candidates,
    dbus_g_type_specialized_construct (TP_TYPE_CANDIDATE_LIST));
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
      g_signal_emit (stream, signals[CLOSE], 0);
    }
}

gboolean
_gabble_media_stream_post_remote_codecs (GabbleMediaStream *stream,
                                         LmMessage *message,
                                         LmMessageNode *desc_node,
                                         GError **error)
{
  GabbleMediaStreamPrivate *priv;
  LmMessageNode *node;
  GPtrArray *codecs;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  codecs = g_value_get_boxed (&priv->remote_codecs);

  g_assert (codecs->len == 0);

  for (node = desc_node->children; node; node = node->next)
    {
      guchar id;
      const gchar *name, *str;
      guint clockrate, channels;
      GHashTable *params;
      GValue codec = { 0, };

      if (g_strdiff (node->name, "payload-type"))
        continue;

      /* id of codec */
      str = lm_message_node_get_attribute (node, "id");
      if (str == NULL)
        {
          g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
              "description has no ID");
          return FALSE;
        }

      id = atoi(str);

      /* codec name */
      name = lm_message_node_get_attribute (node, "name");
      if (name == NULL)
        {
          name = "";
        }

      /* clock rate: jingle and newer GTalk */
      str = lm_message_node_get_attribute (node, "clockrate"); /* google */
      if (str == NULL)
        str = lm_message_node_get_attribute (node, "rate"); /* jingle */

      if (str != NULL)
        {
          clockrate = atoi (str);
        }
      else
        {
          clockrate = 0;
        }

      /* number of channels: jingle only */
      str = lm_message_node_get_attribute (node, "channels");
      if (str != NULL)
        {
          channels = atoi (str);
        }
      else
        {
          channels = 1;
        }

      params = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

      /* bitrate: newer GTalk only */
      str = lm_message_node_get_attribute (node, "bitrate");
      if (str != NULL)
        {
          g_hash_table_insert (params, "bitrate", g_strdup (str));
        }

      g_value_init (&codec, TP_TYPE_CODEC_STRUCT);
      g_value_take_boxed (&codec,
          dbus_g_type_specialized_construct (TP_TYPE_CODEC_STRUCT));

      dbus_g_type_struct_set (&codec,
          0, id,
          1, name,
          2, TP_CODEC_MEDIA_TYPE_AUDIO,
          3, clockrate,
          4, channels,
          5, params,
          G_MAXUINT);

      g_ptr_array_add (codecs, g_value_get_boxed (&codec));
    }

  GMS_DEBUG_INFO (priv->session, "put %d remote codecs from peer into cache",
                  codecs->len);

  push_remote_codecs (stream);

  return TRUE;
}

static void
push_remote_codecs (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;
  GPtrArray *codecs;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (!priv->ready)
    return;

  codecs = g_value_get_boxed (&priv->remote_codecs);
  if (codecs->len == 0)
    return;

  GMS_DEBUG_EVENT (priv->session, "passing %d remote codecs to stream-engine",
                   codecs->len);

  g_signal_emit (stream, signals[SET_REMOTE_CODECS], 0, codecs);

  g_value_take_boxed (&priv->remote_codecs,
      dbus_g_type_specialized_construct (TP_TYPE_CODEC_LIST));
}

gboolean
_gabble_media_stream_post_remote_candidates (GabbleMediaStream *stream,
                                             LmMessage *message,
                                             LmMessageNode *transport_node,
                                             GError **error)
{
  GabbleMediaStreamPrivate *priv;
  LmMessageNode *node;
  const gchar *str;
  GPtrArray *candidates;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  candidates = g_value_get_boxed (&priv->remote_candidates);

  for (node = transport_node->children; node; node = node->next)
    {
      gchar *candidate_id;
      const gchar *name, *addr;
      guint16 port;
      TpMediaStreamProto proto;
      gdouble pref;
      TpMediaStreamTransportType type;
      const gchar *user, *pass;
      guchar net, gen;
      GValue candidate = { 0, };
      GPtrArray *transports;
      GValue transport = { 0, };
      gchar *xml;

      if (g_strdiff (node->name, "candidate"))
        continue;

      /*
       * Candidate
       */

      /* stream name */
      name = lm_message_node_get_attribute (node, "name");
      if (name == NULL || strcmp (name, "rtp") != 0)
        goto FAILURE;


      /*
       * Transport
       */

      /* ip address */
      addr = lm_message_node_get_attribute (node, "address");
      if (addr == NULL)
        goto FAILURE;

      /* port */
      str = lm_message_node_get_attribute (node, "port");
      if (str == NULL)
        goto FAILURE;
      port = atoi (str);

      /* protocol */
      str = lm_message_node_get_attribute (node, "protocol");
      if (str == NULL)
        goto FAILURE;

      if (strcmp (str, "udp") == 0)
        {
          proto = TP_MEDIA_STREAM_PROTO_UDP;
        }
      else if (strcmp (str, "tcp") == 0)
        {
          proto = TP_MEDIA_STREAM_PROTO_TCP;
        }
      else if (strcmp (str, "ssltcp") == 0)
        {
          GMS_DEBUG_WARNING (priv->session, "%s: ssltcp candidates "
                             "not yet supported", G_STRFUNC);
          continue;
        }
      else
        goto FAILURE;

      /* protocol profile: hardcoded to "AVP" for now */

      /* preference */
      str = lm_message_node_get_attribute (node, "preference");
      if (str == NULL)
        goto FAILURE;
      pref = g_ascii_strtod (str, NULL);

      /* type */
      str = lm_message_node_get_attribute (node, "type");
      if (str == NULL)
        goto FAILURE;

      if (strcmp (str, "local") == 0)
        {
          type = TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL;
        }
      else if (strcmp (str, "stun") == 0)
        {
          type = TP_MEDIA_STREAM_TRANSPORT_TYPE_DERIVED;
        }
      else if (strcmp (str, "relay") == 0)
        {
          type = TP_MEDIA_STREAM_TRANSPORT_TYPE_RELAY;
        }
      else
        goto FAILURE;

      /* username */
      user = lm_message_node_get_attribute (node, "username");
      if (user == NULL)
        goto FAILURE;

      /* password */
      pass = lm_message_node_get_attribute (node, "password");
      if (pass == NULL)
        goto FAILURE;

      /* unknown */
      str = lm_message_node_get_attribute (node, "network");
      if (str == NULL)
        goto FAILURE;
      net = atoi (str);

      /* unknown */
      str = lm_message_node_get_attribute (node, "generation");
      if (str == NULL)
        goto FAILURE;
      gen = atoi (str);


      g_value_init (&transport, TP_TYPE_TRANSPORT_STRUCT);
      g_value_take_boxed (&transport,
          dbus_g_type_specialized_construct (TP_TYPE_TRANSPORT_STRUCT));

      dbus_g_type_struct_set (&transport,
          0, 1,         /* component number */
          1, addr,
          2, port,
          3, proto,
          4, "RTP",
          5, "AVP",
          6, pref,
          7, type,
          8, user,
          9, pass,
          G_MAXUINT);

      transports = g_ptr_array_sized_new (1);
      g_ptr_array_add (transports, g_value_get_boxed (&transport));


      g_value_init (&candidate, TP_TYPE_CANDIDATE_STRUCT);
      g_value_take_boxed (&candidate,
          dbus_g_type_specialized_construct (TP_TYPE_CANDIDATE_STRUCT));

      /* FIXME: is this naming scheme sensible? */
      candidate_id = g_strdup_printf ("R%d", ++priv->remote_candidate_count);

      dbus_g_type_struct_set (&candidate,
          0, candidate_id,
          1, transports,
          G_MAXUINT);

      g_ptr_array_add (candidates, g_value_get_boxed (&candidate));

      xml = lm_message_node_to_string (node);
      GMS_DEBUG_INFO (priv->session, "put 1 remote candidate from peer into cache");
      GMS_DEBUG_DUMP (priv->session, "  from Jingle XML: [%s%s%s]",
                      ANSI_BOLD_OFF, xml, ANSI_BOLD_ON);
      GMS_DEBUG_DUMP (priv->session, "  to Telepathy D-Bus struct: [%s\"%s\", %s[%s1, \"%s\", %d, %s, \"%s\", \"%s\", %f, %s, \"%s\", \"%s\"%s]]",
                      ANSI_BOLD_OFF, candidate_id, ANSI_BOLD_ON,
                      ANSI_BOLD_OFF, addr, port, tp_protocols[proto], "RTP", "AVP", pref, tp_transports[type], user, pass, ANSI_BOLD_ON);
      g_free (xml);

      g_free (candidate_id);
    }

/*SUCCESS:*/
  push_remote_candidates (stream);

  return TRUE;

FAILURE:
  g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
      "unable to parse candidate");

  return FALSE;
}

static void
push_remote_candidates (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;
  GPtrArray *candidates;
  guint i;

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

      GMS_DEBUG_EVENT (priv->session, "passing 1 remote candidate "
                       "to stream-engine");

      g_signal_emit (stream, signals[ADD_REMOTE_CANDIDATE], 0,
                     candidate_id, transports);
    }

  g_value_take_boxed (&priv->remote_candidates,
      dbus_g_type_specialized_construct (TP_TYPE_CANDIDATE_LIST));
}

static void
push_playing (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (!priv->ready)
    return;

  GMS_DEBUG_INFO (priv->session, "stream %s emitting SetStreamPlaying(%s)",
      stream->name, stream->playing ? "true" : "false");

  g_signal_emit (stream, signals[SET_STREAM_PLAYING], 0, stream->playing);
}

static void
push_sending (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (!priv->ready)
    return;

  GMS_DEBUG_INFO (priv->session, "stream %s emitting SetStreamSending(%s)",
      stream->name, priv->sending ? "true" : "false");

  g_signal_emit (stream, signals[SET_STREAM_SENDING], 0, priv->sending);
}

/*
 * oh sweet g_hash_table_foreach how beautiful thou be'st
 *
 *    _\ / ^/
 *  \/ \// 7_   __
 *  ( 7 ) (__) (__)
 *  ^\\ |/__/___/
 *   \\/_/     | <-- TP-cable kindly provided by Mika N.
 *    \ /      O
 *     ||     /|\
 *     ||     / \
 *     ||
 * ____||_____________
 */

typedef struct {
    GabbleMediaStreamPrivate *priv;
    LmMessageNode *pt_node;
} CodecParamsFromTpContext;

static const gchar *video_codec_params[] = {
  "x", "y", "width", "height", "layer", "transparent",
};

static void
codec_params_from_tp_foreach (gpointer key, gpointer value, gpointer user_data)
{
  CodecParamsFromTpContext *ctx = user_data;
  GabbleMediaStreamPrivate *priv = ctx->priv;
  const gchar *pname = key, *pvalue = value;

  if (priv->media_type == TP_CODEC_MEDIA_TYPE_AUDIO)
    {
      if (priv->mode == MODE_GOOGLE && strcmp (pname, "bitrate") == 0)
        {
          lm_message_node_set_attribute (ctx->pt_node, pname, pvalue);
          return;
        }
    }
  else if (priv->mode == MODE_JINGLE)
    {
      gint i;

      for (i = 0; video_codec_params[i] != NULL; i++)
        {
          if (strcmp (pname, video_codec_params[i]) == 0)
            {
              lm_message_node_set_attribute (ctx->pt_node, pname, pvalue);
              return;
            }
        }
    }

  DEBUG ("ignoring %s=%s for %s %s stream", pname, pvalue,
      (priv->mode == MODE_JINGLE) ? "jingle" : "google",
      (priv->media_type == TP_CODEC_MEDIA_TYPE_AUDIO) ? "audio" : "video");
}

void
_gabble_media_stream_content_node_add_description (GabbleMediaStream *stream,
                                                   LmMessageNode *content_node)
{
  GabbleMediaStreamPrivate *priv;
  const GPtrArray *codecs;
  LmMessageNode *desc_node;
  guint i;
  const gchar *xmlns;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  codecs = g_value_get_boxed (&priv->native_codecs);

  desc_node = lm_message_node_add_child (content_node, "description", NULL);

  if (priv->mode == MODE_GOOGLE)
    xmlns = NS_GOOGLE_SESSION_PHONE;
  else if (priv->media_type == TP_CODEC_MEDIA_TYPE_VIDEO)
    xmlns = NS_JINGLE_DESCRIPTION_VIDEO;
  else
    xmlns = NS_JINGLE_DESCRIPTION_AUDIO;

  lm_message_node_set_attribute (desc_node, "xmlns", xmlns);

  for (i = 0; i < codecs->len; i++)
    {
      GValue codec = { 0, };
      guint id, clock_rate, channels;
      gchar *name, buf[16];
      GHashTable *params;
      LmMessageNode *pt_node;
      CodecParamsFromTpContext ctx;

      g_value_init (&codec, TP_TYPE_CODEC_STRUCT);
      g_value_set_static_boxed (&codec, g_ptr_array_index (codecs, i));

      dbus_g_type_struct_get (&codec,
          0, &id,
          1, &name,
          3, &clock_rate,
          4, &channels,
          5, &params,
          G_MAXUINT);

      /* create a sub-node called "payload-type" and fill it */
      pt_node = lm_message_node_add_child (desc_node, "payload-type", NULL);

      /* id: required */
      sprintf (buf, "%u", id);
      lm_message_node_set_attribute (pt_node, "id", buf);

      /* name: optional */
      if (*name != '\0')
        {
          lm_message_node_set_attribute (pt_node, "name", name);
        }

      /* clock rate: optional */
      if (clock_rate != 0)
        {
          sprintf (buf, "%u", clock_rate);
          lm_message_node_set_attribute (pt_node,
              (priv->mode == MODE_GOOGLE) ? "clockrate" : "rate", buf);
        }

      /* number of channels: optional, jingle only */
      /* FIXME: is it? */
      if (channels != 0 && priv->mode == MODE_JINGLE)
        {
          sprintf (buf, "%u", channels);
          lm_message_node_set_attribute (pt_node, "channels", buf);
        }

      /* parse the optional params */
      ctx.priv = priv;
      ctx.pt_node = pt_node;
      g_hash_table_foreach (params, codec_params_from_tp_foreach, &ctx);

      /* clean up */
      g_free (name);
      g_hash_table_destroy (params);
    }
}

LmMessageNode *
_gabble_media_stream_content_node_add_transport (GabbleMediaStream *stream,
                                                 LmMessageNode *content_node)
{
  GabbleMediaStreamPrivate *priv;
  LmMessageNode *node;

  g_assert (GABBLE_IS_MEDIA_STREAM (stream));

  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (priv->mode != MODE_JINGLE)
    return content_node;

  node = lm_message_node_add_child (content_node, "transport", NULL);

  lm_message_node_set_attribute (node, "xmlns", NS_GOOGLE_TRANSPORT_P2P);

  return node;
}

