/*
 * gabble-call-stream.h - Header for GabbleCallStream
 * Copyright (C) 2009 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#ifndef __GABBLE_CALL_STREAM_H__
#define __GABBLE_CALL_STREAM_H__

#include <glib-object.h>

#include <telepathy-yell/base-media-call-stream.h>
#include "types.h"

G_BEGIN_DECLS

typedef struct _GabbleCallStream GabbleCallStream;
typedef struct _GabbleCallStreamPrivate GabbleCallStreamPrivate;
typedef struct _GabbleCallStreamClass GabbleCallStreamClass;

struct _GabbleCallStreamClass {
    TpyBaseMediaCallStreamClass parent_class;
};

struct _GabbleCallStream {
    TpyBaseMediaCallStream parent;

    GabbleCallStreamPrivate *priv;
};

GType gabble_call_stream_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_CALL_STREAM \
  (gabble_call_stream_get_type ())
#define GABBLE_CALL_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_CALL_STREAM, GabbleCallStream))
#define GABBLE_CALL_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_CALL_STREAM, \
    GabbleCallStreamClass))
#define GABBLE_IS_CALL_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CALL_STREAM))
#define GABBLE_IS_CALL_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CALL_STREAM))
#define GABBLE_CALL_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CALL_STREAM, \
    GabbleCallStreamClass))



void gabble_call_stream_set_sending (TpyBaseCallStream *stream, gboolean sending, GError **error);
guint gabble_call_stream_get_local_sending_state (GabbleCallStream *self);
GabbleJingleContent *gabble_call_stream_get_jingle_content (
    GabbleCallStream *stream);

G_END_DECLS

#endif /* #ifndef __GABBLE_CALL_STREAM_H__*/
