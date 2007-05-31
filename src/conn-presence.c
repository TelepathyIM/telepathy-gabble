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

#include "conn-presence.h"

#include <string.h>

#include <telepathy-glib/presence-mixin.h>
#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/util.h>

#include "gabble-connection.h"
#include "presence.h"
#include "presence-cache.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION

#include "debug.h"


static const TpPresenceStatusOptionalArgumentSpec gabble_status_arguments[] = {
  { "message",  "s" },
  { "priority", "n" },
  { NULL, NULL }
};


/* order must match PresenceId enum in gabble-connection.h */
/* in increasing order of presence */
static const TpPresenceStatusSpec gabble_statuses[] = {
  { "offline", TP_CONNECTION_PRESENCE_TYPE_OFFLINE, TRUE,
    gabble_status_arguments, NULL, NULL },
  { "hidden", TP_CONNECTION_PRESENCE_TYPE_HIDDEN, TRUE, gabble_status_arguments,
    NULL, NULL },
  { "xa", TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY, TRUE,
    gabble_status_arguments, NULL, NULL },
  { "away", TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE, gabble_status_arguments,
    NULL, NULL },
  { "dnd", TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE, gabble_status_arguments,
    NULL, NULL },
  { "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, TRUE,
    gabble_status_arguments, NULL, NULL },
  { "chat", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, TRUE,
    gabble_status_arguments, NULL, NULL },
  { NULL, 0, FALSE, NULL, NULL, NULL }
};


static GHashTable *
construct_contact_statuses_cb (GObject *obj,
                               const GArray *contact_handles,
                               GError **unused)
{
  GabbleConnection *self = GABBLE_CONNECTION (obj);
  TpBaseConnection *base = (TpBaseConnection *)self;
  guint i;
  TpHandle handle;
  GHashTable *contact_statuses, *parameters;
  TpPresenceStatus *contact_status;
  GValue *message;
  GabblePresence *presence;
  GabblePresenceId status;
  const gchar *status_message;
  TpHandleRepoIface *handle_repo = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  g_assert (tp_handles_are_valid (handle_repo, contact_handles, FALSE, NULL));

  contact_statuses = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) tp_presence_status_free);

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

      contact_status = tp_presence_status_new (status, parameters);
      g_hash_table_unref (parameters);

      g_hash_table_insert (contact_statuses, GUINT_TO_POINTER (handle),
          contact_status);
    }

  return contact_statuses;
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
  GHashTable *contact_statuses;

  contact_statuses = construct_contact_statuses_cb ((GObject *) self,
      contact_handles, NULL);
  tp_presence_mixin_emit_presence_update ((GObject *) self, contact_statuses);
  g_hash_table_destroy (contact_statuses);
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


static gboolean
set_own_status_cb (GObject *obj,
                   const TpPresenceStatus *status,
                   GError **error)
{
  GabbleConnection *conn = GABBLE_CONNECTION (obj);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  gchar *resource;
  gint8 prio;
  gboolean retval = TRUE;

  g_object_get (conn,
        "resource", &resource,
        "priority", &prio,
        NULL);

  if (status)
    {
      GabblePresenceId i = status->index;
      GHashTable *args = status->optional_arguments;
      GValue *message = g_hash_table_lookup (args, "message");
      GValue *priority = g_hash_table_lookup (args, "priority");
      const gchar *status = NULL;

      if (message)
        {
          if (!G_VALUE_HOLDS_STRING (message))
            {
              DEBUG ("got a status message which was not a string");
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                  "Status argument 'message' requires a string");
              retval = FALSE;
              goto OUT;
            }
          status = g_value_get_string (message);
        }

      if (priority)
        {
          if (!G_VALUE_HOLDS_INT (priority))
            {
              DEBUG ("got a priority value which was not a signed integer");
              g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "Status argument 'priority' requires a signed integer");
              retval = FALSE;
              goto OUT;
            }
          prio = CLAMP (g_value_get_int (priority), G_MININT8, G_MAXINT8);
        }

      if (gabble_presence_update (conn->self_presence, resource, i,
            status, prio))
        {
          emit_one_presence_update (conn, base->self_handle);
          retval = _gabble_connection_signal_own_presence (conn,
              error);
        }
      else
        {
          /* Nothing actually changed. */
          retval = TRUE;
        }
    }
  else
    {
      gabble_presence_update (conn->self_presence, resource,
          GABBLE_PRESENCE_AVAILABLE, NULL, prio);
      emit_one_presence_update (conn, base->self_handle);
      return _gabble_connection_signal_own_presence (conn, error);
    }

OUT:

  g_free (resource);

  return retval;
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


static gboolean
status_available_cb (GObject *obj, guint status)
{
  GabbleConnection *conn = GABBLE_CONNECTION (obj);

  if (gabble_statuses[status].presence_type == TP_CONNECTION_PRESENCE_TYPE_HIDDEN &&
      (conn->features & GABBLE_CONNECTION_FEATURES_PRESENCE_INVISIBLE) == 0)
    return FALSE;
  else
    return TRUE;
}


void
conn_presence_class_init (GabbleConnectionClass *klass)
{
  tp_presence_mixin_class_init ((GObjectClass *) klass,
      G_STRUCT_OFFSET (GabbleConnectionClass, presence_class),
      status_available_cb, construct_contact_statuses_cb,
      set_own_status_cb, gabble_statuses);
}


void
conn_presence_init (GabbleConnection *conn)
{
  g_signal_connect (conn->presence_cache, "presence-update",
      G_CALLBACK (connection_presence_update_cb), conn);
  g_signal_connect (conn, "status-changed",
      G_CALLBACK (connection_status_changed_cb), conn);

  tp_presence_mixin_init ((GObject *) conn,
      G_STRUCT_OFFSET (GabbleConnection, presence));
}


void
conn_presence_finalize (GabbleConnection *conn)
{
  tp_presence_mixin_finalize ((GObject *) conn);
}


void
conn_presence_iface_init (gpointer g_iface, gpointer iface_data)
{
  tp_presence_mixin_iface_init (g_iface, iface_data);
}
