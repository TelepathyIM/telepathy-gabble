/*
 * gibber-unix-transport.h - Header for GibberUnixTransport
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

#ifndef __GIBBER_UNIX_TRANSPORT_H__
#define __GIBBER_UNIX_TRANSPORT_H__

#include <glib-object.h>

#include "gibber-fd-transport.h"

G_BEGIN_DECLS

GQuark gibber_unix_transport_error_quark (void);
#define GIBBER_UNIX_TRANSPORT_ERROR gibber_unix_transport_error_quark()

typedef enum
{
  GIBBER_UNIX_TRANSPORT_ERROR_CONNECT_FAILED,
  GIBBER_UNIX_TRANSPORT_ERROR_FAILED,
} GibberUnixTransportError;

typedef struct _GibberUnixTransport GibberUnixTransport;
typedef struct _GibberUnixTransportClass GibberUnixTransportClass;


struct _GibberUnixTransportClass {
    GibberFdTransportClass parent_class;
};

struct _GibberUnixTransport {
    GibberFdTransport parent;
};

GType gibber_unix_transport_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_UNIX_TRANSPORT \
  (gibber_unix_transport_get_type ())
#define GIBBER_UNIX_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_UNIX_TRANSPORT, \
   GibberUnixTransport))
#define GIBBER_UNIX_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_UNIX_TRANSPORT,  \
   GibberUnixTransportClass))
#define GIBBER_IS_UNIX_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_UNIX_TRANSPORT))
#define GIBBER_IS_UNIX_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_UNIX_TRANSPORT))
#define GIBBER_UNIX_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_UNIX_TRANSPORT, \
   GibberUnixTransportClass))

GibberUnixTransport * gibber_unix_transport_new (void);

gboolean gibber_unix_transport_connect (GibberUnixTransport *transport,
    const gchar *path, GError **error);

G_END_DECLS

#endif /* #ifndef __GIBBER_UNIX_TRANSPORT_H__*/
