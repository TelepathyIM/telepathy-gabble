/* XML console plugin
 *
 * Copyright © 2011 Collabora Ltd. <http://www.collabora.co.uk/>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <glib.h>

#include "console/debug.h"

int debug = 0;

static const GDebugKey debug_keys[] = {
      { "console", 1 },
      { NULL, 0 }
};

void
gabble_console_debug_init (void)
{
  debug = g_parse_debug_string (g_getenv ("GABBLE_DEBUG"), debug_keys,
      G_N_ELEMENTS (debug_keys) - 1);
}
