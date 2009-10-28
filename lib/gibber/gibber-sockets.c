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
