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

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEBUG_FLAG GABBLE_DEBUG_MEDIA

#include "ansi.h"
#include "debug.h"
#include "handles.h"
#include "namespaces.h"
#include "util.h"

#include "telepathy-errors.h"
#include "telepathy-helpers.h"

#include "gabble-connection.h"
#include "gabble-media-channel.h"
#include "gabble-media-stream.h"
#include "gabble-presence-cache.h"
#include "gabble-presence.h"

#include "gabble-media-session.h"
#include "gabble-media-session-signals-marshal.h"
#include "gabble-media-session-glue.h"

#include "media-factory.h"

G_DEFINE_TYPE(GabbleMediaSession, gabble_media_session, G_TYPE_OBJECT)

#define DEFAULT_SESSION_TIMEOUT 50000

#define GTALK_STREAM_NAME "gtalk"

/* signal enum */
enum
{
    NEW_ICE_STREAM_HANDLER,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_MEDIA_CHANNEL = 1,
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

  GHashTable *streams;
  gint pending_stream_count;

  gchar *id;
  GabbleHandle initiator;
  GabbleHandle peer;
  gchar *peer_resource;

  JingleSessionState state;

  GabbleMediaFactory *media_factory;

  gboolean ready;
  gboolean accepted;
  gboolean got_active_candidate_pair;

  guint timer_id;

  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_SESSION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MEDIA_SESSION, GabbleMediaSessionPrivate))

typedef struct {
    gchar *name;
    gchar *attributes;
} SessionStateDescription;

static const SessionStateDescription session_states[] =
{
    { "JS_STATE_PENDING_CREATED",   ANSI_BOLD_ON ANSI_FG_BLACK ANSI_BG_WHITE   },
    { "JS_STATE_PENDING_INITIATED", ANSI_BOLD_ON               ANSI_BG_MAGENTA },
    { "JS_STATE_ACTIVE",            ANSI_BOLD_ON               ANSI_BG_BLUE    },
    { "JS_STATE_ENDED",                                        ANSI_BG_RED     }
};

static void
gabble_media_session_init (GabbleMediaSession *obj)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (obj);

  priv->mode = MODE_JINGLE;
  priv->streams = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      g_object_unref);
}

static void stream_new_active_candidate_pair_cb (GabbleMediaStream *stream,
                                                 const gchar *native_candidate_id,
                                                 const gchar *remote_candidate_id,
                                                 GabbleMediaSession *session);
static void stream_new_native_candidate_cb (GabbleMediaStream *stream,
                                            const gchar *candidate_id,
                                            const GPtrArray *transports,
                                            GabbleMediaSession *session);
static void stream_ready_cb (GabbleMediaStream *stream,
                             const GPtrArray *codecs,
                             GabbleMediaSession *session);
static void stream_supported_codecs_cb (GabbleMediaStream *stream,
                                        const GPtrArray *codecs,
                                        GabbleMediaSession *session);

static void
_emit_new_stream (const gchar *name,
                  GabbleMediaStream *stream,
                  GabbleMediaSession *session)
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
  g_signal_emit (session, signals[NEW_ICE_STREAM_HANDLER], 0,
                 object_path, id, media_type,
                 TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL);

  g_free (object_path);
}

static guint
create_media_stream (GabbleMediaSession *session,
                     const gchar *name,
                     guint media_type)
{
  GabbleMediaSessionPrivate *priv;
  gchar *object_path;
  GabbleMediaStream *stream;
  guint id;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));
  g_assert (name != NULL);

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  id = _gabble_media_channel_get_stream_id (priv->channel);

  GMS_DEBUG_INFO (session,
      "creating new %s %s stream called \"%s\" with id %u",
      priv->mode == MODE_GOOGLE ? "google" : "jingle",
      media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video",
      name, id);

  object_path = g_strdup_printf ("%s/IceStream%u", priv->object_path, id);

  stream = g_object_new (GABBLE_TYPE_MEDIA_STREAM,
                         "media-session", session,
                         "object-path", object_path,
                         "mode", priv->mode,
                         "name", name,
                         "id", id,
                         "media-type", media_type,
                         NULL);

  g_signal_connect (stream, "new-active-candidate-pair",
                    (GCallback) stream_new_active_candidate_pair_cb,
                    session);
  g_signal_connect (stream, "new-native-candidate",
                    (GCallback) stream_new_native_candidate_cb,
                    session);
  g_signal_connect (stream, "ready",
                    (GCallback) stream_ready_cb,
                    session);
  g_signal_connect (stream, "supported-codecs",
                    (GCallback) stream_supported_codecs_cb,
                    session);

  g_hash_table_insert (priv->streams, g_strdup (name), stream);

  priv->pending_stream_count++;

  g_free (object_path);

  if (priv->ready)
    _emit_new_stream (name, stream, session);

  return id;
}

#if 0
static gboolean
_get_peer_resource (GabblePresence *presence, gchar **peer_resource,
                     GabbleMediaSessionMode *mode)
{
  const gchar *resource;

  resource = gabble_presence_pick_resource_by_caps (presence,
      PRESENCE_CAP_JINGLE);

  if (resource)
    {
      DEBUG ("using Jingle-capable resource %s\n", resource);
      *peer_resource = g_strdup (resource);
      *mode = MODE_JINGLE;
      return TRUE;
    }

  resource = gabble_presence_pick_resource_by_caps (presence,
      PRESENCE_CAP_GOOGLE_VOICE);

  if (resource)
    {
      DEBUG ("using GTalk-capable resource %s\n", resource);
      *peer_resource = g_strdup (resource);
      if (g_getenv ("GABBLE_JINGLE_FORCE"))
        *mode = MODE_JINGLE;
      else
        *mode = MODE_GOOGLE;
      return TRUE;
    }

  return FALSE;
}
#endif

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

  g_object_get (priv->channel,
                "connection", &priv->conn,
                "factory", &priv->media_factory,
                NULL);

  priv->state = JS_STATE_PENDING_CREATED;

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

#if 0
  if (!priv->peer_resource)
    {
      GabblePresence *presence;

      presence = gabble_presence_cache_get (priv->conn->presence_cache,
          priv->peer);

      if (NULL == presence ||
          !_get_peer_resource (presence, &priv->peer_resource, &priv->mode))
        g_critical ("%s: no voice resource found for remote handle", G_STRFUNC);
    }

  {
    GabbleMediaStream *stream;

    stream = create_media_stream (GABBLE_MEDIA_SESSION (obj), GTALK_STREAM_NAME,
        TP_MEDIA_STREAM_TYPE_AUDIO);
  }
#endif

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
      g_value_set_uint (value, priv->initiator);
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
      priv->initiator = g_value_get_uint (value);
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

  g_type_class_add_private (gabble_media_session_class, sizeof (GabbleMediaSessionPrivate));

  object_class->constructor = gabble_media_session_constructor;

  object_class->get_property = gabble_media_session_get_property;
  object_class->set_property = gabble_media_session_set_property;

  object_class->dispose = gabble_media_session_dispose;
  object_class->finalize = gabble_media_session_finalize;

  param_spec = g_param_spec_object ("media-channel", "GabbleMediaChannel object",
                                    "Gabble media channel object that owns this "
                                    "media session object.",
                                    GABBLE_TYPE_MEDIA_CHANNEL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA_CHANNEL, param_spec);

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
                                  "The GabbleHandle representing the contact "
                                  "who initiated the session.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_INITIATOR, param_spec);

  param_spec = g_param_spec_uint ("peer", "Session peer",
                                  "The GabbleHandle representing the contact "
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

  signals[NEW_ICE_STREAM_HANDLER] =
    g_signal_new ("new-ice-stream-handler",
                  G_OBJECT_CLASS_TYPE (gabble_media_session_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_session_marshal_VOID__STRING_INT_INT_INT,
                  G_TYPE_NONE, 4, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_media_session_class), &dbus_glib_gabble_media_session_object_info);
}

void
gabble_media_session_dispose (GObject *object)
{
  GabbleMediaSession *self = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  _gabble_media_session_terminate (self);

  if (priv->timer_id != 0)
    g_source_remove (priv->timer_id);

  g_object_unref (priv->conn);

  g_hash_table_destroy (priv->streams);
  priv->streams = NULL;

  _gabble_media_factory_free_sid (priv->media_factory, priv->id);

  if (G_OBJECT_CLASS (gabble_media_session_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_session_parent_class)->dispose (object);
}

void
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
 * Implements DBus method Error
 * on interface org.freedesktop.Telepathy.Ice.SessionHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_session_error (GabbleMediaSession *obj, guint errno, const gchar * message, GError **error)
{
  GMS_DEBUG_INFO (obj, "Media.SessionHandler::Error called -- terminating session");

  _gabble_media_session_terminate (obj);

  return TRUE;
}


/**
 * gabble_media_session_ready
 *
 * Implements DBus method Ready
 * on interface org.freedesktop.Telepathy.Ice.SessionHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_session_ready (GabbleMediaSession *obj, GError **error)
{
  GabbleMediaSessionPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_SESSION (obj));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (obj);

  priv->ready = TRUE;

  g_hash_table_foreach (priv->streams, (GHFunc) _emit_new_stream, obj);

  return TRUE;
}

static GabbleMediaStream *
_lookup_stream_by_name (GabbleMediaSession *session,
                        const gchar *stream_name)
{
  GabbleMediaSessionPrivate *priv;
  GabbleMediaStream *stream;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  stream = g_hash_table_lookup (priv->streams, stream_name);

  return stream;
}


static gboolean
_handle_create (GabbleMediaSession *session,
                LmMessage *message,
                LmMessageNode *content_node,
                const gchar *stream_name,
                GabbleMediaStream *stream,
                LmMessageNode *desc_node,
                LmMessageNode *trans_node)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  GabbleMediaSessionMode session_mode;
  TpMediaStreamType stream_type;

  if (stream != NULL)
    {
      GMS_DEBUG_WARNING (session, "can't create new stream called \"%s\", it "
          "already exists; rejecting", stream_name);
      return FALSE;
    }

  if (desc_node == NULL)
    {
      GMS_DEBUG_WARNING (session, "unable to create stream without a "
          "content description");
      return FALSE;
    }

  if (lm_message_node_has_namespace (desc_node,
        NS_GOOGLE_SESSION_PHONE))
    {
      session_mode = MODE_GOOGLE;
      stream_type = TP_MEDIA_STREAM_TYPE_AUDIO;
    }
  else if (lm_message_node_has_namespace (desc_node,
        NS_JINGLE_DESCRIPTION_AUDIO))
    {
      session_mode = MODE_JINGLE;
      stream_type = TP_MEDIA_STREAM_TYPE_AUDIO;
    }
  else if (lm_message_node_has_namespace (desc_node,
        NS_JINGLE_DESCRIPTION_VIDEO))
    {
      session_mode = MODE_JINGLE;
      stream_type = TP_MEDIA_STREAM_TYPE_VIDEO;
    }
  else
    {
      GMS_DEBUG_WARNING (session, "refusing to create stream for "
          "unsupported content description");
      return FALSE;
    }

  /* MODE_GOOGLE is allowed to have a null transport node */
  if (session_mode == MODE_JINGLE && trans_node == NULL)
    {
      GMS_DEBUG_WARNING (session, "refusing to create stream for "
          "unsupported transport");
      return FALSE;
    }

  if (session_mode != priv->mode)
    {
      if (g_hash_table_size (priv->streams) > 0)
        {
          GMS_DEBUG_WARNING (session, "refusing to change mode because "
              "streams already exist");
          return FALSE;
        }
      else
        {
          GMS_DEBUG_INFO (session, "setting session mode to %s",
              session_mode == MODE_GOOGLE ? "google" : "jingle");
          priv->mode = session_mode;
        }
    }

  create_media_stream (session, stream_name, stream_type);

  return TRUE;
}


static gboolean
_handle_codecs (GabbleMediaSession *session,
                LmMessage *message,
                LmMessageNode *content_node,
                const gchar *stream_name,
                GabbleMediaStream *stream,
                LmMessageNode *desc_node,
                LmMessageNode *trans_node)
{
  if (stream == NULL)
    {
      GMS_DEBUG_WARNING (session, "unable to handle codecs for unknown stream "
          "\"%s\"", stream_name);
      return FALSE;
    }

  if (desc_node == NULL)
    {
      GMS_DEBUG_WARNING (session, "unable to handle codecs without a content "
          "description node");
      return FALSE;
    }

  if (!_gabble_media_stream_post_remote_codecs (stream, message, desc_node))
    {
      GMS_DEBUG_INFO (session, "_gabble_media_stream_post_remote_codecs "
          "failed");
      return FALSE;
    }

  return TRUE;
}


static gboolean
_handle_candidates (GabbleMediaSession *session,
                    LmMessage *message,
                    LmMessageNode *content_node,
                    const gchar *stream_name,
                    GabbleMediaStream *stream,
                    LmMessageNode *desc_node,
                    LmMessageNode *trans_node)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  if (stream == NULL)
    {
      GMS_DEBUG_WARNING (session, "unable to handle candidates for unknown "
          "stream \"%s\"", stream_name);
      return FALSE;
    }

  if (trans_node == NULL)
    {
      if (priv->mode == MODE_GOOGLE)
        {
          trans_node = content_node;
        }
      else
        {
          GMS_DEBUG_WARNING (session, "unable to handle candidates without a "
              "transport node");
          return FALSE;
        }
    }

  if (!_gabble_media_stream_post_remote_candidates (stream, message,
        trans_node))
    {
      GMS_DEBUG_INFO (session, "_gabble_media_stream_post_remote_candidates "
          "failed");
      return FALSE;
    }

  return TRUE;
}


typedef gboolean (*StreamHandlerFunc)(GabbleMediaSession *session,
                                      LmMessage *message,
                                      LmMessageNode *content_node,
                                      const gchar *stream_name,
                                      GabbleMediaStream *stream,
                                      LmMessageNode *desc_node,
                                      LmMessageNode *trans_node);

typedef struct _Handler Handler;

struct _Handler {
  const gchar *actions[3];
  JingleSessionState min_allowed_state;
  JingleSessionState max_allowed_state;
  StreamHandlerFunc stream_handlers[3];
  JingleSessionState new_state;
};

static Handler handlers[] = {
  {
    { "initiate", "session-initiate", NULL },
    JS_STATE_PENDING_CREATED,
    JS_STATE_PENDING_CREATED,
    { _handle_create, _handle_codecs, NULL },
    JS_STATE_PENDING_INITIATED
  },
  {
    { "candidates", "transport-info", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ACTIVE,
    { _handle_candidates, NULL },
    JS_STATE_INVALID
  },
  {
    { "accept", "session-accept", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_PENDING_INITIATED,
    { _handle_codecs, NULL },
    JS_STATE_ACTIVE
  },
  {
    { "reject", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_PENDING_INITIATED,
    { NULL },
    JS_STATE_ENDED
  },
  {
    { "terminate", "session-terminate", NULL },
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ENDED,
    { NULL },
    JS_STATE_ENDED
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
                          StreamHandlerFunc *func)
{
  GabbleMediaStream *stream = NULL;
  LmMessageNode *desc_node, *trans_node;
  StreamHandlerFunc *tmp;

  desc_node = lm_message_node_get_child (content_node, "description");

  trans_node = lm_message_node_get_child_with_namespace (content_node,
      "transport", NS_GOOGLE_TRANSPORT_P2P);

  for (tmp = func; *tmp != NULL; tmp++)
    {
       /* handlers may create the stream */
       if (stream == NULL)
         stream = _lookup_stream_by_name (session, stream_name);

       if (!(*tmp) (session, message, content_node, stream_name, stream,
             desc_node, trans_node))
       return FALSE;
    }

  return TRUE;
}


static gboolean
_call_handlers_on_streams (GabbleMediaSession *session,
                           LmMessage *message,
                           LmMessageNode *session_node,
                           StreamHandlerFunc *func)
{
  if (lm_message_node_has_namespace (session_node, NS_GOOGLE_SESSION))
    {
      if (!_call_handlers_on_stream (session, message, session_node,
            GTALK_STREAM_NAME, func))
        return FALSE;
    }
  else
    {
      LmMessageNode *content_node;

      for (content_node = session_node->children;
           NULL != content_node;
           content_node = content_node->next)
        {
          const gchar *stream_name;

          if (g_strdiff (content_node->name, "content"))
            continue;

          stream_name = lm_message_node_get_attribute (content_node, "name");

          if (stream_name == NULL)
            {
              GMS_DEBUG_WARNING (session, "rejecting content node with no "
                  "name");
              return FALSE;
            }

          if (!_call_handlers_on_stream (session, message, content_node,
                stream_name, func))
            return FALSE;
        }
    }

  return TRUE;
}


void
_gabble_media_session_handle_action (GabbleMediaSession *session,
                                     LmMessage *message,
                                     LmMessageNode *session_node,
                                     const gchar *action)
{
  GabbleMediaSessionPrivate *priv;
  StreamHandlerFunc *func = NULL;
  JingleSessionState new_state = JS_STATE_INVALID;
  Handler *i;
  const gchar **tmp;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  GMS_DEBUG_INFO (session, "got jingle session action \"%s\" from peer",
                  action);

  /* do the state machine dance */

  for (i = handlers; NULL != i->actions[0]; i++)
    {
      for (tmp = i->actions; NULL != *tmp; tmp++)
        if (0 == strcmp (*tmp, action))
          break;

      if (NULL == *tmp)
        continue;

      if (priv->state < i->min_allowed_state ||
          priv->state > i->max_allowed_state)
        {
          GMS_DEBUG_ERROR (session, "action \"%s\" not allowed in current "
              "state; terminating session", action);
          goto ACK_FAILURE;
        }

      func = i->stream_handlers;
      new_state = i->new_state;

      break;
    }

  if (NULL == func)
    {
      GMS_DEBUG_ERROR (session, "received unrecognised action \"%s\"; "
          "terminating session", action);
      goto ACK_FAILURE;
    }

  if (NULL != *func)
    {
      if (!_call_handlers_on_streams (session, message, session_node, func))
        goto FUNC_ERROR;
    }

  if (JS_STATE_INVALID != new_state)
    g_object_set (session, "state", new_state, NULL);

  _gabble_connection_acknowledge_set_iq (priv->conn, message);

  return;

FUNC_ERROR:
  GMS_DEBUG_ERROR (session, "error encountered with action \"%s\" in current "
      "state; terminating session", action);

ACK_FAILURE:
  _gabble_connection_send_iq_error (priv->conn, message,
                                    XMPP_ERROR_NOT_ALLOWED);
  _gabble_media_session_terminate (session);
}

static gboolean
timeout_session (gpointer data)
{
  GabbleMediaSession *session = data;

  DEBUG ("session timed out");

  _gabble_media_session_terminate (session);

  return FALSE;
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

  if (new_state == JS_STATE_PENDING_INITIATED)
    {
      priv->timer_id =
        g_timeout_add (DEFAULT_SESSION_TIMEOUT, timeout_session, session);
    }
  else if (new_state == JS_STATE_ACTIVE)
    {
      g_source_remove (priv->timer_id);
      priv->timer_id = 0;
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

  MSG_REPLY_CB_END_SESSION_IF_NOT_SUCCESSFUL (session, "accept failed");

  g_object_set (session, "state", JS_STATE_ACTIVE, NULL);

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
_set_streams_playing_one (const gchar *name,
                          GabbleMediaStream *stream,
                          GabbleMediaSession *session)
{
  GMS_DEBUG_INFO (session, "setting stream \"%s\" playing", name);

  _gabble_media_stream_set_playing (stream, TRUE);
}

static void
_set_streams_playing (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  g_hash_table_foreach (priv->streams, (GHFunc) _set_streams_playing_one, session);
}

typedef struct _AddDescriptionsData AddDescriptionsData;

struct _AddDescriptionsData {
  GabbleMediaSession *session;
  LmMessageNode *session_node;
};

static void
_add_content_descriptions_one (const gchar *name,
                               GabbleMediaStream *stream,
                               AddDescriptionsData *data)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (data->session);
  LmMessageNode *content_node;

  if (priv->mode == MODE_GOOGLE)
    {
      content_node = data->session_node;
    }
  else
    {
      content_node = lm_message_node_add_child (data->session_node, "content", NULL);
      lm_message_node_set_attribute (content_node, "name", name);
    }

  _gabble_media_stream_content_node_add_description (stream, content_node);

  _gabble_media_stream_content_node_add_transport (stream, content_node);
}

static void
_add_content_descriptions (GabbleMediaSession *session,
                           LmMessageNode *session_node)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  AddDescriptionsData data;

  data.session = session;
  data.session_node = session_node;

  g_hash_table_foreach (priv->streams, (GHFunc) _add_content_descriptions_one, &data);
}

static void
try_session_accept (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  LmMessage *msg;
  LmMessageNode *session_node;
  const gchar *action;

  if (!priv->accepted ||
      !priv->got_active_candidate_pair ||
      priv->pending_stream_count > 0)
    {
      GMS_DEBUG_INFO (session, "not sending accept yet, waiting for "
          "acceptance, active candidate pair or streams to become ready");
      return;
    }

  /* Start the stream playing */
  _set_streams_playing (session);

  if (priv->mode == MODE_GOOGLE)
    action = "accept";
  else
    action = "session-accept";

  /* construct a session acceptance message */
  msg = _gabble_media_session_message_new (session, action, &session_node);

  _add_content_descriptions (session, session_node);

  GMS_DEBUG_INFO (session, "sending jingle session action \"%s\" to peer", action);

  /* send the final acceptance message */
  _gabble_connection_send_with_reply (priv->conn, msg, accept_msg_reply_cb,
                                      G_OBJECT (session), NULL, NULL);

  lm_message_unref (msg);
}

static void
stream_new_active_candidate_pair_cb (GabbleMediaStream *stream,
                                     const gchar *native_candidate_id,
                                     const gchar *remote_candidate_id,
                                     GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  GMS_DEBUG_INFO (session, "voip-engine reported a new active candidate pair [\"%s\" - \"%s\"]",
                  native_candidate_id, remote_candidate_id);

  if (priv->got_active_candidate_pair)
    {
      GMS_DEBUG_INFO (session, "not doing anything, already got an active candidate pair");
      return;
    }

  priv->got_active_candidate_pair = TRUE;

  /* send a session accept if the session was initiated by the peer */
  if (priv->initiator == priv->peer)
    {
      try_session_accept (session);
    }
  else
    {
      GMS_DEBUG_INFO (session, "session initiated by us, so we're not going to send an accept");
      _set_streams_playing (session);
    }
}

static void
stream_new_native_candidate_cb (GabbleMediaStream *stream,
                                const gchar *candidate_id,
                                const GPtrArray *transports,
                                GabbleMediaSession *session)
{
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

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void
send_initiate_message (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv;
  LmMessage *msg;
  LmMessageNode *session_node;
  const gchar *action;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  if (priv->mode == MODE_GOOGLE)
      action = "initiate";
  else
      action = "session-initiate";

  msg = _gabble_media_session_message_new (session, action, &session_node);

  _add_content_descriptions (session, session_node);

  GMS_DEBUG_INFO (session, "sending jingle action \"%s\" to peer", action);

  _gabble_connection_send_with_reply (priv->conn, msg, initiate_msg_reply_cb,
                                      G_OBJECT (session), NULL, NULL);

  lm_message_unref (msg);
}

static void
stream_ready_cb (GabbleMediaStream *stream,
                 const GPtrArray *codecs,
                 GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  priv->pending_stream_count--;

  /* any more streams pending to be signalled ready? */
  if (priv->pending_stream_count > 0)
    {
      GMS_DEBUG_INFO (session, "holding our horses and badgers because there's "
          "still %d stream%s waiting to be signalled ready",
          priv->pending_stream_count,
          (priv->pending_stream_count > 1) ? "s" : "");

      return;
    }

  if (priv->initiator == priv->peer)
    {
      /* now we have the codecs, we can possibly send an accept if the session
       * was initiated by the peer */
      try_session_accept (session);
    }
  else
    {
      /* send an invitation if the session was initiated by us */
      send_initiate_message (session);
    }
}

static void
stream_supported_codecs_cb (GabbleMediaStream *stream,
                            const GPtrArray *codecs,
                            GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv;
  /*LmMessageNode *session_node;*/

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  if (priv->initiator != priv->peer)
    {
      GMS_DEBUG_INFO (session, "%s: session not initiated by peer so we're "
                      "not preparing an accept message",
                      G_STRFUNC);
      return;
    }
}

static gchar *
get_jid_for_contact (GabbleMediaSession *session,
                     GabbleHandle handle)
{
  GabbleMediaSessionPrivate *priv;
  const gchar *base_jid;
  GabbleHandle self;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  self = priv->conn->self_handle;

  base_jid = gabble_handle_inspect (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, handle);
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
  LmMessage *msg;
  LmMessageNode *iq_node, *node;
  gchar *peer_jid, *initiator_jid;
  const gchar *element, *xmlns;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

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

  node = lm_message_node_add_child (iq_node, element, NULL);
  initiator_jid = get_jid_for_contact (session, priv->initiator);

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

  priv->accepted = TRUE;

  try_session_accept (session);
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

  if (priv->peer_resource == NULL)
    return;

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

  if (priv->peer_resource == NULL)
    return;

  /* construct a session terminate message */
  if (priv->mode == MODE_GOOGLE)
    action = "terminate";
  else
    action = "session-terminate";

  msg = _gabble_media_session_message_new (session, action, &session_node);

  GMS_DEBUG_INFO (session, "sending jingle session action \"%s\" to peer", action);

  /* send it */
  _gabble_connection_send_with_reply (priv->conn, msg, ignore_reply_cb,
                                      G_OBJECT (session), NULL, NULL);

  lm_message_unref (msg);
}

void
_gabble_media_session_terminate (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  if (priv->state == JS_STATE_ENDED)
    return;

  /* Jingle doesn't have a "reject" action; a termination before an acceptance
   * indicates that the call has been declined */

  if (priv->initiator == priv->peer &&
      priv->state == JS_STATE_PENDING_INITIATED &&
      priv->mode == MODE_GOOGLE)
    {
      send_reject_message (session);
    }

  send_terminate_message (session);

  g_object_set (session, "state", JS_STATE_ENDED, NULL);
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
          type_str = ANSI_BOLD_ON ANSI_FG_WHITE;
          break;
        case DEBUG_MSG_DUMP:
          type_str = ANSI_BOLD_ON ANSI_FG_GREEN;
          break;
        case DEBUG_MSG_WARNING:
          type_str = ANSI_BOLD_ON ANSI_FG_YELLOW;
          break;
        case DEBUG_MSG_ERROR:
          type_str = ANSI_BOLD_ON ANSI_FG_WHITE ANSI_BG_RED;
          break;
        case DEBUG_MSG_EVENT:
          type_str = ANSI_BOLD_ON ANSI_FG_CYAN;
          break;
        default:
          g_assert_not_reached ();
          return;
      }

      printf ("[%s%s%s] %s%-26s%s %s%s%s\n",
          ANSI_BOLD_ON ANSI_FG_WHITE,
          stamp,
          ANSI_RESET,
          session_states[priv->state].attributes,
          session_states[priv->state].name,
          ANSI_RESET,
          type_str,
          buf,
          ANSI_RESET);

      fflush (stdout);
    }
}

#endif /* _GMS_DEBUG_LEVEL */

void
_gabble_media_session_stream_state (GabbleMediaSession *session, guint state)
{
  GabbleMediaSessionPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  _gabble_media_channel_stream_state (priv->channel, state);
}

#define NAME_MAX_LEN 10

static const gchar *
_name_stream (GabbleMediaSession *session,
              TpMediaStreamType media_type)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  static gchar ret[NAME_MAX_LEN] = GTALK_STREAM_NAME;

  if (priv->mode != MODE_GOOGLE)
    {
      guint i = 1;
      guint len;

      do {
          len = g_snprintf (ret, NAME_MAX_LEN, "%s%u",
              media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video",
              i++);

          if (g_hash_table_lookup (priv->streams, ret) != NULL)
            {
              ret[0] = '\0';
            }

          if (len > NAME_MAX_LEN)
            {
              ret[0] = '\0';
              break;
            }
      } while (ret[0] == '\0');
    }

  if (ret[0] == '\0')
    return NULL;
  else
    return ret;
}


gboolean
_gabble_media_session_request_streams (GabbleMediaSession *session,
                                       const GArray *media_types,
                                       GArray **ret,
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

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  presence = gabble_presence_cache_get (priv->conn->presence_cache,
      priv->peer);

  if (presence == NULL)
    {
      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "member has no "
          "audio/video capabilities");

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
          *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument, "given "
            "media type %u is invalid", media_type);
          return FALSE;
        }
    }

  /* work out what we'd need to do these streams with jingle */
  jingle_desired_caps = 0;

  if (want_audio)
    jingle_desired_caps |= jingle_audio_caps;

  if (want_video)
    jingle_desired_caps |= jingle_video_caps;

  /* existing call; the recipient and the mode has already been decided */
  if (priv->peer_resource)
    {
      /* is a google call... we have no other option */
      if (priv->mode == MODE_GOOGLE)
        {
          g_assert (g_hash_table_size (priv->streams) == 1);

          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "google talk "
              "calls may only contain one stream");

          return FALSE;
        }

      if (!gabble_presence_resource_has_caps (presence, priv->peer_resource,
            jingle_desired_caps))
        {
          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "existing "
              "call member doesn't support all requested media types");

          return FALSE;
        }
    }

  /* no existing call; we should choose a recipient and a mode */
  else
    {
      const gchar *resource;

      g_assert (g_hash_table_size (priv->streams) == 0);

      /* see if we have a fully-capable jingle resource; regardless of the
       * desired media type it's best if we can add/remove the others later */
      resource = gabble_presence_pick_resource_by_caps (presence,
          jingle_audio_caps | jingle_video_caps);

      if (resource == NULL)
        {
          /* ok, no problem. see if we can do just what's wanted with jingle */
          resource = gabble_presence_pick_resource_by_caps (presence,
              jingle_desired_caps);

          if (resource == NULL && want_audio && !want_video)
            {
              /* last ditch... if we want only audio and not video, we can make
               * do with google talk */
              resource = gabble_presence_pick_resource_by_caps (presence,
                  google_audio_caps);

              if (resource != NULL)
                {
                  /* only one stream possible with google */
                  if (media_types->len == 1)
                    {
                      priv->mode = MODE_GOOGLE;
                    }
                  else
                    {
                      *error = g_error_new (TELEPATHY_ERRORS, NotAvailable,
                          "google talk calls may only contain one stream");

                      return FALSE;
                    }
                }
            }
        }

      if (resource == NULL)
        {
          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "member does "
              "not have the desired audio/video capabilities");

          return FALSE;
        }

      priv->peer_resource = g_strdup (resource);
    }

  /* if we've got here, we're good to make the streams */

  *ret = g_array_new (FALSE, FALSE, sizeof (guint));

  for (idx = 0; idx < media_types->len; idx++)
    {
      guint media_type = g_array_index (media_types, guint, idx);
      const gchar *stream_name;
      guint stream_id;

      stream_name = _name_stream (session, media_type);

      /* if we've got over 99999 streams of a certain type... */
      if (stream_name == NULL)
        {
          *error = g_error_new (TELEPATHY_ERRORS, NotAvailable, "I think "
              "that's quite enough streams already");

          g_array_free (*ret, TRUE);
          return FALSE;
        }

      stream_id = create_media_stream (session, stream_name, media_type);
      g_array_append_val (*ret, stream_id);
    }

  return TRUE;
}

