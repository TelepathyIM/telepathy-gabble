/*
 * caps-channel-manager.h - interface holding capabilities functions for
 * channel managers
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

#ifndef GABBLE_CAPS_CHANNEL_MANAGER_H
#define GABBLE_CAPS_CHANNEL_MANAGER_H

#include <glib-object.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/handle.h>

#include "capabilities.h"

G_BEGIN_DECLS

#define GABBLE_TYPE_CAPS_CHANNEL_MANAGER \
  (gabble_caps_channel_manager_get_type ())

#define GABBLE_CAPS_CHANNEL_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  GABBLE_TYPE_CAPS_CHANNEL_MANAGER, GabbleCapsChannelManager))

#define GABBLE_IS_CAPS_CHANNEL_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  GABBLE_TYPE_CAPS_CHANNEL_MANAGER))

#define GABBLE_CAPS_CHANNEL_MANAGER_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
  GABBLE_TYPE_CAPS_CHANNEL_MANAGER, GabbleCapsChannelManagerIface))

typedef struct _GabbleCapsChannelManager GabbleCapsChannelManager;
typedef struct _GabbleCapsChannelManagerIface GabbleCapsChannelManagerIface;


/* virtual methods */

typedef void (*GabbleCapsChannelManagerGetContactCapsFunc) (
    GabbleCapsChannelManager *manager,
    TpHandle handle,
    const GabbleCapabilitySet *caps,
    GPtrArray *arr);

typedef void (*GabbleCapsChannelManagerResetCapsFunc) (
    GabbleCapsChannelManager *manager);

typedef void (*GabbleCapsChannelManagerAddCapFunc) (
    GabbleCapsChannelManager *manager,
    GHashTable *cap,
    GabbleCapabilitySet *cap_set);

typedef void (*GabbleCapsChannelManagerRepresentClientFunc) (
    GabbleCapsChannelManager *manager,
    const gchar *client_name,
    const GPtrArray *filters,
    const gchar * const *cap_tokens,
    GabbleCapabilitySet *cap_set);

void gabble_caps_channel_manager_reset_capabilities (
    GabbleCapsChannelManager *caps_manager);

void gabble_caps_channel_manager_get_contact_capabilities (
    GabbleCapsChannelManager *caps_manager,
    TpHandle handle,
    const GabbleCapabilitySet *caps,
    GPtrArray *arr);

void gabble_caps_channel_manager_represent_client (
    GabbleCapsChannelManager *caps_manager,
    const gchar *client_name,
    const GPtrArray *filters,
    const gchar * const *cap_tokens,
    GabbleCapabilitySet *cap_set);

struct _GabbleCapsChannelManagerIface {
    GTypeInterface parent;

    GabbleCapsChannelManagerResetCapsFunc reset_caps;
    GabbleCapsChannelManagerGetContactCapsFunc get_contact_caps;
    GabbleCapsChannelManagerRepresentClientFunc represent_client;

    gpointer priv;
};

GType gabble_caps_channel_manager_get_type (void);

G_END_DECLS

#endif
