/* XML console plugin
 *
 * Copyright © 2011–2013 Collabora Ltd. <http://www.collabora.co.uk/>
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
#include <gabble/gabble.h>

typedef struct _GabbleConsoleChannelManager GabbleConsoleChannelManager;
typedef struct _GabbleConsoleChannelManagerClass GabbleConsoleChannelManagerClass;

struct _GabbleConsoleChannelManagerClass {
  GObjectClass parent_class;
};

struct _GabbleConsoleChannelManager {
  GObject parent;

  GabblePluginConnection *plugin_connection;
};

GType gabble_console_channel_manager_get_type (void);

#define GABBLE_TYPE_CONSOLE_CHANNEL_MANAGER \
  (gabble_console_channel_manager_get_type ())
#define GABBLE_CONSOLE_CHANNEL_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_CONSOLE_CHANNEL_MANAGER, GabbleConsoleChannelManager))
#define GABBLE_CONSOLE_CHANNEL_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GABBLE_TYPE_CONSOLE_CHANNEL_MANAGER,\
                           GabbleConsoleChannelManagerClass))
#define GABBLE_IS_CONSOLE_CHANNEL_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_CONSOLE_CHANNEL_MANAGER))
#define GABBLE_IS_CONSOLE_CHANNEL_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GABBLE_TYPE_CONSOLE_CHANNEL_MANAGER))
#define GABBLE_CONSOLE_CHANNEL_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GABBLE_TYPE_CONSOLE_CHANNEL_MANAGER,\
                              GabbleConsoleChannelManagerClass))
