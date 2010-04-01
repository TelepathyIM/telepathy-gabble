/*
 * gibber-linklocal-transport.c - Source for GibberLLTransport
 * Copyright (C) 2006 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd@luon.net>
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "gibber-sockets.h"
#include "gibber-linklocal-transport.h"
#include "gibber-util.h"

#define DEBUG_FLAG DEBUG_NET
#include "gibber-debug.h"

/* Buffer size used for reading input */
#define BUFSIZE 1024

G_DEFINE_TYPE(GibberLLTransport, gibber_ll_transport, GIBBER_TYPE_FD_TRANSPORT)

GQuark
gibber_ll_transport_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gibber_linklocal_transport_error");

  return quark;
}

/* private structure */
typedef struct _GibberLLTransportPrivate GibberLLTransportPrivate;

struct _GibberLLTransportPrivate
{
  gboolean incoming;
  gboolean dispose_has_run;
};

#define GIBBER_LL_TRANSPORT_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_LL_TRANSPORT, GibberLLTransportPrivate))

static void gibber_ll_transport_finalize (GObject *object);

static void
gibber_ll_transport_init (GibberLLTransport *self)
{
  GibberLLTransportPrivate *priv = GIBBER_LL_TRANSPORT_GET_PRIVATE (self);
  priv->incoming = FALSE;
}

static void gibber_ll_transport_dispose (GObject *object);
static void
gibber_ll_transport_class_init (GibberLLTransportClass *gibber_ll_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_ll_transport_class);

  g_type_class_add_private (gibber_ll_transport_class,
                            sizeof (GibberLLTransportPrivate));

  object_class->dispose = gibber_ll_transport_dispose;
  object_class->finalize = gibber_ll_transport_finalize;
}

void
gibber_ll_transport_dispose (GObject *object)
{
  GibberLLTransport *self = GIBBER_LL_TRANSPORT (object);
  GibberLLTransportPrivate *priv = GIBBER_LL_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (gibber_ll_transport_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_ll_transport_parent_class)->dispose (object);
}

void
gibber_ll_transport_finalize (GObject *object)
{
  G_OBJECT_CLASS (gibber_ll_transport_parent_class)->finalize (object);
}

GibberLLTransport *
gibber_ll_transport_new (void)
{
  return g_object_new (GIBBER_TYPE_LL_TRANSPORT, NULL);
}

void
gibber_ll_transport_open_fd (GibberLLTransport *transport, int fd)
{
  GibberLLTransportPrivate *priv = GIBBER_LL_TRANSPORT_GET_PRIVATE (transport);

  priv->incoming = TRUE;

  gibber_transport_set_state (GIBBER_TRANSPORT (transport),
      GIBBER_TRANSPORT_CONNECTING);
  gibber_fd_transport_set_fd (GIBBER_FD_TRANSPORT (transport), fd, TRUE);
}

gboolean
gibber_ll_transport_open_sockaddr (GibberLLTransport *transport,
    struct sockaddr_storage *addr, GError **error)
{
  GibberLLTransportPrivate *priv = GIBBER_LL_TRANSPORT_GET_PRIVATE (transport);
  char host[NI_MAXHOST];
  char port[NI_MAXSERV];
  int fd;
  int ret;

  g_assert (!priv->incoming);

  gibber_transport_set_state (GIBBER_TRANSPORT(transport),
      GIBBER_TRANSPORT_CONNECTING);

  if (getnameinfo ((struct sockaddr *)addr, sizeof (struct sockaddr_storage),
      host, NI_MAXHOST, port, NI_MAXSERV,
      NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
    DEBUG("Trying to connect to %s port %s", host, port);
  } else {
    DEBUG("Connecting..");
  }

  fd = socket (addr->ss_family, SOCK_STREAM, 0);
  if (fd < 0)
    {
      g_set_error (error, GIBBER_LL_TRANSPORT_ERROR,
          GIBBER_LL_TRANSPORT_ERROR_FAILED,
          "Getting socket failed: %s", g_strerror (errno));
      DEBUG("Getting socket failed: %s", strerror(errno));
      goto failed;
    }

  ret = connect (fd, (struct sockaddr *)addr,
      sizeof (struct sockaddr_storage));
  if (ret < 0)
    {
      g_set_error (error, GIBBER_LL_TRANSPORT_ERROR,
          GIBBER_LL_TRANSPORT_ERROR_CONNECT_FAILED,
          "Connect failed: %s", g_strerror (errno));
      DEBUG("Connecting failed: %s", strerror (errno));
      goto failed;
    }

  gibber_fd_transport_set_fd (GIBBER_FD_TRANSPORT (transport), fd, TRUE);
  return TRUE;

failed:
  gibber_transport_set_state (GIBBER_TRANSPORT (transport),
      GIBBER_TRANSPORT_DISCONNECTED);
  if (fd >= 0)
    {
      close (fd);
    }
  return FALSE;
}

gboolean
gibber_ll_transport_is_incoming (GibberLLTransport *transport)
{
  GibberLLTransportPrivate *priv = GIBBER_LL_TRANSPORT_GET_PRIVATE (transport);
  return priv->incoming;
}

void
gibber_ll_transport_set_incoming (GibberLLTransport *transport,
    gboolean incoming)
{
  GibberLLTransportPrivate *priv = GIBBER_LL_TRANSPORT_GET_PRIVATE (transport);
  g_assert (
    GIBBER_TRANSPORT (transport)->state == GIBBER_TRANSPORT_DISCONNECTED);
  priv->incoming = incoming;
}
