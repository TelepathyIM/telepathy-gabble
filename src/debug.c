
#include "config.h"
#include "debug.h"

#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <glib/gstdio.h>
#include <telepathy-glib/debug.h>

#include "debugger.h"

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
  { "bytestream",    GABBLE_DEBUG_BYTESTREAM },
  { "tubes",         GABBLE_DEBUG_TUBES },
  { "location",      GABBLE_DEBUG_LOCATION },
  { "file-transfer", GABBLE_DEBUG_FT },
  { 0, },
};

void gabble_debug_set_flags_from_env ()
{
  guint nkeys;
  const gchar *flags_string;

  for (nkeys = 0; keys[nkeys].value; nkeys++);

  flags_string = g_getenv ("GABBLE_DEBUG");

  tp_debug_set_flags (flags_string);

  if (flags_string != NULL)
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

static const gchar *
debug_flag_to_key (GabbleDebugFlags flag)
{
  guint i;

  for (i = 0; keys[i].value; i++)
    {
      GDebugKey key = (GDebugKey) keys[i];
      if (key.value == flag)
	return key.key;
    }

  return NULL;
}

static void
log_to_debugger (GabbleDebugFlags flag,
		 const gchar *format,
		 va_list args)
{
  GabbleDebugger *dbg = gabble_debugger_get_singleton ();
  gchar *domain, *message = NULL;
  GTimeVal now;

  g_get_current_time (&now);

  domain = g_strdup_printf ("%s/%s", G_LOG_DOMAIN, debug_flag_to_key (flag));
  message = g_strdup_vprintf (format, args);

  gabble_debugger_add_message (dbg, &now, domain, G_LOG_LEVEL_DEBUG, message);

  g_free (message);
  g_free (domain);
}

void gabble_debug (GabbleDebugFlags flag,
                   const gchar *format,
                   ...)
{
  va_list args;

  va_start (args, format);

  log_to_debugger (flag, format, args);

  if (flag & flags)
    g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);

  va_end (args);
}

#endif /* ENABLE_DEBUG */
