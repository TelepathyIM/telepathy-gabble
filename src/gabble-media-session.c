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

#include "gabble-media-session.h"
#include "gabble-media-session-signals-marshal.h"

#include "gabble-media-session-glue.h"

#include "gabble-media-stream.h"

#include "gabble-media-channel.h"

#include "gabble-connection.h"
#include "handles.h"

#include "telepathy-helpers.h"

G_DEFINE_TYPE(GabbleMediaSession, gabble_media_session, G_TYPE_OBJECT)

#define DEFAULT_SESSION_TIMEOUT 50000

/* signal enum */
enum
{
    NEW_MEDIA_STREAM_HANDLER,
    INVITATION_RECEIVED,
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
  PROP_STATE,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleMediaSessionPrivate GabbleMediaSessionPrivate;

struct _GabbleMediaSessionPrivate
{
  GabbleConnection *conn;
  GabbleMediaChannel *channel;
  gchar *object_path;

  GabbleMediaStream *stream;

  gchar *id;
  GabbleHandle initiator;
  GabbleHandle peer;
  gchar *voice_resource;

  JingleSessionState state;

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
  //GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
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
create_media_stream (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv;
  gchar *object_path;
  GabbleMediaStream *stream;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  object_path = g_strdup_printf ("%s/MediaStream1", priv->object_path);

  stream = g_object_new (GABBLE_TYPE_MEDIA_STREAM,
                         "media-session", session,
                         "object-path", object_path,
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

  priv->stream = stream;

  g_free (object_path);
}

static GObject *
gabble_media_session_constructor (GType type, guint n_props,
                                  GObjectConstructParam *props)
{
  GObject *obj;
  GabbleMediaSessionPrivate *priv;
  DBusGConnection *bus;
  ContactPresence *cp;

  obj = G_OBJECT_CLASS (gabble_media_session_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (GABBLE_MEDIA_SESSION (obj));

  g_object_get (priv->channel, "connection", &priv->conn, NULL);

  priv->state = JS_STATE_PENDING_CREATED;

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  cp = gabble_handle_get_qdata (priv->conn->handles,
                                TP_HANDLE_TYPE_CONTACT, priv->peer,
                                _get_contact_presence_quark ());
  g_assert (cp != NULL);
  g_assert (cp->voice_resource != NULL);
  priv->voice_resource = g_strdup (cp->voice_resource);

  create_media_stream (GABBLE_MEDIA_SESSION (obj));

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

  param_spec = g_param_spec_uint ("state", "Session state",
                                  "The current state that the session is in.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  signals[NEW_MEDIA_STREAM_HANDLER] =
    g_signal_new ("new-media-stream-handler",
                  G_OBJECT_CLASS_TYPE (gabble_media_session_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_session_marshal_VOID__STRING_INT_INT,
                  G_TYPE_NONE, 3, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_UINT, G_TYPE_UINT);

  signals[INVITATION_RECEIVED] =
    g_signal_new ("invitation-received",
                  G_OBJECT_CLASS_TYPE (gabble_media_session_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

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

  if (priv->timer_id != 0)
    g_source_remove (priv->timer_id);

  g_object_unref (priv->conn);

  g_object_unref (priv->stream);
  priv->stream = NULL;

  _gabble_connection_jingle_session_unregister (priv->conn, priv->id);

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
  g_free (priv->voice_resource);

  G_OBJECT_CLASS (gabble_media_session_parent_class)->finalize (object);
}


/**
 * gabble_media_session_error
 *
 * Implements DBus method Error
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
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
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
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
  gchar *object_path;

  g_assert (GABBLE_IS_MEDIA_SESSION (obj));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (obj);

  g_object_get (priv->stream, "object-path", &object_path, NULL);

  g_signal_emit (obj, signals[NEW_MEDIA_STREAM_HANDLER], 0,
                 object_path, TP_MEDIA_STREAM_TYPE_AUDIO,
                 TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL);

  g_free (object_path);

  return TRUE;
}

void
_gabble_media_session_handle_action (GabbleMediaSession *session,
                                     LmMessageNode *iq_node,
                                     LmMessageNode *session_node,
                                     const gchar *action)
{
  GabbleMediaSessionPrivate *priv;
  LmMessageNode *desc_node;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  GMS_DEBUG_INFO (session, "got jingle session action \"%s\" from peer",
                  action);

  /* do the state machine dance */

  if (strcmp (action, "candidates") == 0) /* "negotiate" in JEP */
    {
      if (priv->state < JS_STATE_PENDING_INITIATED || priv->state >= JS_STATE_ENDED)
        goto ACK_FAILURE;

      if (!_gabble_media_stream_post_remote_candidates (priv->stream, iq_node, session_node))
        {
          GMS_DEBUG_ERROR (session, "%s: gabble_media_stream_post_remote_candidates failed",
                           G_STRFUNC);
          HANDLER_DEBUG (session_node, "session_node");
        }

      return;
    }
  else if (strcmp (action, "initiate") == 0)
    {
      if (priv->state != JS_STATE_PENDING_CREATED)
        goto ACK_FAILURE;

      desc_node = lm_message_node_get_child (session_node, "description");
      if (!desc_node)
        goto ACK_FAILURE;

      if (_gabble_media_stream_post_remote_codecs (priv->stream, iq_node, desc_node))
        {
          g_object_set (session, "state", JS_STATE_PENDING_INITIATED, NULL);
        }
      else
        {
          goto ACK_FAILURE;
        }

      return;
    }
  else if (strcmp (action, "accept") == 0)
    {
      if (priv->state != JS_STATE_PENDING_INITIATED)
        goto ACK_FAILURE;

      desc_node = lm_message_node_get_child (session_node, "description");
      if (!desc_node)
        goto ACK_FAILURE;

      if (_gabble_media_stream_post_remote_codecs (priv->stream, iq_node, desc_node))
        {
          g_object_set (session, "state", JS_STATE_ACTIVE, NULL);
        }
      else
        {
          goto ACK_FAILURE;
        }

      return;
    }
  else if (strcmp (action, "reject") == 0)
    {
      if (priv->state != JS_STATE_PENDING_INITIATED)
        goto ACK_FAILURE;

      g_object_set (session, "state", JS_STATE_ENDED, NULL);
    }
  else if (strcmp (action, "terminate") == 0)
    {
      if (priv->state < JS_STATE_PENDING_INITIATED)
        goto ACK_FAILURE;

      g_object_set (session, "state", JS_STATE_ENDED, NULL);
    }

/*ACK_SUCCESS:*/
  _gabble_connection_send_iq_ack (priv->conn, iq_node, LM_MESSAGE_SUB_TYPE_RESULT);
  return;

ACK_FAILURE:
  GMS_DEBUG_ERROR (session, "error encountered with action \"%s\" in current state -- terminating session", action);

  _gabble_connection_send_iq_ack (priv->conn, iq_node, LM_MESSAGE_SUB_TYPE_ERROR);

  _gabble_media_session_terminate (session);
}

static gboolean
timeout_session (gpointer data)
{
  GabbleMediaSession *session = data;

  g_debug ("%s: session timed out", G_STRFUNC);

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
try_session_accept (GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  LmMessage *msg;
  LmMessageNode *session_node;

  if (!priv->accepted || !priv->got_active_candidate_pair)
    {
      GMS_DEBUG_INFO (session, "not sending accept yet, waiting for acceptance or active candidate pair");
      return;
    }

  /* Start the stream playing */
  GMS_DEBUG_INFO (session, "setting stream playing");
  _gabble_media_stream_set_playing (priv->stream, 1);

  /* construct a session acceptance message */
  msg = _gabble_media_session_message_new (session, "accept", &session_node);

  _gabble_media_stream_session_node_add_description (priv->stream, session_node);

  GMS_DEBUG_INFO (session, "sending jingle session action \"accept\" to peer");

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

  /* g_assert (priv->state < JS_STATE_ACTIVE); */

  GMS_DEBUG_INFO (session, "voip-engine reported a new active candidate pair [\"%s\" - \"%s\"]",
                  native_candidate_id, remote_candidate_id);

  priv->got_active_candidate_pair = TRUE;

  /* send a session accept if the session was initiated by the peer */
  if (priv->initiator == priv->peer)
    {
      try_session_accept (session);
    }
  else
    {
      GMS_DEBUG_INFO (session, "session initiated by us, so we're not going to send an accept");
      GMS_DEBUG_INFO (session, "setting stream playing");
      _gabble_media_stream_set_playing (priv->stream, 1);
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
stream_ready_cb (GabbleMediaStream *stream,
                 const GPtrArray *codecs,
                 GabbleMediaSession *session)
{
  GabbleMediaSessionPrivate *priv;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  /* send an invitation if the session was initiated by us */
  if (priv->initiator != priv->peer)
    {
      LmMessage *msg;
      LmMessageNode *session_node;

      msg = _gabble_media_session_message_new (session, "initiate", &session_node);

      _gabble_media_stream_session_node_add_description (priv->stream, session_node);

      GMS_DEBUG_INFO (session, "sending jingle session action \"initiate\" to peer");

      _gabble_connection_send_with_reply (priv->conn, msg, initiate_msg_reply_cb,
                                          G_OBJECT (session), NULL, NULL);

      lm_message_unref (msg);
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
  gchar *resource, *ret;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  self = priv->conn->self_handle;

  base_jid = gabble_handle_inspect (priv->conn->handles, TP_HANDLE_TYPE_CONTACT, handle);
  g_assert (base_jid != NULL);

  if (handle == self)
    {
      g_object_get (priv->conn, "resource", &resource, NULL);
    }
  else
    {
      resource = priv->voice_resource;
    }
  g_assert (resource != NULL);

  ret = g_strdup_printf ("%s/%s", base_jid, resource);

  if (handle == self)
    {
      g_free (resource);
    }

  return ret;
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

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  peer_jid = get_jid_for_contact (session, priv->peer);

  msg = lm_message_new_with_sub_type (
      peer_jid,
      LM_MESSAGE_TYPE_IQ,
      LM_MESSAGE_SUB_TYPE_SET);

  g_free (peer_jid);

  iq_node = lm_message_get_node (msg);
  node = lm_message_node_add_child (iq_node, "session", NULL);

  initiator_jid = get_jid_for_contact (session, priv->initiator);

  lm_message_node_set_attributes (node,
      "xmlns", "http://www.google.com/session",
      "id", priv->id,
      "type", action,
      "initiator", initiator_jid,
      NULL);

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
  LmMessage *msg;
  LmMessageNode *session_node;

  /* construct a session terminate message */
  msg = _gabble_media_session_message_new (session, "terminate", &session_node);

  GMS_DEBUG_INFO (session, "sending jingle session action \"terminate\" to peer");

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

  if (priv->initiator == priv->peer &&
      priv->state == JS_STATE_PENDING_INITIATED)
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

void
_gabble_media_session_stream_state (GabbleMediaSession *session, guint state)
{
  GabbleMediaSessionPrivate *priv;
  g_assert (GABBLE_IS_MEDIA_SESSION (session));
  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  _gabble_media_channel_stream_state (priv->channel, state);
}

#endif /* _GMS_DEBUG_LEVEL */

