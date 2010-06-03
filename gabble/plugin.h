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

#ifndef __GABBLE_PLUGINS_PLUGIN_H__
#define __GABBLE_PLUGINS_PLUGIN_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <telepathy-glib/base-connection.h>
#include <wocky/wocky-session.h>

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
    TpBaseConnection *connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data);

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
     * An implementation of gabble_plugin_create_sidecar().
     */
    GabblePluginCreateSidecarImpl create_sidecar;

    /**
     * The plugin's version, conventionally a "."-separated sequence of
     * numbers.
     */
    const gchar *version;
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

void gabble_plugin_create_sidecar (
    GabblePlugin *plugin,
    const gchar *sidecar_interface,
    TpBaseConnection *connection,
    WockySession *session,
    GAsyncReadyCallback callback,
    gpointer user_data);

GabbleSidecar *gabble_plugin_create_sidecar_finish (
    GabblePlugin *plugin,
    GAsyncResult *result,
    GError **error);

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
