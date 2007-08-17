/*
 * muc-factory.h - Header for GabbleMucFactory
 * Copyright (C) 2006 Collabora Ltd.
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

#ifndef __MUC_FACTORY_H__
#define __MUC_FACTORY_H__

#include <glib-object.h>

#include "bytestream-iface.h"
#include "gabble-types.h"

G_BEGIN_DECLS

typedef struct _GabbleMucFactory GabbleMucFactory;
typedef struct _GabbleMucFactoryClass GabbleMucFactoryClass;

struct _GabbleMucFactoryClass {
  GObjectClass parent_class;
};

struct _GabbleMucFactory {
  GObject parent;
};

GType gabble_muc_factory_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_MUC_FACTORY \
  (gabble_muc_factory_get_type ())
#define GABBLE_MUC_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MUC_FACTORY, \
                              GabbleMucFactory))
#define GABBLE_MUC_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MUC_FACTORY, \
                           GabbleMucFactoryClass))
#define GABBLE_IS_MUC_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MUC_FACTORY))
#define GABBLE_IS_MUC_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MUC_FACTORY))
#define GABBLE_MUC_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MUC_FACTORY, \
                              GabbleMucFactoryClass))

GabbleMucChannel *gabble_muc_factory_find_text_channel (GabbleMucFactory *self,
    TpHandle handle);

void gabble_muc_factory_handle_si_stream_request (GabbleMucFactory *self,
    GabbleBytestreamIface *bytestream, TpHandle room_handle,
    const gchar *stream_id, LmMessage *msg);

GabbleMucChannel *gabble_muc_factory_find_channel (GabbleMucFactory *factory,
    TpHandle handle);

G_END_DECLS

#endif /* #ifndef __MUC_FACTORY_H__ */
