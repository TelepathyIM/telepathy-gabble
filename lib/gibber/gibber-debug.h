
#ifndef __DEBUG_H__
#define __DEBUG_H__

#include "config.h"

#include <glib.h>

#if 0
#include "gibber-xmpp-stanza.h"
#endif

G_BEGIN_DECLS

#ifdef ENABLE_DEBUG

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

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  G_STMT_START { \
  gibber_debug (DEBUG_FLAG, "%s: " format, G_STRFUNC, ##__VA_ARGS__); \
  } G_STMT_END

#define DEBUG_STANZA(stanza, format, ...) \
  G_STMT_START { \
  gibber_debug_stanza (DEBUG_FLAG, stanza, "%s: " format, G_STRFUNC,\
      ##__VA_ARGS__); \
  } G_STMT_END

#define DEBUGGING (debug_flag_is_set (DEBUG_FLAG))

#endif /* DEBUG_FLAG */

#else /* ENABLE_DEBUG */

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) G_STMT_START { } G_STMT_END

#define DEBUG_STANZA(stanza, format, ...) G_STMT_START { } G_STMT_END

#define DEBUGGING (0)

#endif /* DEBUG_FLAG */

#endif /* ENABLE_DEBUG */

G_END_DECLS

#endif
