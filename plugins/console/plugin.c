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

#include "config.h"
#include "console/plugin.h"

#include <telepathy-glib/telepathy-glib.h>
#include <wocky/wocky.h>
#include <gabble/gabble.h>
#include "extensions/extensions.h"

#include "console/channel-manager.h"
#include "console/debug.h"
#include "console/sidecar.h"

static void plugin_iface_init (
    gpointer g_iface,
    gpointer data);

static const gchar * const sidecar_interfaces[] = {
    GABBLE_IFACE_GABBLE_PLUGIN_CONSOLE,
    NULL
};

G_DEFINE_TYPE_WITH_CODE (GabbleConsolePlugin, gabble_console_plugin,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_PLUGIN, plugin_iface_init);
    )

static void
gabble_console_plugin_init (GabbleConsolePlugin *self)
{
}

static void
gabble_console_plugin_class_init (GabbleConsolePluginClass *klass)
{
}

static GPtrArray *
gabble_console_plugin_create_channel_managers (GabblePlugin *plugin,
    GabblePluginConnection *plugin_connection)
{
  GPtrArray *ret = g_ptr_array_new ();

  g_ptr_array_add (ret,
      g_object_new (GABBLE_TYPE_CONSOLE_CHANNEL_MANAGER,
          "plugin-connection", plugin_connection,
          NULL));

  return ret;
}

static void
plugin_iface_init (
    gpointer g_iface,
    gpointer data G_GNUC_UNUSED)
{
  GabblePluginInterface *iface = g_iface;

  iface->name = "XMPP console";
  iface->version = PACKAGE_VERSION;
  iface->create_channel_managers = gabble_console_plugin_create_channel_managers;
}

GabblePlugin *
gabble_plugin_create (void)
{
  gabble_console_debug_init ();

  DEBUG ("loaded");

  return g_object_new (GABBLE_TYPE_CONSOLE_PLUGIN,
      NULL);
}
