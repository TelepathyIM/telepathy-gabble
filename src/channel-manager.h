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
#include <loudmouth/loudmouth.h>
#include <telepathy-glib/handle.h>

#include "exportable-channel.h"
#include "types.h"

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


/* virtual methods */

/* TpChannelManager (GetContactCapabilities) */
typedef void (*GabbleChannelManagerGetContactCapsFunc) (
    GabbleChannelManager *manager, GabbleConnection *conn, TpHandle handle,
    GPtrArray *arr);

/* Gabble specific: send our caps, replying to disco request from contacts */
typedef void (*GabbleChannelManagerGetFeatureListFunc) (
    GabbleChannelManager *manager, gpointer specific_caps, GSList **features);

/* Gabble specific: parse caps stanza from contacts */
typedef gpointer (*GabbleChannelManagerParseCapsFunc) (
    GabbleChannelManager *manager, LmMessageNode *children);

/* Gabble specific */
typedef void (*GabbleChannelManagerFreeCapsFunc) (
    GabbleChannelManager *manager, gpointer specific_caps);

/* Gabble specific */
typedef void (*GabbleChannelManagerCopyCapsFunc) (
    GabbleChannelManager *manager, gpointer *specific_caps_out,
    gpointer specific_caps_in);

/* Gabble specific (merging resource caps) */
typedef void (*GabbleChannelManagerUpdateCapsFunc) (
    GabbleChannelManager *manager, gpointer *specific_caps_out, gpointer specific_caps_in);

/* Gabble specific */
typedef gboolean (*GabbleChannelManagerCapsDiffFunc) (
    GabbleChannelManager *manager, TpHandle handle, gpointer specific_old_caps,
    gpointer specific_new_caps);

/* TpChannelManager (SetSelfCapabilities) */
typedef void (*GabbleChannelManagerAddCapFunc) (
    GabbleChannelManager *manager, GabbleConnection *conn, TpHandle handle,
    GHashTable *cap);


void gabble_channel_manager_get_contact_capabilities (
    GabbleChannelManager *manager, GabbleConnection *conn, TpHandle handle,
    GPtrArray *arr);

void gabble_channel_manager_get_feature_list (
    GabbleChannelManager *manager, gpointer specific_caps, GSList **features);

gpointer gabble_channel_manager_parse_capabilities (
    GabbleChannelManager *manager, LmMessageNode *children);

void gabble_channel_manager_free_capabilities (GabbleChannelManager *manager,
    gpointer specific_caps);

void gabble_channel_manager_copy_capabilities (GabbleChannelManager *manager,
    gpointer *specific_caps_out, gpointer specific_caps_in);

void gabble_channel_manager_update_capabilities (
    GabbleChannelManager *manager, gpointer specific_caps_out,
    gpointer specific_caps_in);

gboolean gabble_channel_manager_capabilities_diff (
    GabbleChannelManager *manager, TpHandle handle, gpointer specific_old_caps,
    gpointer specific_new_caps);

void gabble_channel_manager_add_capability (
    GabbleChannelManager *manager, GabbleConnection *conn, TpHandle handle,
    GHashTable *cap);


typedef void (*GabbleChannelManagerForeachChannelFunc) (
    GabbleChannelManager *manager, GabbleExportableChannelFunc func,
    gpointer user_data);

void gabble_channel_manager_foreach_channel (GabbleChannelManager *manager,
    GabbleExportableChannelFunc func, gpointer user_data);


typedef void (*GabbleChannelManagerChannelClassFunc) (
    GabbleChannelManager *manager,
    GHashTable *fixed_properties,
    const gchar * const *required_properties,
    const gchar * const *optional_properties,
    gpointer user_data);

typedef void (*GabbleChannelManagerForeachChannelClassFunc) (
    GabbleChannelManager *manager, GabbleChannelManagerChannelClassFunc func,
    gpointer user_data);

void gabble_channel_manager_foreach_channel_class (
    GabbleChannelManager *manager,
    GabbleChannelManagerChannelClassFunc func, gpointer user_data);


typedef gboolean (*GabbleChannelManagerRequestFunc) (
    GabbleChannelManager *manager, gpointer request_token,
    GHashTable *request_properties);

gboolean gabble_channel_manager_create_channel (GabbleChannelManager *manager,
    gpointer request_token, GHashTable *request_properties);

gboolean gabble_channel_manager_request_channel (GabbleChannelManager *manager,
    gpointer request_token, GHashTable *request_properties);


struct _GabbleChannelManagerIface {
    GTypeInterface parent;

    GabbleChannelManagerGetContactCapsFunc get_contact_caps;
    GabbleChannelManagerGetFeatureListFunc get_feature_list;
    GabbleChannelManagerParseCapsFunc parse_caps;
    GabbleChannelManagerFreeCapsFunc free_caps;
    GabbleChannelManagerCopyCapsFunc copy_caps;
    GabbleChannelManagerUpdateCapsFunc update_caps;
    GabbleChannelManagerCapsDiffFunc caps_diff;
    GabbleChannelManagerAddCapFunc add_cap;

    GabbleChannelManagerForeachChannelFunc foreach_channel;

    GabbleChannelManagerForeachChannelClassFunc foreach_channel_class;

    GabbleChannelManagerRequestFunc create_channel;
    GabbleChannelManagerRequestFunc request_channel;
    /* in principle we could have EnsureChannel here too */

    GCallback _future[8];
    gpointer priv;
};


GType gabble_channel_manager_get_type (void);


/* signal emission */

void gabble_channel_manager_emit_new_channel (gpointer instance,
    GabbleExportableChannel *channel, GSList *requests);
void gabble_channel_manager_emit_new_channels (gpointer instance,
    GHashTable *channels);

void gabble_channel_manager_emit_channel_closed (gpointer instance,
    const gchar *path);
void gabble_channel_manager_emit_channel_closed_for_object (gpointer instance,
    GabbleExportableChannel *channel);

void gabble_channel_manager_emit_request_already_satisfied (
    gpointer instance, gpointer request_token,
    GabbleExportableChannel *channel);

void gabble_channel_manager_emit_request_failed (gpointer instance,
    gpointer request_token, GQuark domain, gint code, const gchar *message);
void gabble_channel_manager_emit_request_failed_printf (gpointer instance,
    gpointer request_token, GQuark domain, gint code, const gchar *format,
    ...) G_GNUC_PRINTF (5, 6);

G_END_DECLS

#endif
