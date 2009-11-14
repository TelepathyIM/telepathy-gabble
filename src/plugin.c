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

#include "plugin.h"

#include <telepathy-glib/errors.h>
#include <telepathy-glib/util.h>

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
gabble_plugin_create_sidecar (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GabblePluginInterface *iface = GABBLE_PLUGIN_GET_INTERFACE (plugin);

  if (!gabble_plugin_implements_sidecar (plugin, sidecar_interface))
    g_simple_async_report_error_in_idle (G_OBJECT (plugin), callback,
        user_data, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
        "Gabble is buggy: '%s' doesn't implement sidecar %s",
        iface->name, sidecar_interface);
  else if (iface->create_sidecar == NULL)
    g_simple_async_report_error_in_idle (G_OBJECT (plugin), callback,
        user_data, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
        "'%s' is buggy: it claims to implement %s, but does not implement "
        "create_sidecar", iface->name, sidecar_interface);
  else
    iface->create_sidecar (plugin, sidecar_interface, callback, user_data);
}

GabbleSidecar *
gabble_plugin_create_sidecar_finish (
    GabblePlugin *plugin,
    GAsyncResult *result,
    GError **error)
{
  GabbleSidecar *sidecar;

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error))
    return NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
    G_OBJECT (plugin), gabble_plugin_create_sidecar), NULL);

  sidecar = GABBLE_SIDECAR (g_simple_async_result_get_op_res_gpointer (
      G_SIMPLE_ASYNC_RESULT (result)));
  return g_object_ref (sidecar);
}
