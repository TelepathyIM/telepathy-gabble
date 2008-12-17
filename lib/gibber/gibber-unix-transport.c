/*
 * gibber-linklocal-transport.c - Source for GibberLLTransport
 * Copyright (C) 2006, 2008 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd@luon.net>
 *   @author: Alban Crequy <alban.crequy@collabora.co.uk>
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
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "gibber-unix-transport.h"
#include "gibber-util.h"

#define DEBUG_FLAG DEBUG_NET
#include "gibber-debug.h"

G_DEFINE_TYPE(GibberUnixTransport, gibber_unix_transport, \
    GIBBER_TYPE_FD_TRANSPORT)

GQuark
gibber_unix_transport_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gibber_unix_transport_error");

  return quark;
}

/* private structure */
typedef struct _GibberUnixTransportPrivate GibberUnixTransportPrivate;

struct _GibberUnixTransportPrivate
{
  gboolean incoming;
  gboolean dispose_has_run;
};

#define GIBBER_UNIX_TRANSPORT_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_UNIX_TRANSPORT, \
     GibberUnixTransportPrivate))

static void gibber_unix_transport_finalize (GObject *object);

static void
gibber_unix_transport_init (GibberUnixTransport *self)
{
  GibberUnixTransportPrivate *priv = GIBBER_UNIX_TRANSPORT_GET_PRIVATE (self);
  priv->incoming = FALSE;
}

static void gibber_unix_transport_dispose (GObject *object);
static void
gibber_unix_transport_class_init (
    GibberUnixTransportClass *gibber_unix_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_unix_transport_class);

  g_type_class_add_private (gibber_unix_transport_class,
                            sizeof (GibberUnixTransportPrivate));

  object_class->dispose = gibber_unix_transport_dispose;
  object_class->finalize = gibber_unix_transport_finalize;
}

void
gibber_unix_transport_dispose (GObject *object)
{
  GibberUnixTransport *self = GIBBER_UNIX_TRANSPORT (object);
  GibberUnixTransportPrivate *priv = GIBBER_UNIX_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (gibber_unix_transport_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_unix_transport_parent_class)->dispose (object);
}

void
gibber_unix_transport_finalize (GObject *object)
{
  G_OBJECT_CLASS (gibber_unix_transport_parent_class)->finalize (object);
}

GibberUnixTransport *
gibber_unix_transport_new (void)
{
  return g_object_new (GIBBER_TYPE_UNIX_TRANSPORT, NULL);
}

gboolean
gibber_unix_transport_connect (GibberUnixTransport *transport,
                               const gchar *path,
                               GError **error)
{
  union {
      struct sockaddr_un un;
      struct sockaddr addr;
  } addr;
  int fd;

  gibber_transport_set_state (GIBBER_TRANSPORT (transport),
      GIBBER_TRANSPORT_CONNECTING);

  memset (&addr, 0, sizeof (addr));

  fd = socket (PF_UNIX, SOCK_STREAM, 0);
  if (fd == -1)
    {
      DEBUG ("Error creating socket: %s", g_strerror (errno));
      g_set_error (error, GIBBER_UNIX_TRANSPORT_ERROR,
          GIBBER_UNIX_TRANSPORT_ERROR_CONNECT_FAILED,
          "Error creating socket: %s", g_strerror (errno));
      goto failed;
    }

  addr.un.sun_family = PF_UNIX;
  g_strlcpy (addr.un.sun_path, path, sizeof (addr.un.sun_path));

  if (connect (fd, &addr.addr, sizeof (addr.un)) == -1)
    {
      g_set_error (error, GIBBER_UNIX_TRANSPORT_ERROR,
          GIBBER_UNIX_TRANSPORT_ERROR_CONNECT_FAILED,
          "Error connecting socket: %s", g_strerror (errno));
      DEBUG ("Error connecting socket: %s", g_strerror (errno));
      goto failed;
    }
  DEBUG ("Connected to socket");

  gibber_fd_transport_set_fd (GIBBER_FD_TRANSPORT (transport), fd);

  return TRUE;

failed:
  g_assert (error != NULL);
  gibber_transport_emit_error (GIBBER_TRANSPORT(transport), *error);

  gibber_transport_set_state (GIBBER_TRANSPORT (transport),
      GIBBER_TRANSPORT_DISCONNECTED);
  return FALSE;
}

