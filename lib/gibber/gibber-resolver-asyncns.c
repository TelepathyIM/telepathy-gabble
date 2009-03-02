/*
 * gibber-resolver-asyncns.c - Source for GibberResolverAsyncns
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <resolv.h>

#include <asyncns.h>

#include "gibber-resolver-asyncns.h"

G_DEFINE_TYPE(GibberResolverAsyncns, gibber_resolver_asyncns,
  GIBBER_TYPE_RESOLVER)

/* private structure */
typedef struct _GibberResolverAsyncnsPrivate GibberResolverAsyncnsPrivate;

struct _GibberResolverAsyncnsPrivate
{
  asyncns_t *asyncns;
  GIOChannel *asyncio;
  int asyncns_fd;
  guint watch_id;

  gboolean dispose_has_run;
};

typedef enum {
  GIBBER_RESOLVER_ASYNCNS_QUERY_TYPE_SRV,
  GIBBER_RESOLVER_ASYNCNS_QUERY_TYPE_GETADDRINFO,
  GIBBER_RESOLVER_ASYNCNS_QUERY_TYPE_GETNAMEINFO,
} GibberResolverAsyncnsQueryType;

typedef struct {
  GibberResolverAsyncnsQueryType type;
  guint jobid;
  asyncns_query_t *query;
} GibberResolverAsyncnsQuery;

static gboolean asyncns_resolv_srv (GibberResolver *resolver, guint id,
  const gchar *service_name, const char *service,
  GibberResolverServiceType type);

static gboolean asyncns_resolv_addrinfo (GibberResolver *resolver, guint id,
  const gchar *hostname, const char *port, int address_family, int sock_type,
  int protocol, int flags);

static gboolean asyncns_resolv_nameinfo (GibberResolver *resolver, guint id,
  const struct sockaddr *sa, socklen_t salen, gint flags);

static void asyncns_resolv_cancel (GibberResolver *resolver, guint id);

static gboolean asyncns_io_read_cb (GIOChannel *source,
  GIOCondition condition, gpointer data);

#define GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_RESOLVER_ASYNCNS, \
  GibberResolverAsyncnsPrivate))

static void
gibber_resolver_asyncns_init (GibberResolverAsyncns *obj)
{
  GibberResolverAsyncnsPrivate *priv =
    GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE (obj);

  priv->asyncns = asyncns_new (2);
  priv->asyncns_fd = asyncns_fd (priv->asyncns);
  priv->asyncio = g_io_channel_unix_new (priv->asyncns_fd);
  priv->watch_id = g_io_add_watch (priv->asyncio, G_IO_IN, asyncns_io_read_cb,
      obj);
}

static void gibber_resolver_asyncns_dispose (GObject *object);
static void gibber_resolver_asyncns_finalize (GObject *object);

static void
gibber_resolver_asyncns_class_init (
  GibberResolverAsyncnsClass *gibber_resolver_asyncns_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_resolver_asyncns_class);
  GibberResolverClass *resolver_class = GIBBER_RESOLVER_CLASS
      (gibber_resolver_asyncns_class);

  g_type_class_add_private (gibber_resolver_asyncns_class,
      sizeof (GibberResolverAsyncnsPrivate));

  object_class->dispose = gibber_resolver_asyncns_dispose;
  object_class->finalize = gibber_resolver_asyncns_finalize;

  resolver_class->resolv_srv = asyncns_resolv_srv;
  resolver_class->resolv_addrinfo = asyncns_resolv_addrinfo;
  resolver_class->resolv_nameinfo = asyncns_resolv_nameinfo;
  resolver_class->resolv_cancel = asyncns_resolv_cancel;
}

void
gibber_resolver_asyncns_dispose (GObject *object)
{
  GibberResolverAsyncns *self = GIBBER_RESOLVER_ASYNCNS (object);
  GibberResolverAsyncnsPrivate *priv =
      GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->watch_id != 0)
    g_source_remove (priv->watch_id);
  priv->watch_id = 0;

  if (priv->asyncio != NULL)
    g_io_channel_shutdown (priv->asyncio, FALSE, NULL);
  priv->asyncio = NULL;

  if (priv->asyncns != NULL)
    asyncns_free (priv->asyncns);
  priv->asyncns = NULL;

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (gibber_resolver_asyncns_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_resolver_asyncns_parent_class)->dispose (object);
}

void
gibber_resolver_asyncns_finalize (GObject *object)
{
  /* free any data held directly by the object here */
  G_OBJECT_CLASS (gibber_resolver_asyncns_parent_class)->finalize (object);
}

static void
gibber_resolver_asyncns_query_add (GibberResolverAsyncns *resolver,
  GibberResolverAsyncnsQueryType type, guint jobid, asyncns_query_t *query)
{
  GibberResolverAsyncnsPrivate *priv =
      GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE (resolver);
  GibberResolverAsyncnsQuery *q = g_slice_new (GibberResolverAsyncnsQuery);

  q->type = type;
  q->jobid = jobid;
  q->query = query;

  asyncns_setuserdata (priv->asyncns, query, q);
  gibber_resolver_set_data (GIBBER_RESOLVER (resolver), jobid, q);
}

static void
gibber_resolver_asyncns_query_free (GibberResolverAsyncns *resolver,
  GibberResolverAsyncnsQuery *query)
{
  g_slice_free (GibberResolverAsyncnsQuery, query);
}

static void
gibber_resolver_syncns_srv_done (GibberResolverAsyncns *self,
    asyncns_query_t *query, GibberResolverAsyncnsQuery *asyncquery)
{
  GibberResolverAsyncnsPrivate *priv =
      GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE (self);
  int ret;
  unsigned char *answer;
  GList *entries = NULL;
  GError *error = NULL;

  ret = asyncns_res_done (priv->asyncns, query, &answer);

  if (ret >= 0)
    {
      entries = gibber_resolver_res_query_to_list (answer, ret);
      if (entries == NULL)
        error = g_error_new (GIBBER_RESOLVER_ERROR,
          GIBBER_RESOLVER_ERROR_RESOLVE_FAILURE, "Invalid reply received");
      free (answer);
    }
  else
   {
     /* FIXME libasyncns actually returns -errno, but that's normally
      * unusefull... libasyncns should be fixed here.. */
     error = gibber_resolver_h_error_to_g_error (-ret);
   }

  gibber_resolver_srv_result (GIBBER_RESOLVER (self), asyncquery->jobid,
    entries, error);

  if (error != NULL)
    g_error_free (error);

  gibber_resolver_asyncns_query_free (self, asyncquery);
}

static void
gibber_resolver_syncns_addrinfo_done (GibberResolverAsyncns *self,
    asyncns_query_t *query, GibberResolverAsyncnsQuery *asyncquery)
{
  GibberResolverAsyncnsPrivate *priv =
      GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE (self);
  int ret;
  struct addrinfo *addrs;

  ret = asyncns_getaddrinfo_done (priv->asyncns, query, &addrs);
  if (ret != 0)
    {
      GError *err = gibber_resolver_gai_error_to_g_error (ret);
      gibber_resolver_addrinfo_result (GIBBER_RESOLVER (self),
        asyncquery->jobid, NULL, err);
      g_error_free (err);
    }
  else
    {
      struct addrinfo *a;
      GList *entries = NULL;

      for (a = addrs; a != NULL; a = a->ai_next)
        {
          entries = g_list_append (entries,
            gibber_resolver_addrinfo_new (a->ai_family, a->ai_socktype,
              a->ai_protocol, a->ai_addr, a->ai_addrlen));
        }
      gibber_resolver_addrinfo_result (GIBBER_RESOLVER (self),
        asyncquery->jobid, entries, NULL);
      asyncns_freeaddrinfo (addrs);
    }

  gibber_resolver_asyncns_query_free (self, asyncquery);
}

static void
gibber_resolver_syncns_nameinfo_done (GibberResolverAsyncns *self,
    asyncns_query_t *query, GibberResolverAsyncnsQuery *asyncquery)
{
  GibberResolverAsyncnsPrivate *priv =
      GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE (self);
  gchar host[NI_MAXHOST];
  gchar serv[NI_MAXSERV];
  int ret;

  ret = asyncns_getnameinfo_done (priv->asyncns, query,
    host, NI_MAXHOST, serv, NI_MAXSERV);

  if (ret == 0)
    {
      gibber_resolver_nameinfo_result (GIBBER_RESOLVER (self),
        asyncquery->jobid, g_strdup (host), g_strdup (serv), NULL);
    }
  else
   {
     GError *err = gibber_resolver_gai_error_to_g_error (ret);
     gibber_resolver_nameinfo_result (GIBBER_RESOLVER (self),
       asyncquery->jobid, NULL, NULL, err);
     g_error_free (err);
   }

  gibber_resolver_asyncns_query_free (self, asyncquery);
}

static gboolean
asyncns_io_read_cb (GIOChannel *source, GIOCondition condition, gpointer data)
{
  GibberResolverAsyncns *self = GIBBER_RESOLVER_ASYNCNS (data);
  GibberResolverAsyncnsPrivate *priv =
      GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE (self);
  asyncns_query_t *q;

  asyncns_wait (priv->asyncns, 0);

  while ((q = asyncns_getnext (priv->asyncns)) != NULL)
    {
      GibberResolverAsyncnsQuery *asyncquery;

      asyncquery = (GibberResolverAsyncnsQuery *) asyncns_getuserdata (
        priv->asyncns, q);

      switch (asyncquery->type) {
        case GIBBER_RESOLVER_ASYNCNS_QUERY_TYPE_SRV:
          gibber_resolver_syncns_srv_done (self, q, asyncquery);
          break;
        case GIBBER_RESOLVER_ASYNCNS_QUERY_TYPE_GETADDRINFO:
          gibber_resolver_syncns_addrinfo_done (self, q, asyncquery);
          break;
        case GIBBER_RESOLVER_ASYNCNS_QUERY_TYPE_GETNAMEINFO:
          gibber_resolver_syncns_nameinfo_done (self, q, asyncquery);
          break;
      }
    }

  return TRUE;
}

static gboolean
asyncns_resolv_srv (GibberResolver *resolver, guint id,
  const gchar *service_name, const char *service,
  GibberResolverServiceType type)
{
  GibberResolverAsyncns *self = GIBBER_RESOLVER_ASYNCNS (resolver);
  GibberResolverAsyncnsPrivate *priv =
      GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE (self);
  asyncns_query_t *query;
  gchar *srv_str;

  srv_str = g_strdup_printf ("_%s._%s.%s", service,
    type == GIBBER_RESOLVER_SERVICE_TYPE_TCP ? "tcp" : "udp", service_name);

  query = asyncns_res_query (priv->asyncns, srv_str, C_IN, T_SRV);

  if (query == NULL)
    {
      GError e = { GIBBER_RESOLVER_ERROR, GIBBER_RESOLVER_ERROR_MEMORY,
        "Failed to start asyncns query" };
      gibber_resolver_srv_result (resolver, id, NULL, &e);
    }
  else
   {
    gibber_resolver_asyncns_query_add (self,
      GIBBER_RESOLVER_ASYNCNS_QUERY_TYPE_SRV, id, query);
   }

  g_free (srv_str);

  return query != NULL;
}

static gboolean asyncns_resolv_addrinfo (GibberResolver *resolver, guint id,
  const gchar *hostname, const char *port, int address_family, int sock_type,
  int protocol, int flags)
{
  GibberResolverAsyncns *self = GIBBER_RESOLVER_ASYNCNS (resolver);
  GibberResolverAsyncnsPrivate *priv =
      GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE (self);
  asyncns_query_t *query;
  struct addrinfo hints;

  memset (&hints, 0, sizeof (hints));
  hints.ai_family = address_family;
  hints.ai_socktype = sock_type;
  hints.ai_protocol = protocol;
  hints.ai_flags = flags;

  query = asyncns_getaddrinfo (priv->asyncns, hostname, port, &hints);

  if (query == NULL)
    {
      GError e = { GIBBER_RESOLVER_ERROR, GIBBER_RESOLVER_ERROR_MEMORY,
        "Failed to start asyncns query" };
      gibber_resolver_srv_result (resolver, id, NULL, &e);
    }
  else
   {
    gibber_resolver_asyncns_query_add (self,
      GIBBER_RESOLVER_ASYNCNS_QUERY_TYPE_GETADDRINFO, id, query);
   }

  return query != NULL;
}

static gboolean
asyncns_resolv_nameinfo (GibberResolver *resolver, guint id,
  const struct sockaddr *sa, socklen_t salen, gint flags)
{
  GibberResolverAsyncns *self = GIBBER_RESOLVER_ASYNCNS (resolver);
  GibberResolverAsyncnsPrivate *priv =
      GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE (self);
  asyncns_query_t *query;

  query = asyncns_getnameinfo (priv->asyncns, sa, salen, flags, TRUE, TRUE);

  if (query == NULL)
    {
      GError e = { GIBBER_RESOLVER_ERROR, GIBBER_RESOLVER_ERROR_MEMORY,
        "Failed to start asyncns query" };
      gibber_resolver_srv_result (resolver, id, NULL, &e);
    }
  else
   {
    gibber_resolver_asyncns_query_add (self,
      GIBBER_RESOLVER_ASYNCNS_QUERY_TYPE_GETNAMEINFO, id, query);
   }

  return query != NULL;
}

static void
asyncns_resolv_cancel (GibberResolver *resolver, guint id)
{
  GibberResolverAsyncnsQuery *query;
  GibberResolverAsyncns *self = GIBBER_RESOLVER_ASYNCNS (resolver);
  GibberResolverAsyncnsPrivate *priv =
      GIBBER_RESOLVER_ASYNCNS_GET_PRIVATE (self);

  query = (GibberResolverAsyncnsQuery *) gibber_resolver_get_data (resolver,
    id);

  asyncns_cancel (priv->asyncns, query->query);

  gibber_resolver_asyncns_query_free (self, query);
}
