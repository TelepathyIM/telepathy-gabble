#include "config.h"

#include "internal-debug.h"

#ifdef ENABLE_DEBUG

#include <stdarg.h>

#include <glib.h>

#include <telepathy-glib/debug.h>

static TpDebugFlags flags = 0;

void
tp_debug_set_all_flags (void)
{
  flags = 0xffff;
}

static GDebugKey keys[] = {
  { "groups",        TP_DEBUG_GROUPS },
  { "properties",    TP_DEBUG_PROPERTIES },
  { "connection",    TP_DEBUG_CONNECTION },
  { 0, },
};

void tp_debug_set_flags_from_env (const char *var)
{
  guint nkeys;
  const gchar *flags_string;

  for (nkeys = 0; keys[nkeys].value; nkeys++);

  flags_string = g_getenv (var);

  if (flags_string)
    _tp_debug_set_flags (g_parse_debug_string (flags_string, keys, nkeys));
}

void _tp_debug_set_flags (TpDebugFlags new_flags)
{
  flags |= new_flags;
}

gboolean _tp_debug_flag_is_set (TpDebugFlags flag)
{
  return flag & flags;
}

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
tp_debug_set_flags_from_env (const char *var)
{
}

#endif /* !ENABLE_DEBUG */
