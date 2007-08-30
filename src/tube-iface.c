/*
 * tube-iface.c - Source for GabbleTube interface
 * Copyright (C) 2007 Ltd.
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

#include "tube-iface.h"

#include <glib.h>

void
gabble_tube_iface_accept (GabbleTubeIface *self)
{
  void (*virtual_method)(GabbleTubeIface *) =
    GABBLE_TUBE_IFACE_GET_CLASS (self)->accept;
  g_assert (virtual_method != NULL);
  virtual_method (self);
}

void
gabble_tube_iface_close (GabbleTubeIface *self)
{
  void (*virtual_method)(GabbleTubeIface *) =
    GABBLE_TUBE_IFACE_GET_CLASS (self)->close;
  g_assert (virtual_method != NULL);
  virtual_method (self);
}

void
gabble_tube_iface_add_bytestream (GabbleTubeIface *self,
                                  GabbleBytestreamIface *bytestream)
{
  void (*virtual_method)(GabbleTubeIface *, GabbleBytestreamIface *) =
    GABBLE_TUBE_IFACE_GET_CLASS (self)->add_bytestream;
  g_assert (virtual_method != NULL);
  virtual_method (self, bytestream);
}

GType
gabble_tube_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GabbleTubeIfaceClass),
      NULL,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "GabbleTubeIface",
        &info, 0);
  }

  return type;
}
