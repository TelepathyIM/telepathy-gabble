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

#include "types.h"
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
typedef struct _GabbleMediaStreamPrivate GabbleMediaStreamPrivate;

struct _GabbleMediaStreamClass {
    GObjectClass parent_class;
};

struct _GabbleMediaStream {
    GObject parent;

    gchar *name;

    TpMediaStreamState connection_state;

    CombinedStreamDirection combined_direction;
    gboolean playing;

    GabbleMediaStreamPrivate *priv;
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

#define COMBINED_DIRECTION_GET_DIRECTION(d) \
    ((TpMediaStreamDirection) ((d) & TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL))
#define COMBINED_DIRECTION_GET_PENDING_SEND(d) \
    ((TpMediaStreamPendingSend) ((d) >> 2))
#define MAKE_COMBINED_DIRECTION(d, p) \
    ((CombinedStreamDirection) ((d) | ((p) << 2)))

gboolean gabble_media_stream_error (GabbleMediaStream *self, guint errno,
    const gchar *message, GError **error);

void gabble_media_stream_close (GabbleMediaStream *close);
void gabble_media_stream_hold (GabbleMediaStream *stream, gboolean hold);
gboolean gabble_media_stream_change_direction (GabbleMediaStream *stream,
    guint requested_dir, GError **error);
void gabble_media_stream_accept_pending_local_send (GabbleMediaStream *stream);

GabbleMediaStream *gabble_media_stream_new (const gchar *object_path,
    GabbleJingleContent *content,
    const gchar *name,
    guint id,
    const gchar *nat_traversal,
    const GPtrArray *relay_info,
    gboolean local_hold);
TpMediaStreamType gabble_media_stream_get_media_type (GabbleMediaStream *self);

GabbleJingleMediaRtp *gabble_media_stream_get_content (GabbleMediaStream *self);

G_END_DECLS

#endif /* #ifndef __GABBLE_MEDIA_STREAM_H__*/
