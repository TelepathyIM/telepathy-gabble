/*
 * test-resolver.c - Source for TestResolver
 * Copyright © 2009 Collabora Ltd.
 * @author Vivek Dasmohapatra <vivek@collabora.co.uk>
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

#ifndef __TEST_RESOLVER_H__
#define __TEST_RESOLVER_H__

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

GType test_resolver_get_type (void);

#define TEST_TYPE_RESOLVER         (test_resolver_get_type ())
#define TEST_RESOLVER(o)           \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), TEST_TYPE_RESOLVER, TestResolver))
#define TEST_RESOLVER_CLASS(k)     \
  (G_TYPE_CHECK_CLASS_CAST((k), TEST_TYPE_RESOLVER, TestResolverClass))
#define TEST_IS_RESOLVER(o)        \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), TEST_TYPE_RESOLVER))
#define TEST_IS_RESOLVER_CLASS(k)  \
  (G_TYPE_CHECK_CLASS_TYPE ((k), TEST_TYPE_RESOLVER))
#define TEST_RESOLVER_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), TEST_TYPE_RESOLVER, TestResolverClass))

typedef struct {
  GResolver parent_instance;
  GList *fake_A;
  GList *fake_SRV;
} TestResolver;

typedef struct {
  GResolverClass parent_class;
} TestResolverClass;

void test_resolver_reset (TestResolver *tr);

gboolean test_resolver_add_A   (TestResolver *tr,
    const char *hostname,
    const char *addr);
gboolean test_resolver_add_SRV (TestResolver *tr,
    const char *service,
    const char *protocol,
    const char *domain,
    const char *addr,
    guint16 port);

G_END_DECLS

#endif /* __TEST_RESOLVER_H__ */
