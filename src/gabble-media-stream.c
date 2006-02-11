/*
 * gabble-media-stream.c - Source for GabbleMediaStream
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

#include "gabble-media-stream.h"
#include "gabble-media-stream-signals-marshal.h"

#include "gabble-media-stream-glue.h"

#include "gabble-media-session.h"

#include "telepathy-helpers.h"
#include "telepathy-constants.h"

G_DEFINE_TYPE(GabbleMediaStream, gabble_media_stream, G_TYPE_OBJECT)

#define TP_TYPE_TRANSPORT_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_STRING, \
      G_TYPE_DOUBLE, \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_STRING, \
      G_TYPE_INVALID))
#define TP_TYPE_TRANSPORT_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_TRANSPORT_STRUCT))
#define TP_TYPE_CANDIDATE_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_STRING, \
      TP_TYPE_TRANSPORT_LIST, \
      G_TYPE_INVALID))
#define TP_TYPE_CANDIDATE_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_CANDIDATE_STRUCT))

#define TP_TYPE_CODEC_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      DBUS_TYPE_G_STRING_STRING_HASHTABLE, \
      G_TYPE_INVALID))
#define TP_TYPE_CODEC_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_CODEC_STRUCT))

/* signal enum */
enum
{
    ADD_REMOTE_CANDIDATE,
    REMOVE_REMOTE_CANDIDATE,
    SET_ACTIVE_CANDIDATE_PAIR,
    SET_REMOTE_CANDIDATE_LIST,
    SET_REMOTE_CODECS,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_MEDIA_SESSION = 1,
  PROP_OBJECT_PATH,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleMediaStreamPrivate GabbleMediaStreamPrivate;

struct _GabbleMediaStreamPrivate
{
  GabbleMediaSession *session;
  gchar *object_path;

  gboolean ready;
  
  GPtrArray *remote_codecs;
  GPtrArray *remote_candidates;
  
  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_STREAM_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GABBLE_TYPE_MEDIA_STREAM, GabbleMediaStreamPrivate))

static void
gabble_media_stream_init (GabbleMediaStream *obj)
{
  //GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
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

  /* initialize state */
  priv->ready = FALSE;

  priv->remote_codecs = g_ptr_array_sized_new (12);
  priv->remote_candidates = g_ptr_array_sized_new (2);

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
    case PROP_MEDIA_SESSION:
      g_value_set_object (value, priv->session);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
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
    case PROP_MEDIA_SESSION:
      priv->session = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      if (priv->object_path)
        g_free (priv->object_path);

      priv->object_path = g_value_dup_string (value);
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

  signals[ADD_REMOTE_CANDIDATE] =
    g_signal_new ("add-remote-candidate",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_stream_marshal_VOID__STRING_BOXED,
                  G_TYPE_NONE, 2, G_TYPE_STRING, TP_TYPE_TRANSPORT_LIST);

  signals[REMOVE_REMOTE_CANDIDATE] =
    g_signal_new ("remove-remote-candidate",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_stream_marshal_VOID__STRING,
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
                  gabble_media_stream_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, TP_TYPE_CANDIDATE_LIST);

  signals[SET_REMOTE_CODECS] =
    g_signal_new ("set-remote-codecs",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_stream_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, TP_TYPE_CODEC_LIST);
  
  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_media_stream_class), &dbus_glib_gabble_media_stream_object_info);
}

void
gabble_media_stream_dispose (GObject *object)
{
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (object);
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_media_stream_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_media_stream_parent_class)->dispose (object);
}

void
gabble_media_stream_finalize (GObject *object)
{
  //GabbleMediaStream *self = GABBLE_MEDIA_STREAM (object);
  //GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gabble_media_stream_parent_class)->finalize (object);
}



/**
 * gabble_media_stream_codec_choice
 *
 * Implements DBus method CodecChoice
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_stream_codec_choice (GabbleMediaStream *obj, guint codec_id, GError **error)
{
  g_debug ("%s called", G_STRFUNC);
  
  return TRUE;
}


/**
 * gabble_media_stream_error
 *
 * Implements DBus method Error
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_stream_error (GabbleMediaStream *obj, guint errno, const gchar * message, GError **error)
{
  g_debug ("%s called", G_STRFUNC);
  
  return TRUE;
}


/**
 * gabble_media_stream_native_candidates_prepared
 *
 * Implements DBus method NativeCandidatesPrepared
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_stream_native_candidates_prepared (GabbleMediaStream *obj, GError **error)
{
  g_debug ("%s called", G_STRFUNC);
  
  return TRUE;
}


/**
 * gabble_media_stream_new_active_candidate_pair
 *
 * Implements DBus method NewActiveCandidatePair
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_stream_new_active_candidate_pair (GabbleMediaStream *obj, const gchar * native_candidate_id, const gchar * remote_candidate_id, GError **error)
{
  g_debug ("%s called", G_STRFUNC);
  
  return TRUE;
}

/**
 * gabble_media_stream_new_native_candidate
 *
 * Implements DBus method NewNativeCandidate
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_stream_new_native_candidate (GabbleMediaStream *obj, const gchar * candidate_id, const GPtrArray * transports, GError **error)
{
  g_debug ("%s called", G_STRFUNC);
  
  return TRUE;
}

static void push_remote_codecs (GabbleMediaStream *stream);
static void push_remote_candidates (GabbleMediaStream *stream);

/**
 * gabble_media_stream_ready
 *
 * Implements DBus method Ready
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_stream_ready (GabbleMediaStream *obj, const GPtrArray * codecs, GError **error)
{
  GabbleMediaStreamPrivate *priv;
  
  g_debug ("%s called", G_STRFUNC);
  
  g_assert (GABBLE_IS_MEDIA_STREAM (obj));
  
  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (obj);

  priv->ready = TRUE;

  push_remote_codecs (obj);
  push_remote_candidates (obj);

  return TRUE;
}


/**
 * gabble_media_stream_supported_codecs
 *
 * Implements DBus method SupportedCodecs
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean gabble_media_stream_supported_codecs (GabbleMediaStream *obj, const GPtrArray * codecs, GError **error)
{
  g_debug ("%s called", G_STRFUNC);
  
  return TRUE;
}

static void
push_remote_codecs (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;
  /*int i;*/
  
  g_assert (GABBLE_IS_MEDIA_STREAM (stream));
  
  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (!priv->ready)
    return;

  if (priv->remote_codecs->len == 0)
    return;

  g_debug ("%s: emitting MediaStreamHandler::SetRemoteCodecs signal",
      G_STRFUNC);

  g_signal_emit (stream, signals[SET_REMOTE_CODECS], 0,
                 priv->remote_codecs);

  /* FIXME: free */

  g_ptr_array_remove_range (priv->remote_codecs, 0,
      priv->remote_codecs->len);
}

static void
push_remote_candidates (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;
  
  g_assert (GABBLE_IS_MEDIA_STREAM (stream));
  
  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (!priv->ready)
    return;

  if (priv->remote_candidates->len == 0)
    return;
  
  g_debug ("%s: emitting MediaStreamHandler::SetRemoteCandidateList signal",
      G_STRFUNC);

  g_signal_emit (stream, signals[SET_REMOTE_CANDIDATE_LIST], 0,
                 priv->remote_candidates);

  /* FIXME: free */
  
  g_ptr_array_remove_range (priv->remote_candidates, 0,
      priv->remote_candidates->len);
}

gboolean
gabble_media_stream_parse_remote_codecs (GabbleMediaStream *stream, LmMessageNode *desc_node)
{
  GabbleMediaStreamPrivate *priv;
  LmMessageNode *node;
  const gchar *str;
  
  g_assert (GABBLE_IS_MEDIA_STREAM (stream));
  
  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  g_assert (priv->remote_codecs->len == 0);
  
  for (node = desc_node->children; node; node = node->next)
    {
      guchar id;
      const gchar *name;
      GValue codec = { 0 };
      
      /* id of codec */
      str = lm_message_node_get_attribute (node, "id");
      if (!str)
        return FALSE;

      id = atoi(str);

      /* codec name */
      name = lm_message_node_get_attribute (node, "name");
      if (!name)
        return FALSE;
      
      g_value_init (&codec, TP_TYPE_CODEC_STRUCT);
      g_value_set_static_boxed (&codec,
          dbus_g_type_specialized_construct (TP_TYPE_CODEC_STRUCT));
      
      dbus_g_type_struct_set (&codec,
          0, id,
          1, name,
          2, TP_CODEC_MEDIA_TYPE_AUDIO,
          3, 0,                          /* FIXME: valid default clock rate? */
          4, 1,                          /* number of supported channels */
          5, g_hash_table_new (g_str_hash, g_str_equal),
          G_MAXUINT);
      
      g_ptr_array_add (priv->remote_codecs, g_value_get_boxed (&codec));
    }

  g_debug ("%s: parsed %d remote codecs", G_STRFUNC, priv->remote_codecs->len);

  push_remote_codecs (stream);

  return TRUE;
}

#if 0
static GPtrArray *
get_candidate_transports (GabbleMediaStreamPrivate *priv, const gchar *name)
{
  GValueArray *candidate;
  GValue *val;
  int i;
  GPtrArray *arr;

  for (i = 0; i < priv->remote_candidates->len; i++)
    {
      const gchar *str;
      
      candidate = g_ptr_array_index (priv->remote_candidates, i);

      val = g_value_array_get_nth (candidate, 0);
      str = g_value_get_string (val);
      if (!strcmp (str, name))
        {
          val = g_value_array_get_nth (candidate, 1);
          return g_value_get_pointer (val);
        }
    }

  candidate = g_value_array_new (2);
  
  g_value_array_append (candidate, NULL);
  val = g_value_array_get_nth (candidate, 0);
  g_value_init (val, G_TYPE_STRING);
  g_value_set_string (val, name);

  g_value_array_append (candidate, NULL);
  val = g_value_array_get_nth (candidate, 1);
  g_value_init (val, G_TYPE_POINTER);
  arr = g_ptr_array_sized_new (4);
  g_value_set_pointer (val, arr);

  g_ptr_array_add (priv->remote_candidates, candidate);

  return arr;
}
#endif

gboolean
gabble_media_stream_parse_remote_candidates (GabbleMediaStream *stream, LmMessageNode *session_node)
{
  GabbleMediaStreamPrivate *priv;
  LmMessageNode *node;
  const gchar *str;
  
  g_assert (GABBLE_IS_MEDIA_STREAM (stream));
  
  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  for (node = session_node->children; node; node = node->next)
    {
      const gchar /**name, */*addr;
      guint16 port;
      TpMediaStreamProto proto;
      gdouble pref;
      TpMediaStreamTransportType type;
      const gchar *user, *pass;
      guchar net, gen;

      GValue candidate = { 0 };
      GPtrArray *transports;
      GValue transport = { 0 };


      /*
       * Candidate
       */

      /* id/name: assuming "username" here for now */
      
      
      /*
       * Transport
       */
      
      /* ip address */
      addr = lm_message_node_get_attribute (node, "address");
      if (!addr)
        return FALSE;
      
      /* port */
      str = lm_message_node_get_attribute (node, "port");
      if (!str)
        return FALSE;
      port = atoi (str);
      
      /* protocol */
      str = lm_message_node_get_attribute (node, "protocol");
      if (!str)
        return FALSE;
      
      if (!strcmp (str, "udp"))
        proto = TP_MEDIA_STREAM_PROTO_UDP;
      else if (!strcmp (str, "tcp"))
        proto = TP_MEDIA_STREAM_PROTO_TCP;
      else
        return FALSE;

      /* protocol subtype: only "rtp" is supported here for now */
      str = lm_message_node_get_attribute (node, "name");
      if (!str)
        return FALSE;
      if (strcmp (str, "rtp"))
        return FALSE;
      
      /* protocol profile: hardcoded to "AVP" for now */

      /* preference */
      str = lm_message_node_get_attribute (node, "preference");
      if (!str)
        return FALSE;
      pref = g_ascii_strtod (str, NULL);

      /* type */
      str = lm_message_node_get_attribute (node, "type");
      if (!str)
        return FALSE;
      
      if (!strcmp (str, "local"))
        type = TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL;
      else if (!strcmp (str, "stun"))
        type = TP_MEDIA_STREAM_TRANSPORT_TYPE_DERIVED;
      else if (!strcmp (str, "relay"))
        type = TP_MEDIA_STREAM_TRANSPORT_TYPE_RELAY;
      else
        return FALSE;
      
      /* username */
      user = lm_message_node_get_attribute (node, "username");
      if (!user)
        return FALSE;
      
      /* password */
      pass = lm_message_node_get_attribute (node, "password");
      if (!pass)
        return FALSE;

      /* unknown */
      str = lm_message_node_get_attribute (node, "network");
      if (!str)
        return FALSE;
      net = atoi (str);
      
      /* unknown */
      str = lm_message_node_get_attribute (node, "generation");
      if (!str)
        return FALSE;
      gen = atoi (str);


      g_value_init (&transport, TP_TYPE_TRANSPORT_STRUCT);
      g_value_set_static_boxed (&transport,
          dbus_g_type_specialized_construct (TP_TYPE_TRANSPORT_STRUCT));

      dbus_g_type_struct_set (&transport,
          0, 0,         /* component number */
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
      g_value_set_static_boxed (&candidate,
          dbus_g_type_specialized_construct (TP_TYPE_CANDIDATE_STRUCT));

      dbus_g_type_struct_set (&candidate,
          0, user,
          1, transports,
          G_MAXUINT);

      g_ptr_array_add (priv->remote_candidates, g_value_get_boxed (&candidate));

      g_debug ("%s: added new candidate %s, "
               "%d candidate(s) in total now",
               G_STRFUNC,
               user,
               priv->remote_candidates->len);
    }

  push_remote_candidates (stream);
  
  return TRUE;
}

