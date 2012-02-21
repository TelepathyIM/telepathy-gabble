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

#include "config.h"

#include <stdlib.h>

#include <glib.h>

#ifdef G_OS_UNIX
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include "gabble.h"
#include "connection.h"
#include "vcard-manager.h"
#ifdef ENABLE_VOIP
#include "jingle-factory.h"
#include "jingle-session.h"
#endif
#ifdef ENABLE_JINGLE_FILE_TRANSFER
#include "gtalk-file-collection.h"
#endif

#include "test-resolver.h"

#include <dbus/dbus.h>

#ifdef G_OS_UNIX
static gboolean
connection_established_cb (GSignalInvocationHint *ihint,
    guint n_param_values,
    const GValue *param_values,
    gpointer user_data)
{
  GSocketConnection *conn;
  GSocket *sock;
  gint flag, ret, fd;

  conn = g_value_get_object (param_values + 1);
  sock = g_socket_connection_get_socket (conn);

  flag = 1;

  fd = g_socket_get_fd (sock);
  ret = setsockopt (fd, IPPROTO_TCP, TCP_NODELAY,
      (const char *) &flag, sizeof (flag));

  if (ret == -1)
    /* not the worst thing ever. */
    g_print ("Couldn't setsockopt(TCP_NODELAY) on the connection; ain't so bad.\n");

  return TRUE;
}
#endif

int
main (int argc,
      char **argv)
{
  int ret = 1;
  GResolver *kludged;
#ifdef G_OS_UNIX
  gpointer cls;
#endif

  gabble_init ();

  /* needed for connect/disco-no-reply.py */
  gabble_connection_set_disco_reply_timeout (3);
  /* needed for test-avatar-async.py */
  gabble_vcard_manager_set_suspend_reply_timeout (3);
  gabble_vcard_manager_set_default_request_timeout (3);

  /* hook up the fake DNS resolver that lets us divert A and SRV queries *
   * into our local cache before asking the real DNS                     */
  kludged = g_object_new (TEST_TYPE_RESOLVER, NULL);
  g_resolver_set_default (kludged);
  g_object_unref (kludged);

  test_resolver_add_A (TEST_RESOLVER (kludged),
      "resolves-to-5.4.3.2", "5.4.3.2");
  test_resolver_add_A (TEST_RESOLVER (kludged),
      "resolves-to-1.2.3.4", "1.2.3.4");
  test_resolver_add_A (TEST_RESOLVER (kludged),
      "localhost", "127.0.0.1");
  test_resolver_add_A (TEST_RESOLVER (kludged),
      "stun.telepathy.im", "6.7.8.9");

  test_resolver_add_SRV (TEST_RESOLVER (kludged),
      "stun", "udp", "stunning.localhost", "resolves-to-5.4.3.2", 1);

#ifdef ENABLE_VOIP
  wocky_jingle_info_set_test_mode ();
#endif

#ifdef ENABLE_JINGLE_FILE_TRANSFER
  gtalk_file_collection_set_test_mode ();
#endif

#ifdef G_OS_UNIX
  /* We want to set TCP_NODELAY on the socket as soon as possible in
   * the connector so let's use ::connection-established. We need to
   * ref the class type as it's not loaded yet.  */
  cls = g_type_class_ref (WOCKY_TYPE_CONNECTOR);
  if (g_getenv ("GABBLE_NODELAY") != NULL)
    {
      g_signal_add_emission_hook (
          g_signal_lookup ("connection-established", WOCKY_TYPE_CONNECTOR),
          0, connection_established_cb, NULL, NULL);
    }
#endif

  ret = gabble_main (argc, argv);

  /* Hack, remove the ref g_resolver has on this object, atm there is no way to
   * unset a custom resolver */
  g_object_unref (kludged);

#ifdef G_OS_UNIX
  g_type_class_unref (cls);
#endif

  dbus_shutdown ();

  return ret;
}
