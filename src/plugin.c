/*
 * plugin.c — API for telepathy-gabble plugins
 * Copyright © 2009 Collabora Ltd.
 * Copyright © 2009 Nokia Corporation
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

#include "gabble/plugin.h"

#include <telepathy-glib/errors.h>
#include <telepathy-glib/presence-mixin.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG GABBLE_DEBUG_PLUGINS
#include "debug.h"

GType
gabble_plugin_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GabblePluginInterface),
      NULL,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "GabblePlugin", &info, 0);
  }

  return type;
}

const gchar *
gabble_plugin_get_name (GabblePlugin *plugin)
{
  GabblePluginInterface *iface = GABBLE_PLUGIN_GET_INTERFACE (plugin);

  return iface->name;
}

const gchar *
gabble_plugin_get_version (GabblePlugin *plugin)
{
  GabblePluginInterface *iface = GABBLE_PLUGIN_GET_INTERFACE (plugin);

  return iface->version;
}

const gchar * const *
gabble_plugin_get_sidecar_interfaces (GabblePlugin *plugin)
{
  GabblePluginInterface *iface = GABBLE_PLUGIN_GET_INTERFACE (plugin);

  return iface->sidecar_interfaces;
}

gboolean
gabble_plugin_implements_sidecar (
    GabblePlugin *plugin,
    const gchar *sidecar_interface)
{
  GabblePluginInterface *iface = GABBLE_PLUGIN_GET_INTERFACE (plugin);

  return tp_strv_contains (iface->sidecar_interfaces, sidecar_interface);
}

/**
 * gabble_plugin_create_sidecar:
 * @plugin: a plugin
 * @sidecar_interface: the primary D-Bus interface implemented by the sidecar,
 *                     which must be a member of the list returned by
 *                     gabble_plugin_get_sidecar_interfaces ()
 * @callback: function to call when the new sidecar has been created, or an
 *            unrecoverable error has occured
 * @user_data: data to pass to @callback
 */
void
gabble_plugin_create_sidecar_async (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    GabblePluginConnection *plugin_connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabblePluginInterface *iface = GABBLE_PLUGIN_GET_INTERFACE (plugin);

  if (!gabble_plugin_implements_sidecar (plugin, sidecar_interface))
    g_simple_async_report_error_in_idle (G_OBJECT (plugin), callback,
        user_data, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
        "Gabble is buggy: '%s' doesn't implement sidecar %s",
        iface->name, sidecar_interface);
  else if (iface->create_sidecar_async == NULL)
    g_simple_async_report_error_in_idle (G_OBJECT (plugin), callback,
        user_data, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
        "'%s' is buggy: it claims to implement %s, but does not implement "
        "create_sidecar_async", iface->name, sidecar_interface);
  else if (iface->create_sidecar_finish == NULL)
    g_simple_async_report_error_in_idle (G_OBJECT (plugin), callback,
        user_data, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
        "'%s' is buggy: does not imlement create_sidecar_finish",
        iface->name);
  else
    iface->create_sidecar_async (plugin, sidecar_interface, plugin_connection, session,
        callback, user_data);
}

GabbleSidecar *
gabble_plugin_create_sidecar_finish (
    GabblePlugin *plugin,
    GAsyncResult *result,
    GError **error)
{
  GabblePluginInterface *iface = GABBLE_PLUGIN_GET_INTERFACE (plugin);
  GabbleSidecar *sidecar;

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error))
    return NULL;

  if (iface->create_sidecar_finish == NULL) {
    WARNING ("'%s' is buggy: does not implement create_sidecar_finish", iface->name);
    return NULL;
  }

  sidecar = iface->create_sidecar_finish (plugin, result, error);

  return g_object_ref (sidecar);
}

const TpPresenceStatusSpec *
gabble_plugin_get_custom_presence_statuses (
    GabblePlugin *plugin)
{
  GabblePluginInterface *iface = GABBLE_PLUGIN_GET_INTERFACE (plugin);

  return iface->presence_statuses;
}

gboolean
gabble_plugin_implements_presence_status (
    GabblePlugin *plugin,
    const gchar *status)
{
  GabblePluginInterface *iface = GABBLE_PLUGIN_GET_INTERFACE (plugin);
  gint i;

  if (iface->presence_statuses == NULL)
    return FALSE;

  for (i = 0; iface->presence_statuses[i].name; i++)
    {
      if (!tp_strdiff (status, iface->presence_statuses[i].name))
        return TRUE;
    }

  return FALSE;
}

const gchar *
gabble_plugin_presence_status_for_privacy_list (
    GabblePlugin *plugin,
    const gchar *list_name)
{
  GabblePluginInterface *iface = GABBLE_PLUGIN_GET_INTERFACE (plugin);
  int i;

  if (iface->privacy_list_map == NULL)
    return NULL;

  for (i = 0; iface->privacy_list_map[i].privacy_list_name; i++)
    {
      if (!tp_strdiff (list_name,
              iface->privacy_list_map[i].privacy_list_name))
        {
          DEBUG ("Plugin %s links presence %s with privacy list %s",
            iface->name,
            iface->privacy_list_map[i].privacy_list_name,
            iface->privacy_list_map[i].presence_status_name);

          return iface->privacy_list_map[i].presence_status_name;
        }
    }

  DEBUG ("No plugins link presence to privacy list %s",
      list_name);

  return NULL;
}

GPtrArray *
gabble_plugin_create_channel_managers (GabblePlugin *plugin,
    GabblePluginConnection *plugin_connection)
{
  GabblePluginInterface *iface = GABBLE_PLUGIN_GET_INTERFACE (plugin);
  GabblePluginCreateChannelManagersImpl func = iface->create_channel_managers;
  GPtrArray *out = NULL;

  if (func != NULL)
    out = func (plugin, plugin_connection);

  return out;
}
