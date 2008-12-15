/*
 * gibber-tcp-transport.c - Source for GibberTCPTransport
 * Copyright (C) 2006 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd@luon.net>
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

#include "gibber-tcp-transport.h"

#define DEBUG_FLAG DEBUG_NET
#include "gibber-debug.h"

#include "errno.h"

G_DEFINE_TYPE(GibberTCPTransport, gibber_tcp_transport,
              GIBBER_TYPE_FD_TRANSPORT)

/* private structure */
typedef struct _GibberTCPTransportPrivate GibberTCPTransportPrivate;

struct _GibberTCPTransportPrivate
{
  gboolean dispose_has_run;
};

#define GIBBER_TCP_TRANSPORT_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_TCP_TRANSPORT, \
   GibberTCPTransportPrivate))

static void
gibber_tcp_transport_init (GibberTCPTransport *obj)
{
  /* GibberTCPTransportPrivate *priv = GIBBER_TCP_TRANSPORT_GET_PRIVATE (obj);
   */

  /* allocate any data required by the object here */
}

static void gibber_tcp_transport_dispose (GObject *object);
static void gibber_tcp_transport_finalize (GObject *object);

static void
gibber_tcp_transport_class_init (
  GibberTCPTransportClass *gibber_tcp_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_tcp_transport_class);

  g_type_class_add_private (gibber_tcp_transport_class,
    sizeof (GibberTCPTransportPrivate));

  object_class->dispose = gibber_tcp_transport_dispose;
  object_class->finalize = gibber_tcp_transport_finalize;
}

void
gibber_tcp_transport_dispose (GObject *object)
{
  GibberTCPTransport *self = GIBBER_TCP_TRANSPORT (object);
  GibberTCPTransportPrivate *priv = GIBBER_TCP_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gibber_tcp_transport_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_tcp_transport_parent_class)->dispose (object);
}

void
gibber_tcp_transport_finalize (GObject *object)
{
  /*
  GibberTCPTransport *self = GIBBER_TCP_TRANSPORT (object);
  GibberTCPTransportPrivate *priv = GIBBER_TCP_TRANSPORT_GET_PRIVATE (self);
  */

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (gibber_tcp_transport_parent_class)->finalize (object);
}

GibberTCPTransport *
gibber_tcp_transport_new ()
{
  return g_object_new (GIBBER_TYPE_TCP_TRANSPORT, NULL);
}

void
gibber_tcp_transport_connect (GibberTCPTransport *tcp_transport,
    const gchar *host, const gchar *port)
{
  int fd = -1, ret = -1;
  struct addrinfo req, *ans = NULL, *tmpaddr;
  char name[NI_MAXHOST], portname[NI_MAXSERV];

  gibber_transport_set_state (GIBBER_TRANSPORT (tcp_transport),
                             GIBBER_TRANSPORT_CONNECTING);

  memset (&req, 0, sizeof (req));
  req.ai_flags = 0;
  req.ai_family = AF_UNSPEC;
  req.ai_socktype = SOCK_STREAM;
  req.ai_protocol = IPPROTO_TCP;

  ret = getaddrinfo (host, port, &req, &ans);
  if (ret != 0)
    {
      DEBUG("getaddrinfo failed: %s", gai_strerror (ret));
      goto failed;
    }

  tmpaddr = ans;
  while (tmpaddr != NULL)
    {
      getnameinfo (tmpaddr->ai_addr, tmpaddr->ai_addrlen,
          name, sizeof (name), portname, sizeof (portname),
          NI_NUMERICHOST | NI_NUMERICSERV);

      DEBUG ( "Trying %s port %s...", name, portname);

      fd = socket (tmpaddr->ai_family, tmpaddr->ai_socktype,
          tmpaddr->ai_protocol);

      if (fd < 0)
        {
          DEBUG("socket failed: %s", strerror (errno));
        }
      else if ((ret = connect (fd, tmpaddr->ai_addr, tmpaddr->ai_addrlen)) < 0)
        {
          DEBUG( "connect failed: %s", strerror (errno));
        }
      else
        {
          break;
        }

      tmpaddr = tmpaddr->ai_next;
    }

  if (ret != 0 || fd < 0)
    {
      goto failed;
    }

  DEBUG ("succeeded");

  gibber_fd_transport_set_fd (GIBBER_FD_TRANSPORT (tcp_transport), fd);

  freeaddrinfo (ans);
  return;

failed:
  if (ans != NULL)
    freeaddrinfo (ans);

  gibber_transport_set_state (GIBBER_TRANSPORT (tcp_transport),
      GIBBER_TRANSPORT_DISCONNECTED);

}
