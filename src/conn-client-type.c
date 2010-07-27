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

struct _GabbleConnectionClientTypePrivate
{
  /* TpHandle => gchar *resource */
  GHashTable *handle_to_resource;
  guint presences_updated_id;
  GHashTable *cache;
};

static GPtrArray *
get_client_types_from_message (GabbleConnection *conn,
    const gchar *jid,
    LmMessageNode *lm_node,
    TpHandle *contact_handle)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  GabbleConnectionClientTypePrivate *priv = conn->client_type_priv;
  WockyNode *identity, *query_result = (WockyNode *) lm_node;
  WockyNodeIter iter;
  GPtrArray *array;
  TpHandleRepoIface *handles;
  TpHandle handle;
  GError *error = NULL;

  array = g_ptr_array_new_with_free_func (g_free);

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

      g_ptr_array_add (array, g_strdup (type));
    }

  if (array->len == 0)
    {
      DEBUG ("How very odd, we didn't get any client types");
      g_ptr_array_unref (array);
      return NULL;
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
      return NULL;
    }

  /* Add what we have found to the cache. */
  g_hash_table_insert (priv->cache,
      GUINT_TO_POINTER (handle), g_ptr_array_ref (array));

  if (contact_handle != NULL)
    *contact_handle = handle; /* already refed */
  else
    tp_handle_unref (handles, handle);

  return array;
}

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
  TpBaseConnection *base = TP_BASE_CONNECTION (user_data);
  GPtrArray *array;
  TpHandleRepoIface *handles;
  TpHandle handle;

  if (disco_error != NULL)
    {
      DEBUG ("Disco error: %s", disco_error->message);
      return;
    }

  array = get_client_types_from_message (
      GABBLE_CONNECTION (user_data), jid, lm_node, &handle);

  if (array == NULL)
    return;

  gabble_svc_connection_interface_client_type_emit_client_types_updated (
      conn, handle, (const gchar **) array->pdata);

  handles = tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  tp_handle_unref (handles, handle);

  g_ptr_array_unref (array);
}

static void
disco_client_type (GabbleConnection *conn,
    const gchar *full_jid,
    GabbleDiscoCb callback,
    gpointer user_data,
    GError **error)
{
  if (callback == NULL)
    callback = info_request_cb;

  if (user_data == NULL)
    user_data = conn;

  gabble_disco_request (conn->disco, GABBLE_DISCO_TYPE_INFO,
      full_jid, NULL, callback, user_data, G_OBJECT (conn), error);
}

static gboolean
resource_has_changed (GabbleConnection *conn,
    TpHandle handle,
    const gchar *new_resource)
{
  GabbleConnectionClientTypePrivate *priv = conn->client_type_priv;
  const gchar *old_resource;

  old_resource = g_hash_table_lookup (priv->handle_to_resource,
      GUINT_TO_POINTER (handle));

  if (old_resource != NULL && !tp_strdiff (old_resource, new_resource))
    {
      /* The resource hasn't changed. Let's assume that the client
       * types have not changed on this resource since we last looked,
       * because that would be odd. This assumption lets us cut down
       * on the disco traffic, which is nice. */
      return FALSE;
    }

  /* Add this new resource to the hash table so we have something to
   * compare to next time. */
  g_hash_table_insert (priv->handle_to_resource,
      GUINT_TO_POINTER (handle), g_strdup (new_resource));

  return TRUE;
}

static gboolean
dummy_caps_set_predicate (const GabbleCapabilitySet *set,
    gconstpointer user_data)
{
  return TRUE;
}

static gboolean
get_full_jid (GabbleConnection *conn,
    TpHandle handle,
    gchar **full_jid,
    gchar **resource)
{
  TpBaseConnection *base = (TpBaseConnection *) conn;
  GabblePresence *presence;
  const gchar *res, *jid;
  TpHandleRepoIface *contact_repo;

  presence = gabble_presence_cache_get (conn->presence_cache, handle);

  if (presence == NULL)
    return FALSE;

  res = gabble_presence_pick_resource_by_caps (presence,
      DEVICE_AGNOSTIC, dummy_caps_set_predicate, NULL);

  if (res == NULL)
    return FALSE;

  *resource = g_strdup (res);

  contact_repo = tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  jid = tp_handle_inspect (contact_repo, handle);

  *full_jid = g_strdup_printf ("%s/%s", jid, res);

  return TRUE;
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
  GHashTable *client_types;
  GabblePresence *presence;
  GError *error = NULL;
  GPtrArray *types_list, *empty_array;;

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

  empty_array = g_ptr_array_new ();
  g_ptr_array_add (empty_array, NULL);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GPtrArray *types;
      const gchar *res;

      presence = gabble_presence_cache_get (conn->presence_cache, handle);

      /* We know that we know nothing about this chap, so empty array it is. */
      if (presence == NULL)
        {
          types = empty_array;
          goto add_array;
        }

      /* Find the best resource. */
      res = gabble_presence_pick_resource_by_caps (presence,
          DEVICE_AGNOSTIC, dummy_caps_set_predicate, NULL);

      if (res == NULL)
        {
          types = empty_array;
          goto add_array;
        }

      /* Get the cached client types. */
      types = gabble_presence_get_client_types_array (presence, res, TRUE);

      if (types == NULL)
        {
          /* There's a pending disco request happening, so don't give an
           * empty array for this fellow. */
          if (gabble_presence_cache_disco_in_progress (conn->presence_cache,
                  handle, res))
            continue;

          /* This guy, on the other hand, can get the most empty of arrays. */
          types = empty_array;
        }

add_array:
      g_hash_table_insert (client_types, GUINT_TO_POINTER (handle),
          types->pdata);

      if (types != empty_array)
        g_ptr_array_add (types_list, types);
    }

  gabble_svc_connection_interface_client_type_return_from_get_client_types (
      context, client_types);

  g_hash_table_unref (client_types);
  g_ptr_array_unref (types_list);
  g_ptr_array_unref (empty_array);
}

void
conn_client_type_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  GabbleSvcConnectionInterfaceClientTypeClass *klass = g_iface;

#define IMPLEMENT(x) gabble_svc_connection_interface_client_type_implement_##x \
  (klass, client_type_##x)
  IMPLEMENT (get_client_types);
#undef IMPLEMENT
}

static void
conn_client_type_fill_contact_attributes (GObject *obj,
    const GArray *contacts,
    GHashTable *attributes_hash)
{
  GabbleConnection *conn = GABBLE_CONNECTION (obj);
  guint i;
  GPtrArray *empty_array;

  empty_array = g_ptr_array_new ();
  g_ptr_array_add (empty_array, NULL);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GabblePresence *presence;
      GValue *val;
      GPtrArray *types;
      const gchar *res;

      presence = gabble_presence_cache_get (conn->presence_cache, handle);

      if (presence == NULL)
        {
          types = empty_array;
          goto add_array;
        }

      /* Find the best resource. */
      res = gabble_presence_pick_resource_by_caps (presence,
          DEVICE_AGNOSTIC, dummy_caps_set_predicate, NULL);

      if (res == NULL)
        {
          types = empty_array;
          goto add_array;
        }

      /* Get the cached client types. */
      types = gabble_presence_get_client_types_array (presence, res, FALSE);

      if (types == NULL)
        {
          /* There's a pending disco request happening, so don't give an
           * empty array for his troubles. */
          if (gabble_presence_cache_disco_in_progress (conn->presence_cache,
                  handle, res))
            continue;

          /* This guy, on the other hand, can get the most empty of arrays. */
          types = empty_array;
        }

add_array:
      val = tp_g_value_slice_new_boxed (
          dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING),
          types);

      tp_contacts_mixin_set_contact_attribute (attributes_hash, handle,
          GABBLE_IFACE_CONNECTION_INTERFACE_CLIENT_TYPE "/client-type", val);

      if (types != empty_array)
        g_ptr_array_unref (types);
    }

  g_ptr_array_unref (empty_array);

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
      gchar *full_jid, *resource;
      GError *error = NULL;

      if (!get_full_jid (conn, handle, &full_jid, &resource))
        continue;

      if (!resource_has_changed (conn, handle, resource))
        goto next;

      DEBUG ("presence changed for %s", full_jid);

      /* Send a request for the type */
      disco_client_type (conn, full_jid, NULL, NULL, &error);

      if (error != NULL)
        {
          DEBUG ("Failed to make disco request: %s", error->message);
          g_error_free (error);
        }

next:
      g_free (resource);
      g_free (full_jid);
    }
}

void
conn_client_type_init (GabbleConnection *conn)
{
  GabbleConnectionClientTypePrivate *priv;

  conn->client_type_priv = g_slice_new0 (GabbleConnectionClientTypePrivate);
  priv = conn->client_type_priv;

  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (conn),
    GABBLE_IFACE_CONNECTION_INTERFACE_CLIENT_TYPE,
    conn_client_type_fill_contact_attributes);

  priv->presences_updated_id = g_signal_connect (
      conn->presence_cache, "presences-updated",
      G_CALLBACK (presences_updated_cb), conn);

  priv->handle_to_resource = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, g_free);

  priv->cache = g_hash_table_new_full (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) g_ptr_array_unref);
}

void
conn_client_type_dispose (GabbleConnection *conn)
{
  GabbleConnectionClientTypePrivate *priv = conn->client_type_priv;

  if (priv == NULL)
    return;

  g_hash_table_unref (priv->handle_to_resource);
  g_hash_table_unref (priv->cache);

  g_slice_free (GabbleConnectionClientTypePrivate, priv);
  conn->client_type_priv = NULL;
}
