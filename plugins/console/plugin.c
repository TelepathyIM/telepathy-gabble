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

static void
gabble_console_plugin_create_sidecar_async (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    GabblePluginConnection *connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result = g_simple_async_result_new (G_OBJECT (plugin),
      callback, user_data,
      gabble_console_plugin_create_sidecar_async);
  GabbleSidecar *sidecar = NULL;

  if (!tp_strdiff (sidecar_interface, GABBLE_IFACE_GABBLE_PLUGIN_CONSOLE))
    {
      sidecar = g_object_new (GABBLE_TYPE_CONSOLE_SIDECAR,
          "connection", connection,
          "session", session,
          NULL);
    }
  else
    {
      g_simple_async_result_set_error (result, TP_ERROR,
          TP_ERROR_NOT_IMPLEMENTED, "'%s' not implemented", sidecar_interface);
    }

  if (sidecar != NULL)
    g_simple_async_result_set_op_res_gpointer (result, sidecar,
        g_object_unref);

  g_simple_async_result_complete_in_idle (result);
  g_object_unref (result);
}

static GabbleSidecar *
gabble_console_plugin_create_sidecar_finish (
    GabblePlugin *plugin,
    GAsyncResult *result,
    GError **error)
{
  GabbleSidecar *sidecar;

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
        error))
    return NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
        G_OBJECT (plugin), gabble_console_plugin_create_sidecar_async), NULL);

  sidecar = GABBLE_SIDECAR (g_simple_async_result_get_op_res_gpointer (
        G_SIMPLE_ASYNC_RESULT (result)));

  return g_object_ref (sidecar);
}

static void
plugin_iface_init (
    gpointer g_iface,
    gpointer data G_GNUC_UNUSED)
{
  GabblePluginInterface *iface = g_iface;

  iface->name = "XMPP console";
  iface->version = PACKAGE_VERSION;
  iface->sidecar_interfaces = sidecar_interfaces;
  iface->create_sidecar_async = gabble_console_plugin_create_sidecar_async;
  iface->create_sidecar_finish = gabble_console_plugin_create_sidecar_finish;
}

GabblePlugin *
gabble_plugin_create (void)
{
  gabble_console_debug_init ();

  DEBUG ("loaded");

  return g_object_new (GABBLE_TYPE_CONSOLE_PLUGIN,
      NULL);
}
