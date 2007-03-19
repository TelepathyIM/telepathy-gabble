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

/**
 * TpBaseConnectionProc:
 * @self: The connection object
 *
 * Signature of a virtual method on #TpBaseConnection that takes no
 * additional parameters and returns nothing.
 */
typedef void (*TpBaseConnectionProc) (TpBaseConnection *self);

/**
 * TpBaseConnectionProcWithError:
 * @self: The connection object
 * @error: Set to the error if %FALSE is returned
 *
 * Signature of a virtual method on #TpBaseConnection that takes no
 * additional parameters, but can fail and report an error.
 *
 * Returns: %TRUE on success
 */
typedef gboolean (*TpBaseConnectionProcWithError) (TpBaseConnection *self,
    GError **error);

/**
 * TpBaseConnectionCreateHandleReposImpl:
 * @self: The connection object
 * @repos: An array of pointers to be filled in; the implementation
 *         may assume all are initially NULL.
 *
 * Signature of an implementation of the create_handle_repos method
 * of #TpBaseConnection.
 */
typedef void (*TpBaseConnectionCreateHandleReposImpl) (TpBaseConnection *self,
    TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES]);

/** 
 * TpBaseConnectionCreateChannelFactoriesImpl:
 * @self: The implementation, a subclass of TpBaseConnection
 *
 * Signature of an implementation of the create_handle_repos method
 * of #TpBaseConnection.
 *
 * Returns: a GPtrArray of objects implementing #TpChannelFactoryIface
 * which, between them, implement all channel types this Connection
 * supports.
 */
typedef GPtrArray *(*TpBaseConnectionCreateChannelFactoriesImpl) (
    TpBaseConnection *self);

/** 
 * TpBaseConnectionGetUniqueConnectionNameImpl:
 * @self: The implementation, a subclass of TpBaseConnection
 *
 * Signature of the @get_unique_connection_name virtual method
 * on #TpBaseConnection.
 *
 * Returns: a name for this connection which will be unique within this
 * connection manager process, as a string which the caller must free
 * with #g_free.
 */
typedef gchar *(*TpBaseConnectionGetUniqueConnectionNameImpl) (
    TpBaseConnection *self);

/**
 * TpBaseConnectionClass:
 * @parent_class: The superclass' structure
 * @create_handle_repos: Fill in suitable handle repositories in the
 *  given array for all those handle types this Connection supports.
 *  May not be NULL, and the function must create at least a CONTACT
 *  handle repository (failing to do so may cause a crash).
 * @create_channel_factories: Create an array of channel factories for this
 *  Connection. May not be NULL.
 * @get_unique_connection_name: Construct a unique name for this connection
 *  (for example using the protocol's format for usernames). If NULL, a
 *  unique name will be generated.
 * @connecting: Called just after the state changes to CONNECTING. May be NULL
 *  if nothing special needs to happen.
 * @connected: Called just after the state changes to CONNECTED. May be NULL
 *  if nothing special needs to happen.
 * @disconnected: Called just after the state changes to DISCONNECTED. May be
 *  NULL if nothing special needs to happen.
 * @shut_down: Called after the state has changed to DISCONNECTED, and also
 *  after disconnected(). Must start the shutdown process for the underlying
 *  network connection, and arrange for tp_base_connection_finish_shutdown()
 *  to be called after the underlying connection has been closed. May not
 *  be left as NULL.
 * @start_connecting: Asynchronously start connecting. May assume that the
 *  connection is in the NEW state. Must calculate and ref the self_handle.
 *  After this runs, the state will be set to CONNECTING. If %TRUE is
 *  returned, the connection may still fail, signalled by a state change
 *  to DISCONNECTED.
 *
 * The class of a #TpBaseConnection.
 */
struct _TpBaseConnectionClass {
    GObjectClass parent_class;

    TpBaseConnectionCreateHandleReposImpl create_handle_repos;

    TpBaseConnectionCreateChannelFactoriesImpl create_channel_factories;

    TpBaseConnectionGetUniqueConnectionNameImpl get_unique_connection_name;

    TpBaseConnectionProc connecting;
    TpBaseConnectionProc connected;
    TpBaseConnectionProc disconnected;

    TpBaseConnectionProc shut_down;

    TpBaseConnectionProcWithError start_connecting;
};

/**
 * TpBaseConnection:
 * @bus_name: D-Bus well-known bus name, owned by the connection manager
 *  process and associated with this connection.
 * @object_path: The object-path of this connection.
 * @status: Connection status - may either be a valid TpConnectionStatus or
 *  TP_INTERNAL_CONNECTION_STATUS_NEW.
 * @self_handle: The handle of type %TP_HANDLE_TYPE_CONTACT representing the
 *  local user.
 * @priv: Pointer to opaque private data.
 *
 * Data structure representing a generic #TpSvcConnection implementation.
 */
struct _TpBaseConnection {
    GObject parent;

    gchar *bus_name;
    gchar *object_path;

    /**
     * TP_INTERNAL_CONNECTION_STATUS_NEW:
     *
     * A special value for #TpConnectionStatus, used internally to indicate
     * that the connection is disconnected because connection has never been
     * attempted (as distinct from disconnected after connection has started,
     * either by user request or an error).
     *
     * Must never be visible on the D-Bus - %TP_CONNECTION_STATUS_DISCONNECTED
     * is sent instead.
     */
#   define TP_INTERNAL_CONNECTION_STATUS_NEW \
    ((TpConnectionStatus)(-1))

    TpConnectionStatus status;

    TpHandle self_handle;

    gpointer priv;
};

GType tp_base_connection_get_type(void);

TpHandleRepoIface *tp_base_connection_get_handles (TpBaseConnection *self,
    TpHandleType handle_type);

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
