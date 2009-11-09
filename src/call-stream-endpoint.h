/*
 * gabble-call-stream-endpoint.h - Header for GabbleCallStreamEndpoint
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

#ifndef __GABBLE_CALL_STREAM_ENDPOINT_H__
#define __GABBLE_CALL_STREAM_ENDPOINT_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <telepathy-glib/dbus-properties-mixin.h>
#include "jingle-content.h"

G_BEGIN_DECLS

typedef struct _GabbleCallStreamEndpoint GabbleCallStreamEndpoint;
typedef struct _GabbleCallStreamEndpointPrivate
  GabbleCallStreamEndpointPrivate;
typedef struct _GabbleCallStreamEndpointClass GabbleCallStreamEndpointClass;

struct _GabbleCallStreamEndpointClass {
    GObjectClass parent_class;

    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _GabbleCallStreamEndpoint {
    GObject parent;

    GabbleCallStreamEndpointPrivate *priv;
};

GType gabble_call_stream_endpoint_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_CALL_STREAM_ENDPOINT \
  (gabble_call_stream_endpoint_get_type ())
#define GABBLE_CALL_STREAM_ENDPOINT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
  GABBLE_TYPE_CALL_STREAM_ENDPOINT, GabbleCallStreamEndpoint))
#define GABBLE_CALL_STREAM_ENDPOINT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
  GABBLE_TYPE_CALL_STREAM_ENDPOINT, GabbleCallStreamEndpointClass))
#define GABBLE_IS_CALL_STREAM_ENDPOINT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CALL_STREAM_ENDPOINT))
#define GABBLE_IS_CALL_STREAM_ENDPOINT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CALL_STREAM_ENDPOINT))
#define GABBLE_CALL_STREAM_ENDPOINT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
    GABBLE_TYPE_CALL_STREAM_ENDPOINT, GabbleCallStreamEndpointClass))

GabbleCallStreamEndpoint *
gabble_call_stream_endpoint_new (const gchar *object_path,
  GabbleJingleContent *content);

const gchar *gabble_call_stream_endpoint_get_object_path (
    GabbleCallStreamEndpoint *endpoint);

G_END_DECLS

#endif /* #ifndef __GABBLE_CALL_STREAM_ENDPOINT_H__*/
