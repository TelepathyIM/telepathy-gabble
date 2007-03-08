/*
 * conn-presence - Gabble connection presence interface
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2007 Nokia Corporation
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

#include <string.h>

#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/util.h>

#include "gabble-connection.h"
#include "presence.h"
#include "presence-cache.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "debug.h"


typedef struct _StatusInfo StatusInfo;

struct _StatusInfo
{
  const gchar *name;
  TpConnectionPresenceType presence_type;
  const gboolean self;
  const gboolean exclusive;
};

/* order must match PresenceId enum in gabble-connection.h */
/* in increasing order of presence */
static const StatusInfo gabble_statuses[LAST_GABBLE_PRESENCE] = {
 { "offline",   TP_CONNECTION_PRESENCE_TYPE_OFFLINE,       TRUE, TRUE },
 { "hidden",    TP_CONNECTION_PRESENCE_TYPE_HIDDEN,        TRUE, TRUE },
 { "xa",        TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY, TRUE, TRUE },
 { "away",      TP_CONNECTION_PRESENCE_TYPE_AWAY,          TRUE, TRUE },
 { "dnd",       TP_CONNECTION_PRESENCE_TYPE_AWAY,          TRUE, TRUE },
 { "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE,     TRUE, TRUE },
 { "chat",      TP_CONNECTION_PRESENCE_TYPE_AVAILABLE,     TRUE, TRUE }
};


static GHashTable *
construct_presence_hash (GabbleConnection *self,
                         const GArray *contact_handles)
{
  TpBaseConnection *base = (TpBaseConnection *)self;
  guint i;
  TpHandle handle;
  GHashTable *presence_hash, *contact_status, *parameters;
  GValueArray *vals;
  GValue *message;
  GabblePresence *presence;
  GabblePresenceId status;
  const gchar *status_message;
  /* this is never set at the moment*/
  guint timestamp = 0;
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  g_assert (tp_handles_are_valid (handle_repo, contact_handles, FALSE, NULL));

  presence_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_value_array_free);

  for (i = 0; i < contact_handles->len; i++)
    {
      handle = g_array_index (contact_handles, TpHandle, i);

      if (handle == base->self_handle)
        presence = self->self_presence;
      else
        presence = gabble_presence_cache_get (self->presence_cache, handle);

      if (presence)
        {
          status = presence->status;
          status_message = presence->status_message;
        }
      else
        {
          status = GABBLE_PRESENCE_OFFLINE;
          status_message = NULL;
        }

      parameters = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
          (GDestroyNotify) tp_g_value_slice_free);
      if (status_message != NULL) {
        message = g_slice_new0 (GValue);
        g_value_init (message, G_TYPE_STRING);
        g_value_set_static_string (message, status_message);


        g_hash_table_insert (parameters, "message", message);
      }

      contact_status = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
          (GDestroyNotify) g_hash_table_destroy);
      g_hash_table_insert (contact_status,
          (gchar *) gabble_statuses[status].name, parameters);

      vals = g_value_array_new (2);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (vals, 0), timestamp);

      g_value_array_append (vals, NULL);
      g_value_init (g_value_array_get_nth (vals, 1),
          dbus_g_type_get_map ("GHashTable", G_TYPE_STRING,
            dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE)));
      g_value_take_boxed (g_value_array_get_nth (vals, 1), contact_status);

      g_hash_table_insert (presence_hash, GINT_TO_POINTER (handle), vals);
    }

  return presence_hash;
}


/**
 * emit_presence_update:
 * @self: A #GabbleConnection
 * @contact_handles: A zero-terminated array of #TpHandle for
 *                    the contacts to emit presence for
 *
 * Emits the Telepathy PresenceUpdate signal with the current
 * stored presence information for the given contact.
 */
static void
emit_presence_update (GabbleConnection *self,
                      const GArray *contact_handles)
{
  GHashTable *presence_hash;

  presence_hash = construct_presence_hash (self, contact_handles);
  tp_svc_connection_interface_presence_emit_presence_update (self,
      presence_hash);
  g_hash_table_destroy (presence_hash);
}


/**
 * emit_one_presence_update:
 * Convenience function for calling emit_presence_update with one handle.
 */

static void
emit_one_presence_update (GabbleConnection *self,
                          TpHandle handle)
{
  GArray *handles = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);

  g_array_insert_val (handles, 0, handle);
  emit_presence_update (self, handles);
  g_array_free (handles, TRUE);
}




/**
 * gabble_connection_add_status
 *
 * Implements D-Bus method AddStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_add_status (TpSvcConnectionInterfacePresence *iface,
                              const gchar *status,
                              GHashTable *parms,
                              DBusGMethodInvocation *context)
{
  TpBaseConnection *self = TP_BASE_CONNECTION (iface);
  GError *error;
  GError unimpl = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
    "Only one status is possible at a time with this protocol" };

  ERROR_IF_NOT_CONNECTED_ASYNC (self, error, context);

  dbus_g_method_return_error (context, &unimpl);
}


/**
 * gabble_connection_clear_status
 *
 * Implements D-Bus method ClearStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_clear_status (TpSvcConnectionInterfacePresence *iface,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GError *error;
  gboolean ok;
  gchar *resource;
  gint8 priority;

  g_assert (GABBLE_IS_CONNECTION (self));

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context);

  g_object_get (self,
        "resource", &resource,
        "priority", &priority,
        NULL);
  gabble_presence_update (self->self_presence, resource,
      GABBLE_PRESENCE_AVAILABLE, NULL, priority);
  emit_one_presence_update (self, base->self_handle);
  ok = _gabble_connection_signal_own_presence (self, &error);

  if (ok)
    {
      tp_svc_connection_interface_presence_return_from_clear_status (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  g_free (resource);
}


/**
 * status_is_available
 *
 * Returns a boolean to indicate whether the given gabble status is
 * available on this connection.
 */
static gboolean
status_is_available (GabbleConnection *conn, int status)
{
  g_assert (GABBLE_IS_CONNECTION (conn));
  g_assert (status < LAST_GABBLE_PRESENCE);

  if (gabble_statuses[status].presence_type == TP_CONNECTION_PRESENCE_TYPE_HIDDEN &&
      (conn->features & GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE) == 0)
    return FALSE;
  else
    return TRUE;
}


/**
 * gabble_connection_get_presence
 *
 * Implements D-Bus method GetPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @context: The D-Bus invocation context to use to return values
 *           or throw an error.
 */
static void
gabble_connection_get_presence (TpSvcConnectionInterfacePresence *iface,
                                const GArray *contacts,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GHashTable *presence_hash;
  GError *error = NULL;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  presence_hash = construct_presence_hash (self, contacts);
  tp_svc_connection_interface_presence_return_from_get_presence (
      context, presence_hash);
  g_hash_table_destroy (presence_hash);
}


static GHashTable *
get_statuses_arguments ()
{
  static GHashTable *arguments = NULL;

  if (arguments == NULL)
    {
      arguments = g_hash_table_new (g_str_hash, g_str_equal);

      g_hash_table_insert (arguments, "message", "s");
      g_hash_table_insert (arguments, "priority", "n");
    }

  return arguments;
}


/**
 * gabble_connection_get_statuses
 *
 * Implements D-Bus method GetStatuses
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_get_statuses (TpSvcConnectionInterfacePresence *iface,
                                DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GHashTable *ret;
  GError *error;
  GValueArray *status;
  int i;

  g_assert (GABBLE_IS_CONNECTION (self));

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  DEBUG ("called.");

  ret = g_hash_table_new_full (g_str_hash, g_str_equal,
                               NULL, (GDestroyNotify) g_value_array_free);

  for (i=0; i < LAST_GABBLE_PRESENCE; i++)
    {
      /* don't report the invisible presence if the server
       * doesn't have the presence-invisible feature */
      if (!status_is_available (self, i))
        continue;

      status = g_value_array_new (5);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 0), G_TYPE_UINT);
      g_value_set_uint (g_value_array_get_nth (status, 0),
          gabble_statuses[i].presence_type);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 1), G_TYPE_BOOLEAN);
      g_value_set_boolean (g_value_array_get_nth (status, 1),
          gabble_statuses[i].self);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 2), G_TYPE_BOOLEAN);
      g_value_set_boolean (g_value_array_get_nth (status, 2),
          gabble_statuses[i].exclusive);

      g_value_array_append (status, NULL);
      g_value_init (g_value_array_get_nth (status, 3),
          DBUS_TYPE_G_STRING_STRING_HASHTABLE);
      g_value_set_static_boxed (g_value_array_get_nth (status, 3),
          get_statuses_arguments());

      g_hash_table_insert (ret, (gchar*) gabble_statuses[i].name, status);
    }

  tp_svc_connection_interface_presence_return_from_get_statuses (
      context, ret);
  g_hash_table_destroy (ret);
}


/**
 * gabble_connection_remove_status
 *
 * Implements D-Bus method RemoveStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_remove_status (TpSvcConnectionInterfacePresence *iface,
                                 const gchar *status,
                                 DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GabblePresence *presence = self->self_presence;
  GError *error;
  gchar *resource;
  gint8 priority;

  g_assert (GABBLE_IS_CONNECTION (self));

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  g_object_get (self,
      "resource", &resource,
      "priority", &priority,
      NULL);

  if (strcmp (status, gabble_statuses[presence->status].name) == 0)
    {
      gabble_presence_update (presence, resource,
          GABBLE_PRESENCE_AVAILABLE, NULL, priority);
      emit_one_presence_update (self, base->self_handle);
      if (_gabble_connection_signal_own_presence (self, &error))
        {
          tp_svc_connection_interface_presence_return_from_remove_status (
              context);
        }
      else
        {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
        }
    }
  else
    {
      GError nonexistent = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Attempting to remove non-existent presence." };
      dbus_g_method_return_error (context, &nonexistent);
    }

  g_free (resource);
}


/**
 * gabble_connection_request_presence
 *
 * Implements D-Bus method RequestPresence
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 */
static void
gabble_connection_request_presence (TpSvcConnectionInterfacePresence *iface,
                                    const GArray *contacts,
                                    DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GError *error;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  g_assert (GABBLE_IS_CONNECTION (self));

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  if (contacts->len)
    emit_presence_update (self, contacts);

  tp_svc_connection_interface_presence_return_from_request_presence (
      context);
}


/**
 * gabble_connection_set_last_activity_time
 *
 * Implements D-Bus method SetLastActivityTime
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 */
static void
gabble_connection_set_last_activity_time (TpSvcConnectionInterfacePresence *iface,
                                          guint time,
                                          DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  GError *error;

  g_assert (GABBLE_IS_CONNECTION (self));

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  tp_svc_connection_interface_presence_return_from_set_last_activity_time (
      context);
}


struct _i_hate_g_hash_table_foreach
{
  GabbleConnection *conn;
  GError **error;
  gboolean retval;
};


static void
setstatuses_foreach (gpointer key, gpointer value, gpointer user_data)
{
  struct _i_hate_g_hash_table_foreach *data =
    (struct _i_hate_g_hash_table_foreach*) user_data;
  TpBaseConnection *base = (TpBaseConnection *)data->conn;
  gchar *resource;
  int i;

  g_object_get (data->conn, "resource", &resource, NULL);

  for (i = 0; i < LAST_GABBLE_PRESENCE; i++)
    {
      if (0 == strcmp (gabble_statuses[i].name, (const gchar*) key))
        break;
    }

  if (i < LAST_GABBLE_PRESENCE)
    {
      GHashTable *args = (GHashTable *)value;
      GValue *message = g_hash_table_lookup (args, "message");
      GValue *priority = g_hash_table_lookup (args, "priority");
      const gchar *status = NULL;
      gint8 prio;

      g_object_get (data->conn, "priority", &prio, NULL);

      if (!status_is_available (data->conn, i))
        {
          DEBUG ("requested status %s is not available", (const gchar *) key);
          g_set_error (data->error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "requested status '%s' is not available on this connection",
              (const gchar *) key);
          data->retval = FALSE;
          goto OUT;
        }

      if (message)
        {
          if (!G_VALUE_HOLDS_STRING (message))
            {
              DEBUG ("got a status message which was not a string");
              g_set_error (data->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "Status argument 'message' requires a string");
              data->retval = FALSE;
              goto OUT;
            }
          status = g_value_get_string (message);
        }

      if (priority)
        {
          if (!G_VALUE_HOLDS_INT (priority))
            {
              DEBUG ("got a priority value which was not a signed integer");
              g_set_error (data->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "Status argument 'priority' requires a signed integer");
              data->retval = FALSE;
              goto OUT;
            }
          prio = CLAMP (g_value_get_int (priority), G_MININT8, G_MAXINT8);
        }

      gabble_presence_update (data->conn->self_presence, resource, i, status, prio);
      emit_one_presence_update (data->conn, base->self_handle);
      data->retval = _gabble_connection_signal_own_presence (data->conn, data->error);
    }
  else
    {
      DEBUG ("got unknown status identifier %s", (const gchar *) key);
      g_set_error (data->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "unknown status identifier: %s", (const gchar *) key);
      data->retval = FALSE;
    }

OUT:
  g_free (resource);
}

/**
 * gabble_connection_set_status
 *
 * Implements D-Bus method SetStatus
 * on interface org.freedesktop.Telepathy.Connection.Interface.Presence
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
gabble_connection_set_status (TpSvcConnectionInterfacePresence *iface,
                              GHashTable *statuses,
                              DBusGMethodInvocation *context)
{
  GabbleConnection *self = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  struct _i_hate_g_hash_table_foreach data = { NULL, NULL, TRUE };
  GError *error;

  g_assert (GABBLE_IS_CONNECTION (self));

  ERROR_IF_NOT_CONNECTED_ASYNC (base, error, context)

  if (g_hash_table_size (statuses) != 1)
    {
      GError invalid = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Only one status may be set at a time in this protocol" };
      DEBUG ("got more than one status");
      dbus_g_method_return_error (context, &invalid);
      return;
    }

  data.conn = self;
  data.error = &error;
  g_hash_table_foreach (statuses, setstatuses_foreach, &data);

  if (data.retval)
    {
      tp_svc_connection_interface_presence_return_from_set_status (
          context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}


static void
connection_presence_update_cb (
    GabblePresenceCache *cache,
    TpHandle handle,
    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);

  emit_one_presence_update (conn, handle);
}


static void
connection_status_changed_cb (
    GabbleConnection *conn,
    TpConnectionStatus status,
    TpConnectionStatusReason reason,
    gpointer user_data)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;

  if (status == TP_CONNECTION_STATUS_CONNECTED)
    emit_one_presence_update (conn, base->self_handle);
}


void
conn_presence_init (GabbleConnection *conn)
{
  g_signal_connect (conn->presence_cache, "presence-update",
      G_CALLBACK (connection_presence_update_cb), conn);
  g_signal_connect (conn, "status-changed",
      G_CALLBACK (connection_status_changed_cb), conn);
}


void
conn_presence_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfacePresenceClass *klass =
      (TpSvcConnectionInterfacePresenceClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_presence_implement_##x (\
    klass, gabble_connection_##x)
  IMPLEMENT(add_status);
  IMPLEMENT(clear_status);
  IMPLEMENT(get_presence);
  IMPLEMENT(get_statuses);
  IMPLEMENT(remove_status);
  IMPLEMENT(request_presence);
  IMPLEMENT(set_last_activity_time);
  IMPLEMENT(set_status);
#undef IMPLEMENT
}
