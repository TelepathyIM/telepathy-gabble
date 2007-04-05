/*
 * media-factory.h - Header for GabbleMediaFactory
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

#ifndef __MEDIA_FACTORY_H__
#define __MEDIA_FACTORY_H__

#include <glib-object.h>

#include "gabble-media-channel.h"

G_BEGIN_DECLS

typedef struct _GabbleMediaFactory GabbleMediaFactory;
typedef struct _GabbleMediaFactoryClass GabbleMediaFactoryClass;

struct _GabbleMediaFactoryClass {
  GObjectClass parent_class;
};

struct _GabbleMediaFactory {
  GObject parent;
};

GType gabble_media_factory_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_MEDIA_FACTORY \
  (gabble_media_factory_get_type ())
#define GABBLE_MEDIA_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_MEDIA_FACTORY, GabbleMediaFactory))
#define GABBLE_MEDIA_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_MEDIA_FACTORY, GabbleMediaFactoryClass))
#define GABBLE_IS_MEDIA_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_MEDIA_FACTORY))
#define GABBLE_IS_MEDIA_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_MEDIA_FACTORY))
#define GABBLE_MEDIA_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_MEDIA_FACTORY, GabbleMediaFactoryClass))

const gchar * _gabble_media_factory_allocate_sid (GabbleMediaFactory *fac, GabbleMediaChannel *chan);
const gchar * _gabble_media_factory_register_sid (GabbleMediaFactory *fac, const gchar *sid, GabbleMediaChannel *chan);
void _gabble_media_factory_free_sid (GabbleMediaFactory *fac, const gchar *sid);

G_END_DECLS

#endif /* #ifndef __MEDIA_FACTORY_H__ */

