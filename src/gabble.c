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

#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif /* HAVE_EXECINFO_H */

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif /* HAVE_SIGNAL_H */

#include "debug.h"
#include "gabble-connection-manager.h"
#include "telepathy-errors.h"

GSource *timeout = NULL;
GMainLoop *mainloop = NULL;
GabbleConnectionManager *manager = NULL;
gboolean connections_exist = FALSE;
guint timeout_id;

#define DIE_TIME 5000

static gboolean
kill_connection_manager (gpointer data)
{
#ifdef ENABLE_DEBUG
  if (!gabble_debug_flag_is_set (GABBLE_DEBUG_PERSIST) && !connections_exist)
#else
  if (!connections_exist)
#endif
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

  if (0 != timeout_id)
    {
      g_source_remove (timeout_id);
    }
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

#ifdef ENABLE_BACKTRACE
static void
print_backtrace (void)
{
#if defined (HAVE_BACKTRACE) && defined (HAVE_BACKTRACE_SYMBOLS_FD)
  void *array[20];
  size_t size;

#define MSG "\n########## Backtrace (version " VERSION ") ##########\n"
  write (STDERR_FILENO, MSG, strlen (MSG));
#undef MSG

  size = backtrace (array, 20);
  backtrace_symbols_fd (array, size, STDERR_FILENO);
#endif /* HAVE_BACKTRACE && HAVE_BACKTRACE_SYMBOLS_FD */
}

static void
critical_handler (const gchar *log_domain,
                  GLogLevelFlags log_level,
                  const gchar *message,
                  gpointer user_data)
{
  g_log_default_handler (log_domain, log_level, message, user_data);
  print_backtrace ();
}

#ifdef HAVE_SIGNAL
static void
segv_handler (int sig)
{
#define MSG "caught SIGSEGV\n"
  write (STDERR_FILENO, MSG, strlen (MSG));
#undef MSG

  print_backtrace ();
  abort ();
}
#endif /* HAVE_SIGNAL */

#endif /* ENABLE_BACKTRACE */

static void
add_signal_handlers (void)
{
#if defined(HAVE_SIGNAL) && defined(ENABLE_BACKTRACE)
  signal (SIGSEGV, segv_handler);
#endif /* HAVE_SIGNAL && ENABLE_BACKTRACE */
}

int
main (int argc,
      char **argv)
{
  add_signal_handlers ();

  g_type_init();

  g_set_prgname("telepathy-gabble");

#ifdef ENABLE_DEBUG
  gabble_debug_set_flags_from_env ();

  if (g_getenv ("GABBLE_PERSIST"))
    gabble_debug_set_flags (0xffff);
#endif

    {
      GLogLevelFlags fatal_mask;

      fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
      fatal_mask |= G_LOG_LEVEL_CRITICAL;
      g_log_set_always_fatal (fatal_mask);

#ifdef ENABLE_BACKTRACE
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
#endif /* ENABLE_BACKTRACE */
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

  g_debug ("started version " VERSION);

  timeout_id = g_timeout_add (DIE_TIME, kill_connection_manager, NULL);

  g_main_loop_run (mainloop);

  return 0;
}
