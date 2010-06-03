/*
 * sidecar.h — sidecar API availbale to telepathy-gabble plugins
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
#ifndef __SIDECAR_H__
#define __SIDECAR_H__

#include <glib-object.h>

#define GABBLE_TYPE_SIDECAR (gabble_sidecar_get_type ())
#define GABBLE_SIDECAR(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GABBLE_TYPE_SIDECAR, GabbleSidecar))
#define GABBLE_IS_SIDECAR(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GABBLE_TYPE_SIDECAR))
#define GABBLE_SIDECAR_GET_INTERFACE(obj) \
    (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GABBLE_TYPE_SIDECAR, \
        GabbleSidecarInterface))

typedef struct _GabbleSidecar GabbleSidecar;
typedef struct _GabbleSidecarInterface GabbleSidecarInterface;

typedef GHashTable * (*GabbleSidecarGetImmutablePropertiesImpl) (
    GabbleSidecar *);

struct _GabbleSidecarInterface {
    GTypeInterface parent;

    /**
     * The D-Bus interface implemented by this sidecar.
     */
    const gchar *interface;

    /**
     * An implementation of gabble_sidecar_get_immutable_properties().
     */
    GabbleSidecarGetImmutablePropertiesImpl get_immutable_properties;
};

GType gabble_sidecar_get_type (void);

const gchar *gabble_sidecar_get_interface (GabbleSidecar *sidecar);
GHashTable *gabble_sidecar_get_immutable_properties (GabbleSidecar *sidecar);

#endif
