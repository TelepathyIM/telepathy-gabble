/*
 * tubes-factory.h - Header for GabbleTubesFactory
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

#ifndef __TUBES_FACTORY_H__
#define __TUBES_FACTORY_H__

#include <glib-object.h>
#include <loudmouth/loudmouth.h>

#include <telepathy-glib/base-connection.h>
#include "gabble-connection.h"
#include "bytestream-ibb.h"
#include "tubes-channel.h"

G_BEGIN_DECLS

typedef struct _GabbleTubesFactoryClass GabbleTubesFactoryClass;

struct _GabbleTubesFactoryClass {
  GObjectClass parent_class;
};

struct _GabbleTubesFactory {
  GObject parent;

  gpointer priv;
};

GType gabble_tubes_factory_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_TUBES_FACTORY \
  (gabble_tubes_factory_get_type ())
#define GABBLE_TUBES_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_TUBES_FACTORY,\
                              GabbleTubesFactory))
#define GABBLE_TUBES_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_TUBES_FACTORY,\
                           GabbleTubesFactoryClass))
#define GABBLE_IS_TUBES_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_TUBES_FACTORY))
#define GABBLE_IS_TUBES_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_TUBES_FACTORY))
#define GABBLE_TUBES_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_TUBES_FACTORY,\
                              GabbleTubesFactoryClass))

GabbleTubesFactory *
gabble_tubes_factory_new (GabbleConnection *conn);

void
gabble_tubes_factory_handle_si_request (GabbleTubesFactory *fac,
    GabbleBytestreamIBB *bytestream, TpHandle handle, const gchar *stream_id,
    LmMessage *msg);

G_END_DECLS

#endif /* #ifndef __TUBES_FACTORY_H__ */

