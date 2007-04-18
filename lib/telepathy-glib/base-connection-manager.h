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

/**
 * TP_CM_BUS_NAME_BASE:
 *
 * The prefix for a connection manager's bus name, to which the CM's name
 * (e.g. "gabble") should be appended.
 */
#define TP_CM_BUS_NAME_BASE    "org.freedesktop.Telepathy.ConnectionManager."

/**
 * TP_CM_OBJECT_PATH_BASE:
 *
 * The prefix for a connection manager's object path, to which the CM's name
 * (e.g. "gabble") should be appended.
 */
#define TP_CM_OBJECT_PATH_BASE "/org/freedesktop/Telepathy/ConnectionManager/"

/**
 * TpCMParamSpec:
 * @name: Name as passed over D-Bus
 * @dtype: D-Bus type signature. We currently support 16- and 32-bit integers
 *         (@gtype is INT), 16- and 32-bit unsigned integers (gtype is UINT),
 *         strings (gtype is STRING) and booleans (gtype is BOOLEAN).
 * @gtype: GLib type, derived from @dtype as above
 * @flags: Some combination of TP_CONN_MGR_PARAM_FLAG_foo
 * @def: Default value, as a (const gchar *) for string parameters, or
         using #GINT_TO_POINTER or #GUINT_TO_POINTER for integer parameters
 * @offset: Offset of the parameter in the opaque data structure. The member
 *          at that offset is expected to be a gint, guint, (gchar *) or
 *          gboolean, depending on @gtype. Alternatively, this may be
 *          G_MAXSIZE, which means the parameter is obsolete, and is
 *          accepted but ignored.
 * @filter: A callback which is used to validate or normalize the user-provided
 *          value before it is written into the opaque data structure
 * @filter_data: Arbitrary opaque data intended for use by the filter function
 *
 * Structure representing a connection manager parameter, as accepted by
 * RequestConnection.
 *
 * In addition to the fields documented here, there are two gpointer fields
 * which must currently be %NULL. A meaning may be defined for these in a
 * future version of telepathy-glib.
 */

typedef struct _TpCMParamSpec TpCMParamSpec;

/**
 * TpCMParamFilter:
 * @paramspec: The parameter specification. The filter is likely to use
 *  name (for the error message if the value is invalid) and filter_data.
 * @value: The value for that parameter provided by the user.
 *  May be changed to contain a different value of the same type, if
 *  some sort of normalization is required
 * @error: Used to raise %TP_ERROR_INVALID_ARGUMENT if the given value is
 *  rejected
 *
 * Signature of a callback used to validate and/or normalize user-provided
 * CM parameter values.
 *
 * Returns: %TRUE to accept, %FALSE (with @error set) to reject
 */
typedef gboolean (*TpCMParamFilter) (const TpCMParamSpec *paramspec,
    GValue *value, GError **error);

/**
 * tp_cm_param_filter_string_nonempty:
 * @paramspec: The parameter specification for a string parameter
 * @value: A GValue containing a string, which will not be altered
 * @error: Used to return an error if the string is empty
 *
 * A #TpCMParamFilter which rejects empty strings.
 *
 * Returns: %TRUE to accept, %FALSE (with @error set) to reject
 */
gboolean tp_cm_param_filter_string_nonempty (const TpCMParamSpec *paramspec,
    GValue *value, GError **error);

/**
 * tp_cm_param_filter_uint_nonzero:
 * @paramspec: The parameter specification for a guint parameter
 * @value: A GValue containing a guint, which will not be altered
 * @error: Used to return an error if the guint is 0
 *
 * A #TpCMParamFilter which rejects zero, useful for server port numbers.
 *
 * Returns: %TRUE to accept, %FALSE (with @error set) to reject
 */
gboolean tp_cm_param_filter_uint_nonzero (const TpCMParamSpec *paramspec,
    GValue *value, GError **error);

/* XXX: This should be driven by GTypes, but the GType is insufficiently
 * descriptive: if it's UINT we can't tell whether the D-Bus type is
 * UInt32, UInt16 or possibly even Byte. So we have the D-Bus type too.
 *
 * As it stands at the moment it could be driven by the *D-Bus* type, but
 * in future we may want to have more than one possible GType for a D-Bus
 * type, e.g. converting arrays of string into either a strv or a GPtrArray.
 * So, we keep the redundancy for future expansion.
 */

struct _TpCMParamSpec {
    const gchar *name;
    const gchar *dtype;
    const GType gtype;
    guint flags;
    const gpointer def;
    const gsize offset;

    TpCMParamFilter filter;
    const gpointer filter_data;

    /*<private>*/
    gpointer _future1;
    gpointer _future2;
};

/**
 * TpCMProtocolSpec:
 * @name: The name which should be passed to RequestConnection for this
 *        protocol.
 * @parameters: An array of #TpCMParamSpec representing the valid parameters
 *              for this protocol, terminated by a #TpCMParamSpec whose name
 *              entry is NULL.
 * @params_new: A function which allocates an opaque data structure to store
 *              the parsed parameters for this protocol. The offset fields
 *              in the members of the @parameters array refer to offsets
 *              within this opaque structure.
 * @params_free: A function which deallocates the opaque data structure
 *               provided by #params_new, including deallocating its
 *               data members (currently, only strings) if necessary.
 *
 * Structure representing a connection manager protocol.
 *
 * In addition to the fields documented here, there are four gpointer fields
 * which must currently be %NULL. A meaning may be defined for these in a
 * future version of telepathy-glib.
 */
typedef struct {
    const gchar *name;
    const TpCMParamSpec *parameters;
    gpointer (*params_new) (void);
    void (*params_free) (gpointer);

    /*<private>*/
    gpointer _future1;
    gpointer _future2;
    gpointer _future3;
    gpointer _future4;
} TpCMProtocolSpec;

/**
 * TpBaseConnectionManager:
 * @parent: The parent instance structure
 * @priv: Pointer to opaque private data
 *
 * A base class for connection managers. There are no interesting public
 * fields in the instance structure.
 */
typedef struct _TpBaseConnectionManager TpBaseConnectionManager;

/**
 * TpBaseConnectionManagerClass:
 * @parent_class: The parent class
 * @cm_dbus_name: The name of this connection manager, as used to construct
 *  D-Bus object paths and bus names. Must contain only letters, digits
 *  and underscores, and may not start with a digit. Must be filled in by
 *  subclasses in their class_init function.
 * @protocol_params: An array of #TpCMProtocolSpec structures representing
 *  the protocols this connection manager supports, terminated by a structure
 *  whose name member is %NULL.
 * @new_connection: A #TpBaseConnectionManagerNewConnFunc used to construct
 *  new connections. Must be filled in by subclasses in their class_init
 *  function.
 *
 * The class structure for #TpBaseConnectionManager.
 *
 * In addition to the fields documented here, there are four gpointer fields
 * which must currently be %NULL (a meaning may be defined for these in a
 * future version of telepathy-glib), and a pointer to opaque private data.
 */
typedef struct _TpBaseConnectionManagerClass TpBaseConnectionManagerClass;

/**
 * TpBaseConnectionManagerNewConnFunc:
 * @self: The connection manager implementation
 * @proto: The protocol name from the D-Bus request
 * @params_present: A set of integers representing the indexes into the
 *                  array of #TpCMParamSpec of those parameters that were
 *                  present in the request
 * @parsed_params: An opaque data structure as returned by the protocol's
 *                 params_new function, populated according to the
 *                 parameter specifications
 * @error: if not %NULL, used to indicate the error if %NULL is returned
 *
 * A function that will return a new connection according to the
 * parsed parameters; used to implement RequestConnection.
 *
 * The connection manager base class will register the bus name for the
 * new connection, and place a reference to it in its table of
 * connections until the connection's shutdown process finishes.
 *
 * Returns: the new connection object, or %NULL on error.
 */
typedef TpBaseConnection *(*TpBaseConnectionManagerNewConnFunc)(
    TpBaseConnectionManager *self, const gchar *proto,
    TpIntSet *params_present, void *parsed_params, GError **error);

struct _TpBaseConnectionManagerClass {
    GObjectClass parent_class;

    const char *cm_dbus_name;
    const TpCMProtocolSpec *protocol_params;
    TpBaseConnectionManagerNewConnFunc new_connection;

    /*<private>*/
    gpointer _future1;
    gpointer _future2;
    gpointer _future3;
    gpointer _future4;

    gpointer priv;
};

struct _TpBaseConnectionManager {
    /*<private>*/
    GObject parent;

    gpointer priv;
};

GType tp_base_connection_manager_get_type (void);

/**
 * tp_base_connection_manager_register:
 * @self: The connection manager implementation
 *
 * Register the connection manager with an appropriate object path as
 * determined from its @cm_dbus_name, and register the appropriate well-known
 * bus name.
 *
 * Returns: %TRUE on success, %FALSE (having emitted a warning to stderr)
 *          on failure
 */
gboolean tp_base_connection_manager_register (TpBaseConnectionManager *self);

/* TYPE MACROS */
#define TP_TYPE_BASE_CONNECTION_MANAGER \
  (tp_base_connection_manager_get_type ())
#define TP_BASE_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TP_TYPE_BASE_CONNECTION_MANAGER, \
                              TpBaseConnectionManager))
#define TP_BASE_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TP_TYPE_BASE_CONNECTION_MANAGER, \
                           TpBaseConnectionManagerClass))
#define TP_IS_BASE_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TP_TYPE_BASE_CONNECTION_MANAGER))
#define TP_IS_BASE_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TP_TYPE_BASE_CONNECTION_MANAGER))
#define TP_BASE_CONNECTION_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_BASE_CONNECTION_MANAGER, \
                              TpBaseConnectionManagerClass))

G_END_DECLS

#endif /* #ifndef __TP_BASE_CONNECTION_MANAGER_H__*/
