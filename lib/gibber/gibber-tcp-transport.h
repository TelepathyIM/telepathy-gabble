/*
 * gibber-tcp-transport.h - Header for GibberTCPTransport
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

#ifndef __GIBBER_TCP_TRANSPORT_H__
#define __GIBBER_TCP_TRANSPORT_H__

#include <glib-object.h>
#include "gibber-fd-transport.h"

G_BEGIN_DECLS

typedef struct _GibberTCPTransport GibberTCPTransport;
typedef struct _GibberTCPTransportClass GibberTCPTransportClass;

struct _GibberTCPTransportClass {
    GibberFdTransportClass parent_class;
};

struct _GibberTCPTransport {
    GibberFdTransport parent;
};

GType gibber_tcp_transport_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_TCP_TRANSPORT \
  (gibber_tcp_transport_get_type ())
#define GIBBER_TCP_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_TCP_TRANSPORT, \
   GibberTCPTransport))
#define GIBBER_TCP_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_TCP_TRANSPORT, \
   GibberTCPTransportClass))
#define GIBBER_IS_TCP_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_TCP_TRANSPORT))
#define GIBBER_IS_TCP_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_TCP_TRANSPORT))
#define GIBBER_TCP_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_TCP_TRANSPORT, \
   GibberTCPTransportClass))

GibberTCPTransport *
gibber_tcp_transport_new (void);

void gibber_tcp_transport_connect (GibberTCPTransport *tcp_transport,
    const gchar *host, const gchar *port);

G_END_DECLS

#endif /* #ifndef __GIBBER_TCP_TRANSPORT_H__*/
