/*
 * tube-iface.h - Header for GabbleTube interface
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __GABBLE_TUBE_IFACE_H__
#define __GABBLE_TUBE_IFACE_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GabbleTubeIface GabbleTubeIface;
typedef struct _GabbleTubeIfaceClass GabbleTubeIfaceClass;

struct _GabbleTubeIfaceClass {
  GTypeInterface parent;

  gchar * (*get_stream_id) (GabbleTubeIface *tube);
  void (*accept) (GabbleTubeIface *tube);
  void (*close) (GabbleTubeIface *tube);
};

GType gabble_tube_iface_get_type (void);

/* TYPE MACROS */
#define GABBLE_TYPE_TUBE_IFACE \
  (gabble_tube_iface_get_type ())
#define GABBLE_TUBE_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GABBLE_TYPE_TUBE_IFACE, GabbleTubeIface))
#define GABBLE_IS_TUBE_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GABBLE_TYPE_TUBE_IFACE))
#define GABBLE_TUBE_IFACE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GABBLE_TYPE_TUBE_IFACE,\
                              GabbleTubeIfaceClass))

gchar *
gabble_tube_iface_get_stream_id (GabbleTubeIface *tube);

void
gabble_tube_iface_accept (GabbleTubeIface *tube);

void
gabble_tube_iface_close (GabbleTubeIface *tube);

G_END_DECLS

#endif /* #ifndef __GABBLE_TUBE_IFACE_H__ */
