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

#include <telepathy-glib/enums.h>
#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/svc-connection.h>

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
 * TpBaseConnectionStartConnectingImpl:
 * @self: The connection object
 * @error: Set to the error if %FALSE is returned
 *
 * Signature of an implementation of the start_connecting method
 * of #TpBaseConnection.
 *
 * On entry, the implementation may assume that it is in state NEW.
 *
 * If %TRUE is returned, the Connect D-Bus method succeeds; the
 * implementation must either have already set the status to CONNECTED by
 * calling tp_base_connection_change_status(), or have arranged for a
 * status change to either state DISCONNECTED or CONNECTED to be signalled by
 * calling tp_base_connection_change_status() at some later time.
 * If the status is still NEW after returning %TRUE, #TpBaseConnection will
 * automatically change it to CONNECTING for reason REQUESTED.
 *
 * If %FALSE is returned, the error will be raised from Connect as an
 * exception. If the status is not DISCONNECTED after %FALSE is returned,
 * #TpBaseConnection will automatically change it to DISCONNECTED
 * with a reason appropriate to the error; NetworkError results in
 * NETWORK_ERROR, PermissionDenied results in AUTHENTICATION_FAILED, and all
 * other errors currently result in NONE_SPECIFIED.
 *
 * All except the simplest connection managers are expected to implement this
 * asynchronously, returning %TRUE in most cases and changing the status
 * to CONNECTED or DISCONNECTED later.
 *
 * Returns: %FALSE if failure has already occurred, else %TRUE.
 */
typedef gboolean (*TpBaseConnectionStartConnectingImpl) (
    TpBaseConnection *self, GError **error);

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
 *  Must be set by subclasses to a non-%NULL value; the function must create
 *  at least a CONTACT handle repository (failing to do so will cause a crash).
 * @create_channel_factories: Create an array of channel factories for this
 *  Connection. Must be set by subclasses to a non-%NULL value.
 * @get_unique_connection_name: Construct a unique name for this connection
 *  (for example using the protocol's format for usernames). If %NULL (the
 *  default), a unique name will be generated. Subclasses should usually
 *  override this to get more obvious names, to aid debugging and prevent
 *  multiple connections to the same account.
 * @connecting: If set by subclasses, will be called just after the state
 *  changes to CONNECTING. May be %NULL if nothing special needs to happen.
 * @connected: If set by subclasses, will be called just after the state
 *  changes to CONNECTED. May be %NULL if nothing special needs to happen.
 * @disconnected: If set by subclasses, will be called just after the state
 *  changes to DISCONNECTED. May be %NULL if nothing special needs to happen.
 * @shut_down: Called after disconnected() is called, to clean up the
 *  connection. Must start the shutdown process for the underlying
 *  network connection, and arrange for tp_base_connection_finish_shutdown()
 *  to be called after the underlying connection has been closed. May not
 *  be left as %NULL.
 * @start_connecting: Asynchronously start connecting - called to implement
 *  the Connect D-Bus method. See #TpBaseConnectionStartConnectingImpl for
 *  details.
 * @interfaces_always_present: A strv of extra D-Bus interfaces which are
 *  always implemented by instances of this class, which may be filled in
 *  by subclasses. The default is to list no additional interfaces.
 *  Individual instances may detect which additional interfaces they support
 *  and signal them before going to state CONNECTED by calling
 *  tp_base_connection_add_interfaces().
 *
 * The class of a #TpBaseConnection. Many members are virtual methods etc.
 * to be filled in in the subclass' class_init function.
 *
 * In addition to the fields documented here, there are four gpointer fields
 * which must currently be %NULL (a meaning may be defined for these in a
 * future version of telepathy-glib), and a pointer to opaque private data.
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

    TpBaseConnectionStartConnectingImpl start_connecting;

    const gchar **interfaces_always_present;

    /*<private>*/
    gpointer _future1;
    gpointer _future2;
    gpointer _future3;
    gpointer _future4;

    gpointer priv;
};

/**
 * TP_INTERNAL_CONNECTION_STATUS_NEW:
 *
 * A special value for #TpConnectionStatus, used within GLib connection
 * managers to indicate that the connection is disconnected because
 * connection has never been attempted (as distinct from disconnected
 * after connection has started, either by user request or an error).
 *
 * Must never be visible on the D-Bus - %TP_CONNECTION_STATUS_DISCONNECTED
 * is sent instead.
 */
#   define TP_INTERNAL_CONNECTION_STATUS_NEW ((TpConnectionStatus)(-1))

/**
 * TpBaseConnection:
 * @parent: Fields shared by the superclass.
 * @bus_name: A D-Bus well-known bus name, owned by the connection manager
 *  process and associated with this connection. Set by
 *  tp_base_connection_register; should be considered read-only by subclasses.
 * @object_path: The object-path of this connection. Set by
 *  tp_base_connection_register; should be considered read-only by subclasses.
 * @status: Connection status - may either be a valid TpConnectionStatus or
 *  TP_INTERNAL_CONNECTION_STATUS_NEW. Should be considered read-only by
 *  subclasses: use tp_base_connection_change_status() to set it.
 * @self_handle: The handle of type %TP_HANDLE_TYPE_CONTACT representing the
 *  local user. Must be set nonzero by the subclass before moving to state
 *  CONNECTED. If nonzero, the connection must hold a reference to the handle.
 *
 * Data structure representing a generic #TpSvcConnection implementation.
 *
 * In addition to the fields documented here, there are four gpointer fields
 * which must currently be %NULL (a meaning may be defined for these in a
 * future version of telepathy-glib), and a pointer to opaque private data.
 */
struct _TpBaseConnection {
    /*<public>*/
    GObject parent;

    gchar *bus_name;
    gchar *object_path;

    TpConnectionStatus status;

    TpHandle self_handle;

    /*<private>*/
    gpointer _future1;
    gpointer _future2;
    gpointer _future3;
    gpointer _future4;

    gpointer priv;
};

GType tp_base_connection_get_type (void);

TpHandleRepoIface *tp_base_connection_get_handles (TpBaseConnection *self,
    TpHandleType handle_type);

gboolean tp_base_connection_register (TpBaseConnection *self,
    const gchar *cm_name, gchar **bus_name, gchar **object_path,
    GError **error);

void tp_base_connection_change_status (TpBaseConnection *self,
    TpConnectionStatus status, TpConnectionStatusReason reason);

void tp_base_connection_finish_shutdown (TpBaseConnection *self);

void tp_base_connection_add_interfaces (TpBaseConnection *self,
    const gchar **interfaces);

void tp_base_connection_dbus_request_handles (TpSvcConnection *iface,
    guint handle_type, const gchar **names, DBusGMethodInvocation *context);

/* TYPE MACROS */
#define TP_TYPE_BASE_CONNECTION \
  (tp_base_connection_get_type ())
#define TP_BASE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TP_TYPE_BASE_CONNECTION, \
                              TpBaseConnection))
#define TP_BASE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TP_TYPE_BASE_CONNECTION, \
                           TpBaseConnectionClass))
#define TP_IS_BASE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TP_TYPE_BASE_CONNECTION))
#define TP_IS_BASE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TP_TYPE_BASE_CONNECTION))
#define TP_BASE_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_BASE_CONNECTION, \
                              TpBaseConnectionClass))

/**
 * TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED:
 * @conn: A TpBaseConnection
 * @context: A DBusGMethodInvocation
 *
 * If @conn is not in state #TP_CONNECTION_STATUS_CONNECTED, complete the
 * D-Bus method invocation @context by raising the Telepathy error
 * #TP_ERROR_DISCONNECTED, and return from the current function (which
 * must be void). For use in D-Bus method implementations.
 */
#define TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED(conn, context) \
  G_STMT_START { \
    TpBaseConnection *c = (conn); \
    \
    if (c->status != TP_CONNECTION_STATUS_CONNECTED) \
      { \
        GError e = { TP_ERRORS, TP_ERROR_DISCONNECTED, \
            "Connection is disconnected" }; \
        \
        DEBUG ("rejected request as disconnected"); \
        dbus_g_method_return_error ((context), &e); \
        return; \
      } \
  } G_STMT_END

G_END_DECLS

#endif /* #ifndef __TP_BASE_CONNECTION_H__*/
