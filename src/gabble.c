/*
 * gabble.h - entry point and utility functions for telepathy-gabble
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

#include "gabble-connection-manager.h"
#include "telepathy-errors.h"
#include "telepathy-errors-enumtypes.h"

DBusGConnection *
gabble_get_bus ()
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
gabble_get_bus_proxy ()
{
  static DBusGProxy *bus_proxy = NULL;

  if (bus_proxy == NULL)
    {
      DBusGConnection *bus = gabble_get_bus ();

      bus_proxy = dbus_g_proxy_new_for_name (bus,
                                            "org.freedesktop.DBus",
                                            "/org/freedesktop/DBus",
                                            "org.freedesktop.DBus");

      if (bus_proxy == NULL)
        g_error ("Failed to get proxy object for bus.");
    }

  return bus_proxy;
}

int main(int argc, char **argv) {
  GabbleConnectionManager *manager;
  GMainLoop *mainloop;

  g_type_init();

  {
    GLogLevelFlags fatal_mask;

    fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
    fatal_mask |= G_LOG_LEVEL_CRITICAL;
    g_log_set_always_fatal (fatal_mask);
  }

  g_set_prgname("telepathy-gabble");

  mainloop = g_main_loop_new (NULL, FALSE);

  dbus_g_error_domain_register (TELEPATHY_ERRORS, NULL, TELEPATHY_TYPE_ERRORS);

  manager = g_object_new (GABBLE_TYPE_CONNECTION_MANAGER, NULL);

  _gabble_connection_manager_register (manager);

  g_debug("started");

  g_main_loop_run (mainloop);

  return 0;
}
