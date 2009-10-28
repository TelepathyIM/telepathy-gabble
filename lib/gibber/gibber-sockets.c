/*
 * gibber-sockets.c - basic portability wrappers for BSD/Winsock differences
 *
 * Copyright (C) 2009 Collabora Ltd.
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
#include "gibber-sockets.h"

#include <errno.h>

#define DEBUG_FLAG DEBUG_NET
#include "gibber-debug.h"

gboolean
gibber_connect_errno_requires_retry (void)
{
#ifdef G_OS_WIN32
  int err = WSAGetLastError ();

  return (err == WSAEINPROGRESS || err == WSAEALREADY);
#else
  return (errno == EINPROGRESS || errno == EALREADY);
#endif
}

gint
gibber_socket_errno (void)
{
#ifdef G_OS_WIN32
  return WSAGetLastError ();
#else
  return errno;
#endif
}

const gchar *
gibber_socket_strerror (void)
{
#ifdef G_OS_WIN32
  return "[no strerror() in winsock :-(]";
#else
  return g_strerror (errno);
#endif
}

gboolean
gibber_socket_errno_is_eafnosupport (void)
{
#ifdef G_OS_WIN32
  return (WSAGetLastError () == WSAEAFNOSUPPORT);
#else
  return (errno == EAFNOSUPPORT);
#endif
}

gboolean
gibber_socket_errno_is_eaddrinuse (void)
{
#ifdef G_OS_WIN32
  return (WSAGetLastError () == WSAEADDRINUSE);
#else
  return (errno == EADDRINUSE);
#endif
}

void
gibber_socket_set_error (GError **error, const gchar *context,
    GQuark domain, gint code)
{
  gint err = gibber_socket_errno ();
  const gchar *str = gibber_socket_strerror ();

  DEBUG ("%s: #%d %s", context, err, str);
  g_set_error (error, domain, code, "%s: #%d %s", context, err, str);
}

GIOChannel *
gibber_io_channel_new_from_socket (gint sockfd)
{
#ifdef G_OS_WIN32
  return g_io_channel_win32_new_socket (sockfd);
#else
  return g_io_channel_unix_new (sockfd);
#endif
}

void
gibber_socket_set_nonblocking (gint sockfd)
{
#ifdef G_OS_WIN32
  u_long please_dont_block = 1;

  ioctlsocket (sockfd, FIONBIO, &please_dont_block);
#else
  fcntl (sockfd, F_SETFL, O_NONBLOCK);
#endif
}
