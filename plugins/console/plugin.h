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

#include <glib-object.h>

typedef struct _GabbleConsolePlugin GabbleConsolePlugin;
typedef struct _GabbleConsolePluginClass GabbleConsolePluginClass;
typedef struct _GabbleConsolePluginPrivate GabbleConsolePluginPrivate;

struct _GabbleConsolePlugin {
    GObject parent;
    GabbleConsolePluginPrivate *priv;
};

struct _GabbleConsolePluginClass {
    GObjectClass parent;
};

GType gabble_console_plugin_get_type (void);

#define GABBLE_TYPE_CONSOLE_PLUGIN \
  (gabble_console_plugin_get_type ())
#define GABBLE_CONSOLE_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GABBLE_TYPE_CONSOLE_PLUGIN, \
                               GabbleConsolePlugin))
#define GABBLE_CONSOLE_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GABBLE_TYPE_CONSOLE_PLUGIN, \
                            GabbleConsolePluginClass))
#define GABBLE_IS_CONSOLE_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GABBLE_TYPE_CONSOLE_PLUGIN))
#define GABBLE_IS_CONSOLE_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GABBLE_TYPE_CONSOLE_PLUGIN))
#define GABBLE_CONSOLE_PLUGIN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CONSOLE_PLUGIN, \
                              GabbleConsolePluginClass))
