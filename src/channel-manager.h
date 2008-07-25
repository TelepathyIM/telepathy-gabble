/*
 * channel-manager.h - factory and manager for channels relating to a
 *  particular protocol feature
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

#ifndef GABBLE_CHANNEL_MANAGER_H
#define GABBLE_CHANNEL_MANAGER_H

#include <glib-object.h>
#include <telepathy-glib/channel-factory-iface.h>

G_BEGIN_DECLS

#define GABBLE_TYPE_CHANNEL_MANAGER (gabble_channel_manager_get_type ())

#define GABBLE_CHANNEL_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  GABBLE_TYPE_CHANNEL_MANAGER, GabbleChannelManager))

#define GABBLE_IS_CHANNEL_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  GABBLE_TYPE_CHANNEL_MANAGER))

#define GABBLE_CHANNEL_MANAGER_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
  GABBLE_TYPE_CHANNEL_MANAGER, GabbleChannelManagerIface))

typedef struct _GabbleChannelManager GabbleChannelManager;
typedef struct _GabbleChannelManagerIface GabbleChannelManagerIface;

struct _GabbleChannelManagerIface {
    GTypeInterface parent;
};

GType gabble_channel_manager_get_type (void);

TpChannelFactoryRequestStatus gabble_channel_factory_create_channel (
    GabbleChannelManager *manager, GHashTable *properties,
    gpointer request_token, TpChannelIface **ret, GError **error);

void gabble_channel_manager_foreach (GabbleChannelManager *manager,
    TpChannelFunc func, gpointer user_data);

G_END_DECLS

#endif
