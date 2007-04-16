/*
 * gabble-media-session.c - Source for GabbleMediaSession
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

#include "gabble-media-session.h"

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include <telepathy-glib/debug-ansi.h>
#include "debug.h"
#include "namespaces.h"
#include "util.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-media-interfaces.h>

#include "gabble-connection.h"
#include "gabble-media-channel.h"
#include "gabble-media-stream.h"
#include "presence-cache.h"
#include "presence.h"

#include "gabble-signals-marshal.h"

static void session_handler_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(GabbleMediaSession,
    gabble_media_session,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_MEDIA_SESSION_HANDLER,
      session_handler_iface_init)
    )

#define DEFAULT_SESSION_TIMEOUT 50000

#define GTALK_STREAM_NAME "gtalk"

/* 99 streams gives us a max name length of 8 (videoXX\0 or audioXX\0) */
#define MAX_STREAMS 99
#define MAX_STREAM_NAME_LEN 8

/* signal enum */
enum
{
    STREAM_ADDED,
    TERMINATED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_MEDIA_CHANNEL,
  PROP_OBJECT_PATH,
  PROP_SESSION_ID,
  PROP_INITIATOR,
  PROP_PEER,
  PROP_PEER_RESOURCE,
  PROP_STATE,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleMediaSessionPrivate GabbleMediaSessionPrivate;

struct _GabbleMediaSessionPrivate
{
  GabbleConnection *conn;
  GabbleMediaChannel *channel;
  GabbleMediaSessionMode mode;
  gchar *object_path;

  GPtrArray *streams;
  GPtrArray *remove_requests;

  gchar *id;
  TpHandle peer;
  gchar *peer_resource;

  JingleSessionState state;
  gboolean ready;
  gboolean locally_accepted;
  gboolean terminated;

  guint timer_id;

  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_SESSION_GET_PRIVATE(obj) \
    ((GabbleMediaSessionPrivate *)obj->priv)

typedef struct {
    gchar *name;
    gchar *attributes;
} SessionStateDescription;

static const SessionStateDescription session_states[] =
{
    { "JS_STATE_PENDING_CREATED",
      TP_ANSI_BOLD_ON TP_ANSI_FG_BLACK TP_ANSI_BG_WHITE   },
    { "JS_STATE_PENDING_INITIATE_SENT",
      TP_ANSI_BOLD_ON                  TP_ANSI_BG_CYAN    },
    { "JS_STATE_PENDING_INITIATED",
      TP_ANSI_BOLD_ON                  TP_ANSI_BG_MAGENTA },
    { "JS_STATE_PENDING_ACCEPT_SENT",
      TP_ANSI_BOLD_ON                  TP_ANSI_BG_CYAN    },
    { "JS_STATE_ACTIVE",
      TP_ANSI_BOLD_ON                  TP_ANSI_BG_BLUE    },
    { "JS_STATE_ENDED",
                                       TP_ANSI_BG_RED     }
};

static void
gabble_media_session_init (GabbleMediaSession *self)
{
  GabbleMediaSessionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_MEDIA_SESSION, GabbleMediaSessionPrivate);

  self->priv = priv;

  priv->mode = MODE_JINGLE;
  priv->state = JS_STATE_PENDING_CREATED;
  priv->streams = g_ptr_array_new ();
  priv->remove_requests = g_ptr_array_new ();
}

static void stream_connection_state_changed_cb (GabbleMediaStream *stream,
                                                GParamSpec *param,
                                                GabbleMediaSession *session);
static void stream_got_local_codecs_changed_cb (GabbleMediaStream *stream,
                                                GParamSpec *param,
                                                GabbleMediaSession *session);

static void
_emit_new_stream (GabbleMediaSession *session,
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
  tp_svc_media_session_handler_emit_new_stream_handler (session,
      object_path, id, media_type, TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL);

  g_free (object_path);
}


static GabbleMediaStream *
_lookup_stream_by_name_and_initiator (GabbleMediaSession *session,
                                      const gchar *stream_name,
                                      JingleInitiator stream_initiator)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);

      if (tp_strdiff (stream->name, stream_name))
        continue;

      if (stream_initiator != INITIATOR_INVALID &&
          stream_initiator != stream->initiator)
        continue;

      return stream;
    }

  return NULL;
}


static GabbleMediaStream *
create_media_stream (GabbleMediaSession *session,
                     const gchar *name,
                     JingleInitiator initiator,
                     guint media_type)
{
  GabbleMediaSessionPrivate *priv;
  gchar *object_path;
  GabbleMediaStream *stream;
  guint id;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));
  g_assert (name != NULL);

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  /* assert that if we're in google mode:
   *  - we only try to make one stream
   *  - it's an audio stream
   *  - it's called GTALK_STREAM_NAME */
  if (priv->mode == MODE_GOOGLE)
    {
      g_assert (priv->streams->len == 0);
      g_assert (media_type == TP_MEDIA_STREAM_TYPE_AUDIO);
      g_assert (!tp_strdiff (name, GTALK_STREAM_NAME));
    }

  g_assert (priv->streams->len < MAX_STREAMS);
  g_assert (_lookup_stream_by_name_and_initiator (session, name, initiator) ==
      NULL);

  id = _gabble_media_channel_get_stream_id (priv->channel);

  GMS_DEBUG_INFO (session,
      "creating new %s %s stream called \"%s\" with id %u",
      priv->mode == MODE_GOOGLE ? "google" : "jingle",
      media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video",
      name, id);

  object_path = g_strdup_printf ("%s/MediaStream%u", priv->object_path, id);

  stream = g_object_new (GABBLE_TYPE_MEDIA_STREAM,
                         "connection", priv->conn,
                         "media-session", session,
                         "object-path", object_path,
                         "mode", priv->mode,
                         "name", name,
                         "id", id,
                         "initiator", initiator,
                         "media-type", media_type,
                         NULL);

  g_signal_connect (stream, "notify::connection-state",
                    (GCallback) stream_connection_state_changed_cb,
                    session);
  g_signal_connect (stream, "notify::got-local-codecs",
                    (GCallback) stream_got_local_codecs_changed_cb,
                    session);

  g_ptr_array_add (priv->streams, stream);

  g_free (object_path);

  if (priv->ready)
    _emit_new_stream (session, stream);

  g_signal_emit (session, signals[STREAM_ADDED], 0, stream);

  return stream;
}

static void
destroy_media_stream (GabbleMediaSession *session,
                      GabbleMediaStream *stream)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  _gabble_media_stream_close (stream);
  g_ptr_array_remove_fast (priv->streams, stream);
  g_object_unref (stream);
}

static GObject *
gabble_media_session_constructor (GType type, guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMediaSessionPrivate *priv;
  DBusGConnection *bus;

  obj = G_OBJECT_CLASS (gabble_media_session_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (GABBLE_MEDIA_SESSION (obj));

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void
gabble_media_session_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_MEDIA_CHANNEL:
      g_value_set_object (value, priv->channel);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_SESSION_ID:
      g_value_set_string (value, priv->id);
      break;
    case PROP_INITIATOR:
      g_value_set_uint (value, session->initiator);
      break;
    case PROP_PEER:
      g_value_set_uint (value, priv->peer);
      break;
    case PROP_PEER_RESOURCE:
      g_value_set_string (value, priv->peer_resource);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void session_state_changed (GabbleMediaSession *session,
                                   JingleSessionState prev_state,
                                   JingleSessionState new_state);

static void
gabble_media_session_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  JingleSessionState prev_state;

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_MEDIA_CHANNEL:
      priv->channel = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_SESSION_ID:
      g_free (priv->id);
      priv->id = g_value_dup_string (value);
      break;
    case PROP_INITIATOR:
      session->initiator = g_value_get_uint (value);
      break;
    case PROP_PEER:
      priv->peer = g_value_get_uint (value);
      break;
    case PROP_PEER_RESOURCE:
      g_free (priv->peer_resource);
      priv->peer_resource = g_value_dup_string (value);
      break;
    case PROP_STATE:
      prev_state = priv->state;
      priv->state = g_value_get_uint (value);

      if (priv->state == JS_STATE_ENDED)
        g_assert (priv->terminated);

      if (priv->state != prev_state)
        session_state_changed (session, prev_state, priv->state);

      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_media_session_dispose (GObject *object);
static void gabble_media_session_finalize (GObject *object);

static void
gabble_media_session_class_init (GabbleMediaSessionClass *gabble_media_session_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_session_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_media_session_class,
      sizeof (GabbleMediaSessionPrivate));

  object_class->constructor = gabble_media_session_constructor;

  object_class->get_property = gabble_media_session_get_property;
  object_class->set_property = gabble_media_session_set_property;

  object_class->dispose = gabble_media_session_dispose;
  object_class->finalize = gabble_media_session_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "media session's channel.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object ("media-channel",
      "GabbleMediaChannel object",
      "Gabble media channel object that owns this media session object.",
      GABBLE_TYPE_MEDIA_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA_CHANNEL,
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

  param_spec = g_param_spec_string ("session-id", "Session ID",
                                    "A unique session identifier used "
                                    "throughout all communication.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SESSION_ID, param_spec);

  param_spec = g_param_spec_uint ("initiator", "Session initiator",
                                  "An enum signifying which end initiated "
                                  "the session.",
                                  INITIATOR_LOCAL,
                                  INITIATOR_REMOTE,
                                  INITIATOR_LOCAL,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_INITIATOR, param_spec);

  param_spec = g_param_spec_uint ("peer", "Session peer",
                                  "The TpHandle representing the contact "
                                  "with whom this session communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER, param_spec);

  param_spec = g_param_spec_string ("peer-resource",
                                    "Session peer's resource",
                                    "The resource of the contact "
                                    "with whom this session communicates, "
                                    "if applicable",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_WRITABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER_RESOURCE,
                                   param_spec);

  param_spec = g_param_spec_uint ("state", "Session state",
                                  "The current state that the session is in.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  signals[STREAM_ADDED] =
    g_signal_new ("stream-added",
                  G_OBJECT_CLASS_TYPE (gabble_media_session_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, G_TYPE_OBJECT);

  signals[TERMINATED] =
    g_signal_new ("terminated",
                  G_OBJECT_CLASS_TYPE (gabble_media_session_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
}

static void
gabble_media_session_dispose (GObject *object)
{
  GabbleMediaSession *self = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (self);
  guint i;

  DEBUG ("called");

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  _gabble_media_session_terminate (self, INITIATOR_LOCAL,
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  if (priv->timer_id != 0)
    g_source_remove (priv->timer_id);

  if (priv->streams != NULL)
    {
      for (i = 0; i < priv->streams->len; i++)
        g_object_unref (g_ptr_array_index (priv->streams, i));
      g_ptr_array_free (priv->streams, TRUE);
      priv->streams = NULL;
    }

  for (i = 0; i < priv->remove_requests->len; i++)
    g_ptr_array_free (g_ptr_array_index (priv->remove_requests, i), TRUE);
  g_ptr_array_free (priv->remove_requests, TRUE);
  priv->remove_requests = NULL;

  if (G_OBJECT_CLASS (gabble_media_session_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_session_parent_class)->dispose (object);
}

static void
gabble_media_session_finalize (GObject *object)
{
  GabbleMediaSession *self = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (self);

  g_free (priv->id);
  g_free (priv->object_path);
  g_free (priv->peer_resource);
  G_OBJECT_CLASS (gabble_media_session_parent_class)->finalize (object);
}


/**
 * gabble_media_session_error
 *
 * Implements D-Bus method Error
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
 */
static void
gabble_media_session_error (TpSvcMediaSessionHandler *iface,
                            guint errno,
                            const gchar *message,
                            DBusGMethodInvocation *context)
{
  GabbleMediaSession *self = GABBLE_MEDIA_SESSION (iface);
  GabbleMediaSessionPrivate *priv;
  GPtrArray *tmp;
  guint i;

  g_assert (GABBLE_IS_MEDIA_SESSION (self));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (self);

  GMS_DEBUG_INFO (self, "Media.SessionHandler::Error called, error %u (%s) -- "
      "emitting error on each stream", errno, message);

  if (priv->state == JS_STATE_ENDED)
    {
      tp_svc_media_session_handler_return_from_error (context);
      return;
    }
  else if (priv->state == JS_STATE_PENDING_CREATED)
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
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);

      gabble_media_stream_error (stream, errno, message, NULL);
    }

  g_ptr_array_free (tmp, TRUE);

  tp_svc_media_session_handler_return_from_error (context);
}


/**
 * gabble_media_session_ready
 *
 * Implements D-Bus method Ready
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
 */
static void
gabble_media_session_ready (TpSvcMediaSessionHandler *iface,
                            DBusGMethodInvocation *context)
{
  GabbleMediaSession *self = GABBLE_MEDIA_SESSION (iface);
  GabbleMediaSessionPrivate *priv;
  guint i;

  g_assert (GABBLE_IS_MEDIA_SESSION (self));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (self);

  priv->ready = TRUE;

  for (i = 0; i < priv->streams->len; i++)
    _emit_new_stream (self, g_ptr_array_index (priv->streams, i));

  tp_svc_media_session_handler_return_from_ready (context);
}


static gboolean
_handle_create (GabbleMediaSession *session,
                LmMessage *message,
                LmMessageNode *content_node,
                const gchar *stream_name,
                GabbleMediaStream *stream,
                LmMessageNode *desc_node,
                LmMessageNode *trans_node,
                GError **error)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  GabbleMediaSessionMode session_mode;
  TpMediaStreamType stream_type;
  gboolean override_existing = FALSE;

  if ((priv->state == JS_STATE_PENDING_CREATED) &&
      (session->initiator == INITIATOR_LOCAL))
    {
      DEBUG ("we're trying to call ourselves, rejecting with busy");
      _gabble_media_session_terminate (session, INITIATOR_REMOTE,
          TP_CHANNEL_GROUP_CHANGE_REASON_BUSY);
          return FALSE;
    }


  if (stream != NULL)
    {
      /* streams added by the session initiator may replace similarly-named
       * streams which we are trying to add (but havn't had acknowledged) */
      if (stream->signalling_state < STREAM_SIG_STATE_ACKNOWLEDGED)
        {
          if (session->initiator == INITIATOR_REMOTE)
            {
              override_existing = TRUE;
            }
          else
            {
              g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_CONFLICT,
                  "session initiator is creating a stream named \"%s\" "
                  "already", stream_name);
              return FALSE;
            }
        }
      else
        {
          g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_CONFLICT,
              "can't create new stream called \"%s\", it already exists, "
              "rejecting", stream_name);
          return FALSE;
        }
    }

  if (desc_node == NULL)
    {
      g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "unable to create stream without a content description");
      return FALSE;
    }

  if (lm_message_node_has_namespace (desc_node,
        NS_GOOGLE_SESSION_PHONE, NULL))
    {
      session_mode = MODE_GOOGLE;
      stream_type = TP_MEDIA_STREAM_TYPE_AUDIO;
    }
  else if (lm_message_node_has_namespace (desc_node,
        NS_JINGLE_DESCRIPTION_AUDIO, NULL))
    {
      session_mode = MODE_JINGLE;
      stream_type = TP_MEDIA_STREAM_TYPE_AUDIO;
    }
  else if (lm_message_node_has_namespace (desc_node,
        NS_JINGLE_DESCRIPTION_VIDEO, NULL))
    {
      session_mode = MODE_JINGLE;
      stream_type = TP_MEDIA_STREAM_TYPE_VIDEO;
    }
  else
    {
      g_set_error (error, GABBLE_XMPP_ERROR,
          XMPP_ERROR_JINGLE_UNSUPPORTED_CONTENT,
          "refusing to create stream for unsupported content description");
      return FALSE;
    }

  /* MODE_GOOGLE is allowed to have a null transport node */
  if (session_mode == MODE_JINGLE && trans_node == NULL)
    {
      g_set_error (error, GABBLE_XMPP_ERROR,
          XMPP_ERROR_JINGLE_UNSUPPORTED_TRANSPORT,
          "refusing to create stream for unsupported transport");
      return FALSE;
    }

  if (session_mode != priv->mode)
    {
      if (priv->streams->len > 0)
        {
          g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_UNEXPECTED_REQUEST,
              "refusing to change mode because streams already exist");
          return FALSE;
        }
      else
        {
          GMS_DEBUG_INFO (session, "setting session mode to %s",
              session_mode == MODE_GOOGLE ? "google" : "jingle");
          priv->mode = session_mode;
        }
    }

  if (override_existing)
    {
      GMS_DEBUG_INFO (session, "removing our unacknowledged stream \"%s\" "
          "in favour of the session initiator's", stream_name);

      /* disappear this stream */
      destroy_media_stream (session, stream);

      stream = NULL;
    }

  if (priv->streams->len == MAX_STREAMS)
    {
      g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_RESOURCE_CONSTRAINT,
          "refusing to create more than " G_STRINGIFY (MAX_STREAMS)
          " streams");
      return FALSE;
    }

  stream = create_media_stream (session, stream_name, INITIATOR_REMOTE,
      stream_type);

  /* set the signalling state to ACKNOWLEDGED */
  g_object_set (stream,
      "signalling-state", STREAM_SIG_STATE_ACKNOWLEDGED,
      NULL);

  /* for jingle streams, set the direction to none, so that the
   * direction handler adds the right flags */
  if (priv->mode == MODE_JINGLE)
    g_object_set (stream,
        "combined-direction", TP_MEDIA_STREAM_DIRECTION_NONE,
        NULL);

  return TRUE;
}


static TpMediaStreamDirection
_senders_to_direction (GabbleMediaSession *session,
                       const gchar *senders)
{
  TpMediaStreamDirection ret = TP_MEDIA_STREAM_DIRECTION_NONE;

  if (!tp_strdiff (senders, "initiator"))
    {
      if (session->initiator == INITIATOR_LOCAL)
        ret = TP_MEDIA_STREAM_DIRECTION_SEND;
      else
        ret = TP_MEDIA_STREAM_DIRECTION_RECEIVE;
    }
  else if (!tp_strdiff (senders, "responder"))
    {
      if (session->initiator == INITIATOR_REMOTE)
        ret = TP_MEDIA_STREAM_DIRECTION_SEND;
      else
        ret = TP_MEDIA_STREAM_DIRECTION_RECEIVE;
    }
  else if (!tp_strdiff (senders, "both"))
    {
      ret = TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL;
    }

  return ret;
}

static gboolean
_handle_direction (GabbleMediaSession *session,
                   LmMessage *message,
                   LmMessageNode *content_node,
                   const gchar *stream_name,
                   GabbleMediaStream *stream,
                   LmMessageNode *desc_node,
                   LmMessageNode *trans_node,
                   GError **error)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  const gchar *senders;
  CombinedStreamDirection new_combined_dir;
  TpMediaStreamDirection requested_dir, current_dir;
  TpMediaStreamPendingSend pending_send;

  if (priv->mode == MODE_GOOGLE)
    return TRUE;

  requested_dir = TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL;

  senders = lm_message_node_get_attribute (content_node, "senders");
  if (senders != NULL)
    requested_dir = _senders_to_direction (session, senders);

  if (requested_dir == TP_MEDIA_STREAM_DIRECTION_NONE)
    {
      g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "received invalid content senders value \"%s\" on stream \"%s\"; "
          "rejecting", senders, stream_name);
      return FALSE;
    }

  current_dir = COMBINED_DIRECTION_GET_DIRECTION (stream->combined_direction);
  pending_send = COMBINED_DIRECTION_GET_PENDING_SEND
    (stream->combined_direction);

  GMS_DEBUG_INFO (session, "received request for senders \"%s\" on stream "
      "\"%s\"", senders, stream_name);

  /* if local sending has been added, remove it,
   * and set the pending local send flag */
  if (((current_dir & TP_MEDIA_STREAM_DIRECTION_SEND) == 0) &&
    ((requested_dir & TP_MEDIA_STREAM_DIRECTION_SEND) != 0))
    {
      GMS_DEBUG_INFO (session, "setting pending local send flag");
      requested_dir &= ~TP_MEDIA_STREAM_DIRECTION_SEND;
      pending_send |= TP_MEDIA_STREAM_PENDING_LOCAL_SEND;
    }

#if 0
  /* clear any pending remote send */
  if ((pending_send & TP_MEDIA_STREAM_PENDING_REMOTE_SEND) != 0)
    {
      GMS_DEBUG_INFO (session, "setting pending local send flag");
      pending_send &= ~TP_MEDIA_STREAM_PENDING_REMOTE_SEND;
    }
#endif

  /* make any necessary changes */
  new_combined_dir = MAKE_COMBINED_DIRECTION (requested_dir, pending_send);
  if (new_combined_dir != stream->combined_direction)
    {
      g_object_set (stream, "combined-direction", new_combined_dir, NULL);
      _gabble_media_stream_update_sending (stream, FALSE);
    }

  return TRUE;
}


static gboolean
_handle_accept (GabbleMediaSession *session,
                LmMessage *message,
                LmMessageNode *content_node,
                const gchar *stream_name,
                GabbleMediaStream *stream,
                LmMessageNode *desc_node,
                LmMessageNode *trans_node,
                GError **error)
{
  g_object_set (stream, "playing", TRUE, NULL);

  _gabble_media_stream_update_sending (stream, TRUE);

  return TRUE;
}


static gboolean
_handle_codecs (GabbleMediaSession *session,
                LmMessage *message,
                LmMessageNode *content_node,
                const gchar *stream_name,
                GabbleMediaStream *stream,
                LmMessageNode *desc_node,
                LmMessageNode *trans_node,
                GError **error)
{
  if (desc_node == NULL)
    {
      g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "unable to handle codecs without a content description node");
      return FALSE;
    }

  if (!_gabble_media_stream_post_remote_codecs (stream, message, desc_node,
        error))
    return FALSE;

  return TRUE;
}


static gboolean
_handle_candidates (GabbleMediaSession *session,
                    LmMessage *message,
                    LmMessageNode *content_node,
                    const gchar *stream_name,
                    GabbleMediaStream *stream,
                    LmMessageNode *desc_node,
                    LmMessageNode *trans_node,
                    GError **error)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  if (trans_node == NULL)
    {
      if (priv->mode == MODE_GOOGLE)
        {
          trans_node = content_node;
        }
      else
        {
          g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
              "unable to handle candidates without a transport node");
          return FALSE;
        }
    }

  if (!_gabble_media_stream_post_remote_candidates (stream, message,
        trans_node, error))
    return FALSE;

  return TRUE;
}

static guint
_count_non_removing_streams (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  guint i, ret = 0;

  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);

      if (stream->signalling_state < STREAM_SIG_STATE_REMOVING)
        ret++;
    }

  return ret;
}

static gboolean
_handle_remove (GabbleMediaSession *session,
                LmMessage *message,
                LmMessageNode *content_node,
                const gchar *stream_name,
                GabbleMediaStream *stream,
                LmMessageNode *desc_node,
                LmMessageNode *trans_node,
                GError **error)
{
  /* reducing a session to contain 0 streams is invalid; instead the peer
   * should terminate the session. I guess we'll do it for them... */
  if (_count_non_removing_streams (session) == 1)
    {
      g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
          "unable to remove the last stream in a Jingle call");
      return FALSE;
    }

  /* close the stream */
  destroy_media_stream (session, stream);

  return TRUE;
}


static gboolean
_handle_terminate (GabbleMediaSession *session,
                   LmMessage *message,
                   LmMessageNode *content_node,
                   const gchar *stream_name,
                   GabbleMediaStream *stream,
                   LmMessageNode *desc_node,
                   LmMessageNode *trans_node,
                   GError **error)
{
  DEBUG ("called for %s", stream_name);

  _gabble_media_session_terminate (session, INITIATOR_REMOTE,
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  return TRUE;
}


typedef gboolean (*StreamHandlerFunc)(GabbleMediaSession *session,
                                      LmMessage *message,
                                      LmMessageNode *content_node,
                                      const gchar *stream_name,
                                      GabbleMediaStream *stream,
                                      LmMessageNode *desc_node,
                                      LmMessageNode *trans_node,
                                      GError **error);

typedef struct _Handler Handler;

struct _Handler {
  const gchar *actions[3];
  JingleSessionState min_allowed_state;
  JingleSessionState max_allowed_state;
  StreamHandlerFunc stream_handlers[4];
  JingleSessionState new_state;
};

static Handler handlers[] = {
  {
    { "initiate", "session-initiate", NULL },
    JS_STATE_PENDING_CREATED,
    JS_STATE_PENDING_CREATED,
    { _handle_create, _handle_direction, _handle_codecs, NULL },
    JS_STATE_PENDING_INITIATED
  },
  {
    { "accept", "session-accept", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_PENDING_INITIATED,
    { _handle_direction, _handle_codecs, _handle_accept, NULL },
    JS_STATE_ACTIVE
  },
  {
    { "reject", NULL },
    JS_STATE_PENDING_INITIATE_SENT,
    JS_STATE_PENDING_INITIATED,
    { _handle_terminate, NULL },
    JS_STATE_INVALID
  },
  {
    { "terminate", "session-terminate", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ENDED,
    { _handle_terminate, NULL },
    JS_STATE_INVALID
  },
  {
    { "candidates", "transport-info", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ACTIVE,
    { _handle_candidates, NULL },
    JS_STATE_INVALID
  },
  {
    { "content-add", NULL },
    JS_STATE_ACTIVE,
    JS_STATE_ACTIVE,
    { _handle_create, _handle_direction, _handle_codecs, NULL },
    JS_STATE_INVALID,
  },
  {
    { "content-modify", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ACTIVE,
    { _handle_direction, NULL },
    JS_STATE_INVALID
  },
  {
    { "content-accept", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ACTIVE,
    { _handle_direction, _handle_codecs, _handle_accept, NULL },
    JS_STATE_INVALID
  },
  {
    { "content-remove", "content-decline", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ACTIVE,
    { _handle_remove, NULL },
    JS_STATE_INVALID
  },
  {
    { NULL },
    JS_STATE_INVALID,
    JS_STATE_INVALID,
    { NULL },
    JS_STATE_INVALID
  }
};


static gboolean
_call_handlers_on_stream (GabbleMediaSession *session,
                          LmMessage *message,
                          LmMessageNode *content_node,
                          const gchar *stream_name,
                          JingleInitiator stream_creator,
                          StreamHandlerFunc *func,
                          GError **error)
{
  GabbleMediaStream *stream = NULL;
  LmMessageNode *desc_node = NULL, *trans_node = NULL;
  StreamHandlerFunc *tmp;
  gboolean stream_created = FALSE;

  if (content_node != NULL)
    {
      desc_node = lm_message_node_get_child (content_node, "description");

      trans_node = lm_message_node_get_child_with_namespace (content_node,
          "transport", NS_GOOGLE_TRANSPORT_P2P);
    }

  for (tmp = func; *tmp != NULL; tmp++)
    {
      /* handlers may create the stream */
      if (stream == NULL && stream_name != NULL)
        stream = _lookup_stream_by_name_and_initiator (session, stream_name,
            stream_creator);

      /* the create handler is able to check whether or not the stream
       * exists, and act accordingly (sometimes it will replace an existing
       * stream, sometimes it will reject). the termination handler
       * also requires no stream to do it's job. */
      if (*tmp != _handle_create && *tmp != _handle_terminate)
        {
          /* all other handlers require the stream to exist */
          if (stream == NULL)
            {
              const gchar *created = "";

              if (stream_creator == INITIATOR_LOCAL)
                created = "locally-created ";
              else if (stream_creator == INITIATOR_REMOTE)
                created = "remotely-created ";

              g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_ITEM_NOT_FOUND,
                  "unable to handle action for unknown %sstream \"%s\" ",
                  created, stream_name);

              return FALSE;
            }
          else
            {
              /* don't do anything with actions on streams which have not been
               * acknowledged, or that we're trying to remove, to deal with
               * adding/removing race conditions (actions sent by the other end
               * before they're aware that we've added or removed a stream) */
              if (stream->signalling_state != STREAM_SIG_STATE_ACKNOWLEDGED)
                {
                  GMS_DEBUG_WARNING (session, "ignoring action because stream "
                      "%s is in state %d, not ACKNOWLEDGED", stream->name,
                      stream->signalling_state);
                  return TRUE;
                }
            }
        }

      if (!(*tmp) (session, message, content_node, stream_name, stream,
            desc_node, trans_node, error))
        {
          /* if we successfully created the stream but failed to do something
           * with it later, remove it */
          if (stream_created)
            destroy_media_stream (session, stream);

          return FALSE;
        }

      if (*tmp == _handle_create)
        {
          stream_created = TRUE;
          /* force a stream lookup after the create handler, even if we
           * already had one (it has replacement semantics in certain
           * situations) */
          stream = NULL;
        }
    }

  return TRUE;
}


static JingleInitiator
_creator_to_initiator (GabbleMediaSession *session, const gchar *creator)
{
  if (!tp_strdiff (creator, "initiator"))
    {
      if (session->initiator == INITIATOR_LOCAL)
        return INITIATOR_LOCAL;
      else
        return INITIATOR_REMOTE;
    }
  else if (!tp_strdiff (creator, "responder"))
    {
      if (session->initiator == INITIATOR_LOCAL)
        return INITIATOR_REMOTE;
      else
        return INITIATOR_LOCAL;
    }
  else
    return INITIATOR_INVALID;
}


static gboolean
_call_handlers_on_streams (GabbleMediaSession *session,
                           LmMessage *message,
                           LmMessageNode *session_node,
                           StreamHandlerFunc *func,
                           GError **error)
{
  LmMessageNode *content_node;

  if (lm_message_node_has_namespace (session_node, NS_GOOGLE_SESSION, NULL))
    return _call_handlers_on_stream (session, message, session_node,
        GTALK_STREAM_NAME, INITIATOR_INVALID, func, error);

  if (session_node->children == NULL)
    return _call_handlers_on_stream (session, message, NULL, NULL,
        INITIATOR_INVALID, func, error);

  for (content_node = session_node->children;
       NULL != content_node;
       content_node = content_node->next)
    {
      const gchar *stream_name, *stream_creator;
      JingleInitiator stream_initiator;

      if (tp_strdiff (content_node->name, "content"))
        continue;

      stream_name = lm_message_node_get_attribute (content_node, "name");

      if (stream_name == NULL)
        {
          g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
              "rejecting content node with no name");
          return FALSE;
        }

      stream_creator = lm_message_node_get_attribute (content_node, "creator");
      stream_initiator = _creator_to_initiator (session, stream_creator);

      /* we allow NULL creator to mean INITIATOR_INVALID for backwards
       * compatibility with clients that don't put a creator attribute in */
      if (stream_creator != NULL && stream_initiator == INITIATOR_INVALID)
        {
          g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
              "rejecting content node with invalid creators value");
          return FALSE;
        }

      if (!_call_handlers_on_stream (session, message, content_node,
            stream_name, stream_initiator, func, error))
        return FALSE;
    }

  return TRUE;
}


gboolean
_gabble_media_session_handle_action (GabbleMediaSession *session,
                                     LmMessage *message,
                                     LmMessageNode *session_node,
                                     const gchar *action,
                                     GError **error)
{
  GabbleMediaSessionPrivate *priv;
  StreamHandlerFunc *funcs = NULL;
  JingleSessionState new_state = JS_STATE_INVALID;
  Handler *i;
  const gchar **tmp;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  GMS_DEBUG_INFO (session, "got jingle session action \"%s\" from peer",
      action);

  /* do the state machine dance */

  /* search the table of handlers for the action */
  for (i = handlers; NULL != i->actions[0]; i++)
    {
      for (tmp = i->actions; NULL != *tmp; tmp++)
        if (0 == strcmp (*tmp, action))
          break;

      if (NULL == *tmp)
        continue;

      /* if we're outside the allowable states for this action, return an error
       * immediately */
      if (priv->state < i->min_allowed_state ||
          priv->state > i->max_allowed_state)
        {
          g_set_error (error, GABBLE_XMPP_ERROR,
              XMPP_ERROR_JINGLE_OUT_OF_ORDER,
              "action \"%s\" not allowed in current state", action);
          goto ERROR;
        }

      funcs = i->stream_handlers;
      new_state = i->new_state;

      break;
    }

  /* pointer is not NULL if we found a matching action */
  if (NULL == funcs)
    {
      g_set_error (error, GABBLE_XMPP_ERROR,
          XMPP_ERROR_FEATURE_NOT_IMPLEMENTED, "action \"%s\" not implemented",
          action);
      goto ERROR;
    }

  /* call handlers if there are any (NULL-terminated array) */
  if (NULL != *funcs)
    {
      if (!_call_handlers_on_streams (session, message, session_node, funcs,
            error))
        {
          if (*error == NULL)
            g_set_error (error, GABBLE_XMPP_ERROR, XMPP_ERROR_BAD_REQUEST,
                "unknown error encountered with action \"%s\"",
                action);

          goto ERROR;
        }
    }

  /* acknowledge the IQ before changing the state because the new state
   * could perform some actions which the other end will only accept
   * if this action has been acknowledged */
  _gabble_connection_acknowledge_set_iq (priv->conn, message);

  /* if the action specified a new state to go to, set it */
  if (JS_STATE_INVALID != new_state)
    g_object_set (session, "state", new_state, NULL);

  return TRUE;

ERROR:
  g_assert (error != NULL);
  GMS_DEBUG_ERROR (session, (*error)->message);
  return FALSE;
}

static gboolean
timeout_session (gpointer data)
{
  GabbleMediaSession *session = data;

  DEBUG ("session timed out");

  _gabble_media_session_terminate (session, INITIATOR_LOCAL,
      TP_CHANNEL_GROUP_CHANGE_REASON_ERROR);

  return FALSE;
}

static void do_content_add (GabbleMediaSession *, GabbleMediaStream *);

void
_add_ready_new_streams (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);

      GMS_DEBUG_DUMP (session, "pondering accept-time add for stream: %s, got "
          "local codecs: %s, initiator: %s, signalling state: %d",
          stream->name, stream->got_local_codecs ? "true" : "false",
          stream->initiator == INITIATOR_LOCAL ? "local" : "remote",
          stream->signalling_state);

      if (stream->got_local_codecs == FALSE)
        continue;

      if (stream->initiator == INITIATOR_REMOTE)
        continue;

      if (stream->signalling_state > STREAM_SIG_STATE_NEW)
        continue;

      do_content_add (session, stream);
    }
}

static void
session_state_changed (GabbleMediaSession *session,
                       JingleSessionState prev_state,
                       JingleSessionState new_state)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  GMS_DEBUG_EVENT (session, "state changed from %s to %s",
                   session_states[prev_state].name,
                   session_states[new_state].name);

  /*
   * If the state goes from CREATED to INITIATED (which means the remote
   * end initiated), set the timer. If, OTOH, we're the end which just sent an
   * initiate, set the timer.
   */
  if ((prev_state == JS_STATE_PENDING_CREATED &&
       new_state == JS_STATE_PENDING_INITIATED) ||
      (new_state == JS_STATE_PENDING_INITIATE_SENT))
    {
      priv->timer_id =
        g_timeout_add (DEFAULT_SESSION_TIMEOUT, timeout_session, session);
    }
  else if (new_state == JS_STATE_ACTIVE)
    {
      g_source_remove (priv->timer_id);
      priv->timer_id = 0;

      /* signal any streams to the remote end which were added locally &
       * became ready before the session was accepted, so haven't been
       * mentioned yet */
      _add_ready_new_streams (session);
    }
}

static void
_mark_local_streams_sent (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);

      if (stream->initiator == INITIATOR_REMOTE)
        continue;

      GMS_DEBUG_INFO (session, "marking local stream %s as signalled",
          stream->name);

      g_object_set (stream, "signalling-state", STREAM_SIG_STATE_SENT, NULL);
    }
}

static void
_mark_local_streams_acked (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);

      if (stream->initiator == INITIATOR_REMOTE)
        continue;

      if (stream->signalling_state != STREAM_SIG_STATE_SENT)
        continue;

      GMS_DEBUG_INFO (session, "marking local stream %s as acknowledged",
          stream->name);

      g_object_set (stream,
          "signalling-state", STREAM_SIG_STATE_ACKNOWLEDGED,
          NULL);
    }
}

static void
_set_remote_streams_playing (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);

      if (stream->initiator == INITIATOR_LOCAL)
        continue;

      GMS_DEBUG_INFO (session, "setting remote stream %s as playing",
          stream->name);

      g_object_set (stream, "playing", TRUE, NULL);
    }
}

static const gchar *_direction_to_senders (GabbleMediaSession *,
    TpMediaStreamDirection);

static void
_add_content_descriptions_one (GabbleMediaSession *session,
                               GabbleMediaStream *stream,
                               LmMessageNode *session_node)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  LmMessageNode *content_node;

  if (priv->mode == MODE_GOOGLE)
    {
      content_node = session_node;
    }
  else
    {
      TpMediaStreamDirection direction;
      TpMediaStreamPendingSend pending_send;

      content_node = _gabble_media_stream_add_content_node (stream,
          session_node);

      direction = COMBINED_DIRECTION_GET_DIRECTION (stream->combined_direction);
      pending_send = COMBINED_DIRECTION_GET_PENDING_SEND
        (stream->combined_direction);

      /* if we have a pending local send flag set, the signalled (ie understood
       * by both ends) direction of the stream is assuming that we are actually
       * sending, so we should OR that into the direction before deciding what
       * to signal the stream with. we don't need to consider pending remote
       * send because it doesn't happen in Jingle */

      if ((pending_send & TP_MEDIA_STREAM_PENDING_LOCAL_SEND) != 0)
        direction |= TP_MEDIA_STREAM_DIRECTION_SEND;

      if (direction != TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL)
        {
          const gchar *senders;
          senders = _direction_to_senders (session, direction);
          lm_message_node_set_attribute (content_node, "senders", senders);
        }
    }

  _gabble_media_stream_content_node_add_description (stream, content_node);

  _gabble_media_stream_content_node_add_transport (stream, content_node);
}

static void
_add_content_descriptions (GabbleMediaSession *session,
                           LmMessageNode *session_node,
                           JingleInitiator stream_initiator)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);

      if (stream->initiator != stream_initiator)
        {
          GMS_DEBUG_INFO (session,
              "not adding content description for %s stream %s",
              stream->initiator == INITIATOR_LOCAL ? "local" : "remote",
              stream->name);
          continue;
        }

      _add_content_descriptions_one (session, stream, session_node);
    }
}

static LmHandlerResult
accept_msg_reply_cb (GabbleConnection *conn,
                     LmMessage *sent_msg,
                     LmMessage *reply_msg,
                     GObject *object,
                     gpointer user_data)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;

  MSG_REPLY_CB_END_SESSION_IF_NOT_SUCCESSFUL (session, "accept failed");

  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);

      if (stream->initiator == INITIATOR_LOCAL)
        continue;

      _gabble_media_stream_update_sending (stream, TRUE);
    }

  g_object_set (session, "state", JS_STATE_ACTIVE, NULL);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
_stream_not_ready_for_accept (GabbleMediaSession *session,
                              GabbleMediaStream *stream)
{
  /* locally initiated streams shouldn't delay acceptance */
  if (stream->initiator == INITIATOR_LOCAL)
    return FALSE;

  if (!stream->got_local_codecs)
    {
      GMS_DEBUG_INFO (session, "stream %s does not yet have local codecs",
          stream->name);

      return TRUE;
    }

  if (stream->connection_state != TP_MEDIA_STREAM_STATE_CONNECTED)
    {
      GMS_DEBUG_INFO (session, "stream %s is not yet connected", stream->name);

      return TRUE;
    }

  return FALSE;
}

static void
try_session_accept (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  LmMessage *msg;
  LmMessageNode *session_node;
  const gchar *action;
  guint i;

  if (priv->state < JS_STATE_ACTIVE && !priv->locally_accepted)
    {
      GMS_DEBUG_INFO (session, "not sending accept yet, waiting for local "
          "user to accept call");
      return;
    }

  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);

      if (_stream_not_ready_for_accept (session, stream))
        {
          GMS_DEBUG_INFO (session, "not sending accept yet, found a stream "
              "which was not yet connected or was missing local codecs");
          return;
        }
    }

  if (priv->mode == MODE_GOOGLE)
    action = "accept";
  else
    action = "session-accept";

  /* construct a session acceptance message */
  msg = _gabble_media_session_message_new (session, action, &session_node);

  /* only accept REMOTE streams; any LOCAL streams were added by the local
   * user before accepting and should be signalled after the accept */
  _add_content_descriptions (session, session_node, INITIATOR_REMOTE);

  GMS_DEBUG_INFO (session, "sending jingle session action \"%s\" to peer",
      action);

  /* send the final acceptance message */
  _gabble_connection_send_with_reply (priv->conn, msg, accept_msg_reply_cb,
                                      G_OBJECT (session), NULL, NULL);

  lm_message_unref (msg);

  /* set remote streams playing */
  _set_remote_streams_playing (session);

  g_object_set (session, "state", JS_STATE_PENDING_ACCEPT_SENT, NULL);
}

static LmHandlerResult
content_accept_msg_reply_cb (GabbleConnection *conn,
                             LmMessage *sent_msg,
                             LmMessage *reply_msg,
                             GObject *object,
                             gpointer user_data)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (user_data);
  GabbleMediaStream *stream = GABBLE_MEDIA_STREAM (object);

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      GMS_DEBUG_ERROR (session, "content-accept failed; removing stream");
      NODE_DEBUG (sent_msg->node, "message sent");
      NODE_DEBUG (reply_msg->node, "message reply");

      _gabble_media_session_remove_streams (session, &stream, 1);

      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }

  _gabble_media_stream_update_sending (stream, TRUE);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
try_content_accept (GabbleMediaSession *session,
                    GabbleMediaStream *stream)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  LmMessage *msg;
  LmMessageNode *session_node;

  g_assert (priv->state == JS_STATE_ACTIVE);
  g_assert (priv->mode == MODE_JINGLE);

  if (_stream_not_ready_for_accept (session, stream))
    {
      GMS_DEBUG_INFO (session, "not sending content-accept yet, stream %s "
          "is disconnected or missing local codecs", stream->name);
      return;
    }

  /* send a content acceptance message */
  msg = _gabble_media_session_message_new (session, "content-accept",
      &session_node);

  _add_content_descriptions_one (session, stream, session_node);

  GMS_DEBUG_INFO (session, "sending jingle session action \"content-accept\" "
      "to peer for stream %s", stream->name);

  _gabble_connection_send_with_reply (priv->conn, msg,
      content_accept_msg_reply_cb, G_OBJECT (stream), session, NULL);

  lm_message_unref (msg);

  /* set stream playing */
  g_object_set (stream, "playing", TRUE, NULL);
}

static LmHandlerResult
initiate_msg_reply_cb (GabbleConnection *conn,
                       LmMessage *sent_msg,
                       LmMessage *reply_msg,
                       GObject *object,
                       gpointer user_data)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (object);

  MSG_REPLY_CB_END_SESSION_IF_NOT_SUCCESSFUL (session, "initiate failed");

  g_object_set (session, "state", JS_STATE_PENDING_INITIATED, NULL);

  /* mark all of the streams that we sent in the initiate as acknowledged */
  _mark_local_streams_acked (session);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
_stream_not_ready_for_initiate (GabbleMediaSession *session,
                                GabbleMediaStream *stream)
{
  if (!stream->got_local_codecs)
    {
      GMS_DEBUG_INFO (session, "stream %s does not yet have local codecs",
          stream->name);

      return TRUE;
    }

  return FALSE;
}

static void
try_session_initiate (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  LmMessage *msg;
  LmMessageNode *session_node;
  const gchar *action;
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);

      if (_stream_not_ready_for_initiate (session, stream))
        {
          GMS_DEBUG_INFO (session, "not sending initiate yet, found a stream "
            "which was missing local codecs");
          return;
        }
    }

  if (priv->mode == MODE_GOOGLE)
      action = "initiate";
  else
      action = "session-initiate";

  msg = _gabble_media_session_message_new (session, action, &session_node);

  _add_content_descriptions (session, session_node, INITIATOR_LOCAL);

  GMS_DEBUG_INFO (session, "sending jingle action \"%s\" to peer", action);

  _gabble_connection_send_with_reply (priv->conn, msg, initiate_msg_reply_cb,
                                      G_OBJECT (session), NULL, NULL);

  lm_message_unref (msg);

  /* mark local streams as sent (so that eg candidates will be sent) */
  _mark_local_streams_sent (session);

  g_object_set (session, "state", JS_STATE_PENDING_INITIATE_SENT, NULL);
}

static LmHandlerResult
content_add_msg_reply_cb (GabbleConnection *conn,
                          LmMessage *sent_msg,
                          LmMessage *reply_msg,
                          GObject *object,
                          gpointer user_data)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (user_data);
  GabbleMediaStream *stream = GABBLE_MEDIA_STREAM (object);

  if (lm_message_get_sub_type (reply_msg) != LM_MESSAGE_SUB_TYPE_RESULT)
    {
      if (session->initiator == INITIATOR_REMOTE &&
          stream->signalling_state == STREAM_SIG_STATE_ACKNOWLEDGED)
        {
          GMS_DEBUG_INFO (session, "ignoring content-add failure, stream has "
              "been successfully created by the session initiator");
        }
      else
        {
          GMS_DEBUG_ERROR (session, "content-add failed; removing stream");
          NODE_DEBUG (sent_msg->node, "message sent");
          NODE_DEBUG (reply_msg->node, "message reply");

          _gabble_media_stream_close (stream);
        }
    }
  else
    {
      if (stream->signalling_state == STREAM_SIG_STATE_SENT)
        {
          GMS_DEBUG_INFO (session, "content-add succeeded, marking stream as "
              "ACKNOWLEDGED");

          g_object_set (stream,
              "signalling-state", STREAM_SIG_STATE_ACKNOWLEDGED,
              NULL);
        }
      else
        {
          GMS_DEBUG_INFO (session, "content-add succeeded, but not marking"
              "stream as ACKNOWLEDGED, it's in state %d",
              stream->signalling_state);
        }
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
do_content_add (GabbleMediaSession *session,
                GabbleMediaStream *stream)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  LmMessage *msg;
  LmMessageNode *session_node;

  g_assert (priv->state == JS_STATE_ACTIVE);
  g_assert (priv->mode == MODE_JINGLE);

  if (_stream_not_ready_for_initiate (session, stream))
    {
      GMS_DEBUG_ERROR (session, "trying to send content-add for stream %s "
          "but we have no local codecs. what?!", stream->name);
      g_assert_not_reached ();
      return;
    }

  msg = _gabble_media_session_message_new (session, "content-add",
      &session_node);

  _add_content_descriptions_one (session, stream, session_node);

  GMS_DEBUG_INFO (session, "sending jingle action \"content-add\" to peer for "
      "stream %s", stream->name);

  _gabble_connection_send_with_reply (priv->conn, msg,
      content_add_msg_reply_cb, G_OBJECT (stream), session, NULL);

  lm_message_unref (msg);

  /* mark stream as sent */
  g_object_set (stream, "signalling-state", STREAM_SIG_STATE_SENT, NULL);
}

static void
stream_connection_state_changed_cb (GabbleMediaStream *stream,
                                    GParamSpec *param,
                                    GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  if (stream->connection_state != TP_MEDIA_STREAM_STATE_CONNECTED)
    return;

  GMS_DEBUG_INFO (session, "stream %s has gone connected", stream->name);

  if (stream->playing)
    {
      GMS_DEBUG_INFO (session, "doing nothing, stream is already playing");
      return;
    }

  /* after session is active, we do things per-stream with content-* actions */
  if (priv->state < JS_STATE_ACTIVE)
    {
      /* send a session accept if the session was initiated by the peer */
      if (session->initiator == INITIATOR_REMOTE)
        {
          try_session_accept (session);
        }
      else
        {
          GMS_DEBUG_INFO (session, "session initiated by us, so we're not "
              "going to consider sending an accept");
        }
    }
  else
    {
      /* send a content accept if the stream was added by the peer */
      if (stream->initiator == INITIATOR_REMOTE)
        {
          try_content_accept (session, stream);
        }
      else
        {
          GMS_DEBUG_INFO (session, "stream added by us, so we're not going "
              "to send an accept");
        }
    }
}

static void
stream_got_local_codecs_changed_cb (GabbleMediaStream *stream,
                                    GParamSpec *param,
                                    GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  if (!stream->got_local_codecs)
    return;

  GMS_DEBUG_INFO (session, "stream %s has got local codecs", stream->name);

  if (stream->playing)
    {
      GMS_DEBUG_ERROR (session, "stream was already playing and we got local "
          "codecs. what?!");
      g_assert_not_reached ();
      return;
    }

  /* after session is active, we do things per-stream with content-* actions */
  if (priv->state < JS_STATE_ACTIVE)
    {
      if (session->initiator == INITIATOR_REMOTE)
        {
          if (priv->state < JS_STATE_PENDING_ACCEPT_SENT)
            {
              try_session_accept (session);
            }
          else
            {
              GMS_DEBUG_INFO (session, "stream added after sending accept; "
                  "not doing content-add until remote end acknowledges");
            }
        }
      else
        {
          if (priv->state < JS_STATE_PENDING_INITIATE_SENT)
            {
              try_session_initiate (session);
            }
          else
            {
              GMS_DEBUG_INFO (session, "stream added after sending initiate; "
                  "not doing content-add until remote end accepts");
            }
        }
    }
  else
    {
      if (stream->initiator == INITIATOR_REMOTE)
        {
          try_content_accept (session, stream);
        }
      else
        {
          do_content_add (session, stream);
        }
    }
}

static gchar *
get_jid_for_contact (GabbleMediaSession *session,
                     TpHandle handle)
{
  GabbleMediaSessionPrivate *priv;
  TpBaseConnection *conn;
  const gchar *base_jid;
  TpHandle self;
  TpHandleRepoIface *contact_handles;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  conn = (TpBaseConnection *)priv->conn;
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  self = conn->self_handle;

  base_jid = tp_handle_inspect (contact_handles, handle);
  g_assert (base_jid != NULL);

  if (handle == self)
    {
      gchar *resource, *ret;
      g_object_get (priv->conn, "resource", &resource, NULL);
      g_assert (resource != NULL);
      ret = g_strdup_printf ("%s/%s", base_jid, resource);
      g_free (resource);
      return ret;
    }
  else
    {
      g_assert (priv->peer_resource != NULL);
      return g_strdup_printf ("%s/%s", base_jid, priv->peer_resource);
    }
}

LmMessage *
_gabble_media_session_message_new (GabbleMediaSession *session,
                                   const gchar *action,
                                   LmMessageNode **session_node)
{
  GabbleMediaSessionPrivate *priv;
  TpBaseConnection *conn;
  LmMessage *msg;
  LmMessageNode *iq_node, *node;
  gchar *peer_jid, *initiator_jid;
  TpHandle initiator_handle;
  const gchar *element, *xmlns;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  conn = (TpBaseConnection *)priv->conn;

  peer_jid = get_jid_for_contact (session, priv->peer);

  msg = lm_message_new_with_sub_type (
      peer_jid,
      LM_MESSAGE_TYPE_IQ,
      LM_MESSAGE_SUB_TYPE_SET);

  g_free (peer_jid);

  iq_node = lm_message_get_node (msg);

  if (priv->mode == MODE_GOOGLE)
    element = "session";
  else
    element = "jingle";

  if (session->initiator == INITIATOR_LOCAL)
    initiator_handle = conn->self_handle;
  else
    initiator_handle = priv->peer;

  node = lm_message_node_add_child (iq_node, element, NULL);
  initiator_jid = get_jid_for_contact (session, initiator_handle);

  lm_message_node_set_attributes (node,
      (priv->mode == MODE_GOOGLE) ? "id" : "sid", priv->id,
      (priv->mode == MODE_GOOGLE) ? "type" : "action", action,
      "initiator", initiator_jid,
      NULL);

  if (priv->mode == MODE_GOOGLE)
    xmlns = NS_GOOGLE_SESSION;
  else
    xmlns = NS_JINGLE;

  lm_message_node_set_attribute (node, "xmlns", xmlns);
  g_free (initiator_jid);

  if (session_node)
    *session_node = node;

  return msg;
}

void
_gabble_media_session_accept (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;

  priv->locally_accepted = TRUE;

  /* accept any local pending sends */
  for (i = 0; i < priv->streams->len; i++)
    {
      GabbleMediaStream *stream = g_ptr_array_index (priv->streams, i);
      CombinedStreamDirection combined_dir = stream->combined_direction;
      TpMediaStreamDirection current_dir;
      TpMediaStreamPendingSend pending_send;

      current_dir = COMBINED_DIRECTION_GET_DIRECTION (combined_dir);
      pending_send = COMBINED_DIRECTION_GET_PENDING_SEND (combined_dir);

      if ((pending_send & TP_MEDIA_STREAM_PENDING_LOCAL_SEND) != 0)
        {
          GMS_DEBUG_INFO (session, "accepting pending local send on stream %s",
              stream->name);

          current_dir |= TP_MEDIA_STREAM_DIRECTION_SEND;
          pending_send &= ~TP_MEDIA_STREAM_PENDING_LOCAL_SEND;
          combined_dir = MAKE_COMBINED_DIRECTION (current_dir, pending_send);
          g_object_set (stream, "combined-direction", combined_dir, NULL);
          _gabble_media_stream_update_sending (stream, FALSE);
        }
    }

  try_session_accept (session);
}

static LmHandlerResult
content_remove_msg_reply_cb (GabbleConnection *conn,
                             LmMessage *sent_msg,
                             LmMessage *reply_msg,
                             GObject *object,
                             gpointer user_data)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  GPtrArray *removing = (GPtrArray *) user_data;
  guint i;

  MSG_REPLY_CB_END_SESSION_IF_NOT_SUCCESSFUL (session,
      "stream removal failed");

  for (i = 0; i < removing->len; i++)
    destroy_media_stream (session,
        GABBLE_MEDIA_STREAM (g_ptr_array_index (removing, i)));

  g_ptr_array_remove_fast (priv->remove_requests, removing);
  g_ptr_array_free (removing, TRUE);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

void
_gabble_media_session_remove_streams (GabbleMediaSession *session,
                                      GabbleMediaStream **streams,
                                      guint len)
{
  GabbleMediaSessionPrivate *priv;
  LmMessage *msg = NULL;
  LmMessageNode *session_node;
  GPtrArray *removing = NULL;
  guint i;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  /* end the session if there'd be no streams left after reducing it */
  if (_count_non_removing_streams (session) == len)
    {
      _gabble_media_session_terminate (session, INITIATOR_LOCAL,
          TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
      return;
    }

  /* construct a remove message if we're in a state greater than CREATED (ie
   * something has been sent/received about this session) */
  if (priv->state > JS_STATE_PENDING_CREATED)
    {
      msg = _gabble_media_session_message_new (session, "content-remove",
          &session_node);
      removing = g_ptr_array_sized_new (len);
    }

  /* right, remove them */
  for (i = 0; i < len; i++)
    {
      GabbleMediaStream *stream = streams[i];

      switch (stream->signalling_state)
        {
        case STREAM_SIG_STATE_NEW:
          destroy_media_stream (session, stream);
          break;
        case STREAM_SIG_STATE_SENT:
        case STREAM_SIG_STATE_ACKNOWLEDGED:
          {
            LmMessageNode *content_node;

            g_assert (msg != NULL);
            g_assert (removing != NULL);

            content_node = _gabble_media_stream_add_content_node (stream,
                session_node);

            g_object_set (stream,
                "playing", FALSE,
                "signalling-state", STREAM_SIG_STATE_REMOVING,
                NULL);

            /* close the stream now, but don't forget about it until the
             * removal message is acknowledged, since we need to be able to
             * detect content-remove cross-talk */
            _gabble_media_stream_close (stream);
            g_ptr_array_add (removing, stream);
          }
          break;
        case STREAM_SIG_STATE_REMOVING:
          break;
        }
    }

  /* send the remove message if necessary */
  if (msg != NULL)
    {
      if (removing->len > 0)
        {
          GMS_DEBUG_INFO (session, "sending jingle session action "
              "\"content-remove\" to peer");

          _gabble_connection_send_with_reply (priv->conn, msg,
              content_remove_msg_reply_cb, G_OBJECT (session), removing, NULL);

          g_ptr_array_add (priv->remove_requests, removing);
        }
      else
        {
          g_ptr_array_free (removing, TRUE);
        }

      lm_message_unref (msg);
    }
  else
    {
      GMS_DEBUG_INFO (session, "not sending jingle session action "
          "\"content-remove\" to peer, no initiates or adds sent for "
          "these streams");
    }
}

/* for when you want the reply to be removed from
 * the handler chain, but don't care what it is */
static LmHandlerResult
ignore_reply_cb (GabbleConnection *conn,
                 LmMessage *sent_msg,
                 LmMessage *reply_msg,
                 GObject *object,
                 gpointer user_data)
{
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
send_reject_message (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  LmMessage *msg;
  LmMessageNode *session_node;

  /* this should only happen in google mode, and we should only arrive in that
   * mode when we've ended up talking to a resource that doesn't support
   * jingle */
  g_assert (priv->mode == MODE_GOOGLE);
  g_assert (priv->peer_resource != NULL);

  /* construct a session terminate message */
  msg = _gabble_media_session_message_new (session, "reject", &session_node);

  GMS_DEBUG_INFO (session, "sending jingle session action \"reject\" to peer");

  /* send it */
  _gabble_connection_send_with_reply (priv->conn, msg, ignore_reply_cb,
                                      G_OBJECT (session), NULL, NULL);

  lm_message_unref (msg);
}

static void
send_terminate_message (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  const gchar *action;
  LmMessage *msg;
  LmMessageNode *session_node;

  /* construct a session terminate message */
  if (priv->mode == MODE_GOOGLE)
    action = "terminate";
  else
    action = "session-terminate";

  msg = _gabble_media_session_message_new (session, action, &session_node);

  GMS_DEBUG_INFO (session, "sending jingle session action \"%s\" to peer",
      action);

  /* send it */
  _gabble_connection_send_with_reply (priv->conn, msg, ignore_reply_cb,
                                      G_OBJECT (session), NULL, NULL);

  lm_message_unref (msg);
}

void
_gabble_media_session_terminate (GabbleMediaSession *session,
                                 JingleInitiator who,
                                 TpChannelGroupChangeReason why)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  TpBaseConnection *conn = (TpBaseConnection *)priv->conn;
  TpHandle actor;

  if (priv->state == JS_STATE_ENDED)
    return;

  if (who == INITIATOR_REMOTE)
    {
      actor = priv->peer;
    }
  else
    {
      actor = conn->self_handle;

      /* Need to tell them that it's all over. */

      /* Jingle doesn't have a "reject" action; a termination before an
       * acceptance indicates that the call has been declined */

      if (session->initiator == INITIATOR_REMOTE &&
          priv->state == JS_STATE_PENDING_INITIATED &&
          priv->mode == MODE_GOOGLE)
        {
          send_reject_message (session);
        }

      /* if we're still in CREATED, then we've not sent or received any
       * messages about this session yet, so no terminate is necessary */
      else if (priv->state > JS_STATE_PENDING_CREATED)
        {
          send_terminate_message (session);
        }

      while (priv->streams->len > 0)
        destroy_media_stream (session, g_ptr_array_index (priv->streams, 0));
    }

  priv->terminated = TRUE;
  g_object_set (session, "state", JS_STATE_ENDED, NULL);
  g_signal_emit (session, signals[TERMINATED], 0, actor, why);
}

#if _GMS_DEBUG_LEVEL
void
_gabble_media_session_debug (GabbleMediaSession *session,
                             DebugMessageType type,
                             const gchar *format, ...)
{
  if (DEBUGGING)
    {
      va_list list;
      gchar buf[512];
      GabbleMediaSessionPrivate *priv;
      time_t curtime;
      struct tm *loctime;
      gchar stamp[10];
      const gchar *type_str;

      g_assert (GABBLE_IS_MEDIA_SESSION (session));

      priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

      curtime = time (NULL);
      loctime = localtime (&curtime);

      strftime (stamp, sizeof (stamp), "%T", loctime);

      va_start (list, format);

      vsnprintf (buf, sizeof (buf), format, list);

      va_end (list);

      switch (type) {
        case DEBUG_MSG_INFO:
          type_str = TP_ANSI_BOLD_ON TP_ANSI_FG_WHITE;
          break;
        case DEBUG_MSG_DUMP:
          type_str = TP_ANSI_BOLD_ON TP_ANSI_FG_GREEN;
          break;
        case DEBUG_MSG_WARNING:
          type_str = TP_ANSI_BOLD_ON TP_ANSI_FG_YELLOW;
          break;
        case DEBUG_MSG_ERROR:
          type_str = TP_ANSI_BOLD_ON TP_ANSI_FG_WHITE TP_ANSI_BG_RED;
          break;
        case DEBUG_MSG_EVENT:
          type_str = TP_ANSI_BOLD_ON TP_ANSI_FG_CYAN;
          break;
        default:
          g_assert_not_reached ();
          return;
      }

      printf ("[%s%s%s] %s%-26s%s %s%s%s\n",
          TP_ANSI_BOLD_ON TP_ANSI_FG_WHITE,
          stamp,
          TP_ANSI_RESET,
          session_states[priv->state].attributes,
          session_states[priv->state].name,
          TP_ANSI_RESET,
          type_str,
          buf,
          TP_ANSI_RESET);

      fflush (stdout);
    }
}

#endif /* _GMS_DEBUG_LEVEL */

static const gchar *
_name_stream (GabbleMediaSession *session,
              TpMediaStreamType media_type)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  static gchar ret[MAX_STREAM_NAME_LEN] = GTALK_STREAM_NAME;

  if (priv->mode != MODE_GOOGLE)
    {
      guint i = 1;

      do {
          g_snprintf (ret, MAX_STREAM_NAME_LEN, "%s%u",
              media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video",
              i++);

          /* even though we now have seperate namespaces for local and remote,
           * actually check in both so that we can still support clients which
           * have 1 namespace (such as our older selves :D) */
          if (_lookup_stream_by_name_and_initiator (session, ret,
                INITIATOR_INVALID) != NULL)
            {
              ret[0] = '\0';
            }
      } while (ret[0] == '\0');
    }

  return ret;
}


gboolean
_gabble_media_session_request_streams (GabbleMediaSession *session,
                                       const GArray *media_types,
                                       GPtrArray **ret,
                                       GError **error)
{
  static GabblePresenceCapabilities google_audio_caps =
    PRESENCE_CAP_GOOGLE_VOICE;
  static GabblePresenceCapabilities jingle_audio_caps =
    PRESENCE_CAP_JINGLE | PRESENCE_CAP_JINGLE_DESCRIPTION_AUDIO |
    PRESENCE_CAP_GOOGLE_TRANSPORT_P2P;
  static GabblePresenceCapabilities jingle_video_caps =
    PRESENCE_CAP_JINGLE | PRESENCE_CAP_JINGLE_DESCRIPTION_VIDEO |
    PRESENCE_CAP_GOOGLE_TRANSPORT_P2P;

  GabbleMediaSessionPrivate *priv;
  GabblePresence *presence;
  gboolean want_audio, want_video;
  GabblePresenceCapabilities jingle_desired_caps;
  guint idx;
  gchar *dump;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  presence = gabble_presence_cache_get (priv->conn->presence_cache,
      priv->peer);

  if (presence == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "member has no audio/video capabilities");

      return FALSE;
    }

  dump = gabble_presence_dump (presence);
  GMS_DEBUG_DUMP (session, "presence for peer %d:\n%s", priv->peer, dump);
  g_free (dump);

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

  /* work out what we'd need to do these streams with jingle */
  jingle_desired_caps = 0;

  if (want_audio)
    jingle_desired_caps |= jingle_audio_caps;

  if (want_video)
    jingle_desired_caps |= jingle_video_caps;

  GMS_DEBUG_INFO (session, "want audio: %s; want video: %s",
    want_audio ? "yes" : "no", want_video ? "yes" : "no");

  /* existing call; the recipient and the mode has already been decided */
  if (priv->peer_resource)
    {
      /* is a google call... we have no other option */
      if (priv->mode == MODE_GOOGLE)
        {
          GMS_DEBUG_INFO (session, "already in Google mode; can't add new "
              "stream");

          g_assert (priv->streams->len == 1);

          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "Google Talk calls may only contain one stream");

          return FALSE;
        }

      if (!gabble_presence_resource_has_caps (presence, priv->peer_resource,
            jingle_desired_caps))
        {
          GMS_DEBUG_INFO (session,
            "in Jingle mode but have insufficient caps for requested streams");

          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "existing call member doesn't support all requested media"
              " types");

          return FALSE;
        }

      GMS_DEBUG_INFO (session,
        "in Jingle mode, and have necessary caps");
    }

  /* no existing call; we should choose a recipient and a mode */
  else
    {
      const gchar *resource;

      g_assert (priv->streams->len == 0);

      /* see if we have a fully-capable jingle resource; regardless of the
       * desired media type it's best if we can add/remove the others later */
      resource = gabble_presence_pick_resource_by_caps (presence,
          jingle_audio_caps | jingle_video_caps);

      if (resource == NULL)
        {
          GMS_DEBUG_INFO (session, "contact is not fully jingle-capable");

          /* ok, no problem. see if we can do just what's wanted with jingle */
          resource = gabble_presence_pick_resource_by_caps (presence,
              jingle_desired_caps);

          if (resource == NULL && want_audio && !want_video)
            {
              GMS_DEBUG_INFO (session,
                "contact doesn't have desired Jingle capabilities");

              /* last ditch... if we want only audio and not video, we can make
               * do with google talk */
              resource = gabble_presence_pick_resource_by_caps (presence,
                  google_audio_caps);

              if (resource != NULL)
                {
                  /* only one stream possible with google */
                  if (media_types->len == 1)
                    {
                      GMS_DEBUG_INFO (session,
                        "contact has no Jingle capabilities; "
                        "falling back to Google audio call");
                      priv->mode = MODE_GOOGLE;
                    }
                  else
                    {
                      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                          "Google Talk calls may only contain one stream");

                      return FALSE;
                    }
                }
              else
                {
                  GMS_DEBUG_INFO (session,
                    "contact doesn't have desired Google capabilities");
                }
            }
        }

      if (resource == NULL)
        {
          GMS_DEBUG_INFO (session,
            "contact doesn't have a resource with suitable capabilities");

          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "member does not have the desired audio/video capabilities");

          return FALSE;
        }

      priv->peer_resource = g_strdup (resource);
    }

  /* check it's not a ridiculous number of streams */
  if ((priv->streams->len + media_types->len) > MAX_STREAMS)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "I think that's quite enough streams already");
      return FALSE;
    }

  /* if we've got here, we're good to make the streams */

  *ret = g_ptr_array_sized_new (media_types->len);

  for (idx = 0; idx < media_types->len; idx++)
    {
      guint media_type = g_array_index (media_types, guint, idx);
      GabbleMediaStream *stream;
      const gchar *stream_name;

      if (priv->mode == MODE_GOOGLE)
        stream_name = GTALK_STREAM_NAME;
      else
        stream_name = _name_stream (session, media_type);

      stream = create_media_stream (session, stream_name, INITIATOR_LOCAL,
                                    media_type);

      g_ptr_array_add (*ret, stream);
    }

  return TRUE;
}

static const gchar *
_direction_to_senders (GabbleMediaSession *session,
                       TpMediaStreamDirection dir)
{
  const gchar *ret = NULL;

  switch (dir)
    {
      case TP_MEDIA_STREAM_DIRECTION_NONE:
        g_assert_not_reached ();
        break;
      case TP_MEDIA_STREAM_DIRECTION_SEND:
        if (session->initiator == INITIATOR_LOCAL)
          ret = "initiator";
        else
          ret = "responder";
        break;
      case TP_MEDIA_STREAM_DIRECTION_RECEIVE:
        if (session->initiator == INITIATOR_REMOTE)
          ret = "initiator";
        else
          ret = "responder";
        break;
      case TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL:
        ret = "both";
        break;
    }

  g_assert (ret != NULL);

  return ret;
}

static LmHandlerResult
direction_msg_reply_cb (GabbleConnection *conn,
                        LmMessage *sent_msg,
                        LmMessage *reply_msg,
                        GObject *object,
                        gpointer user_data)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (user_data);
  GabbleMediaStream *stream = GABBLE_MEDIA_STREAM (object);

  MSG_REPLY_CB_END_SESSION_IF_NOT_SUCCESSFUL (session,
      "direction change failed");

  if (stream->playing)
    {
      _gabble_media_stream_update_sending (stream, TRUE);
    }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean
send_direction_change (GabbleMediaSession *session,
                       GabbleMediaStream *stream,
                       TpMediaStreamDirection dir,
                       GError **error)
{
  GabbleMediaSessionPrivate *priv;
  const gchar *senders;
  LmMessage *msg;
  LmMessageNode *session_node, *content_node;
  gboolean ret;

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  senders = _direction_to_senders (session, dir);

  if (stream->signalling_state == STREAM_SIG_STATE_NEW ||
      stream->signalling_state == STREAM_SIG_STATE_REMOVING)
    {
      GMS_DEBUG_INFO (session, "not sending content-modify for %s stream %s",
          stream->signalling_state == STREAM_SIG_STATE_NEW ? "new" : "removing",
          stream->name);
      return TRUE;
    }

  GMS_DEBUG_INFO (session, "sending jingle session action \"content-modify\" "
      "to peer for stream %s (senders=%s)", stream->name, senders);

  msg = _gabble_media_session_message_new (session, "content-modify",
      &session_node);
  content_node = _gabble_media_stream_add_content_node (stream, session_node);

  lm_message_node_set_attribute (content_node, "senders", senders);

  ret = _gabble_connection_send_with_reply (priv->conn, msg,
      direction_msg_reply_cb, G_OBJECT (stream), session, error);

  lm_message_unref (msg);

  return ret;
}

gboolean
_gabble_media_session_request_stream_direction (GabbleMediaSession *session,
                                                GabbleMediaStream *stream,
                                                TpMediaStreamDirection requested_dir,
                                                GError **error)
{
  GabbleMediaSessionPrivate *priv;
  CombinedStreamDirection new_combined_dir;
  TpMediaStreamDirection current_dir; //, new_dir;
  TpMediaStreamPendingSend pending_send;

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  current_dir = COMBINED_DIRECTION_GET_DIRECTION (stream->combined_direction);
  pending_send = COMBINED_DIRECTION_GET_PENDING_SEND
    (stream->combined_direction);

  if (priv->mode == MODE_GOOGLE)
    {
      g_assert (current_dir == TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL);

      if (requested_dir == TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL)
        return TRUE;

      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Google Talk calls can only be bi-directional");
      return FALSE;
    }

  if (requested_dir == TP_MEDIA_STREAM_DIRECTION_NONE)
    {
      GMS_DEBUG_INFO (session, "request for NONE direction; removing stream");

      _gabble_media_session_remove_streams (session, &stream, 1);

      return TRUE;
    }

  /* if we're awaiting a local decision on sending... */
  if ((pending_send & TP_MEDIA_STREAM_PENDING_LOCAL_SEND) != 0)
    {
      /* clear the flag */
      pending_send &= ~TP_MEDIA_STREAM_PENDING_LOCAL_SEND;

      /* make our current_dir match what other end thinks (he thinks we're
       * bidirectional) so that we send the correct transitions */
      current_dir ^= TP_MEDIA_STREAM_DIRECTION_SEND;
    }

#if 0
  /* if we're asking the remote end to start sending, set the pending flag and
   * don't change our directionality just yet */
  new_dir = requested_dir;
  if (((current_dir & TP_MEDIA_STREAM_DIRECTION_RECEIVE) == 0) &&
      ((new_dir & TP_MEDIA_STREAM_DIRECTION_RECEIVE) != 0))
    {
      pending_send ^= TP_MEDIA_STREAM_PENDING_REMOTE_SEND;
      new_dir &= ~TP_MEDIA_STREAM_DIRECTION_RECEIVE;
    }
#endif

  /* make any necessary changes */
  new_combined_dir = MAKE_COMBINED_DIRECTION (requested_dir, pending_send);
  if (new_combined_dir != stream->combined_direction)
    {
      g_object_set (stream, "combined-direction", new_combined_dir, NULL);
      _gabble_media_stream_update_sending (stream, FALSE);
    }

  /* short-circuit sending a request if we're not asking for anything new */
  if (current_dir == requested_dir)
    return TRUE;

  /* send request */
  return send_direction_change (session, stream, requested_dir, error);
}

static void
session_handler_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcMediaSessionHandlerClass *klass =
    (TpSvcMediaSessionHandlerClass *)g_iface;

#define IMPLEMENT(x) tp_svc_media_session_handler_implement_##x (\
    klass, gabble_media_session_##x)
  IMPLEMENT(error);
  IMPLEMENT(ready);
#undef IMPLEMENT
}
