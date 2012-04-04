/*
 * sidecar.c — interface for connection sidecars
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

#include "gabble/sidecar.h"

GType
gabble_sidecar_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GabbleSidecarInterface),
      NULL,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "GabbleSidecar", &info, 0);
  }

  return type;
}

const gchar *
gabble_sidecar_get_interface (GabbleSidecar *sidecar)
{
  GabbleSidecarInterface *iface = GABBLE_SIDECAR_GET_INTERFACE (sidecar);

  return iface->interface;
}

/**
 * gabble_sidecar_get_immutable_properties:
 * @sidecar: a sidecar object
 *
 * Retrieves the immutable properties for a sidecar, as a map from
 * fully-qualified property names to GValues containing the properties' values
 * (such as that created with tp_asv_new ()).
 *
 * Returns: a non-%NULL hash of @sidecar's immutable properties, which must be
 *          unreffed but not modified by the caller.
 */
GHashTable *
gabble_sidecar_get_immutable_properties (GabbleSidecar *sidecar)
{
  GabbleSidecarInterface *iface = GABBLE_SIDECAR_GET_INTERFACE (sidecar);

  if (iface->get_immutable_properties)
    return iface->get_immutable_properties (sidecar);
  else
    return g_hash_table_new (NULL, NULL);
}
