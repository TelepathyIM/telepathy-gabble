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
#include "console/channel.h"

static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (GabbleConsoleChannelManager, gabble_console_channel_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER, channel_manager_iface_init)
    G_IMPLEMENT_INTERFACE (GABBLE_TYPE_CAPS_CHANNEL_MANAGER, NULL);
    )

enum {
    PROP_CONNECTION = 1,
};

static void connection_status_changed_cb (
    TpBaseConnection *conn,
    guint status,
    guint reason,
    GabbleConsoleChannelManager *self);
static void gabble_console_channel_manager_close_all (
    GabbleConsoleChannelManager *self);

static void
gabble_console_channel_manager_init (GabbleConsoleChannelManager *self)
{
  g_weak_ref_init (&self->plugin_connection_ref, NULL);
}


static void
gabble_console_channel_manager_constructed (GObject *object)
{
  GabbleConsoleChannelManager *self = GABBLE_CONSOLE_CHANNEL_MANAGER (object);
  GabblePluginConnection *plugin_connection;

  G_OBJECT_CLASS (gabble_console_channel_manager_parent_class)->constructed (object);

  plugin_connection = g_weak_ref_get (&self->plugin_connection_ref);
  if (plugin_connection != NULL)
    {
      g_signal_connect_object (plugin_connection, "status-changed",
          G_CALLBACK (connection_status_changed_cb), self, 0);
      g_object_unref (plugin_connection);
    }
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
        g_weak_ref_set (&self->plugin_connection_ref, g_value_get_object (value));
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
        g_value_take_object (value, g_weak_ref_get (&self->plugin_connection_ref));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}


static void
gabble_console_channel_manager_dispose (
    GObject *object)
{
  GabbleConsoleChannelManager *self = GABBLE_CONSOLE_CHANNEL_MANAGER (object);

  gabble_console_channel_manager_close_all (self);
  g_weak_ref_clear (&self->plugin_connection_ref);

  G_OBJECT_CLASS (gabble_console_channel_manager_parent_class)->dispose (object);
}


static void
connection_status_changed_cb (
    TpBaseConnection *conn,
    guint status,
    guint reason,
    GabbleConsoleChannelManager *self)
{
  switch (status)
    {
      case TP_CONNECTION_STATUS_DISCONNECTED:
        gabble_console_channel_manager_close_all (self);
        break;

      default:
        return;
    }
}

static void
gabble_console_channel_manager_close_all (
    GabbleConsoleChannelManager *self)
{
  TpBaseChannel *channel;

  while ((channel = g_queue_peek_head (&self->console_channels)) != NULL)
    {
      tp_base_channel_close (channel);
    }
}


static void
gabble_console_channel_manager_class_init (GabbleConsoleChannelManagerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->constructed = gabble_console_channel_manager_constructed;
  oclass->set_property = gabble_console_channel_manager_set_property;
  oclass->get_property = gabble_console_channel_manager_get_property;
  oclass->dispose = gabble_console_channel_manager_dispose;

  g_object_class_install_property (oclass, PROP_CONNECTION,
      g_param_spec_object ("plugin-connection", "Gabble Plugin Connection",
          "Gabble Plugin Connection",
          GABBLE_TYPE_PLUGIN_CONNECTION,
          G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}


static const gchar * const allowed[] = {
    TP_PROP_CHANNEL_CHANNEL_TYPE,
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
    NULL
};

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
console_channel_closed_cb (
    GabbleConsoleChannel *channel,
    gpointer user_data)
{
  GabbleConsoleChannelManager *self = GABBLE_CONSOLE_CHANNEL_MANAGER (user_data);

  tp_channel_manager_emit_channel_closed_for_object (self,
      TP_EXPORTABLE_CHANNEL (channel));

  if (g_queue_remove (&self->console_channels, channel))
    {
      g_object_unref (channel);
    }
}


static gboolean
gabble_console_channel_manager_create_channel (
    TpChannelManager *manager,
    gpointer request_token,
    GHashTable *request_properties)
{
  GabbleConsoleChannelManager *self = GABBLE_CONSOLE_CHANNEL_MANAGER (manager);
  GabblePluginConnection *connection;
  TpBaseChannel *channel = NULL;
  GError *error = NULL;
  GSList *request_tokens;

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"),
        GABBLE_IFACE_GABBLE_PLUGIN_CONSOLE))
    return FALSE;

  if (tp_asv_get_uint32 (request_properties,
       TP_IFACE_CHANNEL ".TargetHandleType", NULL) != 0)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
          "Console channels can't have a target handle");
      goto error;
    }

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          allowed,
          allowed,
          &error))
    goto error;

  connection = g_weak_ref_get (&self->plugin_connection_ref);
  g_return_val_if_fail (connection != NULL, FALSE);

  channel = g_object_new (GABBLE_TYPE_CONSOLE_CHANNEL,
      "connection", connection,
      NULL);
  tp_base_channel_register (channel);
  g_signal_connect (channel, "closed", (GCallback) console_channel_closed_cb,
      self);
  g_queue_push_tail (&self->console_channels, channel);

  request_tokens = g_slist_prepend (NULL, request_token);
  tp_channel_manager_emit_new_channel (self,
      TP_EXPORTABLE_CHANNEL (channel), request_tokens);
  g_slist_free (request_tokens);

  g_object_unref (connection);
  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}


static void
channel_manager_iface_init (gpointer g_iface,
    gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->type_foreach_channel_class = gabble_console_channel_manager_type_foreach_channel_class;
  iface->create_channel = gabble_console_channel_manager_create_channel;
}
