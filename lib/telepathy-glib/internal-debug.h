#ifndef __DEBUG_H__
#define __DEBUG_H_

#include "config.h"

#ifdef ENABLE_DEBUG

#include <glib.h>

#include <telepathy-glib/debug.h>

G_BEGIN_DECLS

typedef enum
{
  TP_DEBUG_GROUPS        = 1 << 1,
  TP_DEBUG_PROPERTIES    = 1 << 2,
  TP_DEBUG_IM            = 1 << 3,
  TP_DEBUG_CONNECTION    = 1 << 4,
} TpDebugFlags;

gboolean _tp_debug_flag_is_set (TpDebugFlags flag);
void _tp_debug_set_flags (TpDebugFlags flags);
void _tp_debug (TpDebugFlags flag, const gchar *format, ...)
    G_GNUC_PRINTF (2, 3);

G_END_DECLS

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  _tp_debug(DEBUG_FLAG, "%s: " format, G_STRFUNC, ##__VA_ARGS__)

#define DEBUGGING _tp_debug_flag_is_set(DEBUG_FLAG)

#endif /* DEBUG_FLAG */

#else /* ENABLE_DEBUG */

#ifdef DEBUG_FLAG

#define DEBUG(format, ...)

#define DEBUGGING 0

#define NODE_DEBUG(n, s)

#endif /* DEBUG_FLAG */

#endif /* ENABLE_DEBUG */

#endif /* __DEBUG_H__ */
