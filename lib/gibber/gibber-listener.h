/*
 * gibber-listener.h - Header for GibberListener
 * Copyright (C) 2007, 2008 Collabora Ltd.
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

#ifndef _GIBBER_LISTENER_H_
#define _GIBBER_LISTENER_H_

#include <glib-object.h>

G_BEGIN_DECLS

GQuark gibber_listener_error_quark (void);
#define GIBBER_LISTENER_ERROR \
  gibber_listener_error_quark ()

typedef enum
{
  GIBBER_LISTENER_ERROR_ALREADY_LISTENING,
  GIBBER_LISTENER_ERROR_ADDRESS_IN_USE,
  GIBBER_LISTENER_ERROR_FAMILY_NOT_SUPPORTED,
  GIBBER_LISTENER_ERROR_FAILED,
} GibberListenerError;

typedef enum
{
  GIBBER_AF_IPV4,
  GIBBER_AF_IPV6,
  GIBBER_AF_ANY
} GibberAddressFamily;

typedef struct _GibberListener GibberListener;
typedef struct _GibberListenerClass GibberListenerClass;

struct _GibberListenerClass {
  GObjectClass parent_class;
};

struct _GibberListener {
  GObject parent;

  gpointer priv;
};

GType gibber_listener_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_LISTENER \
  (gibber_listener_get_type ())
#define GIBBER_LISTENER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_LISTENER,\
                              GibberListener))
#define GIBBER_LISTENER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_LISTENER,\
                           GibberListenerClass))
#define GIBBER_IS_LISTENER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_LISTENER))
#define GIBBER_IS_LISTENER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_LISTENER))
#define GIBBER_LISTENER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_LISTENER,\
                              GibberListenerClass))

GibberListener *gibber_listener_new (void);

gboolean gibber_listener_listen_tcp (GibberListener *listener,
  int port, GError **error);

gboolean gibber_listener_listen_tcp_af (GibberListener *listener,
  int port, GibberAddressFamily family, GError **error);

gboolean gibber_listener_listen_tcp_loopback (GibberListener *listener,
  int port, GError **error);

gboolean gibber_listener_listen_tcp_loopback_af (GibberListener *listener,
  int port, GibberAddressFamily family, GError **error);

gboolean gibber_listener_listen_socket (GibberListener *listener,
  gchar *path, gboolean abstract, GError **error);

int gibber_listener_get_port (GibberListener *listener);

G_END_DECLS

#endif /* #ifndef _GIBBER_LISTENER_H_ */
