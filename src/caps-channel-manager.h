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
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/handle.h>

#include "types.h"

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

/* May be moved to TpChannelManager later */
typedef void (*GabbleCapsChannelManagerGetContactCapsFunc) (
    GabbleCapsChannelManager *manager, GabbleConnection *conn, TpHandle handle,
    GPtrArray *arr);

typedef void (*GabbleCapsChannelManagerAddCapFunc) (
    GabbleCapsChannelManager *manager, GabbleConnection *conn, TpHandle handle,
    GHashTable *cap);

/* Specific to Gabble */
typedef void (*GabbleCapsChannelManagerGetFeatureListFunc) (
    GabbleCapsChannelManager *manager, gpointer specific_caps,
    GSList **features);

typedef gpointer (*GabbleCapsChannelManagerParseCapsFunc) (
    GabbleCapsChannelManager *manager, LmMessageNode *children);

typedef void (*GabbleCapsChannelManagerFreeCapsFunc) (
    GabbleCapsChannelManager *manager, gpointer specific_caps);

typedef void (*GabbleCapsChannelManagerCopyCapsFunc) (
    GabbleCapsChannelManager *manager, gpointer *specific_caps_out,
    gpointer specific_caps_in);

typedef void (*GabbleCapsChannelManagerUpdateCapsFunc) (
    GabbleCapsChannelManager *manager, gpointer specific_caps_out,
    gpointer specific_caps_in);

typedef gboolean (*GabbleCapsChannelManagerCapsDiffFunc) (
    GabbleCapsChannelManager *manager, TpHandle handle,
    gpointer specific_old_caps, gpointer specific_new_caps);


void gabble_caps_channel_manager_get_contact_capabilities (
    GabbleCapsChannelManager *manager, GabbleConnection *conn, TpHandle handle,
    GPtrArray *arr);

void gabble_caps_channel_manager_get_feature_list (
    GabbleCapsChannelManager *manager, gpointer specific_caps,
    GSList **features);

gpointer gabble_caps_channel_manager_parse_capabilities (
    GabbleCapsChannelManager *manager, LmMessageNode *query_result);

void gabble_caps_channel_manager_free_capabilities (GabbleCapsChannelManager *manager,
    gpointer specific_caps);

void gabble_caps_channel_manager_copy_capabilities (GabbleCapsChannelManager *manager,
    gpointer *specific_caps_out, gpointer specific_caps_in);

void gabble_caps_channel_manager_update_capabilities (
    GabbleCapsChannelManager *manager, gpointer specific_caps_out,
    gpointer specific_caps_in);

gboolean gabble_caps_channel_manager_capabilities_diff (
    GabbleCapsChannelManager *manager, TpHandle handle,
    gpointer specific_old_caps, gpointer specific_new_caps);

void gabble_caps_channel_manager_add_capability (
    GabbleCapsChannelManager *manager, GabbleConnection *conn, TpHandle handle,
    GHashTable *cap);


struct _GabbleCapsChannelManagerIface {
    GTypeInterface parent;

    GabbleCapsChannelManagerGetContactCapsFunc get_contact_caps;
    GabbleCapsChannelManagerAddCapFunc add_cap;

    GabbleCapsChannelManagerGetFeatureListFunc get_feature_list;
    GabbleCapsChannelManagerParseCapsFunc parse_caps;
    GabbleCapsChannelManagerFreeCapsFunc free_caps;
    GabbleCapsChannelManagerCopyCapsFunc copy_caps;
    GabbleCapsChannelManagerUpdateCapsFunc update_caps;
    GabbleCapsChannelManagerCapsDiffFunc caps_diff;

    GCallback _future[8];
    gpointer priv;
};

GType gabble_caps_channel_manager_get_type (void);

G_END_DECLS

#endif
