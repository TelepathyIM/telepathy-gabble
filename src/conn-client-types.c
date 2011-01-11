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
    GPtrArray **types,
    gboolean add_null)
{
  GabblePresence *presence;
  GPtrArray *empty_array;
  const gchar *res;

  empty_array = g_ptr_array_new ();
  g_ptr_array_add (empty_array, NULL);

  presence = gabble_presence_cache_get (conn->presence_cache, handle);

  /* We know that we know nothing about this chap, so empty array it is. */
  if (presence == NULL)
    {
      *types = empty_array;
      return TRUE;
    }

  /* Find the best resource. */
  res = gabble_presence_pick_resource_by_caps (presence,
      DEVICE_AGNOSTIC, NULL, NULL);

  if (res == NULL)
    {
      *types = empty_array;
      return TRUE;
    }

  /* Get the cached client types. */
  *types = gabble_presence_get_client_types_array (presence, res, add_null);

  if (*types == NULL)
    {
      /* There's a pending disco request happening, so don't give an
       * empty array for this fellow. */
      if (gabble_presence_cache_disco_in_progress (conn->presence_cache,
              handle, res))
        {
          g_ptr_array_unref (empty_array);
          return FALSE;
        }

      /* This guy, on the other hand, can get the most empty of arrays. */
      *types = empty_array;
    }
  else
    {
      g_ptr_array_unref (empty_array);
    }

  return TRUE;
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
  GPtrArray *types_list;

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

  client_types = g_hash_table_new (g_direct_hash, g_direct_equal);
  types_list = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_ptr_array_unref);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GPtrArray *types;

      if (!get_client_types_from_handle (conn, handle, &types, TRUE))
        continue;

      g_hash_table_insert (client_types, GUINT_TO_POINTER (handle),
          types->pdata);

      g_ptr_array_add (types_list, types);
    }

  tp_svc_connection_interface_client_types_return_from_get_client_types (
      context, client_types);

  g_hash_table_unref (client_types);
  g_ptr_array_unref (types_list);
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
      GPtrArray *types;

      if (!get_client_types_from_handle (conn, handle, &types, FALSE))
        continue;

      val = tp_g_value_slice_new_boxed (
          dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING),
          types);

      tp_contacts_mixin_set_contact_attribute (attributes_hash, handle,
          TP_IFACE_CONNECTION_INTERFACE_CLIENT_TYPES "/client-types", val);

      g_ptr_array_unref (types);
    }
}

static void
presences_updated_cb (GabblePresenceCache *presence_cache,
    const GArray *contacts,
    GabbleConnection *conn)
{
  guint i;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GabblePresence *presence;
      GPtrArray *array, *empty_array;
      const gchar *res;

      empty_array = g_ptr_array_new ();
      g_ptr_array_add (empty_array, NULL);

      presence = gabble_presence_cache_get (presence_cache, handle);

      if (presence == NULL)
        {
          array = empty_array;
          goto emit;
        }

      res = gabble_presence_pick_resource_by_caps (presence,
          DEVICE_AGNOSTIC, NULL, NULL);

      if (res == NULL)
        {
          array = empty_array;
          goto emit;
        }

      array = gabble_presence_get_client_types_array (presence, res, TRUE);

      if (gabble_presence_cache_disco_in_progress (presence_cache, handle, res)
          || array == NULL)
        {
          goto cleanup;
        }

emit:
      tp_svc_connection_interface_client_types_emit_client_types_updated (
          conn, handle, (const gchar **) array->pdata);

      if (array != empty_array)
        g_ptr_array_unref (array);

cleanup:
      g_ptr_array_unref (empty_array);
    }
}

void
conn_client_types_init (GabbleConnection *conn)
{
  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (conn),
    TP_IFACE_CONNECTION_INTERFACE_CLIENT_TYPES,
    conn_client_types_fill_contact_attributes);

  g_signal_connect (conn->presence_cache, "presences-updated",
      G_CALLBACK (presences_updated_cb), conn);
}
