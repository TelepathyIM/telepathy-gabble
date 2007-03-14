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
#include <telepathy-glib/enums.h>

G_BEGIN_DECLS

/**
 * TP_CONN_BUS_NAME_BASE:
 *
 * The prefix for a connection's bus name, to which the CM's name
 * (e.g. "gabble"), the protocol (e.g. "jabber") and an element or sequence
 * of elements representing the account should be appended.
 */
#define TP_CONN_BUS_NAME_BASE "org.freedesktop.Telepathy.Connection."

/**
 * TP_CONN_OBJECT_PATH_BASE:
 *
 * The prefix for a connection's object path, to which the CM's name
 * (e.g. "gabble"), the protocol (e.g. "jabber") and an element or sequence
 * of elements representing the account should be appended.
 */
#define TP_CONN_OBJECT_PATH_BASE "/org/freedesktop/Telepathy/Connection/"

typedef struct _TpBaseConnection TpBaseConnection;
typedef struct _TpBaseConnectionClass TpBaseConnectionClass;

struct _TpBaseConnectionClass {
    GObjectClass parent_class;

    /* pure-virtual methods */

    /** Fill in suitable handle repositories in the given array for all those
     * handle types this Connection supports. May not be NULL.
     */
    void (*init_handle_repos) (TpHandleRepoIface *[LAST_TP_HANDLE_TYPE+1]);

    /** Return a GPtrArray of channel factories which, between them,
     * implement all channel types this Connection supports. May not be NULL.
     */
    GPtrArray *(*create_channel_factories) (TpBaseConnection *self);

    /** Return a name for this connection which will be unique within this
     * connection manager process. If NULL, a unique name will be generated.
     */
    gchar *(*get_unique_connection_name) (TpBaseConnection *self);

    /** Called just after the state changes to CONNECTING. May be NULL if
     * nothing special needs to happen. */
    void (*connecting) (TpBaseConnection *self);
    /** Called just after the state changes to CONNECTED. May be NULL if
     * nothing special needs to happen. */
    void (*connected) (TpBaseConnection *self);
    /** Called just after the state changes to DISCONNECTED. May be NULL if
     * nothing special needs to happen. */
    void (*disconnected) (TpBaseConnection *self);

    /** Called after the state has changed to DISCONNECTED, and also after
     * disconnected(). Must start the shutdown process for the underlying
     * network connection, and arrange for tp_base_connection_finish_shutdown()
     * to be called after the underlying connection has been closed.
     *
     * May not be NULL.
     */
    void (*shut_down) (TpBaseConnection *self);

    /** Asynchronously start connecting. May assume that the connection
     * is in the NEW state. Must calculate and ref the self_handle.
     * After this runs, the state will be set to CONNECTING.
     */
    gboolean (*start_connecting) (TpBaseConnection *, GError **);
};

struct _TpBaseConnection {
    GObject parent;

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

    gpointer priv;
};

GType tp_base_connection_get_type(void);

gboolean tp_base_connection_register (TpBaseConnection *self,
    const gchar *cm_name, gchar **bus_name, gchar **object_path,
    GError **error);

void tp_base_connection_change_status (TpBaseConnection *self,
    TpConnectionStatus status, TpConnectionStatusReason reason);

void tp_base_connection_finish_shutdown (TpBaseConnection *self);

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
