/*
 * bytestream-factory.h - Header for GabbleBytestreamFactory
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

#ifndef __BYTESTREAM_FACTORY_H__
#define __BYTESTREAM_FACTORY_H__

#include <glib-object.h>
#include "gabble-connection.h"
#include "bytestream-ibb.h"

G_BEGIN_DECLS

typedef struct _GabbleBytestreamFactory GabbleBytestreamFactory;
typedef struct _GabbleBytestreamFactoryClass GabbleBytestreamFactoryClass;

struct _GabbleBytestreamFactoryClass {
  GObjectClass parent_class;
};

struct _GabbleBytestreamFactory {
  GObject parent;

  gpointer priv;
};

GType gabble_bytestream_factory_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_BYTESTREAM_FACTORY \
  (gabble_bytestream_factory_get_type ())
#define GABBLE_BYTESTREAM_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_BYTESTREAM_FACTORY,\
                              GabbleBytestreamFactory))
#define GABBLE_BYTESTREAM_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_BYTESTREAM_FACTORY,\
                           GabbleBytestreamFactoryClass))
#define GABBLE_IS_BYTESTREAM_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_BYTESTREAM_FACTORY))
#define GABBLE_IS_BYTESTREAM_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_BYTESTREAM_FACTORY))
#define GABBLE_BYTESTREAM_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_BYTESTREAM_FACTORY,\
                              GabbleBytestreamFactoryClass))

typedef void (* GabbleBytestreamFactoryNegotiateReplyFunc) (
    GabbleBytestreamIBB *bytestream, const gchar *stream_id, LmMessage *msg,
    gpointer user_data);

GabbleBytestreamFactory *
gabble_bytestream_factory_new (GabbleConnection *conn);

GabbleBytestreamIBB *
gabble_bytestream_factory_create_ibb (GabbleBytestreamFactory *fac, 
    TpHandle peer_handle, TpHandleType peer_handle_type,
    const gchar *stream_id, const gchar *stream_init_id,
    const gchar *peer_resource, gboolean open);

LmMessage *
gabble_bytestream_factory_make_stream_init_message (const gchar *full_jid,
    const gchar *stream_id, const gchar *profile);

gboolean
gabble_bytestream_factory_negotiate_stream (GabbleBytestreamFactory *fac,
    LmMessage *msg, const gchar *stream_id,
    GabbleBytestreamFactoryNegotiateReplyFunc func,
    gpointer user_data, GError **error);

gchar *
gabble_bytestream_factory_generate_stream_id (void);

G_END_DECLS

#endif /* #ifndef __BYTESTREAM_FACTORY_H__ */

