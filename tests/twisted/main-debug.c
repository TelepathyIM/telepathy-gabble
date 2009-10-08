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

#include "gabble.h"
#include "connection.h"
#include "vcard-manager.h"
#include "jingle-factory.h"
#include "jingle-session.h"

#include <lib/gibber/gibber-resolver.h>

#include "resolver-fake.h"

int
main (int argc,
      char **argv)
{
  gabble_init ();

  /* needed for connect/timeout.py */
  gabble_connection_set_connect_timeout (3);

  /* needed for test-disco-no-reply.py */
  gabble_connection_set_disco_reply_timeout (3);
  /* needed for test-avatar-async.py */
  gabble_vcard_manager_set_suspend_reply_timeout (3);
  gabble_vcard_manager_set_default_request_timeout (3);

  gibber_resolver_set_resolver (GABBLE_TYPE_RESOLVER_FAKE);
  gabble_jingle_factory_set_test_mode ();

  return gabble_main (argc, argv);
}
