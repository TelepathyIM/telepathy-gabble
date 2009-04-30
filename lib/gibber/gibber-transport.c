/*
 * gibber-transport.c - Source for GibberTransport
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


#include <stdio.h>
#include <stdlib.h>

#include "gibber-transport.h"
#include "gibber-signals-marshal.h"

#define DEBUG_FLAG DEBUG_TRANSPORT
#include "gibber-debug.h"

G_DEFINE_TYPE(GibberTransport, gibber_transport, G_TYPE_OBJECT)

/* signal enum */
enum
{
  CONNECTED,
  CONNECTING,
  DISCONNECTED,
  DISCONNECTING,
  ERROR,
  BUFFER_EMPTY,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _GibberTransportPrivate GibberTransportPrivate;

struct _GibberTransportPrivate
{
  gboolean dispose_has_run;
};

#define GIBBER_TRANSPORT_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_TRANSPORT, GibberTransportPrivate))

static void
gibber_transport_init (GibberTransport *obj)
{
  obj->state = GIBBER_TRANSPORT_DISCONNECTED;
  obj->handler = NULL;
}

static void gibber_transport_dispose (GObject *object);
static void gibber_transport_finalize (GObject *object);

static void
gibber_transport_class_init (GibberTransportClass *gibber_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_transport_class);

  g_type_class_add_private (gibber_transport_class, sizeof (GibberTransportPrivate));

  object_class->dispose = gibber_transport_dispose;
  object_class->finalize = gibber_transport_finalize;

  signals[BUFFER_EMPTY] =
    g_signal_new ("buffer-empty",
                  G_OBJECT_CLASS_TYPE (gibber_transport_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[CONNECTED] =
    g_signal_new ("connected",
                  G_OBJECT_CLASS_TYPE (gibber_transport_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[CONNECTING] =
    g_signal_new ("connecting",
                  G_OBJECT_CLASS_TYPE (gibber_transport_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[DISCONNECTING] =
    g_signal_new ("disconnecting",
                  G_OBJECT_CLASS_TYPE (gibber_transport_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[DISCONNECTED] =
    g_signal_new ("disconnected",
                  G_OBJECT_CLASS_TYPE (gibber_transport_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[ERROR] =
    g_signal_new ("error",
                  G_OBJECT_CLASS_TYPE (gibber_transport_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _gibber_signals_marshal_VOID__UINT_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_INT, G_TYPE_STRING);
}

void
gibber_transport_dispose (GObject *object)
{
  GibberTransport *self = GIBBER_TRANSPORT (object);
  GibberTransportPrivate *priv = GIBBER_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gibber_transport_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_transport_parent_class)->dispose (object);
}

void
gibber_transport_finalize (GObject *object)
{
  G_OBJECT_CLASS (gibber_transport_parent_class)->finalize (object);
}

void
gibber_transport_received_data (GibberTransport *transport,
    const guint8 *data, gsize length)
{
  GibberBuffer buffer;
  buffer.length = length;
  buffer.data = data;

  gibber_transport_received_data_custom (transport, &buffer);
}

void
gibber_transport_received_data_custom (GibberTransport *transport,
    GibberBuffer *buffer)
{
  if (G_UNLIKELY (transport->handler == NULL))
    {
      DEBUG("No handler for transport, dropping data!");
    }
  else
    {
      transport->handler (transport, buffer, transport->user_data);
    }
}

void
gibber_transport_set_state (GibberTransport *transport,
    GibberTransportState state)
{
  if (state != transport->state)
    {
      transport->state = state;

      switch (state)
        {
          case GIBBER_TRANSPORT_DISCONNECTED:
            g_signal_emit (transport, signals[DISCONNECTED], 0);
            break;
          case GIBBER_TRANSPORT_CONNECTING:
            g_signal_emit (transport, signals[CONNECTING], 0);
            break;
          case GIBBER_TRANSPORT_CONNECTED:
            g_signal_emit (transport, signals[CONNECTED], 0);
            break;
          case GIBBER_TRANSPORT_DISCONNECTING:
            g_signal_emit (transport, signals[DISCONNECTING], 0);
            break;
        }
    }
}

GibberTransportState
gibber_transport_get_state (GibberTransport *transport)
{
  return transport->state;
}

void
gibber_transport_emit_error (GibberTransport *transport, GError *error)
{
  DEBUG ("Transport error: %s", error->message);
  g_signal_emit (transport, signals[ERROR], 0,
      error->domain, error->code, error->message);
}

gboolean
gibber_transport_send (GibberTransport *transport, const guint8 *data,
  gsize size, GError **error)
{
  GibberTransportClass *cls = GIBBER_TRANSPORT_GET_CLASS (transport);

  g_assert (transport->state == GIBBER_TRANSPORT_CONNECTED);

  return cls->send (transport, data, size, error);
}

void
gibber_transport_disconnect (GibberTransport *transport)
{
  GibberTransportClass *cls = GIBBER_TRANSPORT_GET_CLASS (transport);
  return cls->disconnect (transport);
}

void
gibber_transport_set_handler (GibberTransport *transport,
    GibberHandlerFunc func, gpointer user_data)
{
  g_assert (transport->handler == NULL || func == NULL);

  transport->handler = func;
  transport->user_data = user_data;
}

gboolean
gibber_transport_get_peeraddr (GibberTransport *transport,
   struct sockaddr_storage *addr, socklen_t *len)
{
  GibberTransportClass *cls = GIBBER_TRANSPORT_GET_CLASS (transport);

  if (cls->get_peeraddr != NULL)
    return cls->get_peeraddr (transport, addr, len);

  return FALSE;
}

gboolean
gibber_transport_get_sockaddr (GibberTransport *transport,
   struct sockaddr_storage *addr, socklen_t *len)
{
  GibberTransportClass *cls = GIBBER_TRANSPORT_GET_CLASS (transport);

  if (cls->get_sockaddr != NULL)
    return cls->get_sockaddr (transport, addr, len);

  return FALSE;
}

gboolean
gibber_transport_buffer_is_empty (GibberTransport *transport)
{
  GibberTransportClass *cls = GIBBER_TRANSPORT_GET_CLASS (transport);

  g_assert (cls->buffer_is_empty != NULL);
  return cls->buffer_is_empty (transport);
}

void
gibber_transport_emit_buffer_empty (GibberTransport *transport)
{
  g_signal_emit (transport, signals[BUFFER_EMPTY], 0);
}

void
gibber_transport_block_receiving (GibberTransport *transport,
                                  gboolean block)
{
  GibberTransportClass *cls = GIBBER_TRANSPORT_GET_CLASS (transport);

  g_assert (cls->block_receiving != NULL);
  cls->block_receiving (transport, block);
}

