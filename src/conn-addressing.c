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

#include "config.h"

#include "conn-addressing.h"

#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "extensions/extensions.h"

#include "addressing-util.h"
#include "namespaces.h"
#include "util.h"

static const char *assumed_interfaces[] = {
    TP_IFACE_CONNECTION,
    TP_IFACE_CONNECTION_INTERFACE_ADDRESSING1,
    NULL
  };

gboolean
conn_addressing_fill_contact_attributes (GabbleConnection *self,
    const gchar *dbus_interface,
    TpHandle contact,
    TpContactAttributeMap *attributes)
{
  TpHandleRepoIface *contact_repo;
  gchar **uris;
  GHashTable *addresses;

  if (tp_strdiff (dbus_interface, TP_IFACE_CONNECTION_INTERFACE_ADDRESSING1))
    return FALSE;

  contact_repo = tp_base_connection_get_handles ((TpBaseConnection *) self,
      TP_ENTITY_TYPE_CONTACT);
  uris = gabble_uris_for_handle (contact_repo, contact);
  addresses = gabble_vcard_addresses_for_handle (contact_repo, contact);

  tp_contact_attribute_map_take_sliced_gvalue (attributes,
      contact, TP_TOKEN_CONNECTION_INTERFACE_ADDRESSING1_URIS,
      tp_g_value_slice_new_take_boxed (G_TYPE_STRV, uris));

  tp_contact_attribute_map_take_sliced_gvalue (attributes,
      contact, TP_TOKEN_CONNECTION_INTERFACE_ADDRESSING1_ADDRESSES,
      tp_g_value_slice_new_take_boxed (TP_HASH_TYPE_STRING_STRING_MAP,
        addresses));

  return TRUE;
}

static void
conn_addressing_get_contacts_by_uri (TpSvcConnectionInterfaceAddressing1 *iface,
    const gchar **uris,
    const gchar **interfaces,
    DBusGMethodInvocation *context)
{
  const gchar **uri;
  TpBaseConnection *base = TP_BASE_CONNECTION (iface);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base,
      TP_ENTITY_TYPE_CONTACT);
  GHashTable *attributes;
  GHashTable *requested = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  GArray *handles = g_array_sized_new (TRUE, TRUE, sizeof (TpHandle),
      g_strv_length ((gchar **) uris));

  for (uri = uris; *uri != NULL; uri++)
    {
      TpHandle h = gabble_ensure_handle_from_uri (contact_repo, *uri, NULL);

      if (h == 0)
        continue;

      g_hash_table_insert (requested, g_strdup (*uri), GUINT_TO_POINTER (h));
      g_array_append_val (handles, h);
    }

  attributes = tp_base_connection_dup_contact_attributes_hash (base, handles,
      interfaces, assumed_interfaces);

  tp_svc_connection_interface_addressing1_return_from_get_contacts_by_uri (
      context, requested, attributes);

  g_array_unref (handles);
  g_hash_table_unref (requested);
  g_hash_table_unref (attributes);
}

static void
conn_addressing_get_contacts_by_vcard_field (TpSvcConnectionInterfaceAddressing1 *iface,
    const gchar *field,
    const gchar **addresses,
    const gchar **interfaces,
    DBusGMethodInvocation *context)
{
  const gchar **address;
  TpBaseConnection *base = TP_BASE_CONNECTION (iface);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base,
      TP_ENTITY_TYPE_CONTACT);
  GHashTable *attributes;
  GHashTable *requested = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  GArray *handles = g_array_sized_new (TRUE, TRUE, sizeof (TpHandle),
      g_strv_length ((gchar **) addresses));

  for (address = addresses; *address != NULL; address++)
    {
      TpHandle h = gabble_ensure_handle_from_vcard_address (contact_repo, field,
          *address, NULL);

      if (h == 0)
        continue;

      g_hash_table_insert (requested, g_strdup (*address), GUINT_TO_POINTER (h));
      g_array_append_val (handles, h);
    }

  attributes = tp_base_connection_dup_contact_attributes_hash (base, handles,
      interfaces, assumed_interfaces);

  tp_svc_connection_interface_addressing1_return_from_get_contacts_by_vcard_field (
      context, requested, attributes);

  g_array_unref (handles);
  g_hash_table_unref (requested);
  g_hash_table_unref (attributes);
}

void
conn_addressing_init (GabbleConnection *self)
{
}

void
conn_addressing_iface_init (gpointer g_iface,
    gpointer iface_data)
{
#define IMPLEMENT(x) \
  tp_svc_connection_interface_addressing1_implement_##x (\
  g_iface, conn_addressing_##x)

  IMPLEMENT (get_contacts_by_uri);
  IMPLEMENT (get_contacts_by_vcard_field);
#undef IMPLEMENT
}
