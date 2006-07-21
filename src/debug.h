
#ifndef __DEBUG_H__
#define __DEBUG_H_

#include <glib.h>

G_BEGIN_DECLS

enum
{
  GABBLE_DEBUG_PRESENCE = 1 << 0,
  GABBLE_DEBUG_GROUPS   = 1 << 1,
  GABBLE_DEBUG_ROSTER   = 1 << 2,
  GABBLE_DEBUG_DISCO    = 1 << 3,
};

#define GABBLE_DEBUG_ALL 0xffffffff

void gabble_debug_set_flags_from_env ();
void gabble_debug_set_flags (guint flags);
gboolean gabble_debug_flag_is_set (guint flag);
void gabble_debug (guint flag, const gchar *format, ...);

#ifdef DEBUG_FLAG
#define DEBUG(format, ...) \
  gabble_debug(DEBUG_FLAG, format, __VA_ARGS__)

#define IF_DEBUG if (gabble_debug_flag_is_set (DEBUG_FLAG))
#endif

G_END_DECLS

#endif

