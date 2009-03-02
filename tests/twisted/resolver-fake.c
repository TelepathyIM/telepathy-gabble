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

#include "resolver-fake.h"

#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#define DEBUG(format, ...) \
  g_debug ("%s: " format, G_STRFUNC, ##__VA_ARGS__)

G_DEFINE_TYPE(GabbleResolverFake, gabble_resolver_fake,
  GIBBER_TYPE_RESOLVER)

struct _GabbleResolverFakePrivate
{
  GibberResolver *real_resolver;
  gboolean dispose_has_run;
};

static void
gabble_resolver_fake_init (GabbleResolverFake *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GABBLE_TYPE_RESOLVER_FAKE,
      GabbleResolverFakePrivate);

  self->priv->real_resolver = g_object_new (GIBBER_TYPE_RESOLVER,
      NULL);
  self->priv->dispose_has_run = FALSE;
}

static void
gabble_resolver_fake_dispose (GObject *object)
{
  GabbleResolverFake *self = GABBLE_RESOLVER_FAKE (object);

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  g_object_unref (self->priv->real_resolver);

  if (G_OBJECT_CLASS (gabble_resolver_fake_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_resolver_fake_parent_class)->dispose (object);
}

static void
gabble_resolver_fake_finalize (GObject *object)
{
  G_OBJECT_CLASS (gabble_resolver_fake_parent_class)->finalize (object);
}

static void
fake_resolv_srv_cb (GibberResolver *real_resolver,
                    GList *srv_list,
                    GError *error,
                    gpointer user_data,
                    GObject *object)
{
  GabbleResolverFake *self = GABBLE_RESOLVER_FAKE (object);

  gibber_resolver_srv_result (GIBBER_RESOLVER (self),
      GPOINTER_TO_UINT (user_data), srv_list, error);
}

static gboolean
fake_resolv_srv (GibberResolver *resolver,
                 guint id,
                 const gchar *service_name,
                 const char *service,
                 GibberResolverServiceType type)
{
  GabbleResolverFake *self = GABBLE_RESOLVER_FAKE (resolver);
  guint job;

  DEBUG ("id=%u name=\"%s\" service=\"%s\" type=%u",
      id, service_name, service, type);

  job = gibber_resolver_srv (self->priv->real_resolver, service_name, service,
      type, fake_resolv_srv_cb, GUINT_TO_POINTER (id), NULL, G_OBJECT (self));
  /* FIXME: we assume the real resolver is synchronous */
  g_assert (job == 0);
  return FALSE;
}

static void
fake_resolv_addrinfo_cb (GibberResolver *real_resolver,
                         GList *entries,
                         GError *error,
                         gpointer user_data,
                         GObject *object)
{
  GabbleResolverFake *self = GABBLE_RESOLVER_FAKE (object);

  gibber_resolver_addrinfo_result (GIBBER_RESOLVER (self),
      GPOINTER_TO_UINT (user_data), entries, error);
}

static gboolean
fake_resolv_addrinfo (GibberResolver *resolver,
                      guint id,
                      const gchar *hostname,
                      const char *port,
                      int address_family,
                      int sock_type,
                      int protocol,
                      int flags)
{
  GabbleResolverFake *self = GABBLE_RESOLVER_FAKE (resolver);
  guint job;

  DEBUG ("id=%u hostname=\"%s\" port=\"%s\" af=%i sock_type=%i "
      "protocol=%i flags=%i", id, hostname, port, address_family, sock_type,
      protocol, flags);

  if (g_str_has_prefix (hostname, "resolves-to-"))
    {
      g_assert (address_family == AF_UNSPEC || address_family == AF_INET);
      g_assert (sock_type == SOCK_STREAM || sock_type == SOCK_DGRAM);
      g_assert (flags == 0);

      job = gibber_resolver_addrinfo (self->priv->real_resolver,
          hostname + strlen ("resolves-to-"), port,
          address_family, sock_type, protocol, AI_NUMERICHOST,
          fake_resolv_addrinfo_cb, GUINT_TO_POINTER (id), NULL,
          G_OBJECT (self));
      /* FIXME: we assume the real resolver is synchronous */
      g_assert (job == 0);
      return FALSE;
    }

  job = gibber_resolver_addrinfo (self->priv->real_resolver, hostname, port,
      address_family, sock_type, protocol, flags,
      fake_resolv_addrinfo_cb, GUINT_TO_POINTER (id), NULL, G_OBJECT (self));
  /* FIXME: we assume the real resolver is synchronous */
  g_assert (job == 0);
  return FALSE;
}

static void
fake_resolv_nameinfo_cb (GibberResolver *real_resolver,
                         const gchar *host,
                         const gchar *port,
                         GError *error,
                         gpointer user_data,
                         GObject *object)
{
  GabbleResolverFake *self = GABBLE_RESOLVER_FAKE (object);

  gibber_resolver_nameinfo_result (GIBBER_RESOLVER (self),
      GPOINTER_TO_UINT (user_data), host, port, error);
}

static gboolean
fake_resolv_nameinfo (GibberResolver *resolver,
                      guint id,
                      const struct sockaddr *sa,
                      socklen_t salen,
                      gint flags)
{
  GabbleResolverFake *self = GABBLE_RESOLVER_FAKE (resolver);
  guint job;

  switch (sa->sa_family)
    {
    case AF_INET:
        {
          const struct sockaddr_in *sai = (const struct sockaddr_in *) sa;
          guint port = ntohs (sai->sin_port);
          guint ip = ntohl (sai->sin_addr.s_addr);

          DEBUG ("id=%u IPv4 %u.%u.%u.%u port=%u flags=%i", id,
              (ip & 0xFF000000U) >> 24, (ip & 0xFF0000U) >> 16,
              (ip & 0xFF00U) >> 8, (ip & 0xFF), port, flags);
        }
      break;
    default:
      DEBUG ("id=%u sa_family=%u length=%u flags=%i", id,
          (guint) sa->sa_family, (guint) salen, flags);
    }

  job = gibber_resolver_nameinfo (self->priv->real_resolver, sa, salen,
      flags,
      fake_resolv_nameinfo_cb, GUINT_TO_POINTER (id), NULL, G_OBJECT (self));
  /* FIXME: we assume the real resolver is synchronous */
  g_assert (job == 0);
  return FALSE;
}

static void fake_resolv_cancel (GibberResolver *resolver,
                                guint id)
{
  DEBUG ("%u", id);
  /* Nothing to do - we rely on the real resolver being synchronous */
}

static void
gabble_resolver_fake_class_init (GabbleResolverFakeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GibberResolverClass *resolver_class = GIBBER_RESOLVER_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GabbleResolverFakePrivate));

  object_class->dispose = gabble_resolver_fake_dispose;
  object_class->finalize = gabble_resolver_fake_finalize;

  resolver_class->resolv_srv = fake_resolv_srv;
  resolver_class->resolv_addrinfo = fake_resolv_addrinfo;
  resolver_class->resolv_nameinfo = fake_resolv_nameinfo;
  resolver_class->resolv_cancel = fake_resolv_cancel;
}
