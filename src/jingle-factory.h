/*
 * jingle-factory.h - Header for WockyJingleFactory
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

typedef struct _WockyJingleFactoryClass WockyJingleFactoryClass;

GType wocky_jingle_factory_get_type (void);

/* TYPE MACROS */
#define WOCKY_TYPE_JINGLE_FACTORY \
  (wocky_jingle_factory_get_type ())
#define WOCKY_JINGLE_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), WOCKY_TYPE_JINGLE_FACTORY, \
                              WockyJingleFactory))
#define WOCKY_JINGLE_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), WOCKY_TYPE_JINGLE_FACTORY, \
                           WockyJingleFactoryClass))
#define WOCKY_IS_JINGLE_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), WOCKY_TYPE_JINGLE_FACTORY))
#define WOCKY_IS_JINGLE_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), WOCKY_TYPE_JINGLE_FACTORY))
#define WOCKY_JINGLE_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), WOCKY_TYPE_JINGLE_FACTORY, \
                              WockyJingleFactoryClass))

struct _WockyJingleFactoryClass {
    GObjectClass parent_class;
};

typedef struct _WockyJingleFactoryPrivate WockyJingleFactoryPrivate;

struct _WockyJingleFactory {
    GObject parent;

    WockyJingleFactoryPrivate *priv;
};

WockyJingleFactory *wocky_jingle_factory_new (
    WockySession *session);

void wocky_jingle_factory_stop (WockyJingleFactory *self);

void wocky_jingle_factory_register_content_type (WockyJingleFactory *self,
    gchar *xmlns, GType content_type);
GType wocky_jingle_factory_lookup_content_type (WockyJingleFactory *self,
    const gchar *xmlns);

void wocky_jingle_factory_register_transport (WockyJingleFactory *self,
    gchar *xmlns, GType transport_type);
GType wocky_jingle_factory_lookup_transport (WockyJingleFactory *self,
    const gchar *xmlns);

WockyJingleSession *wocky_jingle_factory_create_session (
    WockyJingleFactory *fac,
    const gchar *jid,
    WockyJingleDialect dialect,
    gboolean local_hold);

WockyJingleInfo *wocky_jingle_factory_get_jingle_info (
    WockyJingleFactory *fac);

G_END_DECLS;

#endif /* __JINGLE_FACTORY_H__ */

