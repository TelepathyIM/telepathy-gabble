/*
 * jingle-factory.h - Header for GabbleJingleFactory
 * Copyright (C) 2008 Collabora Ltd.
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

#ifndef __JINGLE_FACTORY_H__
#define __JINGLE_FACTORY_H__

#include <glib-object.h>

#include "jingle-info.h"
#include "jingle-types.h"

G_BEGIN_DECLS

typedef struct _GabbleJingleFactoryClass GabbleJingleFactoryClass;

GType gabble_jingle_factory_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_JINGLE_FACTORY \
  (gabble_jingle_factory_get_type ())
#define GABBLE_JINGLE_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_JINGLE_FACTORY, \
                              GabbleJingleFactory))
#define GABBLE_JINGLE_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_JINGLE_FACTORY, \
                           GabbleJingleFactoryClass))
#define GABBLE_IS_JINGLE_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_JINGLE_FACTORY))
#define GABBLE_IS_JINGLE_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_JINGLE_FACTORY))
#define GABBLE_JINGLE_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_JINGLE_FACTORY, \
                              GabbleJingleFactoryClass))

struct _GabbleJingleFactoryClass {
    GObjectClass parent_class;
};

typedef struct _GabbleJingleFactoryPrivate GabbleJingleFactoryPrivate;

struct _GabbleJingleFactory {
    GObject parent;

    GabbleJingleFactoryPrivate *priv;
};

GabbleJingleFactory *gabble_jingle_factory_new (
    WockySession *session);

void gabble_jingle_factory_stop (GabbleJingleFactory *self);

void gabble_jingle_factory_register_content_type (GabbleJingleFactory *self,
    gchar *xmlns, GType content_type);
GType gabble_jingle_factory_lookup_content_type (GabbleJingleFactory *self,
    const gchar *xmlns);

void gabble_jingle_factory_register_transport (GabbleJingleFactory *self,
    gchar *xmlns, GType transport_type);
GType gabble_jingle_factory_lookup_transport (GabbleJingleFactory *self,
    const gchar *xmlns);

GabbleJingleSession *gabble_jingle_factory_create_session (
    GabbleJingleFactory *fac,
    const gchar *jid,
    JingleDialect dialect,
    gboolean local_hold);

GabbleJingleInfo *gabble_jingle_factory_get_jingle_info (
    GabbleJingleFactory *fac);

G_END_DECLS;

#endif /* __JINGLE_FACTORY_H__ */

