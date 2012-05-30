/*
 * plugin.h — plugin API for telepathy-gabble plugins
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

#ifndef GABBLE_PLUGINS_PLUGIN_H
#define GABBLE_PLUGINS_PLUGIN_H

#include <glib-object.h>
#include <gio/gio.h>

#include <telepathy-glib/telepathy-glib.h>
#include <wocky/wocky.h>

#include <gabble/plugin-connection.h>
#include <gabble/sidecar.h>
#include <gabble/types.h>

G_BEGIN_DECLS

#define GABBLE_TYPE_PLUGIN (gabble_plugin_get_type ())
#define GABBLE_PLUGIN(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GABBLE_TYPE_PLUGIN, GabblePlugin))
#define GABBLE_IS_PLUGIN(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GABBLE_TYPE_PLUGIN))
#define GABBLE_PLUGIN_GET_INTERFACE(obj) \
    (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GABBLE_TYPE_PLUGIN, \
        GabblePluginInterface))

typedef struct _GabblePluginInterface GabblePluginInterface;

typedef void (*GabblePluginCreateSidecarImpl) (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    GabblePluginConnection *plugin_connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data);

/* The return type should be a new GPtrArray* which will be freed
 * straight after this function is called, so the pointer array must
 * not have a free function. */
typedef GPtrArray * (*GabblePluginCreateChannelManagersImpl) (
    GabblePlugin *plugin,
    GabblePluginConnection *plugin_connection);

typedef GabbleSidecar * (*GabblePluginCreateSidecarFinishImpl) (
     GabblePlugin *plugin,
     GAsyncResult *result,
     GError **error);

struct _GabblePluginPrivacyListMap {
    const gchar *presence_status_name;
    const gchar *privacy_list_name;
};
typedef struct _GabblePluginPrivacyListMap GabblePluginPrivacyListMap;

struct _GabblePluginInterface {
    GTypeInterface parent;

    /**
     * An arbitrary human-readable name identifying this plugin.
     */
    const gchar *name;

    /**
     * A %NULL-terminated array of strings listing the sidecar D-Bus interfaces
     * implemented by this plugin.
     */
    const gchar * const *sidecar_interfaces;

    /**
     * An implementation of gabble_plugin_create_sidecar_async().
     */
    GabblePluginCreateSidecarImpl create_sidecar_async;

    /**
     * An implementation of gabble_plugin_create_sidecar_finish().
     */
    GabblePluginCreateSidecarFinishImpl create_sidecar_finish;

    /**
     * The plugin's version, conventionally a "."-separated sequence of
     * numbers.
     */
    const gchar *version;

    /**
     * Additional custom statuses supported by the plugin.
     */
    TpPresenceStatusSpec *presence_statuses;

    /**
     * Privacy lists implementing specific statuses
     */
    GabblePluginPrivacyListMap *privacy_list_map;

    /**
     * An optional callback to create additional channel managers.
     */
    GabblePluginCreateChannelManagersImpl create_channel_managers;
};

GType gabble_plugin_get_type (void);

const gchar *gabble_plugin_get_name (
    GabblePlugin *plugin);
const gchar *gabble_plugin_get_version (
    GabblePlugin *plugin);
const gchar * const *gabble_plugin_get_sidecar_interfaces (
    GabblePlugin *plugin);

gboolean gabble_plugin_implements_sidecar (
    GabblePlugin *plugin,
    const gchar *sidecar_interface);

void gabble_plugin_create_sidecar_async (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    GabblePluginConnection *plugin_connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data);

GabbleSidecar *gabble_plugin_create_sidecar_finish (
    GabblePlugin *plugin,
    GAsyncResult *result,
    GError **error);

const TpPresenceStatusSpec *gabble_plugin_get_custom_presence_statuses (
    GabblePlugin *plugin);

gboolean gabble_plugin_implements_presence_status (
    GabblePlugin *plugin,
    const gchar *status);

const gchar *gabble_plugin_presence_status_for_privacy_list (
    GabblePlugin *plugin,
    const gchar *list_name);

GPtrArray * gabble_plugin_create_channel_managers (GabblePlugin *plugin,
    GabblePluginConnection *plugin_connection);

/**
 * gabble_plugin_create:
 *
 * Prototype for the plugin entry point.
 *
 * Returns: a new instance of this plugin, which must not be %NULL.
 */
GabblePlugin *gabble_plugin_create (void);

typedef GabblePlugin *(*GabblePluginCreateImpl) (void);

G_END_DECLS

#endif
