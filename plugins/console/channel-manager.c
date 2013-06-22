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

#include "config.h"
#include "console/channel-manager.h"

#include "extensions/extensions.h"

static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleConsoleChannelManager, gabble_console_channel_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER, channel_manager_iface_init)
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER, NULL);
    )

enum {
    PROP_CONNECTION = 1,
};

static void
gabble_console_channel_manager_init (GabbleConsoleChannelManager *self)
{
}

static void
gabble_console_channel_manager_set_property (
    GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GabbleConsoleChannelManager *self = GABBLE_CONSOLE_CHANNEL_MANAGER (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        /* Not reffing this: the connection owns all channel managers, so it
         * must outlive us. Taking a reference leads to a cycle.
         */
        self->plugin_connection = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gabble_console_channel_manager_get_property (
    GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  GabbleConsoleChannelManager *self = GABBLE_CONSOLE_CHANNEL_MANAGER (object);

  switch (property_id)
    {
      case PROP_CONNECTION:
        g_value_set_object (value, self->plugin_connection);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
gabble_console_channel_manager_class_init (GabbleConsoleChannelManagerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->set_property = gabble_console_channel_manager_set_property;
  oclass->get_property = gabble_console_channel_manager_get_property;

  g_object_class_install_property (oclass, PROP_CONNECTION,
      g_param_spec_object ("plugin-connection", "Gabble Plugin Connection",
          "Gabble Plugin Connection",
          GABBLE_TYPE_PLUGIN_CONNECTION,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
gabble_console_channel_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, GABBLE_IFACE_GABBLE_PLUGIN_CONSOLE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_NONE,
      NULL);

  func (type, table, NULL, user_data);

  g_hash_table_unref (table);
}

static void
channel_manager_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->type_foreach_channel_class = gabble_console_channel_manager_type_foreach_channel_class;

  /* not requestable. */
  iface->ensure_channel = NULL;
  iface->create_channel = NULL;
  iface->request_channel = NULL;
}
