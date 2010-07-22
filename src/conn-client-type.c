/*
 * conn-client-type - Gabble client type interface
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

#include "conn-client-type.h"
#include "disco.h"
#include "namespaces.h"
#include "presence.h"
#include "presence-cache.h"

#define DEBUG_FLAG GABBLE_DEBUG_CLIENT_TYPE
#include "debug.h"

static void
info_request_cb (GabbleDisco *disco,
    GabbleDiscoRequest *request,
    const gchar *jid,
    const gchar *node,
    LmMessageNode *lm_node,
    GError *disco_error,
    gpointer user_data)
{
  GabbleConnection *conn = GABBLE_CONNECTION (user_data);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  WockyNode *identity, *query_result = (WockyNode *) lm_node;
  WockyNodeIter iter;
  GPtrArray *array;
  TpHandleRepoIface *handles;
  TpHandle handle;
  GError *error = NULL;

  if (disco_error != NULL)
    {
      DEBUG ("Disco error: %s", disco_error->message);
      return;
    }

  array = g_ptr_array_new ();

  /* Find all identity nodes in the return. */
  wocky_node_iter_init (&iter, query_result,
      "identity", NS_DISCO_INFO);
  while (wocky_node_iter_next (&iter, &identity))
    {
      const gchar *type;

      /* Now get the client type */
      if ((type = wocky_node_get_attribute (identity, "type")) == NULL)
        continue;

      DEBUG ("Got type for %s: %s", jid, type);

      g_ptr_array_add (array, (gpointer) type);
    }

  if (array->len == 0)
    {
      DEBUG ("How very odd, we didn't get any client types");
      g_ptr_array_unref (array);
      return;
    }

  g_ptr_array_add (array, NULL);

  /* Now we need the handle */
  handles = tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  handle = tp_handle_ensure (handles, jid, NULL, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to ensure handle: %s", error->message);
      g_error_free (error);
      g_ptr_array_unref (array);
      return;
    }

  gabble_svc_connection_interface_client_type_emit_client_types_updated (
      conn, handle, (const gchar **) array->pdata);

  tp_handle_unref (handles, handle);
  g_ptr_array_unref (array);
}

static gboolean
dummy_caps_set_predicate (const GabbleCapabilitySet *set,
    gconstpointer user_data)
{
  return TRUE;
}

static GPtrArray *
get_cached_client_types_or_query (GabbleConnection *conn,
    TpHandle handle,
    GError **error)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  const gchar *jid, *resource;
  TpHandleRepoIface *contact_repo;
  GabblePresence *presence;
  gchar *full_jid;

  contact_repo = tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  jid = tp_handle_inspect (contact_repo, handle);

  /* TODO: get cached client types */

  presence = gabble_presence_cache_get (conn->presence_cache, handle);

  if (presence == NULL)
    {
      GPtrArray *arr = g_ptr_array_new ();
      g_ptr_array_add (arr, NULL);
      return arr;
    }

  resource = gabble_presence_pick_resource_by_caps (presence,
      DEVICE_AGNOSTIC, dummy_caps_set_predicate, NULL);

  if (resource == NULL)
    {
      DEBUG ("Failed to determine a good resource for %s", jid);
      return NULL;
    }

  full_jid = g_strdup_printf ("%s/%s", jid, resource);

  /* Send a request for the type */
  gabble_disco_request (conn->disco, GABBLE_DISCO_TYPE_INFO,
      full_jid, NULL, info_request_cb, conn, G_OBJECT (conn), error);

  g_free (full_jid);

  return NULL;
}

static void
client_type_get_client_types (GabbleSvcConnectionInterfaceClientType *iface,
    const GArray *contacts,
    DBusGMethodInvocation *context)
{
  GabbleConnection *conn = GABBLE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) conn;
  TpHandleRepoIface *contact_handles;
  guint i;
  GError *error = NULL;
  GHashTable *client_types;
  GPtrArray *types_arrays;

  if (DEBUGGING)
    {
      DEBUG ("GetClientTypes called on the following handles:");

      for (i = 0; i < contacts->len; i++)
        {
          DEBUG (" * %u", g_array_index (contacts, TpHandle, i));
        }
    }

  /* Validate contacts */
  contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handles_are_valid (contact_handles, contacts, TRUE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  /* Let's get ready to rumble^Wreturn */
  client_types = g_hash_table_new (g_direct_hash, g_direct_equal);

  types_arrays = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_ptr_array_unref);

  for (i = 0; i < contacts->len; i++)
    {
      GPtrArray *types;
      TpHandle contact = g_array_index (contacts, TpHandle, i);

      types = get_cached_client_types_or_query (conn, contact, &error);

      if (error != NULL)
        {
          GError error2 = { TP_ERRORS, TP_ERROR_NETWORK_ERROR,
              "Getting client type failed" };

          DEBUG ("Sending client type disco request failed: %s",
              error->message);
          g_error_free (error);
          dbus_g_method_return_error (context, &error2);
          goto cleanup;
        }

      if (types != NULL)
        {
          g_ptr_array_add (types_arrays, types);
          g_hash_table_insert (client_types, GUINT_TO_POINTER (contact),
              types->pdata);
        }
    }

  gabble_svc_connection_interface_client_type_return_from_get_client_types (
      context, client_types);

cleanup:
  g_hash_table_unref (client_types);
  g_ptr_array_unref (types_arrays);
}

void
conn_client_type_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  GabbleSvcConnectionInterfaceClientTypeClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_connection_interface_client_type_implement_##x \
  (klass, client_type_##x)
  IMPLEMENT(get_client_types);
#undef IMPLEMENT
}
static void
conn_client_type_fill_contact_attributes (GObject *obj,
    const GArray *contacts,
    GHashTable *attributes_hash)
{
  GabbleConnection *self = GABBLE_CONNECTION (obj);
  guint i;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GPtrArray *types;
      /*
      GValue *val;
      */

      types = get_cached_client_types_or_query (self, handle, NULL);

      if (types == NULL)
        continue;

      /* FIXME */
      /*
      val = tp_g_value_slice_new_boxed (TP_HASH_TYPE_STRING_VARIANT_MAP, types);

      tp_contacts_mixin_set_contact_attribute (attributes_hash, handle,
          GABBLE_IFACE_CONNECTION_INTERFACE_CLIENT_TYPE "/client-type", val);
      */
    }
}

void
conn_client_type_init (GabbleConnection *conn)
{
  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (conn),
    GABBLE_IFACE_CONNECTION_INTERFACE_CLIENT_TYPE,
    conn_client_type_fill_contact_attributes);
}
