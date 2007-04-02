/*
 * dbus.c - Source for some Telepathy D-Bus helper functions
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

/**
 * SECTION:dbus
 * @title: D-Bus utilities
 * @short_description: some D-Bus utility functions
 *
 * D-Bus utility functions used in telepathy-glib.
 */

#include <telepathy-glib/dbus.h>

#include <stdlib.h>

#include <telepathy-glib/errors.h>

/**
 * tp_dbus_g_method_return_not_implemented:
 * @context: The D-Bus method invocation context
 *
 * Return the Telepathy error NotImplemented from the method invocation
 * given by @context.
 */
void
tp_dbus_g_method_return_not_implemented (DBusGMethodInvocation *context)
{
  GError e = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED, "Not implemented" };

  dbus_g_method_return_error (context, &e);
}

/**
 * tp_get_bus:
 *
 * <!--Returns: says it all-->
 *
 * Returns: a connection to the starter or session D-Bus daemon.
 */
DBusGConnection *
tp_get_bus ()
{
  static DBusGConnection *bus = NULL;

  if (bus == NULL)
    {
      GError *error = NULL;

      bus = dbus_g_bus_get (DBUS_BUS_STARTER, &error);

      if (bus == NULL)
        {
          g_warning ("Failed to connect to starter bus: %s", error->message);
          exit (1);
        }
    }

  return bus;
}

/**
 * tp_get_bus_proxy:
 *
 * <!--Returns: says it all-->
 *
 * Returns: a proxy for the bus daemon object on the starter or session bus.
 */
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
