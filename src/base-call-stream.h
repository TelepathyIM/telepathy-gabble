/*
 * base-call-stream.h - Header for GabbleBaseCallStream
 * Copyright © 2009–2010 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 * @author Will Thompson <will.thompson@collabora.co.uk>
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

#ifndef GABBLE_BASE_CALL_STREAM_H
#define GABBLE_BASE_CALL_STREAM_H

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>
#include <extensions/extensions.h>

#include "types.h"

G_BEGIN_DECLS

typedef struct _GabbleBaseCallStream GabbleBaseCallStream;
typedef struct _GabbleBaseCallStreamPrivate GabbleBaseCallStreamPrivate;
typedef struct _GabbleBaseCallStreamClass GabbleBaseCallStreamClass;

struct _GabbleBaseCallStreamClass {
    GObjectClass parent_class;

    TpDBusPropertiesMixinClass dbus_props_class;

    const gchar * const *extra_interfaces;
};

struct _GabbleBaseCallStream {
    GObject parent;

    GabbleBaseCallStreamPrivate *priv;
};

GType gabble_base_call_stream_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_BASE_CALL_STREAM \
  (gabble_base_call_stream_get_type ())
#define GABBLE_BASE_CALL_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_BASE_CALL_STREAM, GabbleBaseCallStream))
#define GABBLE_BASE_CALL_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_BASE_CALL_STREAM, \
    GabbleBaseCallStreamClass))
#define GABBLE_IS_BASE_CALL_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_BASE_CALL_STREAM))
#define GABBLE_IS_BASE_CALL_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_BASE_CALL_STREAM))
#define GABBLE_BASE_CALL_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_BASE_CALL_STREAM, \
    GabbleBaseCallStreamClass))

GabbleConnection *gabble_base_call_stream_get_connection (
    GabbleBaseCallStream *self);
const gchar *gabble_base_call_stream_get_object_path (
    GabbleBaseCallStream *self);

GabbleSendingState gabble_base_call_stream_get_sender_state (
    GabbleBaseCallStream *self,
    TpHandle sender,
    gboolean *existed);

gboolean gabble_base_call_stream_update_local_sending_state (
  GabbleBaseCallStream *self,
  GabbleSendingState state);

GabbleSendingState
gabble_base_call_stream_get_local_sending_state (
  GabbleBaseCallStream *self);

gboolean
gabble_base_call_stream_remote_member_update_state (GabbleBaseCallStream *self,
    TpHandle contact,
    GabbleSendingState state);


gboolean gabble_base_call_stream_update_senders (
    GabbleBaseCallStream *self,
    TpHandle contact,
    GabbleSendingState state,
    ...) G_GNUC_NULL_TERMINATED;

G_END_DECLS

#endif
