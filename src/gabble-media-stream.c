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

#include "gabble-media-stream.h"
#include "gabble-media-stream-signals-marshal.h"

#include "gabble-media-stream-glue.h"

#include "gabble-media-session.h"

#include "telepathy-helpers.h"
#include "telepathy-constants.h"

G_DEFINE_TYPE(GabbleMediaStream, gabble_media_stream, G_TYPE_OBJECT)

#define TP_CODEC_SET_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      DBUS_TYPE_G_STRING_STRING_HASHTABLE, \
      G_TYPE_INVALID))

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

typedef struct _JingleCandidate JingleCandidate;
typedef struct _JingleCodec JingleCodec;

struct _JingleCandidate {
    gchar *name;
    gchar *address;
    guint16 port;
    gchar *username;
    gchar *password;
    gfloat preference;
    gchar *protocol;
    gchar *type;
    guchar network;
    guchar generation;
};

struct _JingleCodec {
    guchar id;
    gchar *name;
};

JingleCandidate *jingle_candidate_new (const gchar *name,
                                       const gchar *address,
                                       guint16 port,
                                       const gchar *username,
                                       const gchar *password,
                                       gfloat preference,
                                       const gchar *protocol,
                                       const gchar *type,
                                       guchar network,
                                       guchar generation);
void jingle_candidate_free (JingleCandidate *candidate);

JingleCodec *jingle_codec_new (guchar id, const gchar *name);
void jingle_codec_free (JingleCodec *codec);

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
  
  obj = G_OBJECT_CLASS (gabble_media_stream_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (GABBLE_MEDIA_STREAM (obj));

  priv->ready = FALSE;

  priv->remote_codecs = g_ptr_array_sized_new (12);
  priv->remote_candidates = g_ptr_array_sized_new (4);

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
                  G_TYPE_NONE, 2, G_TYPE_STRING, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID)))));

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
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID)))), G_TYPE_INVALID)))));

  signals[SET_REMOTE_CODECS] =
    g_signal_new ("set-remote-codecs",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_stream_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, DBUS_TYPE_G_STRING_STRING_HASHTABLE, G_TYPE_INVALID)))));
  
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
gboolean gabble_media_stream_ready (GabbleMediaStream *obj, GError **error)
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
  int i;
  
  g_assert (GABBLE_IS_MEDIA_STREAM (stream));
  
  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (!priv->ready)
    return;

  if (priv->remote_codecs->len == 0)
    return;

  g_signal_emit (stream, signals[SET_REMOTE_CODECS], 0,
                 priv->remote_codecs);

  for (i = 0; i < priv->remote_codecs->len; i++)
    {
      g_value_array_free (g_ptr_array_index (priv->remote_codecs, i));
    }

  g_ptr_array_remove_range (priv->remote_codecs, 0, priv->remote_codecs->len);
}

static void
push_remote_candidates (GabbleMediaStream *stream)
{
  GabbleMediaStreamPrivate *priv;
  
  g_assert (GABBLE_IS_MEDIA_STREAM (stream));
  
  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  if (!priv->ready)
    return;


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
      
      g_value_init (&codec, TP_CODEC_SET_TYPE);
      g_value_set_static_boxed (&codec,
          dbus_g_type_specialized_construct (TP_CODEC_SET_TYPE));
      
      dbus_g_type_struct_set (&codec,
          0, id,
          1, name,
          2, TP_MEDIA_STREAM_TYPE_AUDIO, /* FIXME: this enum happens to match
                                            FarsightMediaType, is this the intention? */
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

gboolean
gabble_media_stream_parse_remote_candidates (GabbleMediaStream *stream, LmMessageNode *session_node)
{
  GabbleMediaStreamPrivate *priv;
  gint prev_len;
  LmMessageNode *node;
  const gchar *str;
  
  g_assert (GABBLE_IS_MEDIA_STREAM (stream));
  
  priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (stream);

  prev_len = priv->remote_candidates->len;

  for (node = session_node->children; node; node = node->next)
    {
      const gchar *c_name, *c_addr, *c_user, *c_pass, *c_proto, *c_type;
      guint16 c_port;
      gfloat c_pref;
      guchar c_net, c_gen;
      JingleCandidate *candidate;

      c_name = lm_message_node_get_attribute (node, "name");
      if (!c_name)
        return FALSE;
      
      c_addr = lm_message_node_get_attribute (node, "address");
      if (!c_addr)
        return FALSE;
      
      str = lm_message_node_get_attribute (node, "port");
      if (!str)
        return FALSE;
      c_port = atoi (str);

      c_user = lm_message_node_get_attribute (node, "username");
      if (!c_user)
        return FALSE;
      
      c_pass = lm_message_node_get_attribute (node, "password");
      if (!c_pass)
        return FALSE;

      str = lm_message_node_get_attribute (node, "preference");
      if (!str)
        return FALSE;
      c_pref = (gfloat) g_ascii_strtod (str, NULL);
      
      c_proto = lm_message_node_get_attribute (node, "protocol");
      if (!c_proto)
        return FALSE;

      c_type = lm_message_node_get_attribute (node, "type");
      if (!c_type)
        return FALSE;

      str = lm_message_node_get_attribute (node, "network");
      if (!str)
        return FALSE;
      c_net = atoi (str);
      
      str = lm_message_node_get_attribute (node, "generation");
      if (!str)
        return FALSE;
      c_gen = atoi (str);

      candidate = jingle_candidate_new (c_name, c_addr, c_port,
                                        c_user, c_pass, c_pref,
                                        c_proto, c_type, c_net,
                                        c_gen);

      g_ptr_array_add (priv->remote_candidates, candidate);
    }
  
  g_debug ("%s: parsed %d new remote candidate(s), "
           "%d remote candidate(s) in total now",
           G_STRFUNC,
           priv->remote_candidates->len - prev_len,
           priv->remote_candidates->len);

  push_remote_candidates (stream);
  
  return TRUE;
}

/*
 * JingleCandidate
 */

JingleCandidate *jingle_candidate_new (const gchar *name,
                                       const gchar *address,
                                       guint16 port,
                                       const gchar *username,
                                       const gchar *password,
                                       gfloat preference,
                                       const gchar *protocol,
                                       const gchar *type,
                                       guchar network,
                                       guchar generation)
{
  JingleCandidate *candidate = g_new (JingleCandidate, 1);

  candidate->name = g_strdup (name);
  candidate->address = g_strdup (address);
  candidate->port = port;
  candidate->username = g_strdup (username);
  candidate->password = g_strdup (password);
  candidate->preference = preference;
  candidate->protocol = g_strdup (protocol);
  candidate->type = g_strdup (type);
  candidate->network = network;
  candidate->generation = generation;

  return candidate;
}

void jingle_candidate_free (JingleCandidate *candidate)
{
  g_free (candidate->name);
  g_free (candidate->address);
  g_free (candidate->username);
  g_free (candidate->password);
  g_free (candidate->protocol);
  g_free (candidate->type);
  
  g_free (candidate);
}


/*
 * JingleCodec
 */

JingleCodec *
jingle_codec_new (guchar id, const gchar *name)
{
  JingleCodec *codec = g_new (JingleCodec, 1);

  codec->id = id;
  codec->name = g_strdup (name);

  return codec;
}

void
jingle_codec_free (JingleCodec *codec)
{
  g_free (codec->name);

  g_free (codec);
}

