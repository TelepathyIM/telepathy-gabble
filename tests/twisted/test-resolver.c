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

/* this code largely culled from gunixresolver.c in glib and modified to
 * make a dummy resolver we can insert duff records in on the fly */

/* examples:
 * GResolver *kludged;
 * kludged = g_object_new (TEST_TYPE_RESOLVER, NULL);
 * g_resolver_set_default (kludged);
 * test_resolver_add_SRV (TEST_RESOLVER (kludged),
 *     "xmpp-client", "tcp", "jabber.earth.li", "localhost", 1337);
 * test_resolver_add_A (TEST_RESOLVER (kludged), "localhost", "127.0.1.1");
 */

#include "config.h"

#include <stdio.h>
#include <glib.h>

#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "test-resolver.h"

typedef struct _fake_host
{
  char *key;
  char *addr;
} fake_host;

typedef struct _fake_serv
{
  char *key;
  GSrvTarget *srv;
} fake_serv;

G_DEFINE_TYPE (TestResolver, test_resolver, G_TYPE_RESOLVER);

/* ************************************************************************* */

static gchar *
_service_rrname (const char *service,
                 const char *protocol,
                 const char *domain)
{
  gchar *rrname, *ascii_domain;

  ascii_domain = g_hostname_to_ascii (domain);
  rrname = g_strdup_printf ("_%s._%s.%s", service, protocol, ascii_domain);
  g_free (ascii_domain);

  return rrname;
}

static GList *
find_fake_services (TestResolver *tr, const char *name)
{
  GList *fake = NULL;
  GList *rval = NULL;

  for (fake = tr->fake_SRV; fake != NULL; fake = g_list_next (fake))
    {
      fake_serv *entry = fake->data;
      if (entry != NULL && !g_strcmp0 (entry->key, name))
          rval = g_list_append (rval, g_srv_target_copy (entry->srv));
    }
  return rval;
}

static GList *
find_fake_hosts (TestResolver *tr, const char *name)
{
  GList *fake = NULL;
  GList *rval = NULL;

  for (fake = tr->fake_A; fake != NULL; fake = g_list_next (fake))
    {
      fake_host *entry = fake->data;
      if (entry != NULL && !g_strcmp0 (entry->key, name))
        rval =
          g_list_append (rval, g_inet_address_new_from_string (entry->addr));
    }
  return rval;
}


static void
lookup_service_async (GResolver *resolver,
    const char *rr,
    GCancellable *cancellable,
    GAsyncReadyCallback  cb,
    gpointer data)
{
  TestResolver *tr = TEST_RESOLVER (resolver);
  GList *addr = find_fake_services (tr, rr);
  GObject *source = G_OBJECT (resolver);
  GSimpleAsyncResult *res =
      g_simple_async_result_new (source, cb, data, lookup_service_async);

  if (addr != NULL)
    g_simple_async_result_set_op_res_gpointer (res, addr, NULL);
  else
    g_simple_async_result_set_error (res, G_RESOLVER_ERROR,
        G_RESOLVER_ERROR_NOT_FOUND, "No fake SRV record registered");

  g_simple_async_result_complete_in_idle (res);
  g_object_unref (res);
}

static GList *
lookup_service_finish (GResolver *resolver,
                       GAsyncResult *result,
                       GError **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;

  return g_simple_async_result_get_op_res_gpointer (simple);
}

static GList *
lookup_by_name (GResolver *resolver,
    const gchar *hostname,
    GCancellable *cancellable,
    GError **error)
{
  GList *result;

  result = find_fake_hosts (TEST_RESOLVER (resolver), hostname);

  if (result == NULL)
    g_set_error (error, G_RESOLVER_ERROR,
        G_RESOLVER_ERROR_NOT_FOUND,
        "No fake hostname record registered");

  return result;
}

#if GLIB_VERSION_CUR_STABLE < G_ENCODE_VERSION(2,60)
typedef enum {
  G_RESOLVER_NAME_LOOKUP_FLAGS_DEFAULT
} GResolverNameLookupFlags;
#endif

static void
lookup_by_name_with_flags_async (GResolver *resolver,
                                 const gchar *hostname,
                                 GResolverNameLookupFlags flags,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback  cb,
                                 gpointer data)
{
  GObject *source = G_OBJECT (resolver);
  GSimpleAsyncResult *res =
      g_simple_async_result_new (source, cb, data, NULL);
  GList *addr;
  GError *error = NULL;

  addr = lookup_by_name (resolver, hostname, NULL, &error);

  if (addr != NULL)
    {
      g_simple_async_result_set_op_res_gpointer (res, addr, NULL);
    }
  else
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);
    }

  g_simple_async_result_complete_in_idle (res);
  g_object_unref (res);
}

static void
lookup_by_name_async (GResolver *resolver,
                      const gchar *hostname,
                      GCancellable *cancellable,
                      GAsyncReadyCallback  cb,
                      gpointer data)
{
  lookup_by_name_with_flags_async (resolver, hostname,
                                   G_RESOLVER_NAME_LOOKUP_FLAGS_DEFAULT,
                                   cancellable, cb, data);
}

static GList *
lookup_by_name_finish (GResolver *resolver,
    GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;

  return g_simple_async_result_get_op_res_gpointer (simple);
}

#if GLIB_VERSION_CUR_STABLE >= (G_ENCODE_VERSION (2, 60))
static GList *
lookup_by_name_with_flags_finish (GResolver *resolver,
    GAsyncResult *result,
    GError **error)
{
  return lookup_by_name_finish (resolver, result, error);
}
#endif

/* ************************************************************************* */

static void
test_resolver_init (TestResolver *tr)
{
}

static void
test_resolver_class_init (TestResolverClass *klass)
{
  GResolverClass *resolver_class = G_RESOLVER_CLASS (klass);

  resolver_class->lookup_by_name_async     = lookup_by_name_async;
  resolver_class->lookup_by_name_finish    = lookup_by_name_finish;
  resolver_class->lookup_service_async     = lookup_service_async;
  resolver_class->lookup_service_finish    = lookup_service_finish;
#if GLIB_VERSION_CUR_STABLE >= (G_ENCODE_VERSION (2, 60))
  resolver_class->lookup_by_name_with_flags_async  = lookup_by_name_with_flags_async;
  resolver_class->lookup_by_name_with_flags_finish = lookup_by_name_with_flags_finish;
#endif
  resolver_class->lookup_by_name = lookup_by_name;
}

void
test_resolver_reset (TestResolver *tr)
{
  GList *fake = NULL;

  for (fake = tr->fake_A; fake != NULL; fake = g_list_next (fake))
    {
      fake_host *entry = fake->data;
      g_free (entry->key);
      g_free (entry->addr);
      g_free (entry);
    }
  g_list_free (tr->fake_A);
  tr->fake_A = NULL;

  for (fake = tr->fake_SRV; fake != NULL; fake = g_list_next (fake))
    {
      fake_serv *entry = fake->data;
      g_free (entry->key);
      g_srv_target_free (entry->srv);
      g_free (entry);
    }
  g_list_free (tr->fake_SRV);
  tr->fake_SRV = NULL;
}

gboolean
test_resolver_add_A (TestResolver *tr,
    const char *hostname,
    const char *addr)
{
  fake_host *entry = g_new0( fake_host, 1 );
  entry->key = g_strdup (hostname);
  entry->addr = g_strdup (addr);
  tr->fake_A = g_list_append (tr->fake_A, entry);
  return TRUE;
}

gboolean test_resolver_add_SRV (TestResolver *tr,
    const char *service,
    const char *protocol,
    const char *domain,
    const char *addr,
    guint16     port)
{
  char *key = _service_rrname (service, protocol, domain);
  fake_serv *entry = g_new0 (fake_serv, 1);
  GSrvTarget *serv = g_srv_target_new (addr, port, 0, 0);
  entry->key = key;
  entry->srv = serv;
  tr->fake_SRV = g_list_append (tr->fake_SRV, entry);
  return TRUE;
}
