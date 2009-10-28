/*
 * gibber-sockets.h - meta-header for assorted semi-portable socket code
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

#ifndef GIBBER_SOCKETS_H
#define GIBBER_SOCKETS_H

#include <glib.h>

#ifdef G_OS_WIN32
#   include "gibber-sockets-win32.h"
#else
#   include "gibber-sockets-unix.h"
#endif

G_BEGIN_DECLS

gboolean gibber_connect_errno_requires_retry (void);
gboolean gibber_socket_errno_is_eafnosupport (void);
gboolean gibber_socket_errno_is_eaddrinuse (void);
void gibber_socket_set_error (GError **error, const gchar *context,
    GQuark domain, gint code);
gint gibber_socket_errno (void);
const gchar *gibber_socket_strerror (void);

GIOChannel *gibber_io_channel_new_from_socket (gint sockfd);

void gibber_socket_set_nonblocking (gint sockfd);

G_END_DECLS

#endif
