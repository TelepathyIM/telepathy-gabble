/*
 * plugin-connection.c — API for telepathy-gabble plugins
 * Copyright © 2012 Collabora Ltd.
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

#include "gabble/plugin-connection.h"

#include <glib-object.h>
#include <gabble/types.h>
#include <telepathy-glib/errors.h>
#include <debug.h>


static guint sig_id_porter_available = 0;

/**
 * SECTION: gabble-plugin-connection
 * @title: GabblePluginConnection
 * @short_description: Object representing gabble connection, implemented by
 *    Gabble internals.
 *
 * This Object represents Gabble Connection.
 *
 * Virtual methods in GabblePluginConnectionInterface interface are implemented
 * by GabbleConnection object. And only Gabble should implement this interface.
 */
G_DEFINE_INTERFACE (GabblePluginConnection,
    gabble_plugin_connection,
    G_TYPE_OBJECT);

static void
gabble_plugin_connection_default_init (GabblePluginConnectionInterface *iface)
{
  static gsize once = 0;

  if (g_once_init_enter (&once))
    {
      /**
       * @self: a connection interface
       * @porter: a porter
       *
       * Emitted when the WockyPorter becomes available.
       */
      sig_id_porter_available = g_signal_new (
          "porter-available",
          G_OBJECT_CLASS_TYPE (iface),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, 0, NULL, NULL,
          g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, WOCKY_TYPE_PORTER);
      g_once_init_leave (&once, 1);
    }
}

gchar *
gabble_plugin_connection_add_sidecar_own_caps (
    GabblePluginConnection *plugin_connection,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities)
{
  GabblePluginConnectionInterface *iface =
    GABBLE_PLUGIN_CONNECTION_GET_IFACE (plugin_connection);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->add_sidecar_own_caps != NULL, NULL);
  
  return iface->add_sidecar_own_caps (plugin_connection, cap_set, identities);
}

gchar *
gabble_plugin_connection_add_sidecar_own_caps_full (
    GabblePluginConnection *plugin_connection,
    const GabbleCapabilitySet *cap_set,
    const GPtrArray *identities,
    GPtrArray *data_forms)
{
  GabblePluginConnectionInterface *iface =
    GABBLE_PLUGIN_CONNECTION_GET_IFACE (plugin_connection);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->add_sidecar_own_caps_full != NULL, NULL);
  
  return iface->add_sidecar_own_caps_full (plugin_connection, cap_set, identities,
      data_forms);
}

WockySession *
gabble_plugin_connection_get_session (
    GabblePluginConnection *plugin_connection)
{
  GabblePluginConnectionInterface *iface =
    GABBLE_PLUGIN_CONNECTION_GET_IFACE (plugin_connection);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_session != NULL, NULL);

  return iface->get_session (plugin_connection);
}

gchar *
gabble_plugin_connection_get_full_jid (
    GabblePluginConnection *plugin_connection)
{
  GabblePluginConnectionInterface *iface =
    GABBLE_PLUGIN_CONNECTION_GET_IFACE (plugin_connection);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_full_jid != NULL, NULL);

  return iface->get_full_jid (plugin_connection);
}

const gchar *
gabble_plugin_connection_get_jid_for_caps (
    GabblePluginConnection *plugin_connection,
    WockyXep0115Capabilities *caps)
{
  GabblePluginConnectionInterface *iface =
    GABBLE_PLUGIN_CONNECTION_GET_IFACE (plugin_connection);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_jid_for_caps != NULL, NULL);

  return iface->get_jid_for_caps (plugin_connection, caps);
}

const gchar *
gabble_plugin_connection_pick_best_resource_for_caps (
    GabblePluginConnection *plugin_connection,
    const gchar *jid,
    GabbleCapabilitySetPredicate predicate,
    gconstpointer user_data)
{
  GabblePluginConnectionInterface *iface =
    GABBLE_PLUGIN_CONNECTION_GET_IFACE (plugin_connection);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->pick_best_resource_for_caps != NULL, NULL);

  return iface->pick_best_resource_for_caps (plugin_connection, jid, predicate,
      user_data);
}

TpBaseContactList *
gabble_plugin_connection_get_contact_list (
    GabblePluginConnection *plugin_connection)
{
  GabblePluginConnectionInterface *iface =
    GABBLE_PLUGIN_CONNECTION_GET_IFACE (plugin_connection);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_contact_list != NULL, NULL);

  return iface->get_contact_list (plugin_connection);
}

WockyXep0115Capabilities *
gabble_plugin_connection_get_caps (
    GabblePluginConnection *plugin_connection,
    TpHandle handle)
{
  GabblePluginConnectionInterface *iface =
    GABBLE_PLUGIN_CONNECTION_GET_IFACE (plugin_connection);

  g_return_val_if_fail (iface != NULL, NULL);
  g_return_val_if_fail (iface->get_contact_list != NULL, NULL);

  return iface->get_caps (plugin_connection, handle);
}
