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

#include "config.h"

#include <telepathy-glib/debug.h>
#include <telepathy-glib/run.h>
#include "debug.h"
#include "gabble-connection-manager.h"

static TpBaseConnectionManager *
construct_cm (void)
{
  return (TpBaseConnectionManager *)g_object_new (
      GABBLE_TYPE_CONNECTION_MANAGER, NULL);
}

int
main (int argc,
      char **argv)
{
  gabble_debug_set_log_file_from_env ();

#ifdef ENABLE_DEBUG
  gabble_debug_set_flags_from_env ();

  /* For backwards compatibility within the stable branch, GABBLE_PERSIST
   * still implies all debug flags in 0.6.x. */
  if (g_getenv ("GABBLE_PERSIST"))
    {
#ifdef HAVE_TP_DEBUG_SET_FLAGS
      /* tp-glib >= 0.6.1: persist is no longer a flag in quite the same way */
      tp_debug_set_flags ("all");
      tp_debug_set_persistent (TRUE);
#else
      /* tp-glib < 0.6.1: persist is a flag, of sorts */
      tp_debug_set_flags_from_string ("all");
#endif
    }
#endif

  return tp_run_connection_manager ("telepathy-gabble", VERSION,
      construct_cm, argc, argv);
}
