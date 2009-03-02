/*
 * gibber-resolver-asyncns.h - Header for GibberResolverAsyncns
 * Copyright (C) 2008 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#ifndef __GIBBER_RESOLVER_ASYNCNS_H__
#define __GIBBER_RESOLVER_ASYNCNS_H__

#include <glib-object.h>
#include "gibber-resolver.h"

G_BEGIN_DECLS

typedef struct _GibberResolverAsyncns GibberResolverAsyncns;
typedef struct _GibberResolverAsyncnsClass GibberResolverAsyncnsClass;

struct _GibberResolverAsyncnsClass {
    GibberResolverClass parent_class;
};

struct _GibberResolverAsyncns {
    GibberResolver parent;
};

GType gibber_resolver_asyncns_get_type(void);

/* TYPE MACROS */
#define GIBBER_TYPE_RESOLVER_ASYNCNS \
  (gibber_resolver_asyncns_get_type())
#define GIBBER_RESOLVER_ASYNCNS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_RESOLVER_ASYNCNS, GibberResolverAsyncns))
#define GIBBER_RESOLVER_ASYNCNS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_RESOLVER_ASYNCNS, GibberResolverAsyncnsClass))
#define GIBBER_IS_RESOLVER_ASYNCNS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_RESOLVER_ASYNCNS))
#define GIBBER_IS_RESOLVER_ASYNCNS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_RESOLVER_ASYNCNS))
#define GIBBER_RESOLVER_ASYNCNS_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_RESOLVER_ASYNCNS, GibberResolverAsyncnsClass))


G_END_DECLS

#endif /* #ifndef __GIBBER_RESOLVER_ASYNCNS_H__*/
