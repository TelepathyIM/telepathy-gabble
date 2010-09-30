/*
 * server-tls-manager.h - Header for GabbleServerTLSManager
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
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

#ifndef __GABBLE_SERVER_TLS_MANAGER_H__
#define __GABBLE_SERVER_TLS_MANAGER_H__

#include <glib-object.h>
#include <wocky/wocky-tls-handler.h>

#include <telepathy-glib/enums.h>

#include "extensions/extensions.h"

G_BEGIN_DECLS

typedef struct _GabbleServerTLSManager GabbleServerTLSManager;
typedef struct _GabbleServerTLSManagerClass GabbleServerTLSManagerClass;
typedef struct _GabbleServerTLSManagerPrivate GabbleServerTLSManagerPrivate;

struct _GabbleServerTLSManagerClass {
  WockyTLSHandlerClass parent_class;
};

struct _GabbleServerTLSManager {
  WockyTLSHandler parent;
  GabbleServerTLSManagerPrivate *priv;
};

GType gabble_server_tls_manager_get_type (void);

#define GABBLE_TYPE_SERVER_TLS_MANAGER \
  (gabble_server_tls_manager_get_type ())
#define GABBLE_SERVER_TLS_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_SERVER_TLS_MANAGER, \
      GabbleServerTLSManager))
#define GABBLE_SERVER_TLS_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_SERVER_TLS_MANAGER, \
      GabbleServerTLSManagerClass))
#define GABBLE_IS_SERVER_TLS_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_SERVER_TLS_MANAGER))
#define GABBLE_IS_SERVER_TLS_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_SERVER_TLS_MANAGER))
#define GABBLE_SERVER_TLS_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_SERVER_TLS_MANAGER, \
      GabbleServerTLSManagerClass))

#define GABBLE_SERVER_TLS_ERROR gabble_server_tls_error_quark ()
GQuark gabble_server_tls_error_quark (void);

void gabble_server_tls_manager_get_rejection_details (
    GabbleServerTLSManager *self,
    gchar **dbus_error,
    GHashTable **details,
    TpConnectionStatusReason *reason);

G_END_DECLS

#endif /* #ifndef __GABBLE_SERVER_TLS_MANAGER_H__ */
