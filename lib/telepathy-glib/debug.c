/*
 * debug.c - Common debug support
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007 Nokia Corporation
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
 * SECTION:debug
 * @title: Common debug support
 * @short_description: API to activate debugging messages from telepathy-glib
 *
 * telepathy-glib has an internal mechanism for debug messages and filtering.
 * Connection managers written with telepathy-glib are expected to connect
 * this to their own debugging mechanisms: when the CM's debugging mechanism
 * is activated, it should call tp_debug_set_flags_from_string(),
 * tp_debug_set_flags_from_env() or tp_debug_set_all_flags().
 *
 * For example, at the time of writing Gabble has the following behaviour:
 *
 * <itemizedlist>
 * <listitem>The environment variable $GABBLE_DEBUG contains a comma-separated
 * list of debug modes to activate. In addition to parsing this variable
 * itself, Gabble calls
 * <literal>tp_debug_set_flags_from_env ("GABBLE_DEBUG")</literal>.</listitem>
 * <listitem>The environment variable $GABBLE_PERSIST activates all possible
 * debug modes. If this variable is set, in addition to activating all of
 * its own debug modes, Gabble calls tp_debug_set_all_flags() to activate
 * all possible debug output in telepathy-glib.</listitem>
 * </itemizedlist>
 *
 * The supported debug-mode keywords are subject to change, but currently
 * include:
 *
 * <itemizedlist>
 * <listitem><literal>persist</literal> - keep running even if there are no
 * connections for a while</listitem>
 * <listitem><literal>connection</literal> - output debug messages regarding
 * #TpBaseConnection</listitem>
 * <listitem><literal>im</literal> - output debug messages regarding
 * (text) instant messaging</listitem>
 * <listitem><literal>properties</literal> - output debug messages regarding
 * #TpPropertiesMixin</listitem>
 * <listitem><literal>params</literal> - output debug messages regarding
 * connection manager parameters</listitem>
 * <listitem><literal>all</literal> - all of the above</listitem>
 * </itemizedlist>
 */
#include "config.h"

#include <glib.h>

#ifdef ENABLE_DEBUG

#include <telepathy-glib/debug.h>

#include "internal-debug.h"

#include <stdarg.h>

static TpDebugFlags flags = 0;

/**
 * tp_debug_set_all_flags:
 *
 * Activate all possible debug modes.
 */
void
tp_debug_set_all_flags (void)
{
  flags = 0xffff;
}

static GDebugKey keys[] = {
  { "groups",        TP_DEBUG_GROUPS },
  { "properties",    TP_DEBUG_PROPERTIES },
  { "connection",    TP_DEBUG_CONNECTION },
  { "persist",       TP_DEBUG_PERSIST },
  { "im",            TP_DEBUG_IM },
  { "params",        TP_DEBUG_PARAMS },
  { 0, },
};

/**
 * tp_debug_set_flags_from_string:
 * @flags_string: The flags to set, comma-separated. If %NULL or empty,
 *  no additional flags are set.
 *
 * Set the debug flags indicated by @flags_string, in addition to any already
 * set.
 *
 * The parsing matches that of g_parse_debug_string().
 */
void
tp_debug_set_flags_from_string (const gchar *flags_string)
{
  guint nkeys;

  for (nkeys = 0; keys[nkeys].value; nkeys++);

  if (flags_string)
    _tp_debug_set_flags (g_parse_debug_string (flags_string, keys, nkeys));
}

/**
 * tp_debug_set_flags_from_env:
 * @var: The name of the environment variable to parse
 *
 * Set the debug flags indicated by the given environment variable, in
 * addition to any already set. Equivalent to
 * <literal>tp_debug_set_flags_from_string (g_getenv (var))</literal>.
 */
void
tp_debug_set_flags_from_env (const gchar *var)
{
  tp_debug_set_flags_from_string (g_getenv (var));
}

/*
 * _tp_debug_set_flags:
 * @new_flags More flags to set
 *
 * Set extra flags. For internal use only
 */
void
_tp_debug_set_flags (TpDebugFlags new_flags)
{
  flags |= new_flags;
}

/*
 * _tp_debug_set_flags:
 * @flag: Flag to test
 *
 * Returns: %TRUE if the flag is set. For use via DEBUGGING() only.
 */
gboolean
_tp_debug_flag_is_set (TpDebugFlags flag)
{
  return (flag & flags) != 0;
}

/*
 * _tp_debug_set_flags:
 * @flag: Flag to test
 * @format: Format string for g_logv
 *
 * Emit a debug message with the given format and arguments, but only if
 * the given debug flag is set. For use via DEBUG() only.
 */
void _tp_debug (TpDebugFlags flag,
                const gchar *format,
                ...)
{
  if (flag & flags)
    {
      va_list args;
      va_start (args, format);
      g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
      va_end (args);
    }
}

#else /* !ENABLE_DEBUG */

void
tp_debug_set_all_flags (void)
{
}

void
tp_debug_set_flags_from_string (const gchar *flags_string)
{
}

void
tp_debug_set_flags_from_env (const gchar *var)
{
}

#endif /* !ENABLE_DEBUG */
