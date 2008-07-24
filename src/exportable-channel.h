/*
 * exportable-channel.h - A channel usable with the Channel Manager
 *
 * Copyright (C) 2008 Collabora Ltd.
 * Copyright (C) 2008 Nokia Corporation
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

#ifndef GABBLE_EXPORTABLE_CHANNEL_H
#define GABBLE_EXPORTABLE_CHANNEL_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GABBLE_TYPE_EXPORTABLE_CHANNEL (gabble_exportable_channel_get_type ())

#define GABBLE_EXPORTABLE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  GABBLE_TYPE_EXPORTABLE_CHANNEL, GabbleExportableChannel))

#define GABBLE_IS_EXPORTABLE_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  GABBLE_TYPE_EXPORTABLE_CHANNEL))

#define GABBLE_EXPORTABLE_CHANNEL_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
  GABBLE_TYPE_EXPORTABLE_CHANNEL, GabbleExportableChannelIface))

typedef struct _GabbleExportableChannel GabbleExportableChannel;
typedef struct _GabbleExportableChannelIface GabbleExportableChannelIface;

struct _GabbleExportableChannelIface {
    GTypeInterface parent;
};

GType gabble_exportable_channel_get_type (void);

G_END_DECLS

#endif
