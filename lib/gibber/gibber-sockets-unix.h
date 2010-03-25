/* To be included by gibber-sockets.h only.
 * Do not include this header directly. */

/*
 * gibber-sockets-unix.h - meta-header for assorted semi-portable socket code
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

/* prerequisite for stdlib.h on Darwin etc., according to autoconf.info */
#include <stdio.h>
/* prerequisite for sys/socket.h on Darwin, according to autoconf.info */
#include <stdlib.h>
/* prerequisite for all sorts of things */
#include <sys/types.h>

#ifdef HAVE_ARPA_INET_H
#   include <arpa/inet.h>
#endif
#ifdef HAVE_ARPA_NAMESER_H
#   include <arpa/nameser.h>
#endif
#ifdef HAVE_FCNTL_H
#   include <fcntl.h>
#endif
#ifdef HAVE_NETDB_H
#   include <netdb.h>
#endif
#ifdef HAVE_NETINET_IN_H
#   include <netinet/in.h>
#endif
#ifdef HAVE_RESOLV_H
    /* autoconf.info recommends putting sys/types.h, netinet/in.h,
     * arpa/nameser.h, netdb.h before this one; if you re-order this header,
     * please keep that true */
#   include <resolv.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#   include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_UN_H
#   include <sys/un.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#   include <sys/socket.h>
#endif
