/*
 * bytestream-iface.c - Source for GabbleBytestream interface
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

#include "bytestream-iface.h"

#include <glib.h>

gboolean
gabble_bytestream_iface_initiate (GabbleBytestreamIface *self)
{
  gboolean (*virtual_method)(GabbleBytestreamIface *) =
    GABBLE_BYTESTREAM_IFACE_GET_CLASS (self)->initiate;
  g_assert (virtual_method != NULL);
  return virtual_method (self);
}

gboolean
gabble_bytestream_iface_send (GabbleBytestreamIface *self,
                              guint len,
                              const gchar *data)
{
  gboolean (*virtual_method)(GabbleBytestreamIface *, guint, const gchar *) =
    GABBLE_BYTESTREAM_IFACE_GET_CLASS (self)->send;
  g_assert (virtual_method != NULL);
  return virtual_method (self, len, data);
}

void
gabble_bytestream_iface_close (GabbleBytestreamIface *self)
{
  void (*virtual_method)(GabbleBytestreamIface *) =
    GABBLE_BYTESTREAM_IFACE_GET_CLASS (self)->close;
  g_assert (virtual_method != NULL);
  virtual_method (self);
}

void
gabble_bytestream_iface_accept (GabbleBytestreamIface *self,
                                LmMessage *msg)
{
  void (*virtual_method)(GabbleBytestreamIface *, LmMessage *) =
    GABBLE_BYTESTREAM_IFACE_GET_CLASS (self)->accept;
  g_assert (virtual_method != NULL);
  virtual_method (self, msg);
}

const gchar *
gabble_bytestream_iface_get_protocol (GabbleBytestreamIface *self)
{
  const gchar * (*virtual_method)(GabbleBytestreamIface *) =
    GABBLE_BYTESTREAM_IFACE_GET_CLASS (self)->get_protocol;
  g_assert (virtual_method != NULL);
  return virtual_method (self);
}

GType
gabble_bytestream_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GabbleBytestreamIfaceClass),
      NULL,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "GabbleBytestreamIface",
        &info, 0);
  }

  return type;
}
