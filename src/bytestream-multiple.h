/*
 * bytestream-socks5.h - Header for GabbleBytestreamMultiple
 * Copyright (C) 2007-2008 Collabora Ltd.
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

#ifndef __GABBLE_BYTESTREAM_MULTIPLE_H__
#define __GABBLE_BYTESTREAM_MULTIPLE_H__

#include <stdlib.h>

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include <telepathy-glib/base-connection.h>

#include "bytestream-iface.h"
#include "error.h"

G_BEGIN_DECLS

typedef struct _GabbleBytestreamMultiple GabbleBytestreamMultiple;
typedef struct _GabbleBytestreamMultipleClass GabbleBytestreamMultipleClass;
typedef struct _GabbleBytestreamMultiplePrivate GabbleBytestreamMultiplePrivate;

struct _GabbleBytestreamMultipleClass {
  GObjectClass parent_class;
};

struct _GabbleBytestreamMultiple {
  GObject parent;

  GabbleBytestreamMultiplePrivate *priv;
};

GType gabble_bytestream_multiple_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_BYTESTREAM_MULTIPLE \
  (gabble_bytestream_multiple_get_type ())
#define GABBLE_BYTESTREAM_MULTIPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_BYTESTREAM_MULTIPLE,\
                              GabbleBytestreamMultiple))
#define GABBLE_BYTESTREAM_MULTIPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_BYTESTREAM_MULTIPLE,\
                           GabbleBytestreamMultipleClass))
#define GABBLE_IS_BYTESTREAM_MULTIPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_BYTESTREAM_MULTIPLE))
#define GABBLE_IS_BYTESTREAM_MULTIPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_BYTESTREAM_MULTIPLE))
#define GABBLE_BYTESTREAM_MULTIPLE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_BYTESTREAM_MULTIPLE,\
                              GabbleBytestreamMultipleClass))

void gabble_bytestream_multiple_add_stream_method (
    GabbleBytestreamMultiple *self, const gchar *method);

gboolean gabble_bytestream_multiple_has_stream_method (
    GabbleBytestreamMultiple *self);

G_END_DECLS

#endif /* #ifndef __GABBLE_BYTESTREAM_MULTIPLE_H__ */
