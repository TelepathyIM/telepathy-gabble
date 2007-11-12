#include "config.h"

#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <telepathy-glib/debug.h>

#include "debug.h"

void
gabble_debug_set_log_file_from_env (void)
{
  const gchar *output_file;
  int out;

  output_file = g_getenv ("GABBLE_LOGFILE");
  if (output_file == NULL)
    return;

  out = g_open (output_file, O_WRONLY | O_CREAT, 0644);
  if (out == -1)
    {
      g_warning ("Can't open logfile '%s': %s", output_file,
          g_strerror (errno));
      return;
    }

  if (dup2 (out, STDOUT_FILENO) == -1)
    {
      g_warning ("Error when duplicating stdout file descriptor: %s",
          g_strerror (errno));
      return;
    }

  if (dup2 (out, STDERR_FILENO) == -1)
    {
      g_warning ("Error when duplicating stderr file descriptor: %s",
          g_strerror (errno));
      return;
    }
}

#ifdef ENABLE_DEBUG

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
  { "vcard",         GABBLE_DEBUG_VCARD },
  { "pipeline",      GABBLE_DEBUG_PIPELINE },
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

#ifdef HAVE_TP_DEBUG_SET_FLAGS
      tp_debug_set_flags (flags_string);
#else
      tp_debug_set_flags_from_string (flags_string);
#endif

  if (flags_string)
    {
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

#endif /* ENABLE_DEBUG */
