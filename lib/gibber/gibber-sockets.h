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

#endif
