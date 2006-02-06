/*
 * gabble-media-session.c - Source for GabbleMediaSession
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#include "gabble-media-session.h"
#include "gabble-media-session-signals-marshal.h"

#include "gabble-media-session-glue.h"

#include "gabble-media-stream.h"

#include "gabble-media-channel.h"

#include "telepathy-helpers.h"

G_DEFINE_TYPE(GabbleMediaSession, gabble_media_session, G_TYPE_OBJECT)

/* signal enum */
enum
{
    NEW_MEDIA_STREAM_HANDLER,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_MEDIA_CHANNEL = 1,
  PROP_OBJECT_PATH,
  PROP_SESSION_ID,
  LAST_PROPERTY
};

typedef enum {
    JS_STATE_PENDING = 0,
    JS_STATE_ACTIVE = 1,
    JS_STATE_ENDED
} JingleSessionState;

/* private structure */
typedef struct _GabbleMediaSessionPrivate GabbleMediaSessionPrivate;

struct _GabbleMediaSessionPrivate
{
  GabbleMediaChannel *channel;
  gchar *object_path;

  GabbleMediaStream *stream;

  gboolean ready;
  
  guint32 id;
  JingleSessionState state;
  
  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_SESSION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MEDIA_SESSION, GabbleMediaSessionPrivate))

static void
gabble_media_session_init (GabbleMediaSession *obj)
{
  //GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
}

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
  
  obj = G_OBJECT_CLASS (gabble_media_session_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (GABBLE_MEDIA_SESSION (obj));

  priv->ready = FALSE;

  priv->state = JS_STATE_PENDING;

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

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
      g_value_set_uint (value, priv->id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_media_session_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GabbleMediaSession *session = GABBLE_MEDIA_SESSION (object);
  GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);

  switch (property_id) {
    case PROP_MEDIA_CHANNEL:
      priv->channel = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      if (priv->object_path)
        g_free (priv->object_path);

      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_SESSION_ID:
      priv->id = g_value_get_uint (value);
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
  
  param_spec = g_param_spec_uint ("session-id", "Session ID",
                                  "A unique session identifier used "
                                  "throughout all communication.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SESSION_ID, param_spec);

  signals[NEW_MEDIA_STREAM_HANDLER] =
    g_signal_new ("new-media-stream-handler",
                  G_OBJECT_CLASS_TYPE (gabble_media_session_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_session_marshal_VOID__STRING_INT_INT,
                  G_TYPE_NONE, 3, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_UINT, G_TYPE_UINT);

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

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_media_session_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_session_parent_class)->dispose (object);
}

void
gabble_media_session_finalize (GObject *object)
{
  //GabbleMediaSession *self = GABBLE_MEDIA_SESSION (object);
  //GabbleMediaSessionPrivate *priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (self);

  /* free any data held directly by the object here */

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
  g_debug ("%s called", G_STRFUNC);
  
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
  
  g_debug ("%s called", G_STRFUNC);

  g_assert (GABBLE_IS_MEDIA_SESSION (obj));
  
  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (obj);
  
  priv->ready = TRUE;

  g_object_get (priv->stream, "object-path", &object_path, NULL);
  
  g_signal_emit (obj, signals[NEW_MEDIA_STREAM_HANDLER], 0,
                 object_path, TP_MEDIA_STREAM_TYPE_AUDIO,
                 TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL);

  g_free (object_path);

  return TRUE;
}

gboolean gabble_media_session_dispatch_action (GabbleMediaSession *session,
                                               const gchar *action,
                                               LmMessageNode *session_node)
{
  GabbleMediaSessionPrivate *priv;
  LmMessageNode *desc_node;

  g_assert (GABBLE_IS_MEDIA_SESSION (session));

  priv = GABBLE_MEDIA_SESSION_GET_PRIVATE (session);
  
  /* do the state machine dance */
  switch (priv->state) {
    case JS_STATE_PENDING:
      if (!strcmp (action, "initiate"))
        {
          desc_node = lm_message_node_get_child (session_node, "description");
          if (!desc_node)
            return FALSE;
          
          if (!gabble_media_stream_parse_remote_codecs (priv->stream, desc_node))
            return FALSE;
        }
      else if (!strcmp (action, "candidates")) /* "negotiate" in JEP */
        {
          if (!gabble_media_stream_parse_remote_candidates (priv->stream, session_node))
            {
              g_warning ("%s: gabble_media_stream_parse_remote_candidates failed", G_STRFUNC);
              return FALSE;
            }
        }
      else
        {
          g_debug ("%s: unhandled action \"%s\" in state JS_STATE_PENDING",
              G_STRFUNC, action);
          return FALSE;
        }
      
      break;
    case JS_STATE_ACTIVE:
      g_debug ("%s: unhandled action \"%s\" in state JS_STATE_ACTIVE",
          G_STRFUNC, action);
      return FALSE;
      
      break;
    case JS_STATE_ENDED:
      g_debug ("%s: unhandled action \"%s\" in state JS_STATE_ENDED",
          G_STRFUNC, action);
      return FALSE;
      
      break;
    default:
      g_debug ("%s: unknown state, ignoring action \"%s\"",
          G_STRFUNC, action);
      return FALSE;
  };

  return TRUE;
}

