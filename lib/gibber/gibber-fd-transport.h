/*
 * gibber-fd-transport.h - Header for GibberFdTransport
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

#ifndef __GIBBER_FD_TRANSPORT_H__
#define __GIBBER_FD_TRANSPORT_H__

#include <glib-object.h>

#include "gibber-sockets.h"
#include "gibber-transport.h"

typedef enum {
  GIBBER_FD_IO_RESULT_SUCCESS,
  GIBBER_FD_IO_RESULT_AGAIN,
  GIBBER_FD_IO_RESULT_ERROR,
  GIBBER_FD_IO_RESULT_EOF,
} GibberFdIOResult;

G_BEGIN_DECLS

GQuark gibber_fd_transport_error_quark (void);
#define GIBBER_FD_TRANSPORT_ERROR gibber_fd_transport_error_quark()

typedef enum
{
  GIBBER_FD_TRANSPORT_ERROR_PIPE,
  GIBBER_FD_TRANSPORT_ERROR_FAILED,
} GibberFdTransportError;

typedef struct _GibberFdTransport GibberFdTransport;
typedef struct _GibberFdTransportClass GibberFdTransportClass;


struct _GibberFdTransportClass {
    GibberTransportClass parent_class;
    /* Called when fd is ready for reading */
    GibberFdIOResult (*read) (GibberFdTransport *fd_transport,
        GIOChannel *channel, GError **error);
    /* Called when something needs to be written*/
    GibberFdIOResult (*write) (GibberFdTransport *fd_transport,
        GIOChannel *channel, const guint8 *data, int len,
        gsize *written, GError **error);
};

struct _GibberFdTransport {
    GibberTransport parent;
    int fd;
};

GType gibber_fd_transport_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_FD_TRANSPORT \
  (gibber_fd_transport_get_type ())
#define GIBBER_FD_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_FD_TRANSPORT, \
   GibberFdTransport))
#define GIBBER_FD_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_FD_TRANSPORT, \
   GibberFdTransportClass))
#define GIBBER_IS_FD_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_FD_TRANSPORT))
#define GIBBER_IS_FD_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_FD_TRANSPORT))
#define GIBBER_FD_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_FD_TRANSPORT, \
   GibberFdTransportClass))

void
gibber_fd_transport_set_fd (GibberFdTransport *fd_transport, int fd,
    gboolean is_socket);

GibberFdIOResult gibber_fd_transport_read (GibberFdTransport *transport,
    GIOChannel *channel,
    GError **error);

G_END_DECLS

#endif /* #ifndef __GIBBER_FD_TRANSPORT_H__*/
