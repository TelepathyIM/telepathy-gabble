/*
 * gabble-media-stream-handler.h - Header for GabbleMediaStreamHandler
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

#ifndef __GABBLE_MEDIA_STREAM_HANDLER_H__
#define __GABBLE_MEDIA_STREAM_HANDLER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GabbleMediaStreamHandler GabbleMediaStreamHandler;
typedef struct _GabbleMediaStreamHandlerClass GabbleMediaStreamHandlerClass;

struct _GabbleMediaStreamHandlerClass {
    GObjectClass parent_class;
};

struct _GabbleMediaStreamHandler {
    GObject parent;
};

GType gabble_media_stream_handler_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_MEDIA_STREAM_HANDLER \
  (gabble_media_stream_handler_get_type())
#define GABBLE_MEDIA_STREAM_HANDLER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MEDIA_STREAM_HANDLER, GabbleMediaStreamHandler))
#define GABBLE_MEDIA_STREAM_HANDLER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MEDIA_STREAM_HANDLER, GabbleMediaStreamHandlerClass))
#define GABBLE_IS_MEDIA_STREAM_HANDLER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MEDIA_STREAM_HANDLER))
#define GABBLE_IS_MEDIA_STREAM_HANDLER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MEDIA_STREAM_HANDLER))
#define GABBLE_MEDIA_STREAM_HANDLER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MEDIA_STREAM_HANDLER, GabbleMediaStreamHandlerClass))


gboolean gabble_media_stream_handler_codec_choice (GabbleMediaStreamHandler *obj, guint codec_id, GError **error);
gboolean gabble_media_stream_handler_error (GabbleMediaStreamHandler *obj, guint errno, const gchar * message, GError **error);
gboolean gabble_media_stream_handler_native_candidates_prepared (GabbleMediaStreamHandler *obj, GError **error);
gboolean gabble_media_stream_handler_new_active_candidate_pair (GabbleMediaStreamHandler *obj, const gchar * native_candidate_id, const gchar * remote_candidate_id, GError **error);
gboolean gabble_media_stream_handler_new_native_candidate (GabbleMediaStreamHandler *obj, const gchar * candidate_id, const GPtrArray * transports, GError **error);
gboolean gabble_media_stream_handler_ready (GabbleMediaStreamHandler *obj, GError **error);
gboolean gabble_media_stream_handler_supported_codecs (GabbleMediaStreamHandler *obj, const GPtrArray * codecs, GError **error);


G_END_DECLS

#endif /* #ifndef __GABBLE_MEDIA_STREAM_HANDLER_H__*/
