/*
 * gabble-media-stream.h - Header for GabbleMediaStream
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

#ifndef __GABBLE_MEDIA_STREAM_H__
#define __GABBLE_MEDIA_STREAM_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

G_BEGIN_DECLS

typedef struct _GabbleMediaStream GabbleMediaStream;
typedef struct _GabbleMediaStreamClass GabbleMediaStreamClass;

struct _GabbleMediaStreamClass {
    GObjectClass parent_class;
};

struct _GabbleMediaStream {
    GObject parent;
};

GType gabble_media_stream_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_MEDIA_STREAM \
  (gabble_media_stream_get_type())
#define GABBLE_MEDIA_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MEDIA_STREAM, GabbleMediaStream))
#define GABBLE_MEDIA_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MEDIA_STREAM, GabbleMediaStreamClass))
#define GABBLE_IS_MEDIA_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MEDIA_STREAM))
#define GABBLE_IS_MEDIA_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MEDIA_STREAM))
#define GABBLE_MEDIA_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MEDIA_STREAM, GabbleMediaStreamClass))


gboolean gabble_media_stream_codec_choice (GabbleMediaStream *obj, guint codec_id, GError **error);
gboolean gabble_media_stream_error (GabbleMediaStream *obj, guint errno, const gchar * message, GError **error);
gboolean gabble_media_stream_native_candidates_prepared (GabbleMediaStream *obj, GError **error);
gboolean gabble_media_stream_new_active_candidate_pair (GabbleMediaStream *obj, const gchar * native_candidate_id, const gchar * remote_candidate_id, GError **error);
gboolean gabble_media_stream_new_native_candidate (GabbleMediaStream *obj, const gchar * candidate_id, const GPtrArray * transports, GError **error);
gboolean gabble_media_stream_ready (GabbleMediaStream *obj, GError **error);
gboolean gabble_media_stream_supported_codecs (GabbleMediaStream *obj, const GPtrArray * codecs, GError **error);

gboolean gabble_media_stream_parse_remote_codecs (GabbleMediaStream *stream, LmMessageNode *desc_node);
gboolean gabble_media_stream_parse_remote_candidates (GabbleMediaStream *stream, LmMessageNode *session_node);

G_END_DECLS

#endif /* #ifndef __GABBLE_MEDIA_STREAM_H__*/
