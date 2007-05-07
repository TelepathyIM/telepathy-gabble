#include "config.h"

#ifdef ENABLE_DEBUG

#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <telepathy-glib/debug.h>

#include "debug.h"

static GabbleDebugFlags flags = 0;

static GDebugKey keys[] = {
  { "presence",      GABBLE_DEBUG_PRESENCE },
  { "groups",        GABBLE_DEBUG_GROUPS },
  { "roster",        GABBLE_DEBUG_ROSTER },
  { "disco",         GABBLE_DEBUG_DISCO },
  { "properties",    GABBLE_DEBUG_PROPERTIES },
  { "roomlist",      GABBLE_DEBUG_ROOMLIST },
  { "media-channel", GABBLE_DEBUG_MEDIA },
  { "im",            GABBLE_DEBUG_IM },
  { "muc",           GABBLE_DEBUG_MUC },
  { "connection",    GABBLE_DEBUG_CONNECTION },
  { "persist",       GABBLE_DEBUG_PERSIST },
  { "vcard",         GABBLE_DEBUG_VCARD },
  { "jid",           GABBLE_DEBUG_JID },
  { "olpc",          GABBLE_DEBUG_OLPC },
  { 0, },
};

void gabble_debug_set_flags_from_env ()
{
  guint nkeys;
  const gchar *flags_string;

  for (nkeys = 0; keys[nkeys].value; nkeys++);

  flags_string = g_getenv ("GABBLE_DEBUG");

  if (flags_string)
    {
      tp_debug_set_flags_from_env ("GABBLE_DEBUG");
      gabble_debug_set_flags (g_parse_debug_string (flags_string, keys,
            nkeys));
    }
}

void gabble_debug_set_flags (GabbleDebugFlags new_flags)
{
  flags |= new_flags;
}

gboolean gabble_debug_flag_is_set (GabbleDebugFlags flag)
{
  return flag & flags;
}

void gabble_debug (GabbleDebugFlags flag,
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

void
gabble_debug_set_output_from_env (void)
{
  const gchar *output_file;
  int out;

  output_file = g_getenv ("GABBLE_OUTPUT");
  if (output_file == NULL)
    return;

  out = g_open (output_file, O_WRONLY | O_CREAT, 0644);
  if (out == -1)
    {
      g_warning ("Can't use this file as output: %s\n", output_file);
      return;
    }

  close (STDOUT_FILENO);
  dup (out);

  close (STDERR_FILENO);
  dup (out);
}

#endif /* ENABLE_DEBUG */

