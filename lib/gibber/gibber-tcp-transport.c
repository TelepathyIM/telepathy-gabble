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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "gibber-sockets.h"
#include "gibber-tcp-transport.h"

#define DEBUG_FLAG DEBUG_NET
#include "gibber-debug.h"

#include <gio/gio.h>

#include "errno.h"

G_DEFINE_TYPE(GibberTCPTransport, gibber_tcp_transport,
              GIBBER_TYPE_FD_TRANSPORT)

/* private structure */
typedef struct _GibberTCPTransportPrivate GibberTCPTransportPrivate;

struct _GibberTCPTransportPrivate
{
  GIOChannel *channel;
  GList *addresses;
  guint16 port;
  guint watch_in;

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

static void
clean_connect_attempt (GibberTCPTransport *self)
{
  GibberTCPTransportPrivate *priv = GIBBER_TCP_TRANSPORT_GET_PRIVATE (
      self);

  if (priv->watch_in != 0)
    {
      g_source_remove (priv->watch_in);
      priv->watch_in = 0;
    }

  if (priv->channel != NULL)
    {
      g_io_channel_unref (priv->channel);
      priv->channel = NULL;
    }
}

static void
clean_all_connect_attempts (GibberTCPTransport *self)
{
  GibberTCPTransportPrivate *priv = GIBBER_TCP_TRANSPORT_GET_PRIVATE (
      self);

  clean_connect_attempt (self);

  g_resolver_free_addresses (priv->addresses);
  priv->addresses = NULL;
}

void
gibber_tcp_transport_dispose (GObject *object)
{
  GibberTCPTransport *self = GIBBER_TCP_TRANSPORT (object);
  GibberTCPTransportPrivate *priv = GIBBER_TCP_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  clean_all_connect_attempts (self);
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

static void new_connect_attempt (GibberTCPTransport *self);

static gboolean
try_to_connect (GibberTCPTransport *self)
{
  GibberTCPTransportPrivate *priv = GIBBER_TCP_TRANSPORT_GET_PRIVATE (
      self);
  GSocketAddress *gaddr;
  struct sockaddr_storage addr;
  gint fd;
  int ret;

  g_assert (priv->channel != NULL);

  fd = g_io_channel_unix_get_fd (priv->channel);

  gaddr = g_inet_socket_address_new (G_INET_ADDRESS (priv->addresses->data),
    priv->port);

  g_socket_address_to_native (gaddr, &addr, sizeof (addr), NULL);
  ret = connect (fd, (struct sockaddr *)&addr,
    g_socket_address_get_native_size (gaddr));

  g_object_unref (gaddr);

  if (ret == 0)
    {
      DEBUG ("connect succeeded");

      clean_all_connect_attempts (self);
      gibber_fd_transport_set_fd (GIBBER_FD_TRANSPORT (self), fd, TRUE);
      return FALSE;
    }

  if (gibber_connect_errno_requires_retry ())
    {
      /* We have to wait longer */
      return TRUE;
    }

  clean_connect_attempt (self);
  g_object_unref (priv->addresses->data);
  priv->addresses = g_list_remove_link (priv->addresses, priv->addresses);
  new_connect_attempt (self);

  return FALSE;
}

static gboolean
_channel_io (GIOChannel *source,
             GIOCondition condition,
             gpointer data)
{
  GibberTCPTransport *self = GIBBER_TCP_TRANSPORT (data);

  return try_to_connect (self);
}

static void
new_connect_attempt (GibberTCPTransport *self)
{
  GibberTCPTransportPrivate *priv = GIBBER_TCP_TRANSPORT_GET_PRIVATE (
      self);
  int fd;
  GSocketAddress *gaddr;
  gchar *tmpstr;

  if (priv->addresses == NULL)
    {
      /* no more candidate to try */
      DEBUG ("connection failed");
      goto failed;
    }

  tmpstr = g_inet_address_to_string (G_INET_ADDRESS (priv->addresses->data));
  DEBUG ("Trying %s port %d...", tmpstr, priv->port);
  g_free (tmpstr);

  gaddr = g_inet_socket_address_new (G_INET_ADDRESS (priv->addresses->data),
    priv->port);
  fd = socket (g_socket_address_get_family (gaddr), SOCK_STREAM, IPPROTO_TCP);
  g_object_unref (gaddr);

  if (fd < 0)
    {
      DEBUG("socket failed: #%d %s", gibber_socket_errno (),
          gibber_socket_strerror ());
      goto failed;
    }

  gibber_socket_set_nonblocking (fd);
  priv->channel = gibber_io_channel_new_from_socket (fd);
  g_io_channel_set_close_on_unref (priv->channel, FALSE);
  g_io_channel_set_encoding (priv->channel, NULL, NULL);
  g_io_channel_set_buffered (priv->channel, FALSE);

  priv->watch_in = g_io_add_watch (priv->channel, G_IO_IN | G_IO_PRI | G_IO_OUT,
      _channel_io, self);

  try_to_connect (self);
  return;

failed:
  clean_all_connect_attempts (self);

  gibber_transport_set_state (GIBBER_TRANSPORT (self),
      GIBBER_TRANSPORT_DISCONNECTED);
}

void
gibber_tcp_transport_connect (GibberTCPTransport *tcp_transport,
    const gchar *host, guint16 port)
{
  GibberTCPTransportPrivate *priv = GIBBER_TCP_TRANSPORT_GET_PRIVATE (
      tcp_transport);
  GResolver *resolver = g_resolver_get_default ();
  GError *error = NULL;

  gibber_transport_set_state (GIBBER_TRANSPORT (tcp_transport),
                             GIBBER_TRANSPORT_CONNECTING);

  priv->port = port;

  g_assert (priv->addresses == NULL);
  g_assert (priv->channel == NULL);

  priv->addresses = g_resolver_lookup_by_name (resolver, host, NULL, &error);

  if (priv->addresses == NULL)
    {
      DEBUG("Address lookup failed: %s", error->message);

      gibber_transport_set_state (GIBBER_TRANSPORT (tcp_transport),
          GIBBER_TRANSPORT_DISCONNECTED);

      g_error_free (error);
      goto out;
    }

  new_connect_attempt (tcp_transport);

out:
  g_object_unref (resolver);
}
