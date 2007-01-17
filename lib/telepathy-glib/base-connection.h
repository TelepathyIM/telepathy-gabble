/*
 * base-connection.h - Header for TpBaseConnection
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

#ifndef __TP_BASE_CONNECTION_H__
#define __TP_BASE_CONNECTION_H__

#include <dbus/dbus-glib.h>
#include <glib-object.h>

#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/properties-mixin.h>
#include <telepathy-glib/enums.h>

G_BEGIN_DECLS

typedef struct _TpBaseConnection TpBaseConnection;
typedef struct _TpBaseConnectionClass TpBaseConnectionClass;

struct _TpBaseConnectionClass {
    GObjectClass parent_class;

    TpPropertiesMixinClass properties_class;

    /* pure-virtual methods */
    void (*init_handle_repos) (TpHandleRepoIface *[LAST_TP_HANDLE_TYPE+1]);
    GPtrArray *(*create_channel_factories) (TpBaseConnection *self);
    gchar *(*get_unique_connection_name) (TpBaseConnection *self);
    gchar *(*get_protocol) (TpBaseConnection *self);
};

struct _TpBaseConnection {
    GObject parent;

    TpPropertiesMixin properties;

    /* dbus object location */
    gchar *bus_name;
    gchar *object_path;

#   define TP_INTERNAL_CONNECTION_STATUS_NEW ((TpConnectionStatus)(LAST_TP_CONNECTION_STATUS + 1))
    /* connection status - may either be a TpConnectionStatus or
     * TP_INTERNAL_CONNECTION_STATUS_NEW */
    TpConnectionStatus status;

    /* handles */
    TpHandleRepoIface *handles[LAST_TP_HANDLE_TYPE + 1];
    TpHandle self_handle;

    /* currently unused */
    gpointer priv;
};

GType tp_base_connection_get_type(void);

gboolean tp_base_connection_register (TpBaseConnection *self,
    const gchar *cm_name, gchar **bus_name, gchar **object_path,
    GError **error);

void tp_base_connection_close_all_channels (TpBaseConnection *self);
void tp_base_connection_disconnected (TpBaseConnection *self);
void tp_base_connection_connecting (TpBaseConnection *self);
void tp_base_connection_connected (TpBaseConnection *self);
gboolean tp_base_connection_get_status (TpBaseConnection *self,
    guint *ret, GError **error);
void tp_base_connection_hold_handles (TpBaseConnection *self,
    guint handle_type, const GArray *handles, DBusGMethodInvocation *context);
void tp_base_connection_inspect_handles (TpBaseConnection *self,
    guint handle_type, const GArray *handles, DBusGMethodInvocation *context);
gboolean tp_base_connection_list_channels (TpBaseConnection *self,
    GPtrArray **ret, GError **error);
void tp_base_connection_release_handles (TpBaseConnection *self,
    guint handle_type, const GArray *handles, DBusGMethodInvocation *context);
void tp_base_connection_request_channel (TpBaseConnection *self,
    const gchar *type, guint handle_type, guint handle,
    gboolean suppress_handler, DBusGMethodInvocation *context);
void tp_base_connection_release_handles (TpBaseConnection *self,
    guint handle_type, const GArray *handles, DBusGMethodInvocation *context);

/* TYPE MACROS */
#define TP_TYPE_BASE_CONNECTION \
  (tp_base_connection_get_type())
#define TP_BASE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TP_TYPE_BASE_CONNECTION, TpBaseConnection))
#define TP_BASE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TP_TYPE_BASE_CONNECTION, TpBaseConnectionClass))
#define TP_IS_BASE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TP_TYPE_BASE_CONNECTION))
#define TP_IS_BASE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TP_TYPE_BASE_CONNECTION))
#define TP_BASE_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_BASE_CONNECTION, TpBaseConnectionClass))

G_END_DECLS

#endif /* #ifndef __TP_BASE_CONNECTION_H__*/
