/*
 * Fake resolver that returns whatever it's told
 * Copyright (C) 2008-2009 Collabora Ltd.
 * Copyright (C) 2009 Nokia Corporation
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

#ifndef __GABBLE_RESOLVER_FAKE_H__
#define __GABBLE_RESOLVER_FAKE_H__

#include <glib-object.h>

#include <lib/gibber/gibber-resolver.h>

G_BEGIN_DECLS

typedef struct _GabbleResolverFake GabbleResolverFake;
typedef struct _GabbleResolverFakeClass GabbleResolverFakeClass;
typedef struct _GabbleResolverFakePrivate GabbleResolverFakePrivate;

struct _GabbleResolverFakeClass {
    GibberResolverClass parent_class;
};

struct _GabbleResolverFake {
    GibberResolver parent;
    GabbleResolverFakePrivate *priv;
};

GType gabble_resolver_fake_get_type(void);

/* TYPE MACROS */
#define GABBLE_TYPE_RESOLVER_FAKE \
  (gabble_resolver_fake_get_type())
#define GABBLE_RESOLVER_FAKE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_RESOLVER_FAKE, GabbleResolverFake))
#define GABBLE_RESOLVER_FAKE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_RESOLVER_FAKE, GabbleResolverFakeClass))
#define GABBLE_IS_RESOLVER_FAKE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_RESOLVER_FAKE))
#define GABBLE_IS_RESOLVER_FAKE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_RESOLVER_FAKE))
#define GABBLE_RESOLVER_FAKE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_RESOLVER_FAKE, GabbleResolverFakeClass))

G_END_DECLS

#endif
