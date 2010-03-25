/*
 * gibber-transport.h - Header for GibberTransport
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

#ifndef __GIBBER_TRANSPORT_H__
#define __GIBBER_TRANSPORT_H__

#include <glib-object.h>

#include "gibber-sockets.h"

G_BEGIN_DECLS

typedef enum {
  GIBBER_TRANSPORT_DISCONNECTED = 0,
  GIBBER_TRANSPORT_CONNECTING,
  GIBBER_TRANSPORT_CONNECTED,
  GIBBER_TRANSPORT_DISCONNECTING,
} GibberTransportState;


typedef struct _GibberTransport GibberTransport;
typedef struct _GibberTransportClass GibberTransportClass;
typedef struct _GibberBuffer GibberBuffer;
typedef void (*GibberHandlerFunc) (GibberTransport *transport,
                                   GibberBuffer *buffer,
                                   gpointer user_data);

struct _GibberBuffer {
  const guint8 *data;
  gsize length;
};

struct _GibberTransportClass {
    GObjectClass parent_class;
    gboolean (*send) (GibberTransport *transport,
                          const guint8 *data, gsize length, GError **error);
    void (*disconnect) (GibberTransport *transport);
    gboolean (*get_peeraddr) (GibberTransport *transport,
        struct sockaddr_storage *addr, socklen_t *len);
    gboolean (*get_sockaddr) (GibberTransport *transport,
        struct sockaddr_storage *addr, socklen_t *len);
    gboolean (*buffer_is_empty) (GibberTransport *transport);
    void (*block_receiving) (GibberTransport *transport, gboolean block);
};

struct _GibberTransport {
    GObject parent;
    GibberTransportState state;

    /* Maximum packet size for transports where it matters, 0 otherwise */
    gsize max_packet_size;

    /* FIXME Should be private... */
    GibberHandlerFunc handler;
    gpointer user_data;
};

GType gibber_transport_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_TRANSPORT  (gibber_transport_get_type ())
#define GIBBER_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_TRANSPORT, GibberTransport))
#define GIBBER_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_TRANSPORT, \
   GibberTransportClass))
#define GIBBER_IS_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_TRANSPORT))
#define GIBBER_IS_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_TRANSPORT))
#define GIBBER_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_TRANSPORT, \
   GibberTransportClass))

/* Utility functions for the classes based on GibberTransport   */
void gibber_transport_received_data (GibberTransport *transport,
    const guint8 *data, gsize length);

void gibber_transport_received_data_custom (GibberTransport *transport,
    GibberBuffer *buffer);

void gibber_transport_set_state (GibberTransport *transport,
    GibberTransportState state);

void gibber_transport_emit_error (GibberTransport *transport, GError *error);

/* Public api */
GibberTransportState gibber_transport_get_state (GibberTransport *transport);

gboolean gibber_transport_send (GibberTransport *transport, const guint8 *data,
    gsize size, GError **error);

void gibber_transport_disconnect (GibberTransport *transport);

void gibber_transport_set_handler (GibberTransport *transport,
    GibberHandlerFunc func, gpointer user_data);

gboolean gibber_transport_get_peeraddr (GibberTransport *transport,
    struct sockaddr_storage *addr, socklen_t *len);

gboolean gibber_transport_get_sockaddr (GibberTransport *transport,
    struct sockaddr_storage *addr,
    socklen_t *len);

gboolean gibber_transport_buffer_is_empty (GibberTransport *transport);

void gibber_transport_emit_buffer_empty (GibberTransport *transport);

void gibber_transport_block_receiving (GibberTransport *transport,
    gboolean block);

G_END_DECLS

#endif /* #ifndef __GIBBER_TRANSPORT_H__*/
