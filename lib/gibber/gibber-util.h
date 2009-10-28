/*
 * gibber-util.h - Header for Gibber utility functions
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __GIBBER_UTIL_H__
#define __GIBBER_UTIL_H__

#include "gibber-sockets.h"

#include <glib.h>

G_BEGIN_DECLS

void gibber_normalize_address (struct sockaddr_storage *addr);
gboolean gibber_strdiff (const gchar *left, const gchar *right);

G_END_DECLS

#endif /* #ifndef __GIBBER_UTIL_H__ */
