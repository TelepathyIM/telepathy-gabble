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

#include "test-resolver.h"

#include <dbus/dbus.h>

int
main (int argc,
      char **argv)
{
  int ret = 1;
  GResolver *kludged;

  gabble_init ();

  /* needed for test-disco-no-reply.py */
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

  gabble_jingle_factory_set_test_mode ();

  ret = gabble_main (argc, argv);

  /* Hack, remove the ref g_resolver has on this object, atm there is no way to
   * unset a custom resolver */
  g_object_unref (kludged);

  dbus_shutdown ();

  return ret;
}
