/*
 * run.c - entry point for telepathy-glib connection managers
 * Copyright (C) 2005, 2007 Collabora Ltd.
 * Copyright (C) 2005, 2007 Nokia Corporation
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
 * SECTION:run
 * @title: Connection manager life cycle
 * @short_description: entry point for telepathy-glib connection managers
 *
 * tp_run_connection_manager() provides a convenient entry point for
 * telepathy-glib connection managers. It initializes most of the
 * functionality the CM will need, constructs a connection manager object
 * and lets it run.
 *
 * This function also manages the connection manager's lifetime - if there
 * are no new connections for a while, it times out and exits.
 */

#include "config.h"

#include <telepathy-glib/run.h>

#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "internal-debug.h"
#include <telepathy-glib/base-connection-manager.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/errors.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif /* HAVE_EXECINFO_H */

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif /* HAVE_SIGNAL_H */

static GMainLoop *mainloop = NULL;
static TpBaseConnectionManager *manager = NULL;
static gboolean connections_exist = FALSE;
static guint timeout_id = 0;

static gboolean
kill_connection_manager (gpointer data)
{
#ifdef ENABLE_DEBUG
  if (!_tp_debug_flag_is_set (TP_DEBUG_PERSIST) && !connections_exist)
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
new_connection (TpBaseConnectionManager *conn,
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

#define DIE_TIME 5000

static void
no_more_connections (TpBaseConnectionManager *conn)
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
#if defined (HAVE_SIGNAL) && defined (ENABLE_BACKTRACE)
  signal (SIGSEGV, segv_handler);
#endif /* HAVE_SIGNAL && ENABLE_BACKTRACE */
}

/**
 * tp_run_connection_manager:
 * @prog_name: The program name to be used in debug messages etc.
 * @version: The program version
 * @construct_cm: A function which will return the connection manager
 *                object
 * @argc: The number of arguments passed to the program
 * @argv: The arguments passed to the program
 *
 * Run the connection manager by initializing libraries, constructing
 * a main loop, instantiating a connection manager and running the main
 * loop. When this function returns, the program should exit.
 *
 * If the connection manager does not create a connection within a
 * short arbitrary time (currently 5 seconds), either on startup or after
 * the last open connection is disconnected, and the PERSIST debug
 * flag is not set, return 0.
 *
 * If registering the connection manager on D-Bus fails, return 1.
 *
 * Returns: the status code with which the process should exit
 */

int
tp_run_connection_manager (const char *prog_name,
                           const char *version,
                           TpBaseConnectionManager *(*construct_cm) (void),
                           int argc,
                           char **argv)
{
  GLogLevelFlags fatal_mask;

  add_signal_handlers ();

  g_type_init ();

  g_set_prgname (prog_name);

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

  mainloop = g_main_loop_new (NULL, FALSE);

  dbus_g_error_domain_register (TP_ERRORS,
      "org.freedesktop.Telepathy.Error", TP_TYPE_ERROR);

  manager = construct_cm ();

  g_signal_connect (manager, "new-connection",
      (GCallback) new_connection, NULL);

  g_signal_connect (manager, "no-more-connections",
      (GCallback) no_more_connections, NULL);

  if (!tp_base_connection_manager_register (manager))
    {
      return 1;
    }

  g_debug ("started version %s (telepathy-glib version %s)", version,
      VERSION);

  timeout_id = g_timeout_add (DIE_TIME, kill_connection_manager, NULL);

  g_main_loop_run (mainloop);

  return 0;
}
