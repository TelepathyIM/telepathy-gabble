/*
 * conn-client-types - Gabble client types interface
 * Copyright (C) 2010 Collabora Ltd.
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

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <telepathy-glib/interfaces.h>

#include <extensions/extensions.h>

#include "conn-client-types.h"
#include "disco.h"
#include "namespaces.h"
#include "presence.h"
#include "presence-cache.h"

#define DEBUG_FLAG GABBLE_DEBUG_CLIENT_TYPES
#include "debug.h"

static gboolean
get_client_types_from_handle (GabbleConnection *conn,
    TpHandle handle,
    gchar ***types_out)
{
  GabblePresence *presence = gabble_presence_cache_get (conn->presence_cache,
      handle);

  g_return_val_if_fail (types_out != NULL, FALSE);

  if (presence == NULL)
    {
      /* We have no presence information for this contact; so they have no
       * known client types.
       */
      static gchar *empty[] = { NULL };
      *types_out = g_strdupv (empty);
      return TRUE;
    }
  else
    {
      const gchar *res;
      gchar **types = gabble_presence_get_client_types_array (presence, &res);

      /* If we don't have any client types for this contact, and a disco
       * request is in progress, then keep quiet rather than reporting that
       * they have no client types; when the result comes in, their true client
       * types will be reported.
       */
      if (types[0] == NULL &&
          gabble_presence_cache_disco_in_progress (conn->presence_cache,
              handle, res))
        {
          g_strfreev (types);
          return FALSE;
        }

      *types_out = types;
      return TRUE;
    }
}

static void
client_types_get_client_types (TpSvcConnectionInterfaceClientTypes *iface,
    const GArray *contacts,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_handles;
  guint i;
  GHashTable *client_types;
  GError *error = NULL;

  /* Validate contacts */
  contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handles_are_valid (contact_handles, contacts, TRUE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (DEBUGGING)
    {
      DEBUG ("GetClientTypes called on the following handles:");

      for (i = 0; i < contacts->len; i++)
        {
          DEBUG (" * %u", g_array_index (contacts, TpHandle, i));
        }
    }

  client_types = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_strfreev);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      gchar **types;

      if (!get_client_types_from_handle (conn, handle, &types))
        continue;

      g_hash_table_insert (client_types, GUINT_TO_POINTER (handle),
          types);
    }

  tp_svc_connection_interface_client_types_return_from_get_client_types (
      context, client_types);

  g_hash_table_unref (client_types);
}

void
conn_client_types_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpSvcConnectionInterfaceClientTypesClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_client_types_implement_##x \
  (klass, client_types_##x)
  IMPLEMENT (get_client_types);
#undef IMPLEMENT
}

static void
conn_client_types_fill_contact_attributes (GObject *obj,
    const GArray *contacts,
    GHashTable *attributes_hash)
{
  GabbleConnection *conn = GABBLE_CONNECTION (obj);
  guint i;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GValue *val;
      gchar **types;

      if (!get_client_types_from_handle (conn, handle, &types))
        continue;

      val = tp_g_value_slice_new_take_boxed (G_TYPE_STRV, types);

      tp_contacts_mixin_set_contact_attribute (attributes_hash, handle,
          TP_IFACE_CONNECTION_INTERFACE_CLIENT_TYPES "/client-types", val);
    }
}

typedef struct
{
  TpHandle handle;
  GabbleConnection *conn;
} UpdatedData;

static void
updated_data_free (gpointer user_data)
{
  UpdatedData *data = user_data;

  if (data->conn != NULL)
    g_object_remove_weak_pointer (G_OBJECT (data->conn),
        (gpointer *) &data->conn);

  g_slice_free (UpdatedData, data);
}

static gboolean
idle_timeout (gpointer user_data)
{
  UpdatedData *data = user_data;
  gchar **types;

  if (data->conn == NULL)
    return FALSE;

  if (get_client_types_from_handle (data->conn, data->handle, &types))
    {
      tp_svc_connection_interface_client_types_emit_client_types_updated (
          data->conn, data->handle, (const gchar **) types);
      g_strfreev (types);
    }

  return FALSE;
}

static void
presence_cache_client_types_updated_cb (GabblePresenceCache *presence_cache,
    TpHandle handle,
    GabbleConnection *conn)
{
  UpdatedData *data = g_slice_new0 (UpdatedData);
  data->handle = handle;
  data->conn = conn;
  g_object_add_weak_pointer (G_OBJECT (conn), (gpointer *) &data->conn);

  /* Unfortunately, the client-types-updated signal can be emitted before the
   * caps URIs have been processed to determine which client types a
   * freshly-online contact has (and whether a disco request needs to be made
   * to find out).
   *
   * Specifically, gabble_presence_cache_update() may emit client-types-updated
   * if a presence change causes the "dominant" resource to change and the new
   * resource has a different set of client types to the previous one. It is
   * called by gabble_presence_parse_presence_message() just before
   * _process_caps() is called (within which disco requests are sent if
   * necessary). It turns out to be very difficult to rearrange things to sort
   * this out. Moving the emission of the D-Bus signal to an idle allows us to
   * avoid it when we're actually waiting for a disco response to come in.
   */
  g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, idle_timeout, data,
      updated_data_free);
}

void
conn_client_types_init (GabbleConnection *conn)
{
  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (conn),
    TP_IFACE_CONNECTION_INTERFACE_CLIENT_TYPES,
    conn_client_types_fill_contact_attributes);

  g_signal_connect (conn->presence_cache, "client-types-updated",
      G_CALLBACK (presence_cache_client_types_updated_cb), conn);
}
