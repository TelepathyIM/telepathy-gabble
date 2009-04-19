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
#include "gabble.h"

#include <unistd.h>

#include <telepathy-glib/debug.h>
#include <telepathy-glib/run.h>

#include "debug.h"
#include "debugger.h"
#include "connection-manager.h"

static TpBaseConnectionManager *
construct_cm (void)
{
  return (TpBaseConnectionManager *) g_object_new (
      GABBLE_TYPE_CONNECTION_MANAGER, NULL);
}

#ifdef ENABLE_DEBUG
static void
log_to_debugger (GTimeVal *timestamp,
		 const gchar *log_domain,
		 GLogLevelFlags log_level,
		 const gchar *string)
{
  GabbleDebugger *dbg = gabble_debugger_get_singleton ();

  gabble_debugger_add_message (dbg, timestamp, log_domain, log_level, string);
}

static void
simple_log (const gchar *log_domain,
            GLogLevelFlags log_level,
            const gchar *message,
            gpointer user_data)
{
  g_log_default_handler (log_domain, log_level, message, NULL);

  if (tp_strdiff (log_domain, G_LOG_DOMAIN))
    {
      GTimeVal now;
      g_get_current_time (&now);
      log_to_debugger (&now, log_domain, log_level, message);
    }
}

static void
stamp_log (const gchar *log_domain,
           GLogLevelFlags log_level,
           const gchar *message,
           gpointer user_data)
{
  GTimeVal now;
  gchar now_str[32];
  gchar *tmp;
  struct tm tm;

  g_get_current_time (&now);
  localtime_r (&(now.tv_sec), &tm);
  strftime (now_str, 32, "%Y-%m-%d %H:%M:%S", &tm);
  tmp = g_strdup_printf ("%s.%06ld: %s", now_str, now.tv_usec, message);
  g_log_default_handler (log_domain, log_level, tmp, NULL);
  g_free (tmp);

  if (tp_strdiff (log_domain, G_LOG_DOMAIN))
    log_to_debugger (&now, log_domain, log_level, message);
}
#endif

int
gabble_main (int argc,
             char **argv)
{
  tp_debug_divert_messages (g_getenv ("GABBLE_LOGFILE"));

#ifdef ENABLE_DEBUG
  gabble_debug_set_flags_from_env ();

  if (g_getenv ("GABBLE_TIMING") != NULL)
    g_log_set_default_handler (stamp_log, NULL);
  else
    g_log_set_default_handler (simple_log, NULL);

  if (g_getenv ("GABBLE_PERSIST") != NULL)
    tp_debug_set_persistent (TRUE);
#endif

  return tp_run_connection_manager ("telepathy-gabble", VERSION,
      construct_cm, argc, argv);
}
