#include <glib.h>
#include "telepathy-errors.h"

GQuark
telepathy_errors_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("telepathy_errors");
  return quark;
}
