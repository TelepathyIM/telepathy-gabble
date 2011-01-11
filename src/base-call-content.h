/*
 * base-call-content.h - Header for GabbleBaseBaseCallContent
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

#ifndef GABBLE_BASE_CALL_CONTENT_H
#define GABBLE_BASE_CALL_CONTENT_H

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>

#include <extensions/extensions.h>

#include "connection.h"
#include "base-call-stream.h"

G_BEGIN_DECLS

typedef struct _GabbleBaseCallContent GabbleBaseCallContent;
typedef struct _GabbleBaseCallContentPrivate GabbleBaseCallContentPrivate;
typedef struct _GabbleBaseCallContentClass GabbleBaseCallContentClass;

typedef void (*GabbleBaseCallContentFunc) (GabbleBaseCallContent *);

struct _GabbleBaseCallContentClass {
    GObjectClass parent_class;

    TpDBusPropertiesMixinClass dbus_props_class;

    const gchar * const *extra_interfaces;
    GabbleBaseCallContentFunc deinit;
};

struct _GabbleBaseCallContent {
    GObject parent;

    GabbleBaseCallContentPrivate *priv;
};

GType gabble_base_call_content_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_BASE_CALL_CONTENT \
  (gabble_base_call_content_get_type ())
#define GABBLE_BASE_CALL_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
      GABBLE_TYPE_BASE_CALL_CONTENT, GabbleBaseCallContent))
#define GABBLE_BASE_CALL_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
    GABBLE_TYPE_BASE_CALL_CONTENT, GabbleBaseCallContentClass))
#define GABBLE_IS_BASE_CALL_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_BASE_CALL_CONTENT))
#define GABBLE_IS_BASE_CALL_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_BASE_CALL_CONTENT))
#define GABBLE_BASE_CALL_CONTENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
    GABBLE_TYPE_BASE_CALL_CONTENT, GabbleBaseCallContentClass))

GabbleConnection *gabble_base_call_content_get_connection (
    GabbleBaseCallContent *self);
const gchar *gabble_base_call_content_get_object_path (
    GabbleBaseCallContent *self);

const gchar *gabble_base_call_content_get_name (GabbleBaseCallContent *self);
TpMediaStreamType gabble_base_call_content_get_media_type (
    GabbleBaseCallContent *self);
TpyCallContentDisposition gabble_base_call_content_get_disposition (
    GabbleBaseCallContent *self);

GList *gabble_base_call_content_get_streams (GabbleBaseCallContent *self);
void gabble_base_call_content_add_stream (GabbleBaseCallContent *self,
    GabbleBaseCallStream *stream);
void gabble_base_call_content_remove_stream (GabbleBaseCallContent *self,
    GabbleBaseCallStream *stream);

void gabble_base_call_content_deinit (GabbleBaseCallContent *self);

G_END_DECLS

#endif /* #ifndef __GABBLE_BASE_CALL_CONTENT_H__*/
