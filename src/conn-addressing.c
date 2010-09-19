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
      const gchar *identifier = tp_handle_inspect (contact_repo, contact);
      gchar **uris = g_new0 (gchar *, 2);
      GHashTable *addresses = g_hash_table_new_full (g_str_hash,
          g_str_equal, NULL, NULL);
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
#undef IMPLEMENT
}
