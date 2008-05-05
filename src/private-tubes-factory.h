/*
 * private-tubes-factory.h - Header for GabblePrivateTubesFactory
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

#ifndef __PRIVATE_TUBES_FACTORY_H__
#define __PRIVATE_TUBES_FACTORY_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include <telepathy-glib/base-connection.h>
#include "gabble-connection.h"
#include "bytestream-iface.h"
#include "tubes-channel.h"

G_BEGIN_DECLS

typedef struct _GabblePrivateTubesFactoryClass GabblePrivateTubesFactoryClass;

struct _GabblePrivateTubesFactoryClass {
  GObjectClass parent_class;
};

struct _GabblePrivateTubesFactory {
  GObject parent;

  gpointer priv;
};

GType gabble_private_tubes_factory_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_PRIVATE_TUBES_FACTORY \
  (gabble_private_tubes_factory_get_type ())
#define GABBLE_PRIVATE_TUBES_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_PRIVATE_TUBES_FACTORY,\
                              GabblePrivateTubesFactory))
#define GABBLE_PRIVATE_TUBES_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_PRIVATE_TUBES_FACTORY,\
                           GabblePrivateTubesFactoryClass))
#define GABBLE_IS_PRIVATE_TUBES_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_PRIVATE_TUBES_FACTORY))
#define GABBLE_IS_PRIVATE_TUBES_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_PRIVATE_TUBES_FACTORY))
#define GABBLE_PRIVATE_TUBES_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_PRIVATE_TUBES_FACTORY,\
                              GabblePrivateTubesFactoryClass))

GabblePrivateTubesFactory * gabble_private_tubes_factory_new (
    GabbleConnection *conn);

/* New 1-1 D-Bus tube requested by the remote contact */
void gabble_private_tubes_factory_handle_si_tube_request (
    GabblePrivateTubesFactory *fac, GabbleBytestreamIface *bytestream,
    TpHandle handle, const gchar *stream_id, LmMessage *msg);

/* New connection requested in a existing 1-1 stream tube */
void gabble_private_tubes_factory_handle_si_stream_request (
    GabblePrivateTubesFactory *fac, GabbleBytestreamIface *bytestream,
    TpHandle handle, const gchar *stream_id, LmMessage *msg);

G_END_DECLS

#endif /* #ifndef __PRIVATE_TUBES_FACTORY_H__ */

