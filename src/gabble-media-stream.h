/*
 * gabble-media-stream.h - Header for GabbleMediaStream
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#ifndef __GABBLE_MEDIA_STREAM_H__
#define __GABBLE_MEDIA_STREAM_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include "gabble-types.h"
#include <telepathy-glib/enums.h>

G_BEGIN_DECLS

typedef enum
{
  STREAM_SIG_STATE_NEW,
  STREAM_SIG_STATE_SENT,
  STREAM_SIG_STATE_ACKNOWLEDGED,
  STREAM_SIG_STATE_REMOVING
} StreamSignallingState;

typedef guint32 CombinedStreamDirection;

typedef struct _GabbleMediaStream GabbleMediaStream;
typedef struct _GabbleMediaStreamClass GabbleMediaStreamClass;

struct _GabbleMediaStreamClass {
    GObjectClass parent_class;
};

struct _GabbleMediaStream {
    GObject parent;

    gchar *name;

    JingleInitiator initiator;
    TpMediaStreamState connection_state;
    StreamSignallingState signalling_state;

    CombinedStreamDirection combined_direction;
    gboolean got_local_codecs;
    gboolean playing;

    gpointer priv;
};

GType gabble_media_stream_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_MEDIA_STREAM \
  (gabble_media_stream_get_type ())
#define GABBLE_MEDIA_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MEDIA_STREAM, \
                              GabbleMediaStream))
#define GABBLE_MEDIA_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MEDIA_STREAM, \
                           GabbleMediaStreamClass))
#define GABBLE_IS_MEDIA_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MEDIA_STREAM))
#define GABBLE_IS_MEDIA_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MEDIA_STREAM))
#define GABBLE_MEDIA_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MEDIA_STREAM, \
                              GabbleMediaStreamClass))

#define GABBLE_TP_TYPE_TRANSPORT_STRUCT (dbus_g_type_get_struct ("GValueArray", \
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
#define GABBLE_TP_TYPE_TRANSPORT_LIST (dbus_g_type_get_collection ("GPtrArray", \
      GABBLE_TP_TYPE_TRANSPORT_STRUCT))
#define GABBLE_TP_TYPE_CANDIDATE_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_STRING, \
      GABBLE_TP_TYPE_TRANSPORT_LIST, \
      G_TYPE_INVALID))
#define GABBLE_TP_TYPE_CANDIDATE_LIST (dbus_g_type_get_collection ("GPtrArray", \
      GABBLE_TP_TYPE_CANDIDATE_STRUCT))

#define GABBLE_TP_TYPE_CODEC_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      DBUS_TYPE_G_STRING_STRING_HASHTABLE, \
      G_TYPE_INVALID))
#define GABBLE_TP_TYPE_CODEC_LIST (dbus_g_type_get_collection ("GPtrArray", \
      GABBLE_TP_TYPE_CODEC_STRUCT))

#define COMBINED_DIRECTION_GET_DIRECTION(d) \
    ((TpMediaStreamDirection) ((d) & TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL))
#define COMBINED_DIRECTION_GET_PENDING_SEND(d) \
    ((TpMediaStreamPendingSend) ((d) >> 2))
#define MAKE_COMBINED_DIRECTION(d, p) \
    ((CombinedStreamDirection) ((d) | ((p) << 2)))

gboolean gabble_media_stream_error (GabbleMediaStream *self, guint errno,
    const gchar *message, GError **error);

void _gabble_media_stream_close (GabbleMediaStream *close);
gboolean _gabble_media_stream_post_remote_codecs (GabbleMediaStream *stream,
    LmMessage *message, LmMessageNode *desc_node, GError **error);
gboolean _gabble_media_stream_post_remote_candidates (
    GabbleMediaStream *stream, LmMessage *message,
    LmMessageNode *transport_node, GError **error);
LmMessageNode *_gabble_media_stream_add_content_node (
    GabbleMediaStream *stream, LmMessageNode *session_node);
void _gabble_media_stream_content_node_add_description (
    GabbleMediaStream *stream, LmMessageNode *content_node);
LmMessageNode *_gabble_media_stream_content_node_add_transport (
    GabbleMediaStream *stream, LmMessageNode *content_node);
void _gabble_media_stream_update_sending (GabbleMediaStream *stream,
    gboolean start_sending);

G_END_DECLS

#endif /* #ifndef __GABBLE_MEDIA_STREAM_H__*/
