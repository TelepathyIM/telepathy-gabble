/*
 * tube-stream.h - Header for GabbleTubeStream
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __GABBLE_TUBE_STREAM_H__
#define __GABBLE_TUBE_STREAM_H__

#include <glib-object.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/interfaces.h>

#include "connection.h"
#include "extensions/extensions.h"
#include "muc-channel.h"

G_BEGIN_DECLS

typedef struct _GabbleTubeStream GabbleTubeStream;
typedef struct _GabbleTubeStreamPrivate GabbleTubeStreamPrivate;
typedef struct _GabbleTubeStreamClass GabbleTubeStreamClass;

struct _GabbleTubeStreamClass {
  GObjectClass parent_class;

  TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleTubeStream {
  GObject parent;

  GabbleTubeStreamPrivate *priv;
};

GType gabble_tube_stream_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_TUBE_STREAM \
  (gabble_tube_stream_get_type ())
#define GABBLE_TUBE_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_TUBE_STREAM, GabbleTubeStream))
#define GABBLE_TUBE_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_TUBE_STREAM,\
                           GabbleTubeStreamClass))
#define GABBLE_IS_TUBE_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_TUBE_STREAM))
#define GABBLE_IS_TUBE_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_TUBE_STREAM))
#define GABBLE_TUBE_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_TUBE_STREAM,\
                              GabbleTubeStreamClass))

GabbleTubeStream *gabble_tube_stream_new (GabbleConnection *conn,
    TpHandle handle, TpHandleType handle_type, TpHandle self_handle,
    TpHandle initiator, const gchar *service, GHashTable *parameters,
    guint id, GabbleMucChannel *muc);

gboolean gabble_tube_stream_check_params (TpSocketAddressType address_type,
    const GValue *address, TpSocketAccessControl access_control,
    const GValue *access_control_param, GError **error);

gboolean gabble_tube_stream_offer (GabbleTubeStream *self, GError **error);

GHashTable *gabble_tube_stream_get_supported_socket_types (void);

const gchar * const * gabble_tube_stream_channel_get_allowed_properties (void);

G_END_DECLS

#endif /* #ifndef __GABBLE_TUBE_STREAM_H__ */
