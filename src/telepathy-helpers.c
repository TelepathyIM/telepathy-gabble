/*
 * telepathy-helpers.c - Source for some Telepathy D-Bus helper functions
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#include <dbus/dbus-glib.h>
#include "telepathy-helpers.h"

DBusGConnection *
tp_get_bus ()
{
  static DBusGConnection *bus = NULL;

  if (bus == NULL)
    {
      GError *error = NULL;

      bus = dbus_g_bus_get (DBUS_BUS_STARTER, &error);

      if (bus == NULL)
        g_error ("Failed to connect to starter bus: %s", error->message);
    }

  return bus;
}

DBusGProxy *
tp_get_bus_proxy ()
{
  static DBusGProxy *bus_proxy = NULL;

  if (bus_proxy == NULL)
    {
      DBusGConnection *bus = tp_get_bus ();

      bus_proxy = dbus_g_proxy_new_for_name (bus,
                                            "org.freedesktop.DBus",
                                            "/org/freedesktop/DBus",
                                            "org.freedesktop.DBus");

      if (bus_proxy == NULL)
        g_error ("Failed to get proxy object for bus.");
    }

  return bus_proxy;
}

static void _list_builder (gpointer key, gpointer value, gpointer data);

GSList *
tp_hash_to_key_value_list (GHashTable *hash)
{
  GSList *ret = NULL;

  g_hash_table_foreach (hash, _list_builder, &ret);

  return ret;
}

void
tp_key_value_list_free (GSList *list)
{
  GSList *iter;

  for (iter = list; iter; iter = g_slist_next(iter))
  {
    g_free (iter->data);
  }

  g_slist_free (list);
}

static void _list_builder (gpointer key, gpointer value, gpointer data)
{
  GSList **list = (GSList **) data;
  TpKeyValue *kv = g_new0 (TpKeyValue, 1);

  kv->key = key;
  kv->value = value;

  *list = g_slist_prepend (*list, kv);
}

