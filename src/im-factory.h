/*
 * im-factory.h - Header for GabbleImFactory
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

#ifndef __IM_FACTORY_H__
#define __IM_FACTORY_H__

#include <glib-object.h>

#include "gabble-im-channel.h"

G_BEGIN_DECLS

typedef struct _GabbleImFactory GabbleImFactory;
typedef struct _GabbleImFactoryClass GabbleImFactoryClass;

struct _GabbleImFactoryClass {
  GObjectClass parent_class;
};

struct _GabbleImFactory {
  GObject parent;
};

GType gabble_im_factory_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_IM_FACTORY \
  (gabble_im_factory_get_type ())
#define GABBLE_IM_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_IM_FACTORY, GabbleImFactory))
#define GABBLE_IM_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_IM_FACTORY, GabbleImFactoryClass))
#define GABBLE_IS_IM_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_IM_FACTORY))
#define GABBLE_IS_IM_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_IM_FACTORY))
#define GABBLE_IM_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_IM_FACTORY, GabbleImFactoryClass))


G_END_DECLS

#endif /* #ifndef __IM_FACTORY_H__ */

