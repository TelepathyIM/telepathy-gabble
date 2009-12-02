/*
 * main.c - entry point for telepathy-gabble-debug used by tests
 * Copyright (C) 2008 Collabora Ltd.
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

#include <stdlib.h>

#include "gabble.h"
#include "connection.h"
#include "vcard-manager.h"
#include "jingle-factory.h"
#include "jingle-session.h"

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <telepathy-glib/dbus.h>
#include <wocky/wocky.h>

#include "test-resolver.h"

static DBusHandlerResult
dbus_filter_function (DBusConnection *connection,
    DBusMessage *message,
    void *user_data)
{
  if (dbus_message_is_signal (message, DBUS_INTERFACE_LOCAL, "Disconnected") &&
      !tp_strdiff (dbus_message_get_path (message), DBUS_PATH_LOCAL))
    {
      wocky_deinit ();
      exit (1);
    }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int
main (int argc,
      char **argv)
{
  TpDBusDaemon *bus_daemon = NULL;
  GError *error = NULL;
  DBusConnection *connection;
  int ret = 1;
  GResolver *kludged;

  gabble_init ();

  bus_daemon = tp_dbus_daemon_dup (&error);
  if (bus_daemon == NULL)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      error = NULL;
      goto out;
    }

  /* It appears that dbus-glib registers a filter that wrongly returns
   * DBUS_HANDLER_RESULT_HANDLED for signals, so for *our* filter to have any
   * effect, we need to install it as soon as possible */
  connection = dbus_g_connection_get_connection (
      ((TpProxy *) bus_daemon)->dbus_connection);
  dbus_connection_add_filter (connection, dbus_filter_function, NULL, NULL);

  dbus_connection_set_exit_on_disconnect (connection, FALSE);
  /* needed for test-disco-no-reply.py */
  gabble_connection_set_disco_reply_timeout (3);
  /* needed for test-avatar-async.py */
  gabble_vcard_manager_set_suspend_reply_timeout (3);
  gabble_vcard_manager_set_default_request_timeout (3);
  
  /* hook up the fake DNS resolver that lets us divert A and SRV queries *
   * into our local cache before asking the real DNS                     */
  kludged = g_object_new (TEST_TYPE_RESOLVER, NULL);
  g_resolver_set_default (kludged);

  test_resolver_add_A (TEST_RESOLVER (kludged),
      "resolves-to-5.4.3.2", "5.4.3.2");
  test_resolver_add_A (TEST_RESOLVER (kludged),
      "resolves-to-1.2.3.4", "1.2.3.4");
  test_resolver_add_A (TEST_RESOLVER (kludged),
      "localhost", "127.0.0.1");
  test_resolver_add_A (TEST_RESOLVER (kludged),
      "stun.collabora.co.uk", "6.7.8.9");

  gabble_jingle_factory_set_test_mode ();

  ret = gabble_main (argc, argv);

  g_object_unref (bus_daemon);
out:
  return ret;
}
