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
