/*
 * gibber-linklocal-transport.h - Header for GibberLLTransport
 * Copyright (C) 2006 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd@luon.net>
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

#ifndef __GIBBER_LL_TRANSPORT_H__
#define __GIBBER_LL_TRANSPORT_H__

#include <glib-object.h>

#include "gibber-fd-transport.h"

G_BEGIN_DECLS

GQuark gibber_ll_transport_error_quark (void);
#define GIBBER_LL_TRANSPORT_ERROR gibber_ll_transport_error_quark()

typedef enum
{
  GIBBER_LL_TRANSPORT_ERROR_CONNECT_FAILED,
  GIBBER_LL_TRANSPORT_ERROR_FAILED,
} GibberLLTransportError;

typedef struct _GibberLLTransport GibberLLTransport;
typedef struct _GibberLLTransportClass GibberLLTransportClass;


struct _GibberLLTransportClass {
    GibberFdTransportClass parent_class;
};

struct _GibberLLTransport {
    GibberFdTransport parent;
};

GType gibber_ll_transport_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_LL_TRANSPORT \
  (gibber_ll_transport_get_type ())
#define GIBBER_LL_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_LL_TRANSPORT, \
   GibberLLTransport))
#define GIBBER_LL_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_LL_TRANSPORT,  \
   GibberLLTransportClass))
#define GIBBER_IS_LL_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_LL_TRANSPORT))
#define GIBBER_IS_LL_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_LL_TRANSPORT))
#define GIBBER_LL_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_LL_TRANSPORT, \
   GibberLLTransportClass))

GibberLLTransport * gibber_ll_transport_new (void);

void gibber_ll_transport_open_fd (GibberLLTransport *connection, int fd);

gboolean gibber_ll_transport_open_sockaddr (GibberLLTransport *connection,
    struct sockaddr_storage *addr, GError **error);

gboolean gibber_ll_transport_is_incoming (GibberLLTransport *connection);

void gibber_ll_transport_set_incoming (GibberLLTransport *connetion,
    gboolean incoming);

G_END_DECLS

#endif /* #ifndef __GIBBER_LL_TRANSPORT_H__*/
