/*
 * base-connection-manager.h - Header for TpBaseConnectionManager
 *
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007 Nokia Corporation
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

#ifndef __TP_BASE_CONNECTION_MANAGER_H__
#define __TP_BASE_CONNECTION_MANAGER_H__

#include <dbus/dbus-glib.h>
#include <glib-object.h>
#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/svc-connection-manager.h>

G_BEGIN_DECLS

#define TP_CM_BUS_NAME_BASE    "org.freedesktop.Telepathy.ConnectionManager."
#define TP_CM_OBJECT_PATH_BASE "/org/freedesktop/Telepathy/ConnectionManager/"

typedef struct {
    /** Name as passed over D-Bus */
    const gchar *name;
    /** D-Bus type signature. We currently support:
     * 16-bit integer (gtype is INT)
     * 32-bit integer (gtype is INT)
     * 16-bit unsigned (gtype is UINT)
     * 32-bit unsigned (gtype is UINT)
     * string (gtype is STRING)
     * boolean (gtype is BOOLEAN)
     * */
    const gchar *dtype;
    /** GLib type, derived from dtype as above */
    const GType gtype;
    /** Some combination of TP_CONN_MGR_PARAM_FLAG_foo */
    guint flags;
    /** Default - gchar * or GINT_TO_POINTER */ 
    const gpointer def;
    /** Offset in structure, internal use only. GMAXSIZE means the parameter
     * is just thrown away. */
    const gsize offset;
} TpCMParamSpec;

typedef struct {
    const gchar *name;
    const TpCMParamSpec *parameters;       /* terminated by a NULL name */
    void *(*params_new) (void);
    void (*params_free) (void *);
} TpCMProtocolSpec;

typedef struct _TpBaseConnectionManager TpBaseConnectionManager;
typedef struct _TpBaseConnectionManagerClass TpBaseConnectionManagerClass;

struct _TpBaseConnectionManagerClass {
    GObjectClass parent_class;

    const char *cm_dbus_name;
    const TpCMProtocolSpec *protocol_params; /* terminated by a NULL name */

    /* pure-virtual */

    TpBaseConnection *(*new_connection)(TpBaseConnectionManager *self,
        const gchar *proto, TpIntSet *params_present, void *parsed_params,
        GError **error);
};

struct _TpBaseConnectionManager {
    GObject parent;

    gpointer priv;
};

GType tp_base_connection_manager_get_type(void);

gboolean tp_base_connection_manager_register (TpBaseConnectionManager *self);

/* TYPE MACROS */
#define TP_TYPE_BASE_CONNECTION_MANAGER \
  (tp_base_connection_manager_get_type())
#define TP_BASE_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TP_TYPE_BASE_CONNECTION_MANAGER, TpBaseConnectionManager))
#define TP_BASE_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TP_TYPE_BASE_CONNECTION_MANAGER, TpBaseConnectionManagerClass))
#define TP_IS_BASE_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TP_TYPE_BASE_CONNECTION_MANAGER))
#define TP_IS_BASE_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TP_TYPE_BASE_CONNECTION_MANAGER))
#define TP_BASE_CONNECTION_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_BASE_CONNECTION_MANAGER, TpBaseConnectionManagerClass))

G_END_DECLS

#endif /* #ifndef __TP_BASE_CONNECTION_MANAGER_H__*/
