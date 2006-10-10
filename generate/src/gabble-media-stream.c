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

G_DEFINE_TYPE(GabbleMediaStream, gabble_media_stream, G_TYPE_OBJECT)

/* signal enum */
enum
{
    ADD_REMOTE_CANDIDATE,
    CLOSE,
    REMOVE_REMOTE_CANDIDATE,
    SET_ACTIVE_CANDIDATE_PAIR,
    SET_REMOTE_CANDIDATE_LIST,
    SET_REMOTE_CODECS,
    SET_STREAM_PLAYING,
    SET_STREAM_SENDING,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _GabbleMediaStreamPrivate GabbleMediaStreamPrivate;

struct _GabbleMediaStreamPrivate
{
  gboolean dispose_has_run;
};

#define GABBLE_MEDIA_STREAM_GET_PRIVATE(obj) \
    ((GabbleMediaStreamPrivate *)obj->priv)

static void
gabble_media_stream_init (GabbleMediaStream *self)
{
  GabbleMediaStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_MEDIA_STREAM, GabbleMediaStreamPrivate);

  self->priv = priv;

  /* allocate any data required by the object here */
}

static void gabble_media_stream_dispose (GObject *object);
static void gabble_media_stream_finalize (GObject *object);

static void
gabble_media_stream_class_init (GabbleMediaStreamClass *gabble_media_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_media_stream_class);

  g_type_class_add_private (gabble_media_stream_class, sizeof (GabbleMediaStreamPrivate));

  object_class->dispose = gabble_media_stream_dispose;
  object_class->finalize = gabble_media_stream_finalize;

  signals[ADD_REMOTE_CANDIDATE] =
    g_signal_new ("add-remote-candidate",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  gabble_media_stream_marshal_VOID__STRING_BOXED,
                  G_TYPE_NONE, 2, G_TYPE_STRING, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID)))));

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
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID)))), G_TYPE_INVALID)))));

  signals[SET_REMOTE_CODECS] =
    g_signal_new ("set-remote-codecs",
                  G_OBJECT_CLASS_TYPE (gabble_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, (dbus_g_type_get_collection ("GPtrArray", (dbus_g_type_get_struct ("GValueArray", G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, DBUS_TYPE_G_STRING_STRING_HASHTABLE, G_TYPE_INVALID)))));

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
  GabbleMediaStream *self = GABBLE_MEDIA_STREAM (object);
  GabbleMediaStreamPrivate *priv = GABBLE_MEDIA_STREAM_GET_PRIVATE (self);

  /* free any data held directly by the object here */

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
  return TRUE;
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
                                  guint state,
                                  GError **error)
{
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
  return TRUE;
}

