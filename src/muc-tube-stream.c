/*
 * muc-tube-stream.c - Source for GabbleMucTubeStream
 * Copyright (C) 2012 Collabora Ltd.
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

#include "muc-tube-stream.h"

G_DEFINE_TYPE (GabbleMucTubeStream, gabble_muc_tube_stream,
    GABBLE_TYPE_TUBE_STREAM)

static const gchar *gabble_muc_tube_stream_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_GROUP,
    TP_IFACE_CHANNEL_INTERFACE_TUBE,
    NULL
};

static void
gabble_muc_tube_stream_init (GabbleMucTubeStream *self)
{
}

static void
gabble_muc_tube_stream_class_init (
    GabbleMucTubeStreamClass *gabble_muc_tube_stream_class)
{
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (
      gabble_muc_tube_stream_class);

  base_class->interfaces = gabble_muc_tube_stream_interfaces;
  base_class->target_handle_type = TP_HANDLE_TYPE_ROOM;
}
