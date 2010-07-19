
#include "config.h"
#include "debug.h"

#include <stdarg.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#include <errno.h>

#include <glib/gstdio.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/debug-sender.h>

static GabbleDebugFlags flags = 0;

static GDebugKey keys[] = {
  { "presence",       GABBLE_DEBUG_PRESENCE },
  { "groups",         GABBLE_DEBUG_GROUPS },
  { "roster",         GABBLE_DEBUG_ROSTER },
  { "disco",          GABBLE_DEBUG_DISCO },
  { "properties",     GABBLE_DEBUG_PROPERTIES },
  { "roomlist",       GABBLE_DEBUG_ROOMLIST },
  { "media-channel",  GABBLE_DEBUG_MEDIA },
  { "im",             GABBLE_DEBUG_IM },
  { "muc",            GABBLE_DEBUG_MUC },
  { "connection",     GABBLE_DEBUG_CONNECTION },
  { "vcard",          GABBLE_DEBUG_VCARD },
  { "pipeline",       GABBLE_DEBUG_PIPELINE },
  { "jid",            GABBLE_DEBUG_JID },
  { "olpc",           GABBLE_DEBUG_OLPC },
  { "bytestream",     GABBLE_DEBUG_BYTESTREAM },
  { "tubes",          GABBLE_DEBUG_TUBES },
  { "location",       GABBLE_DEBUG_LOCATION },
  { "file-transfer",  GABBLE_DEBUG_FT },
  { "search",         GABBLE_DEBUG_SEARCH },
  { "base-channel",   GABBLE_DEBUG_BASE_CHANNEL },
  { "plugins",        GABBLE_DEBUG_PLUGINS },
  { "mail",           GABBLE_DEBUG_MAIL_NOTIF },
  { "authentication", GABBLE_DEBUG_AUTH },
  { "share",          GABBLE_DEBUG_SHARE },
  { "tls",            GABBLE_DEBUG_TLS },
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

GHashTable *flag_to_domains = NULL;

static const gchar *
debug_flag_to_domain (GabbleDebugFlags flag)
{
  if (G_UNLIKELY (flag_to_domains == NULL))
    {
      guint i;

      flag_to_domains = g_hash_table_new_full (g_direct_hash, g_direct_equal,
          NULL, g_free);

      for (i = 0; keys[i].value; i++)
        {
          GDebugKey key = (GDebugKey) keys[i];
          gchar *val;

          val = g_strdup_printf ("%s/%s", G_LOG_DOMAIN, key.key);
          g_hash_table_insert (flag_to_domains,
              GUINT_TO_POINTER (key.value), val);
        }
    }

  return g_hash_table_lookup (flag_to_domains, GUINT_TO_POINTER (flag));
}

void
gabble_debug_free (void)
{
  if (flag_to_domains == NULL)
    return;

  g_hash_table_destroy (flag_to_domains);
  flag_to_domains = NULL;
}

static void
log_to_debug_sender (GLogLevelFlags level,
    GabbleDebugFlags flag,
    const gchar *message)
{
  TpDebugSender *dbg;
  GTimeVal now;

  dbg = tp_debug_sender_dup ();

  g_get_current_time (&now);

  tp_debug_sender_add_message (dbg, &now, debug_flag_to_domain (flag),
      level, message);

  g_object_unref (dbg);
}

void gabble_log (GLogLevelFlags level,
    GabbleDebugFlags flag,
    const gchar *format,
    ...)
{
  gchar *message;
  va_list args;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  log_to_debug_sender (level, flag, message);

  if (flag & flags || level > G_LOG_LEVEL_DEBUG)
    g_log (G_LOG_DOMAIN, level, "%s", message);

  g_free (message);
}
