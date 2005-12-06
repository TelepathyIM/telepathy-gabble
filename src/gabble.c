#include <dbus/dbus-glib.h>

#include "gabble-connection-manager.h"
#include "telepathy-errors.h"
#include "telepathy-errors-enumtypes.h"

int main(int argc, char **argv) {
  DBusGConnection *bus;
  DBusGProxy *bus_proxy;
  GError *error = NULL;
  GabbleConnectionManager *manager;
  GMainLoop *mainloop;
  guint request_name_result;

  g_type_init();

  {
    GLogLevelFlags fatal_mask;

    fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
    fatal_mask |= G_LOG_LEVEL_CRITICAL;
    g_log_set_always_fatal (fatal_mask);
  }

  mainloop = g_main_loop_new (NULL, FALSE);

  dbus_g_error_domain_register (TELEPATHY_ERRORS, NULL, TELEPATHY_TYPE_ERRORS);

  bus = dbus_g_bus_get (DBUS_BUS_STARTER, &error);
  if (!bus)
    g_error ("Failed to connect to starter bus: %s", error->message);

  bus_proxy = dbus_g_proxy_new_for_name (bus,
                                         "org.freedesktop.DBus",
                                         "/org/freedesktop/DBus",
                                         "org.freedesktop.DBus");
  if (!bus_proxy)
    g_error ("Failed to get proxy object for bus.");

  if (!dbus_g_proxy_call (bus_proxy, "RequestName", &error,
                          G_TYPE_STRING, "org.freedesktop.Telepathy.ConnectionManager.gabble",
                          G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                          G_TYPE_INVALID,
                          G_TYPE_UINT, &request_name_result,
                          G_TYPE_INVALID))
    g_error("Failed to request bus name: %s", error->message);

  if (request_name_result == DBUS_REQUEST_NAME_REPLY_EXISTS)
    g_error("Failed to acquire bus name, connection manager already running?");

  manager = g_object_new (GABBLE_TYPE_CONNECTION_MANAGER, NULL);

  dbus_g_connection_register_g_object (bus, "/org/freedesktop/Telepathy/ConnectionManager/gabble", G_OBJECT (manager));

  g_debug("started");

  g_main_loop_run (mainloop);

  return 0;
}
