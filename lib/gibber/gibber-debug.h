
#ifndef __DEBUG_H__
#define __DEBUG_H__

#include "config.h"

#include <glib.h>

#if 0
#include "gibber-xmpp-stanza.h"
#endif

#ifdef ENABLE_DEBUG

G_BEGIN_DECLS

typedef enum
{
  DEBUG_TRANSPORT         = 1 << 0,
  DEBUG_NET               = 1 << 1,
  DEBUG_XMPP_READER       = 1 << 2,
  DEBUG_XMPP_WRITER       = 1 << 3,
  DEBUG_SASL              = 1 << 4,
  DEBUG_SSL               = 1 << 5,
  DEBUG_RMULTICAST        = 1 << 6,
  DEBUG_RMULTICAST_SENDER = 1 << 7,
  DEBUG_MUC_CONNECTION    = 1 << 8,
  DEBUG_BYTESTREAM        = 1 << 9,
  DEBUG_FILE_TRANSFER     = 1 << 10,
} DebugFlags;

#define DEBUG_XMPP (DEBUG_XMPP_READER | DEBUG_XMPP_WRITER)

void gibber_debug_set_flags_from_env (void);
void gibber_debug_set_flags (DebugFlags flags);
gboolean gibber_debug_flag_is_set (DebugFlags flag);
void gibber_debug (DebugFlags flag, const gchar *format, ...)
    G_GNUC_PRINTF (2, 3);
#if 0
void gibber_debug_stanza (DebugFlags flag, GibberXmppStanza *stanza,
    const gchar *format, ...)
    G_GNUC_PRINTF (3, 4);
#endif

#define gibber_goto_if_reached(label) G_STMT_START{ \
    g_log (G_LOG_DOMAIN, \
        G_LOG_LEVEL_CRITICAL, \
        "file %s: line %d: should not be reached", \
        __FILE__, \
        __LINE__); \
    goto label; }G_STMT_END;

#define gibber_goto_if_fail(expr, label)  G_STMT_START{ \
    if (expr) {} \
    else \
      { \
        g_log (G_LOG_DOMAIN, \
            G_LOG_LEVEL_CRITICAL, \
            "file %s: line %d: assertion `%s' failed", \
            __FILE__, \
            __LINE__, \
            #expr); \
        goto label; \
      }; }G_STMT_END


#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  gibber_debug (DEBUG_FLAG, "%s: " format, G_STRFUNC, ##__VA_ARGS__)

#define DEBUG_STANZA(stanza, format, ...) \
  gibber_debug_stanza (DEBUG_FLAG, stanza, "%s: " format, G_STRFUNC,\
      ##__VA_ARGS__)

#define DEBUGGING debug_flag_is_set(DEBUG_FLAG)

#endif /* DEBUG_FLAG */

#else /* ENABLE_DEBUG */

#ifdef DEBUG_FLAG

#define DEBUG(format, ...)

#define DEBUG_STANZA(stanza, format, ...)

#define DEBUGGING 0

#define NODE_DEBUG(n, s)

#endif /* DEBUG_FLAG */

#endif /* ENABLE_DEBUG */

G_END_DECLS

#endif

