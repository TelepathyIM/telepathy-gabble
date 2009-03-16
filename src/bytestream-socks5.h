/*
 * bytestream-socks5.h - Header for GabbleBytestreamSocks5
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

#ifndef __GABBLE_BYTESTREAM_SOCKS5_H__
#define __GABBLE_BYTESTREAM_SOCKS5_H__

#include <stdlib.h>

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include <telepathy-glib/base-connection.h>

#include "error.h"

G_BEGIN_DECLS

typedef struct _GabbleBytestreamSocks5 GabbleBytestreamSocks5;
typedef struct _GabbleBytestreamSocks5Class GabbleBytestreamSocks5Class;
typedef struct _GabbleBytestreamSocks5Private GabbleBytestreamSocks5Private;

struct _GabbleBytestreamSocks5Class {
  GObjectClass parent_class;
};

struct _GabbleBytestreamSocks5 {
  GObject parent;

  GabbleBytestreamSocks5Private *priv;
};

GType gabble_bytestream_socks5_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_BYTESTREAM_SOCKS5 \
  (gabble_bytestream_socks5_get_type ())
#define GABBLE_BYTESTREAM_SOCKS5(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_BYTESTREAM_SOCKS5,\
                              GabbleBytestreamSocks5))
#define GABBLE_BYTESTREAM_SOCKS5_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_BYTESTREAM_SOCKS5,\
                           GabbleBytestreamSocks5Class))
#define GABBLE_IS_BYTESTREAM_SOCKS5(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_BYTESTREAM_SOCKS5))
#define GABBLE_IS_BYTESTREAM_SOCKS5_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_BYTESTREAM_SOCKS5))
#define GABBLE_BYTESTREAM_SOCKS5_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_BYTESTREAM_SOCKS5,\
                              GabbleBytestreamSocks5Class))

void gabble_bytestream_socks5_add_streamhost (GabbleBytestreamSocks5 *socks5,
    LmMessageNode *streamhost_node);

void gabble_bytestream_socks5_connect_to_streamhost (
    GabbleBytestreamSocks5 *socks5, LmMessage *msg);

G_END_DECLS

#endif /* #ifndef __GABBLE_BYTESTREAM_SOCKS5_H__ */
