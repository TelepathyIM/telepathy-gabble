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

/* needed for struct ucred */
#define _GNU_SOURCE

#include <glib.h>

#ifdef G_OS_UNIX

/* If you claim to be Unix but you don't have these headers, you may have
 * already lost. */
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

  GibberUnixTransportRecvCredentialsCb recv_creds_cb;
  gpointer recv_creds_data;

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
static GibberFdIOResult gibber_unix_transport_read (
    GibberFdTransport *transport,
    GIOChannel *channel,
    GError **error);

static void
gibber_unix_transport_class_init (
    GibberUnixTransportClass *gibber_unix_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_unix_transport_class);
  GibberFdTransportClass *fd_class = GIBBER_FD_TRANSPORT_CLASS (
      gibber_unix_transport_class);

  g_type_class_add_private (gibber_unix_transport_class,
                            sizeof (GibberUnixTransportPrivate));

  object_class->dispose = gibber_unix_transport_dispose;
  object_class->finalize = gibber_unix_transport_finalize;

  /* override GibberFdTransport's read */
  fd_class->read = gibber_unix_transport_read;
}

void
gibber_unix_transport_dispose (GObject *object)
{
  GibberUnixTransport *self = GIBBER_UNIX_TRANSPORT (object);
  GibberUnixTransportPrivate *priv = GIBBER_UNIX_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  priv->recv_creds_cb = NULL;
  priv->recv_creds_data = NULL;

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

GibberUnixTransport *
gibber_unix_transport_new_from_fd (int fd)
{
  GibberUnixTransport *transport;

  transport = gibber_unix_transport_new ();
  gibber_fd_transport_set_fd (GIBBER_FD_TRANSPORT (transport), fd);
  return transport;
}

/* Patches that reimplement these functions for non-Linux would be welcome
 * (please file a bug) */

#if defined(__linux__)

gboolean
gibber_unix_transport_supports_credentials (void)
{
  return TRUE;
}

gboolean
gibber_unix_transport_send_credentials (GibberUnixTransport *transport,
    const guint8 *data,
    gsize size)
{
  int fd, ret;
  struct ucred *cred;
  struct msghdr msg;
  struct cmsghdr *ch;
  struct iovec iov;
  char buffer[CMSG_SPACE (sizeof (struct ucred))];

  DEBUG ("send credentials");
  fd = GIBBER_FD_TRANSPORT (transport)->fd;

  /* Set the message payload */
  memset (&iov, 0, sizeof (iov));
  iov.iov_base = (void *) data;
  iov.iov_len = size;

  memset (&msg, 0, sizeof (msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = buffer;
  msg.msg_controllen = sizeof (buffer);
  memset (buffer, 0, sizeof (buffer));

  /* Set the credentials */
  ch = CMSG_FIRSTHDR (&msg);
  ch->cmsg_len = CMSG_LEN (sizeof (struct ucred));
  ch->cmsg_level = SOL_SOCKET;
  ch->cmsg_type = SCM_CREDENTIALS;

  cred = (struct ucred *) CMSG_DATA (ch);
  cred->pid = getpid ();
  cred->uid = getuid ();
  cred->gid = getgid ();

  ret = sendmsg (fd, &msg, 0);
  if (ret == -1)
    {
      DEBUG ("sendmsg failed: %s", g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

#define BUFSIZE 1024

static GibberFdIOResult
gibber_unix_transport_read (GibberFdTransport *transport,
    GIOChannel *channel,
    GError **error)
{
  GibberUnixTransport *self = GIBBER_UNIX_TRANSPORT (transport);
  GibberUnixTransportPrivate *priv = GIBBER_UNIX_TRANSPORT_GET_PRIVATE (self);
  int fd;
  guint8 buffer[BUFSIZE];
  ssize_t bytes_read;
  GibberBuffer buf;
  struct iovec iov;
  struct msghdr msg;
  char control[CMSG_SPACE (sizeof (struct ucred))];
  struct cmsghdr *ch;
  struct ucred *cred;
  int opt;

  if (priv->recv_creds_cb == NULL)
    return gibber_fd_transport_read (transport, channel, error);

  /* We are waiting for credentials */
  fd = transport->fd;

  /* set SO_PASSCRED flag */
  opt = 1;
  setsockopt (fd, SOL_SOCKET, SO_PASSCRED, &opt, sizeof (opt));

  memset (buffer, 0, sizeof (buffer));
  memset (&iov, 0, sizeof (iov));
  iov.iov_base = buffer;
  iov.iov_len = sizeof (buffer);

  memset (&msg, 0, sizeof (msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof (control);

  bytes_read = recvmsg (fd, &msg, 0);

  if (bytes_read == -1)
    {
      GError *err = NULL;

      g_set_error_literal (&err, G_IO_CHANNEL_ERROR,
          g_io_channel_error_from_errno (errno), "recvmsg failed");

      priv->recv_creds_cb (self, NULL, NULL, err, priv->recv_creds_data);
      g_propagate_error (error, err);

      priv->recv_creds_cb = NULL;
      priv->recv_creds_data = NULL;
      return GIBBER_FD_IO_RESULT_ERROR;
    }

  /* unset SO_PASSCRED flag */
  opt = 0;
  setsockopt (fd, SOL_SOCKET, SO_PASSCRED, &opt, sizeof (opt));

  buf.data = buffer;
  buf.length = bytes_read;

  /* extract the credentials */
  ch = CMSG_FIRSTHDR (&msg);
  if (ch == NULL)
    {
      GError *err = NULL;

      DEBUG ("Message doesn't contain credentials");

      g_set_error_literal (&err, GIBBER_UNIX_TRANSPORT_ERROR,
          GIBBER_UNIX_TRANSPORT_ERROR_NO_CREDENTIALS,
          "no credentials received");

      priv->recv_creds_cb (self, &buf, NULL, err, priv->recv_creds_data);
      g_error_free (err);
    }
  else
    {
      GibberCredentials credentials;

      cred = (struct ucred *) CMSG_DATA (ch);
      credentials.pid = cred->pid;
      credentials.uid = cred->uid;
      credentials.gid = cred->gid;

      priv->recv_creds_cb (self, &buf, &credentials, NULL,
          priv->recv_creds_data);
    }

  priv->recv_creds_cb = NULL;
  priv->recv_creds_data = NULL;
  return GIBBER_FD_IO_RESULT_SUCCESS;
}

gboolean
gibber_unix_transport_recv_credentials (GibberUnixTransport *self,
    GibberUnixTransportRecvCredentialsCb callback,
    gpointer user_data)
{
  GibberUnixTransportPrivate *priv = GIBBER_UNIX_TRANSPORT_GET_PRIVATE (self);

  if (priv->recv_creds_cb != NULL)
    {
      DEBUG ("already waiting for credentials");
      return FALSE;
    }

  priv->recv_creds_cb = callback;
  priv->recv_creds_data = user_data;
  return TRUE;
}

#else /* OSs where we have no implementation */

gboolean
gibber_unix_transport_supports_credentials (void)
{
  return FALSE;
}

gboolean
gibber_unix_transport_recv_credentials (GibberUnixTransport *self,
    GibberUnixTransportRecvCredentialsCb callback,
    gpointer user_data)
{
  DEBUG ("stub implementation, failing");
  return FALSE;
}

gboolean
gibber_unix_transport_send_credentials (GibberUnixTransport *transport,
    const guint8 *data,
    gsize size)
{
  DEBUG ("stub implementation, failing");
  return FALSE;
}

static GibberFdIOResult
gibber_unix_transport_read (GibberFdTransport *transport,
    GIOChannel *channel,
    GError **error)
{
  return gibber_fd_transport_read (transport, channel, error);
}

#endif /* OSs where we have no implementation of credentials */

#endif /* G_OS_UNIX */
