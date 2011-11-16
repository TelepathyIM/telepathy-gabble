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

#include <dbus/dbus-glib-lowlevel.h>

#include "extensions/extensions.h"

#include "addressing-util.h"
#include "namespaces.h"
#include "util.h"

static const char *assumed_interfaces[] = {
    TP_IFACE_CONNECTION,
    GABBLE_IFACE_CONNECTION_INTERFACE_ADDRESSING,
    NULL
  };


static void
_fill_contact_attributes (TpHandleRepoIface *contact_repo,
    TpHandle contact,
    GHashTable *attributes_hash)
{
  gchar **uris = gabble_uris_for_handle (contact_repo, contact);
  GHashTable *addresses = gabble_vcard_addresses_for_handle (contact_repo, contact);

  tp_contacts_mixin_set_contact_attribute (attributes_hash,
      contact, GABBLE_IFACE_CONNECTION_INTERFACE_ADDRESSING"/uris",
      tp_g_value_slice_new_take_boxed (G_TYPE_STRV, uris));

  tp_contacts_mixin_set_contact_attribute (attributes_hash,
      contact, GABBLE_IFACE_CONNECTION_INTERFACE_ADDRESSING"/addresses",
      tp_g_value_slice_new_take_boxed (TP_HASH_TYPE_STRING_STRING_MAP, addresses));
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
    const gchar **uris,
    const gchar **interfaces,
    DBusGMethodInvocation *context)
{
  const gchar **uri;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) iface, TP_HANDLE_TYPE_CONTACT);
  GHashTable *attributes;
  GHashTable *requested = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  GArray *handles = g_array_sized_new (TRUE, TRUE, sizeof (TpHandle),
      g_strv_length ((gchar **) uris));
  gchar *sender = dbus_g_method_get_sender (context);

  for (uri = uris; *uri != NULL; uri++)
    {
      TpHandle h = gabble_ensure_handle_from_uri (contact_repo, *uri, NULL);

      if (h == 0)
        continue;

      g_hash_table_insert (requested, (gpointer) g_strdup (*uri), GUINT_TO_POINTER (h));
      g_array_append_val (handles, h);
    }

  attributes = tp_contacts_mixin_get_contact_attributes (G_OBJECT (iface), handles,
      interfaces, assumed_interfaces, sender);

  gabble_svc_connection_interface_addressing_return_from_get_contacts_by_uri (
      context, requested, attributes);

  tp_handles_unref (contact_repo, handles);
  g_hash_table_unref (requested);
  g_hash_table_unref (attributes);
  g_free (sender);
}

static void
conn_addressing_get_contacts_by_vcard_field (GabbleSvcConnectionInterfaceAddressing *iface,
    const gchar *field,
    const gchar **addresses,
    const gchar **interfaces,
    DBusGMethodInvocation *context)
{
  const gchar **address;
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *) iface, TP_HANDLE_TYPE_CONTACT);
  GHashTable *attributes;
  GHashTable *requested = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  GArray *handles = g_array_sized_new (TRUE, TRUE, sizeof (TpHandle),
      g_strv_length ((gchar **) addresses));
  gchar *sender = dbus_g_method_get_sender (context);

  for (address = addresses; *address != NULL; address++)
    {
      TpHandle h = gabble_ensure_handle_from_vcard_address (contact_repo, field,
          *address, NULL);

      if (h == 0)
        continue;

      g_hash_table_insert (requested, (gpointer) g_strdup (*address), GUINT_TO_POINTER (h));
      g_array_append_val (handles, h);
    }

  attributes = tp_contacts_mixin_get_contact_attributes (G_OBJECT (iface), handles,
      interfaces, assumed_interfaces, sender);

  gabble_svc_connection_interface_addressing_return_from_get_contacts_by_vcard_field (
      context, requested, attributes);

  tp_handles_unref (contact_repo, handles);
  g_hash_table_unref (requested);
  g_hash_table_unref (attributes);
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
