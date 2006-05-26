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

#include <stdlib.h>
#include <stdio.h>
#include <execinfo.h>

#include <dbus/dbus-glib.h>

#include "gabble-connection-manager.h"
#include "telepathy-errors.h"
#include "telepathy-errors-enumtypes.h"

GSource *timeout = NULL;
GMainLoop *mainloop = NULL;
GabbleConnectionManager *manager = NULL;
gboolean connections_exist = FALSE;
guint timeout_id;

#define DIE_TIME 5000

static gboolean
kill_connection_manager (gpointer data)
{
  if (!g_getenv ("GABBLE_PERSIST") && !connections_exist)
    {
      g_debug ("no connections, and timed out");
      g_object_unref (manager);
      g_main_loop_quit (mainloop);
    }

  timeout_id = 0;
  return FALSE;
}

static void
new_connection (GabbleConnectionManager *conn,
                gchar *bus_name,
                gchar *object_path,
                gchar *proto)
{
  connections_exist = TRUE;
  g_source_remove (timeout_id);
}

static void
no_more_connections (GabbleConnectionManager *conn)
{
  connections_exist = FALSE;

  if (0 != timeout_id)
    {
      g_source_remove (timeout_id);
    }

  timeout_id = g_timeout_add (DIE_TIME, kill_connection_manager, NULL);
}

static void
print_backtrace (void)
{
  void *array[10];
  size_t size;
  char **strings;
  size_t i;

  fprintf(stderr, "\n########## Backtrace ##########\n");
  size = backtrace (array, 10);
  strings = backtrace_symbols (array, size);

  fprintf (stderr, "Obtained %zd stack frames.\n", size);

  for (i = 0; i < size; i++)
     fprintf (stderr, "%s\n", strings[i]);

  free (strings);
}

static void
critical_handler (const gchar *log_domain,
                  GLogLevelFlags log_level,
                  const gchar *message,
                  gpointer user_data)
{
  print_backtrace ();

  g_log_default_handler (log_domain, log_level, message, user_data);
}

int
main (int argc,
      char **argv)
{
  g_type_init();

  g_set_prgname("telepathy-gabble");

    {
      GLogLevelFlags fatal_mask;

      fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
      fatal_mask |= G_LOG_LEVEL_CRITICAL;
      g_log_set_always_fatal (fatal_mask);

      g_log_set_handler ("GLib-GObject",
          G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR |
          G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
          critical_handler, NULL);
      g_log_set_handler ("GLib",
          G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR |
          G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
          critical_handler, NULL);
      g_log_set_handler (NULL,
          G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR |
          G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION,
          critical_handler, NULL);
    }

  mainloop = g_main_loop_new (NULL, FALSE);

  dbus_g_error_domain_register (TELEPATHY_ERRORS,
      "org.freedesktop.Telepathy.Error", TELEPATHY_TYPE_ERRORS);

  manager = g_object_new (GABBLE_TYPE_CONNECTION_MANAGER, NULL);

  g_signal_connect (manager, "new-connection",
      (GCallback) new_connection, NULL);

  g_signal_connect (manager, "no-more-connections",
      (GCallback) no_more_connections, NULL);

  _gabble_connection_manager_register (manager);

  g_debug ("started version " GABBLE_VERSION);

  timeout_id = g_timeout_add (DIE_TIME, kill_connection_manager, NULL);

  g_main_loop_run (mainloop);

  return 0;
}
