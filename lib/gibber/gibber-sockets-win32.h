/* To be included by gibber-sockets.h only.
 * Do not include this header directly. */

/*
 * gibber-sockets-win32.h - meta-header for assorted semi-portable socket code
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

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winbase.h>
/* Winsock makes some inappropriately-namespaced definitions */
#undef ERROR
#undef interface
