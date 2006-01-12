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
