/*
 * conn-addressing.h - Header for Gabble connection code handling addressing.
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

#include "conn-addressing.h"

#define DEBUG_FLAG GABBLE_DEBUG_CONNECTION
#include "debug.h"
#include "namespaces.h"
#include "util.h"

#include "extensions/extensions.h"

#include <dbus/dbus-glib-lowlevel.h>

static void
_fill_contact_attributes (TpHandleRepoIface *contact_repo,
    TpHandle contact,
    GHashTable *attributes_hash)
{
  const gchar *identifier = tp_handle_inspect (contact_repo, contact);
  gchar **uris = g_new0 (gchar *, 2);
  GHashTable *addresses = g_hash_table_new (g_str_hash, g_str_equal);
  GValue *uris_val;
  GValue *addr_val;

  *uris = g_strdup_printf ("xmpp:%s", identifier);
  uris_val = tp_g_value_slice_new_take_boxed (G_TYPE_STRV, uris);

  tp_contacts_mixin_set_contact_attribute (attributes_hash,
      contact, GABBLE_IFACE_CONNECTION_INTERFACE_ADDRESSING"/uris",
      uris_val);

  g_hash_table_insert (addresses, "x-jabber", (gpointer) identifier);
  addr_val = tp_g_value_slice_new_take_boxed (
      TP_HASH_TYPE_STRING_STRING_MAP, addresses);

  tp_contacts_mixin_set_contact_attribute (attributes_hash,
      contact, GABBLE_IFACE_CONNECTION_INTERFACE_ADDRESSING"/addresses",
      addr_val);
}

static void
conn_addressing_fill_contact_attributes (GObject *obj,
    const GArray *contacts,
    GHashTable *attributes_hash)
{
  guint i;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) obj, TP_HANDLE_TYPE_CONTACT);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle contact = g_array_index (contacts, TpHandle, i);
      _fill_contact_attributes (contact_repo, contact, attributes_hash);
    }
}

static void
conn_addressing_get_contacts_by_uri (GabbleSvcConnectionInterfaceAddressing *iface,
    const gchar **in_URIs,
    const gchar **in_Interfaces,
    DBusGMethodInvocation *context)
{
  const gchar **uri;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) iface, TP_HANDLE_TYPE_CONTACT);
  GHashTable *result;
  GHashTable *requested = g_hash_table_new (g_direct_hash, g_direct_equal);
  GArray *handles = g_array_sized_new (TRUE, TRUE, sizeof (TpHandle),
      g_strv_length ((gchar **) in_URIs));
  gchar *sender = dbus_g_method_get_sender (context);
  GList *contacts;
  GList *contact;

  for (uri = in_URIs; *uri != NULL; uri++)
    {
      TpHandle h = 0;
      gchar *jid = gabble_uri_to_jid (*uri, NULL);

      if (jid != NULL)
        h = tp_handle_ensure (contact_repo, jid, NULL, NULL);

      g_free (jid);

      if (h == 0)
        continue;

      g_hash_table_insert (requested, GUINT_TO_POINTER (h), (gpointer) *uri);
      g_array_append_val (handles, h);
    }

  result = tp_contacts_mixin_get_contacts_attributes (G_OBJECT (iface), handles,
      in_Interfaces, sender);

  contacts = g_hash_table_get_keys (result);

  for (contact = contacts; contact != NULL; contact = contact->next)
    {
      GValue *val = tp_g_value_slice_new_string (g_hash_table_lookup (requested,
              contact->data));
      TpHandle h = GPOINTER_TO_UINT (contact->data);

      _fill_contact_attributes (contact_repo, h, result);

      tp_contacts_mixin_set_contact_attribute (result, h,
          GABBLE_IFACE_CONNECTION_INTERFACE_ADDRESSING"/requested-uri", val);
    }

  gabble_svc_connection_interface_addressing_return_from_get_contacts_by_uri (context,
      result);

  tp_handles_unref (contact_repo, handles);
  g_list_free (contacts);
  g_hash_table_destroy (requested);
  g_hash_table_unref (result);
  g_free (sender);
}

static void
conn_addressing_get_contacts_by_vcard_field (GabbleSvcConnectionInterfaceAddressing *iface,
    const gchar *in_Field,
    const gchar **in_Addresses,
    const gchar **in_Interfaces,
    DBusGMethodInvocation *context)
{
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) iface, TP_HANDLE_TYPE_CONTACT);
  const gchar **address;
  gchar *sender = dbus_g_method_get_sender (context);
  GHashTable *result;
  GHashTable *requested = g_hash_table_new (g_direct_hash, g_direct_equal);
  GArray *handles = g_array_sized_new (TRUE, TRUE, sizeof (TpHandle),
      g_strv_length ((gchar **) in_Addresses));
  GList *contacts;
  GList *contact;

  if (g_ascii_strcasecmp (in_Field, "x-jabber") != 0)
    {
      GError *error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "'%s' vCard field is not supported.", in_Field);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  for (address = in_Addresses; *address != NULL; address++)
    {
      TpHandle h = tp_handle_ensure (contact_repo, *address, NULL, NULL);

      if (h == 0)
        continue;

      g_hash_table_insert (requested, GUINT_TO_POINTER (h), (gpointer) *address);
      g_array_append_val (handles, h);
    }

  result = tp_contacts_mixin_get_contacts_attributes (G_OBJECT (iface), handles,
      in_Interfaces, sender);

  contacts = g_hash_table_get_keys (result);

  for (contact = contacts; contact != NULL; contact = contact->next)
    {
      TpHandle h = GPOINTER_TO_UINT (contact->data);
      GValueArray *req_address = tp_value_array_build (2,
          G_TYPE_STRING, in_Field,
          G_TYPE_STRING, g_hash_table_lookup (requested, contact->data),
          G_TYPE_INVALID);
      GValue *val = tp_g_value_slice_new_take_boxed (
          GABBLE_STRUCT_TYPE_REQUESTED_ADDRESS, req_address);

      _fill_contact_attributes (contact_repo, h, result);

      tp_contacts_mixin_set_contact_attribute (result, h,
          GABBLE_IFACE_CONNECTION_INTERFACE_ADDRESSING"/requested-address", val);
    }

  gabble_svc_connection_interface_addressing_return_from_get_contacts_by_vcard_field (
      context, result);

  tp_handles_unref (contact_repo, handles);
  g_list_free (contacts);
  g_hash_table_destroy (requested);
  g_hash_table_unref (result);
  g_free (sender);
}

void
conn_addressing_init (GabbleConnection *self) {
  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (self),
      GABBLE_IFACE_CONNECTION_INTERFACE_ADDRESSING,
      conn_addressing_fill_contact_attributes);
}

void
conn_addressing_iface_init (gpointer g_iface,
    gpointer iface_data)
{
#define IMPLEMENT(x) \
  gabble_svc_connection_interface_addressing_implement_##x (\
  g_iface, conn_addressing_##x)

  IMPLEMENT (get_contacts_by_uri);
  IMPLEMENT (get_contacts_by_vcard_field);
#undef IMPLEMENT
}
