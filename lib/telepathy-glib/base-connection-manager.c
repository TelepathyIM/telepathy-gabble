/*
 * base-connection-manager.c - Source for TpBaseConnectionManager
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

/**
 * SECTION:base-connection-manager
 * @title: TpBaseConnectionManager
 * @short_description: base class for #TpSvcConnectionManager implementations
 * @see_also: #TpBaseConnection, #TpSvcConnectionManager, #run
 *
 * This base class makes it easier to write #TpSvcConnectionManager
 * implementations by managing the D-Bus object path and bus name,
 * and maintaining a table of active connections. Subclasses should usually
 * only need to override the members of the class data structure.
 */

#include <telepathy-glib/base-connection-manager.h>

#include <string.h>

#include <dbus/dbus-protocol.h>

#include <telepathy-glib/dbus.h>
#define DEBUG_FLAG TP_DEBUG_PARAMS
#include "internal-debug.h"

static void service_iface_init (gpointer, gpointer);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE(TpBaseConnectionManager,
    tp_base_connection_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_CONNECTION_MANAGER,
        service_iface_init))

#define TP_BASE_CONNECTION_MANAGER_GET_PRIVATE(obj) \
    ((TpBaseConnectionManagerPrivate *)obj->priv)

typedef struct _TpBaseConnectionManagerPrivate
{
  /* if TRUE, the object has gone away */
  gboolean dispose_has_run;
  /* used as a set: key is TpBaseConnection *, value is TRUE */
  GHashTable *connections;
} TpBaseConnectionManagerPrivate;

/* signal enum */
enum
{
    NO_MORE_CONNECTIONS,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

#define TP_TYPE_PARAM (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_VALUE, \
      G_TYPE_INVALID))

static void
tp_base_connection_manager_dispose (GObject *object)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (object);
  TpBaseConnectionManagerPrivate *priv =
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
}

static void
tp_base_connection_manager_finalize (GObject *object)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (object);
  TpBaseConnectionManagerPrivate *priv =
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_hash_table_destroy (priv->connections);

  G_OBJECT_CLASS (tp_base_connection_manager_parent_class)->finalize (object);
}

static void
tp_base_connection_manager_class_init (TpBaseConnectionManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (TpBaseConnectionManagerPrivate));
  object_class->dispose = tp_base_connection_manager_dispose;
  object_class->finalize = tp_base_connection_manager_finalize;

  /**
   * TpBaseConnectionManager::no-more-connections:
   *
   * Emitted when the table of active connections becomes empty.
   * tp_run_connection_manager() uses this to detect when to shut down the
   * connection manager.
   */
  signals[NO_MORE_CONNECTIONS] =
    g_signal_new ("no-more-connections",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
tp_base_connection_manager_init (TpBaseConnectionManager *self)
{
  TpBaseConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      TP_TYPE_BASE_CONNECTION_MANAGER, TpBaseConnectionManagerPrivate);
  TpBaseConnectionManagerClass *cls =
    TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);

  (void)cls;

  self->priv = priv;

  priv->connections = g_hash_table_new (g_direct_hash, g_direct_equal);
}

/**
 * connection_shutdown_finished_cb:
 * @conn: #GabbleConnection
 * @data: data passed in callback
 *
 * Signal handler called when a connection object disconnects.
 * When they become disconnected, we can unref and discard
 * them, and they will disappear from the bus.
 */
static void
connection_shutdown_finished_cb (TpBaseConnection *conn,
                                 gpointer data)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (data);
  TpBaseConnectionManagerPrivate *priv =
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);

  g_assert (g_hash_table_lookup (priv->connections, conn));
  g_hash_table_remove (priv->connections, conn);

  g_object_unref (conn);

  g_debug ("%s: dereferenced connection", G_STRFUNC);
  if (g_hash_table_size (priv->connections) == 0)
    {
      g_signal_emit (self, signals[NO_MORE_CONNECTIONS], 0);
    }
}

/* Parameter parsing */

static gboolean
get_parameters (const TpCMProtocolSpec *protos,
                const char *proto,
                const TpCMProtocolSpec **ret,
                GError **error)
{
  guint i;

  for (i = 0; protos[i].name; i++)
    {
      if (!strcmp (proto, protos[i].name))
        {
          *ret = protos + i;
          return TRUE;
        }
    }

  DEBUG ("unknown protocol %s", proto);

  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
      "unknown protocol %s", proto);

  return FALSE;
}

static GValue *
param_default_value (const TpCMParamSpec *params, int i)
{
  GValue *value;

  value = g_slice_new0 (GValue);
  g_value_init (value, params[i].gtype);

  /* If HAS_DEFAULT is false, we don't really care what the value is, so we'll
   * just use whatever's in the user-supplied param spec. As long as we're
   * careful to accept NULL, that should be fine. */

  switch (params[i].dtype[0])
    {
      case DBUS_TYPE_STRING:
        g_assert (params[i].gtype == G_TYPE_STRING);
        if (params[i].def == NULL)
          g_value_set_static_string (value, "");
        else
          g_value_set_static_string (value, (const gchar *) params[i].def);
        break;
      case DBUS_TYPE_INT16:
      case DBUS_TYPE_INT32:
        g_assert (params[i].gtype == G_TYPE_INT);
        g_value_set_int (value, GPOINTER_TO_INT (params[i].def));
        break;
      case DBUS_TYPE_UINT16:
      case DBUS_TYPE_UINT32:
        g_assert (params[i].gtype == G_TYPE_UINT);
        g_value_set_uint (value, GPOINTER_TO_UINT (params[i].def));
        break;
      case DBUS_TYPE_BOOLEAN:
        g_assert (params[i].gtype == G_TYPE_BOOLEAN);
        g_value_set_boolean (value, GPOINTER_TO_INT (params[i].def));
        break;
      default:
        g_error ("parameter_defaults: encountered unknown type %s on "
            "argument %s", params[i].dtype, params[i].name);
    }

  return value;
}

static void
set_param_from_default (const TpCMParamSpec *paramspec,
                        gpointer params)
{
  switch (paramspec->dtype[0])
    {
    case DBUS_TYPE_STRING:
      {
        gchar **save_to = (gchar **) (params + paramspec->offset);
        g_assert (paramspec->gtype == G_TYPE_STRING);
        g_assert (paramspec->def != NULL);

        *save_to = g_strdup ((const gchar *) (paramspec->def));
        DEBUG ("%s = \"%s\"", paramspec->name, *save_to);
      }
      break;
    case DBUS_TYPE_INT16:
    case DBUS_TYPE_INT32:
      {
        gint *save_to = (gint *) (params + paramspec->offset);
        g_assert (paramspec->gtype == G_TYPE_INT);

        *save_to = GPOINTER_TO_INT (paramspec->def);
        DEBUG ("%s = %d = 0x%x", paramspec->name, *save_to, *save_to);
      }
      break;
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_UINT32:
      {
        guint *save_to = (guint *) (params + paramspec->offset);
        g_assert (paramspec->gtype == G_TYPE_UINT);

        *save_to = GPOINTER_TO_UINT (paramspec->def);
        DEBUG ("%s = %u = 0x%x", paramspec->name, *save_to, *save_to);
      }
      break;
    case DBUS_TYPE_BOOLEAN:
      {
        gboolean *save_to = (gboolean *) (params + paramspec->offset);
        g_assert (paramspec->gtype == G_TYPE_BOOLEAN);
        g_assert (paramspec->def == GINT_TO_POINTER (TRUE) || paramspec->def == GINT_TO_POINTER (FALSE));

        *save_to = GPOINTER_TO_INT (paramspec->def);
        DEBUG ("%s = %s", paramspec->name, *save_to ? "TRUE" : "FALSE");
      }
      break;
    default:
      g_error ("%s: encountered unhandled D-Bus type %s "
               "on argument %s", G_STRFUNC, paramspec->dtype, paramspec->name);
      g_assert_not_reached ();
    }
}

static gboolean
set_param_from_value (const TpCMParamSpec *paramspec,
                      GValue *value,
                      void *params,
                      GError **error)
{
  if (G_VALUE_TYPE (value) != paramspec->gtype)
    {
      DEBUG ("expected type %s for parameter %s, got %s",
               g_type_name (paramspec->gtype), paramspec->name,
               G_VALUE_TYPE_NAME (value));
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "expected type %s for account parameter %s, got %s",
          g_type_name (paramspec->gtype), paramspec->name,
          G_VALUE_TYPE_NAME (value));
      return FALSE;
    }

  if (paramspec->filter != NULL)
    {
      if (!(paramspec->filter) (paramspec, value, error))
        {
          DEBUG ("parameter %s rejected by filter function: %s",
              paramspec->name, error ? (*error)->message : "(error ignored)");
          return FALSE;
        }

      /* the filter may not change the type of the GValue */
      g_return_val_if_fail (G_VALUE_TYPE (value) == paramspec->gtype, FALSE);
    }

  switch (paramspec->dtype[0])
    {
      case DBUS_TYPE_STRING:
        {
          gchar **save_to = (gchar **) (params + paramspec->offset);
          const gchar *str;

          g_assert (paramspec->gtype == G_TYPE_STRING);
          str = g_value_get_string (value);
          g_free (*save_to);
          if (str == NULL)
            {
              *save_to = g_strdup ("");
            }
          else
            {
              *save_to = g_value_dup_string (value);
            }
          if (DEBUGGING)
            {
              if (strstr (paramspec->name, "password") != NULL)
                DEBUG ("%s = <hidden>", paramspec->name);
              else
                DEBUG ("%s = \"%s\"", paramspec->name, *save_to);
            }
        }
        break;
      case DBUS_TYPE_INT16:
      case DBUS_TYPE_INT32:
        {
          gint i = g_value_get_int (value);

          g_assert (paramspec->gtype == G_TYPE_INT);
          *((gint *) (params + paramspec->offset)) = i;
          DEBUG ("%s = %d = 0x%x", paramspec->name, i, i);
        }
        break;
      case DBUS_TYPE_UINT16:
      case DBUS_TYPE_UINT32:
        {
          guint i = g_value_get_uint (value);

          g_assert (paramspec->gtype == G_TYPE_UINT);
          *((guint *) (params + paramspec->offset)) = i;
          DEBUG ("%s = %u = 0x%x", paramspec->name, i, i);
        }
        break;
      case DBUS_TYPE_BOOLEAN:
        {
          gboolean b = g_value_get_boolean (value);

          g_assert (paramspec->gtype == G_TYPE_BOOLEAN);
          *((gboolean *) (params + paramspec->offset)) = b;
          DEBUG ("%s = %s", paramspec->name, b ? "TRUE" : "FALSE");
        }
        break;
      default:
        g_error ("set_param_from_value: encountered unhandled D-Bus type %s "
                 "on argument %s", paramspec->dtype, paramspec->name);
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
            "encountered unhandled D-Bus type %s for account parameter %s",
            paramspec->dtype, paramspec->name);
        return FALSE;
    }

  return TRUE;
}

static void
report_unknown_param (gpointer key, gpointer value, gpointer user_data)
{
  const char *arg = (const char *) key;
  GString **error_str = (GString **) user_data;
  *error_str = g_string_append_c (*error_str, ' ');
  *error_str = g_string_append (*error_str, arg);
}

static gboolean
parse_parameters (const TpCMParamSpec *paramspec,
                  GHashTable *provided,
                  TpIntSet *params_present,
                  void *params,
                  GError **error)
{
  int i;
  guint mandatory_flag = TP_CONN_MGR_PARAM_FLAG_REQUIRED;
  GValue *value;

  value = g_hash_table_lookup (provided, "register");
  if (value != NULL && G_VALUE_TYPE(value) == G_TYPE_BOOLEAN &&
      g_value_get_boolean (value))
    {
      mandatory_flag = TP_CONN_MGR_PARAM_FLAG_REGISTER;
    }

  for (i = 0; paramspec[i].name; i++)
    {
      if (paramspec->offset == G_MAXSIZE)
        {
          /* quietly ignore any obsolete params provided */
          g_hash_table_remove (provided, paramspec[i].name);
          continue;
        }

      value = g_hash_table_lookup (provided, paramspec[i].name);

      if (value == NULL)
        {
          if (paramspec[i].flags & mandatory_flag)
            {
              DEBUG ("missing mandatory param %s", paramspec[i].name);
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "missing mandatory account parameter %s", paramspec[i].name);
              return FALSE;
            }
          else if (paramspec[i].flags & TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT)
            {
              /* FIXME: Should we add it to params_present? */
              set_param_from_default (&paramspec[i], params);
            }
          else
            {
              DEBUG ("%s not given, using default behaviour",
                  paramspec[i].name);
            }
        }
      else
        {
          if (!set_param_from_value (&paramspec[i], value, params, error))
            {
              return FALSE;
            }

          tp_intset_add (params_present, i);

          g_hash_table_remove (provided, paramspec[i].name);
        }
    }

  if (g_hash_table_size (provided) != 0)
    {
      gchar *error_txt;
      GString *error_str = g_string_new ("unknown parameters provided:");

      g_hash_table_foreach (provided, report_unknown_param, &error_str);
      error_txt = g_string_free (error_str, FALSE);

      DEBUG ("%s", error_txt);
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          error_txt);
      g_free (error_txt);
      return FALSE;
    }

  return TRUE;
}


/**
 * tp_base_connection_manager_get_parameters
 *
 * Implements D-Bus method GetParameters
 * on interface org.freedesktop.Telepathy.ConnectionManager
 */
static void
tp_base_connection_manager_get_parameters (TpSvcConnectionManager *iface,
                                           const gchar *proto,
                                           DBusGMethodInvocation *context)
{
  GPtrArray *ret;
  GError *error = NULL;
  const TpCMProtocolSpec *protospec = NULL;
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (iface);
  TpBaseConnectionManagerClass *cls =
    TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);
  int i;

  g_assert (TP_IS_BASE_CONNECTION_MANAGER (iface));
  g_assert (cls->protocol_params != NULL);

  if (!get_parameters (cls->protocol_params, proto, &protospec, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = g_ptr_array_new ();

  for (i = 0; protospec->parameters[i].name; i++)
    {
      GValue *def_value;
      GValue param = { 0, };

      g_value_init (&param, TP_TYPE_PARAM);
      g_value_set_static_boxed (&param,
        dbus_g_type_specialized_construct (TP_TYPE_PARAM));

      def_value = param_default_value (protospec->parameters, i);
      dbus_g_type_struct_set (&param,
        0, protospec->parameters[i].name,
        1, protospec->parameters[i].flags,
        2, protospec->parameters[i].dtype,
        3, def_value,
        G_MAXUINT);
      g_value_unset (def_value);
      g_slice_free (GValue, def_value);

      g_ptr_array_add (ret, g_value_get_boxed (&param));
    }

  tp_svc_connection_manager_return_from_get_parameters (context, ret);
  g_ptr_array_free (ret, TRUE);
}


/**
 * tp_base_connection_manager_list_protocols
 *
 * Implements D-Bus method ListProtocols
 * on interface org.freedesktop.Telepathy.ConnectionManager
 */
static void
tp_base_connection_manager_list_protocols (TpSvcConnectionManager *iface,
                                           DBusGMethodInvocation *context)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (iface);
  TpBaseConnectionManagerClass *cls =
    TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);
  const char **protocols;
  guint i = 0;

  while (cls->protocol_params[i].name)
    i++;

  protocols = g_new0 (const char *, i + 1);
  for (i = 0; cls->protocol_params[i].name; i++)
    {
      protocols[i] = cls->protocol_params[i].name;
    }
  g_assert (protocols[i] == NULL);

  tp_svc_connection_manager_return_from_list_protocols (
      context, protocols);
  g_free (protocols);
}


/**
 * tp_base_connection_manager_request_connection
 *
 * Implements D-Bus method RequestConnection
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
tp_base_connection_manager_request_connection (TpSvcConnectionManager *iface,
                                               const gchar *proto,
                                               GHashTable *parameters,
                                               DBusGMethodInvocation *context)
{
  TpBaseConnectionManager *self = TP_BASE_CONNECTION_MANAGER (iface);
  TpBaseConnectionManagerClass *cls =
    TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);
  TpBaseConnectionManagerPrivate *priv =
    TP_BASE_CONNECTION_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *conn;
  gchar *bus_name;
  gchar *object_path;
  GError *error = NULL;
  void *params = NULL;
  TpIntSet *params_present = NULL;
  const TpCMProtocolSpec *protospec = NULL;

  g_assert (TP_IS_BASE_CONNECTION_MANAGER (iface));
  g_assert (cls->new_connection != NULL);
  g_assert (cls->cm_dbus_name != NULL);
  g_assert (cls->protocol_params != NULL);

  if (!get_parameters (cls->protocol_params, proto, &protospec, &error))
    {
      goto ERROR;
    }

  g_assert (protospec->params_new != NULL);
  g_assert (protospec->params_free != NULL);

  params_present = tp_intset_new ();
  params = protospec->params_new ();

  if (!parse_parameters (protospec->parameters, parameters, params_present,
        params, &error))
    {
      goto ERROR;
    }

  conn = (cls->new_connection)(self, proto, params_present, params, &error);
  if (!conn)
    {
      goto ERROR;
    }

  /* register on bus and save bus name and object path */
  if (!tp_base_connection_register ((TpBaseConnection *)conn,
        cls->cm_dbus_name, &bus_name, &object_path, &error))
    {
      g_debug ("%s failed: %s", G_STRFUNC, error->message);

      g_object_unref (G_OBJECT (conn));
      goto ERROR;
    }

  /* bind to status change signals from the connection object */
  g_signal_connect (conn, "shutdown-finished",
                    G_CALLBACK (connection_shutdown_finished_cb),
                    self);

  /* store the connection, using a hash table as a set */
  g_hash_table_insert (priv->connections, conn, GINT_TO_POINTER(TRUE));

  /* emit the new connection signal */
  tp_svc_connection_manager_emit_new_connection (
      self, bus_name, object_path, proto);

  tp_svc_connection_manager_return_from_request_connection (
      context, bus_name, object_path);

  g_free (bus_name);
  g_free (object_path);
  goto OUT;

ERROR:
  dbus_g_method_return_error (context, error);
  g_error_free (error);

OUT:
  if (params_present)
    tp_intset_destroy (params_present);
  if (params)
    protospec->params_free (params);
}

gboolean
tp_base_connection_manager_register (TpBaseConnectionManager *self)
{
  DBusGConnection *bus;
  DBusGProxy *bus_proxy;
  GError *error = NULL;
  guint request_name_result;
  TpBaseConnectionManagerClass *cls;
  GString *string;

  g_assert (TP_IS_BASE_CONNECTION_MANAGER (self));
  cls = TP_BASE_CONNECTION_MANAGER_GET_CLASS (self);
  g_assert (cls->cm_dbus_name);

  bus = tp_get_bus ();
  bus_proxy = tp_get_bus_proxy ();

  string = g_string_new (TP_CM_BUS_NAME_BASE);
  g_string_append (string, cls->cm_dbus_name);

  if (!dbus_g_proxy_call (bus_proxy, "RequestName", &error,
                          G_TYPE_STRING, string->str,
                          G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                          G_TYPE_INVALID,
                          G_TYPE_UINT, &request_name_result,
                          G_TYPE_INVALID))
    g_error ("Failed to request bus name: %s", error->message);

  if (request_name_result == DBUS_REQUEST_NAME_REPLY_EXISTS)
    {
      g_warning ("Failed to acquire bus name, connection manager already "
          "running?");

      g_string_free (string, TRUE);
      return FALSE;
    }

  g_string_assign (string, TP_CM_OBJECT_PATH_BASE);
  g_string_append (string, cls->cm_dbus_name);
  dbus_g_connection_register_g_object (bus, string->str, G_OBJECT (self));

  g_string_free (string, TRUE);

  return TRUE;
}

static void
service_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionManagerClass *klass = (TpSvcConnectionManagerClass *)g_iface;

#define IMPLEMENT(x) tp_svc_connection_manager_implement_##x (klass, \
    tp_base_connection_manager_##x)
  IMPLEMENT(get_parameters);
  IMPLEMENT(list_protocols);
  IMPLEMENT(request_connection);
#undef IMPLEMENT
}

gboolean
tp_cm_param_filter_uint_nonzero (const TpCMParamSpec *paramspec,
                                 GValue *value,
                                 GError **error)
{
  if (g_value_get_uint (value) == 0)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Account parameter '%s' may not be set to zero",
          paramspec->name);
      return FALSE;
    }
  return TRUE;
}

gboolean
tp_cm_param_filter_string_nonempty (const TpCMParamSpec *paramspec,
                                    GValue *value,
                                    GError **error)
{
  const gchar *str = g_value_get_string (value);

  if (str == NULL || str[0] == '\0')
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Account parameter '%s' may not be set to an empty string",
          paramspec->name);
      return FALSE;
    }
  return TRUE;
}
